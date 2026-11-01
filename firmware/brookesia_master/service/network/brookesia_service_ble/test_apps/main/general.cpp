/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */
#include "general.hpp"

#include <algorithm>

namespace esp_brookesia::test {

BleHelper::PeripheralConfig make_test_config()
{
    return {
        .device_name = "Brookesia-BLE-Test",
        .preferred_mtu = 247,
        .max_connections = 1,
        .auto_restart_advertising = true,
        .advertised_service_uuids = {std::string(SERVICE_UUID)},
        .services = {{
                .uuid = std::string(SERVICE_UUID),
                .characteristics = {
                    {
                        .uuid = std::string(RX_UUID),
                        .write = true,
                        .write_without_response = true,
                        .notify = false,
                    },
                    {
                        .uuid = std::string(TX_UUID),
                        .write = false,
                        .write_without_response = false,
                        .notify = true,
                    },
                },
            }
        },
    };
}

hal::bluetooth::ble::CharacteristicId make_rx_characteristic()
{
    return {
        .service_uuid = std::string(SERVICE_UUID),
        .characteristic_uuid = std::string(RX_UUID),
    };
}

hal::bluetooth::ble::CharacteristicId make_tx_characteristic()
{
    return {
        .service_uuid = std::string(SERVICE_UUID),
        .characteristic_uuid = std::string(TX_UUID),
    };
}

bool start_ble_service(
    service::ServiceBinding &binding, std::shared_ptr<FakeBlePeripheral> &peripheral
)
{
    auto &manager = service::ServiceManager::get_instance();
    if (!manager.init() || !manager.start()) {
        return false;
    }
    if (!service::ServiceRegistry::has_plugin(std::string(BleHelper::get_name()))) {
        return false;
    }
    binding = manager.bind(BleHelper::get_name().data());
    if (!binding.is_valid()) {
        return false;
    }
    peripheral = get_fake_ble_peripheral();
    return static_cast<bool>(peripheral);
}

void stop_ble_service(service::ServiceBinding &binding)
{
    binding.release();
    auto &manager = service::ServiceManager::get_instance();
    manager.stop();
    manager.deinit();
}

bool configure_and_start_advertising()
{
    auto result = BleHelper::call_function_sync(
                      BleHelper::FunctionId::SetPeripheralConfig,
                      BROOKESIA_DESCRIBE_TO_JSON(make_test_config()).as_object()
                  );
    if (!result) {
        return false;
    }
    return static_cast<bool>(
               BleHelper::call_function_sync(BleHelper::FunctionId::TriggerAdvertisingStart)
           );
}

bool BleEventCollector::start()
{
    connections_.clear();
    connections_.push_back(BleHelper::subscribe_event(
                               BleHelper::EventId::AdvertisingStateChanged,
    [this](const std::string &, bool is_advertising) {
        std::lock_guard<std::mutex> lock(mutex_);
        advertising_states_.push_back(is_advertising);
        cv_.notify_all();
    }
                           ));
    connections_.push_back(BleHelper::subscribe_event(
                               BleHelper::EventId::ConnectionStateChanged,
    [this](const std::string &, const boost::json::object &, bool is_connected, const std::string &) {
        std::lock_guard<std::mutex> lock(mutex_);
        connection_states_.push_back(is_connected);
        cv_.notify_all();
    }
                           ));
    connections_.push_back(BleHelper::subscribe_event(
                               BleHelper::EventId::SubscriptionChanged,
    [this](const std::string &, double, const std::string &, const std::string &, bool enabled) {
        std::lock_guard<std::mutex> lock(mutex_);
        subscription_states_.push_back(enabled);
        cv_.notify_all();
    }
                           ));
    connections_.push_back(BleHelper::subscribe_event(
                               BleHelper::EventId::MtuChanged,
    [this](const std::string &, double, double mtu) {
        std::lock_guard<std::mutex> lock(mutex_);
        mtus_.push_back(static_cast<uint16_t>(mtu));
        cv_.notify_all();
    }
                           ));
    connections_.push_back(BleHelper::subscribe_event(
                               BleHelper::EventId::CharacteristicWritten,
                               [this](
                                   const std::string &, double, const std::string & service_uuid,
                                   const std::string & characteristic_uuid, const boost::json::array & data
    ) {
        std::lock_guard<std::mutex> lock(mutex_);
        write_count_++;
        last_write_service_uuid_ = service_uuid;
        last_write_characteristic_uuid_ = characteristic_uuid;
        last_write_data_ = data;
        cv_.notify_all();
    }
                           ));
    connections_.push_back(BleHelper::subscribe_event(
                               BleHelper::EventId::ErrorHappened,
    [this](const std::string &, const std::string &, double, const std::string &) {
        std::lock_guard<std::mutex> lock(mutex_);
        error_count_++;
        cv_.notify_all();
    }
                           ));

    return std::ranges::all_of(connections_, [](const auto & connection) {
        return connection.connected();
    });
}

bool BleEventCollector::wait_for_write_count(size_t count, uint32_t timeout_ms)
{
    return wait_until([this, count]() {
        return write_count_ >= count;
    }, timeout_ms);
}

bool BleEventCollector::wait_for_advertising(bool expected, uint32_t timeout_ms)
{
    return wait_until([this, expected]() {
        return std::ranges::find(advertising_states_, expected) != advertising_states_.end();
    }, timeout_ms);
}

bool BleEventCollector::wait_for_connection(bool expected, uint32_t timeout_ms)
{
    return wait_until([this, expected]() {
        return std::ranges::find(connection_states_, expected) != connection_states_.end();
    }, timeout_ms);
}

bool BleEventCollector::wait_for_subscription(bool expected, uint32_t timeout_ms)
{
    return wait_until([this, expected]() {
        return std::ranges::find(subscription_states_, expected) != subscription_states_.end();
    }, timeout_ms);
}

bool BleEventCollector::wait_for_mtu(uint16_t expected, uint32_t timeout_ms)
{
    return wait_until([this, expected]() {
        return std::ranges::find(mtus_, expected) != mtus_.end();
    }, timeout_ms);
}

bool BleEventCollector::wait_for_error_count(size_t count, uint32_t timeout_ms)
{
    return wait_until([this, count]() {
        return error_count_ >= count;
    }, timeout_ms);
}

size_t BleEventCollector::write_count() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return write_count_;
}

boost::json::array BleEventCollector::last_write_data() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return last_write_data_;
}

std::string BleEventCollector::last_write_service_uuid() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return last_write_service_uuid_;
}

std::string BleEventCollector::last_write_characteristic_uuid() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return last_write_characteristic_uuid_;
}

} // namespace esp_brookesia::test
