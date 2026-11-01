/*
 * SPDX-License-Identifier: Apache-2.0
 */
#include "brookesia/app_video/video_app.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include <dirent.h>
#include <sys/stat.h>

#include <esp_heap_caps.h>
#include <esp_log.h>

#include "brookesia/lib_utils/thread_config.hpp"

#include "private/utils.hpp"

namespace esp_brookesia::app::video {

using namespace esp_brookesia::system;

namespace {

constexpr const char *APP_ID = "brookesia.waveshare.video";
constexpr const char *APP_NAME = "Video";
constexpr const char *APP_NAME_ZH_CN = "视频";
constexpr const char *APP_ICON_ID = "launcher_icon";
constexpr const char *GUI_ROOT = "res/root.json";
constexpr const char *VIDEO_FLOW_ID = "video_flow";
constexpr const char *UI_TIMER_NAME = "video_ui";
constexpr int32_t UI_PERIOD_MS = 250;

// Absolute paths inside the video screen document.
constexpr const char *PATH_FRAME = "/video/page/frame";
constexpr const char *PATH_TITLE = "/video/page/hint/title";
constexpr const char *PATH_STATUS = "/video/page/hint/status";
constexpr const char *PATH_TIME = "/video/page/bar/time";
constexpr const char *PATH_PROGRESS = "/video/page/bar/progress";
constexpr const char *PATH_TOGGLE_TEXT = "/video/page/bar/toggle/text";

constexpr const char *ACTION_PREV = "video.prev";
constexpr const char *ACTION_TOGGLE = "video.toggle";
constexpr const char *ACTION_NEXT = "video.next";
constexpr const char *ACTION_SEEK = "video.seek";
constexpr const char *ACTION_REFRESH = "video.refresh";

constexpr const char *MOVIE_DIRS[] = {
    "/littlefs/videos",
    "/sdcard/videos",
    "/sdcard",
};

// Nominal playback rate. MJPEG files carry no timing information, so playback
// is paced by this constant (12 fps keeps a 640x360 movie smooth on the panel).
constexpr uint32_t PLAYBACK_FPS = 12;
constexpr int32_t PLAYBACK_FRAME_MS = 1000 / PLAYBACK_FPS;
constexpr size_t MAX_MOVIE_BYTES = 24U * 1024U * 1024U;
constexpr size_t MAX_FRAME_BYTES = 2U * 1024U * 1024U;   // one 1024x1024 RGB565 picture
constexpr size_t MAX_NAME_LENGTH = 34;

bool has_movie_extension(const std::string &name)
{
    const auto ends_with = [&name](const char *suffix) {
        const auto suffix_length = std::strlen(suffix);
        if (name.size() < suffix_length) {
            return false;
        }
        return std::equal(
                   suffix, suffix + suffix_length, name.end() - static_cast<std::ptrdiff_t>(suffix_length),
                   [](char a, char b) {
            return std::tolower(static_cast<unsigned char>(a)) ==
                   std::tolower(static_cast<unsigned char>(b));
        }
               );
    };
    return ends_with(".mjpeg") || ends_with(".mjpg") || ends_with(".mjp");
}

std::string file_name_of(const std::string &path)
{
    const auto slash = path.find_last_of('/');
    std::string name = (slash == std::string::npos) ? path : path.substr(slash + 1);
    if (name.size() > MAX_NAME_LENGTH) {
        // Keep the extension visible, drop the middle of the base name.
        const auto dot = name.find_last_of('.');
        const std::string extension = (dot == std::string::npos) ? std::string() : name.substr(dot);
        name.resize(MAX_NAME_LENGTH - extension.size() - 3);
        while (!name.empty() && (static_cast<unsigned char>(name.back()) & 0xC0) == 0x80) {
            name.pop_back();
        }
        name += "..." + extension;
    }
    return name;
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

/**
 * @brief Split a concatenated JPEG stream into individually decodable pictures.
 *
 * The scanner walks the marker segments of every picture instead of looking for
 * the next FFD9 pair, so EXIF thumbnails (which contain their own EOI) cannot
 * terminate a frame early.
 */
void scan_jpeg_frames(const uint8_t *data, size_t size, std::vector<std::pair<uint32_t, uint32_t>> &frames)
{
    size_t pos = 0;
    while (pos + 3 < size) {
        if (data[pos] != 0xFF || data[pos + 1] != 0xD8) {
            pos++;
            continue;
        }
        const size_t start = pos;
        size_t cursor = pos + 2;
        bool complete = false;
        while (cursor + 1 < size) {
            if (data[cursor] != 0xFF) {
                cursor++;
                continue;
            }
            const uint8_t marker = data[cursor + 1];
            if (marker == 0xFF) {
                cursor++;
                continue;
            }
            if (marker == 0xD8) {
                cursor += 2;
                continue;
            }
            if (marker == 0xD9) {
                cursor += 2;
                complete = true;
                break;
            }
            if (marker == 0xDA) {
                // Start of scan: skip the segment header, then walk the entropy
                // coded data until a real marker (not a stuffed 0xFF00 or a RSTn).
                cursor += 2;
                if (cursor + 2 > size) {
                    break;
                }
                const uint16_t length = static_cast<uint16_t>((data[cursor] << 8) | data[cursor + 1]);
                if (length < 2) {
                    break;
                }
                cursor += length;
                while (cursor + 1 < size) {
                    if (data[cursor] == 0xFF) {
                        const uint8_t next = data[cursor + 1];
                        if (next == 0x00 || next == 0xFF || (next >= 0xD0 && next <= 0xD7)) {
                            cursor += (next == 0xFF) ? 1 : 2;
                            continue;
                        }
                        break;
                    }
                    cursor++;
                }
                continue;
            }
            if (marker == 0x01) {
                cursor += 2;
                continue;
            }
            if (cursor + 4 > size) {
                break;
            }
            const uint16_t length = static_cast<uint16_t>((data[cursor + 2] << 8) | data[cursor + 3]);
            if (length < 2) {
                break;
            }
            cursor += 2 + length;
        }
        if (!complete) {
            break;
        }
        const auto frame_size = static_cast<uint32_t>(cursor - start);
        if (frame_size > 0) {
            frames.emplace_back(static_cast<uint32_t>(start), frame_size);
        }
        pos = cursor;
    }
}

uint8_t *alloc_psram(size_t bytes)
{
    // JPEG codec, PPA and 2D-DMA all operate on cache lines; 256 bytes covers
    // every alignment the ESP32-P4 cache line size can report.
    return static_cast<uint8_t *>(
               heap_caps_aligned_alloc(256, bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
           );
}

} // namespace

VideoApp::VideoApp() = default;

VideoApp::~VideoApp()
{
    stop_worker();
    release_source();
    unregister_frame_images();
    if (input_buffer_ != nullptr) {
        heap_caps_free(input_buffer_);
        input_buffer_ = nullptr;
        input_buffer_capacity_ = 0;
    }
    if (decoder_ != nullptr) {
        jpeg_del_decoder_engine(decoder_);
        decoder_ = nullptr;
    }
}

system::core::AppManifest VideoApp::get_manifest() const
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

system::core::AppGuiDescriptor VideoApp::get_gui_descriptor() const
{
    return {
        .root_kind = system::core::GuiRootKind::File,
        .root = GUI_ROOT,
        .resources = {},
        .screen_flows = {
            system::core::GuiScreenFlowEntry{
                .screen_flow = VIDEO_FLOW_ID,
                .layer = system::core::GuiAppLayer::AppDefault,
            },
        },
    };
}

std::expected<void, std::string> VideoApp::on_install(system::core::AppContext &)
{
    BROOKESIA_LOGI("Video installed");
    return {};
}

void VideoApp::on_uninstall(system::core::AppContext &)
{
}

std::expected<void, std::string> VideoApp::on_start(system::core::AppContext &context)
{
    BROOKESIA_LOG_TRACE_GUARD_WITH_THIS();

    context_ = &context;
    paused_ui_ = false;
    playing_ = false;

    if (decoder_ == nullptr) {
        jpeg_decode_engine_cfg_t engine_config = {
            .intr_priority = 0,
            .timeout_ms = 200,
            .flags = {
                .allow_pd = 0,
            },
        };
        if (jpeg_new_decoder_engine(&engine_config, &decoder_) != ESP_OK || decoder_ == nullptr) {
            decoder_ = nullptr;
            BROOKESIA_LOGE("Video: failed to create the hardware JPEG decoder");
            set_status("硬件 JPEG 解码器初始化失败");
            return {};
        }
    }

    scan_movies();
    subscribe_actions(context);

    auto timer = context.timer().start_periodic(UI_TIMER_NAME, UI_PERIOD_MS);
    if (!timer) {
        BROOKESIA_LOGW("Failed to start video UI timer: %1%", timer.error());
    }

    if (movies_.empty()) {
        set_status("把 .mjpeg / .mjpg 文件放到 /littlefs/videos 后点刷新");
        refresh_ui(context);
        return {};
    }

    movie_index_ = 0;
    if (!load_source(movie_index_)) {
        refresh_ui(context);
        return {};
    }
    request_play();
    refresh_ui(context);
    return {};
}

std::expected<void, std::string> VideoApp::on_stop(system::core::AppContext &)
{
    stop_worker();
    release_source();
    unregister_frame_images();
    if (input_buffer_ != nullptr) {
        heap_caps_free(input_buffer_);
        input_buffer_ = nullptr;
        input_buffer_capacity_ = 0;
    }
    if (decoder_ != nullptr) {
        jpeg_del_decoder_engine(decoder_);
        decoder_ = nullptr;
    }
    action_connections_.clear();
    context_ = nullptr;
    return {};
}

std::expected<void, std::string> VideoApp::on_pause(system::core::AppContext &)
{
    // Freeze the movie while it sits in the background; decoding a 640x360
    // stream behind another app would only steal CPU and PSRAM bandwidth.
    paused_ui_ = true;
    {
        std::lock_guard<std::mutex> lock(state_.mutex);
        state_.paused = true;
    }
    notify_worker();
    return {};
}

std::expected<void, std::string> VideoApp::on_resume(system::core::AppContext &context)
{
    paused_ui_ = false;
    if (playing_ && !source_.frames.empty()) {
        std::lock_guard<std::mutex> lock(state_.mutex);
        state_.paused = false;
    }
    notify_worker();
    refresh_ui(context);
    return {};
}

std::expected<void, std::string> VideoApp::on_timer(
    system::core::AppContext &context,
    system::core::TimerId timer_id,
    std::string_view name
)
{
    (void)timer_id;
    if (name != UI_TIMER_NAME) {
        return {};
    }

    bool finished = false;
    bool failed = false;
    std::string error_text;
    {
        std::lock_guard<std::mutex> lock(state_.mutex);
        if (state_.finished) {
            state_.finished = false;
            finished = true;
        }
        if (state_.error) {
            state_.error = false;
            failed = true;
            error_text = state_.error_text;
        }
    }

    if (failed) {
        playing_ = false;
        set_status(error_text.empty() ? "无法解码这个视频" : error_text);
    } else if (finished) {
        // Auto advance to the next movie.
        if (!movies_.empty()) {
            movie_index_ = (movie_index_ + 1) % movies_.size();
            if (load_source(movie_index_)) {
                request_play();
            } else {
                playing_ = false;
            }
        }
    }

    if (!paused_ui_) {
        refresh_ui(context);
    }
    return {};
}

void VideoApp::scan_movies()
{
    movies_.clear();
    for (const auto *dir_path : MOVIE_DIRS) {
        auto *dir = opendir(dir_path);
        if (dir == nullptr) {
            continue;
        }
        while (auto *entry = readdir(dir)) {
            const std::string name = entry->d_name;
            if (name == "." || name == ".." || !has_movie_extension(name)) {
                continue;
            }
            const std::string full_path = std::string(dir_path) + "/" + name;
            struct stat st {};
            if (stat(full_path.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) {
                continue;
            }
            movies_.push_back(full_path);
        }
        closedir(dir);
    }
    std::sort(movies_.begin(), movies_.end());
    movies_.erase(std::unique(movies_.begin(), movies_.end()), movies_.end());
    BROOKESIA_LOGI("Video found %1% movie(s)", movies_.size());
}

bool VideoApp::load_source(size_t index)
{
    stop_worker();
    release_source();

    if (movies_.empty()) {
        set_status("没有找到视频文件");
        return false;
    }
    index %= movies_.size();
    const std::string &path = movies_[index];
    movie_index_ = index;

    struct stat st {};
    if (stat(path.c_str(), &st) != 0 || st.st_size <= 0) {
        set_status("无法打开：" + file_name_of(path));
        return false;
    }
    if (static_cast<size_t>(st.st_size) > MAX_MOVIE_BYTES) {
        set_status("文件太大（上限 24MB）：" + file_name_of(path));
        return false;
    }

    auto *file = std::fopen(path.c_str(), "rb");
    if (file == nullptr) {
        set_status("无法打开：" + file_name_of(path));
        return false;
    }
    const size_t file_size = static_cast<size_t>(st.st_size);
    uint8_t *data = alloc_psram(file_size + 64);
    if (data == nullptr) {
        std::fclose(file);
        set_status("内存不足，无法载入视频");
        return false;
    }
    const size_t read = std::fread(data, 1, file_size, file);
    std::fclose(file);
    if (read != file_size) {
        heap_caps_free(data);
        set_status("读取失败：" + file_name_of(path));
        return false;
    }

    std::vector<std::pair<uint32_t, uint32_t>> frames;
    scan_jpeg_frames(data, file_size, frames);
    if (frames.empty()) {
        heap_caps_free(data);
        set_status("不是有效的 MJPEG 文件：" + file_name_of(path));
        return false;
    }

    jpeg_decode_picture_info_t info = {};
    if (jpeg_decoder_get_info(data + frames[0].first, frames[0].second, &info) != ESP_OK ||
            info.width == 0 || info.height == 0) {
        heap_caps_free(data);
        set_status("无法解析第一帧：" + file_name_of(path));
        return false;
    }

    source_.data = data;
    source_.size = file_size;
    source_.frames.reserve(frames.size());
    for (const auto &frame : frames) {
        source_.frames.push_back(FrameSpan{ .offset = frame.first, .length = frame.second });
    }
    source_.width = static_cast<int32_t>(info.width);
    source_.height = static_cast<int32_t>(info.height);
    source_.padded_width = static_cast<int32_t>((info.width + 15U) & ~15U);
    source_.padded_height = static_cast<int32_t>((info.height + 15U) & ~15U);
    source_.path = path;

    size_t largest_frame = 0;
    for (const auto &frame : source_.frames) {
        largest_frame = std::max<size_t>(largest_frame, frame.length);
    }
    if (largest_frame + 64U > input_buffer_capacity_) {
        if (input_buffer_ != nullptr) {
            heap_caps_free(input_buffer_);
            input_buffer_ = nullptr;
            input_buffer_capacity_ = 0;
        }
        input_buffer_ = alloc_psram(largest_frame + 64U);
        if (input_buffer_ == nullptr) {
            release_source();
            set_status("内存不足，无法解码这个视频");
            return false;
        }
        input_buffer_capacity_ = largest_frame + 64U;
    }

    if (!ensure_decode_buffers(source_)) {
        release_source();
        set_status("显存不足，无法解码这个视频");
        return false;
    }
    register_frame_images(source_);
    apply_frame_geometry();

    BROOKESIA_LOGI(
        "Video loaded: %1% (%2%x%3%, %4% frame(s), %5% bytes)", file_name_of(path), source_.width,
        source_.height, source_.frames.size(), source_.size
    );

    {
        std::lock_guard<std::mutex> lock(state_.mutex);
        state_.cursor = 0;
        state_.current_frame = 0;
        state_.seek_request = UINT32_MAX;
        state_.finished = false;
        state_.error = false;
        state_.paused = true;
    }
    shown_width_ = 0;
    shown_height_ = 0;
    first_frame_logged_ = false;
    last_programmed_permille_ = -1;
    return true;
}

void VideoApp::release_source()
{
    if (source_.data != nullptr) {
        heap_caps_free(source_.data);
        source_.data = nullptr;
    }
    source_.size = 0;
    source_.frames.clear();
    source_.width = 0;
    source_.height = 0;
    source_.path.clear();
    decode_error_logged_ = false;
}

bool VideoApp::ensure_decode_buffers(const Source &source)
{
    const size_t raw_size = static_cast<size_t>(source.padded_width) *
                            static_cast<size_t>(source.padded_height) * 2U;
    // The JPEG driver requires the output buffer *size* to be a multiple of the
    // cache line (and the address to be cache aligned), so round the allocation
    // up generously instead of matching the picture size exactly.
    const size_t required = (raw_size + 4095U) & ~static_cast<size_t>(4095U);
    if (required > MAX_FRAME_BYTES) {
        BROOKESIA_LOGE("Video: frame buffer too large (%1% bytes)", required);
        return false;
    }
    if (required <= frame_buffer_capacity_) {
        return true;
    }
    for (auto &buffer : frame_buffer_) {
        if (buffer != nullptr) {
            heap_caps_free(buffer);
            buffer = nullptr;
        }
    }
    frame_buffer_capacity_ = 0;
    for (auto &buffer : frame_buffer_) {
        buffer = alloc_psram(required);
        if (buffer == nullptr) {
            for (auto &other : frame_buffer_) {
                if (other != nullptr) {
                    heap_caps_free(other);
                    other = nullptr;
                }
            }
            return false;
        }
        std::memset(buffer, 0, required);
    }
    frame_buffer_capacity_ = required;
    return true;
}

void VideoApp::register_frame_images(const Source &source)
{
    if (context_ == nullptr) {
        return;
    }
    for (int32_t i = 0; i < 2; i++) {
        if (frame_buffer_[i] == nullptr) {
            continue;
        }
        frame_descriptor_[i] = {};
        frame_descriptor_[i].header.magic = LV_IMAGE_HEADER_MAGIC;
        frame_descriptor_[i].header.cf = LV_COLOR_FORMAT_RGB565;
        frame_descriptor_[i].header.flags = 0;
        frame_descriptor_[i].header.w = static_cast<uint32_t>(source.width);
        frame_descriptor_[i].header.h = static_cast<uint32_t>(source.height);
        frame_descriptor_[i].header.stride = static_cast<uint32_t>(source.padded_width) * 2U;
        frame_descriptor_[i].data_size =
            static_cast<uint32_t>(source.padded_width) * static_cast<uint32_t>(source.padded_height) * 2U;
        frame_descriptor_[i].data = frame_buffer_[i];

        gui::RuntimeImageResource resource = {
            .id = "video.frame." + std::to_string(i),
            .primary_src = "",
            .native_src = reinterpret_cast<uintptr_t>(&frame_descriptor_[i]),
            .width = source.width,
            .height = source.height,
            .preload = false,
        };
        auto result = context_->gui().register_image(resource);
        if (!result) {
            BROOKESIA_LOGW("Failed to register video frame image %1%: %2%", i, result.error());
        }
    }
    images_registered_ = true;
}

void VideoApp::unregister_frame_images()
{
    if (context_ == nullptr || !images_registered_) {
        images_registered_ = false;
        return;
    }
    for (int32_t i = 0; i < 2; i++) {
        (void)context_->gui().unregister_image("video.frame." + std::to_string(i));
    }
    images_registered_ = false;
}

void VideoApp::apply_frame_geometry()
{
    if (context_ == nullptr || source_.width <= 0 || source_.height <= 0) {
        return;
    }
    if (!frame_area_valid_) {
        if (auto frame = context_->gui().get_view_frame(PATH_FRAME)) {
            if (frame->width > 0 && frame->height > 0) {
                frame_area_width_ = frame->width;
                frame_area_height_ = frame->height;
                frame_area_valid_ = true;
            }
        }
    }
    const int32_t area_width = frame_area_width_;
    const int32_t area_height = frame_area_height_;

    // The video area is bigger than most movies: showing them at their native
    // size turns the per-pixel LVGL software transform into a plain blit, which
    // is several times faster (and avoids upscaling artifacts).
    int32_t width = source_.width;
    int32_t height = source_.height;
    if (width > area_width || height > area_height) {
        const int64_t scale_width = (static_cast<int64_t>(area_width) << 16) / width;
        const int64_t scale_height = (static_cast<int64_t>(area_height) << 16) / height;
        const int64_t scale = std::min(scale_width, scale_height);
        width = std::max<int32_t>(16, static_cast<int32_t>((static_cast<int64_t>(width) * scale) >> 16));
        height = std::max<int32_t>(16, static_cast<int32_t>((static_cast<int64_t>(height) * scale) >> 16));
    }
    const int32_t x = std::max<int32_t>((area_width - width) / 2, 0);
    const int32_t y = std::max<int32_t>((area_height - height) / 2, 0);

    BROOKESIA_LOGI(
        "Video view: %1%x%2% inside %3%x%4% at (%5%,%6%)", width, height, area_width, area_height, x, y
    );
    (void)context_->gui().set_binding_values({
        { PATH_FRAME, "frame_x", std::to_string(x) },
        { PATH_FRAME, "frame_y", std::to_string(y) },
        { PATH_FRAME, "frame_width", std::to_string(width) },
        { PATH_FRAME, "frame_height", std::to_string(height) },
    });
}

void VideoApp::subscribe_actions(system::core::AppContext &context)
{
    std::vector<system::core::GuiActionHandlerSubscription> subscriptions;
    subscriptions.push_back({
        .action = ACTION_PREV,
        .handler = [this](const gui::Event &) {
            if (context_ != nullptr) {
                step_movie(-1);
            }
        },
    });
    subscriptions.push_back({
        .action = ACTION_TOGGLE,
        .handler = [this](const gui::Event &) {
            if (context_ != nullptr) {
                toggle_playback();
            }
        },
    });
    subscriptions.push_back({
        .action = ACTION_NEXT,
        .handler = [this](const gui::Event &) {
            if (context_ != nullptr) {
                step_movie(1);
            }
        },
    });
    subscriptions.push_back({
        .action = ACTION_SEEK,
        .handler = [this](const gui::Event &event) {
            auto value = event.get_int64("value");
            if (value.has_value()) {
                seek_to_permille(static_cast<int32_t>(*value));
            }
        },
    });
    subscriptions.push_back({
        .action = ACTION_REFRESH,
        .handler = [this](const gui::Event &) {
            if (context_ == nullptr) {
                return;
            }
            scan_movies();
            playing_ = false;
            stop_worker();
            release_source();
            if (movies_.empty()) {
                set_status("没有找到视频文件");
                refresh_ui(*context_);
                return;
            }
            movie_index_ = 0;
            if (load_source(movie_index_)) {
                request_play();
            }
            refresh_ui(*context_);
        },
    });

    action_connections_ = context.gui().subscribe_actions(subscriptions);
}

void VideoApp::start_worker()
{
    if (worker_started_) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(state_.mutex);
        state_.quit = false;
    }
    {
        BROOKESIA_THREAD_CONFIG_GUARD({
            .stack_size = 8192,
            .stack_in_ext = true,
        });
        worker_ = boost::thread([this]() {
            worker_loop();
        });
    }
    worker_started_ = true;
}

void VideoApp::stop_worker()
{
    if (!worker_started_) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(state_.mutex);
        state_.quit = true;
        state_.paused = true;
    }
    notify_worker();
    if (worker_.joinable()) {
        worker_.join();
    }
    worker_started_ = false;
    {
        std::lock_guard<std::mutex> lock(state_.mutex);
        state_.quit = false;
    }
}

void VideoApp::notify_worker()
{
    state_.cv.notify_all();
}

void VideoApp::request_play()
{
    if (source_.frames.empty()) {
        return;
    }
    start_worker();
    {
        std::lock_guard<std::mutex> lock(state_.mutex);
        state_.paused = false;
        state_.error = false;
    }
    playing_ = true;
    set_status("");
    notify_worker();
}

void VideoApp::toggle_playback()
{
    if (source_.frames.empty() || context_ == nullptr) {
        set_status("没有可播放的视频");
        return;
    }
    if (!worker_started_) {
        request_play();
        refresh_ui(*context_);
        return;
    }
    bool now_playing = false;
    {
        std::lock_guard<std::mutex> lock(state_.mutex);
        state_.paused = !state_.paused;
        now_playing = !state_.paused;
    }
    playing_ = now_playing;
    notify_worker();
    refresh_ui(*context_);
}

void VideoApp::step_movie(int direction)
{
    if (movies_.empty() || context_ == nullptr) {
        set_status("没有找到视频文件");
        return;
    }
    const int count = static_cast<int>(movies_.size());
    int next = (static_cast<int>(movie_index_) + direction) % count;
    if (next < 0) {
        next += count;
    }
    if (load_source(static_cast<size_t>(next))) {
        request_play();
    }
    refresh_ui(*context_);
}

void VideoApp::seek_to_permille(int32_t permille)
{
    if (source_.frames.empty()) {
        return;
    }
    permille = std::clamp<int32_t>(permille, 0, 1000);
    if (permille == last_programmed_permille_) {
        // Echo of the position painted by the UI timer, not a user request:
        // treating it as a seek would rewind every few frames.
        return;
    }
    const auto total = static_cast<uint32_t>(source_.frames.size());
    uint32_t frame = static_cast<uint32_t>((static_cast<uint64_t>(permille) * total) / 1000U);
    if (frame >= total) {
        frame = total - 1;
    }
    last_programmed_permille_ = permille;
    {
        std::lock_guard<std::mutex> lock(state_.mutex);
        state_.seek_request = frame;
        state_.finished = false;
    }
    notify_worker();
}

bool VideoApp::decode_frame(uint32_t frame_index, int32_t buffer_index)
{
    if (source_.data == nullptr || frame_index >= source_.frames.size() ||
            frame_buffer_[buffer_index] == nullptr) {
        return false;
    }
    const auto &span = source_.frames[frame_index];
    // The hardware decoder needs a cache-line aligned bit stream, while the
    // frames inside a movie start at arbitrary offsets: stage the picture in an
    // aligned scratch buffer first (a few tens of KiB per frame).
    if (span.length > input_buffer_capacity_) {
        return false;
    }
    std::memcpy(input_buffer_, source_.data + span.offset, span.length);

    jpeg_decode_cfg_t decode_config = {
        .output_format = JPEG_DECODE_OUT_FORMAT_RGB565,
        .rgb_order = JPEG_DEC_RGB_ELEMENT_ORDER_BGR,
        .conv_std = JPEG_YUV_RGB_CONV_STD_BT601,
    };
    uint32_t written = 0;
    auto err = jpeg_decoder_process(
                   decoder_, &decode_config, input_buffer_, span.length,
                   frame_buffer_[buffer_index], static_cast<uint32_t>(frame_buffer_capacity_), &written
               );
    if (err != ESP_OK) {
        if (!decode_error_logged_) {
            decode_error_logged_ = true;
            BROOKESIA_LOGE(
                "Video: JPEG decode failed: %1% (frame %2%, %3% bytes, out capacity %4%)",
                esp_err_to_name(err), frame_index, span.length, frame_buffer_capacity_
            );
        }
        return false;
    }
    if (!first_frame_logged_) {
        first_frame_logged_ = true;
        const auto *pixels = reinterpret_cast<const uint16_t *>(frame_buffer_[buffer_index]);
        char pixel_text[16] = {};
        std::snprintf(pixel_text, sizeof(pixel_text), "0x%04X", static_cast<unsigned>(pixels[0]));
        BROOKESIA_LOGI(
            "Video first frame decoded: %1%x%2% (padded %3%x%4%), %5% bytes, first pixel %6%",
            source_.width, source_.height, source_.padded_width, source_.padded_height, written, pixel_text
        );
    }
    return true;
}

void VideoApp::worker_loop()
{
    auto next_deadline = std::chrono::steady_clock::now();
    uint32_t consecutive_errors = 0;

    while (true) {
        uint32_t frame_index = 0;
        int32_t buffer_index = 0;
        {
            std::unique_lock<std::mutex> lock(state_.mutex);
            state_.cv.wait(lock, [this]() {
                return state_.quit || state_.seek_request != UINT32_MAX ||
                       (!state_.paused && !source_.frames.empty());
            });
            if (state_.quit) {
                break;
            }
            if (state_.seek_request != UINT32_MAX) {
                state_.cursor = state_.seek_request;
                state_.seek_request = UINT32_MAX;
                next_deadline = std::chrono::steady_clock::now();
            } else if (state_.paused) {
                // A seek may wake the worker while paused (to show the target
                // frame); otherwise it goes straight back to waiting.
                continue;
            }
            if (state_.cursor >= source_.frames.size()) {
                state_.paused = true;
                state_.finished = true;
                continue;
            }
            frame_index = state_.cursor;
            buffer_index = front_buffer_;
        }

        const bool decoded = decode_frame(frame_index, buffer_index);
        if (!decoded) {
            consecutive_errors++;
            BROOKESIA_LOGW("Video: failed to decode frame %1%", frame_index);
            if (consecutive_errors >= 8) {
                std::lock_guard<std::mutex> lock(state_.mutex);
                state_.error = true;
                state_.error_text = "视频帧解码失败";
                state_.paused = true;
                consecutive_errors = 0;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(40));
            continue;
        }
        consecutive_errors = 0;

        {
            std::lock_guard<std::mutex> lock(state_.mutex);
            state_.current_frame = frame_index;
            state_.cursor = frame_index + 1;
            front_buffer_ = 1 - buffer_index;
        }

        if (context_ != nullptr) {
            (void)context_->gui().set_view_src(PATH_FRAME, "video.frame." + std::to_string(buffer_index));
        }

        next_deadline += std::chrono::milliseconds(PLAYBACK_FRAME_MS);
        const auto now = std::chrono::steady_clock::now();
        if (next_deadline < now - std::chrono::milliseconds(1000)) {
            // Running behind by more than a second (slow storage or heavy UI):
            // re-anchor the clock instead of trying to catch up.
            next_deadline = now;
        }
        if (next_deadline > now) {
            std::this_thread::sleep_for(
                std::chrono::duration_cast<std::chrono::milliseconds>(next_deadline - now)
            );
        }
    }
}

void VideoApp::refresh_ui(system::core::AppContext &context)
{
    if (movies_.empty()) {
        (void)context.gui().set_binding_value(PATH_TITLE, "title_text", "视频");
        (void)context.gui().set_binding_value(PATH_TOGGLE_TEXT, "toggle_text", "播放");
        (void)context.gui().set_binding_value(PATH_TIME, "time_text", "00:00 / 00:00");
        last_programmed_permille_ = 0;
        (void)context.gui().set_value(PATH_PROGRESS, 0);
        return;
    }

    uint32_t current_frame = 0;
    uint32_t total_frames = 0;
    bool paused = true;
    {
        std::lock_guard<std::mutex> lock(state_.mutex);
        current_frame = state_.current_frame;
        total_frames = static_cast<uint32_t>(source_.frames.size());
        paused = state_.paused || state_.quit;
    }

    (void)context.gui().set_binding_value(
        PATH_TITLE, "title_text",
        file_name_of(source_.path) + "  (" + std::to_string(movie_index_ + 1) + "/" +
        std::to_string(movies_.size()) + ")"
    );

    const uint32_t total_seconds = (total_frames + PLAYBACK_FPS - 1) / PLAYBACK_FPS;
    const uint32_t elapsed_seconds = current_frame / PLAYBACK_FPS;
    (void)context.gui().set_binding_value(
        PATH_TIME, "time_text",
        format_time(elapsed_seconds) + " / " + format_time(total_seconds) +
        "  " + std::to_string(source_.width) + "x" + std::to_string(source_.height)
    );

    int32_t permille = 0;
    if (total_frames > 1) {
        permille = static_cast<int32_t>((static_cast<uint64_t>(current_frame) * 1000U) / (total_frames - 1));
    }
    permille = std::clamp<int32_t>(permille, 0, 1000);
    last_programmed_permille_ = permille;
    (void)context.gui().set_value(PATH_PROGRESS, permille);

    const char *toggle_text = paused ? "继续" : "暂停";
    (void)context.gui().set_binding_value(PATH_TOGGLE_TEXT, "toggle_text", toggle_text);
}

void VideoApp::set_status(const std::string &text)
{
    if (context_ == nullptr) {
        return;
    }
    (void)context_->gui().set_binding_value(PATH_STATUS, "status_text", text);
}

} // namespace esp_brookesia::app::video

BROOKESIA_SYSTEM_CORE_APP_PROVIDER_REGISTER_WITH_SYMBOL(
    esp_brookesia::app::video::VideoAppProvider,
    "brookesia.waveshare.video",
    app_video_provider_symbol
);
