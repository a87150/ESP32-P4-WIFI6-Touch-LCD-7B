/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */
#pragma once

#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>
#include "brookesia/hal_interface/device.hpp"
#include "brookesia/hal_interface/interfaces/bluetooth/ble/peripheral.hpp"

namespace esp_brookesia::test {

class FakeBlePeripheral: public hal::bluetooth::ble::PeripheralIface {
public:
    static constexpr const char *INSTANCE_NAME = "TestBle:Peripheral";

    struct Counters {
        size_t configure = 0;
        size_t clear_callbacks = 0;
        size_t init = 0;
        size_t deinit = 0;
        size_t start = 0;
        size_t stop = 0;
        size_t start_advertising = 0;
        size_t stop_advertising = 0;
        size_t notify = 0;
        size_t disconnect = 0;
    };

    struct Notification {
        uint16_t connection_id = 0;
        hal::bluetooth::ble::CharacteristicId characteristic;
        hal::bluetooth::ble::ByteArray data;
    };

    bool configure(const hal::bluetooth::ble::PeripheralConfig &config, Callbacks callbacks) override;
    bool clear_callbacks() override;
    bool init() override;
    bool deinit() override;
    bool start() override;
    bool stop() override;
    bool start_advertising() override;
    bool stop_advertising() override;
    std::vector<hal::bluetooth::ble::ConnectionInfo> get_connections() const override;
    bool is_subscribed(
        uint16_t connection_id, const hal::bluetooth::ble::CharacteristicId &characteristic
    ) const override;
    bool notify(
        uint16_t connection_id, const hal::bluetooth::ble::CharacteristicId &characteristic,
        const hal::bluetooth::ble::ByteArray &data
    ) override;
    bool disconnect(uint16_t connection_id) override;

    bool inject_connect(uint16_t connection_id, std::string peer_address, uint16_t mtu);
    bool inject_mtu(uint16_t connection_id, uint16_t mtu);
    bool inject_subscription(
        uint16_t connection_id, const hal::bluetooth::ble::CharacteristicId &characteristic, bool enabled
    );
    bool inject_write(
        uint16_t connection_id, const hal::bluetooth::ble::CharacteristicId &characteristic,
        hal::bluetooth::ble::ByteArray data
    );
    void emit_late_write(const hal::bluetooth::ble::WriteEvent &event);

    void fail_next_init();
    void fail_next_start();
    void fail_next_advertising_start();
    void defer_next_advertising_start();
    Counters get_counters() const;
    std::vector<Notification> get_notifications() const;
    bool is_advertising() const;
    bool has_callbacks() const;

private:
    bool characteristic_has_property(
        const hal::bluetooth::ble::CharacteristicId &characteristic,
        bool hal::bluetooth::ble::CharacteristicConfig::*property
    ) const;
    std::optional<hal::bluetooth::ble::ConnectionInfo> find_connection(uint16_t connection_id) const;

    mutable std::mutex mutex_;
    hal::bluetooth::ble::PeripheralConfig config_;
    Callbacks callbacks_;
    Callbacks cleared_callbacks_;
    Counters counters_;
    std::vector<hal::bluetooth::ble::ConnectionInfo> connections_;
    std::vector<std::pair<uint16_t, hal::bluetooth::ble::CharacteristicId>> subscriptions_;
    std::vector<Notification> notifications_;
    bool configured_ = false;
    bool initialized_ = false;
    bool started_ = false;
    bool advertising_ = false;
    bool fail_init_ = false;
    bool fail_start_ = false;
    bool fail_advertising_start_ = false;
    bool defer_advertising_start_ = false;
};

class FakeBleDevice: public hal::Device {
public:
    static constexpr const char *NAME = "TestBle";

    FakeBleDevice()
        : hal::Device(NAME)
    {
    }

    bool probe() override;
    std::vector<hal::InterfaceSpec> get_interface_specs() const override;
    bool on_init() override;
    void on_deinit() override;
};

std::shared_ptr<FakeBlePeripheral> get_fake_ble_peripheral();

} // namespace esp_brookesia::test
