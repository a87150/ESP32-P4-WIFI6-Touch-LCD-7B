/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */
#include "fake_ble_hal.hpp"

#include <algorithm>
#include <utility>
#include "brookesia/lib_utils/plugin.hpp"

namespace esp_brookesia::test {

namespace {

std::mutex fake_peripheral_mutex;
std::weak_ptr<FakeBlePeripheral> fake_peripheral;

} // namespace

bool FakeBlePeripheral::configure(const hal::bluetooth::ble::PeripheralConfig &config, Callbacks callbacks)
{
    std::string error_message;
    if (!hal::bluetooth::ble::validate_peripheral_config(config, &error_message)) {
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    counters_.configure++;
    if (initialized_ || started_ || advertising_ || !connections_.empty()) {
        return false;
    }
    config_ = hal::bluetooth::ble::normalize_peripheral_config(config);
    callbacks_ = std::move(callbacks);
    configured_ = true;
    subscriptions_.clear();
    notifications_.clear();
    return true;
}

bool FakeBlePeripheral::clear_callbacks()
{
    std::lock_guard<std::mutex> lock(mutex_);
    counters_.clear_callbacks++;
    cleared_callbacks_ = callbacks_;
    callbacks_ = {};
    return true;
}

bool FakeBlePeripheral::init()
{
    std::lock_guard<std::mutex> lock(mutex_);
    counters_.init++;
    if (!configured_ || initialized_) {
        return initialized_;
    }
    if (std::exchange(fail_init_, false)) {
        return false;
    }
    initialized_ = true;
    return true;
}

bool FakeBlePeripheral::deinit()
{
    std::lock_guard<std::mutex> lock(mutex_);
    counters_.deinit++;
    if (started_ || advertising_ || !connections_.empty()) {
        return false;
    }
    initialized_ = false;
    configured_ = false;
    subscriptions_.clear();
    return true;
}

bool FakeBlePeripheral::start()
{
    std::lock_guard<std::mutex> lock(mutex_);
    counters_.start++;
    if (!initialized_ || started_) {
        return started_;
    }
    if (std::exchange(fail_start_, false)) {
        return false;
    }
    started_ = true;
    return true;
}

bool FakeBlePeripheral::stop()
{
    std::lock_guard<std::mutex> lock(mutex_);
    counters_.stop++;
    if (advertising_ || !connections_.empty()) {
        return false;
    }
    started_ = false;
    return true;
}

bool FakeBlePeripheral::start_advertising()
{
    Callbacks callbacks;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        counters_.start_advertising++;
        if (!started_ || !connections_.empty()) {
            return false;
        }
        if (advertising_) {
            return true;
        }
        if (std::exchange(fail_advertising_start_, false)) {
            return false;
        }
        if (std::exchange(defer_advertising_start_, false)) {
            return true;
        }
        advertising_ = true;
        callbacks = callbacks_;
    }
    if (callbacks.on_advertising_state_changed) {
        callbacks.on_advertising_state_changed(true);
    }
    return true;
}

bool FakeBlePeripheral::stop_advertising()
{
    Callbacks callbacks;
    bool changed = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        counters_.stop_advertising++;
        defer_advertising_start_ = false;
        changed = advertising_;
        advertising_ = false;
        callbacks = callbacks_;
    }
    if (changed && callbacks.on_advertising_state_changed) {
        callbacks.on_advertising_state_changed(false);
    }
    return true;
}

std::vector<hal::bluetooth::ble::ConnectionInfo> FakeBlePeripheral::get_connections() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return connections_;
}

bool FakeBlePeripheral::is_subscribed(
    uint16_t connection_id, const hal::bluetooth::ble::CharacteristicId &characteristic
) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return std::ranges::any_of(subscriptions_, [&](const auto & subscription) {
        return (subscription.first == connection_id) && (subscription.second == characteristic);
    });
}

