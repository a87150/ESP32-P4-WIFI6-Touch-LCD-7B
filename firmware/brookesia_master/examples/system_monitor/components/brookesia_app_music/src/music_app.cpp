/*
 * SPDX-License-Identifier: Apache-2.0
 */
#include "brookesia/app_music/music_app.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include <dirent.h>
#include <sys/stat.h>

#include "brookesia/lib_utils/thread_config.hpp"

#include "private/utils.hpp"

namespace esp_brookesia::app::music {

using namespace esp_brookesia::system;

namespace {

struct WavInfo {
    uint32_t data_offset = 0;
    uint32_t data_size = 0;
    uint32_t sample_rate = 0;
    uint16_t channels = 0;
    uint16_t bits = 0;
};

constexpr const char *APP_ID = "brookesia.waveshare.music";
constexpr const char *APP_NAME = "Music";
constexpr const char *APP_NAME_ZH_CN = "音乐";
constexpr const char *APP_ICON_ID = "launcher_icon";
constexpr const char *GUI_ROOT = "res/root.json";
constexpr const char *MUSIC_FLOW_ID = "music_flow";
constexpr const char *PLAYBACK_TIMER_NAME = "music_playback";
constexpr int32_t PLAYBACK_PERIOD_MS = 500;

// Absolute paths inside the music screen document.
constexpr const char *PATH_TRACK_NAME = "/music/page/player_card/info/track_name";
constexpr const char *PATH_TRACK_META = "/music/page/player_card/info/track_meta";
constexpr const char *PATH_PROGRESS = "/music/page/player_card/info/progress";
constexpr const char *PATH_TIME_LABEL = "/music/page/player_card/info/time_label";
constexpr const char *PATH_VOLUME_SLIDER = "/music/page/player_card/info/volume/volume_slider";
constexpr const char *PATH_VOLUME_TEXT = "/music/page/player_card/info/volume/volume_text";
constexpr const char *PATH_TOGGLE_TEXT = "/music/page/controls/toggle/text";
constexpr const char *PATH_STATUS = "/music/page/status";

constexpr const char *ACTION_PREV = "music.prev";
constexpr const char *ACTION_TOGGLE = "music.toggle";
constexpr const char *ACTION_NEXT = "music.next";
constexpr const char *ACTION_REFRESH = "music.refresh";
constexpr const char *ACTION_SEEK = "music.seek";
constexpr const char *ACTION_VOLUME = "music.volume";

constexpr const char *TRACK_DIRS[] = {
    "/littlefs/sounds",
    "/sdcard/music",
    "/sdcard",
};

constexpr uint8_t PLAYBACK_VOLUME = 75;
constexpr size_t MAX_NAME_LENGTH = 40;

// Ignore slider echoes for a moment after seeking so a programmatic refresh
// cannot drag the playback position around.
constexpr int64_t SEEK_HOLD_MS = 700;

int64_t now_ms()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch()
           ).count();
}

bool has_wav_extension(const std::string &name)
{
    if (name.size() < 4) {
        return false;
    }
    const auto ends_with_wav = std::equal(
                                   name.end() - 4, name.end(), ".wav",
                                   [](char a, char b) {
        return std::tolower(static_cast<unsigned char>(a)) ==
               std::tolower(static_cast<unsigned char>(b));
    }
                               );
    return ends_with_wav;
}

std::string file_name_of(const std::string &path)
{
    const auto slash = path.find_last_of('/');
    std::string name = (slash == std::string::npos) ? path : path.substr(slash + 1);
    if (name.size() > MAX_NAME_LENGTH) {
        name.resize(MAX_NAME_LENGTH - 3);
        // Never cut a UTF-8 sequence in half.
        while (!name.empty() && (static_cast<unsigned char>(name.back()) & 0xC0) == 0x80) {
            name.pop_back();
        }
        name += "...";
    }
    return name;
}

