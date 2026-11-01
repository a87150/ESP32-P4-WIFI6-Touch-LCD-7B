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
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "brookesia/service_manager/dataflow/registry.hpp"
#include "private/shell_app.hpp"
#include "private/system_constants.hpp"
#include "private/utils.hpp"

namespace esp_brookesia::system::super {
namespace {

inline constexpr const char *SUPER_GUI_DISPLAY_SOURCE_ROLE = "gui";
inline constexpr const char *SUPER_DISPLAY_DATAFLOW_PROVIDER_ID = "Display";
inline constexpr const char *SUPER_DISPLAY_CONTROL_SOURCE_NAME = "SystemSuper";
inline constexpr const char *SUPER_DISPLAY_CONTROL_SOURCE_ROLE = "system";

} // namespace

bool ShellApp::ensure_display_operation()
{
    if (display_operation_ && display_operation_->is_available()) {
        return true;
    }
    display_operation_.reset();

    service::dataflow::VisualOperationConfig operation_config;
    operation_config.owner = SUPER_DISPLAY_CONTROL_SOURCE_NAME;
    operation_config.provider_id = SUPER_DISPLAY_DATAFLOW_PROVIDER_ID;
    operation_config.model = service::dataflow::Model::Visual;
    operation_config.source = {
        .name = SUPER_DISPLAY_CONTROL_SOURCE_NAME,
        .role = SUPER_DISPLAY_CONTROL_SOURCE_ROLE,
        .preferred_outputs = {},
        .priority = 0,
    };
    auto operation_result = service::ServiceManager::get_instance().get_dataflow_registry().open_visual_operation(
                                std::move(operation_config)
                            );
    if (!operation_result) {
        BROOKESIA_LOGW(
            "Failed to open Display data-flow operation for Shell source control: %1%", operation_result.error()
        );
        return false;
    }
    display_operation_ = std::move(operation_result.value());
    return true;
}

void ShellApp::release_display_operation()
{
    display_source_restore_records_.clear();
    if (display_operation_) {
        display_operation_->close();
        display_operation_.reset();
    }
}

std::vector<std::string> ShellApp::get_gui_display_output_names() const
{
    std::vector<std::string> output_names;
    if (!display_operation_ || !display_operation_->is_available()) {
        return output_names;
    }

    auto sources = display_operation_->get_sources();
    auto source_it = std::find_if(sources.begin(), sources.end(), [](const auto & source) {
        return source.role == SUPER_GUI_DISPLAY_SOURCE_ROLE;
    });
    if (source_it != sources.end()) {
        output_names = source_it->preferred_outputs;
    }

    if (output_names.empty()) {
        auto outputs = display_operation_->get_outputs();
        if (!outputs.empty()) {
            output_names.push_back(outputs.front().output.name);
        }
    }
    return output_names;
}

bool ShellApp::is_display_source_available(std::string_view source_name) const
{
    if (!display_operation_ || !display_operation_->is_available()) {
        return false;
    }
    auto sources = display_operation_->get_sources();
    return std::find_if(sources.begin(), sources.end(), [source_name](const auto & source) {
        return source.name == source_name;
    }) != sources.end();
}

bool ShellApp::is_display_source_role_available(std::string_view role) const
{
    if (!display_operation_ || !display_operation_->is_available()) {
        return false;
    }
    auto sources = display_operation_->get_sources();
    return std::find_if(sources.begin(), sources.end(), [role](const auto & source) {
        return source.role == role;
    }) != sources.end();
}

void ShellApp::switch_display_to_gui_for_system_ui()
{
    if (!ensure_display_operation()) {
        return;
    }
    if (!is_display_source_role_available(SUPER_GUI_DISPLAY_SOURCE_ROLE)) {
        BROOKESIA_LOGW(
            "Skip switching Display to GUI: source role %1% is not registered", SUPER_GUI_DISPLAY_SOURCE_ROLE
        );
        return;
    }

    auto output_names = get_gui_display_output_names();
    if (output_names.empty()) {
        BROOKESIA_LOGW("Skip switching Display to GUI: no output is available");
        return;
    }

    for (const auto &output_name : output_names) {
        auto active_source = display_operation_->get_active_source(output_name);
        if (!active_source) {
            BROOKESIA_LOGW(
                "Failed to read active Display source before showing system UI: output(%1%), error(%2%)",
                output_name, active_source.error()
            );
            continue;
        }
        auto active_role = display_operation_->get_active_source_role(output_name);
        if (!active_role) {
            BROOKESIA_LOGW(
                "Failed to read active Display role before showing system UI: output(%1%), error(%2%)",
                output_name, active_role.error()
            );
            continue;
        }

        if (!foreground_is_shell_ && !active_source->empty() && (*active_role != SUPER_GUI_DISPLAY_SOURCE_ROLE)) {
            const auto existing_record = std::find_if(
                                             display_source_restore_records_.begin(),
                                             display_source_restore_records_.end(),
            [&output_name](const auto & item) {
                return item.output_name == output_name;
            }
                                         );
            if (existing_record == display_source_restore_records_.end()) {
                // A second system-UI request can arrive before the previous
                // gesture animation restores the application source. Preserve
                // the first source until the matching final restore.
                display_source_restore_records_.push_back(DisplaySourceRestoreRecord{
                    .output_name = output_name,
                    .source_name = *active_source,
                });
            }
        }

        if (*active_role == SUPER_GUI_DISPLAY_SOURCE_ROLE) {
            continue;
        }

        auto result = display_operation_->set_active_source_role(output_name, SUPER_GUI_DISPLAY_SOURCE_ROLE);
        if (!result) {
            BROOKESIA_LOGW(
                "Failed to switch Display output %1% active source role to %2% for system UI: %3%",
                output_name, SUPER_GUI_DISPLAY_SOURCE_ROLE, result.error()
            );
            continue;
        }
        BROOKESIA_LOGI(
            "Switch Display output %1% active source role to %2% for system UI",
            output_name, SUPER_GUI_DISPLAY_SOURCE_ROLE
        );
    }
}

void ShellApp::restore_display_after_system_ui()
{
    if (display_source_restore_records_.empty()) {
        return;
    }
    auto restore_records = std::move(display_source_restore_records_);
    display_source_restore_records_.clear();
    if (!ensure_display_operation()) {
        return;
    }

    for (const auto &record : restore_records) {
        if (!is_display_source_available(record.source_name)) {
            BROOKESIA_LOGW(
                "Skip restoring Display output %1% source %2%: source is not registered",
                record.output_name, record.source_name
            );
            continue;
        }

        auto result = display_operation_->set_active_source_named(record.output_name, record.source_name);
        if (!result) {
            BROOKESIA_LOGW(
                "Failed to restore Display output %1% active source to %2%: %3%",
                record.output_name, record.source_name, result.error()
            );
            continue;
        }
        BROOKESIA_LOGI(
            "Restore Display output %1% active source to %2%",
            record.output_name, record.source_name
        );
    }
}

void ShellApp::clear_display_source_restore_records()
{
    display_source_restore_records_.clear();
}

std::expected<void, std::string> ShellApp::set_foreground_app(const std::optional<core::AppInfo> &app)
{
    const bool next_foreground_is_shell = !app.has_value() || (app->app_id == owner_.shell_app_id_);
    const bool should_reset_gesture = gesture_tracking_ || gesture_exit_armed_ || gesture_exit_triggered_ ||
                                      gesture_exit_hold_timer_id_ != core::INVALID_TIMER_ID ||
                                      gesture_indicator_animation_id_ != 0 ||
                                      gesture_indicator_x_animation_id_ != 0;
    if (next_foreground_is_shell && foreground_is_shell_ && system_ui_expanded_ && !should_reset_gesture) {
        return {};
    }

    if (!foreground_is_shell_) {
        reset_status_peek_session(true, true, "foreground app change");
    }
    if (app.has_value() && app->app_id != owner_.shell_app_id_) {
        remember_recent_app(app->app_id);
    }
    if (should_reset_gesture) {
        BROOKESIA_LOGI(
            "Reset Shell bottom gesture: reason(foreground app change), tracking(%1%), armed(%2%), triggered(%3%)",
            gesture_tracking_,
            gesture_exit_armed_,
            gesture_exit_triggered_
        );
        reset_gesture_indicator();
    }
    foreground_is_shell_ = next_foreground_is_shell;
    set_status_app_icon(app);
    if (foreground_is_shell_) {
        clear_display_source_restore_records();
    }
    if (auto state_result = refresh_system_ui_state_bindings(); !state_result) {
        BROOKESIA_LOGW("Failed to refresh Shell system UI mask after foreground change: %1%", state_result.error());
    }
    auto background_result = context_->gui().trigger_screen_flow(
                                 SUPER_BACKGROUND_FLOW_ID,
                                 foreground_is_shell_ ? SUPER_ACTION_BACKGROUND_SHELL : SUPER_ACTION_BACKGROUND_APP
                             );
    if (!background_result) {
        return background_result;
    }
    return set_system_ui_expanded(foreground_is_shell_);
}

} // namespace esp_brookesia::system::super
