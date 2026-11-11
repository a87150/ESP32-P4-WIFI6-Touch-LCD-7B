/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

void FileManagerApp::request_rename(system::core::AppContext &context)
{
    if (!can_operate_selected()) {
        show_message(
            context,
            tr("rename_unavailable"),
            tr("select_first"),
            system::core::MessageDialogIcon::Warning
        );
        return;
    }
    auto selected = *selected_entry_;
    auto keyboard_result_handler = [this, selected](const system::core::KeyboardResult & result) {
        if (!context_ || !result.confirmed) {
            return;
        }
        const auto new_name = result.text;
        if (new_name.empty() || new_name.find('/') != std::string::npos || new_name == "." || new_name == "..") {
            show_message(*context_, tr("rename_failed"), tr("invalid_name"), system::core::MessageDialogIcon::Warning);
            return;
        }
        const auto old_path = selected.path.lexically_normal();
        auto destination = selected.path.parent_path() / new_name;
        destination = destination.lexically_normal();
        if (!is_inside_current_root(destination)) {
            show_message(
                *context_,
                tr("rename_failed"),
                tr("destination_escapes"),
                system::core::MessageDialogIcon::Critical
            );
            return;
        }

        auto operation = PendingFileOperation{
            .kind = FileOperationKind::Rename,
            .entry_kind = selected.kind,
            .source_path = old_path,
            .destination_path = destination,
        };
        auto schedule_result = schedule_file_operation(*context_, std::move(operation));
        if (!schedule_result) {
            show_message(
                *context_,
                tr("rename_failed"),
                schedule_result.error(),
                system::core::MessageDialogIcon::Warning
            );
        }
    };
    auto keyboard_options = system::core::KeyboardRequestOptions{
        .title = tr("rename_title"),
        .placeholder = tr("rename_placeholder"),
        .initial_text = selected.name,
        .max_length = 96,
        .password = false,
        .mode = "text",
        .allowed_modes = {},
    };
    auto keyboard = context.system_service().show_keyboard(
                        std::move(keyboard_options),
                        std::move(keyboard_result_handler)
                    );
    if (!keyboard) {
        show_message(context, tr("rename_failed"), keyboard.error(), system::core::MessageDialogIcon::Warning);
    }
}

void FileManagerApp::request_delete(system::core::AppContext &context)
{
    if (!can_operate_selected()) {
        show_message(
            context,
            tr("delete_unavailable"),
            tr("select_first"),
            system::core::MessageDialogIcon::Warning
        );
        return;
    }
    auto selected = *selected_entry_;
    const auto title = tr("delete_title_prefix") + selected.name + tr("delete_title_suffix");
    auto dialog_result_handler = [this, selected](const system::core::MessageDialogResult & result) {
        if (!context_ || result.button_role != system::core::MessageDialogButtonRole::Destructive) {
            return;
        }

        auto operation = PendingFileOperation{
            .kind = FileOperationKind::Delete,
            .entry_kind = selected.kind,
            .source_path = selected.path.lexically_normal(),
            .destination_path = {},
        };
        auto schedule_result = schedule_file_operation(*context_, std::move(operation));
        if (!schedule_result) {
            show_message(
                *context_,
                tr("delete_failed"),
                schedule_result.error(),
                system::core::MessageDialogIcon::Warning
            );
        }
    };
    auto dialog_options = system::core::MessageDialogOptions{
        .text = title,
        .informative_text = selected.path.generic_string(),
        .icon = system::core::MessageDialogIcon::Warning,
        .buttons = {
            system::core::MessageDialogButton{
                .text = tr("delete"),
                .role = system::core::MessageDialogButtonRole::Destructive,
            },
            system::core::MessageDialogButton{
                .text = tr("cancel"),
                .role = system::core::MessageDialogButtonRole::Reject,
            },
        },
        .auto_close_ms = 0,
    };
    auto dialog = context.system_service().show_message_dialog(
                      std::move(dialog_options),
                      std::move(dialog_result_handler)
                  );
    if (!dialog) {
        show_message(context, tr("delete_failed"), dialog.error(), system::core::MessageDialogIcon::Warning);
    }
}