uint32_t read_u32(const uint8_t *data)
{
    return static_cast<uint32_t>(data[0]) | (static_cast<uint32_t>(data[1]) << 8) |
           (static_cast<uint32_t>(data[2]) << 16) | (static_cast<uint32_t>(data[3]) << 24);
}

uint16_t read_u16(const uint8_t *data)
{
    return static_cast<uint16_t>(static_cast<uint16_t>(data[0]) | (static_cast<uint16_t>(data[1]) << 8));
}

bool parse_wav_header(std::FILE *file, WavInfo &info)
{
    uint8_t riff[12] = {};
    if (std::fread(riff, 1, sizeof(riff), file) != sizeof(riff) ||
            std::memcmp(riff, "RIFF", 4) != 0 || std::memcmp(riff + 8, "WAVE", 4) != 0) {
        return false;
    }

    bool have_format = false;
    while (true) {
        uint8_t chunk[8] = {};
        if (std::fread(chunk, 1, sizeof(chunk), file) != sizeof(chunk)) {
            return false;
        }
        const uint32_t size = read_u32(chunk + 4);
        if (std::memcmp(chunk, "fmt ", 4) == 0) {
            if (size < 16) {
                return false;
            }
            uint8_t format[16] = {};
            if (std::fread(format, 1, sizeof(format), file) != sizeof(format)) {
                return false;
            }
            const auto audio_format = read_u16(format);
            if (audio_format != 1) {
                BROOKESIA_LOGW("Music: unsupported WAV format %1% (PCM only)", audio_format);
                return false;
            }
            info.channels = read_u16(format + 2);
            info.sample_rate = read_u32(format + 4);
            info.bits = read_u16(format + 14);
            if (info.channels == 0 || info.sample_rate == 0 || (info.bits != 8 && info.bits != 16 &&
                    info.bits != 24 && info.bits != 32)) {
                BROOKESIA_LOGW("Music: unsupported WAV parameters");
                return false;
            }
            have_format = true;
            if (size > sizeof(format)) {
                std::fseek(file, static_cast<long>(size - sizeof(format)), SEEK_CUR);
            }
            continue;
        }
        if (std::memcmp(chunk, "data", 4) == 0) {
            if (!have_format) {
                return false;
            }
            info.data_offset = static_cast<uint32_t>(std::ftell(file));
            info.data_size = size;
            return true;
        }
        std::fseek(file, static_cast<long>(size), SEEK_CUR);
    }
}

std::string format_time(uint32_t seconds)
{
    char buffer[16] = {};
    std::snprintf(
        buffer, sizeof(buffer), "%02u:%02u",
        static_cast<unsigned>(seconds / 60U), static_cast<unsigned>(seconds % 60U)
    );
    return buffer;
}

} // namespace

MusicApp::MusicApp() = default;
MusicApp::~MusicApp() = default;

system::core::AppManifest MusicApp::get_manifest() const
{
    return {
        .id = APP_ID,
        .name = APP_NAME,
        .localized_names = {
            {"zh_CN", APP_NAME_ZH_CN},
        },
        .version = "1.0.0",
        .kind = system::core::AppKind::Native,
        .visible = true,
        .preload_dom = true,
        .icon_id = APP_ICON_ID,
        .supported_systems = {},
        .icon_path = "",
        .runtime_type = runtime::BackendType::Unknown,
        .app_path = "",
        .entry = "",
        .resource_dir = APP_ID,
        .arguments = {},
    };
}

system::core::AppGuiDescriptor MusicApp::get_gui_descriptor() const
{
    return {
        .root_kind = system::core::GuiRootKind::File,
        .root = GUI_ROOT,
        .resources = {},
        .screen_flows = {
            system::core::GuiScreenFlowEntry{
                .screen_flow = MUSIC_FLOW_ID,
                .layer = system::core::GuiAppLayer::AppDefault,
            },
        },
    };
}

std::expected<void, std::string> MusicApp::on_install(system::core::AppContext &)
{
    BROOKESIA_LOGI("Music installed");
    return {};
}

void MusicApp::on_uninstall(system::core::AppContext &)
{
}

