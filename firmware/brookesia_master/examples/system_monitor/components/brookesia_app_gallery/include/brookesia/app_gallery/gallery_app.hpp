/*
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "brookesia/gui_interface.hpp"
#include "brookesia/system_core.hpp"

namespace esp_brookesia::app::gallery {

/**
 * @brief Local picture browser that can also set the desktop wallpaper.
 *
 * Pictures are collected from a few well known folders on the mounted file
 * systems. Choosing one writes its absolute path into the Shell wallpaper
 * configuration so the launcher picks it up on the next visit.
 */
class GalleryApp final: public system::core::IApp {
public:
    GalleryApp();
    ~GalleryApp() override;

    GalleryApp(const GalleryApp &) = delete;
    GalleryApp &operator=(const GalleryApp &) = delete;

    system::core::AppManifest get_manifest() const override;
    system::core::AppGuiDescriptor get_gui_descriptor() const override;

    std::expected<void, std::string> on_install(system::core::AppContext &context) override;
    void on_uninstall(system::core::AppContext &context) override;
    std::expected<void, std::string> on_start(system::core::AppContext &context) override;
    std::expected<void, std::string> on_stop(system::core::AppContext &context) override;
    std::expected<void, std::string> on_pause(system::core::AppContext &context) override;
    std::expected<void, std::string> on_resume(system::core::AppContext &context) override;
    std::expected<void, std::string> on_timer(
        system::core::AppContext &context,
        system::core::TimerId timer_id,
        std::string_view name
    ) override;

private:
    void scan_images();
    void subscribe_actions(system::core::AppContext &context);
    void rebuild_list(system::core::AppContext &context);
    void destroy_list(system::core::AppContext &context);
    void show_list(system::core::AppContext &context);
    void open_image(system::core::AppContext &context, size_t index);
    void change_page(system::core::AppContext &context, int direction);
    void show_image(system::core::AppContext &context, size_t index);
    void step_image(system::core::AppContext &context, int direction);
    void set_wallpaper(system::core::AppContext &context);
    void toggle_slideshow(system::core::AppContext &context);
    void stop_slideshow();
    void set_status(system::core::AppContext &context, const std::string &text);

    system::core::AppContext *context_ = nullptr;
    std::vector<std::string> images_;
    std::vector<std::string> list_paths_;
    size_t list_page_ = 0;
    size_t index_ = 0;
    bool slideshow_ = false;
    system::core::TimerId slideshow_timer_ = system::core::INVALID_TIMER_ID;
    std::vector<gui::ScopedConnection> action_connections_;
};

/**
 * @brief Provider that instantiates the Gallery app.
 */
class GalleryAppProvider final: public system::core::IAppProvider {
public:
    system::core::AppManifest get_manifest() const override
    {
        static const GalleryApp prototype;
        return prototype.get_manifest();
    }

    std::shared_ptr<system::core::IApp> create_app() override
    {
        return std::make_shared<GalleryApp>();
    }
};

} // namespace esp_brookesia::app::gallery