bool FakeBlePeripheral::notify(
    uint16_t connection_id, const hal::bluetooth::ble::CharacteristicId &characteristic,
    const hal::bluetooth::ble::ByteArray &data
)
{
    std::lock_guard<std::mutex> lock(mutex_);
    counters_.notify++;
    auto connection = find_connection(connection_id);
    if (!connection || !characteristic_has_property(characteristic, &hal::bluetooth::ble::CharacteristicConfig::notify)) {
        return false;
    }
    if (!std::ranges::any_of(subscriptions_, [&](const auto & subscription) {
    return (subscription.first == connection_id) && (subscription.second == characteristic);
    })) {
        return false;
    }
    if (data.size() > static_cast<size_t>(connection->mtu - 3)) {
        return false;
    }
    notifications_.push_back({
        .connection_id = connection_id,
        .characteristic = characteristic,
        .data = data,
    });
    return true;
}

bool FakeBlePeripheral::disconnect(uint16_t connection_id)
{
    Callbacks callbacks;
    hal::bluetooth::ble::ConnectionInfo connection;
    bool should_restart_advertising = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        counters_.disconnect++;
        const auto it = std::ranges::find_if(connections_, [&](const auto & item) {
            return item.connection_id == connection_id;
        });
        if (it == connections_.end()) {
            return false;
        }
        connection = *it;
        connections_.erase(it);
        std::erase_if(subscriptions_, [&](const auto & subscription) {
            return subscription.first == connection_id;
        });
        should_restart_advertising = config_.auto_restart_advertising && started_;
        advertising_ = should_restart_advertising;
        callbacks = callbacks_;
    }

    if (callbacks.on_connection_state_changed) {
        callbacks.on_connection_state_changed(connection, false, "requested");
    }
    if (should_restart_advertising && callbacks.on_advertising_state_changed) {
        callbacks.on_advertising_state_changed(true);
    }
    return true;
}

bool FakeBlePeripheral::inject_connect(uint16_t connection_id, std::string peer_address, uint16_t mtu)
{
    Callbacks callbacks;
    hal::bluetooth::ble::ConnectionInfo connection{
        .connection_id = connection_id,
        .peer_address = std::move(peer_address),
        .mtu = mtu,
    };
    bool stopped_advertising = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!started_ || !connections_.empty() || (mtu < hal::bluetooth::ble::ATT_MTU_MIN) ||
                (mtu > hal::bluetooth::ble::ATT_MTU_MAX)) {
            return false;
        }
        stopped_advertising = advertising_;
        advertising_ = false;
        connections_.push_back(connection);
        callbacks = callbacks_;
    }
    if (stopped_advertising && callbacks.on_advertising_state_changed) {
        callbacks.on_advertising_state_changed(false);
    }
    if (callbacks.on_connection_state_changed) {
        callbacks.on_connection_state_changed(connection, true, "connected");
    }
    return true;
}

bool FakeBlePeripheral::inject_mtu(uint16_t connection_id, uint16_t mtu)
{
    Callbacks callbacks;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = std::ranges::find_if(connections_, [&](const auto & connection) {
            return connection.connection_id == connection_id;
        });
        if ((it == connections_.end()) || (mtu < hal::bluetooth::ble::ATT_MTU_MIN) || (mtu > hal::bluetooth::ble::ATT_MTU_MAX)) {
            return false;
        }
        it->mtu = mtu;
        callbacks = callbacks_;
    }
    if (callbacks.on_mtu_changed) {
        callbacks.on_mtu_changed(connection_id, mtu);
    }
    return true;
}

bool FakeBlePeripheral::inject_subscription(
    uint16_t connection_id, const hal::bluetooth::ble::CharacteristicId &characteristic, bool enabled
)
{
    Callbacks callbacks;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!find_connection(connection_id) ||
                !characteristic_has_property(characteristic, &hal::bluetooth::ble::CharacteristicConfig::notify)) {
            return false;
        }
        const auto it = std::ranges::find_if(subscriptions_, [&](const auto & subscription) {
            return (subscription.first == connection_id) && (subscription.second == characteristic);
        });
        if (enabled && (it == subscriptions_.end())) {
            subscriptions_.emplace_back(connection_id, characteristic);
        } else if (!enabled && (it != subscriptions_.end())) {
            subscriptions_.erase(it);
        }
        callbacks = callbacks_;
    }
    if (callbacks.on_subscription_changed) {
        callbacks.on_subscription_changed(connection_id, characteristic, enabled);
    }
    return true;
}