std::expected<void, std::string> MusicApp::on_start(system::core::AppContext &context)
{
    BROOKESIA_LOG_TRACE_GUARD_WITH_THIS();

    context_ = &context;
    paused_ui_ = false;
    last_programmed_permille_ = -1;
    last_programmed_volume_ = -1;
    requested_seek_ = -1;
    seek_hold_until_ms_ = 0;
    scan_tracks();
    subscribe_actions(context);
    start_engine();

    auto timer = context.timer().start_periodic(PLAYBACK_TIMER_NAME, PLAYBACK_PERIOD_MS);
    if (!timer) {
        BROOKESIA_LOGW("Failed to start music UI timer: %1%", timer.error());
    }

    if (tracks_.empty()) {
        (void)context.gui().set_binding_value(PATH_TRACK_META, "track_meta", "没有找到 WAV 文件");
        (void)context.gui().set_binding_value(
            PATH_STATUS, "status_text", "把 WAV 文件放到 SD 卡或 /littlefs/sounds 后点刷新列表"
        );
        return {};
    }

    // Reopening the app should continue with the track that was playing before.
    if (index_ >= tracks_.size()) {
        index_ = 0;
    }
    play_index(index_, true);
    return {};
}

std::expected<void, std::string> MusicApp::on_stop(system::core::AppContext &)
{
    stop_engine();
    action_connections_.clear();
    context_ = nullptr;
    return {};
}

std::expected<void, std::string> MusicApp::on_pause(system::core::AppContext &)
{
    // Keep playing in the background; only stop painting the hidden GUI.
    paused_ui_ = true;
    return {};
}

std::expected<void, std::string> MusicApp::on_resume(system::core::AppContext &context)
{
    paused_ui_ = false;
    refresh_ui(context);
    return {};
}

std::expected<void, std::string> MusicApp::on_timer(
    system::core::AppContext &context,
    system::core::TimerId timer_id,
    std::string_view name
)
{
    (void)timer_id;
    if (name != PLAYBACK_TIMER_NAME) {
        return {};
    }

    bool finished = false;
    bool restart_failed = false;
    {
        std::lock_guard<std::mutex> lock(state_.mutex);
        if (state_.track_finished) {
            state_.track_finished = false;
            finished = true;
        }
        if (state_.error) {
            state_.error = false;
            restart_failed = true;
        }
    }

    if (restart_failed && !paused_ui_) {
        refresh_status("无法播放这个 WAV 文件（仅支持 PCM WAV）");
        refresh_ui(context);
        return {};
    }

    if (finished && !tracks_.empty()) {
        // Auto-advance to the next track.
        play_index((index_ + 1) % tracks_.size(), true);
    }

    if (requested_seek_ >= 0) {
        const int32_t target = requested_seek_;
        requested_seek_ = -1;
        apply_seek(target);
    }

    if (!paused_ui_) {
        refresh_ui(context);
    }
    return {};
}