void FileManagerApp::request_create(system::core::AppContext &context)
{
    if (!find_current_volume()) {
        show_message(
            context,
            tr("create_failed"),
            tr("open_volume_first"),
            system::core::MessageDialogIcon::Warning
        );
        return;
    }

    auto dialog_result_handler = [this](const system::core::MessageDialogResult & result) {
        if (!context_ || result.reason != system::core::MessageDialogCloseReason::Button) {
            return;
        }
        if (result.button_index == 0) {
            prompt_create_entry(*context_, true);
        } else if (result.button_index == 1) {
            prompt_create_entry(*context_, false);
        }
    };
    auto dialog_options = system::core::MessageDialogOptions{
        .text = tr("create_title"),
        .informative_text = current_directory_path().generic_string(),
        .icon = system::core::MessageDialogIcon::Question,
        .buttons = {
            system::core::MessageDialogButton{
                .text = tr("create_folder"),
                .role = system::core::MessageDialogButtonRole::Accept,
            },
            system::core::MessageDialogButton{
                .text = tr("create_file"),
                .role = system::core::MessageDialogButtonRole::Action,
            },
            system::core::MessageDialogButton{
                .text = tr("cancel"),
                .role = system::core::MessageDialogButtonRole::Reject,
            },
        },
        .auto_close_ms = 0,
    };
    auto dialog = context.system_service().show_message_dialog(
                      std::move(dialog_options),
                      std::move(dialog_result_handler)
                  );
    if (!dialog) {
        show_message(context, tr("create_failed"), dialog.error(), system::core::MessageDialogIcon::Warning);
    }
}

void FileManagerApp::prompt_create_entry(system::core::AppContext &context, bool directory)
{
    auto keyboard_result_handler = [this, directory](const system::core::KeyboardResult & result) {
        if (!context_ || !result.confirmed) {
            return;
        }
        const auto name = result.text;
        if (name.empty() || name == "." || name == ".." ||
                name.find('/') != std::string::npos || name.find('\\') != std::string::npos) {
            show_message(*context_, tr("create_failed"), tr("invalid_name"), system::core::MessageDialogIcon::Warning);
            return;
        }
        const auto failure_key = directory ? "create_folder_failed" : "create_file_failed";
        const auto directory_path = current_directory_path();
        if (directory_path.empty()) {
            show_message(
                *context_,
                tr(failure_key),
                tr("open_volume_first"),
                system::core::MessageDialogIcon::Warning
            );
            return;
        }
        const auto target = (directory_path / name).lexically_normal();
        if (!is_inside_current_root(target)) {
            show_message(
                *context_,
                tr(failure_key),
                tr("destination_escapes"),
                system::core::MessageDialogIcon::Critical
            );
            return;
        }
        auto existing = StorageHelper::fs_stat(target.generic_string());
        if (existing && existing->exists) {
            show_message(
                *context_,
                tr(failure_key),
                tr("destination_exists"),
                system::core::MessageDialogIcon::Warning
            );
            return;
        }

        auto operation = PendingFileOperation{
            .kind = directory ? FileOperationKind::CreateDirectory : FileOperationKind::CreateFile,
            .entry_kind = directory ? EntryKind::Directory : EntryKind::File,
            .source_path = target,
            .destination_path = {},
        };
        auto schedule_result = schedule_file_operation(*context_, std::move(operation));
        if (!schedule_result) {
            show_message(
                *context_,
                tr(failure_key),
                schedule_result.error(),
                system::core::MessageDialogIcon::Warning
            );
        }
    };
    auto keyboard_options = system::core::KeyboardRequestOptions{
        .title = directory ? tr("new_folder_title") : tr("new_file_title"),
        .placeholder = tr("name_placeholder"),
        .initial_text = {},
        .max_length = 96,
        .password = false,
        .mode = "text",
        .allowed_modes = {},
    };
    auto keyboard = context.system_service().show_keyboard(
                        std::move(keyboard_options),
                        std::move(keyboard_result_handler)
                    );
    if (!keyboard) {
        show_message(
            context,
            directory ? tr("create_folder_failed") : tr("create_file_failed"),
            keyboard.error(),
            system::core::MessageDialogIcon::Warning
        );
    }
}

void FileManagerApp::request_copy(system::core::AppContext &context)
{
    begin_picker(context, FileOperationKind::Copy);
}

void FileManagerApp::request_move(system::core::AppContext &context)
{
    begin_picker(context, FileOperationKind::Move);
}