bool FakeBlePeripheral::inject_write(
    uint16_t connection_id, const hal::bluetooth::ble::CharacteristicId &characteristic,
    hal::bluetooth::ble::ByteArray data
)
{
    Callbacks callbacks;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!find_connection(connection_id) ||
                (!characteristic_has_property(characteristic, &hal::bluetooth::ble::CharacteristicConfig::write) &&
                 !characteristic_has_property(
                     characteristic, &hal::bluetooth::ble::CharacteristicConfig::write_without_response
                 ))) {
            return false;
        }
        callbacks = callbacks_;
    }
    if (callbacks.on_characteristic_written) {
        callbacks.on_characteristic_written({
            .connection_id = connection_id,
            .characteristic = characteristic,
            .data = std::move(data),
        });
    }
    return true;
}

void FakeBlePeripheral::emit_late_write(const hal::bluetooth::ble::WriteEvent &event)
{
    Callbacks callbacks;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        callbacks = cleared_callbacks_;
    }
    if (callbacks.on_characteristic_written) {
        callbacks.on_characteristic_written(event);
    }
}

void FakeBlePeripheral::fail_next_init()
{
    std::lock_guard<std::mutex> lock(mutex_);
    fail_init_ = true;
}

void FakeBlePeripheral::fail_next_start()
{
    std::lock_guard<std::mutex> lock(mutex_);
    fail_start_ = true;
}

void FakeBlePeripheral::fail_next_advertising_start()
{
    std::lock_guard<std::mutex> lock(mutex_);
    fail_advertising_start_ = true;
}

void FakeBlePeripheral::defer_next_advertising_start()
{
    std::lock_guard<std::mutex> lock(mutex_);
    defer_advertising_start_ = true;
}

FakeBlePeripheral::Counters FakeBlePeripheral::get_counters() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return counters_;
}

std::vector<FakeBlePeripheral::Notification> FakeBlePeripheral::get_notifications() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return notifications_;
}

bool FakeBlePeripheral::is_advertising() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return advertising_;
}

bool FakeBlePeripheral::has_callbacks() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return static_cast<bool>(callbacks_.on_characteristic_written) ||
           static_cast<bool>(callbacks_.on_connection_state_changed) ||
           static_cast<bool>(callbacks_.on_error);
}

bool FakeBlePeripheral::characteristic_has_property(
    const hal::bluetooth::ble::CharacteristicId &characteristic,
    bool hal::bluetooth::ble::CharacteristicConfig::*property
) const
{
    for (const auto &service : config_.services) {
        if (service.uuid != characteristic.service_uuid) {
            continue;
        }
        for (const auto &candidate : service.characteristics) {
            if (candidate.uuid == characteristic.characteristic_uuid) {
                return candidate.*property;
            }
        }
    }
    return false;
}

std::optional<hal::bluetooth::ble::ConnectionInfo> FakeBlePeripheral::find_connection(uint16_t connection_id) const
{
    const auto it = std::ranges::find_if(connections_, [&](const auto & connection) {
        return connection.connection_id == connection_id;
    });
    if (it == connections_.end()) {
        return std::nullopt;
    }
    return *it;
}

bool FakeBleDevice::probe()
{
    return true;
}

std::vector<hal::InterfaceSpec> FakeBleDevice::get_interface_specs() const
{
    return {{hal::bluetooth::ble::PeripheralIface::NAME, FakeBlePeripheral::INSTANCE_NAME}};
}

bool FakeBleDevice::on_init()
{
    auto interface = std::make_shared<FakeBlePeripheral>();
    interfaces_.emplace(FakeBlePeripheral::INSTANCE_NAME, interface);
    std::lock_guard<std::mutex> lock(fake_peripheral_mutex);
    fake_peripheral = interface;
    return true;
}

void FakeBleDevice::on_deinit()
{
    interfaces_.clear();
    std::lock_guard<std::mutex> lock(fake_peripheral_mutex);
    fake_peripheral.reset();
}

std::shared_ptr<FakeBlePeripheral> get_fake_ble_peripheral()
{
    std::lock_guard<std::mutex> lock(fake_peripheral_mutex);
    return fake_peripheral.lock();
}

} // namespace esp_brookesia::test

using TestBleDevicePlugin = esp_brookesia::test::FakeBleDevice;

BROOKESIA_PLUGIN_REGISTER(
    esp_brookesia::hal::Device, TestBleDevicePlugin, std::string(TestBleDevicePlugin::NAME)
);