void MusicApp::scan_tracks()
{
    tracks_.clear();
    for (const auto *dir_path : TRACK_DIRS) {
        auto *dir = opendir(dir_path);
        if (dir == nullptr) {
            continue;
        }
        while (auto *entry = readdir(dir)) {
            const std::string name = entry->d_name;
            if (name == "." || name == ".." || !has_wav_extension(name)) {
                continue;
            }
            const std::string full_path = std::string(dir_path) + "/" + name;
            struct stat st {};
            if (stat(full_path.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) {
                continue;
            }
            tracks_.push_back(full_path);
        }
        closedir(dir);
    }
    std::sort(tracks_.begin(), tracks_.end());
    tracks_.erase(std::unique(tracks_.begin(), tracks_.end()), tracks_.end());
    BROOKESIA_LOGI("Music found %1% track(s)", tracks_.size());
}

void MusicApp::subscribe_actions(system::core::AppContext &context)
{
    std::vector<system::core::GuiActionHandlerSubscription> subscriptions;
    subscriptions.push_back({
        .action = ACTION_PREV,
        .handler = [this](const gui::Event &) {
            step_track(-1);
        },
    });
    subscriptions.push_back({
        .action = ACTION_TOGGLE,
        .handler = [this](const gui::Event &) {
            toggle_playback();
        },
    });
    subscriptions.push_back({
        .action = ACTION_NEXT,
        .handler = [this](const gui::Event &) {
            step_track(1);
        },
    });
    subscriptions.push_back({
        .action = ACTION_REFRESH,
        .handler = [this](const gui::Event &) {
            scan_tracks();
            if (tracks_.empty()) {
                stop_engine();
                start_engine();
                index_ = 0;
                if (context_ != nullptr) {
                    refresh_status("没有找到 WAV 文件");
                    refresh_ui(*context_);
                }
                return;
            }
            if (index_ >= tracks_.size()) {
                index_ = 0;
            }
            play_index(index_, true);
            refresh_status("列表已刷新");
        },
    });
    subscriptions.push_back({
        .action = ACTION_SEEK,
        .handler = [this](const gui::Event &event) {
            if (const auto value = event.get_int64("value")) {
                seek_to(static_cast<int32_t>(*value));
            }
        },
    });
    subscriptions.push_back({
        .action = ACTION_VOLUME,
        .handler = [this](const gui::Event &event) {
            if (const auto value = event.get_int64("value")) {
                set_volume(static_cast<int>(*value));
            }
        },
    });

    action_connections_ = context.gui().subscribe_actions(subscriptions);
}

void MusicApp::play_index(size_t index, bool start_playback)
{
    if (tracks_.empty()) {
        return;
    }
    index_ = index % tracks_.size();
    requested_seek_ = -1;
    {
        std::lock_guard<std::mutex> lock(state_.mutex);
        state_.path = tracks_[index_];
        state_.generation++;
        state_.byte_pos = 0;
        state_.seek_pending = false;
        state_.data_size = 0;
        state_.sample_rate = 0;
        state_.channels = 0;
        state_.bits = 16;
        state_.track_finished = false;
        state_.error = false;
        state_.play_requested = start_playback;
        if (start_playback) {
            state_.paused = false;
        }
    }
    notify_engine();
    if (context_ != nullptr && !paused_ui_) {
        refresh_status("正在播放：" + file_name_of(tracks_[index_]));
        refresh_ui(*context_);
    }
}

void MusicApp::toggle_playback()
{
    if (tracks_.empty()) {
        refresh_status("没有找到 WAV 文件");
        return;
    }
    bool now_playing = false;
    {
        std::lock_guard<std::mutex> lock(state_.mutex);
        if (!state_.play_requested) {
            // Nothing loaded yet (or playback reached the end): start this track.
            state_.path = tracks_[index_];
            state_.generation++;
            state_.byte_pos = 0;
            state_.paused = false;
            state_.play_requested = true;
            now_playing = true;
        } else if (state_.paused) {
            state_.paused = false;
            now_playing = true;
        } else {
            state_.paused = true;
            now_playing = false;
        }
    }
    notify_engine();
    refresh_status(now_playing ? "正在播放" : "已暂停");
    if (context_ != nullptr) {
        refresh_ui(*context_);
    }
}

void MusicApp::step_track(int direction)
{
    if (tracks_.empty()) {
        refresh_status("没有找到 WAV 文件");
        return;
    }
    const int count = static_cast<int>(tracks_.size());
    int next = (static_cast<int>(index_) + direction) % count;
    if (next < 0) {
        next += count;
    }
    play_index(static_cast<size_t>(next), true);
}

void MusicApp::seek_to(int32_t permille)
{
    if (tracks_.empty()) {
        return;
    }
    permille = std::clamp<int32_t>(permille, 0, 1000);
    if (permille == last_programmed_permille_) {
        // Echo of the position we just painted, not a user request.
        return;
    }
    // Dragging the slider fires a burst of events; remember the newest position
    // and let the UI timer apply it once, instead of restarting the codec for
    // every intermediate value.
    requested_seek_ = permille;
}

void MusicApp::apply_seek(int32_t permille)
{
    if (tracks_.empty()) {
        return;
    }
    permille = std::clamp<int32_t>(permille, 0, 1000);
    {
        std::lock_guard<std::mutex> lock(state_.mutex);
        if (!state_.play_requested || state_.data_size == 0) {
            return;
        }
        const uint32_t target = static_cast<uint32_t>(
                                    (static_cast<uint64_t>(permille) * state_.data_size) / 1000U
                                );
        state_.seek_pos = target;
        state_.seek_pending = true;
        state_.paused = false;
        state_.play_requested = true;
        state_.byte_pos = target;
        last_programmed_permille_ = permille;
    }
    seek_hold_until_ms_ = now_ms() + SEEK_HOLD_MS;
    notify_engine();
    BROOKESIA_LOGI("Music: seek to %1%%%", permille / 10);
}

void MusicApp::set_volume(int volume)
{
    volume = std::clamp(volume, 0, 100);
    if (volume == last_programmed_volume_) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(state_.mutex);
        state_.volume = volume;
        state_.volume_changed = true;
    }
    volume_ = volume;
    notify_engine();
    if (context_ != nullptr && !paused_ui_) {
        refresh_ui(*context_);
    }
}


