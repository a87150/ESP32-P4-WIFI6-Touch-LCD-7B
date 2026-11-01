/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "brookesia/system_super/macro_configs.h"
#if !BROOKESIA_SYSTEM_SUPER_ENABLE_DEBUG_LOG
#   define BROOKESIA_LOG_DISABLE_DEBUG_TRACE 1
#endif

#include <cstdio>
#include <string>
#include <vector>

#include "private/shell_app.hpp"
#include "private/system_constants.hpp"
#include "private/utils.hpp"

namespace esp_brookesia::system::super {
namespace {

/**
 * @brief Read the wallpaper path written by the Gallery app.
 *
 * The file holds a single absolute path, optionally followed by a newline.
 * Anything unreadable or empty means "no wallpaper".
 */
std::string load_wallpaper_path()
{
    auto *file = std::fopen(SUPER_WALLPAPER_CONFIG_PATH, "rb");
    if (file == nullptr) {
        // Nothing chosen yet: use the bundled default so the desktop is never a
        // bare gradient. Registration fails gracefully if the file is missing.
        return SUPER_DEFAULT_WALLPAPER_PATH;
    }

    std::string path;
    char buffer[256] = {};
    while (path.size() < 512) {
        const auto read = std::fread(buffer, 1, sizeof(buffer), file);
        if (read == 0) {
            break;
        }
        path.append(buffer, read);
    }
    std::fclose(file);

    const auto first = path.find_first_not_of(" \t\r\n");
    const auto last = path.find_last_not_of(" \t\r\n");
    if (first == std::string::npos || last == std::string::npos) {
        return {};
    }
    return path.substr(first, last - first + 1);
}

} // namespace

void ShellApp::refresh_wallpaper()
{
    if (context_ == nullptr) {
        return;
    }

    const auto path = load_wallpaper_path();
    if (path.empty()) {
        if (wallpaper_source_path_.empty()) {
            return;
        }
        std::vector<gui::BindingValueUpdate> updates;
        updates.push_back(gui::BindingValueUpdate{
            .absolute_path = SUPER_WALLPAPER_PATH,
            .key = "wallpaper_hidden",
            .value = "true",
        });
        (void)context_->gui().set_binding_values(updates);
        if (!wallpaper_image_id_.empty()) {
            (void)context_->gui().unregister_image(wallpaper_image_id_);
        }
        wallpaper_image_id_.clear();
        wallpaper_source_path_.clear();
        return;
    }
    if (path == wallpaper_source_path_) {
        return;
    }

    const auto image_id = std::string(SUPER_WALLPAPER_IMAGE_ID_PREFIX) + std::to_string(++wallpaper_image_serial_);
    auto register_result = context_->gui().register_image(gui::RuntimeImageResource{
        .id = image_id,
        .primary_src = path,
        .preload = false,
    });
    if (!register_result) {
        BROOKESIA_LOGW("Failed to register wallpaper image %1%: %2%", path, register_result.error());
        return;
    }

    std::vector<gui::BindingValueUpdate> updates;
    updates.push_back(gui::BindingValueUpdate{
        .absolute_path = SUPER_WALLPAPER_PATH,
        .key = "wallpaper_src",
        .value = image_id,
    });
    updates.push_back(gui::BindingValueUpdate{
        .absolute_path = SUPER_WALLPAPER_PATH,
        .key = "wallpaper_hidden",
        .value = "false",
    });
    auto update_result = context_->gui().set_binding_values(updates);
    if (!update_result) {
        BROOKESIA_LOGW("Failed to show wallpaper %1%: %2%", path, update_result.error());
        return;
    }

    if (!wallpaper_image_id_.empty()) {
        (void)context_->gui().unregister_image(wallpaper_image_id_);
    }
    wallpaper_image_id_ = image_id;
    wallpaper_source_path_ = path;
    BROOKESIA_LOGI("Wallpaper applied: %1%", path);
}

} // namespace esp_brookesia::system::super
