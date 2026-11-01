/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "private/shell_impl.hpp"

namespace esp_brookesia::system::super {



std::expected<void, std::string> ShellApp::refresh_system_ui_bindings()
{
    auto result = refresh_system_ui_state_bindings();
    if (!result) {
        return result;
    }
    return refresh_system_ui_position_bindings();
}


std::expected<void, std::string> ShellApp::refresh_system_ui_position_bindings()
{
    if (context_ == nullptr) {
        return {};
    }

    const auto status_bar_target_y = system_ui_expanded_ ?
                                     SUPER_STATUS_BAR_EXPANDED_Y :
                                     get_collapsed_status_bar_y(*context_);
    std::vector<gui::BindingValueUpdate> updates;
    add_binding_update(
        updates,
        SUPER_STATUS_BAR_PATH,
        "system_ui_status_y",
        std::to_string(status_bar_target_y)
    );
    return context_->gui().set_binding_values(updates);
}


std::expected<void, std::string> ShellApp::refresh_system_ui_state_bindings()
{
    if (context_ == nullptr) {
        return {};
    }

    const bool mask_visible = !foreground_is_shell_ &&
                              (system_ui_expanded_ || status_peek_tracking_ || status_peek_visible_ ||
                               gesture_tracking_ || gesture_exit_armed_ || gesture_exit_triggered_ ||
                               message_dialog_mounted_);
    std::vector<gui::BindingValueUpdate> updates;
    add_binding_update(
        updates,
        SUPER_SYSTEM_UI_MASK_PATH,
        "hidden",
        bool_to_binding(!mask_visible)
    );
    return context_->gui().set_binding_values(updates);
}


std::expected<void, std::string> ShellApp::show_launch_overlay(
    const core::AppInfo &app,
    const std::optional<gui::ViewFrame> &origin,
    std::function<void()> completed_handler
)
{
    if (context_ == nullptr) {
        return std::unexpected("Shell app is not running");
    }
    if (launch_overlay_active_) {
        return std::unexpected("App launch overlay is already active");
    }

    const auto environment = owner_.get_environment();
    const auto start_frame = origin.value_or(make_fallback_origin(environment));
    const auto screen_width = std::max<int32_t>(environment.width_px, SUPER_APP_LAUNCH_FINAL_ICON_SIZE);
    const auto screen_height = std::max<int32_t>(environment.height_px, SUPER_APP_LAUNCH_FINAL_ICON_SIZE);
    const auto final_icon_size = std::max<int32_t>(SUPER_APP_LAUNCH_FINAL_ICON_SIZE, 1);
    const auto final_icon_x = (screen_width - final_icon_size) / 2;
    const auto final_icon_y = (screen_height - final_icon_size) / 2;
    const bool has_icon = core::has_app_icon_image(app.manifest);
    const auto display_name = get_app_display_name(app);
    const auto initial_surface_x = SUPER_APP_LAUNCH_ANIMATION_ENABLED ? start_frame.x : 0;
    const auto initial_surface_y = SUPER_APP_LAUNCH_ANIMATION_ENABLED ? start_frame.y : 0;
    const auto initial_surface_width = SUPER_APP_LAUNCH_ANIMATION_ENABLED ? start_frame.width : screen_width;
    const auto initial_surface_height = SUPER_APP_LAUNCH_ANIMATION_ENABLED ? start_frame.height : screen_height;
    const auto initial_icon_x = SUPER_APP_LAUNCH_ANIMATION_ENABLED ? start_frame.x : final_icon_x;
    const auto initial_icon_y = SUPER_APP_LAUNCH_ANIMATION_ENABLED ? start_frame.y : final_icon_y;
    const auto initial_icon_width = SUPER_APP_LAUNCH_ANIMATION_ENABLED ? start_frame.width : final_icon_size;
    const auto initial_icon_height = SUPER_APP_LAUNCH_ANIMATION_ENABLED ? start_frame.height : final_icon_size;

    std::vector<gui::BindingValueUpdate> updates;
    add_binding_update(updates, SUPER_APP_MODAL_PATH, "hidden", "false");
    add_binding_update(updates, SUPER_APP_MODAL_LAUNCH_PATH, "hidden", "false");
    add_binding_update(updates, SUPER_APP_MODAL_LOADING_PATH, "hidden", "true");
    add_binding_update(updates, SUPER_APP_MODAL_LAUNCH_SURFACE_PATH, "x", int_to_binding(initial_surface_x));
    add_binding_update(updates, SUPER_APP_MODAL_LAUNCH_SURFACE_PATH, "y", int_to_binding(initial_surface_y));
    add_binding_update(updates, SUPER_APP_MODAL_LAUNCH_SURFACE_PATH, "width", int_to_binding(initial_surface_width));
    add_binding_update(updates, SUPER_APP_MODAL_LAUNCH_SURFACE_PATH, "height", int_to_binding(initial_surface_height));
    add_binding_update(updates, SUPER_APP_MODAL_LAUNCH_ICON_BOX_PATH, "hidden", "false");
    add_binding_update(updates, SUPER_APP_MODAL_LAUNCH_ICON_BOX_PATH, "x", int_to_binding(initial_icon_x));
    add_binding_update(updates, SUPER_APP_MODAL_LAUNCH_ICON_BOX_PATH, "y", int_to_binding(initial_icon_y));
    add_binding_update(updates, SUPER_APP_MODAL_LAUNCH_ICON_BOX_PATH, "width", int_to_binding(initial_icon_width));
    add_binding_update(updates, SUPER_APP_MODAL_LAUNCH_ICON_BOX_PATH, "height", int_to_binding(initial_icon_height));
    add_binding_update(
        updates,
        SUPER_APP_MODAL_LAUNCH_ICON_IMAGE_PATH,
        "src",
        has_icon ? core::resolve_app_icon_resource_id(app.manifest) : SUPER_DEFAULT_IMAGE_ID
    );
    add_binding_update(updates, SUPER_APP_MODAL_LAUNCH_ICON_IMAGE_PATH, "hidden", bool_to_binding(!has_icon));
    add_binding_update(
        updates,
        SUPER_APP_MODAL_LAUNCH_FALLBACK_LABEL_PATH,
        "text",
        get_app_icon_text(display_name)
    );
    add_binding_update(
        updates,
        SUPER_APP_MODAL_LAUNCH_FALLBACK_LABEL_PATH,
        "hidden",
        bool_to_binding(has_icon)
    );
    auto binding_result = context_->gui().set_binding_values(updates);
    if (!binding_result) {
        return binding_result;
    }

    launch_overlay_active_ = true;
    if (!SUPER_APP_LAUNCH_ANIMATION_ENABLED) {
        if (completed_handler) {
            completed_handler();
        }
        return {};
    }

    const std::array<std::pair<const char *, gui::Animation>, 8> animations = {{
            {SUPER_APP_MODAL_LAUNCH_SURFACE_PATH, make_modal_animation(gui::AnimationProperty::X, 0)},
            {SUPER_APP_MODAL_LAUNCH_SURFACE_PATH, make_modal_animation(gui::AnimationProperty::Y, 0)},
            {SUPER_APP_MODAL_LAUNCH_SURFACE_PATH, make_modal_animation(gui::AnimationProperty::Width, screen_width)},
            {SUPER_APP_MODAL_LAUNCH_SURFACE_PATH, make_modal_animation(gui::AnimationProperty::Height, screen_height)},
            {SUPER_APP_MODAL_LAUNCH_ICON_BOX_PATH, make_modal_animation(gui::AnimationProperty::X, final_icon_x)},
            {SUPER_APP_MODAL_LAUNCH_ICON_BOX_PATH, make_modal_animation(gui::AnimationProperty::Y, final_icon_y)},
            {SUPER_APP_MODAL_LAUNCH_ICON_BOX_PATH, make_modal_animation(gui::AnimationProperty::Width, final_icon_size)},
            {SUPER_APP_MODAL_LAUNCH_ICON_BOX_PATH, make_modal_animation(gui::AnimationProperty::Height, final_icon_size)},
        }
    };
    auto barrier = std::make_shared<AnimationCompletionBarrier>(std::move(completed_handler));
    int32_t started_count = 0;
    for (const auto &[path, animation] : animations) {
        barrier->add();
        auto animation_result = context_->gui().start_view_animation_with_result(
                                    path,
                                    animation,
        [barrier]() {
            barrier->complete();
        }
                                );
        if (animation_result) {
            ++started_count;
            continue;
        }
        BROOKESIA_LOGW("Failed to start launch animation: path(%1%), error(%2%)", path, animation_result.error());
        barrier->complete();
    }
    if (started_count == 0) {
        launch_overlay_active_ = false;
        auto refresh_result = refresh_app_modal_for_loading();
        if (!refresh_result) {
            BROOKESIA_LOGW("Failed to restore loading modal after launch animation failure: %1%", refresh_result.error());
        }
        return std::unexpected("No app launch animations started");
    }
    barrier->arm();
    return {};
}


void ShellApp::finish_launch_overlay()
{
    launch_overlay_active_ = false;
    cancel_pending_launch();
    auto result = apply_app_modal_after_launch();
    if (!result) {
        BROOKESIA_LOGW("Failed to finish launch overlay: %1%", result.error());
    }
}


void ShellApp::cancel_pending_launch()
{
    if (context_ != nullptr && launch_hold_timer_id_ != core::INVALID_TIMER_ID) {
        (void)context_->timer().stop(launch_hold_timer_id_);
    }
    launch_hold_timer_id_ = core::INVALID_TIMER_ID;
    pending_launch_app_id_.reset();
    launch_request_started_at_.reset();
}


void ShellApp::start_app_after_launch(core::AppId app_id)
{
    const auto request_started_at = launch_request_started_at_;
    pending_launch_app_id_.reset();
    launch_hold_timer_id_ = core::INVALID_TIMER_ID;
    if (!launch_overlay_active_) {
        BROOKESIA_LOGW("Skip app launch after overlay completion: app_id(%1%)", app_id);
        return;
    }
    if (context_ == nullptr) {
        BROOKESIA_LOGW("Skip app launch after overlay completion because Shell is not running: app_id(%1%)", app_id);
        finish_launch_overlay();
        return;
    }

    auto modal_result = prepare_app_modal_for_app_start();
    if (!modal_result) {
        BROOKESIA_LOGW("Failed to prepare app modal before app start: %1%", modal_result.error());
    }

    const auto app_start_started_at = SteadyClock::now();
    auto result = owner_.activate_app(app_id, core::System::AppStartOptions{});
    const auto app_start_ended_at = SteadyClock::now();
    finish_launch_overlay();
    SYSTEM_SUPER_PROFILE_LOGI(
        "Shell app launch profile: app_id(%1%), pre_start_ms(%2%), start_app_ms(%3%), total_ms(%4%)",
        app_id,
        request_started_at.has_value() ? elapsed_ms_since(*request_started_at, app_start_started_at) : 0,
        elapsed_ms_since(app_start_started_at, app_start_ended_at),
        request_started_at.has_value() ? elapsed_ms_since(*request_started_at, app_start_ended_at) : 0
    );
    if (!result) {
        BROOKESIA_LOGW("Failed to launch app: app_id(%1%), error(%2%)", app_id, result.error());
    }
}


std::expected<void, std::string> ShellApp::prepare_app_modal_for_app_start()
{
    const auto environment = owner_.get_environment();
    const auto final_icon_size = std::max<int32_t>(SUPER_APP_LAUNCH_FINAL_ICON_SIZE, 1);
    const auto screen_width = std::max<int32_t>(environment.width_px, final_icon_size);
    const auto screen_height = std::max<int32_t>(environment.height_px, final_icon_size);
    const auto final_icon_x = (screen_width - final_icon_size) / 2;
    const auto final_icon_y = (screen_height - final_icon_size) / 2;

    std::vector<gui::BindingValueUpdate> updates;
    add_binding_update(updates, SUPER_APP_MODAL_PATH, "hidden", "false");
    add_binding_update(updates, SUPER_APP_MODAL_LAUNCH_PATH, "hidden", "false");
    add_binding_update(updates, SUPER_APP_MODAL_LOADING_PATH, "hidden", "true");
    add_binding_update(updates, SUPER_APP_MODAL_LAUNCH_SURFACE_PATH, "x", "0");
    add_binding_update(updates, SUPER_APP_MODAL_LAUNCH_SURFACE_PATH, "y", "0");
    add_binding_update(updates, SUPER_APP_MODAL_LAUNCH_SURFACE_PATH, "width", int_to_binding(screen_width));
    add_binding_update(updates, SUPER_APP_MODAL_LAUNCH_SURFACE_PATH, "height", int_to_binding(screen_height));
    add_binding_update(updates, SUPER_APP_MODAL_LAUNCH_ICON_BOX_PATH, "hidden", "false");
    add_binding_update(updates, SUPER_APP_MODAL_LAUNCH_ICON_BOX_PATH, "x", int_to_binding(final_icon_x));
    add_binding_update(updates, SUPER_APP_MODAL_LAUNCH_ICON_BOX_PATH, "y", int_to_binding(final_icon_y));
    add_binding_update(updates, SUPER_APP_MODAL_LAUNCH_ICON_BOX_PATH, "width", int_to_binding(final_icon_size));
    add_binding_update(updates, SUPER_APP_MODAL_LAUNCH_ICON_BOX_PATH, "height", int_to_binding(final_icon_size));

    core::GuiBatchCommand command;
    command.type = core::GuiBatchCommand::Type::SetBindings;
    command.binding_updates = std::move(updates);
    auto result = owner_.gui_execute_batch(owner_.shell_app_id_, {std::move(command)});
    if (!result) {
        return std::unexpected(result.error());
    }
    if (!result->success) {
        return std::unexpected("Failed to prepare app modal for app start batch");
    }
    return {};
}


std::expected<void, std::string> ShellApp::apply_app_modal_after_launch()
{
    const bool show_loading = !loading_owners_.empty();
    std::vector<gui::BindingValueUpdate> updates;
    add_binding_update(updates, SUPER_APP_MODAL_PATH, "hidden", bool_to_binding(!show_loading));
    add_binding_update(updates, SUPER_APP_MODAL_LAUNCH_PATH, "hidden", "true");
    add_binding_update(updates, SUPER_APP_MODAL_LOADING_PATH, "hidden", bool_to_binding(!show_loading));
    add_binding_update(updates, SUPER_APP_MODAL_LAUNCH_ICON_BOX_PATH, "hidden", "true");
    add_binding_update(updates, SUPER_APP_MODAL_LAUNCH_ICON_IMAGE_PATH, "hidden", "true");
    add_binding_update(updates, SUPER_APP_MODAL_LAUNCH_FALLBACK_LABEL_PATH, "hidden", "true");

    core::GuiBatchCommand command;
    command.type = core::GuiBatchCommand::Type::SetBindings;
    command.binding_updates = std::move(updates);
    auto result = owner_.gui_execute_batch(owner_.shell_app_id_, {std::move(command)});
    if (!result) {
        return std::unexpected(result.error());
    }
    if (!result->success) {
        return std::unexpected("Failed to apply app modal after launch batch");
    }
    return {};
}


void ShellApp::schedule_app_start_after_launch(core::AppId app_id)
{
    const auto hold_ms = SUPER_APP_LAUNCH_ANIMATION_ENABLED ? SUPER_APP_LAUNCH_POST_COMPLETE_HOLD_MS :
                         std::max(SUPER_APP_LAUNCH_POST_COMPLETE_HOLD_MS, SUPER_APP_LAUNCH_NO_ANIMATION_MIN_HOLD_MS);
    if (!launch_request_started_at_.has_value()) {
        launch_request_started_at_ = SteadyClock::now();
    }
    if (hold_ms <= 0 || context_ == nullptr) {
        start_app_after_launch(app_id);
        return;
    }

    if (launch_hold_timer_id_ != core::INVALID_TIMER_ID) {
        (void)context_->timer().stop(launch_hold_timer_id_);
    }
    launch_hold_timer_id_ = core::INVALID_TIMER_ID;
    pending_launch_app_id_.reset();
    pending_launch_app_id_ = app_id;
    auto timer = context_->timer().start_delayed(
                     SUPER_APP_LAUNCH_HOLD_TIMER_NAME,
                     hold_ms
                 );
    if (!timer) {
        BROOKESIA_LOGW("Failed to start app launch hold timer: %1%", timer.error());
        start_app_after_launch(app_id);
        return;
    }
    launch_hold_timer_id_ = *timer;
    SYSTEM_SUPER_PROFILE_LOGI(
        "Shell app launch profile: app_id(%1%), wait_before_start_ms(%2%), hold_ms(%3%)",
        app_id,
        launch_request_started_at_.has_value() ? elapsed_ms_since(*launch_request_started_at_) : 0,
        hold_ms
    );
}


std::expected<void, std::string> ShellApp::show_loading(core::AppId app_id)
{
    auto it = std::remove(loading_owners_.begin(), loading_owners_.end(), app_id);
    loading_owners_.erase(it, loading_owners_.end());
    loading_owners_.push_back(app_id);
    if (launch_overlay_active_) {
        return {};
    }
    return show_loading_overlay();
}


void ShellApp::hide_loading(core::AppId app_id)
{
    auto it = std::remove(loading_owners_.begin(), loading_owners_.end(), app_id);
    loading_owners_.erase(it, loading_owners_.end());
    if (launch_overlay_active_) {
        return;
    }
    auto result = refresh_app_modal_for_loading();
    if (!result) {
        BROOKESIA_LOGW("Failed to hide loading overlay: %1%", result.error());
    }
}


std::expected<void, std::string> ShellApp::show_loading_overlay()
{
    if (context_ == nullptr) {
        return std::unexpected("Shell app is not running");
    }

    std::vector<gui::BindingValueUpdate> updates;
    add_binding_update(updates, SUPER_APP_MODAL_PATH, "hidden", "false");
    add_binding_update(updates, SUPER_APP_MODAL_LAUNCH_PATH, "hidden", "true");
    add_binding_update(updates, SUPER_APP_MODAL_LOADING_PATH, "hidden", "false");
    return context_->gui().set_binding_values(updates);
}


std::expected<void, std::string> ShellApp::hide_app_modal()
{
    if (context_ == nullptr) {
        return {};
    }

    std::vector<gui::BindingValueUpdate> updates;
    add_binding_update(updates, SUPER_APP_MODAL_PATH, "hidden", "true");
    add_binding_update(updates, SUPER_APP_MODAL_LAUNCH_PATH, "hidden", "true");
    add_binding_update(updates, SUPER_APP_MODAL_LOADING_PATH, "hidden", "true");
    return context_->gui().set_binding_values(updates);
}


std::expected<void, std::string> ShellApp::refresh_app_modal_for_loading()
{
    if (!loading_owners_.empty()) {
        return show_loading_overlay();
    }
    return hide_app_modal();
}


std::expected<void, std::string> ShellApp::mount_keyboard()
{
    if (context_ == nullptr) {
        return std::unexpected("Shell app is not running");
    }
    if (keyboard_mounted_) {
        return {};
    }

    if (keyboard_action_connections_.empty()) {
        auto subscribe_keyboard_action = [this](const char *action, bool confirmed) {
            keyboard_action_connections_.push_back(context_->gui().subscribe_action(
                    action,
            [this, confirmed](const gui::Event &) {
                if (keyboard_closing_ || !keyboard_accepting_actions_) {
                    return;
                }
                finish_keyboard(confirmed);
            }
                                                   ));
        };
        keyboard_action_connections_.push_back(context_->gui().subscribe_action(
                SUPER_ACTION_KEYBOARD_TEXT_CHANGED,
        [this](const gui::Event & event) {
            if (keyboard_closing_) {
                return;
            }
            auto text = event.get_string("text");
            if (text.has_value()) {
                active_keyboard_text_ = std::string(*text);
            }
        }
                                               ));
        keyboard_action_connections_.push_back(context_->gui().subscribe_action(
                SUPER_ACTION_KEYBOARD_TOGGLE_PASSWORD,
        [this](const gui::Event &) {
            if (keyboard_closing_ || !active_keyboard_request_id_.has_value() ||
                    !active_keyboard_password_requested_) {
                return;
            }
            active_keyboard_password_hidden_ = !active_keyboard_password_hidden_;
            auto result = refresh_keyboard_password_bindings();
            if (!result) {
                BROOKESIA_LOGW("Failed to toggle keyboard password visibility: %1%", result.error());
            }
        }
                                               ));
        subscribe_keyboard_action(SUPER_ACTION_KEYBOARD_SUBMIT_INPUT, true);
        subscribe_keyboard_action(SUPER_ACTION_KEYBOARD_SUBMIT_KEYBOARD, true);
        subscribe_keyboard_action(SUPER_ACTION_KEYBOARD_CANCEL_INPUT, false);
        subscribe_keyboard_action(SUPER_ACTION_KEYBOARD_CANCEL_KEYBOARD, false);
        subscribe_keyboard_action(SUPER_ACTION_KEYBOARD_CANCEL_BACKDROP, false);
    }

    auto flow_result = context_->gui().trigger_screen_flow(SUPER_KEYBOARD_FLOW_ID, SUPER_ACTION_KEYBOARD_SHOW);
    if (!flow_result) {
        return std::unexpected(flow_result.error());
    }
    keyboard_mounted_ = true;
    keyboard_closing_ = false;
    keyboard_accepting_actions_ = false;
    return {};
}
}