void MusicApp::refresh_status(const std::string &text)
{
    if (context_ == nullptr) {
        return;
    }
    (void)context_->gui().set_binding_value(PATH_STATUS, "status_text", text);
}

void MusicApp::refresh_ui(system::core::AppContext &context)
{
    if (tracks_.empty()) {
        (void)context.gui().set_binding_value(PATH_TRACK_NAME, "track_name", "未在播放");
        (void)context.gui().set_binding_value(PATH_TRACK_META, "track_meta", "没有找到 WAV 文件");
        (void)context.gui().set_binding_value(PATH_TIME_LABEL, "time_label", "00:00 / 00:00");
        (void)context.gui().set_value(PATH_PROGRESS, 0);
        (void)context.gui().set_binding_value(PATH_TOGGLE_TEXT, "toggle_text", "播放");
        (void)context.gui().set_value(PATH_VOLUME_SLIDER, volume_);
        (void)context.gui().set_binding_value(
            PATH_VOLUME_TEXT, "volume_text", std::to_string(volume_) + "%"
        );
        return;
    }

    uint32_t byte_pos = 0;
    uint32_t data_size = 0;
    uint32_t sample_rate = 0;
    uint16_t channels = 0;
    uint16_t bits = 16;
    int32_t volume = volume_;
    bool playing = false;
    bool paused = false;
    {
        std::lock_guard<std::mutex> lock(state_.mutex);
        byte_pos = state_.byte_pos;
        data_size = state_.data_size;
        sample_rate = state_.sample_rate;
        channels = state_.channels;
        bits = state_.bits;
        volume = state_.volume;
        playing = state_.play_requested && !state_.paused;
        paused = state_.paused;
    }

    (void)context.gui().set_binding_value(PATH_TRACK_NAME, "track_name", file_name_of(tracks_[index_]));
    std::string meta = "第 " + std::to_string(index_ + 1) + " / " + std::to_string(tracks_.size()) + " 首";
    if (sample_rate > 0) {
        char detail[64] = {};
        std::snprintf(
            detail, sizeof(detail), " · %.1f kHz · %s · %u-bit", sample_rate / 1000.0,
            (channels >= 2) ? "立体声" : "单声道", static_cast<unsigned>(bits)
        );
        meta += detail;
    }
    (void)context.gui().set_binding_value(PATH_TRACK_META, "track_meta", meta);

    const uint32_t bytes_per_second = sample_rate * channels * (bits / 8U);
    const uint32_t elapsed = (bytes_per_second > 0) ? (byte_pos / bytes_per_second) : 0;
    const uint32_t total = (bytes_per_second > 0 && data_size > 0) ? (data_size / bytes_per_second) : 0;
    (void)context.gui().set_binding_value(
        PATH_TIME_LABEL, "time_label", format_time(elapsed) + " / " + format_time(total)
    );

    int32_t permille = 0;
    if (data_size > 0) {
        permille = static_cast<int32_t>((static_cast<uint64_t>(byte_pos) * 1000U) / data_size);
        permille = std::clamp<int32_t>(permille, 0, 1000);
    }
    // Keep the knob where the finger left it while a seek is being applied.
    if (now_ms() >= seek_hold_until_ms_) {
        last_programmed_permille_ = permille;
        (void)context.gui().set_value(PATH_PROGRESS, permille);
    }

    (void)context.gui().set_binding_value(
        PATH_VOLUME_TEXT, "volume_text", std::to_string(volume) + "%"
    );
    if (volume != last_programmed_volume_) {
        last_programmed_volume_ = volume;
        (void)context.gui().set_value(PATH_VOLUME_SLIDER, volume);
    }

    const char *toggle_text = playing ? "暂停" : (paused ? "继续" : "播放");
    (void)context.gui().set_binding_value(PATH_TOGGLE_TEXT, "toggle_text", toggle_text);
}

