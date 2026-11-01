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
#include <array>
#include <chrono>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "private/shell_app.hpp"
#include "private/system_constants.hpp"
#include "private/utils.hpp"

namespace esp_brookesia::system::super {
namespace {

inline constexpr size_t RECENTS_MAX_ENTRIES = 8;
inline constexpr int32_t RECENTS_CARD_HEIGHT_DP = 96;
inline constexpr int32_t RECENTS_CARD_GAP_DP = 12;

using SteadyClock = std::chrono::steady_clock;
using SteadyTimePoint = SteadyClock::time_point;

std::string make_recents_instance_id(core::AppId app_id)
{
    return std::string(SUPER_RECENTS_INSTANCE_PREFIX) + std::to_string(app_id);
}

std::string make_recents_item_path(core::AppId app_id)
{
    return std::string(SUPER_RECENTS_ITEM_LAYER_PATH) + "/" + make_recents_instance_id(app_id);
}

std::optional<std::string> get_recents_instance_id(const gui::Event &event)
{
    if (!event.path.starts_with(SUPER_RECENTS_PATH_PREFIX)) {
        return std::nullopt;
    }
    auto instance_id = event.path.substr(std::string_view(SUPER_RECENTS_PATH_PREFIX).size());
    auto child_separator = instance_id.find('/');
    if (child_separator != std::string::npos) {
        instance_id.resize(child_separator);
    }
    if (instance_id.empty()) {
        return std::nullopt;
    }
    return std::string(SUPER_RECENTS_INSTANCE_PREFIX) + instance_id;
}

} // namespace

void ShellApp::remember_recent_app(core::AppId app_id)
{
    if (app_id == core::INVALID_APP_ID) {
        return;
    }
    recent_apps_.erase(std::remove(recent_apps_.begin(), recent_apps_.end(), app_id), recent_apps_.end());
    recent_apps_.insert(recent_apps_.begin(), app_id);
    if (recent_apps_.size() > RECENTS_MAX_ENTRIES) {
        recent_apps_.resize(RECENTS_MAX_ENTRIES);
    }
}

void ShellApp::remove_recent_app(core::AppId app_id)
{
    recent_apps_.erase(std::remove(recent_apps_.begin(), recent_apps_.end(), app_id), recent_apps_.end());
}

void ShellApp::disconnect_recents_actions()
{
    recents_action_connections_.clear();
}

std::expected<void, std::string> ShellApp::refresh_recents()
{
    if (context_ == nullptr) {
        return {};
    }
    return populate_recents(*context_);
}

std::expected<void, std::string> ShellApp::populate_recents(core::AppContext &context)
{
    disconnect_recents_actions();
    for (const auto &[instance_id, app_id] : recents_instance_to_app_) {
        (void)instance_id;
        (void)context.gui().destroy_view(make_recents_item_path(app_id));
    }
    recents_instance_to_app_.clear();
    recents_populated_ = false;

    // Drop entries whose apps are gone or no longer visible.
    recent_apps_.erase(
        std::remove_if(
            recent_apps_.begin(),
            recent_apps_.end(),
    [this](core::AppId app_id) {
        auto app = owner_.get_app(app_id);
        return !app.has_value() || !app->manifest.visible;
    }
        ),
        recent_apps_.end()
    );

    std::vector<gui::BindingValueUpdate> binding_updates;
    size_t created = 0;
    for (const auto app_id : recent_apps_) {
        auto app = owner_.get_app(app_id);
        if (!app.has_value()) {
            continue;
        }

        const auto instance_id = make_recents_instance_id(app_id);
        const auto instance_path = make_recents_item_path(app_id);
        const auto title_path = instance_path + "/" + SUPER_RECENTS_LABEL_ID;
        const auto icon_path = instance_path + "/" + SUPER_RECENTS_FALLBACK_ICON_ID;
        const auto display_name = get_app_display_name(*app);

        auto create_result = context.gui().create_view(
                                 SUPER_RECENTS_CARD_TEMPLATE_ID,
                                 SUPER_RECENTS_ITEM_LAYER_PATH,
                                 instance_id
                             );
        if (!create_result) {
            return std::unexpected(create_result.error());
        }

        add_binding_update(binding_updates, title_path, "name", display_name);
        add_binding_update(binding_updates, icon_path, "text", get_app_icon_text(display_name));
        recents_instance_to_app_.emplace(instance_id, app_id);
        ++created;
    }

    const auto content_height = static_cast<int32_t>(created) * RECENTS_CARD_HEIGHT_DP +
                                (created > 0 ? (static_cast<int32_t>(created) - 1) * RECENTS_CARD_GAP_DP : 0);
    add_binding_update(
        binding_updates,
        SUPER_RECENTS_ITEM_LAYER_PATH,
        "content_height",
        std::to_string(std::max<int32_t>(content_height, 1))
    );
    add_binding_update(
        binding_updates,
        std::string(SUPER_RECENTS_LIST_PATH) + "/empty",
        "empty_hidden",
        created > 0 ? "true" : "false"
    );

    static constexpr std::array recents_actions = {
        SUPER_ACTION_RECENTS_OPEN,
        SUPER_ACTION_RECENTS_CLOSE,
        SUPER_ACTION_RECENTS_ALL_APPS,
        SUPER_ACTION_RECENTS_CLEAR_ALL,
    };
    for (const auto *action : recents_actions) {
        auto recents_handler = [this](const gui::Event & event) {
            handle_recents_event(event);
        };
        auto connection = context.gui().subscribe_action(action, recents_handler);
        if (!connection.connected()) {
            disconnect_recents_actions();
            return std::unexpected(std::string("Failed to subscribe recents action: ") + action);
        }
        recents_action_connections_.push_back(std::move(connection));
    }

    auto binding_result = context.gui().set_binding_values(binding_updates);
    if (!binding_result) {
        disconnect_recents_actions();
        return binding_result;
    }

    recents_populated_ = true;
    return {};
}

void ShellApp::handle_recents_event(const gui::Event &event)
{
    if (event.action == SUPER_ACTION_RECENTS_ALL_APPS) {
        auto result = owner_.open_app_launcher();
        if (!result) {
            BROOKESIA_LOGW("Failed to open app launcher from recents: %1%", result.error());
        }
        return;
    }

    if (event.action == SUPER_ACTION_RECENTS_CLEAR_ALL) {
        BROOKESIA_LOGI("Recents: closing all tasks, count(%1%)", recent_apps_.size());
        const auto app_ids = recent_apps_;
        for (const auto app_id : app_ids) {
            auto app = owner_.get_app(app_id);
            if (!app.has_value()) {
                continue;
            }
            if ((app->state != core::AppState::Running) && (app->state != core::AppState::Paused)) {
                continue;
            }
            auto result = owner_.stop_app(app_id);
            if (!result) {
                BROOKESIA_LOGW("Recents: failed to close task: app_id(%1%), error(%2%)", app_id, result.error());
            }
        }
        recent_apps_.clear();
        auto refresh_result = refresh_recents();
        if (!refresh_result) {
            BROOKESIA_LOGW("Failed to refresh recents after closing all: %1%", refresh_result.error());
        }
        return;
    }

    auto instance_id = get_recents_instance_id(event);
    if (!instance_id) {
        BROOKESIA_LOGW("Recents action from unknown path: %1%", event.path);
        return;
    }
    auto app_it = recents_instance_to_app_.find(*instance_id);
    if (app_it == recents_instance_to_app_.end()) {
        BROOKESIA_LOGW("Recents action from unknown instance: %1%", *instance_id);
        return;
    }

    const auto app_id = app_it->second;
    if (event.action == SUPER_ACTION_RECENTS_CLOSE) {
        BROOKESIA_LOGI("Recents: closing task, app_id(%1%)", app_id);
        remove_recent_app(app_id);
        auto refresh_result = refresh_recents();
        if (!refresh_result) {
            BROOKESIA_LOGW("Failed to refresh recents after close: %1%", refresh_result.error());
        }
        return;
    }

    if (launch_overlay_active_) {
        BROOKESIA_LOGW("Ignore recents action while app launch overlay is active");
        return;
    }

    auto app = owner_.get_app(app_id);
    if (!app.has_value()) {
        BROOKESIA_LOGW("Recents: app is no longer available, app_id(%1%)", app_id);
        return;
    }

    auto start_app = [this, app_id]() {
        auto result = owner_.activate_app(app_id, core::System::AppStartOptions{});
        if (!result) {
            BROOKESIA_LOGW("Failed to launch app from recents: app_id(%1%), error(%2%)", app_id, result.error());
        }
    };

    auto collapse_result = set_system_ui_expanded(false);
    if (!collapse_result) {
        BROOKESIA_LOGW("Failed to collapse system UI before app launch: %1%", collapse_result.error());
    }

    auto overlay_result = show_launch_overlay(*app, std::nullopt, [this, app_id]() {
        schedule_app_start_after_launch(app_id);
    });
    if (!overlay_result) {
        BROOKESIA_LOGW("Failed to show app launch overlay from recents: %1%", overlay_result.error());
        start_app();
    }
}

} // namespace esp_brookesia::system::super
