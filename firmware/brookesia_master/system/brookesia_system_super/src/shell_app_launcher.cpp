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
#include <cctype>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "private/shell_app.hpp"
#include "private/system_constants.hpp"
#include "private/utils.hpp"

#if BROOKESIA_SYSTEM_SUPER_ENABLE_PROFILE_LOG
#   define SYSTEM_SUPER_PROFILE_LOGI(...) BROOKESIA_LOGI(__VA_ARGS__)
#else
#   define SYSTEM_SUPER_PROFILE_LOGI(...) do { if (false) { BROOKESIA_LOGI(__VA_ARGS__); } } while (0)
#endif

namespace esp_brookesia::system::super {
namespace {

inline constexpr int32_t LAUNCHER_ITEM_WIDTH = 112;
inline constexpr int32_t LAUNCHER_ITEM_HEIGHT = 112;
inline constexpr int32_t LAUNCHER_ITEM_GAP = 18;
using SteadyClock = std::chrono::steady_clock;
using SteadyTimePoint = SteadyClock::time_point;

int64_t elapsed_ms_since(SteadyTimePoint started_at, SteadyTimePoint ended_at = SteadyClock::now())
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(ended_at - started_at).count();
}

std::optional<std::string> get_launcher_instance_id(const gui::Event &event)
{
    if (!event.path.starts_with(SUPER_LAUNCHER_PATH_PREFIX)) {
        return std::nullopt;
    }
    auto instance_id = event.path.substr(std::string_view(SUPER_LAUNCHER_PATH_PREFIX).size());
    auto child_separator = instance_id.find('/');
    if (child_separator != std::string::npos) {
        instance_id.resize(child_separator);
    }
    if (instance_id.empty()) {
        return std::nullopt;
    }
    return std::string(SUPER_LAUNCHER_INSTANCE_PREFIX) + instance_id;
}

std::string make_launcher_slot_path(size_t index)
{
    return std::string(SUPER_LAUNCHER_SLOT_PATH_PREFIX) + std::to_string(index);
}

std::string make_launcher_item_instance_id(core::AppId app_id)
{
    return std::string(SUPER_LAUNCHER_INSTANCE_PREFIX) + std::to_string(app_id);
}

std::string make_launcher_item_path(core::AppId app_id)
{
    return std::string(SUPER_LAUNCHER_PATH_PREFIX) + std::to_string(app_id);
}

std::string make_launcher_icon_text(std::string_view name)
{
    for (char ch : name) {
        const auto unsigned_ch = static_cast<unsigned char>(ch);
        if (std::isalnum(unsigned_ch) != 0) {
            return std::string(1, static_cast<char>(std::toupper(unsigned_ch)));
        }
    }
    if (!name.empty()) {
        const auto first = static_cast<unsigned char>(name.front());
        size_t char_length = 1;
        if ((first & 0xE0U) == 0xC0U) {
            char_length = 2;
        } else if ((first & 0xF0U) == 0xE0U) {
            char_length = 3;
        } else if ((first & 0xF8U) == 0xF0U) {
            char_length = 4;
        }
        return std::string(name.substr(0, std::min(char_length, name.size())));
    }
    return "?";
}

std::string normalize_theme_id(std::string_view theme_id)
{
    if (theme_id == "shell.light") {
        return SUPER_LIGHT_THEME_ID;
    }
    if (theme_id == "shell.dark") {
        return SUPER_DARK_THEME_ID;
    }
    return std::string(theme_id);
}

std::optional<gui::ViewFrame> get_absolute_view_frame(core::AppContext &context, std::string_view absolute_path)
{
    if (absolute_path.empty() || absolute_path.front() != '/') {
        return std::nullopt;
    }

    gui::ViewFrame result;
    std::string current_path;
    size_t start = 1;
    while (start <= absolute_path.size()) {
        const auto slash = absolute_path.find('/', start);
        const auto part_length = slash == std::string_view::npos ? std::string_view::npos : slash - start;
        const auto part = absolute_path.substr(start, part_length);
        if (part.empty()) {
            break;
        }

        current_path += "/";
        current_path += part;
        auto frame = context.gui().get_view_frame(current_path);
        if (!frame.has_value()) {
            return std::nullopt;
        }
        result.x += frame->x;
        result.y += frame->y;
        result.width = frame->width;
        result.height = frame->height;

        if (slash == std::string_view::npos) {
            break;
        }
        start = slash + 1;
    }

    return result.width > 0 && result.height > 0 ? std::optional<gui::ViewFrame>(result) : std::nullopt;
}

} // namespace