void MusicApp::start_engine()
{
    if (engine_started_) {
        return;
    }
    // Acquire the codec player on the app task; the playback thread then reuses
    // this handle so no HAL lookup happens off the app task.
    player_ = hal::acquire_first_interface<hal::audio::CodecPlayerIface>();
    if (!static_cast<bool>(player_)) {
        BROOKESIA_LOGW("Music: audio codec player is not available");
        std::lock_guard<std::mutex> lock(state_.mutex);
        state_.error = true;
        state_.error_text = "audio codec unavailable";
        state_.play_requested = false;
        return;
    }
    {
        std::lock_guard<std::mutex> lock(state_.mutex);
        state_.quit = false;
        state_.play_requested = false;
        state_.paused = false;
        state_.byte_pos = 0;
        state_.data_size = 0;
        state_.track_finished = false;
        state_.error = false;
        state_.seek_pending = false;
        state_.volume = volume_;
        state_.volume_changed = false;
    }
    {
        BROOKESIA_THREAD_CONFIG_GUARD({
            .stack_size = 10240,
            .stack_in_ext = true,
        });
        engine_thread_ = boost::thread([this]() {
            engine_loop();
        });
    }
    engine_started_ = true;
}

void MusicApp::stop_engine()
{
    if (!engine_started_) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(state_.mutex);
        state_.quit = true;
        state_.play_requested = false;
    }
    notify_engine();
    if (engine_thread_.joinable()) {
        engine_thread_.join();
    }
    engine_started_ = false;
    player_.reset();
}

void MusicApp::notify_engine()
{
    state_.cv.notify_all();
}

