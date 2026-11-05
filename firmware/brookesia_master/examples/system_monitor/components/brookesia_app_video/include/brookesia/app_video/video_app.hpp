/*
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "boost/thread.hpp"
#include "driver/jpeg_decode.h"
#include "lvgl.h"
#include "brookesia/gui_interface.hpp"
#include "brookesia/hal_interface.hpp"
#include "brookesia/system_core.hpp"

namespace esp_brookesia::app::video {

/**
 * @brief MJPEG movie player for the board panel.
 *
 * A movie file is a plain concatenation of JPEG pictures (`.mjpeg`/`.mjpg`).
 * Every picture is decoded by the hardware JPEG codec into a PSRAM frame
 * buffer and handed to the GUI as a native image source, so playback does not
 * allocate or copy a full frame per picture.
 */
class VideoApp final: public system::core::IApp {
public:
    VideoApp();
    ~VideoApp() override;

    VideoApp(const VideoApp &) = delete;
    VideoApp &operator=(const VideoApp &) = delete;

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
    struct FrameSpan {
        uint32_t offset = 0;
        uint32_t length = 0;
    };

    struct Source {
        uint8_t *data = nullptr;             ///< Whole movie file (PSRAM)
        size_t size = 0;
        std::vector<FrameSpan> frames;
        int32_t width = 0;
        int32_t height = 0;
        int32_t padded_width = 0;
        int32_t padded_height = 0;
        std::string path;
    };

    struct PlaybackState {
        std::mutex mutex;
        std::condition_variable cv;
        bool quit = false;
        bool paused = true;
        // Playback worker requests. Switching movies (or rescanning the folder)
        // touches the file system, the JPEG codec and the frame buffers, so it is
        // always executed by the playback worker instead of the GUI/app-input
        // task: the caller only posts a request.
        bool request_scan = false;
        bool request_load = false;
        size_t pending_index = 0;
        uint32_t cursor = 0;
        uint32_t seek_request = UINT32_MAX;
        uint32_t current_frame = 0;
        bool finished = false;
        bool error = false;
        std::string error_text;
    };

    /* ---- source handling (app task / playback worker) ---- */
    void scan_movies();
    bool load_source(size_t index);
    /**
     * @brief Load a movie without stopping the playback worker.
     *
     * Must be called by the task that owns the decoder (the playback worker, or
     * the app task before the worker has been started).
     */
    bool load_source_impl(size_t index);
    void release_source();
    bool ensure_decode_buffers(const Source &source);
    void register_frame_images(const Source &source);
    void unregister_frame_images();
    void apply_frame_geometry();

    /* ---- playback control (app task) ---- */
    void subscribe_actions(system::core::AppContext &context);
    void rebuild_list(system::core::AppContext &context);
    void destroy_list(system::core::AppContext &context);
    void open_list(system::core::AppContext &context);
    void close_list(system::core::AppContext &context);
    void open_movie(system::core::AppContext &context, size_t index);
    void change_page(system::core::AppContext &context, int direction);
    void start_worker();
    void stop_worker();
    void worker_loop();
    void request_play();
    void toggle_playback();
    void step_movie(int direction);
    void seek_to_permille(int32_t permille);
    void notify_worker();
    bool decode_frame(uint32_t frame_index, int32_t buffer_index);

    /* ---- UI (app task) ---- */
    void refresh_ui(system::core::AppContext &context);
    void set_status(const std::string &text);

    system::core::AppContext *context_ = nullptr;
    std::vector<std::string> movies_;
    std::vector<std::string> list_paths_;
    size_t list_page_ = 0;
    size_t movie_index_ = 0;
    bool paused_ui_ = false;
    std::atomic<bool> playing_{false};

    Source source_;
    jpeg_decoder_handle_t decoder_ = nullptr;
    uint8_t *frame_buffer_[2] = {};
    size_t frame_buffer_capacity_ = 0;
    uint8_t *input_buffer_ = nullptr;
    size_t input_buffer_capacity_ = 0;
    lv_image_dsc_t frame_descriptor_[2] = {};
    int32_t front_buffer_ = 0;
    int32_t shown_width_ = 0;
    int32_t shown_height_ = 0;
    bool images_registered_ = false;
    bool frame_area_valid_ = false;
    int32_t frame_area_width_ = 1024;
    int32_t frame_area_height_ = 524;

    PlaybackState state_;
    boost::thread worker_;
    bool worker_started_ = false;
    bool first_frame_logged_ = false;
    bool decode_error_logged_ = false;
    int32_t last_programmed_permille_ = -1;
    /// Timestamp of the first tap on the picture area (double tap detection).
    std::chrono::steady_clock::time_point last_tap_{};

    std::vector<gui::ScopedConnection> action_connections_;
};

/**
 * @brief Provider that instantiates the Video app.
 */
class VideoAppProvider final: public system::core::IAppProvider {
public:
    system::core::AppManifest get_manifest() const override
    {
        static const VideoApp prototype;
        return prototype.get_manifest();
    }

    std::shared_ptr<system::core::IApp> create_app() override
    {
        return std::make_shared<VideoApp>();
    }
};

} // namespace esp_brookesia::app::video
