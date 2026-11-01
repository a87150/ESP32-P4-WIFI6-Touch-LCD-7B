/*
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <memory>
#include <array>
#include <string>

#include "brookesia/hal_interface.hpp"
#include "brookesia/hal_interface/interfaces/audio/codec_player.hpp"
#include "brookesia/system_core.hpp"

namespace esp_brookesia::app::system_monitor {

/**
 * @brief Live PC health dashboard fed by UDP broadcasts from scripts/pc_agent.py.
 */
class SystemMonitorApp final: public system::core::IApp {
public:
    SystemMonitorApp();
    ~SystemMonitorApp() override;

    SystemMonitorApp(const SystemMonitorApp &) = delete;
    SystemMonitorApp &operator=(const SystemMonitorApp &) = delete;

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

public:
    struct Snapshot {
        static constexpr size_t MAX_DISKS = 6;
        float cpu = 0.0F;
        float mem_used = 0.0F;
        float mem_total = 0.0F;
        float disk_used = 0.0F;
        float disk_total = 0.0F;
        float gpu_util = 0.0F;
        float gpu_mem_used = 0.0F;
        float gpu_mem_total = 0.0F;
        float net_down = 0.0F;
        float net_up = 0.0F;
        float disk_read = 0.0F;
        float disk_write = 0.0F;
        /** PC CPU/GPU temperature in Celsius from the agent, -1 when unknown. */
        float cpu_temp = -1.0F;
        float gpu_temp = -1.0F;
        int disk_count = 0;
        std::array<float, MAX_DISKS> disk_pcts{};
        std::array<std::string, MAX_DISKS> disk_names{};
        bool has_data = false;
        uint32_t last_packet_tick = 0;
    };

private:
    bool ensure_udp_started();
    void stop_udp();
    bool ensure_temperature_sensor();
    void release_temperature_sensor();
    float read_soc_temperature();
    void play_alarm_if_due(uint32_t now_tick);

    system::core::TimerId refresh_timer_id_ = system::core::INVALID_TIMER_ID;
    bool paused_ = false;
    bool udp_running_ = false;
    void *udp_task_handle_ = nullptr; // TaskHandle_t, kept as void* to avoid FreeRTOS includes
    void *temperature_sensor_ = nullptr; // temperature_sensor_handle_t
    hal::InterfaceHandle<hal::audio::CodecPlayerIface> alarm_player_;
    uint32_t last_alarm_tick_ = 0;
    bool alarm_audio_warned_ = false;
};

/**
 * @brief Provider that instantiates the System Monitor app.
 */
class SystemMonitorAppProvider final: public system::core::IAppProvider {
public:
    system::core::AppManifest get_manifest() const override
    {
        static const SystemMonitorApp prototype;
        return prototype.get_manifest();
    }

    std::shared_ptr<system::core::IApp> create_app() override
    {
        return std::make_shared<SystemMonitorApp>();
    }
};

} // namespace esp_brookesia::app::system_monitor