void MusicApp::engine_loop()
{
    if (!static_cast<bool>(player_)) {
        std::lock_guard<std::mutex> lock(state_.mutex);
        state_.error = true;
        state_.error_text = "audio codec unavailable";
        state_.play_requested = false;
        return;
    }

    std::vector<uint8_t> buffer(4096);

    while (true) {
        std::string path;
        uint32_t generation = 0;
        uint32_t start_pos = 0;
        {
            std::unique_lock<std::mutex> lock(state_.mutex);
            state_.cv.wait(lock, [this]() {
                return state_.quit || (state_.play_requested && !state_.paused);
            });
            if (state_.quit) {
                break;
            }
            path = state_.path;
            generation = state_.generation;
            start_pos = state_.byte_pos;
        }

        if (path.empty()) {
            continue;
        }

        std::FILE *file = std::fopen(path.c_str(), "rb");
        WavInfo info;
        if (file == nullptr || !parse_wav_header(file, info)) {
            if (file != nullptr) {
                std::fclose(file);
            }
            BROOKESIA_LOGW("Music: cannot read WAV file: %1%", path);
            std::lock_guard<std::mutex> lock(state_.mutex);
            if (state_.generation == generation) {
                state_.error = true;
                state_.error_text = "invalid wav";
                state_.play_requested = false;
                state_.byte_pos = 0;
                state_.data_size = 0;
            }
            continue;
        }

        if (start_pos >= info.data_size) {
            start_pos = 0;
        }
        {
            std::lock_guard<std::mutex> lock(state_.mutex);
            if (state_.generation != generation) {
                std::fclose(file);
                continue;
            }
            state_.data_size = info.data_size;
            state_.sample_rate = info.sample_rate;
            state_.channels = info.channels;
            state_.bits = info.bits;
            state_.byte_pos = start_pos;
        }

        hal::audio::CodecPlayerIface::Config config = {
            .bits = static_cast<uint8_t>(info.bits),
            .channels = static_cast<uint8_t>(info.channels),
            .sample_rate = info.sample_rate,
        };
        if (!player_->open(config)) {
            BROOKESIA_LOGW("Music: failed to open the codec player");
            std::fclose(file);
            std::lock_guard<std::mutex> lock(state_.mutex);
            if (state_.generation == generation) {
                state_.error = true;
                state_.error_text = "codec open failed";
                state_.play_requested = false;
            }
            continue;
        }
        player_->set_volume(static_cast<uint8_t>(volume_));
        {
            std::lock_guard<std::mutex> lock(state_.mutex);
            state_.volume = volume_;
            state_.volume_changed = false;
        }
        if (player_->is_pa_on_off_supported() && !player_->is_pa_on()) {
            player_->set_pa_on_off(true);
        }

        (void)std::fseek(file, static_cast<long>(info.data_offset + start_pos), SEEK_SET);
        uint32_t pos = start_pos;
        while (true) {
            bool should_stop = false;
            bool apply_volume = false;
            int volume = 0;
            {
                std::lock_guard<std::mutex> lock(state_.mutex);
                should_stop = state_.quit || state_.paused || !state_.play_requested ||
                              state_.generation != generation;
                if (state_.seek_pending && state_.generation == generation) {
                    // Leave the loop so the outer one reopens at the new offset.
                    should_stop = true;
                }
                if (state_.volume_changed) {
                    state_.volume_changed = false;
                    apply_volume = true;
                    volume = state_.volume;
                }
            }
            if (apply_volume) {
                player_->set_volume(static_cast<uint8_t>(volume));
            }
            if (should_stop) {
                break;
            }
            const uint32_t remaining = (pos < info.data_size) ? (info.data_size - pos) : 0;
            if (remaining == 0) {
                break;
            }
            const size_t want = std::min<size_t>(remaining, buffer.size());
            const size_t read = std::fread(buffer.data(), 1, want, file);
            if (read == 0) {
                break;
            }
            player_->write_data(buffer.data(), read);
            pos += static_cast<uint32_t>(read);
            {
                std::lock_guard<std::mutex> lock(state_.mutex);
                state_.byte_pos = pos;
            }
        }

        player_->close();
        std::fclose(file);

        bool seeked = false;
        {
            std::lock_guard<std::mutex> lock(state_.mutex);
            if (state_.seek_pending && state_.generation == generation) {
                state_.seek_pending = false;
                state_.byte_pos = state_.seek_pos;
                state_.paused = false;
                seeked = true;
            }
        }
        if (seeked) {
            continue;
        }

        {
            std::lock_guard<std::mutex> lock(state_.mutex);
            const bool at_end = (pos >= state_.data_size) && state_.generation == generation;
            if (at_end && state_.play_requested && !state_.paused && !state_.quit) {
                state_.play_requested = false;
                state_.track_finished = true;
            }
        }
    }

    if (static_cast<bool>(player_)) {
        player_->close();
    }
}

} // namespace esp_brookesia::app::music

BROOKESIA_SYSTEM_CORE_APP_PROVIDER_REGISTER_WITH_SYMBOL(
    esp_brookesia::app::music::MusicAppProvider,
    "brookesia.waveshare.music",
    app_music_provider_symbol
);
