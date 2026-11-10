/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

std::vector<FileManagerApp::VolumeEntry> FileManagerApp::build_volumes() const
{
    std::vector<VolumeEntry> result;
    if (!storage_layout_) {
        return result;
    }

    auto append = [this, &result](const system::core::StorageVolume &volume, const char *name_key) {
        result.push_back(VolumeEntry{
            .volume = volume,
            .display_name = tr(name_key),
            .capacity_text = {},
        });
    };
    auto has_root = [&result](std::string_view root) {
        return std::any_of(result.begin(), result.end(), [root](const VolumeEntry & entry) {
            return entry.volume.root_path == root;
        });
    };

    if (storage_layout_->internal.available) {
        append(storage_layout_->internal, "volume_internal");
    }

    bool has_sd = false;
    for (const auto &volume : storage_layout_->external) {
        if (!volume.available) {
            continue;
        }
        if (volume.medium_type == hal::storage::FileSystemIface::MediumType::SDCard) {
            has_sd = true;
            append(volume, "volume_sd");
        } else {
            append(volume, "volume_external");
        }
    }

    // Present a "My Computer"-style volume list even when removable media are
    // absent. These placeholders are marked unavailable so opening them shows a
    // hint instead of trying to browse a non-existent mount point.
    if (!has_sd && !has_root("/sdcard")) {
        system::core::StorageVolume volume = {};
        volume.id = "sdcard";
        volume.partition = system::core::StoragePartition::External;
        volume.mount_point = "/sdcard";
        volume.root_path = "/sdcard";
        volume.medium_type = hal::storage::FileSystemIface::MediumType::SDCard;
        volume.fs_type = hal::storage::FileSystemIface::FileSystemType::FATFS;
        volume.available = false;
        append(volume, "volume_sd");
    }
    if (!has_root("/usb")) {
        system::core::StorageVolume volume = {};
        volume.id = "usb";
        volume.partition = system::core::StoragePartition::External;
        volume.mount_point = "/usb";
        volume.root_path = "/usb";
        volume.medium_type = hal::storage::FileSystemIface::MediumType::Flash;
        volume.fs_type = hal::storage::FileSystemIface::FileSystemType::FATFS;
        volume.available = false;
        append(volume, "volume_usb");
    }
    if (!has_root("/network")) {
        system::core::StorageVolume volume = {};
        volume.id = "network";
        volume.partition = system::core::StoragePartition::External;
        volume.mount_point = "/network";
        volume.root_path = "/network";
        volume.medium_type = hal::storage::FileSystemIface::MediumType::Flash;
        volume.fs_type = hal::storage::FileSystemIface::FileSystemType::LittleFS;
        volume.available = false;
        append(volume, "volume_network");
    }

    return result;
}

std::optional<size_t> FileManagerApp::find_current_volume() const
{
    if (!current_volume_index_ || *current_volume_index_ >= volumes_.size()) {
        return std::nullopt;
    }
    return current_volume_index_;
}

std::filesystem::path FileManagerApp::current_root() const
{
    const auto index = find_current_volume();
    if (!index) {
        return {};
    }
    return std::filesystem::path(volumes_[*index].volume.root_path).lexically_normal();
}

std::filesystem::path FileManagerApp::current_directory_path() const
{
    const auto root = current_root();
    if (root.empty()) {
        return {};
    }
    return (root / current_directory_).lexically_normal();
}

bool FileManagerApp::is_inside_current_root(const std::filesystem::path &path) const
{
    const auto root = current_root().lexically_normal();
    const auto target = path.lexically_normal();
    const auto root_text = root.generic_string();
    const auto target_text = target.generic_string();
    return target_text == root_text || target_text.starts_with(root_text + "/");
}

bool FileManagerApp::can_operate_selected() const
{
    return selected_entry_ &&
           selected_entry_->kind != EntryKind::Volume &&
           is_inside_current_root(selected_entry_->path);
}

void FileManagerApp::set_current_directory_from_absolute_path(const std::filesystem::path &path)
{
    const auto root = current_root().lexically_normal();
    if (root.empty()) {
        current_directory_.clear();
        return;
    }

    const auto target = path.lexically_normal();
    if (target == root) {
        current_directory_.clear();
        return;
    }
    if (!path_is_same_or_child(target, root)) {
        current_directory_.clear();
        return;
    }

    auto relative = make_relative_path_inside_root(target, root);
    if (!relative || relative->empty() || *relative == ".") {
        current_directory_.clear();
        return;
    }
    current_directory_ = relative->lexically_normal();
}

void FileManagerApp::rebase_current_directory_after_move(
    const std::filesystem::path &old_path,
    const std::filesystem::path &new_path
)
{
    const auto root = current_root();
    if (root.empty()) {
        return;
    }

    const auto current_absolute = (root / current_directory_).lexically_normal();
    const auto old_normalized = old_path.lexically_normal();
    if (!path_is_same_or_child(current_absolute, old_normalized)) {
        return;
    }

    auto rebased = new_path.lexically_normal();
    const auto current_text = current_absolute.generic_string();
    const auto old_text = old_normalized.generic_string();
    if (current_text.size() > old_text.size()) {
        rebased /= current_text.substr(old_text.size() + 1);
    }
    set_current_directory_from_absolute_path(rebased);
}

void FileManagerApp::recover_current_directory_after_remove(const std::filesystem::path &removed_path)
{
    const auto root = current_root();
    if (root.empty()) {
        return;
    }

    const auto current_absolute = (root / current_directory_).lexically_normal();
    const auto removed_normalized = removed_path.lexically_normal();
    if (!path_is_same_or_child(current_absolute, removed_normalized)) {
        return;
    }

    auto fallback = removed_normalized.parent_path().lexically_normal();
    auto is_directory = [](const std::filesystem::path & path) {
        auto info = StorageHelper::fs_stat(path.generic_string());
        return info && info->type == StorageHelper::FileType::Directory;
    };
    while (path_is_same_or_child(fallback, root) && fallback != root && !is_directory(fallback)) {
        fallback = fallback.parent_path().lexically_normal();
    }
    if (!path_is_same_or_child(fallback, root) || !is_directory(fallback)) {
        current_directory_.clear();
        return;
    }
    set_current_directory_from_absolute_path(fallback);
}

std::optional<FileManagerApp::Entry> FileManagerApp::find_entry_by_path(std::string_view event_path) const
{
    for (const auto &entry : entries_) {
        if (event_path == entry.view_path || std::string(event_path).starts_with(entry.view_path + "/")) {
            return entry;
        }
    }
    return std::nullopt;
}
