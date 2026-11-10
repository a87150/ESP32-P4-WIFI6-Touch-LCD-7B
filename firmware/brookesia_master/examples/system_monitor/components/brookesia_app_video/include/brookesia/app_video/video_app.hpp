/*
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "boost/thread.hpp"
#include "driver/jpeg_decode.h"
#include "esp_h264_dec.h"
#include "esp_h264_dec_param.h"
#include "esp_h264_dec_sw.h"
#include "lvgl.h"
#include "brookesia/app_video/mp4_demuxer.hpp"
#include "brookesia/gui_interface.hpp"
#include "brookesia/hal_interface.hpp"
#include "brookesia/hal_interface/interfaces/audio/codec_player.hpp"
#include "brookesia/system_core.hpp"

namespace esp_brookesia::app::video {

/**
 * @brief MJPEG / MP4 movie player for the board panel.
 *
 * MJPEG files (`.mjpeg`/`.mjpg`) are a plain concatenation of JPEG pictures:
 * every picture is decoded by the hardware JPEG codec into a PSRAM frame
 * buffer. MP4/MOV files (`.mp4`/`.m4v`/`.mov`) with an H.264 (AVC) video track
 * are demuxed from their sample tables and decoded by the software H.264
 * decoder (the ESP32-P4 has a hardware H.264 encoder only). Both paths hand
 * the finished RGB565 picture to the GUI as a native image source, so playback
 * does not allocate or copy a full frame per picture. Audio tracks in MP4
 * files are ignored.
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

    enum class SourceFormat {
        Mjpeg,
        Mp4,
    };

    struct Source {
        uint8_t *data = nullptr;             ///< Whole MJPEG file (PSRAM)
        size_t size = 0;
        std::FILE *file = nullptr;           ///< MP4 file, kept open for sample reads
        SourceFormat format = SourceFormat::Mjpeg;
        std::vector<FrameSpan> frames;       ///< MJPEG pictures
        std::vector<mp4::Sample> samples;    ///< MP4 coded samples
        std::vector<uint8_t> extradata;      ///< MP4 Annex-B SPS/PPS
        uint32_t fps = 12;                   ///< Nominal source frame rate
        uint32_t frame_ms = 83;              ///< Playback pacing
        uint32_t duration_ms = 0;
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
    bool ensure_h264_decoder();
    void close_h264_decoder();

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
    bool decode_mp4_frame(uint32_t frame_index, int32_t buffer_index);

    /* ---- UI (app task) ---- */
    void refresh_ui(system::core::AppContext &context);
    void set_status(const std::string &text);
    void adjust_volume(int delta);

    system::core::AppContext *context_ = nullptr;
    std::vector<std::string> movies_;
    std::vector<std::string> list_paths_;
    size_t list_page_ = 0;
    size_t movie_index_ = 0;
    bool paused_ui_ = false;
    std::atomic<bool> playing_{false};
    // Optional "play the next movie when this one ends" mode (off by default).
    bool auto_next_ = false;
    // Shared codec volume (the movie itself has no audio, but the same codec may
    // still be playing music in the background).
    hal::InterfaceHandle<hal::audio::CodecPlayerIface> audio_;
    int32_t volume_ = 75;

    Source source_;
    jpeg_decoder_handle_t decoder_ = nullptr;
    esp_h264_dec_handle_t h264_decoder_ = nullptr;
    esp_h264_dec_param_handle_t h264_param_ = nullptr;
    // Scratch buffer for the Annex-B version of the current MP4 sample.
    std::vector<uint8_t> annexb_buffer_;
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
    int32_t frame_area_width_ = 976;
    int32_t frame_area_height_ = 432;

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
