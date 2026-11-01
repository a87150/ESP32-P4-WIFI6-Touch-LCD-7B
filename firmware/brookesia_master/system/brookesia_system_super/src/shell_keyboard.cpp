/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "private/shell_impl.hpp"

namespace esp_brookesia::system::super {



void ShellApp::unmount_keyboard()
{
    keyboard_action_connections_.clear();
    stop_keyboard_animations();
    if (context_ != nullptr && keyboard_mounted_) {
        std::vector<gui::BindingValueUpdate> updates;
        add_binding_update(updates, SUPER_KEYBOARD_INPUT_BAR_PATH, "inputHidden", "true");
        add_binding_update(updates, SUPER_KEYBOARD_INPUT_KEYBOARD_PANEL_PATH, "keyboardHidden", "true");
        core::GuiBatchCommand command;
        command.type = core::GuiBatchCommand::Type::SetBindings;
        command.binding_updates = std::move(updates);
        auto result = owner_.gui_execute_batch(owner_.shell_app_id_, {std::move(command)});
        if (!result || !result->success) {
            BROOKESIA_LOGW("Failed to reset keyboard hidden state before unmount");
        }
        auto flow_result = context_->gui().trigger_screen_flow(SUPER_KEYBOARD_FLOW_ID, SUPER_ACTION_KEYBOARD_HIDE);
        if (!flow_result) {
            BROOKESIA_LOGW("Failed to hide keyboard screen: %1%", flow_result.error());
        }
    }
    keyboard_mounted_ = false;
    reset_keyboard_state();
}


std::expected<void, std::string> ShellApp::refresh_keyboard_bindings(
    const core::KeyboardRequestOptions &options
)
{
    if (context_ == nullptr) {
        return std::unexpected("Shell app is not running");
    }

    active_keyboard_password_requested_ = options.password;
    active_keyboard_password_hidden_ = options.password;

    std::vector<gui::BindingValueUpdate> updates;
    add_binding_update(updates, SUPER_KEYBOARD_INPUT_TEXT_PATH, "text", options.initial_text);
    add_binding_update(updates, SUPER_KEYBOARD_INPUT_TEXT_PATH, "placeholder", options.placeholder);
    add_binding_update(updates, SUPER_KEYBOARD_INPUT_TEXT_PATH, "password", bool_to_binding(active_keyboard_password_hidden_));
    add_binding_update(updates, SUPER_KEYBOARD_INPUT_TEXT_PATH, "maxLength", int_to_binding(options.max_length));
    add_binding_update(
        updates,
        SUPER_KEYBOARD_PASSWORD_TOGGLE_PATH,
        "passwordToggleHidden",
        bool_to_binding(!active_keyboard_password_requested_)
    );
    add_binding_update(
        updates,
        SUPER_KEYBOARD_PASSWORD_TOGGLE_ICON_PATH,
        "passwordIcon",
        active_keyboard_password_hidden_ ? SUPER_KEYBOARD_EYE_HIDE_IMAGE_ID : SUPER_KEYBOARD_EYE_SHOW_IMAGE_ID
    );
    add_binding_update(updates, SUPER_KEYBOARD_INPUT_KEYBOARD_PATH, "mode", options.mode);
    add_binding_update(
        updates,
        SUPER_KEYBOARD_INPUT_KEYBOARD_PATH,
        "allowedModes",
        join_keyboard_modes(options.allowed_modes.empty() ? default_keyboard_modes() : options.allowed_modes)
    );
    return context_->gui().set_binding_values(updates);
}


std::expected<void, std::string> ShellApp::refresh_keyboard_password_bindings()
{
    if (context_ == nullptr) {
        return std::unexpected("Shell app is not running");
    }

    std::vector<gui::BindingValueUpdate> updates;
    add_binding_update(
        updates,
        SUPER_KEYBOARD_INPUT_TEXT_PATH,
        "password",
        bool_to_binding(active_keyboard_password_hidden_)
    );
    add_binding_update(
        updates,
        SUPER_KEYBOARD_PASSWORD_TOGGLE_PATH,
        "passwordToggleHidden",
        bool_to_binding(!active_keyboard_password_requested_)
    );
    add_binding_update(
        updates,
        SUPER_KEYBOARD_PASSWORD_TOGGLE_ICON_PATH,
        "passwordIcon",
        active_keyboard_password_hidden_ ? SUPER_KEYBOARD_EYE_HIDE_IMAGE_ID : SUPER_KEYBOARD_EYE_SHOW_IMAGE_ID
    );
    return context_->gui().set_binding_values(updates);
}


std::expected<void, std::string> ShellApp::start_keyboard_show_animation()
{
    if (context_ == nullptr || !keyboard_mounted_) {
        return {};
    }

    stop_keyboard_animations();
    const auto input_frame = context_->gui().get_view_frame(SUPER_KEYBOARD_INPUT_BAR_PATH);
    const auto keyboard_frame = context_->gui().get_view_frame(SUPER_KEYBOARD_INPUT_KEYBOARD_PANEL_PATH);
    if (!input_frame || !keyboard_frame) {
        return std::unexpected("Keyboard views are not mounted");
    }

    const auto final_input_y = input_frame->y;
    const auto final_keyboard_y = keyboard_frame->y;
    const auto hidden_input_y = -std::max<int32_t>(input_frame->height, 1) - 2;
    const auto hidden_keyboard_y = owner_.get_environment().height_px + std::max<int32_t>(keyboard_frame->height, 1);

    const auto generation = keyboard_animation_generation_;
    auto barrier = std::make_shared<AnimationCompletionBarrier>([this, generation]() {
        if (context_ == nullptr || generation != keyboard_animation_generation_) {
            return;
        }
        keyboard_input_animation_id_ = 0;
        keyboard_body_animation_id_ = 0;
        keyboard_accepting_actions_ = true;
    });

    barrier->add();
    auto input_animation = context_->gui().start_view_animation_with_result(
                               SUPER_KEYBOARD_INPUT_BAR_PATH,
                               make_keyboard_animation(gui::AnimationProperty::Y, hidden_input_y, final_input_y),
    [barrier]() {
        barrier->complete();
    }
                           );
    if (!input_animation) {
        return std::unexpected(input_animation.error());
    }

    barrier->add();
    auto keyboard_animation = context_->gui().start_view_animation_with_result(
                                  SUPER_KEYBOARD_INPUT_KEYBOARD_PANEL_PATH,
                                  make_keyboard_animation(gui::AnimationProperty::Y, hidden_keyboard_y, final_keyboard_y),
    [barrier]() {
        barrier->complete();
    }
                              );
    if (!keyboard_animation) {
        context_->gui().stop_animation(input_animation->subscription_id);
        return std::unexpected(keyboard_animation.error());
    }

    std::vector<gui::BindingValueUpdate> visibility_updates;
    add_binding_update(visibility_updates, SUPER_KEYBOARD_INPUT_BAR_PATH, "inputHidden", "false");
    add_binding_update(visibility_updates, SUPER_KEYBOARD_INPUT_KEYBOARD_PANEL_PATH, "keyboardHidden", "false");
    auto visibility_result = context_->gui().set_binding_values(visibility_updates);
    if (!visibility_result) {
        context_->gui().stop_animation(input_animation->subscription_id);
        context_->gui().stop_animation(keyboard_animation->subscription_id);
        return visibility_result;
    }

    keyboard_input_animation_id_ = input_animation->subscription_id;
    keyboard_body_animation_id_ = keyboard_animation->subscription_id;
    barrier->arm();
    return {};
}


void ShellApp::start_keyboard_hide_animation(std::function<void()> completed_handler)
{
    if (context_ == nullptr || !keyboard_mounted_) {
        if (completed_handler) {
            completed_handler();
        }
        return;
    }

    keyboard_closing_ = true;
    stop_keyboard_animations();
    const auto input_frame = context_->gui().get_view_frame(SUPER_KEYBOARD_INPUT_BAR_PATH);
    const auto keyboard_frame = context_->gui().get_view_frame(SUPER_KEYBOARD_INPUT_KEYBOARD_PANEL_PATH);
    if (!input_frame || !keyboard_frame) {
        unmount_keyboard();
        if (completed_handler) {
            completed_handler();
        }
        return;
    }

    auto completed_handler_ptr = std::make_shared<std::function<void()>>(std::move(completed_handler));
    const auto hidden_input_y = -std::max<int32_t>(input_frame->height, 1) - 2;
    const auto hidden_keyboard_y = owner_.get_environment().height_px + std::max<int32_t>(keyboard_frame->height, 1);
    const auto generation = keyboard_animation_generation_;
    auto barrier = std::make_shared<AnimationCompletionBarrier>(
    [this, generation, completed_handler_ptr]() {
        if (context_ == nullptr || generation != keyboard_animation_generation_) {
            return;
        }
        keyboard_input_animation_id_ = 0;
        keyboard_body_animation_id_ = 0;
        unmount_keyboard();
        if (*completed_handler_ptr) {
            (*completed_handler_ptr)();
        }
    }
                   );

    barrier->add();
    auto input_animation = context_->gui().start_view_animation_with_result(
                               SUPER_KEYBOARD_INPUT_BAR_PATH,
                               make_keyboard_animation(gui::AnimationProperty::Y, hidden_input_y),
    [barrier]() {
        barrier->complete();
    }
                           );
    if (!input_animation) {
        unmount_keyboard();
        if (*completed_handler_ptr) {
            (*completed_handler_ptr)();
        }
        return;
    }

    barrier->add();
    auto keyboard_animation = context_->gui().start_view_animation_with_result(
                                  SUPER_KEYBOARD_INPUT_KEYBOARD_PANEL_PATH,
                                  make_keyboard_animation(gui::AnimationProperty::Y, hidden_keyboard_y),
    [barrier]() {
        barrier->complete();
    }
                              );
    if (!keyboard_animation) {
        context_->gui().stop_animation(input_animation->subscription_id);
        unmount_keyboard();
        if (*completed_handler_ptr) {
            (*completed_handler_ptr)();
        }
        return;
    }

    keyboard_input_animation_id_ = input_animation->subscription_id;
    keyboard_body_animation_id_ = keyboard_animation->subscription_id;
    barrier->arm();
}


void ShellApp::stop_keyboard_animations()
{
    ++keyboard_animation_generation_;
    if (context_ != nullptr && keyboard_input_animation_id_ != 0) {
        context_->gui().stop_animation(keyboard_input_animation_id_);
    }
    if (context_ != nullptr && keyboard_body_animation_id_ != 0) {
        context_->gui().stop_animation(keyboard_body_animation_id_);
    }
    keyboard_input_animation_id_ = 0;
    keyboard_body_animation_id_ = 0;
}


void ShellApp::reset_keyboard_state()
{
    active_keyboard_request_id_.reset();
    active_keyboard_owner_ = core::INVALID_APP_ID;
    active_keyboard_text_.clear();
    keyboard_accepting_actions_ = false;
    active_keyboard_password_requested_ = false;
    active_keyboard_password_hidden_ = false;
    keyboard_closing_ = false;
}


std::expected<void, std::string> ShellApp::show_keyboard(
    core::AppId app_id,
    core::KeyboardRequestId request_id,
    const core::KeyboardRequestOptions &options
)
{
    if (active_keyboard_request_id_.has_value()) {
        return std::unexpected("Keyboard input is already active");
    }
    if (context_ == nullptr) {
        return std::unexpected("Shell app is not running");
    }
    auto normalized_options = normalize_keyboard_options(options);
    if (!normalized_options) {
        return std::unexpected(normalized_options.error());
    }

    active_keyboard_request_id_ = request_id;
    active_keyboard_owner_ = app_id;
    active_keyboard_text_ = normalized_options->initial_text;

    auto mount_result = mount_keyboard();
    if (!mount_result) {
        reset_keyboard_state();
        return mount_result;
    }

    auto binding_result = refresh_keyboard_bindings(*normalized_options);
    if (!binding_result) {
        unmount_keyboard();
        return binding_result;
    }

    auto animation_result = start_keyboard_show_animation();
    if (!animation_result) {
        BROOKESIA_LOGW("Failed to start keyboard show animation: %1%", animation_result.error());
        keyboard_accepting_actions_ = true;
    }
    return {};
}


void ShellApp::hide_keyboard(core::AppId app_id, core::KeyboardRequestId request_id)
{
    if (!active_keyboard_request_id_.has_value() || *active_keyboard_request_id_ != request_id ||
            active_keyboard_owner_ != app_id) {
        return;
    }
    start_keyboard_hide_animation({});
}


void ShellApp::finish_keyboard(bool confirmed)
{
    if (!active_keyboard_request_id_.has_value()) {
        return;
    }

    const auto request_id = *active_keyboard_request_id_;
    const auto app_id = active_keyboard_owner_;
    auto text = confirmed ? active_keyboard_text_ : std::string();
    start_keyboard_hide_animation([this, app_id, request_id, confirmed, text = std::move(text)]() mutable {
        auto result = owner_.complete_app_keyboard(app_id, request_id, confirmed, std::move(text));
        if (!result)
        {
            BROOKESIA_LOGW("Failed to complete keyboard request: %1%", result.error());
        }
    });
}


std::expected<void, std::string> ShellApp::mount_message_dialog()
{
    if (context_ == nullptr) {
        return std::unexpected("Shell app is not running");
    }

    if (message_dialog_action_connections_.empty()) {
        auto subscribe_button = [this](const char *action, int32_t button_index) {
            message_dialog_action_connections_.push_back(context_->gui().subscribe_action(
                        action,
            [this, button_index](const gui::Event &) {
                if (message_dialog_closing_) {
                    return;
                }
                if (button_index < 0 ||
                        static_cast<size_t>(button_index) >= active_message_dialog_options_.buttons.size()) {
                    return;
                }
                finish_message_dialog(button_index, core::MessageDialogCloseReason::Button);
            }
                    ));
        };
        subscribe_button(SUPER_ACTION_MESSAGE_DIALOG_BUTTON0, 0);
        subscribe_button(SUPER_ACTION_MESSAGE_DIALOG_BUTTON1, 1);
        subscribe_button(SUPER_ACTION_MESSAGE_DIALOG_BUTTON2, 2);
    }

    auto flow_result = context_->gui().trigger_screen_flow(
                           SUPER_MESSAGE_DIALOG_FLOW_ID,
                           SUPER_ACTION_MESSAGE_DIALOG_SHOW
                       );
    if (!flow_result) {
        return std::unexpected(flow_result.error());
    }
    message_dialog_mounted_ = true;
    message_dialog_closing_ = false;
    if (auto state_result = refresh_system_ui_state_bindings(); !state_result) {
        BROOKESIA_LOGW("Failed to refresh system UI mask after message dialog mount: %1%", state_result.error());
    }
    return {};
}
}
