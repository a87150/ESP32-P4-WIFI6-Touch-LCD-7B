/*
 * SPDX-License-Identifier: Apache-2.0
 */
#include "brookesia/app_music/music_app.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <dirent.h>
#include <sys/stat.h>

#include "brookesia/lib_utils/thread_config.hpp"

#include "minimp3.h"
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
constexpr const char *UI_TIMER_NAME = "music_ui";
constexpr int32_t UI_PERIOD_MS = 110;
// The timer also drives the waveform, so only every N-th tick refreshes the
// text bindings (~440 ms, close to the old 500 ms progress cadence).
constexpr int32_t UI_REFRESH_TICKS = 4;

// Absolute paths inside the music screen document.
constexpr const char *PATH_DRAWER = "/music/drawer";
constexpr const char *PATH_LIST = "/music/drawer/list";
constexpr const char *PATH_LIST_INFO = "/music/drawer/head/list_info";
constexpr const char *PATH_PAGER = "/music/drawer/pager";
constexpr const char *PATH_PAGE_TEXT = "/music/drawer/pager/page";
constexpr const char *PATH_TRACK_NAME = "/music/page/player_card/info/track_name";
constexpr const char *PATH_TRACK_META = "/music/page/player_card/info/track_meta";
constexpr const char *PATH_TRACK_DETAIL = "/music/page/player_card/info/track_detail";
constexpr const char *PATH_PROGRESS = "/music/page/player_card/info/progress";
constexpr const char *PATH_TIME_LABEL = "/music/page/player_card/info/time_label";
constexpr const char *PATH_WAVE_GLOW = "/music/page/player_card/info/wave/wave_glow";
constexpr const char *PATH_WAVE_LINE = "/music/page/player_card/info/wave/wave_line";
constexpr int WAVE_POINT_COUNT = 49;
constexpr int WAVE_WIDTH_PX = 632;
constexpr int WAVE_HEIGHT_PX = 88;
constexpr int WAVE_CENTER_Y = WAVE_HEIGHT_PX / 2;
constexpr int WAVE_AMPLITUDE_PX = 30;
constexpr const char *PATH_TOGGLE_TEXT = "/music/page/controls/toggle/text";
constexpr const char *PATH_AUTO_NEXT_TEXT = "/music/page/controls/right_group/auto_next/text";
constexpr const char *PATH_STATUS = "/music/page/status";

// The playlist is paged so a full SD card never builds hundreds of views.
constexpr const char *LIST_TEMPLATE_ID = "music_list_item";
constexpr size_t LIST_PAGE_SIZE = 5;

constexpr const char *ACTION_OPEN = "music.open";
constexpr const char *ACTION_LIST = "music.list";
constexpr const char *ACTION_CLOSE = "music.close";
constexpr const char *ACTION_PAGE_PREV = "music.page.prev";
constexpr const char *ACTION_PAGE_NEXT = "music.page.next";
constexpr const char *ACTION_PREV = "music.prev";
constexpr const char *ACTION_TOGGLE = "music.toggle";
constexpr const char *ACTION_NEXT = "music.next";
constexpr const char *ACTION_AUTO_NEXT = "music.auto_next";
constexpr const char *ACTION_REFRESH = "music.refresh";
constexpr const char *ACTION_SEEK = "music.seek";
constexpr const char *ACTION_VOLUME_DOWN = "music.volume.down";
constexpr const char *ACTION_VOLUME_UP = "music.volume.up";

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

std::string make_wave_points(const std::array<int32_t, WAVE_POINT_COUNT> &ys, int32_t y_offset)
{
    std::string result;
    result.reserve(16 + WAVE_POINT_COUNT * 20);
    result += '[';
    for (int i = 0; i < WAVE_POINT_COUNT; i++) {
        if (i != 0) {
            result += ',';
        }
        const int32_t x = static_cast<int32_t>(
                              std::lround(static_cast<double>(i) * (WAVE_WIDTH_PX - 1) / (WAVE_POINT_COUNT - 1))
                          );
        const int32_t y = std::clamp<int32_t>(ys[i] + y_offset, 2, WAVE_HEIGHT_PX - 2);
        result += "{\"x\":";
        result += std::to_string(x);
        result += ",\"y\":";
        result += std::to_string(y);
        result += '}';
    }
    result += ']';
    return result;
}

