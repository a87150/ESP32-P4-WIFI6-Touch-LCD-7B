/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "private/shell_impl.hpp"

namespace esp_brookesia::system::super {



void ShellApp::unmount_message_dialog()
{
    message_dialog_action_connections_.clear();
    stop_message_dialog_auto_close_timer();
    if (context_ != nullptr && message_dialog_mounted_) {
        auto flow_result = context_->gui().trigger_screen_flow(
                               SUPER_MESSAGE_DIALOG_FLOW_ID,
                               SUPER_ACTION_MESSAGE_DIALOG_HIDE
                           );
        if (!flow_result) {
            BROOKESIA_LOGW("Failed to hide message dialog screen: %1%", flow_result.error());
        }
    }
    message_dialog_mounted_ = false;
    message_dialog_needs_reset_ = context_ != nullptr;
    if (context_ != nullptr) {
        if (auto state_result = refresh_system_ui_state_bindings(); !state_result) {
            BROOKESIA_LOGW("Failed to refresh system UI mask after message dialog unmount: %1%", state_result.error());
        }
    }
    reset_message_dialog_state();
}


std::expected<void, std::string> ShellApp::refresh_message_dialog_bindings(
    const core::MessageDialogOptions &options
)
{
    if (context_ == nullptr) {
        return std::unexpected("Shell app is not running");
    }

    std::vector<gui::BindingValueUpdate> updates;
    const auto icon_src = message_dialog_icon_src(options.icon);
    const auto icon_recolor = message_dialog_icon_recolor(options.icon);
    add_binding_update(updates, SUPER_MESSAGE_DIALOG_ICON_PATH, "src", icon_src);
    add_binding_update(updates, SUPER_MESSAGE_DIALOG_ICON_PATH, "recolor", icon_recolor);
    add_binding_update(updates, SUPER_MESSAGE_DIALOG_ICON_PATH, "recolorOpacity", int_to_binding(255));
    add_binding_update(updates, SUPER_MESSAGE_DIALOG_ICON_PATH, "hidden", bool_to_binding(icon_src.empty()));
    add_binding_update(updates, SUPER_MESSAGE_DIALOG_TEXT_PATH, "text", options.text);
    add_binding_update(
        updates,
        SUPER_MESSAGE_DIALOG_INFORMATIVE_TEXT_PATH,
        "text",
        options.informative_text
    );
    add_binding_update(
        updates,
        SUPER_MESSAGE_DIALOG_INFORMATIVE_TEXT_PATH,
        "hidden",
        bool_to_binding(options.informative_text.empty())
    );
    add_binding_update(updates, SUPER_MESSAGE_DIALOG_ACTIONS_PATH, "hidden", bool_to_binding(options.buttons.empty()));

    const std::array<const char *, 3> button_paths = {
        SUPER_MESSAGE_DIALOG_BUTTON0_PATH,
        SUPER_MESSAGE_DIALOG_BUTTON1_PATH,
        SUPER_MESSAGE_DIALOG_BUTTON2_PATH,
    };
    const std::array<const char *, 3> label_paths = {
        SUPER_MESSAGE_DIALOG_BUTTON0_LABEL_PATH,
        SUPER_MESSAGE_DIALOG_BUTTON1_LABEL_PATH,
        SUPER_MESSAGE_DIALOG_BUTTON2_LABEL_PATH,
    };
    for (size_t i = 0; i < button_paths.size(); ++i) {
        const bool visible = i < options.buttons.size();
        add_binding_update(updates, button_paths[i], "hidden", bool_to_binding(!visible));
        add_binding_update(updates, label_paths[i], "text", visible ? options.buttons[i].text : std::string());
    }
    return context_->gui().set_binding_values(updates);
}


std::expected<void, std::string> ShellApp::clear_message_dialog_bindings()
{
    if (context_ == nullptr) {
        return std::unexpected("Shell app is not running");
    }

    std::vector<gui::BindingValueUpdate> updates;
    add_binding_update(updates, SUPER_MESSAGE_DIALOG_ICON_PATH, "src", "");
    add_binding_update(updates, SUPER_MESSAGE_DIALOG_ICON_PATH, "recolor", "");
    add_binding_update(updates, SUPER_MESSAGE_DIALOG_ICON_PATH, "recolorOpacity", int_to_binding(255));
    add_binding_update(updates, SUPER_MESSAGE_DIALOG_ICON_PATH, "hidden", "true");
    add_binding_update(updates, SUPER_MESSAGE_DIALOG_TEXT_PATH, "text", "");
    add_binding_update(updates, SUPER_MESSAGE_DIALOG_INFORMATIVE_TEXT_PATH, "text", "");
    add_binding_update(updates, SUPER_MESSAGE_DIALOG_INFORMATIVE_TEXT_PATH, "hidden", "true");
    add_binding_update(updates, SUPER_MESSAGE_DIALOG_ACTIONS_PATH, "hidden", "true");

    const std::array<const char *, 3> button_paths = {
        SUPER_MESSAGE_DIALOG_BUTTON0_PATH,
        SUPER_MESSAGE_DIALOG_BUTTON1_PATH,
        SUPER_MESSAGE_DIALOG_BUTTON2_PATH,
    };
    const std::array<const char *, 3> label_paths = {
        SUPER_MESSAGE_DIALOG_BUTTON0_LABEL_PATH,
        SUPER_MESSAGE_DIALOG_BUTTON1_LABEL_PATH,
        SUPER_MESSAGE_DIALOG_BUTTON2_LABEL_PATH,
    };
    for (size_t i = 0; i < button_paths.size(); ++i) {
        add_binding_update(updates, button_paths[i], "hidden", "true");
        add_binding_update(updates, label_paths[i], "text", "");
    }
    return context_->gui().set_binding_values(updates);
}


void ShellApp::start_message_dialog_auto_close_timer(int32_t auto_close_ms)
{
    stop_message_dialog_auto_close_timer();
    if (context_ == nullptr || auto_close_ms <= 0) {
        return;
    }

    auto timer = context_->timer().start_delayed(SUPER_MESSAGE_DIALOG_TIMEOUT_TIMER_NAME, auto_close_ms);
    if (!timer) {
        BROOKESIA_LOGW("Failed to start message dialog auto-close timer: %1%", timer.error());
        return;
    }
    message_dialog_auto_close_timer_id_ = *timer;
}


void ShellApp::stop_message_dialog_auto_close_timer()
{
    if (context_ != nullptr && message_dialog_auto_close_timer_id_ != core::INVALID_TIMER_ID) {
        (void)context_->timer().stop(message_dialog_auto_close_timer_id_);
    }
    message_dialog_auto_close_timer_id_ = core::INVALID_TIMER_ID;
}


void ShellApp::reset_message_dialog_state()
{
    active_message_dialog_request_id_.reset();
    active_message_dialog_owner_ = core::INVALID_APP_ID;
    active_message_dialog_options_ = {};
    message_dialog_closing_ = false;
}


std::expected<void, std::string> ShellApp::show_message_dialog(
    core::AppId app_id,
    core::MessageDialogRequestId request_id,
    const core::MessageDialogOptions &options
)
{
    if (active_message_dialog_request_id_.has_value()) {
        return std::unexpected("Message dialog is already active");
    }
    if (context_ == nullptr) {
        return std::unexpected("Shell app is not running");
    }

    reset_status_peek_session(false, true, "message dialog");
    if (gesture_tracking_ || gesture_exit_armed_ || gesture_exit_triggered_) {
        reset_gesture_indicator();
    }
    if (!system_ui_expanded_) {
        switch_display_to_gui_for_system_ui();
    }

    active_message_dialog_request_id_ = request_id;
    active_message_dialog_owner_ = app_id;
    active_message_dialog_options_ = options;
    message_dialog_needs_reset_ = false;

    auto mount_result = mount_message_dialog();
    if (!mount_result) {
        reset_message_dialog_state();
        return mount_result;
    }

    auto binding_result = refresh_message_dialog_bindings(options);
    if (!binding_result) {
        unmount_message_dialog();
        return binding_result;
    }

    start_message_dialog_auto_close_timer(options.auto_close_ms);
    return {};
}


std::expected<void, std::string> ShellApp::update_message_dialog(
    core::AppId app_id,
    core::MessageDialogRequestId request_id,
    const core::MessageDialogOptions &options
)
{
    if (!active_message_dialog_request_id_.has_value() || *active_message_dialog_request_id_ != request_id ||
            active_message_dialog_owner_ != app_id) {
        return std::unexpected("Message dialog is not active");
    }
    if (context_ == nullptr) {
        return std::unexpected("Shell app is not running");
    }

    active_message_dialog_options_ = options;
    auto binding_result = refresh_message_dialog_bindings(options);
    if (!binding_result) {
        return binding_result;
    }
    start_message_dialog_auto_close_timer(options.auto_close_ms);
    return {};
}


void ShellApp::hide_message_dialog(core::AppId app_id, core::MessageDialogRequestId request_id)
{
    if (!active_message_dialog_request_id_.has_value() || *active_message_dialog_request_id_ != request_id ||
            active_message_dialog_owner_ != app_id) {
        return;
    }

    message_dialog_closing_ = true;
    stop_message_dialog_auto_close_timer();
    if (context_ != nullptr && message_dialog_mounted_) {
        auto flow_result = context_->gui().trigger_screen_flow(
                               SUPER_MESSAGE_DIALOG_FLOW_ID,
                               SUPER_ACTION_MESSAGE_DIALOG_HIDE
                           );
        if (!flow_result) {
            BROOKESIA_LOGW("Failed to hide message dialog screen: %1%", flow_result.error());
        }
    }
    message_dialog_mounted_ = false;
    message_dialog_needs_reset_ = true;
    if (context_ != nullptr) {
        if (auto state_result = refresh_system_ui_state_bindings(); !state_result) {
            BROOKESIA_LOGW("Failed to refresh system UI mask after message dialog hide: %1%", state_result.error());
        }
    }
    reset_message_dialog_state();
}


void ShellApp::handle_message_dialog_idle()
{
    if (message_dialog_needs_reset_) {
        auto clear_result = clear_message_dialog_bindings();
        if (!clear_result) {
            BROOKESIA_LOGW("Failed to clear message dialog bindings: %1%", clear_result.error());
        }
        message_dialog_needs_reset_ = false;
    }
    if (!system_ui_expanded_) {
        restore_display_after_system_ui();
    }
}


void ShellApp::finish_message_dialog(int32_t button_index, core::MessageDialogCloseReason reason)
{
    if (!active_message_dialog_request_id_.has_value()) {
        return;
    }
    if (reason == core::MessageDialogCloseReason::Button &&
            (button_index < 0 || static_cast<size_t>(button_index) >= active_message_dialog_options_.buttons.size())) {
        return;
    }

    const auto request_id = *active_message_dialog_request_id_;
    const auto app_id = active_message_dialog_owner_;
    message_dialog_closing_ = true;
    stop_message_dialog_auto_close_timer();
    if (context_ != nullptr && message_dialog_mounted_) {
        auto flow_result = context_->gui().trigger_screen_flow(
                               SUPER_MESSAGE_DIALOG_FLOW_ID,
                               SUPER_ACTION_MESSAGE_DIALOG_HIDE
                           );
        if (!flow_result) {
            BROOKESIA_LOGW("Failed to hide message dialog screen: %1%", flow_result.error());
        }
    }
    message_dialog_mounted_ = false;
    message_dialog_needs_reset_ = true;
    if (context_ != nullptr) {
        if (auto state_result = refresh_system_ui_state_bindings(); !state_result) {
            BROOKESIA_LOGW("Failed to refresh system UI mask after message dialog finish: %1%", state_result.error());
        }
    }
    reset_message_dialog_state();

    auto result = owner_.complete_app_message_dialog(app_id, request_id, button_index, reason);
    if (!result) {
        BROOKESIA_LOGW("Failed to complete message dialog request: %1%", result.error());
    }
}


std::expected<void, std::string> ShellApp::set_system_ui_expanded(bool expanded)
{
    if (context_ == nullptr) {
        return {};
    }
    ++system_ui_animation_generation_;
    if (status_bar_animation_id_ != 0) {
        context_->gui().stop_animation(status_bar_animation_id_);
        status_bar_animation_id_ = 0;
    }

    if (expanded) {
        switch_display_to_gui_for_system_ui();
    }

    if (expanded == system_ui_expanded_) {
        auto state_result = refresh_system_ui_state_bindings();
        if (!state_result) {
            return state_result;
        }
        auto position_result = refresh_system_ui_position_bindings();
        if (!expanded) {
            restore_display_after_system_ui();
        }
        return position_result;
    }

    system_ui_expanded_ = expanded;
    auto binding_result = refresh_system_ui_state_bindings();
    if (!binding_result) {
        return binding_result;
    }

    const auto status_bar_target_y = expanded ?
                                     SUPER_STATUS_BAR_EXPANDED_Y :
                                     get_collapsed_status_bar_y(*context_);
    const auto generation = system_ui_animation_generation_;
    auto barrier = std::make_shared<AnimationCompletionBarrier>([this, generation, expanded]() {
        if (context_ == nullptr || generation != system_ui_animation_generation_) {
            return;
        }
        status_bar_animation_id_ = 0;
        auto result = refresh_system_ui_position_bindings();
        if (!result) {
            BROOKESIA_LOGW("Failed to finalize system UI animation bindings: %1%", result.error());
        }
        if (!expanded) {
            restore_display_after_system_ui();
        }
    });
    barrier->add();
    auto status_animation_result = context_->gui().start_view_animation_with_result(
                                       SUPER_STATUS_BAR_PATH,
                                       make_position_animation(gui::AnimationProperty::Y, status_bar_target_y),
    [barrier]() {
        barrier->complete();
    }
                                   );
    if (!status_animation_result) {
        (void)refresh_system_ui_position_bindings();
        if (!expanded) {
            restore_display_after_system_ui();
        }
        return std::unexpected(status_animation_result.error());
    }
    status_bar_animation_id_ = status_animation_result->subscription_id;
    barrier->arm();
    return {};
}
}
