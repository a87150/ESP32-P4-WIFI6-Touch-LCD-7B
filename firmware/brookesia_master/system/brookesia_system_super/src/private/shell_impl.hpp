#pragma once

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#endif

/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "brookesia/system_super/macro_configs.h"
#if !BROOKESIA_SYSTEM_SUPER_ENABLE_DEBUG_LOG
#   define BROOKESIA_LOG_DISABLE_DEBUG_TRACE 1
#endif
#include "private/utils.hpp"
#include "private/shell_app.hpp"
#include "private/system_constants.hpp"
#include "brookesia/service_helper/network/sntp.hpp"
#include "brookesia/service_manager/dataflow/registry.hpp"
#include "brookesia/service_wifi.hpp"

#include <algorithm>
#include <atomic>
#include <array>
#include <chrono>
#include <ctime>
#include <cstdlib>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#if BROOKESIA_SYSTEM_SUPER_ENABLE_PROFILE_LOG
#   define SYSTEM_SUPER_PROFILE_LOGI(...) BROOKESIA_LOGI(__VA_ARGS__)
#else
#   define SYSTEM_SUPER_PROFILE_LOGI(...) do { if (false) { BROOKESIA_LOGI(__VA_ARGS__); } } while (0)
#endif

namespace esp_brookesia::system::super {
namespace {

inline constexpr const char *SUPER_GUI_DISPLAY_SOURCE_ROLE = "gui";
inline constexpr const char *SUPER_DISPLAY_DATAFLOW_PROVIDER_ID = "Display";
inline constexpr const char *SUPER_DISPLAY_CONTROL_SOURCE_NAME = "SystemSuper";
inline constexpr const char *SUPER_DISPLAY_CONTROL_SOURCE_ROLE = "system";
inline constexpr uint8_t SUPER_TOUCH_GESTURE_AREA_TOP_EDGE = (1U << 0);
inline constexpr uint8_t SUPER_TOUCH_GESTURE_AREA_BOTTOM_EDGE = (1U << 1);
using SNTPHelper = service::helper::SNTP;
using WifiHelper = service::helper::Wifi;
using DisplayService = service::Display;
using DisplayHelper = service::helper::Display;
using SteadyClock = std::chrono::steady_clock;
using SteadyTimePoint = SteadyClock::time_point;

int64_t elapsed_ms_since(SteadyTimePoint started_at, SteadyTimePoint ended_at = SteadyClock::now())
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(ended_at - started_at).count();
}

struct WifiStatusState {
    bool visible = false;
    bool connected = false;
};

WifiStatusState get_wifi_status_from_state(std::string_view state)
{
    if (state == BROOKESIA_DESCRIBE_TO_STR(WifiHelper::GeneralState::Connected)) {
        return {
            .visible = true,
            .connected = true,
        };
    }
    if (state == BROOKESIA_DESCRIBE_TO_STR(WifiHelper::GeneralState::Started) ||
            state == BROOKESIA_DESCRIBE_TO_STR(WifiHelper::GeneralState::Connecting) ||
            state == BROOKESIA_DESCRIBE_TO_STR(WifiHelper::GeneralState::Disconnecting)) {
        return {
            .visible = true,
            .connected = false,
        };
    }
    return {};
}

WifiStatusState get_wifi_status_from_event(std::string_view event)
{
    if (event == BROOKESIA_DESCRIBE_TO_STR(WifiHelper::GeneralEvent::Connected)) {
        return {
            .visible = true,
            .connected = true,
        };
    }
    if (event == BROOKESIA_DESCRIBE_TO_STR(WifiHelper::GeneralEvent::Started) ||
            event == BROOKESIA_DESCRIBE_TO_STR(WifiHelper::GeneralEvent::Disconnected)) {
        return {
            .visible = true,
            .connected = false,
        };
    }
    return {};
}

struct AnimationCompletionBarrier {
    explicit AnimationCompletionBarrier(std::function<void()> completed_handler_in)
        : completed_handler(std::move(completed_handler_in))
    {
    }

    void add()
    {
        remaining.fetch_add(1, std::memory_order_relaxed);
    }

    void complete()
    {
        if (remaining.fetch_sub(1, std::memory_order_acq_rel) == 1 && armed.load(std::memory_order_acquire)) {
            fire();
        }
    }

    void arm()
    {
        armed.store(true, std::memory_order_release);
        if (remaining.load(std::memory_order_acquire) == 0) {
            fire();
        }
    }

    void fire()
    {
        bool expected = false;
        if (!fired.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
            return;
        }
        if (completed_handler) {
            completed_handler();
        }
    }