void FileManagerApp::begin_picker(system::core::AppContext &context, FileOperationKind kind)
{
    const bool copy = kind == FileOperationKind::Copy;
    if (!can_operate_selected()) {
        show_message(
            context,
            tr(copy ? "copy_unavailable" : "move_unavailable"),
            tr("select_first"),
            system::core::MessageDialogIcon::Warning
        );
        return;
    }

    picker_active_ = true;
    picker_kind_ = kind;
    picker_source_ = *selected_entry_;
    picker_return_directory_ = current_directory_;
    picker_return_volume_index_ = current_volume_index_;

    // Go back to the browser and turn it into a destination folder chooser.
    auto flow_result = hide_operations(context);
    if (!flow_result) {
        picker_active_ = false;
        picker_source_.reset();
        show_message(
            context,
            tr(copy ? "copy_failed" : "move_failed"),
            flow_result.error(),
            system::core::MessageDialogIcon::Warning
        );
        return;
    }
    auto ui_result = refresh_ui(context);
    if (!ui_result) {
        picker_active_ = false;
        picker_source_.reset();
        show_message(
            context,
            tr(copy ? "copy_failed" : "move_failed"),
            ui_result.error(),
            system::core::MessageDialogIcon::Warning
        );
    }
}

std::expected<void, std::string> FileManagerApp::confirm_picker(system::core::AppContext &context)
{
    if (!picker_active_ || !picker_source_) {
        return {};
    }

    const bool copy = picker_kind_ == FileOperationKind::Copy;
    const auto failure_key = copy ? "copy_failed" : "move_failed";
    const auto directory_path = current_directory_path();
    if (directory_path.empty()) {
        show_message(
            context,
            tr(failure_key),
            tr("open_volume_first"),
            system::core::MessageDialogIcon::Warning
        );
        return {};
    }

    const auto source = picker_source_->path.lexically_normal();
    const auto destination = (directory_path / source.filename()).lexically_normal();
    if (destination == source) {
        show_message(
            context,
            tr(failure_key),
            tr("destination_exists"),
            system::core::MessageDialogIcon::Warning
        );
        return {};
    }
    if (!is_inside_current_root(destination)) {
        show_message(
            context,
            tr(failure_key),
            tr("destination_escapes"),
            system::core::MessageDialogIcon::Critical
        );
        return {};
    }
    if (picker_source_->kind == EntryKind::Directory && path_is_same_or_child(destination, source)) {
        show_message(
            context,
            tr(failure_key),
            tr(copy ? "copy_into_self" : "move_into_self"),
            system::core::MessageDialogIcon::Warning
        );
        return {};
    }
    auto destination_info = StorageHelper::fs_stat(destination.generic_string());
    if (destination_info && destination_info->exists) {
        show_message(
            context,
            tr(failure_key),
            tr("destination_exists"),
            system::core::MessageDialogIcon::Warning
        );
        return {};
    }

    auto operation = PendingFileOperation{
        .kind = picker_kind_,
        .entry_kind = picker_source_->kind,
        .source_path = source,
        .destination_path = destination,
    };
    auto schedule_result = schedule_file_operation(context, std::move(operation));
    if (!schedule_result) {
        show_message(
            context,
            tr(failure_key),
            schedule_result.error(),
            system::core::MessageDialogIcon::Warning
        );
        return {};
    }

    // Keep the browser at the chosen destination; the commit refresh will show
    // the copied or moved item there.
    picker_active_ = false;
    picker_source_.reset();
    return refresh_ui(context);
}

std::expected<void, std::string> FileManagerApp::cancel_picker(system::core::AppContext &context)
{
    picker_active_ = false;
    picker_source_.reset();
    if (picker_return_volume_index_) {
        current_volume_index_ = picker_return_volume_index_;
        current_directory_ = picker_return_directory_;
    } else {
        current_volume_index_.reset();
        current_directory_.clear();
    }
    picker_return_volume_index_.reset();
    picker_return_directory_.clear();
    return refresh_entries(context);
}