std::string ShellApp::get_app_icon_text(std::string_view name) const
{
    return make_launcher_icon_text(name);
}

std::expected<void, std::string> ShellApp::populate_launcher(core::AppContext &context)
{
    disconnect_launcher_actions();
    for (const auto &[instance_id, app_id] : launcher_instance_to_app_) {
        (void)instance_id;
        const auto instance_path = make_launcher_item_path(app_id);
        if (!context.gui().destroy_view(instance_path)) {
            BROOKESIA_LOGW("Failed to destroy launcher item before refresh: path(%1%)", instance_path);
        }
    }
    for (size_t i = 0; i < launcher_slot_count_; ++i) {
        const auto slot_path = make_launcher_slot_path(i);
        if (!context.gui().destroy_view(slot_path)) {
            BROOKESIA_LOGW("Failed to destroy launcher slot before refresh: path(%1%)", slot_path);
        }
    }
    launcher_slot_count_ = 0;
    launcher_order_.clear();
    launcher_instance_to_app_.clear();
    launcher_populated_ = false;

    std::vector<core::AppInfo> visible_apps;
    for (const auto &app : owner_.list_apps()) {
        if (!app.manifest.visible) {
            continue;
        }
        visible_apps.push_back(app);
        launcher_order_.push_back(app.app_id);
    }

    const auto environment = owner_.get_environment();
    const auto density = std::max(environment.density, 0.001F);
    const auto fallback_width = static_cast<int32_t>(
                                    std::max(1.0F, static_cast<float>(environment.width_px) / density)
                                );
    const auto content_frame = context.gui().get_view_frame(SUPER_LAUNCHER_CONTENT_PATH);
    const auto available_width = content_frame.has_value() ?
                                 std::max<int32_t>(content_frame->width, LAUNCHER_ITEM_WIDTH) :
                                 std::max<int32_t>(fallback_width, LAUNCHER_ITEM_WIDTH);
    // Keep a comfortable margin on both sides of the screen, then lay the apps out
    // from left to right inside the grid. Centering each row made the icons pile up
    // in the middle of the screen, which looked worse than a plain left-to-right flow.
    const auto launcher_margin_x = std::clamp<int32_t>(available_width / 16, 32, 96);
    const auto launcher_grid_width = std::max<int32_t>(
                                         available_width - launcher_margin_x * 2,
                                         LAUNCHER_ITEM_WIDTH
                                     );
    const auto launcher_grid_x = launcher_margin_x;
    const auto launcher_columns = std::max<int32_t>(
                                      1,
                                      (launcher_grid_width + LAUNCHER_ITEM_GAP) /
                                      (LAUNCHER_ITEM_WIDTH + LAUNCHER_ITEM_GAP)
                                  );
    std::vector<gui::BindingValueUpdate> binding_updates;
    add_binding_update(binding_updates, SUPER_LAUNCHER_GRID_STAGE_PATH, "grid_x", std::to_string(launcher_grid_x));
    add_binding_update(
        binding_updates,
        SUPER_LAUNCHER_GRID_STAGE_PATH,
        "grid_width",
        std::to_string(launcher_grid_width)
    );

    launcher_slot_count_ = 0;

    // The scroll range must be measured against the scroll viewport (the grid
    // stage), not the outer content container: the stage is inset by
    // `launcherGridTop`, so using the taller content frame left a permanent
    // ~22px scroll range that clipped the top of the first icon row.
    const auto stage_frame = context.gui().get_view_frame(SUPER_LAUNCHER_GRID_STAGE_PATH);
    int32_t content_height = stage_frame.has_value() ? std::max<int32_t>(stage_frame->height, 1) : 1;
    for (size_t i = 0; i < visible_apps.size(); ++i) {
        const auto &app = visible_apps[i];
        const auto column = static_cast<int32_t>(i % static_cast<size_t>(launcher_columns));
        const auto row = static_cast<int32_t>(i / static_cast<size_t>(launcher_columns));
        // Lay every row out from left to right inside the grid; the margin comes
        // from the grid offset so the icons never sit too close to the screen edge.
        const auto item_x = column * (LAUNCHER_ITEM_WIDTH + LAUNCHER_ITEM_GAP);
        const auto item_y = row * (LAUNCHER_ITEM_HEIGHT + LAUNCHER_ITEM_GAP);
        content_height = std::max(content_height, item_y + LAUNCHER_ITEM_HEIGHT);

        const auto instance_id = make_launcher_item_instance_id(app.app_id);
        const auto instance_path = make_launcher_item_path(app.app_id);
        const auto label_path = instance_path + "/" + SUPER_LAUNCHER_LABEL_ID;
        const auto fallback_icon_path = instance_path + "/" + SUPER_LAUNCHER_FALLBACK_ICON_ID;
        const auto image_icon_path = instance_path + "/" + SUPER_LAUNCHER_IMAGE_ICON_ID;
        const auto has_image_icon = core::has_app_icon_image(app.manifest);
        const auto display_name = get_app_display_name(app);

        auto create_result = context.gui().create_view(
                                 SUPER_LAUNCHER_BUTTON_TEMPLATE_ID,
                                 SUPER_LAUNCHER_ITEM_LAYER_PATH,
                                 instance_id
                             );
        if (!create_result) {
            return std::unexpected(create_result.error());
        }

        add_binding_update(binding_updates, instance_path, "x", std::to_string(item_x));
        add_binding_update(binding_updates, instance_path, "y", std::to_string(item_y));
        add_binding_update(binding_updates, label_path, "name", display_name);
        if (has_image_icon) {
            add_binding_update(
                binding_updates,
                image_icon_path,
                "src",
                core::resolve_app_icon_resource_id(app.manifest)
            );
            add_binding_update(binding_updates, image_icon_path, "hidden", "false");
            add_binding_update(binding_updates, fallback_icon_path, "hidden", "true");
            add_binding_update(binding_updates, fallback_icon_path, "text", "");
        } else {
            add_binding_update(binding_updates, image_icon_path, "hidden", "true");
            add_binding_update(binding_updates, fallback_icon_path, "hidden", "false");
            add_binding_update(binding_updates, fallback_icon_path, "text", make_launcher_icon_text(display_name));
        }

        launcher_instance_to_app_.emplace(instance_id, app.app_id);
    }

    static constexpr std::array launcher_actions = {
        SUPER_ACTION_LAUNCH_APP,
    };
    for (const auto *action : launcher_actions) {
        auto launcher_handler = [this](const gui::Event & event) {
            handle_launcher_event(event);
        };
        auto connection = context.gui().subscribe_action(action, launcher_handler);
        if (!connection.connected()) {
            disconnect_launcher_actions();
            return std::unexpected(std::string("Failed to subscribe launcher action: ") + action);
        }
        launcher_action_connections_.push_back(std::move(connection));
    }

    add_binding_update(
        binding_updates,
        SUPER_LAUNCHER_SLOT_GRID_PATH,
        "content_height",
        std::to_string(content_height)
    );
    add_binding_update(
        binding_updates,
        SUPER_LAUNCHER_ITEM_LAYER_PATH,
        "content_height",
        std::to_string(content_height)
    );
    add_binding_update(
        binding_updates,
        SUPER_LAUNCHER_DRAG_LAYER_PATH,
        "content_height",
        std::to_string(content_height)
    );
    auto binding_result = context.gui().set_binding_values(binding_updates);
    if (!binding_result) {
        disconnect_launcher_actions();
        return binding_result;
    }

    launcher_populated_ = true;
    return {};
}

