/*
 * SPDX-License-Identifier: Apache-2.0
 */
#include "brookesia/app_gallery/gallery_app.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include <dirent.h>
#include <sys/stat.h>

#include "private/utils.hpp"

namespace esp_brookesia::app::gallery {

using namespace esp_brookesia::system;

namespace {

constexpr const char *APP_ID = "brookesia.waveshare.gallery";
constexpr const char *APP_NAME = "Gallery";
constexpr const char *APP_NAME_ZH_CN = "图片";
constexpr const char *APP_ICON_ID = "launcher_icon";
constexpr const char *GUI_ROOT = "res/root.json";
constexpr const char *GALLERY_FLOW_ID = "gallery_flow";

// Absolute paths inside the gallery screen document.
constexpr const char *PATH_PHOTO = "/gallery/page/preview/photo";
constexpr const char *PATH_FILE_NAME = "/gallery/page/bar/info/file_name";
constexpr const char *PATH_PHOTO_INDEX = "/gallery/page/bar/info/photo_index";
constexpr const char *PATH_PHOTO_INFO = "/gallery/page/bar/info/photo_info";
constexpr const char *PATH_SLIDE_TEXT = "/gallery/page/bar/buttons/slideshow/text";
constexpr const char *PATH_STATUS = "/gallery/page/bar/status";

constexpr const char *ACTION_PREV = "gallery.prev";
constexpr const char *ACTION_NEXT = "gallery.next";
constexpr const char *ACTION_SET_WALLPAPER = "gallery.set_wallpaper";
constexpr const char *ACTION_REFRESH = "gallery.refresh";
constexpr const char *ACTION_SLIDESHOW = "gallery.slideshow";

// One stable resource id: re-registering the same id releases the memory of the
// previous picture, so browsing cannot leak decoded frames.
constexpr const char *PHOTO_IMAGE_ID = "gallery.photo";

constexpr const char *SLIDESHOW_TIMER_NAME = "gallery_slideshow";
constexpr int32_t SLIDESHOW_INTERVAL_MS = 4000;

// The Shell reads this file when it shows the launcher again.
constexpr const char *WALLPAPER_CONFIG_DIR = "/littlefs/system/super";
constexpr const char *WALLPAPER_CONFIG_PATH = "/littlefs/system/super/wallpaper.txt";

// Directories scanned for pictures, in order. Missing directories are skipped.
constexpr const char *IMAGE_DIRS[] = {
    "/littlefs/pictures",
    "/littlefs/wallpapers",
    "/sdcard/pictures",
    "/sdcard",
};

constexpr size_t MAX_NAME_LENGTH = 28;

bool has_image_extension(const std::string &name)
{
    const auto lower_ends_with = [&name](const char *suffix) {
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
    return lower_ends_with(".png") || lower_ends_with(".jpg") || lower_ends_with(".jpeg");
}

std::string file_name_of(const std::string &path)
{
    const auto slash = path.find_last_of('/');
    std::string name = (slash == std::string::npos) ? path : path.substr(slash + 1);
    if (name.size() > MAX_NAME_LENGTH) {
        name = name.substr(0, MAX_NAME_LENGTH - 3) + "...";
    }
    return name;
}

bool ensure_directory(const char *path)
{
    struct stat st {};
    if (stat(path, &st) == 0) {
        return S_ISDIR(st.st_mode);
    }
    return mkdir(path, 0755) == 0;
}

bool write_text_file(const char *path, const std::string &text)
{
    auto *file = std::fopen(path, "wb");
    if (file == nullptr) {
        return false;
    }
    const auto written = std::fwrite(text.data(), 1, text.size(), file);
    std::fclose(file);
    return written == text.size();
}

uint16_t read_be16(const uint8_t *data)
{
    return static_cast<uint16_t>((static_cast<uint16_t>(data[0]) << 8) | data[1]);
}

uint32_t read_be32(const uint8_t *data)
{
    return (static_cast<uint32_t>(data[0]) << 24) | (static_cast<uint32_t>(data[1]) << 16) |
           (static_cast<uint32_t>(data[2]) << 8) | static_cast<uint32_t>(data[3]);
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

struct ImageProbe {
    int32_t width = 0;
    int32_t height = 0;
    const char *format = "";
    int32_t bits_per_pixel = 0;
};

/**
 * @brief Read the picture size, format and colour depth from the PNG or JPEG header.
 */
bool probe_image(const std::string &path, ImageProbe &probe)
{
    auto *file = std::fopen(path.c_str(), "rb");
    if (file == nullptr) {
        return false;
    }
    std::vector<uint8_t> data(65536);
    const size_t got = std::fread(data.data(), 1, data.size(), file);
    std::fclose(file);
    if (got < 24) {
        return false;
    }
    if (data[0] == 0x89 && data[1] == 'P' && data[2] == 'N' && data[3] == 'G') {
        probe.width = static_cast<int32_t>(read_be32(data.data() + 16));
        probe.height = static_cast<int32_t>(read_be32(data.data() + 20));
        probe.format = "PNG";
        // IHDR keeps the bit depth at offset 24 and the colour type at 25.
        const uint8_t bit_depth = data[24];
        const uint8_t color_type = data[25];
        int32_t channels = 1;
        switch (color_type) {
        case 2: // RGB
        case 6: // RGBA
            channels = (color_type == 2) ? 3 : 4;
            break;
        case 4: // grey + alpha
            channels = 2;
            break;
        default: // grey or palette
            channels = 1;
            break;
        }
        probe.bits_per_pixel = static_cast<int32_t>(bit_depth) * channels;
        return probe.width > 0 && probe.height > 0;
    }
    if (data[0] == 0xFF && data[1] == 0xD8) {
        size_t pos = 2;
        while (pos + 9 < got) {
            if (data[pos] != 0xFF) {
                ++pos;
                continue;
            }
            const uint8_t marker = data[pos + 1];
            if (marker == 0xFF) {
                ++pos;
                continue;
            }
            if (marker == 0xD8 || marker == 0x01 || (marker >= 0xD0 && marker <= 0xD7)) {
                pos += 2;
                continue;
            }
            const uint16_t length = read_be16(data.data() + pos + 2);
            const bool is_sof = (marker >= 0xC0 && marker <= 0xCF) && marker != 0xC4 &&
                                marker != 0xC8 && marker != 0xCC;
            if (is_sof) {
                probe.height = static_cast<int32_t>(read_be16(data.data() + pos + 5));
                probe.width = static_cast<int32_t>(read_be16(data.data() + pos + 7));
                probe.format = "JPEG";
                // SOF carries the sample precision at offset 4 and the number of
                // components at offset 9; 8-bit YCbCr reports as 24-bit.
                probe.bits_per_pixel = static_cast<int32_t>(data[pos + 4]) * data[pos + 9];
                return probe.width > 0 && probe.height > 0;
            }
            if (length < 2) {
                break;
            }
            pos += 2U + length;
        }
    }
    return false;
}

} // namespace

GalleryApp::GalleryApp() = default;
GalleryApp::~GalleryApp() = default;

system::core::AppManifest GalleryApp::get_manifest() const
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

system::core::AppGuiDescriptor GalleryApp::get_gui_descriptor() const
{
    return {
        .root_kind = system::core::GuiRootKind::File,
        .root = GUI_ROOT,
        .resources = {},
        .screen_flows = {
            system::core::GuiScreenFlowEntry{
                .screen_flow = GALLERY_FLOW_ID,
                .layer = system::core::GuiAppLayer::AppDefault,
            },
        },
    };
}

std::expected<void, std::string> GalleryApp::on_install(system::core::AppContext &)
{
    BROOKESIA_LOGI("Gallery installed");
    return {};
}

void GalleryApp::on_uninstall(system::core::AppContext &)
{
}

std::expected<void, std::string> GalleryApp::on_start(system::core::AppContext &context)
{
    BROOKESIA_LOG_TRACE_GUARD_WITH_THIS();

    context_ = &context;
    slideshow_ = false;
    slideshow_timer_ = system::core::INVALID_TIMER_ID;
    (void)context.gui().set_binding_value(PATH_SLIDE_TEXT, "slide_text", "自动播放");
    scan_images();
    index_ = 0;
    subscribe_actions(context);
    if (images_.empty()) {
        (void)context.gui().set_binding_value(PATH_PHOTO, "photo_src", "");
        (void)context.gui().set_binding_value(PATH_FILE_NAME, "photo_name", "没有找到图片");
        (void)context.gui().set_binding_value(PATH_PHOTO_INDEX, "photo_index", "0 / 0");
        (void)context.gui().set_binding_value(PATH_PHOTO_INFO, "photo_info", "");
        set_status(context, "把图片放进 SD 卡或 /littlefs/pictures 后点刷新");
        return {};
    }
    show_image(context, 0);
    return {};
}

std::expected<void, std::string> GalleryApp::on_stop(system::core::AppContext &context)
{
    stop_slideshow();
    // Release the decoded picture before the document is torn down.
    (void)context.gui().unregister_image(PHOTO_IMAGE_ID);
    action_connections_.clear();
    context_ = nullptr;
    return {};
}

std::expected<void, std::string> GalleryApp::on_pause(system::core::AppContext &)
{
    stop_slideshow();
    return {};
}

std::expected<void, std::string> GalleryApp::on_resume(system::core::AppContext &context)
{
    slideshow_ = false;
    slideshow_timer_ = system::core::INVALID_TIMER_ID;
    (void)context.gui().set_binding_value(PATH_SLIDE_TEXT, "slide_text", "自动播放");
    scan_images();
    if (images_.empty()) {
        (void)context.gui().set_binding_value(PATH_PHOTO, "photo_src", "");
        (void)context.gui().set_binding_value(PATH_FILE_NAME, "photo_name", "没有找到图片");
        (void)context.gui().set_binding_value(PATH_PHOTO_INDEX, "photo_index", "0 / 0");
        (void)context.gui().set_binding_value(PATH_PHOTO_INFO, "photo_info", "");
        set_status(context, "把图片放进 SD 卡或 /littlefs/pictures 后点刷新");
        return {};
    }
    if (index_ >= images_.size()) {
        index_ = 0;
    }
    show_image(context, index_);
    return {};
}

std::expected<void, std::string> GalleryApp::on_timer(
    system::core::AppContext &context,
    system::core::TimerId timer_id,
    std::string_view name
)
{
    (void)timer_id;
    if (name != SLIDESHOW_TIMER_NAME || !slideshow_ || images_.size() < 2) {
        return {};
    }
    step_image(context, 1);
    return {};
}

void GalleryApp::scan_images()
{
    images_.clear();
    for (const auto *dir_path : IMAGE_DIRS) {
        auto *dir = opendir(dir_path);
        if (dir == nullptr) {
            continue;
        }
        while (auto *entry = readdir(dir)) {
            const std::string name = entry->d_name;
            if (name == "." || name == ".." || !has_image_extension(name)) {
                continue;
            }
            const std::string full_path = std::string(dir_path) + "/" + name;
            struct stat st {};
            if (stat(full_path.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) {
                continue;
            }
            images_.push_back(full_path);
        }
        closedir(dir);
    }
    std::sort(images_.begin(), images_.end());
    images_.erase(std::unique(images_.begin(), images_.end()), images_.end());
    BROOKESIA_LOGI("Gallery found %1% picture(s)", images_.size());
}

void GalleryApp::subscribe_actions(system::core::AppContext &context)
{
    std::vector<system::core::GuiActionHandlerSubscription> subscriptions;
    subscriptions.push_back({
        .action = ACTION_PREV,
        .handler = [this](const gui::Event &) {
            if (context_ != nullptr) {
                step_image(*context_, -1);
            }
        },
    });
    subscriptions.push_back({
        .action = ACTION_NEXT,
        .handler = [this](const gui::Event &) {
            if (context_ != nullptr) {
                step_image(*context_, 1);
            }
        },
    });
    subscriptions.push_back({
        .action = ACTION_SET_WALLPAPER,
        .handler = [this](const gui::Event &) {
            if (context_ != nullptr) {
                set_wallpaper(*context_);
            }
        },
    });
    subscriptions.push_back({
        .action = ACTION_REFRESH,
        .handler = [this](const gui::Event &) {
            if (context_ == nullptr) {
                return;
            }
            stop_slideshow();
            (void)context_->gui().set_binding_value(PATH_SLIDE_TEXT, "slide_text", "自动播放");
            scan_images();
            if (images_.empty()) {
                (void)context_->gui().set_binding_value(PATH_PHOTO, "photo_src", "");
                (void)context_->gui().set_binding_value(PATH_FILE_NAME, "photo_name", "没有找到图片");
                (void)context_->gui().set_binding_value(PATH_PHOTO_INDEX, "photo_index", "0 / 0");
                (void)context_->gui().set_binding_value(PATH_PHOTO_INFO, "photo_info", "");
                set_status(*context_, "把图片放进 SD 卡或 /littlefs/pictures 后点刷新");
                return;
            }
            index_ = 0;
            show_image(*context_, index_);
            set_status(*context_, "列表已刷新");
        },
    });
    subscriptions.push_back({
        .action = ACTION_SLIDESHOW,
        .handler = [this](const gui::Event &) {
            if (context_ != nullptr) {
                toggle_slideshow(*context_);
            }
        },
    });

    action_connections_ = context.gui().subscribe_actions(subscriptions);
}

void GalleryApp::show_image(system::core::AppContext &context, size_t index)
{
    if (images_.empty()) {
        return;
    }
    index_ = index % images_.size();
    const auto &path = images_[index_];

    // Reuse one resource id: re-registering replaces the entry and releases the
    // decoded pixels of the previous picture instead of leaking them.
    auto register_result = context.gui().register_image(gui::RuntimeImageResource{
        .id = PHOTO_IMAGE_ID,
        .primary_src = path,
        .preload = false,
    });
    if (!register_result) {
        BROOKESIA_LOGW("Failed to register gallery image %1%: %2%", path, register_result.error());
        set_status(context, "无法读取这张图片");
        return;
    }

    auto src_result = context.gui().set_binding_value(PATH_PHOTO, "photo_src", PHOTO_IMAGE_ID);
    if (!src_result) {
        BROOKESIA_LOGW("Failed to show gallery image %1%: %2%", path, src_result.error());
        return;
    }
    (void)context.gui().set_binding_value(PATH_FILE_NAME, "photo_name", file_name_of(path));
    (void)context.gui().set_binding_value(
        PATH_PHOTO_INDEX, "photo_index",
        std::to_string(index_ + 1) + " / " + std::to_string(images_.size())
    );

    uint64_t bytes = 0;
    struct stat st {};
    if (stat(path.c_str(), &st) == 0) {
        bytes = static_cast<uint64_t>(st.st_size);
    }
    std::string info;
    ImageProbe probe;
    const bool probed = probe_image(path, probe);
    if (probed) {
        info = std::to_string(probe.width) + "x" + std::to_string(probe.height);
    }
    if (bytes > 0) {
        if (!info.empty()) {
            info += " · ";
        }
        info += format_size(bytes);
    }
    if (probed && probe.format[0] != '\0') {
        info += std::string(" · ") + probe.format;
        if (probe.bits_per_pixel > 0) {
            info += " · " + std::to_string(probe.bits_per_pixel) + "-bit";
        }
    }
    (void)context.gui().set_binding_value(PATH_PHOTO_INFO, "photo_info", info);
}

void GalleryApp::step_image(system::core::AppContext &context, int direction)
{
    if (images_.empty()) {
        return;
    }
    const auto count = images_.size();
    const auto next = (direction >= 0) ? ((index_ + 1) % count) : ((index_ + count - 1) % count);
    show_image(context, next);
    if (!slideshow_) {
        set_status(context, "");
    }
}

void GalleryApp::toggle_slideshow(system::core::AppContext &context)
{
    if (slideshow_) {
        stop_slideshow();
        (void)context.gui().set_binding_value(PATH_SLIDE_TEXT, "slide_text", "自动播放");
        set_status(context, "已停止自动播放");
        return;
    }
    if (images_.size() < 2) {
        set_status(context, "至少需要两张图片才能自动播放");
        return;
    }
    auto timer = context.timer().start_periodic(SLIDESHOW_TIMER_NAME, SLIDESHOW_INTERVAL_MS);
    if (!timer) {
        BROOKESIA_LOGW("Failed to start gallery slideshow timer: %1%", timer.error());
        set_status(context, "无法启动自动播放");
        return;
    }
    slideshow_timer_ = *timer;
    slideshow_ = true;
    (void)context.gui().set_binding_value(PATH_SLIDE_TEXT, "slide_text", "停止播放");
    set_status(context, "自动播放中（每 4 秒换一张）");
}

void GalleryApp::stop_slideshow()
{
    if (slideshow_timer_ != system::core::INVALID_TIMER_ID && context_ != nullptr) {
        (void)context_->timer().stop(slideshow_timer_);
    }
    slideshow_timer_ = system::core::INVALID_TIMER_ID;
    slideshow_ = false;
}

void GalleryApp::set_wallpaper(system::core::AppContext &context)
{
    if (images_.empty()) {
        set_status(context, "没有可用的图片");
        return;
    }
    const auto &path = images_[index_];
    if (!ensure_directory(WALLPAPER_CONFIG_DIR) || !write_text_file(WALLPAPER_CONFIG_PATH, path)) {
        BROOKESIA_LOGW("Failed to write wallpaper config: %1%", WALLPAPER_CONFIG_PATH);
        set_status(context, "写入壁纸配置失败");
        return;
    }
    BROOKESIA_LOGI("Wallpaper set to %1%", path);
    set_status(context, "已设为壁纸，返回桌面即可看到");
}

void GalleryApp::set_status(system::core::AppContext &context, const std::string &text)
{
    (void)context.gui().set_binding_value(PATH_STATUS, "status_text", text);
}

} // namespace esp_brookesia::app::gallery

BROOKESIA_SYSTEM_CORE_APP_PROVIDER_REGISTER_WITH_SYMBOL(
    esp_brookesia::app::gallery::GalleryAppProvider,
    "brookesia.waveshare.gallery",
    app_gallery_provider_symbol
);