std::expected<void, std::string> FileManagerApp::schedule_file_operation(
    system::core::AppContext &context,
    PendingFileOperation operation
)
{
    if (pending_file_operation_ || file_operation_dialog_timer_id_ != system::core::INVALID_TIMER_ID ||
            file_operation_commit_timer_id_ != system::core::INVALID_TIMER_ID ||
            file_operation_dialog_request_id_ != system::core::INVALID_MESSAGE_DIALOG_REQUEST_ID) {
        BROOKESIA_LOGW("Ignore File Manager operation while another operation is running");
        return {};
    }

    reset_message_dialog(context);
    pending_file_operation_ = std::move(operation);
    auto timer = context.timer().start_delayed(FILE_OPERATION_DIALOG_TIMER_NAME, FILE_OPERATION_DEFER_MS);
    if (!timer) {
        BROOKESIA_LOGW("Failed to schedule File Manager operation dialog: %1%", timer.error());
        auto dialog_result = show_file_operation_dialog(context);
        if (!dialog_result) {
            BROOKESIA_LOGW("Failed to show File Manager operation dialog: %1%", dialog_result.error());
        }
        return schedule_file_operation_commit(context);
    }

    file_operation_dialog_timer_id_ = *timer;
    return {};
}

std::expected<void, std::string> FileManagerApp::schedule_file_operation_commit(
    system::core::AppContext &context
)
{
    if (!pending_file_operation_) {
        hide_file_operation_dialog(context);
        return {};
    }

    auto timer = context.timer().start_delayed(FILE_OPERATION_COMMIT_TIMER_NAME, FILE_OPERATION_DEFER_MS);
    if (!timer) {
        BROOKESIA_LOGW("Failed to schedule File Manager operation commit: %1%", timer.error());
        return commit_file_operation(context);
    }

    file_operation_commit_timer_id_ = *timer;
    return {};
}

