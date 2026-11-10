/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "brookesia/system_super/macro_configs.h"
#if !BROOKESIA_SYSTEM_SUPER_ENABLE_DEBUG_LOG
#   define BROOKESIA_LOG_DISABLE_DEBUG_TRACE 1
#endif

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <ctime>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

#include "brookesia/lib_utils/describe_helpers.hpp"
#include "brookesia/service_helper/network/sntp.hpp"
#include "brookesia/service_helper/network/wifi.hpp"
#include "brookesia/service_helper/system/device.hpp"
#include "boost/json.hpp"
#include "soc/soc_caps.h"
#if SOC_USB_SERIAL_JTAG_SUPPORTED
#   include "driver/usb_serial_jtag.h"
#endif
#include "private/shell_app.hpp"
#include "private/system_constants.hpp"
#include "private/utils.hpp"

namespace esp_brookesia::system::super {
namespace {

using SNTPHelper = service::helper::SNTP;
using WifiHelper = service::helper::Wifi;
using DeviceHelper = service::helper::Device;

struct WifiStatusState {
    bool visible = false;
    bool connected = false;
};

WifiStatusState get_wifi_status_from_state(std::string_view state)
{
    if (state.empty()) {
        return {};
    }
    if (state == BROOKESIA_DESCRIBE_TO_STR(WifiHelper::GeneralState::Connected)) {
        return {
            .visible = true,
            .connected = true,
        };
    }
    // Any other valid state means the Wi-Fi service is alive but not
    // connected. Keep the icon visible so the status bar can show the
    // "disconnected" glyph instead of silently hiding Wi-Fi.
    return {
        .visible = true,
        .connected = false,
    };
}

WifiStatusState get_wifi_status_from_event(std::string_view event)
{
    if (event == BROOKESIA_DESCRIBE_TO_STR(WifiHelper::GeneralEvent::Connected)) {
        return {
            .visible = true,
            .connected = true,
        };
    }
    return {
        .visible = true,
        .connected = false,
    };
}

int signal_dbm_to_level(int dbm)
{
    if (dbm > -55) {
        return 4;
    }
    if (dbm > -65) {
        return 3;
    }
    if (dbm > -75) {
        return 2;
    }
    return 1;
}

std::string bool_to_binding(bool value)
{
    return value ? "true" : "false";
}

std::string make_clock_text()
{
    const auto now = std::chrono::system_clock::now();
    const auto time = std::chrono::system_clock::to_time_t(now);
    std::tm local_time{};
#if defined(_WIN32)
    localtime_s(&local_time, &time);
#else
    localtime_r(&time, &local_time);
#endif
    char buffer[8] = {};
    if (std::strftime(buffer, sizeof(buffer), "%H:%M", &local_time) == 0) {
        return "--:--";
    }
    return buffer;
}

} // namespace

bool ShellApp::ensure_wifi_service_binding()
{
    if (wifi_service_binding_.is_valid()) {
        if (WifiHelper::is_running()) {
            return true;
        }
        ++wifi_status_generation_;
        set_status_wifi_state(false, false);
        return false;
    }
    if (!WifiHelper::is_available()) {
        ++wifi_status_generation_;
        set_status_wifi_state(false, false);
        return false;
    }
    wifi_service_binding_ = service::ServiceManager::get_instance().bind(WifiHelper::get_name().data());
    if (!wifi_service_binding_.is_valid()) {
        BROOKESIA_LOGW("Failed to bind Wi-Fi service for Shell status");
        ++wifi_status_generation_;
        set_status_wifi_state(false, false);
        return false;
    }
    if (!WifiHelper::is_running()) {
        ++wifi_status_generation_;
        wifi_service_binding_.release();
        set_status_wifi_state(false, false);
        return false;
    }

    if (!wifi_event_connection_.connected()) {
        const auto general_callback = [this](const std::string &, const std::string & event, bool unexpected) {
            if (unexpected) {
                BROOKESIA_LOGW("Wi-Fi service reported unexpected event for Shell status: %1%", event);
            }
            ++wifi_status_generation_;
            const auto status = get_wifi_status_from_event(event);
            set_status_wifi_state(status.visible, status.connected);
            if (status.connected) {
                refresh_wifi_signal();
            }
        };
        wifi_event_connection_ = WifiHelper::subscribe_event(
                                     WifiHelper::EventId::GeneralEventHappened,
                                     general_callback
                                 );
        if (!wifi_event_connection_.connected()) {
            BROOKESIA_LOGW("Failed to subscribe Wi-Fi general event for Shell status");
        }
    }
    return true;
}

void ShellApp::release_wifi_service_binding()
{
    ++wifi_status_generation_;
    wifi_event_connection_.disconnect();
    wifi_service_binding_.release();
    release_wifi_signal_service_binding();
    wifi_connected_ = false;
    wifi_status_visible_ = false;
    wifi_signal_level_ = 0;
    wifi_status_refresh_tick_ = 0;
    set_status_wifi_state(false, false);
}

bool ShellApp::ensure_wifi_signal_service_binding()
{
    if (wifi_signal_service_binding_.is_valid()) {
        return true;
    }
    if (!DeviceHelper::is_available()) {
        return false;
    }
    wifi_signal_service_binding_ = service::ServiceManager::get_instance().bind(DeviceHelper::get_name().data());
    if (!wifi_signal_service_binding_.is_valid()) {
        BROOKESIA_LOGW("Failed to bind Device service for Shell Wi-Fi signal");
        return false;
    }
    return true;
}

void ShellApp::release_wifi_signal_service_binding()
{
    ++wifi_signal_generation_;
    wifi_signal_service_binding_.release();
}

void ShellApp::refresh_wifi_signal()
{
    if (context_ == nullptr || !ensure_wifi_signal_service_binding()) {
        return;
    }

    const auto generation = ++wifi_signal_generation_;
    const auto handler = [this, generation](service::FunctionResult && result) {
        if (context_ == nullptr || generation != wifi_signal_generation_ ||
                !wifi_signal_service_binding_.is_valid()) {
            return;
        }
        if (!result.success || !result.has_data()) {
            return;
        }
        auto &json_value = result.get_data<boost::json::array>();
        DeviceHelper::NetworkConnectivityInfos infos;
        if (!BROOKESIA_DESCRIBE_FROM_JSON(json_value, infos)) {
            return;
        }
        for (const auto &info : infos) {
            if (info.status.interface_type != hal::network::InterfaceType::WifiStation) {
                continue;
            }
            if (!info.status.signal_dbm.has_value()) {
                continue;
            }
            set_status_wifi_bars(signal_dbm_to_level(*info.status.signal_dbm));
            break;
        }
    };
    if (!DeviceHelper::call_function_async(DeviceHelper::FunctionId::GetNetworkConnectivityInfo, handler)) {
        BROOKESIA_LOGW("Failed to submit Wi-Fi signal request for Shell status");
    }
}

void ShellApp::refresh_wifi_status()
{
    if (!ensure_wifi_service_binding()) {
        return;
    }

    const auto generation = ++wifi_status_generation_;
    const auto state_handler = [this, generation](service::FunctionResult && result) {
        if (context_ == nullptr || generation != wifi_status_generation_ || !wifi_service_binding_.is_valid()) {
            return;
        }
        if (!result.success || !result.has_data()) {
            set_status_wifi_state(true, false);
            return;
        }
        const auto &state = result.get_data<std::string>();
        const auto status = get_wifi_status_from_state(state);
        set_status_wifi_state(status.visible, status.connected);
        if (status.connected) {
            refresh_wifi_signal();
        }
    };
    if (!WifiHelper::call_function_async(WifiHelper::FunctionId::GetGeneralState, state_handler)) {
        BROOKESIA_LOGW("Failed to submit Wi-Fi state request for Shell status");
        set_status_wifi_state(true, false);
    }
}

void ShellApp::set_status_wifi_state(bool visible, bool connected)
{
    wifi_status_visible_ = visible;
    wifi_connected_ = connected;
    if (!visible) {
        wifi_signal_level_ = 0;
    } else if (connected && wifi_signal_level_ <= 0) {
        // Unknown RSSI right after connecting: show the full icon until the
        // connectivity service reports a value.
        wifi_signal_level_ = 4;
    } else if (!connected) {
        wifi_signal_level_ = 0;
    }
    if (context_ == nullptr) {
        return;
    }
    std::vector<gui::BindingValueUpdate> updates;
    updates.push_back(gui::BindingValueUpdate{
        .absolute_path = SUPER_STATUS_WIFI_PATH,
        .key = "wifi_hidden",
        .value = bool_to_binding(!visible),
    });
    updates.push_back(gui::BindingValueUpdate{
        .absolute_path = std::string(SUPER_STATUS_WIFI_PATH) + "/wifi_off_slash",
        .key = "wifi_off_hidden",
        .value = bool_to_binding(connected),
    });
    // Keep the familiar ascending bar glyph in both states: green bars up to
    // the current RSSI level when connected, and the full muted stack behind
    // the red slash when disconnected.
    const std::string_view bar_color = connected ? "${color.success.fill}" : "${color.text.muted}";
    for (int level = 1; level <= 4; ++level) {
        const auto bar_path = std::string(SUPER_STATUS_WIFI_PATH) + "/wifi_bar_" + std::to_string(level);
        updates.push_back(gui::BindingValueUpdate{
            .absolute_path = bar_path,
            .key = "wifi_bar" + std::to_string(level) + "_hidden",
            .value = bool_to_binding(connected && wifi_signal_level_ < level),
        });
        updates.push_back(gui::BindingValueUpdate{
            .absolute_path = bar_path,
            .key = "wifi_bar_color",
            .value = std::string(bar_color),
        });
    }
    auto result = context_->gui().set_binding_values(updates);
    if (!result) {
        BROOKESIA_LOGW("Failed to refresh Shell Wi-Fi status icon: %1%", result.error());
    }
}

void ShellApp::set_status_wifi_bars(int level)
{
    if (!wifi_status_visible_ || !wifi_connected_) {
        return;
    }
    level = std::clamp(level, 1, 4);
    if (level == wifi_signal_level_) {
        return;
    }
    wifi_signal_level_ = level;
    if (context_ == nullptr) {
        return;
    }
    std::vector<gui::BindingValueUpdate> updates;
    for (int bar = 1; bar <= 4; ++bar) {
        updates.push_back(gui::BindingValueUpdate{
            .absolute_path = std::string(SUPER_STATUS_WIFI_PATH) + "/wifi_bar_" + std::to_string(bar),
            .key = "wifi_bar" + std::to_string(bar) + "_hidden",
            .value = bool_to_binding(bar > level),
        });
    }
    auto result = context_->gui().set_binding_values(updates);
    if (!result) {
        BROOKESIA_LOGW("Failed to refresh Shell Wi-Fi signal bars: %1%", result.error());
    }
}

void ShellApp::set_status_app_icon(const std::optional<core::AppInfo> &app)
{
    (void)app;
    refresh_status_app_icons();
}

void ShellApp::refresh_status_app_icons()
{
    if (context_ == nullptr) {
        return;
    }

    // Collect the apps that are alive in the background (paused) so the status
    // bar can show what is still running behind the Shell.
    std::vector<core::AppInfo> background_apps;
    for (const auto &info : owner_.list_apps()) {
        if (info.app_id == owner_.shell_app_id_ || info.state != core::AppState::Paused) {
            continue;
        }
        if (!core::has_app_icon_image(info.manifest)) {
            continue;
        }
        background_apps.push_back(info);
    }

    // Most recently used first.
    auto recency_rank = [this](core::AppId app_id) {
        auto it = std::find(recent_apps_.begin(), recent_apps_.end(), app_id);
        return (it == recent_apps_.end()) ? std::numeric_limits<size_t>::max()
               : static_cast<size_t>(std::distance(recent_apps_.begin(), it));
    };
    std::sort(
        background_apps.begin(), background_apps.end(), [&recency_rank](const auto & lhs, const auto & rhs) {
        return recency_rank(lhs.app_id) < recency_rank(rhs.app_id);
    }
    );

    constexpr const char *icon_paths[SUPER_STATUS_BACKGROUND_APP_SLOTS] = {
        SUPER_STATUS_APP_ICON_PATH,
        SUPER_STATUS_APP_ICON_PATH_1,
        SUPER_STATUS_APP_ICON_PATH_2,
    };

    std::vector<gui::BindingValueUpdate> updates;
    for (size_t i = 0; i < SUPER_STATUS_BACKGROUND_APP_SLOTS; ++i) {
        const bool show = i < background_apps.size();
        status_app_ids_[i] = show ? background_apps[i].app_id : core::INVALID_APP_ID;
        updates.push_back(gui::BindingValueUpdate{
            .absolute_path = icon_paths[i],
            .key = "app_icon_hidden",
            .value = bool_to_binding(!show),
        });
        if (show) {
            updates.push_back(gui::BindingValueUpdate{
                .absolute_path = icon_paths[i],
                .key = "app_icon_src",
                .value = core::resolve_app_icon_resource_id(background_apps[i].manifest),
            });
        }
    }
    auto result = context_->gui().set_binding_values(updates);
    if (!result) {
        BROOKESIA_LOGW("Failed to refresh Shell background app icons: %1%", result.error());
    }
}

void ShellApp::activate_status_background_app(size_t slot)
{
    if (slot >= SUPER_STATUS_BACKGROUND_APP_SLOTS) {
        return;
    }
    const auto app_id = status_app_ids_[slot];
    if (app_id == core::INVALID_APP_ID) {
        return;
    }
    auto result = owner_.activate_app(app_id, core::System::AppStartOptions{});
    if (!result) {
        BROOKESIA_LOGW(
            "Failed to bring background app to the foreground: app_id(%1%), error(%2%)", app_id, result.error()
        );
        refresh_status_app_icons();
    }
}

void ShellApp::refresh_battery_status()
{
    if (context_ == nullptr) {
        return;
    }

    // This board exposes the battery connector to the charger only; there is
    // no battery voltage or charger STAT route to the P4. Report the power
    // source we can actually detect instead of inventing a percentage.
    bool usb_connected = false;
#if SOC_USB_SERIAL_JTAG_SUPPORTED
    usb_connected = usb_serial_jtag_is_connected();
#endif
    const auto environment = owner_.get_environment();
    const bool portrait = environment.height_px > environment.width_px;
    const auto icon_color = usb_connected ? "${color.success.fill}" : "${color.text.muted}";

    std::vector<gui::BindingValueUpdate> updates;
    updates.push_back(gui::BindingValueUpdate{
        .absolute_path = SUPER_STATUS_BATTERY_PATH,
        .key = "battery_hidden",
        .value = bool_to_binding(portrait),
    });
    updates.push_back(gui::BindingValueUpdate{
        .absolute_path = SUPER_STATUS_BATTERY_TEXT_PATH,
        .key = "battery_text",
        .value = usb_connected ? "USB" : "BATTERY",
    });
    updates.push_back(gui::BindingValueUpdate{
        .absolute_path = SUPER_STATUS_BATTERY_TEXT_PATH,
        .key = "battery_text_color",
        .value = icon_color,
    });
    const auto fill_path = std::string(SUPER_STATUS_BATTERY_PATH) +
                           "/battery_icon/battery_body/battery_fill";
    updates.push_back(gui::BindingValueUpdate{
        .absolute_path = fill_path,
        .key = "battery_color",
        .value = icon_color,
    });
    updates.push_back(gui::BindingValueUpdate{
        .absolute_path = std::string(SUPER_STATUS_BATTERY_PATH) + "/battery_icon/battery_tip",
        .key = "battery_color",
        .value = icon_color,
    });

    auto result = context_->gui().set_binding_values(updates);
    if (!result) {
        BROOKESIA_LOGW("Failed to refresh Shell battery status: %1%", result.error());
    }
}

bool ShellApp::ensure_sntp_service_binding()
{
    if (sntp_service_binding_.is_valid()) {
        return true;
    }
    if (!SNTPHelper::is_available()) {
        BROOKESIA_LOGD("SNTP service is not available for Shell status clock updates");
        return false;
    }

    sntp_service_binding_ = service::ServiceManager::get_instance().bind(SNTPHelper::get_name().data());
    if (!sntp_service_binding_.is_valid()) {
        BROOKESIA_LOGW("Failed to bind SNTP service for Shell status clock updates");
        return false;
    }
    return true;
}

void ShellApp::release_sntp_service_binding()
{
    disconnect_sntp_events();
    sntp_service_binding_.release();
}

void ShellApp::subscribe_sntp_events()
{
    disconnect_sntp_events();
    if (!ensure_sntp_service_binding()) {
        return;
    }

    const auto timezone_callback = [this](const std::string &, const service::EventItemMap &) {
        BROOKESIA_LOGI("SNTP timezone changed, refresh Shell status clock");
        refresh_status_clock();
        stop_status_clock_timer();
        schedule_status_clock_timer();
    };
    sntp_event_connection_ = SNTPHelper::subscribe_event(
                                 SNTPHelper::EventId::TimezoneChanged,
                                 timezone_callback
                             );
    if (!sntp_event_connection_.connected()) {
        BROOKESIA_LOGW("Failed to subscribe SNTP timezone event for Shell status clock");
    }
}

void ShellApp::disconnect_sntp_events()
{
    sntp_event_connection_.disconnect();
}

void ShellApp::refresh_status_clock()
{
    if (context_ == nullptr) {
        return;
    }
    auto result = context_->gui().set_binding_value(SUPER_STATUS_CLOCK_PATH, "clock_text", make_clock_text());
    if (!result) {
        BROOKESIA_LOGW("Failed to refresh Shell status clock: %1%", result.error());
    }
}

void ShellApp::schedule_status_clock_timer()
{
    if (context_ == nullptr || status_clock_timer_id_ != core::INVALID_TIMER_ID) {
        return;
    }
    auto timer = context_->timer().start_delayed(SUPER_STATUS_CLOCK_TIMER_NAME, 1000);
    if (!timer) {
        BROOKESIA_LOGW("Failed to start Shell status clock timer: %1%", timer.error());
        return;
    }
    status_clock_timer_id_ = *timer;
}

void ShellApp::stop_status_clock_timer()
{
    if (context_ != nullptr && status_clock_timer_id_ != core::INVALID_TIMER_ID) {
        (void)context_->timer().stop(status_clock_timer_id_);
    }
    status_clock_timer_id_ = core::INVALID_TIMER_ID;
}

} // namespace esp_brookesia::system::super