    std::atomic<int32_t> remaining = 0;
    std::atomic<bool> armed = false;
    std::atomic<bool> fired = false;
    std::function<void()> completed_handler;
};

gui::Animation make_position_animation(gui::AnimationProperty property, int32_t to)
{
    return gui::Animation{
        .id = {},
        .trigger = gui::AnimationTrigger::Manual,
        .property = property,
        .from_mode = gui::AnimationValueMode::Current,
        .from = 0,
        .to_mode = gui::AnimationValueMode::Absolute,
        .to = to,
        .duration = SUPER_SYSTEM_UI_ANIMATION_MS,
        .delay = 0,
        .easing = gui::AnimationEasing::EaseOut,
        .repeat = 0,
        .playback = false,
    };
}

gui::Animation make_timed_animation(gui::AnimationProperty property, int32_t to, int32_t duration_ms)
{
    return gui::Animation{
        .id = {},
        .trigger = gui::AnimationTrigger::Manual,
        .property = property,
        .from_mode = gui::AnimationValueMode::Current,
        .from = 0,
        .to_mode = gui::AnimationValueMode::Absolute,
        .to = to,
        .duration = duration_ms,
        .delay = 0,
        .easing = gui::AnimationEasing::EaseOut,
        .repeat = 0,
        .playback = false,
    };
}

gui::Animation make_modal_animation(gui::AnimationProperty property, int32_t to)
{
    return gui::Animation{
        .id = {},
        .trigger = gui::AnimationTrigger::Manual,
        .property = property,
        .from_mode = gui::AnimationValueMode::Current,
        .from = 0,
        .to_mode = gui::AnimationValueMode::Absolute,
        .to = to,
        .duration = SUPER_APP_LAUNCH_ANIMATION_MS,
        .delay = 0,
        .easing = gui::AnimationEasing::EaseOut,
        .repeat = 0,
        .playback = false,
    };
}

gui::Animation make_keyboard_animation(gui::AnimationProperty property, int32_t to)
{
    return gui::Animation{
        .id = {},
        .trigger = gui::AnimationTrigger::Manual,
        .property = property,
        .from_mode = gui::AnimationValueMode::Current,
        .from = 0,
        .to_mode = gui::AnimationValueMode::Absolute,
        .to = to,
        .duration = SUPER_KEYBOARD_ANIMATION_MS,
        .delay = 0,
        .easing = gui::AnimationEasing::EaseOut,
        .repeat = 0,
        .playback = false,
    };
}

gui::Animation make_keyboard_animation(gui::AnimationProperty property, int32_t from, int32_t to)
{
    return gui::Animation{
        .id = {},
        .trigger = gui::AnimationTrigger::Manual,
        .property = property,
        .from_mode = gui::AnimationValueMode::Absolute,
        .from = from,
        .to_mode = gui::AnimationValueMode::Absolute,
        .to = to,
        .duration = SUPER_KEYBOARD_ANIMATION_MS,
        .delay = 0,
        .easing = gui::AnimationEasing::EaseOut,
        .repeat = 0,
        .playback = false,
    };
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

bool has_gesture_area(uint8_t areas, uint8_t area)
{
    return (areas & area) != 0;
}

int32_t get_gesture_exit_distance_px(const gui::Environment &environment)
{
    return std::clamp<int32_t>(std::max<int32_t>(environment.height_px / 5, 1), 80, 140);
}

int32_t get_status_peek_distance_px(const gui::Environment &environment)
{
    return std::clamp<int32_t>(std::max<int32_t>(environment.height_px / 10, 1), 36, 72);
}

uint16_t get_gesture_vertical_edge_px(uint32_t height)
{
    return static_cast<uint16_t>(std::clamp<uint32_t>(std::max<uint32_t>(height * 8 / 100, 24), 24, 96));
}

uint16_t get_gesture_horizontal_edge_px(uint32_t width)
{
    return static_cast<uint16_t>(std::clamp<uint32_t>(std::max<uint32_t>(width * 6 / 100, 24), 24, 96));
}

int32_t get_fallback_gesture_indicator_width(const gui::Environment &environment)
{
    return std::clamp<int32_t>(std::max<int32_t>(environment.width_px / 5, 1), 96, 180);
}

std::string message_dialog_icon_src(core::MessageDialogIcon icon)
{
    switch (icon) {
    case core::MessageDialogIcon::Information:
        return SUPER_MESSAGE_DIALOG_INFORMATION_IMAGE_ID;
    case core::MessageDialogIcon::Question:
        return SUPER_MESSAGE_DIALOG_QUESTION_IMAGE_ID;
    case core::MessageDialogIcon::Warning:
        return SUPER_MESSAGE_DIALOG_WARNING_IMAGE_ID;
    case core::MessageDialogIcon::Critical:
        return SUPER_MESSAGE_DIALOG_CRITICAL_IMAGE_ID;
    case core::MessageDialogIcon::None:
    default:
        return {};
    }
}

std::string message_dialog_icon_recolor(core::MessageDialogIcon icon)
{
    switch (icon) {
    case core::MessageDialogIcon::Information:
        return "#2563eb";
    case core::MessageDialogIcon::Question:
        return "#7c3aed";
    case core::MessageDialogIcon::Warning:
        return "#f59e0b";
    case core::MessageDialogIcon::Critical:
        return "#dc2626";
    case core::MessageDialogIcon::None:
    default:
        return {};
    }
}

std::string int_to_binding(int32_t value)
{
    return std::to_string(value);
}

int32_t get_centered_gesture_bar_x(int32_t max_width, int32_t width)
{
    return std::max<int32_t>((std::max<int32_t>(max_width, width) - width) / 2, 0);
}

void add_gesture_indicator_binding_updates(
    std::vector<gui::BindingValueUpdate> &updates,
    bool hidden,
    int32_t max_width,
    int32_t width
)
{
    const auto safe_max_width = std::max<int32_t>(max_width, 1);
    const auto clamped_width = std::clamp<int32_t>(width, 1, safe_max_width);
    add_binding_update(
        updates,
        SUPER_GESTURE_INDICATOR_PATH,
        "gesture_indicator_hidden",
        bool_to_binding(hidden)
    );
    add_binding_update(
        updates,
        SUPER_GESTURE_INDICATOR_BAR_PATH,
        "gesture_bar_x",
        int_to_binding(get_centered_gesture_bar_x(safe_max_width, clamped_width))
    );
    add_binding_update(
        updates,
        SUPER_GESTURE_INDICATOR_BAR_PATH,
        "gesture_bar_width",
        int_to_binding(clamped_width)
    );
}

gui::ViewFrame make_fallback_origin(const gui::Environment &environment)
{
    const auto icon_size = std::max<int32_t>(SUPER_APP_LAUNCH_FINAL_ICON_SIZE, 1);
    const auto width = std::max(environment.width_px, icon_size);
    const auto height = std::max(environment.height_px, icon_size);
    return gui::ViewFrame{
        .x = (width - icon_size) / 2,
        .y = (height - icon_size) / 2,
        .width = icon_size,
        .height = icon_size,
    };
}

int32_t get_collapsed_status_bar_y(core::AppContext &context)
{
    auto frame = context.gui().get_view_frame(SUPER_STATUS_BAR_PATH);
    if (frame && frame->height > 0) {
        return -std::max(std::abs(SUPER_STATUS_BAR_COLLAPSED_Y), frame->height * 2);
    }
    return SUPER_STATUS_BAR_COLLAPSED_Y;
}

bool is_supported_keyboard_mode(std::string_view mode)
{
    return mode == "text" || mode == "upper" || mode == "number" || mode == "special";
}

std::vector<std::string> default_keyboard_modes()
{
    return {"text", "upper", "number", "special"};
}

std::string join_keyboard_modes(const std::vector<std::string> &modes)
{
    std::string result;
    for (size_t i = 0; i < modes.size(); ++i) {
        if (i > 0) {
            result.push_back(',');
        }
        result += modes[i];
    }
    return result;
}

std::expected<core::KeyboardRequestOptions, std::string> normalize_keyboard_options(
    core::KeyboardRequestOptions options)
{
    if (!is_supported_keyboard_mode(options.mode)) {
        return std::unexpected("Keyboard mode must be one of: text, upper, number, special");
    }
    if (options.allowed_modes.empty()) {
        options.allowed_modes = default_keyboard_modes();
    }
    for (const auto &mode : options.allowed_modes) {
        if (!is_supported_keyboard_mode(mode)) {
            return std::unexpected("Keyboard allowed mode must be one of: text, upper, number, special");
        }
        if (std::count(options.allowed_modes.begin(), options.allowed_modes.end(), mode) > 1) {
            return std::unexpected("Keyboard allowed modes must not contain duplicates");
        }
    }
    if (std::find(options.allowed_modes.begin(), options.allowed_modes.end(), options.mode) ==
            options.allowed_modes.end()) {
        options.mode = options.allowed_modes.front();
    }
    return options;
}

} // namespace


} // namespace esp_brookesia::system::super

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif
