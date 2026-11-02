/*
 * SPDX-License-Identifier: Apache-2.0
 */
#include "brookesia/app_system_monitor/system_monitor_app.hpp"

#include <atomic>
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#include "driver/temperature_sensor.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "lwip/sockets.h"

#include "boost/thread.hpp"
#include "brookesia/lib_utils/describe_helpers.hpp"
#include "brookesia/lib_utils/function_guard.hpp"
#include "brookesia/lib_utils/thread_config.hpp"
#include "brookesia/service_helper/media/audio.hpp"

#include "private/utils.hpp"

namespace esp_brookesia::app::system_monitor {

using namespace esp_brookesia::system;

namespace {

constexpr const char *APP_ID = "brookesia.waveshare.system_monitor";
constexpr const char *APP_NAME = "System Monitor";
constexpr const char *APP_NAME_ZH_CN = "系统监视器";
constexpr const char *APP_ICON_ID = "launcher_icon";
constexpr const char *GUI_ROOT = "res/root.json";
constexpr const char *MONITOR_FLOW_ID = "system_monitor_flow";
constexpr const char *REFRESH_TIMER_NAME = "sysmon_refresh";
constexpr int32_t REFRESH_PERIOD_MS = 1000;
constexpr int UDP_PORT = 9999;
constexpr uint32_t UDP_TASK_STACK_SIZE = 4 * 1024;
constexpr uint32_t LINK_STALE_MS = 5000;

// Absolute paths inside the monitor screen document. Every node id segment is
// part of the path, so nested cards must be listed explicitly.
constexpr const char *PATH_LINK_STATE = "/system_monitor/dashboard/header/link_state";

constexpr const char *PATH_CPU_ARC = "/system_monitor/dashboard/gauges/cpu_card/cpu_arc";
constexpr const char *PATH_CPU_VALUE = "/system_monitor/dashboard/gauges/cpu_card/cpu_center/cpu_value";
constexpr const char *PATH_CPU_TEMP = "/system_monitor/dashboard/gauges/cpu_card/cpu_center/cpu_temp";
constexpr const char *PATH_CPU_DETAIL = "/system_monitor/dashboard/gauges/cpu_card/cpu_detail";

constexpr const char *PATH_MEM_ARC = "/system_monitor/dashboard/gauges/mem_card/mem_arc";
constexpr const char *PATH_MEM_VALUE = "/system_monitor/dashboard/gauges/mem_card/mem_center/mem_value";
constexpr const char *PATH_MEM_DETAIL = "/system_monitor/dashboard/gauges/mem_card/mem_detail";

constexpr const char *PATH_DISK_ARC = "/system_monitor/dashboard/gauges/disk_card/disk_arc";
constexpr const char *PATH_DISK_VALUE = "/system_monitor/dashboard/gauges/disk_card/disk_center/disk_value";
constexpr const char *PATH_DISK_DETAIL = "/system_monitor/dashboard/gauges/disk_card/disk_detail";

constexpr const char *PATH_GPU_ARC = "/system_monitor/dashboard/gauges/gpu_card/gpu_arc";
constexpr const char *PATH_GPU_VALUE = "/system_monitor/dashboard/gauges/gpu_card/gpu_center/gpu_value";
constexpr const char *PATH_GPU_TEMP = "/system_monitor/dashboard/gauges/gpu_card/gpu_center/gpu_temp";
constexpr const char *PATH_GPU_DETAIL = "/system_monitor/dashboard/gauges/gpu_card/gpu_detail";

constexpr const char *PATH_NET_DOWN = "/system_monitor/dashboard/bottom/net_card/net_col/net_down";
constexpr const char *PATH_NET_UP = "/system_monitor/dashboard/bottom/net_card/net_col/net_up";
constexpr const char *PATH_IO_READ = "/system_monitor/dashboard/bottom/net_card/io_col/io_read";
constexpr const char *PATH_IO_WRITE = "/system_monitor/dashboard/bottom/net_card/io_col/io_write";

SystemMonitorApp::Snapshot g_snapshot;
std::mutex g_snapshot_mutex;
std::atomic<bool> g_udp_quit{false};

// Locate a quoted "key" and return a pointer to the first byte of its value.
// Matching the closing quote avoids accidental hits such as "cpu" inside
// "cpu_temp" or "mem_used" inside "gpu_mem_used".
const char *find_json_value(const std::vector<char> &buf, const char *key)
{
    const std::string pattern = std::string("\"") + key + "\"";
    const char *begin = buf.data();
    const char *end = begin + buf.size();
    const char *found = std::search(begin, end, pattern.begin(), pattern.end());
    if (found == end) {
        return nullptr;
    }
    const char *scan = found + pattern.size();
    while (scan < end && (*scan == ' ' || *scan == '\t' || *scan == '\r' || *scan == '\n' || *scan == ':')) {
        ++scan;
    }
    return scan < end ? scan : nullptr;
}

float get_json_float(const std::vector<char> &buf, const char *key)
{
    const char *scan = find_json_value(buf, key);
    if (scan == nullptr) {
        return 0.0F;
    }
    const char *end = buf.data() + buf.size();
    float value = 0.0F;
    auto result = std::from_chars(scan, end, value);
    return result.ec == std::errc() ? value : 0.0F;
}

/**
 * @brief Read an optional float that has no meaningful zero value (temperatures).
 *
 * Returns @p fallback when the key is missing or not a number, so the UI can tell
 * "sensor not reported" apart from a real reading.
 */
float get_json_optional_float(const std::vector<char> &buf, const char *key, float fallback)
{
    if (find_json_value(buf, key) == nullptr) {
        return fallback;
    }
    return get_json_float(buf, key);
}

std::string get_json_string(const std::vector<char> &buf, const char *key)
{
    const char *scan = find_json_value(buf, key);
    if (scan == nullptr || *scan != '"') {
        return {};
    }
    ++scan;
    const char *end = buf.data() + buf.size();
    std::string value;
    while (scan < end && *scan != '"') {
        if (*scan == '\\' && (scan + 1) < end) {
            ++scan;
            switch (*scan) {
            case 'n':
                value.push_back('\n');
                break;
            case 'r':
                value.push_back('\r');
                break;
            case 't':
                value.push_back('\t');
                break;
            case '\\':
            case '"':
            case '/':
                value.push_back(*scan);
                break;
            default:
                value.push_back(*scan);
                break;
            }
        } else {
            value.push_back(*scan);
        }
        ++scan;
    }
    return value;
}

void udp_receive_task(void *)
{
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock < 0) {
        BROOKESIA_LOGE("SystemMonitor UDP: failed to create socket");
        g_udp_quit = true;
        vTaskDelete(nullptr);
        return;
    }
    sockaddr_in local_addr = {};
    local_addr.sin_family = AF_INET;
    local_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    local_addr.sin_port = htons(UDP_PORT);
    if (bind(sock, reinterpret_cast<struct sockaddr *>(&local_addr), sizeof(local_addr)) != 0) {
        BROOKESIA_LOGE("SystemMonitor UDP: bind %d failed", UDP_PORT);
        lwip_close(sock);
        g_udp_quit = true;
        vTaskDelete(nullptr);
        return;
    }
    // Allow quick reuse and make floor of 1 second blocking so shutdown checks often.
    int opt = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    timeval tv = { .tv_sec = 1, .tv_usec = 0 };
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    BROOKESIA_LOGI("SystemMonitor UDP listening on port %d", UDP_PORT);

    while (!g_udp_quit) {
        sockaddr_in from = {};
        std::vector<char> buf(1024);
        socklen_t from_len = sizeof(from);
        int len = recvfrom(sock, buf.data(), buf.size() - 1, 0,
                           reinterpret_cast<struct sockaddr *>(&from), &from_len);
        if (len <= 0) {
            continue;
        }
        buf[len] = '\0';
        {
            std::lock_guard<std::mutex> lock(g_snapshot_mutex);
            if (std::memchr(buf.data(), '"', len) == nullptr) {
                continue; // crude guard against non-agent packets
            }
            g_snapshot.cpu = get_json_float(buf, "cpu");
            g_snapshot.mem_used = get_json_float(buf, "mem_used");
            g_snapshot.mem_total = get_json_float(buf, "mem_total");
            g_snapshot.disk_used = get_json_float(buf, "disk_used");
            g_snapshot.disk_total = get_json_float(buf, "disk_total");
            g_snapshot.gpu_util = get_json_float(buf, "gpu_util");
            g_snapshot.gpu_mem_used = get_json_float(buf, "gpu_mem_used");
            g_snapshot.gpu_mem_total = get_json_float(buf, "gpu_mem_total");
            g_snapshot.net_down = get_json_float(buf, "net_down");
            g_snapshot.net_up = get_json_float(buf, "net_up");
            g_snapshot.disk_read = get_json_float(buf, "disk_read");
            g_snapshot.disk_write = get_json_float(buf, "disk_write");
            g_snapshot.cpu_temp = get_json_optional_float(buf, "cpu_temp", -1.0F);
            g_snapshot.gpu_temp = get_json_optional_float(buf, "gpu_temp", -1.0F);
            const auto disk_count = static_cast<int>(get_json_float(buf, "disk_count"));
            g_snapshot.disk_count = std::clamp(
                                        disk_count, 0, static_cast<int>(SystemMonitorApp::Snapshot::MAX_DISKS)
                                    );
            for (int i = 0; i < g_snapshot.disk_count; ++i) {
                char key[24] = {};
                std::snprintf(key, sizeof(key), "disk%d_pct", i);
                g_snapshot.disk_pcts[static_cast<size_t>(i)] = get_json_float(buf, key);
                std::snprintf(key, sizeof(key), "disk%d_name", i);
                g_snapshot.disk_names[static_cast<size_t>(i)] = get_json_string(buf, key);
            }
            g_snapshot.has_data = true;
            g_snapshot.last_packet_tick = xTaskGetTickCount();
        }
    }
    lwip_close(sock);
    BROOKESIA_LOGI("SystemMonitor UDP task exited");
    vTaskDelete(nullptr);
}

std::string format_str(std::string_view fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    std::vector<char> buf(256);
    int len = vsnprintf(buf.data(), buf.size(), fmt.data(), args);
    va_end(args);
    (void)len;
    return std::string(buf.data());
}

// Streams a PCM WAV file through the HAL codec player. The audio service cannot
// be used for the alarm because it depends on the DSP processor implementation,
// which this firmware intentionally leaves out.
bool play_wav_with_codec_player(hal::audio::CodecPlayerIface *player, const char *path, uint8_t volume)
{
    if (player == nullptr) {
        return false;
    }
    std::FILE *file = std::fopen(path, "rb");
    if (file == nullptr) {
        BROOKESIA_LOGW("SystemMonitor: audio file is not available: %s", path);
        return false;
    }

    uint8_t riff[12] = {};
    bool ok = std::fread(riff, 1, sizeof(riff), file) == sizeof(riff) &&
              std::memcmp(riff, "RIFF", 4) == 0 && std::memcmp(riff + 8, "WAVE", 4) == 0;
    uint16_t channels = 1;
    uint16_t bits = 16;
    uint32_t sample_rate = 16000;
    bool opened = false;
    std::vector<uint8_t> buffer(1024);

    while (ok) {
        uint8_t chunk[8] = {};
        if (std::fread(chunk, 1, sizeof(chunk), file) != sizeof(chunk)) {
            break;
        }
        const uint32_t size = static_cast<uint32_t>(chunk[4]) | (static_cast<uint32_t>(chunk[5]) << 8) |
                              (static_cast<uint32_t>(chunk[6]) << 16) | (static_cast<uint32_t>(chunk[7]) << 24);
        if (std::memcmp(chunk, "fmt ", 4) == 0 && size >= 16) {
            uint8_t format[16] = {};
            if (std::fread(format, 1, sizeof(format), file) != sizeof(format)) {
                ok = false;
                break;
            }
            channels = static_cast<uint16_t>(format[2] | (format[3] << 8));
            sample_rate = static_cast<uint32_t>(format[4]) | (static_cast<uint32_t>(format[5]) << 8) |
                          (static_cast<uint32_t>(format[6]) << 16) | (static_cast<uint32_t>(format[7]) << 24);
            bits = static_cast<uint16_t>(format[14] | (format[15] << 8));
            if (size > sizeof(format)) {
                std::fseek(file, static_cast<long>(size - sizeof(format)), SEEK_CUR);
            }
            continue;
        }
        if (std::memcmp(chunk, "data", 4) == 0) {
            hal::audio::CodecPlayerIface::Config config = {
                .bits = static_cast<uint8_t>(bits),
                .channels = static_cast<uint8_t>(channels),
                .sample_rate = sample_rate,
            };
            if (!player->open(config)) {
                BROOKESIA_LOGW("SystemMonitor: failed to open the codec player");
                ok = false;
                break;
            }
            opened = true;
            player->set_volume(volume);
            if (player->is_pa_on_off_supported() && !player->is_pa_on()) {
                player->set_pa_on_off(true);
            }
            uint32_t remaining = size;
            while (remaining > 0) {
                const size_t want = std::min<size_t>(remaining, buffer.size());
                const size_t read = std::fread(buffer.data(), 1, want, file);
                if (read == 0) {
                    break;
                }
                player->write_data(buffer.data(), read);
                remaining -= static_cast<uint32_t>(read);
            }
            break;
        }
        std::fseek(file, static_cast<long>(size), SEEK_CUR);
    }

    if (opened) {
        player->close();
    }
    std::fclose(file);
    return opened;
}

} // namespace

SystemMonitorApp::SystemMonitorApp() = default;
SystemMonitorApp::~SystemMonitorApp() = default;

system::core::AppManifest SystemMonitorApp::get_manifest() const
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
        .preload_dom = false,
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

system::core::AppGuiDescriptor SystemMonitorApp::get_gui_descriptor() const
{
    return {
        .root_kind = system::core::GuiRootKind::File,
        .root = GUI_ROOT,
        .resources = {},
        .screen_flows = {
            system::core::GuiScreenFlowEntry{
                .screen_flow = MONITOR_FLOW_ID,
                .layer = system::core::GuiAppLayer::AppDefault,
            },
        },
    };
}

std::expected<void, std::string> SystemMonitorApp::on_install(system::core::AppContext &)
{
    BROOKESIA_LOGI("SystemMonitor installed");
    return {};
}

void SystemMonitorApp::on_uninstall(system::core::AppContext &)
{
}

std::expected<void, std::string> SystemMonitorApp::on_start(system::core::AppContext &context)
{
    BROOKESIA_LOG_TRACE_GUARD_WITH_THIS();

    if (!ensure_udp_started()) {
        return std::unexpected("Failed to start SystemMonitor UDP receiver");
    }
    ensure_temperature_sensor();

    auto timer = context.timer().start_periodic(REFRESH_TIMER_NAME, REFRESH_PERIOD_MS);
    if (!timer) {
        return std::unexpected("Failed to start refresh timer: " + timer.error());
    }
    refresh_timer_id_ = timer.value();

    // Paint an initial refresh so the user does not see dashes until the next tick.
    return on_timer(context, refresh_timer_id_, REFRESH_TIMER_NAME);
}

std::expected<void, std::string> SystemMonitorApp::on_stop(system::core::AppContext &)
{
    std::expected<void, std::string> result;
    if (refresh_timer_id_ != system::core::INVALID_TIMER_ID) {
        // Timer runtime belongs to the context; keep the id for reference only.
        refresh_timer_id_ = system::core::INVALID_TIMER_ID;
    }
    stop_udp();
    release_temperature_sensor();
    if (static_cast<bool>(alarm_player_)) {
        alarm_player_->close();
        alarm_player_.reset();
    }
    return result;
}

std::expected<void, std::string> SystemMonitorApp::on_pause(system::core::AppContext &)
{
    // Keep the UDP task running while paused so the snapshot stays warm, but stop
    // painting the hidden GUI (and stop the alarm beeps) until we come back.
    paused_ = true;
    return {};
}

std::expected<void, std::string> SystemMonitorApp::on_resume(system::core::AppContext &context)
{
    paused_ = false;
    last_alarm_tick_ = 0;
    return on_timer(context, refresh_timer_id_, REFRESH_TIMER_NAME);
}

bool SystemMonitorApp::ensure_udp_started()
{
    if (udp_running_) {
        return true;
    }
    g_udp_quit = false;
    {
        BROOKESIA_THREAD_CONFIG_GUARD({
            .stack_size = UDP_TASK_STACK_SIZE,
            .stack_in_ext = true,
        });
        boost::thread udp_thread([]() { udp_receive_task(nullptr); });
        udp_thread.detach();
    }
    udp_running_ = true;
    return true;
}

void SystemMonitorApp::stop_udp()
{
    if (!udp_running_) {
        return;
    }
    g_udp_quit = true;
    udp_running_ = false;
}

bool SystemMonitorApp::ensure_temperature_sensor()
{
    if (temperature_sensor_ != nullptr) {
        return true;
    }

    temperature_sensor_handle_t handle = nullptr;
    temperature_sensor_config_t config = TEMPERATURE_SENSOR_CONFIG_DEFAULT(-10, 80);
    auto result = temperature_sensor_install(&config, &handle);
    if (result != ESP_OK) {
        BROOKESIA_LOGW("SystemMonitor: temperature sensor install failed: %s", esp_err_to_name(result));
        return false;
    }
    result = temperature_sensor_enable(handle);
    if (result != ESP_OK) {
        BROOKESIA_LOGW("SystemMonitor: temperature sensor enable failed: %s", esp_err_to_name(result));
        temperature_sensor_uninstall(handle);
        return false;
    }
    temperature_sensor_ = handle;
    BROOKESIA_LOGI("SystemMonitor: internal temperature sensor ready");
    return true;
}

void SystemMonitorApp::release_temperature_sensor()
{
    if (temperature_sensor_ == nullptr) {
        return;
    }
    auto handle = reinterpret_cast<temperature_sensor_handle_t>(temperature_sensor_);
    temperature_sensor_disable(handle);
    temperature_sensor_uninstall(handle);
    temperature_sensor_ = nullptr;
}

float SystemMonitorApp::read_soc_temperature()
{
    if (!ensure_temperature_sensor()) {
        return NAN;
    }
    auto handle = reinterpret_cast<temperature_sensor_handle_t>(temperature_sensor_);
    float celsius = NAN;
    if (temperature_sensor_get_celsius(handle, &celsius) != ESP_OK) {
        return NAN;
    }
    return celsius;
}

void SystemMonitorApp::play_alarm_if_due(uint32_t now_tick)
{
    // Three short beeps every ~2.5 s while any metric is in the red zone.
    constexpr uint32_t ALARM_INTERVAL_TICKS = pdMS_TO_TICKS(2500);
    if (last_alarm_tick_ != 0 && (now_tick - last_alarm_tick_) < ALARM_INTERVAL_TICKS) {
        return;
    }
    last_alarm_tick_ = now_tick;

    if (!static_cast<bool>(alarm_player_)) {
        alarm_player_ = hal::acquire_first_interface<hal::audio::CodecPlayerIface>();
    }
    if (!static_cast<bool>(alarm_player_)) {
        if (!alarm_audio_warned_) {
            alarm_audio_warned_ = true;
            BROOKESIA_LOGW("SystemMonitor: audio codec player is unavailable for the alarm");
        }
        return;
    }
    if (!play_wav_with_codec_player(alarm_player_.get().get(), "/littlefs/sounds/alarm.wav", 90)) {
        BROOKESIA_LOGW("SystemMonitor: alarm sound playback failed");
    }
}

std::expected<void, std::string> SystemMonitorApp::on_timer(
    system::core::AppContext &context,
    system::core::TimerId timer_id,
    std::string_view name
)
{
    (void)timer_id;
    if (name != REFRESH_TIMER_NAME) {
        return {};
    }
    if (paused_) {
        // The GUI flow is detached while the app sits in the background.
        return {};
    }

    Snapshot snapshot;
    {
        std::lock_guard<std::mutex> lock(g_snapshot_mutex);
        snapshot = g_snapshot;
    }

    const uint32_t now_tick = xTaskGetTickCount();
    const bool link_fresh = snapshot.has_data &&
                            ((now_tick - snapshot.last_packet_tick) < pdMS_TO_TICKS(LINK_STALE_MS));

    // Only warn once: a missing path would otherwise flood the log once per second.
    static std::atomic<bool> path_warned{false};
    auto set_text = [&](const char *path, const std::string &value) {
        auto result = context.gui().set_text(path, value);
        if (!result && !path_warned.exchange(true)) {
            BROOKESIA_LOGW("SystemMonitor set_text %1% failed: %2%", path, result.error());
        }
    };
    auto set_arc = [&](const char *path, int32_t value) {
        auto result = context.gui().set_value(path, value);
        if (!result && !path_warned.exchange(true)) {
            BROOKESIA_LOGW("SystemMonitor set_value %1% failed: %2%", path, result.error());
        }
    };
    auto percent = [](float used, float total) -> int32_t {
        if (total <= 0.0F) {
            return 0;
        }
        int32_t value = static_cast<int32_t>(used / total * 100.0F + 0.5F);
        return value < 0 ? 0 : (value > 100 ? 100 : value);
    };

    if (!link_fresh) {
        set_text(PATH_LINK_STATE, "WAITING FOR PC AGENT    UDP :9999");
        set_arc(PATH_CPU_ARC, 0);
        set_arc(PATH_MEM_ARC, 0);
        set_arc(PATH_DISK_ARC, 0);
        set_arc(PATH_GPU_ARC, 0);
        set_text(PATH_CPU_VALUE, "--");
        set_text(PATH_MEM_VALUE, "--");
        set_text(PATH_DISK_VALUE, "--");
        set_text(PATH_GPU_VALUE, "--");
        set_text(PATH_CPU_TEMP, "CPU -- C");
        set_text(PATH_GPU_TEMP, "GPU -- C");
        set_text(PATH_CPU_DETAIL, "waiting for agent");
        set_text(PATH_MEM_DETAIL, "waiting for agent");
        set_text(PATH_DISK_DETAIL, "waiting for agent");
        set_text(PATH_GPU_DETAIL, "waiting for agent");
        set_text(PATH_NET_DOWN, "DOWN  0.00 Mb/s");
        set_text(PATH_NET_UP, "UP  0.00 Mb/s");
        set_text(PATH_IO_READ, "READ  0.00 MB/s");
        set_text(PATH_IO_WRITE, "WRITE  0.00 MB/s");
        last_alarm_tick_ = 0;
        alarm_audio_warned_ = false;
        return {};
    }

    const int32_t cpu_pct = percent(snapshot.cpu, 100.0F);
    const int32_t mem_pct = percent(snapshot.mem_used, snapshot.mem_total);
    // The ring shows how much of the total capacity of all volumes is in use; a
    // single volume must not be able to masquerade as the overall figure.
    const int32_t disk_pct = percent(snapshot.disk_used, snapshot.disk_total);
    const bool gpu_present = snapshot.gpu_util >= 0.0F;
    const int32_t gpu_pct = gpu_present ? percent(snapshot.gpu_util, 100.0F) : 0;

    // Every volume is still listed individually, and one that is at or above the
    // alarm threshold is marked so the ring percentage stays explainable.
    std::string disk_detail;
    int32_t disk_worst = 0;
    bool has_disk_worst = false;
    std::string disk_lines;
    int line_entries = 0;
    for (int i = 0; i < snapshot.disk_count; ++i) {
        const auto raw_pct = snapshot.disk_pcts[static_cast<size_t>(i)];
        int32_t volume_pct = static_cast<int32_t>(raw_pct + 0.5F);
        volume_pct = std::clamp<int32_t>(volume_pct, 0, 100);
        if (volume_pct > disk_worst || !has_disk_worst) {
            disk_worst = volume_pct;
            has_disk_worst = true;
        }

        std::string name = snapshot.disk_names[static_cast<size_t>(i)];
        while (!name.empty() && (name.back() == '\\' || name.back() == '/')) {
            name.pop_back();
        }
        if (name.empty()) {
            name = "/";
        }
        if (line_entries == 3) {
            disk_lines += "\n";
            line_entries = 0;
        } else if (line_entries != 0) {
            disk_lines += "  ";
        }
        disk_lines += format_str("%s %d%%%s", name.c_str(), static_cast<int>(volume_pct),
                                 volume_pct >= 90 ? "!" : "");
        ++line_entries;
    }
    if (has_disk_worst) {
        disk_detail = disk_lines + "\n" + format_str("%.0f/%.0f GB total", snapshot.disk_used,
                      snapshot.disk_total);
    } else {
        disk_detail = format_str("%.0f / %.0f GB", snapshot.disk_used, snapshot.disk_total);
    }

    // The ring center shows the PC CPU temperature reported by LibreHardwareMonitor
    // through the agent. It is left blank (rather than falling back to the SoC
    // sensor) so the value always describes the PC.
    constexpr float TEMP_ALARM_C = 90.0F;
    const bool cpu_temp_alarm = snapshot.cpu_temp >= TEMP_ALARM_C;
    const bool gpu_temp_alarm = gpu_present && snapshot.gpu_temp >= TEMP_ALARM_C;
    auto cpu_temp_text = (snapshot.cpu_temp >= 0.0F)
                         ? format_str("CPU %.1f C", snapshot.cpu_temp)
                         : "CPU -- C";
    if (cpu_temp_alarm) {
        cpu_temp_text += " HIGH!";
    }

    set_text(PATH_LINK_STATE, "LINK ONLINE    UDP :9999");

    set_arc(PATH_CPU_ARC, cpu_pct);
    set_text(PATH_CPU_VALUE, format_str("%d%%", static_cast<int>(cpu_pct)));
    set_text(PATH_CPU_TEMP, cpu_temp_text);
    set_text(PATH_CPU_DETAIL, "1s average");

    set_arc(PATH_MEM_ARC, mem_pct);
    set_text(PATH_MEM_VALUE, format_str("%d%%", static_cast<int>(mem_pct)));
    set_text(PATH_MEM_DETAIL, format_str("%.1f / %.1f GB", snapshot.mem_used / 1024.0F,
                                         snapshot.mem_total / 1024.0F));

    set_arc(PATH_DISK_ARC, disk_pct);
    set_text(PATH_DISK_VALUE, format_str("%d%%", static_cast<int>(disk_pct)));
    set_text(PATH_DISK_DETAIL, disk_detail);

    set_arc(PATH_GPU_ARC, gpu_pct);
    if (gpu_present) {
        set_text(PATH_GPU_VALUE, format_str("%d%%", static_cast<int>(gpu_pct)));
        auto gpu_temp_text = (snapshot.gpu_temp >= 0.0F)
                             ? format_str("GPU %.1f C", snapshot.gpu_temp)
                             : "GPU -- C";
        if (gpu_temp_alarm) {
            gpu_temp_text += " HIGH!";
        }
        set_text(PATH_GPU_TEMP, gpu_temp_text);
        auto gpu_detail = format_str("%.1f / %.1f GB", snapshot.gpu_mem_used / 1024.0F,
                                     snapshot.gpu_mem_total / 1024.0F);
        set_text(PATH_GPU_DETAIL, gpu_detail);
    } else {
        set_text(PATH_GPU_VALUE, "N/A");
        set_text(PATH_GPU_TEMP, "GPU -- C");
        set_text(PATH_GPU_DETAIL, "no NVIDIA GPU");
    }

    set_text(PATH_NET_DOWN, format_str("DOWN  %.2f Mb/s", snapshot.net_down));
    set_text(PATH_NET_UP, format_str("UP  %.2f Mb/s", snapshot.net_up));
    set_text(PATH_IO_READ, format_str("READ  %.2f MB/s", snapshot.disk_read));
    set_text(PATH_IO_WRITE, format_str("WRITE  %.2f MB/s", snapshot.disk_write));

    constexpr int32_t ALARM_THRESHOLD = 90;
    const bool cpu_alarm = cpu_pct >= ALARM_THRESHOLD;
    const bool mem_alarm = mem_pct >= ALARM_THRESHOLD;
    // The ring is an overall figure, but a single nearly full volume still has to
    // raise the alarm (and turn the ring red) so it cannot go unnoticed.
    const bool disk_alarm = disk_pct >= ALARM_THRESHOLD ||
                            (has_disk_worst && disk_worst >= ALARM_THRESHOLD);
    const bool gpu_alarm = gpu_present && gpu_pct >= ALARM_THRESHOLD;
    const bool any_alarm = cpu_alarm || mem_alarm || disk_alarm || gpu_alarm || cpu_temp_alarm ||
                           gpu_temp_alarm;

    std::vector<gui::BindingValueUpdate> color_updates;
    const auto add_arc_color = [&color_updates](const char *path, bool alarm, const char *normal_color) {
        color_updates.push_back(gui::BindingValueUpdate{
            .absolute_path = path,
            .key = "indicator_color",
            .value = alarm ? "#ef4444" : normal_color,
        });
        color_updates.push_back(gui::BindingValueUpdate{
            .absolute_path = path,
            .key = "indicator_gradient",
            .value = alarm ? "#b91c1c" : normal_color,
        });
    };
    add_arc_color(PATH_CPU_ARC, cpu_alarm, "#22d3ee");
    add_arc_color(PATH_MEM_ARC, mem_alarm, "#34d399");
    add_arc_color(PATH_DISK_ARC, disk_alarm, "#f59e0b");
    add_arc_color(PATH_GPU_ARC, gpu_alarm, "#a78bfa");
    auto color_result = context.gui().set_binding_values(color_updates);
    if (!color_result && !path_warned.exchange(true)) {
        BROOKESIA_LOGW("SystemMonitor alarm color update failed: %1%", color_result.error());
    }

    if (any_alarm) {
        play_alarm_if_due(now_tick);
    } else {
        last_alarm_tick_ = 0;
        alarm_audio_warned_ = false;
    }

    return {};
}

} // namespace esp_brookesia::app::system_monitor

BROOKESIA_SYSTEM_CORE_APP_PROVIDER_REGISTER_WITH_SYMBOL(
    esp_brookesia::app::system_monitor::SystemMonitorAppProvider,
    "brookesia.waveshare.system_monitor",
    app_system_monitor_provider_symbol
);
