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
#include "brookesia/system_super/system.hpp"

namespace esp_brookesia::system::super {

bool System::is_shell_active() const
{
    const auto active = get_active_app();
    if (active.has_value()) {
        return active->app_id == shell_app_id_;
    }
    const auto shell = get_app(shell_app_id_);
    return shell.has_value() && (shell->state == core::AppState::Running || shell->state == core::AppState::Paused);
}

std::expected<void, std::string> System::open_shell_page(ShellPage page)
{
    pending_shell_page_ = page;

    const auto active = get_active_app();
    if (active.has_value() && active->app_id != shell_app_id_) {
        // Keep the app resident in the background instead of destroying it: the
        // Shell shows again, the app keeps its state and can be resumed from the
        // status bar or the recents page.
        auto pause_result = pause_app(active->app_id);
        if (!pause_result) {
            BROOKESIA_LOGW(
                "Failed to pause app while opening Shell page, stopping it instead: app_id(%1%), reason(%2%)",
                active->app_id, pause_result.error()
            );
            return stop_app(active->app_id);
        }
    }
    return restore_shell();
}

std::expected<void, std::string> System::open_app_launcher()
{
    return open_shell_page(ShellPage::AppLauncher);
}

std::expected<void, std::string> System::open_quick_control()
{
    return std::unexpected("Shell Quick Control page is no longer available");
}

std::expected<void, std::string> System::open_system_monitor()
{
    return std::unexpected("Shell System Monitor page is no longer available");
}

std::expected<void, std::string> System::open_notifications()
{
    return open_shell_page(ShellPage::AppLauncher);
}

std::expected<void, std::string> System::open_recents()
{
    return open_shell_page(ShellPage::Recents);
}

std::expected<void, std::string> System::close_active_app()
{
    auto active = get_active_app();
    if (!active || active->app_id == shell_app_id_) {
        return shell_app_ ? shell_app_->set_foreground_app(std::nullopt) : std::expected<void, std::string> {};
    }
    pending_shell_page_ = ShellPage::AppLauncher;
    return stop_app(active->app_id);
}

std::expected<void, std::string> System::go_home()
{
    return open_shell_page(ShellPage::AppLauncher);
}

std::expected<void, std::string> System::activate_app(
    core::AppId app_id, const core::System::AppStartOptions &options
)
{
    auto app = get_app(app_id);
    if (!app.has_value()) {
        return std::unexpected("App not found");
    }
    if (app->state != core::AppState::Paused) {
        return start_app(app_id, options);
    }

    // The app is living in the background: bring it back instead of starting a
    // second instance.
    auto resume_result = resume_app(app_id);
    if (!resume_result) {
        // Resuming can only fail while re-attaching the GUI. Do not leave the user
        // with a dead icon: tear the app down and start it again from scratch.
        BROOKESIA_LOGW(
            "Failed to resume app, restarting it: app_id(%1%), reason(%2%)",
            app_id, resume_result.error()
        );
        (void)stop_app(app_id);
        return start_app(app_id, options);
    }
    if (shell_app_) {
        auto foreground_result = shell_app_->set_foreground_app(*app);
        if (!foreground_result) {
            return foreground_result;
        }
    }
    return {};
}

std::expected<void, std::string> System::restore_shell()
{
    if (shell_app_id_ == core::INVALID_APP_ID) {
        return {};
    }
    auto shell = get_app(shell_app_id_);
    if (shell && (shell->state == core::AppState::Running || shell->state == core::AppState::Paused)) {
        if (shell_app_) {
            auto page_result = shell_app_->show_page(pending_shell_page_);
            if (!page_result) {
                return page_result;
            }
            return shell_app_->set_foreground_app(std::nullopt);
        }
        return {};
    }

    auto start_result = start_app(shell_app_id_);
    if (!start_result) {
        return start_result;
    }
    if (pending_shell_page_ == ShellPage::AppLauncher || shell_app_ == nullptr) {
        return {};
    }
    return shell_app_->show_page(pending_shell_page_);
}

} // namespace esp_brookesia::system::super