std::expected<void, std::string> FileManagerApp::commit_file_operation(system::core::AppContext &context)
{
    if (!pending_file_operation_) {
        hide_file_operation_dialog(context);
        return {};
    }

    if (file_operation_dialog_timer_id_ != system::core::INVALID_TIMER_ID) {
        (void)context.timer().stop(file_operation_dialog_timer_id_);
        file_operation_dialog_timer_id_ = system::core::INVALID_TIMER_ID;
    }

    auto operation = std::move(*pending_file_operation_);
    pending_file_operation_.reset();

    auto hide_guard = lib_utils::FunctionGuard([this, &context]() {
        hide_file_operation_dialog(context);
    });
    auto show_operation_message = [this, &context](
                                      std::string text,
                                      std::string informative_text,
                                      system::core::MessageDialogIcon icon
                                  ) {
        hide_file_operation_dialog(context);
        show_message(context, std::move(text), std::move(informative_text), icon);
    };

    if (operation.kind == FileOperationKind::CreateDirectory || operation.kind == FileOperationKind::CreateFile) {
        const bool directory = operation.kind == FileOperationKind::CreateDirectory;
        const auto failure_key = directory ? "create_folder_failed" : "create_file_failed";
        BROOKESIA_LOGI(
            "Create File Manager %1%: path(%2%)",
            directory ? "directory" : "file",
            operation.source_path.generic_string()
        );
        std::expected<void, std::string> create_result = directory ?
                StorageHelper::fs_mkdir(operation.source_path.generic_string()) :
                StorageHelper::fs_write_text(operation.source_path.generic_string(), "");
        if (!create_result) {
            hide_guard.release();
            show_operation_message(
                tr(failure_key),
                create_result.error(),
                system::core::MessageDialogIcon::Warning
            );
            return {};
        }
        auto refresh_result = refresh_entries(context);
        if (!refresh_result) {
            hide_guard.release();
            show_operation_message(
                tr("refresh_failed"),
                refresh_result.error(),
                system::core::MessageDialogIcon::Warning
            );
        }
        return {};
    }

    if (operation.kind == FileOperationKind::Copy) {
        BROOKESIA_LOGI(
            "Copy File Manager item: from(%1%) to(%2%)",
            operation.source_path.generic_string(),
            operation.destination_path.generic_string()
        );
        auto copy_result = StorageHelper::fs_copy_tree(
                               operation.source_path.generic_string(),
                               operation.destination_path.generic_string(),
                               false
                           );
        if (!copy_result) {
            hide_guard.release();
            show_operation_message(
                tr("copy_failed"),
                copy_result.error(),
                system::core::MessageDialogIcon::Warning
            );
            return {};
        }
        BROOKESIA_LOGI("Copied File Manager item: path(%1%)", operation.destination_path.generic_string());
        (void)hide_operations(context);
        auto refresh_result = refresh_entries(context);
        if (!refresh_result) {
            hide_guard.release();
            show_operation_message(
                tr("refresh_failed"),
                refresh_result.error(),
                system::core::MessageDialogIcon::Warning
            );
        }
        return {};
    }

    if (operation.kind == FileOperationKind::Move) {
        BROOKESIA_LOGI(
            "Move File Manager item: from(%1%) to(%2%)",
            operation.source_path.generic_string(),
            operation.destination_path.generic_string()
        );
        auto move_result = StorageHelper::fs_rename(
                               operation.source_path.generic_string(),
                               operation.destination_path.generic_string()
                           );
        if (!move_result && !is_inside_current_root(operation.source_path)) {
            // Cross-volume move: rename cannot cross filesystems, so copy the
            // tree into the destination volume and remove the source once the
            // copy succeeded.
            BROOKESIA_LOGI("Move File Manager item across volumes; copying then removing source");
            auto cross_copy_result = StorageHelper::fs_copy_tree(
                                         operation.source_path.generic_string(),
                                         operation.destination_path.generic_string(),
                                         false
                                     );
            if (!cross_copy_result) {
                hide_guard.release();
                show_operation_message(
                    tr("move_failed"),
                    cross_copy_result.error(),
                    system::core::MessageDialogIcon::Warning
                );
                return {};
            }
            auto remove_source_result = remove_path_tree(operation.source_path);
            if (!remove_source_result) {
                hide_guard.release();
                show_operation_message(
                    tr("move_failed"),
                    remove_source_result.error(),
                    system::core::MessageDialogIcon::Warning
                );
                return {};
            }
            move_result = std::expected<void, std::string>{};
        }
        if (!move_result) {
            hide_guard.release();
            show_operation_message(
                tr("move_failed"),
                move_result.error(),
                system::core::MessageDialogIcon::Warning
            );
            return {};
        }
        if (operation.entry_kind == EntryKind::Directory) {
            rebase_current_directory_after_move(operation.source_path, operation.destination_path);
        }
        (void)hide_operations(context);
        auto refresh_result = refresh_entries(context);
        if (!refresh_result) {
            hide_guard.release();
            show_operation_message(
                tr("refresh_failed"),
                refresh_result.error(),
                system::core::MessageDialogIcon::Warning
            );
        }
        return {};
    }

    if (operation.kind == FileOperationKind::Rename) {
        BROOKESIA_LOGI(
            "Rename File Manager item: from(%1%) to(%2%)",
            operation.source_path.generic_string(),
            operation.destination_path.generic_string()
        );
        auto rename_result = StorageHelper::fs_rename(
                                 operation.source_path.generic_string(),
                                 operation.destination_path.generic_string()
                             );
        if (!rename_result) {
            hide_guard.release();
            show_operation_message(
                tr("rename_failed"),
                rename_result.error(),
                system::core::MessageDialogIcon::Warning
            );
            return {};
        }
        if (operation.entry_kind == EntryKind::Directory) {
            rebase_current_directory_after_move(operation.source_path, operation.destination_path);
        }
        BROOKESIA_LOGI("Renamed File Manager item: path(%1%)", operation.destination_path.generic_string());
        (void)hide_operations(context);
        auto refresh_result = refresh_entries(context);
        if (!refresh_result) {
            hide_guard.release();
            show_operation_message(
                tr("refresh_failed"),
                refresh_result.error(),
                system::core::MessageDialogIcon::Warning
            );
        }
        return {};
    }

    BROOKESIA_LOGI("Delete File Manager item: path(%1%)", operation.source_path.generic_string());
    auto remove_result = remove_path_tree(operation.source_path);
    if (!remove_result) {
        BROOKESIA_LOGW(
            "Failed to delete File Manager item: path(%1%), error(%2%)",
            operation.source_path.generic_string(),
            remove_result.error()
        );
        hide_guard.release();
        show_operation_message(
            tr("delete_failed"),
            remove_result.error(),
            system::core::MessageDialogIcon::Warning
        );
        return {};
    }
    if (operation.entry_kind == EntryKind::Directory) {
        recover_current_directory_after_remove(operation.source_path);
    }
    BROOKESIA_LOGI(
        "Deleted File Manager item: path(%1%), entries(%2%)",
        operation.source_path.generic_string(),
        static_cast<unsigned long long>(*remove_result)
    );
    (void)hide_operations(context);
    auto refresh_result = refresh_entries(context);
    if (!refresh_result) {
        hide_guard.release();
        show_operation_message(
            tr("refresh_failed"),
            refresh_result.error(),
            system::core::MessageDialogIcon::Warning
        );
    }
    return {};
}