std::expected<void, std::string> ShellApp::refresh_launcher()
{
    if (context_ == nullptr) {
        return {};
    }
    return populate_launcher(*context_);
}

std::expected<void, std::string> ShellApp::refresh_environment()
{
    if (context_ == nullptr) {
        return {};
    }
    current_theme_id_ = normalize_theme_id(context_->gui().get_theme());
    auto i18n_result = apply_i18n_updates();
    if (!i18n_result) {
        return i18n_result;
    }
    if (!launcher_populated_) {
        return {};
    }

    std::vector<gui::BindingValueUpdate> updates;
    for (const auto &app : owner_.list_apps()) {
        if (!app.manifest.visible) {
            continue;
        }

        const auto instance_path = std::string(SUPER_LAUNCHER_PATH_PREFIX) + std::to_string(app.app_id);
        const auto label_path = instance_path + "/" + SUPER_LAUNCHER_LABEL_ID;
        const auto fallback_icon_path = instance_path + "/" + SUPER_LAUNCHER_FALLBACK_ICON_ID;
        const auto image_icon_path = instance_path + "/" + SUPER_LAUNCHER_IMAGE_ICON_ID;
        const auto has_image_icon = core::has_app_icon_image(app.manifest);
        const auto display_name = get_app_display_name(app);
        const auto fallback_text = has_image_icon ? std::string() : get_app_icon_text(display_name);

        add_binding_update(updates, label_path, "name", display_name);
        if (has_image_icon) {
            add_binding_update(
                updates,
                image_icon_path,
                "src",
                core::resolve_app_icon_resource_id(app.manifest)
            );
            add_binding_update(updates, image_icon_path, "hidden", "false");
            add_binding_update(updates, fallback_icon_path, "hidden", "true");
            add_binding_update(updates, fallback_icon_path, "text", "");
        } else {
            add_binding_update(updates, image_icon_path, "hidden", "true");
            add_binding_update(updates, fallback_icon_path, "hidden", "false");
            add_binding_update(updates, fallback_icon_path, "text", fallback_text);
        }
    }

    if (!updates.empty()) {
        auto binding_result = context_->gui().set_binding_values(updates);
        if (!binding_result) {
            return binding_result;
        }
    }

    return {};
}