bool has_extension(const std::string &name, const char *extension)
{
    const size_t length = std::strlen(extension);
    if (name.size() < length) {
        return false;
    }
    return std::equal(
               name.end() - static_cast<std::ptrdiff_t>(length), name.end(), extension,
               [](char a, char b) {
        return std::tolower(static_cast<unsigned char>(a)) ==
               std::tolower(static_cast<unsigned char>(b));
    }
           );
}

bool has_wav_extension(const std::string &name)
{
    return has_extension(name, ".wav");
}

bool has_mp3_extension(const std::string &name)
{
    return has_extension(name, ".mp3");
}

bool has_audio_extension(const std::string &name)
{
    // Only formats the player can actually stream: PCM WAV and MPEG Layer III.
    return has_wav_extension(name) || has_mp3_extension(name);
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

// Row instances are named "item<index>"; the click event carries the absolute
// path of the instance, so the index is the trailing number.
size_t item_index_of(std::string_view path)
{
    const auto marker = path.rfind("/item");
    if (marker == std::string_view::npos) {
        return static_cast<size_t>(-1);
    }
    size_t index = 0;
    for (size_t i = marker + 5; i < path.size(); ++i) {
        const auto character = static_cast<unsigned char>(path[i]);
        if (!std::isdigit(character)) {
            break;
        }
        index = index * 10 + static_cast<size_t>(character - '0');
    }
    return index;
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

struct Mp3Header {
    uint32_t sample_rate = 0;
    uint32_t frame_bytes = 0;
    uint32_t samples = 0;
    uint32_t bitrate_kbps = 0;
    uint16_t channels = 0;
};

// Parse a 4-byte MPEG audio frame header. Only Layer III is accepted; that is
// what every .mp3 file in the wild uses.
bool parse_mp3_header(const uint8_t *data, Mp3Header &header)
{
    if (data[0] != 0xFF || (data[1] & 0xE0) != 0xE0) {
        return false;
    }
    const uint8_t version = (data[1] >> 3) & 0x03; // 0 = 2.5, 2 = 2, 3 = 1
    const uint8_t layer = (data[1] >> 1) & 0x03;   // 1 = Layer III
    const uint8_t bitrate_index = (data[2] >> 4) & 0x0F;
    const uint8_t rate_index = (data[2] >> 2) & 0x03;
    const uint8_t padding = (data[2] >> 1) & 0x01;
    const uint8_t channel_mode = (data[3] >> 6) & 0x03;

    if (version == 1 || layer != 1 || bitrate_index == 0 || bitrate_index == 15 || rate_index == 3) {
        return false;
    }

    static const uint16_t BITRATES_MPEG1[16] = {
        0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 0
    };
    static const uint16_t BITRATES_MPEG2[16] = {
        0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0
    };
    static const uint32_t SAMPLE_RATES[3] = {44100, 48000, 32000};

    const uint32_t base_rate = SAMPLE_RATES[rate_index];
    if (version == 3) {
        header.bitrate_kbps = BITRATES_MPEG1[bitrate_index];
        header.sample_rate = base_rate;
        header.samples = 1152;
        header.frame_bytes = (144000U * header.bitrate_kbps) / header.sample_rate + padding;
    } else {
        header.bitrate_kbps = BITRATES_MPEG2[bitrate_index];
        header.sample_rate = (version == 2) ? (base_rate / 2U) : (base_rate / 4U);
        header.samples = 576;
        header.frame_bytes = (72000U * header.bitrate_kbps) / header.sample_rate + padding;
    }
    header.channels = (channel_mode == 3) ? 1 : 2;
    return header.frame_bytes >= 4;
}

uint32_t read_synchsafe(const uint8_t *data)
{
    return (static_cast<uint32_t>(data[0] & 0x7F) << 21) |
           (static_cast<uint32_t>(data[1] & 0x7F) << 14) |
           (static_cast<uint32_t>(data[2] & 0x7F) << 7) |
           static_cast<uint32_t>(data[3] & 0x7F);
}

const char *format_tag_of(const std::string &path)
{
    return has_mp3_extension(path) ? "MP3" : "WAV";
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

std::string format_size(uint64_t bytes)
{
    char text[32] = {};
    if (bytes >= 1024ULL * 1024ULL) {
        std::snprintf(text, sizeof(text), "%.1f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
    } else if (bytes >= 1024ULL) {
        std::snprintf(text, sizeof(text), "%.0f KB", static_cast<double>(bytes) / 1024.0);
    } else {
        std::snprintf(text, sizeof(text), "%llu B", static_cast<unsigned long long>(bytes));
    }
    return text;
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
    requested_seek_ = -1;
    seek_hold_until_ms_ = 0;
    wave_points_.fill(-1000);
    list_page_ = 0;
    auto_next_ = false;
    scan_tracks();
    subscribe_actions(context);
    start_engine();
    (void)context.gui().set_binding_value(PATH_AUTO_NEXT_TEXT, "auto_next_text", "连播：关");

    auto timer = context.timer().start_periodic(UI_TIMER_NAME, UI_PERIOD_MS);
    if (!timer) {
        BROOKESIA_LOGW("Failed to start music UI timer: %1%", timer.error());
    }

    if (tracks_.empty()) {
        (void)context.gui().set_binding_value(PATH_TRACK_NAME, "track_name", "未在播放");
        (void)context.gui().set_binding_value(PATH_TRACK_META, "track_meta", "没有找到 WAV / MP3 文件");
        (void)context.gui().set_binding_value(
            PATH_TRACK_DETAIL, "track_detail", "把 WAV / MP3 放进 /littlefs/sounds 或 SD 卡后点「重新扫描」"
        );
        (void)context.gui().set_binding_value(PATH_STATUS, "status_text", "");
    }
    // The player keeps the full width; the playlist only slides over it when
    // the user asks for it, so entering the app is instant and silent.
    close_list(context);
    rebuild_list(context);
    return {};
}

std::expected<void, std::string> MusicApp::on_stop(system::core::AppContext &context)
{
    destroy_list(context);
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
    close_list(context);
    bool has_track = false;
    {
        std::lock_guard<std::mutex> lock(state_.mutex);
        has_track = !state_.path.empty();
    }
    if (has_track) {
        // Coming back to a track that is still loaded (and possibly playing)
        // keeps the player view untouched.
        refresh_ui(context);
        return {};
    }
    rebuild_list(context);
    return {};
}

std::expected<void, std::string> MusicApp::on_timer(
    system::core::AppContext &context,
    system::core::TimerId timer_id,
    std::string_view name
)
{
    (void)timer_id;
    if (name != UI_TIMER_NAME) {
        return {};
    }

    // All GUI writes for this app happen from this single timer, so the
    // waveform and the text bindings are never applied concurrently.
    tick_wave(context);
    if (++ui_tick_ < UI_REFRESH_TICKS) {
        return {};
    }
    ui_tick_ = 0;

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
        refresh_status("无法播放这个文件（支持 PCM WAV 与 MP3）");
        refresh_ui(context);
        return {};
    }

    if (finished && !tracks_.empty()) {
        if (auto_next_) {
            // 连播打开：播完自动切下一首。
            play_index((index_ + 1) % tracks_.size(), true);
        } else {
            // 连播关闭：停在当前曲目，下次按播放从头重放。
            {
                std::lock_guard<std::mutex> lock(state_.mutex);
                state_.play_requested = false;
                state_.paused = false;
                state_.byte_pos = 0;
                state_.track_finished = false;
            }
            notify_engine();
            if (!paused_ui_) {
                refresh_status("播放完毕（连播已关闭）");
                refresh_ui(context);
            }
        }
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
    std::vector<std::pair<std::string, uint32_t>> found;
    for (const auto *dir_path : TRACK_DIRS) {
        auto *dir = opendir(dir_path);
        if (dir == nullptr) {
            continue;
        }
        while (auto *entry = readdir(dir)) {
            const std::string name = entry->d_name;
            if (name == "." || name == ".." || !has_audio_extension(name)) {
                continue;
            }
            const std::string full_path = std::string(dir_path) + "/" + name;
            struct stat st {};
            if (stat(full_path.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) {
                continue;
            }
            found.emplace_back(full_path, static_cast<uint32_t>(st.st_size));
        }
        closedir(dir);
    }
    std::sort(found.begin(), found.end());
    found.erase(std::unique(found.begin(), found.end()), found.end());
    track_bytes_list_.clear();
    tracks_.reserve(found.size());
    track_bytes_list_.reserve(found.size());
    for (const auto &entry : found) {
        tracks_.push_back(entry.first);
        track_bytes_list_.push_back(entry.second);
    }
    BROOKESIA_LOGI("Music found %1% track(s)", tracks_.size());
}

void MusicApp::subscribe_actions(system::core::AppContext &context)
{
    std::vector<system::core::GuiActionHandlerSubscription> subscriptions;
    subscriptions.push_back({
        .action = ACTION_OPEN,
        .handler = [this](const gui::Event &event) {
            if (context_ == nullptr) {
                return;
            }
            const size_t slot = item_index_of(event.path);
            if (slot >= list_paths_.size()) {
                return;
            }
            open_track(*context_, list_page_ * LIST_PAGE_SIZE + slot);
        },
    });
    subscriptions.push_back({
        .action = ACTION_LIST,
        .handler = [this](const gui::Event &) {
            if (context_ != nullptr) {
                open_list(*context_);
            }
        },
    });
    subscriptions.push_back({
        .action = ACTION_CLOSE,
        .handler = [this](const gui::Event &) {
            if (context_ != nullptr) {
                close_list(*context_);
            }
        },
    });
    subscriptions.push_back({
        .action = ACTION_PAGE_PREV,
        .handler = [this](const gui::Event &) {
            if (context_ != nullptr) {
                change_page(*context_, -1);
            }
        },
    });
    subscriptions.push_back({
        .action = ACTION_PAGE_NEXT,
        .handler = [this](const gui::Event &) {
            if (context_ != nullptr) {
                change_page(*context_, 1);
            }
        },
    });
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
        .action = ACTION_AUTO_NEXT,
        .handler = [this](const gui::Event &) {
            auto_next_ = !auto_next_;
            if (context_ == nullptr) {
                return;
            }
            (void)context_->gui().set_binding_value(
                PATH_AUTO_NEXT_TEXT, "auto_next_text", auto_next_ ? "连播：开" : "连播：关"
            );
            refresh_status(auto_next_ ? "播完自动播放下一首" : "播完停在当前曲目");
        },
    });
    subscriptions.push_back({
        .action = ACTION_REFRESH,
        .handler = [this](const gui::Event &) {
            scan_tracks();
            list_page_ = 0;
            if (context_ == nullptr) {
                return;
            }
            close_list(*context_);
            rebuild_list(*context_);
            if (tracks_.empty()) {
                refresh_status("没有找到 WAV / MP3 文件");
            }
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
        .action = ACTION_VOLUME_DOWN,
        .handler = [this](const gui::Event &) {
            adjust_volume(-10);
        },
    });
    subscriptions.push_back({
        .action = ACTION_VOLUME_UP,
        .handler = [this](const gui::Event &) {
            adjust_volume(10);
        },
    });

    action_connections_ = context.gui().subscribe_actions(subscriptions);
}

void MusicApp::destroy_list(system::core::AppContext &context)
{
    for (auto it = list_paths_.rbegin(); it != list_paths_.rend(); ++it) {
        (void)context.gui().destroy_view(*it);
    }
    list_paths_.clear();
}

void MusicApp::rebuild_list(system::core::AppContext &context)
{
    destroy_list(context);

    const size_t total = tracks_.size();
    const size_t page_count = (total + LIST_PAGE_SIZE - 1) / LIST_PAGE_SIZE;
    if (page_count == 0) {
        list_page_ = 0;
    } else if (list_page_ >= page_count) {
        list_page_ = page_count - 1;
    }

    const size_t start = list_page_ * LIST_PAGE_SIZE;
    const size_t end = std::min(start + LIST_PAGE_SIZE, total);

    bool has_track = false;
    bool track_playing = false;
    {
        std::lock_guard<std::mutex> lock(state_.mutex);
        has_track = !state_.path.empty();
        track_playing = has_track && state_.play_requested && !state_.paused;
    }

    for (size_t i = start; i < end; ++i) {
        const std::string id = "item" + std::to_string(i - start);
        const std::string path = std::string(PATH_LIST) + "/" + id;
        auto created = context.gui().create_view(LIST_TEMPLATE_ID, PATH_LIST, id);
        if (!created) {
            BROOKESIA_LOGW("Failed to create music list row: %1%", created.error());
            break;
        }
        list_paths_.push_back(path);

        const uint32_t bytes = (i < track_bytes_list_.size()) ? track_bytes_list_[i] : 0;
        const std::string meta = (bytes > 0) ? format_size(bytes) : std::string("--");
        const bool current = has_track && (i == index_);
        const char *tag = current ? (track_playing ? "播放中" : "已暂停") : format_tag_of(tracks_[i]);

        (void)context.gui().set_binding_value(path, "commonProps.hidden", "false");
        (void)context.gui().set_binding_value(path + "/text/title", "title", file_name_of(tracks_[i]));
        (void)context.gui().set_binding_value(path + "/text/meta", "meta", meta);
        (void)context.gui().set_binding_value(path + "/tag", "tag", tag);
    }

    std::string info;
    if (total == 0) {
        info = "没有找到音乐，把 WAV / MP3 放进 /littlefs/sounds 或 SD 卡";
    } else {
        info = "共 " + std::to_string(total) + " 首 · 第 " + std::to_string(start + 1) + "-" +
               std::to_string(end) + " 首";
    }
    (void)context.gui().set_binding_value(PATH_LIST_INFO, "list_info", info);
    (void)context.gui().set_binding_value(
        PATH_PAGE_TEXT, "page_text",
        std::to_string(page_count == 0 ? 0 : list_page_ + 1) + " / " + std::to_string(page_count)
    );
    (void)context.gui().set_binding_value(PATH_PAGER, "pager_hidden", page_count > 1 ? "false" : "true");
}

void MusicApp::open_list(system::core::AppContext &context)
{
    rebuild_list(context);
    (void)context.gui().set_binding_value(PATH_DRAWER, "drawer_hidden", "false");
}

void MusicApp::close_list(system::core::AppContext &context)
{
    (void)context.gui().set_binding_value(PATH_DRAWER, "drawer_hidden", "true");
}

void MusicApp::open_track(system::core::AppContext &context, size_t index)
{
    if (tracks_.empty()) {
        return;
    }
    close_list(context);
    play_index(index, true);
}

void MusicApp::change_page(system::core::AppContext &context, int direction)
{
    const size_t page_count = (tracks_.size() + LIST_PAGE_SIZE - 1) / LIST_PAGE_SIZE;
    if (page_count == 0) {
        return;
    }
    if (direction < 0) {
        if (list_page_ == 0) {
            return;
        }
        --list_page_;
    } else {
        if (list_page_ + 1 >= page_count) {
            return;
        }
        ++list_page_;
    }
    rebuild_list(context);
}

void MusicApp::play_index(size_t index, bool start_playback)
{
    if (tracks_.empty()) {
        return;
    }
    index_ = index % tracks_.size();
    requested_seek_ = -1;
    track_bytes_ = 0;
    struct stat st {};
    if (stat(tracks_[index_].c_str(), &st) == 0 && S_ISREG(st.st_mode)) {
        track_bytes_ = static_cast<uint32_t>(st.st_size);
    }
    {
        std::lock_guard<std::mutex> lock(state_.mutex);
        state_.path = tracks_[index_];
        state_.generation++;
        state_.byte_pos = 0;
        state_.seek_pending = false;
        state_.data_size = 0;
        state_.sample_rate = 0;
        state_.samples_per_frame = 0;
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
        refresh_status("没有找到 WAV / MP3 文件");
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
        refresh_status("没有找到 WAV / MP3 文件");
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

void MusicApp::adjust_volume(int delta)
{
    set_volume(volume_ + delta);
}

void MusicApp::set_volume(int volume)
{
    volume = std::clamp(volume, 0, 100);
    if (volume == volume_) {
        refresh_status("音量 " + std::to_string(volume) + "%");
        return;
    }
    {
        std::lock_guard<std::mutex> lock(state_.mutex);
        state_.volume = volume;
        state_.volume_changed = true;
    }
    volume_ = volume;
    notify_engine();
    refresh_status("音量 " + std::to_string(volume) + "%");
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
        (void)context.gui().set_binding_value(PATH_TRACK_META, "track_meta", "没有找到 WAV / MP3 文件");
        (void)context.gui().set_binding_value(
            PATH_TRACK_DETAIL, "track_detail", "把 WAV / MP3 放进 /littlefs/sounds 或 SD 卡后点「重新扫描」"
        );
        (void)context.gui().set_binding_value(PATH_TIME_LABEL, "time_label", "00:00 / 00:00");
        (void)context.gui().set_value(PATH_PROGRESS, 0);
        (void)context.gui().set_binding_value(PATH_TOGGLE_TEXT, "toggle_text", "播放");
        return;
    }

    uint32_t byte_pos = 0;
    uint32_t data_size = 0;
    uint32_t sample_rate = 0;
    uint32_t samples_per_frame = 0;
    uint16_t channels = 0;
    uint16_t bits = 16;
    bool playing = false;
    bool paused = false;
    bool has_track = false;
    {
        std::lock_guard<std::mutex> lock(state_.mutex);
        byte_pos = state_.byte_pos;
        data_size = state_.data_size;
        sample_rate = state_.sample_rate;
        samples_per_frame = state_.samples_per_frame;
        channels = state_.channels;
        bits = state_.bits;
        playing = state_.play_requested && !state_.paused;
        paused = state_.paused;
        has_track = !state_.path.empty();
    }

    if (!has_track) {
        // The playlist is open and nothing has been picked yet.
        (void)context.gui().set_binding_value(PATH_TRACK_NAME, "track_name", "未在播放");
        (void)context.gui().set_binding_value(
            PATH_TRACK_META, "track_meta",
            "共 " + std::to_string(tracks_.size()) + " 首 · 点列表选择播放"
        );
        (void)context.gui().set_binding_value(PATH_TRACK_DETAIL, "track_detail", "选一首开始播放，离开 App 后继续");
        (void)context.gui().set_binding_value(PATH_TIME_LABEL, "time_label", "00:00 / 00:00");
        (void)context.gui().set_value(PATH_PROGRESS, 0);
        (void)context.gui().set_binding_value(PATH_TOGGLE_TEXT, "toggle_text", "播放");
        return;
    }

    const bool is_mp3 = samples_per_frame > 0;
    (void)context.gui().set_binding_value(PATH_TRACK_NAME, "track_name", file_name_of(tracks_[index_]));
    std::string meta = "第 " + std::to_string(index_ + 1) + " / " + std::to_string(tracks_.size()) +
                       " 首 · " + (is_mp3 ? "MP3" : "WAV");
    std::string detail;
    uint32_t total = 0;
    if (is_mp3 && sample_rate > 0 && data_size > 0) {
        total = static_cast<uint32_t>(
                    (static_cast<uint64_t>(data_size) * samples_per_frame) / sample_rate
                );
        const auto kbps = (total > 0 && track_bytes_ > 0) ?
                          static_cast<unsigned>((static_cast<uint64_t>(track_bytes_) * 8U) / total / 1000U) :
                          0U;
        const char *layout = (channels >= 2) ? "立体声" : "单声道";
        char text[96] = {};
        if (track_bytes_ > 0) {
            std::snprintf(
                text, sizeof(text), "%.1f kHz · %s · 16-bit · %u kbps · %s", sample_rate / 1000.0,
                layout, kbps, format_size(track_bytes_).c_str()
            );
        } else {
            std::snprintf(
                text, sizeof(text), "%.1f kHz · %s · 16-bit · %u kbps", sample_rate / 1000.0,
                layout, kbps
            );
        }
        detail = text;
    } else if (sample_rate > 0) {
        char text[96] = {};
        const auto kbps = static_cast<unsigned>(
                              (static_cast<uint64_t>(sample_rate) * channels * bits) / 1000U
                          );
        const char *layout = (channels >= 2) ? "立体声" : "单声道";
        if (track_bytes_ > 0) {
            std::snprintf(
                text, sizeof(text), "%.1f kHz · %s · %u-bit · %u kbps · %s", sample_rate / 1000.0,
                layout, static_cast<unsigned>(bits), kbps, format_size(track_bytes_).c_str()
            );
        } else {
            std::snprintf(
                text, sizeof(text), "%.1f kHz · %s · %u-bit · %u kbps", sample_rate / 1000.0,
                layout, static_cast<unsigned>(bits), kbps
            );
        }
        detail = text;
    }
    (void)context.gui().set_binding_value(PATH_TRACK_META, "track_meta", meta);
    (void)context.gui().set_binding_value(PATH_TRACK_DETAIL, "track_detail", detail);

    uint32_t elapsed = 0;
    if (is_mp3) {
        if (sample_rate > 0 && data_size > 0) {
            elapsed = static_cast<uint32_t>(
                          (static_cast<uint64_t>(byte_pos) * samples_per_frame) / sample_rate
                      );
        }
    } else {
        const uint32_t bytes_per_second = sample_rate * channels * (bits / 8U);
        elapsed = (bytes_per_second > 0) ? (byte_pos / bytes_per_second) : 0;
        total = (bytes_per_second > 0 && data_size > 0) ? (data_size / bytes_per_second) : 0;
    }
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

    const char *toggle_text = playing ? "暂停" : (paused ? "继续" : "播放");
    (void)context.gui().set_binding_value(PATH_TOGGLE_TEXT, "toggle_text", toggle_text);
}

void MusicApp::tick_wave(system::core::AppContext &context)
{
    if (paused_ui_) {
        return;
    }
    bool playing = false;
    {
        std::lock_guard<std::mutex> lock(state_.mutex);
        playing = state_.play_requested && !state_.paused;
    }

    const float seconds = static_cast<float>(now_ms() % 100000) / 1000.0f;
    // The idle line keeps a slow, shallow ripple so the card never looks frozen.
    const float level = playing ? (0.30f + 0.70f * static_cast<float>(volume_) / 100.0f) : 0.14f;
    const float speed = playing ? 6.0f : 1.4f;

    bool changed = false;
    const int32_t amplitude = static_cast<int32_t>(WAVE_AMPLITUDE_PX * level);
    for (int i = 0; i < WAVE_POINT_COUNT; i++) {
        const float phase = static_cast<float>(i) * 0.34f;
        const float wave =
            std::sin(seconds * speed + phase) * 0.55f +
            std::sin(seconds * speed * 1.7f + phase * 1.9f) * 0.30f +
            std::sin(seconds * speed * 0.6f - phase * 0.7f) * 0.15f;
        const int32_t y = WAVE_CENTER_Y + static_cast<int32_t>(wave * amplitude);
        if (y != wave_points_[i]) {
            wave_points_[i] = y;
            changed = true;
        }
    }
    if (!changed) {
        return;
    }
    const auto glow = make_wave_points(wave_points_, 3);
    const auto line = make_wave_points(wave_points_, 0);
    (void)context.gui().set_binding_value(PATH_WAVE_GLOW, "wave_glow", glow);
    (void)context.gui().set_binding_value(PATH_WAVE_LINE, "wave_line", line);
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
            .stack_size = 32 * 1024,
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

bool MusicApp::ensure_mp3_index(const std::string &path)
{
    if (mp3_index_.path == path && mp3_index_.total_frames > 0) {
        return true;
    }
    mp3_index_ = Mp3Index{};

    std::FILE *file = std::fopen(path.c_str(), "rb");
    if (file == nullptr) {
        return false;
    }
    struct stat st {};
    uint64_t file_size = 0;
    if (stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode)) {
        file_size = static_cast<uint64_t>(st.st_size);
    }

    // Skip an ID3v2 tag (and its optional footer) before the first frame.
    uint8_t tag[10] = {};
    uint64_t start = 0;
    if (std::fread(tag, 1, sizeof(tag), file) == sizeof(tag) && std::memcmp(tag, "ID3", 3) == 0) {
        start = 10 + read_synchsafe(tag + 6);
        if ((tag[5] & 0x10) != 0) {
            start += 10;
        }
    }
    if (file_size > 0 && start >= file_size) {
        std::fclose(file);
        return false;
    }

    constexpr size_t CHUNK = 65536;
    // Larger than the biggest possible Layer III frame (~1441 bytes).
    constexpr size_t GUARD = 2048;
    std::vector<uint8_t> buffer(CHUNK + GUARD);
    uint64_t base = start;
    size_t offset = 0;
    size_t length = 0;
    bool eof = false;

    if (std::fseek(file, static_cast<long>(start), SEEK_SET) != 0) {
        std::fclose(file);
        return false;
    }
    length = std::fread(buffer.data(), 1, CHUNK, file);
    eof = (length < CHUNK);

    while (offset + 4 <= length) {
        if (length - offset < GUARD && !eof) {
            // Slide the tail to the front and top up the window.
            const size_t remaining = length - offset;
            std::memmove(buffer.data(), buffer.data() + offset, remaining);
            base += offset;
            offset = 0;
            const size_t got = std::fread(buffer.data() + remaining, 1, CHUNK - remaining, file);
            length = remaining + got;
            if (got == 0) {
                eof = true;
            }
            if (length < 4) {
                break;
            }
            continue;
        }

        Mp3Header header;
        if (!parse_mp3_header(buffer.data() + offset, header)) {
            ++offset;
            continue;
        }
        if (mp3_index_.total_frames == 0) {
            mp3_index_.sample_rate = header.sample_rate;
            mp3_index_.channels = header.channels;
            mp3_index_.samples_per_frame = header.samples;
        } else if (header.samples != mp3_index_.samples_per_frame ||
                   header.sample_rate != mp3_index_.sample_rate) {
            // Mixed sample rates inside one file are out of scope.
            ++offset;
            continue;
        }
        if (offset + header.frame_bytes > length) {
            // Truncated final frame.
            break;
        }
        mp3_index_.offsets.push_back(static_cast<uint32_t>(base + offset));
        mp3_index_.lengths.push_back(static_cast<uint16_t>(header.frame_bytes));
        ++mp3_index_.total_frames;
        offset += header.frame_bytes;
    }
    std::fclose(file);

    mp3_index_.path = path;
    if (mp3_index_.total_frames == 0) {
        BROOKESIA_LOGW("Music: no MP3 frames found in %1%", path);
        return false;
    }
    BROOKESIA_LOGI(
        "Music: indexed %1% MP3 frame(s), %2% Hz, %3% ch", mp3_index_.total_frames,
        mp3_index_.sample_rate, mp3_index_.channels
    );
    return true;
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

        const bool is_mp3 = has_mp3_extension(path);
        std::FILE *file = nullptr;
        WavInfo info;

        if (is_mp3) {
            if (!ensure_mp3_index(path)) {
                BROOKESIA_LOGW("Music: cannot index MP3 file: %1%", path);
                std::lock_guard<std::mutex> lock(state_.mutex);
                if (state_.generation == generation) {
                    state_.error = true;
                    state_.error_text = "invalid mp3";
                    state_.play_requested = false;
                    state_.byte_pos = 0;
                    state_.data_size = 0;
                }
                continue;
            }
            if (start_pos >= mp3_index_.total_frames) {
                start_pos = 0;
            }
            file = std::fopen(path.c_str(), "rb");
            if (file == nullptr) {
                BROOKESIA_LOGW("Music: cannot open MP3 file: %1%", path);
                std::lock_guard<std::mutex> lock(state_.mutex);
                if (state_.generation == generation) {
                    state_.error = true;
                    state_.error_text = "invalid mp3";
                    state_.play_requested = false;
                }
                continue;
            }
        } else {
            file = std::fopen(path.c_str(), "rb");
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
        }

        // WAV positions are byte offsets, MP3 positions are frame indexes.
        const uint32_t total_units = is_mp3 ? mp3_index_.total_frames : info.data_size;
        const uint32_t sample_rate = is_mp3 ? mp3_index_.sample_rate : info.sample_rate;
        const uint16_t channels = is_mp3 ? mp3_index_.channels : info.channels;
        const uint16_t bits = is_mp3 ? 16 : info.bits;
        {
            std::lock_guard<std::mutex> lock(state_.mutex);
            if (state_.generation != generation) {
                std::fclose(file);
                continue;
            }
            state_.data_size = total_units;
            state_.sample_rate = sample_rate;
            state_.samples_per_frame = is_mp3 ? mp3_index_.samples_per_frame : 0;
            state_.channels = channels;
            state_.bits = bits;
            state_.byte_pos = start_pos;
        }

        hal::audio::CodecPlayerIface::Config config = {
            .bits = static_cast<uint8_t>(bits),
            .channels = static_cast<uint8_t>(channels),
            .sample_rate = sample_rate,
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

        // The minimp3 state is ~7 KiB (MDCT overlap + QMF history). Keep it on
        // the heap: the engine thread stack is only 10 KiB and a stack-allocated
        // decoder overflows it as soon as an MP3 starts playing.
        std::unique_ptr<mp3dec_t> decoder;
        std::vector<int16_t> pcm;
        if (is_mp3) {
            decoder = std::make_unique<mp3dec_t>();
            mp3dec_init(decoder.get());
            pcm.resize(MINIMP3_MAX_SAMPLES_PER_FRAME);
        } else {
            (void)std::fseek(file, static_cast<long>(info.data_offset + start_pos), SEEK_SET);
        }

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
            const uint32_t remaining = (pos < total_units) ? (total_units - pos) : 0;
            if (remaining == 0) {
                break;
            }

            if (is_mp3) {
                const uint32_t frame_offset = mp3_index_.offsets[pos];
                const uint16_t frame_length = mp3_index_.lengths[pos];
                if (std::ftell(file) != static_cast<long>(frame_offset)) {
                    if (std::fseek(file, static_cast<long>(frame_offset), SEEK_SET) != 0) {
                        break;
                    }
                }
                const size_t read = std::fread(buffer.data(), 1, frame_length, file);
                if (read < 4) {
                    break;
                }
                mp3dec_frame_info_t frame_info{};
                const int decoded = mp3dec_decode_frame(
                                        decoder.get(), buffer.data(), static_cast<int>(read), pcm.data(), &frame_info
                                    );
                if (decoded > 0 && frame_info.channels > 0) {
                    player_->write_data(
                        reinterpret_cast<const uint8_t *>(pcm.data()),
                        static_cast<size_t>(decoded) * static_cast<size_t>(frame_info.channels) *
                        sizeof(int16_t)
                    );
                }
                ++pos;
            } else {
                const size_t want = std::min<size_t>(remaining, buffer.size());
                const size_t read = std::fread(buffer.data(), 1, want, file);
                if (read == 0) {
                    break;
                }
                player_->write_data(buffer.data(), read);
                pos += static_cast<uint32_t>(read);
            }
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
