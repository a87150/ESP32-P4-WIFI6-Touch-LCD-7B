/*
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <condition_variable>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "boost/thread.hpp"
#include "brookesia/gui_interface.hpp"
#include "brookesia/hal_interface.hpp"
#include "brookesia/hal_interface/interfaces/audio/codec_player.hpp"
#include "brookesia/system_core.hpp"

namespace esp_brookesia::app::music {

/**
 * @brief Simple WAV music player for the board speaker.
 *
 * Tracks are scanned from the SD card and the littlefs sounds folder. Decoding
 * and playback run on a dedicated thread so the GUI stays responsive and the
 * music keeps playing while the app sits in the background.
 */
class MusicApp final: public system::core::IApp {
public:
    MusicApp();
    ~MusicApp() override;

    MusicApp(const MusicApp &) = delete;
    MusicApp &operator=(const MusicApp &) = delete;

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
    struct EngineState {
        std::mutex mutex;
        std::condition_variable cv;
        std::string path;
        uint32_t generation = 0;
        bool play_requested = false;
        bool paused = false;
        bool quit = false;
        bool track_finished = false;
        bool error = false;
        std::string error_text;
        uint32_t byte_pos = 0;
        uint32_t data_size = 0;
        uint32_t sample_rate = 0;
        uint16_t channels = 0;
        uint16_t bits = 16;
        bool seek_pending = false;
        uint32_t seek_pos = 0;
        int volume = 75;
        bool volume_changed = false;
    };

    void scan_tracks();
    void subscribe_actions(system::core::AppContext &context);
    void rebuild_list(system::core::AppContext &context);
    void destroy_list(system::core::AppContext &context);
    void open_list(system::core::AppContext &context);
    void close_list(system::core::AppContext &context);
    void open_track(system::core::AppContext &context, size_t index);
    void change_page(system::core::AppContext &context, int direction);
    void play_index(size_t index, bool start_playback);
    void toggle_playback();
    void step_track(int direction);
    void seek_to(int32_t permille);
    void apply_seek(int32_t permille);
    void adjust_volume(int delta);
    void set_volume(int volume);
    void refresh_status(const std::string &text);
    void refresh_ui(system::core::AppContext &context);
    void tick_wave(system::core::AppContext &context);

    void start_engine();
    void stop_engine();
    void engine_loop();
    void notify_engine();

    system::core::AppContext *context_ = nullptr;
    std::vector<std::string> tracks_;
    std::vector<uint32_t> track_bytes_list_;
    std::vector<std::string> list_paths_;
    size_t list_page_ = 0;
    size_t index_ = 0;
    // Size of the current WAV file, cached when the track changes so the UI
    // timer never stats the filesystem.
    uint32_t track_bytes_ = 0;
    bool paused_ui_ = false;
    // Optional "play the next track when this one ends" mode (off by default).
    bool auto_next_ = false;

    // Last value written to the progress slider. LVGL emits a value-changed
    // event for programmatic updates too, so remember it to avoid seek loops.
    int32_t last_programmed_permille_ = -1;
    int32_t requested_seek_ = -1;
    int32_t volume_ = 75;
    int64_t seek_hold_until_ms_ = 0;
    // Vertical position of every waveform point in the current frame; a value
    // of -1000 forces the first refresh.
    std::array<int32_t, 49> wave_points_{};
    // Counts waveform ticks between two text-binding refreshes.
    int32_t ui_tick_ = 0;

    EngineState state_;
    hal::InterfaceHandle<hal::audio::CodecPlayerIface> player_;
    boost::thread engine_thread_;
    bool engine_started_ = false;
    std::vector<gui::ScopedConnection> action_connections_;
};

/**
 * @brief Provider that instantiates the Music app.
 */
class MusicAppProvider final: public system::core::IAppProvider {
public:
    system::core::AppManifest get_manifest() const override
    {
        static const MusicApp prototype;
        return prototype.get_manifest();
    }

    std::shared_ptr<system::core::IApp> create_app() override
    {
        return std::make_shared<MusicApp>();
    }
};

} // namespace esp_brookesia::app::music