void ShellApp::disconnect_launcher_actions()
{
    for (auto &connection : launcher_action_connections_) {
        connection.disconnect();
    }
    launcher_action_connections_.clear();
}

void ShellApp::handle_launcher_event(const gui::Event &event)
{
    if (launch_overlay_active_) {
        BROOKESIA_LOGW("Ignore launcher action while app launch overlay is active");
        return;
    }

    auto instance_id = get_launcher_instance_id(event);
    if (!instance_id) {
        BROOKESIA_LOGW("Launcher action from unknown path: %1%", event.path);
        return;
    }
    auto app_it = launcher_instance_to_app_.find(*instance_id);
    if (app_it == launcher_instance_to_app_.end()) {
        BROOKESIA_LOGW("Launcher action from unknown instance: %1%", *instance_id);
        return;
    }

    std::optional<gui::ViewFrame> launch_origin_frame;
    if (context_ != nullptr) {
        const auto icon_box_path =
            std::string(SUPER_LAUNCHER_ITEM_LAYER_PATH) + "/" + *instance_id + "/" + SUPER_LAUNCHER_ICON_BOX_ID;
        launch_origin_frame = get_absolute_view_frame(*context_, icon_box_path);
        if (!launch_origin_frame.has_value()) {
            BROOKESIA_LOGW("Launcher icon frame not available: path(%1%)", icon_box_path);
        }
    }

    const auto app_id = app_it->second;
    launch_request_started_at_ = SteadyClock::now();
    auto start_app = [this, app_id]() {
        const auto request_started_at = launch_request_started_at_;
        const auto app_start_started_at = SteadyClock::now();
        auto result = owner_.activate_app(app_id, core::System::AppStartOptions{});
        const auto now = SteadyClock::now();
        SYSTEM_SUPER_PROFILE_LOGI(
            "Shell app launch profile: app_id(%1%), start_app_ms(%2%), total_ms(%3%)",
            app_id,
            elapsed_ms_since(app_start_started_at, now),
            request_started_at.has_value() ? elapsed_ms_since(*request_started_at, now) : 0
        );
        launch_request_started_at_.reset();
        if (!result) {
            BROOKESIA_LOGW("Failed to launch app: app_id(%1%), error(%2%)", app_id, result.error());
        }
    };
    auto app = owner_.get_app(app_id);
    if (!app.has_value()) {
        start_app();
        return;
    }

    auto collapse_result = set_system_ui_expanded(false);
    if (!collapse_result) {
        BROOKESIA_LOGW("Failed to collapse system UI before app launch: %1%", collapse_result.error());
    }

    auto overlay_result = show_launch_overlay(*app, launch_origin_frame, [this, app_id]() {
        schedule_app_start_after_launch(app_id);
    });
    if (!overlay_result) {
        BROOKESIA_LOGW("Failed to show app launch overlay: %1%", overlay_result.error());
        start_app();
    }
}

} // namespace esp_brookesia::system::super
