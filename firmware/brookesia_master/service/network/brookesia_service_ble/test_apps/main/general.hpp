/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */
#pragma once

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include "boost/json.hpp"
#include "brookesia/lib_utils.hpp"
#include "brookesia/lib_utils/test_adapter.hpp"
#include "brookesia/service_ble.hpp"
#include "brookesia/service_helper/network/ble.hpp"
#include "brookesia/service_manager.hpp"
#include "fake_ble_hal.hpp"

namespace esp_brookesia::test {

using BleHelper = service::helper::Ble;

inline constexpr std::string_view SERVICE_UUID = "7a5a0001-9b7b-4d20-8f30-6d9f0e7f4a10";
inline constexpr std::string_view RX_UUID = "7a5a0002-9b7b-4d20-8f30-6d9f0e7f4a10";
inline constexpr std::string_view TX_UUID = "7a5a0003-9b7b-4d20-8f30-6d9f0e7f4a10";
inline constexpr uint16_t CONNECTION_ID = 7;
inline constexpr uint32_t EVENT_TIMEOUT_MS = 3000;

BleHelper::PeripheralConfig make_test_config();
hal::bluetooth::ble::CharacteristicId make_rx_characteristic();
hal::bluetooth::ble::CharacteristicId make_tx_characteristic();

bool start_ble_service(
    service::ServiceBinding &binding, std::shared_ptr<FakeBlePeripheral> &peripheral
);
void stop_ble_service(service::ServiceBinding &binding);
bool configure_and_start_advertising();

class BleEventCollector {
public:
    bool start();
    bool wait_for_write_count(size_t count, uint32_t timeout_ms = EVENT_TIMEOUT_MS);
    bool wait_for_advertising(bool expected, uint32_t timeout_ms = EVENT_TIMEOUT_MS);
    bool wait_for_connection(bool expected, uint32_t timeout_ms = EVENT_TIMEOUT_MS);
    bool wait_for_subscription(bool expected, uint32_t timeout_ms = EVENT_TIMEOUT_MS);
    bool wait_for_mtu(uint16_t expected, uint32_t timeout_ms = EVENT_TIMEOUT_MS);
    bool wait_for_error_count(size_t count, uint32_t timeout_ms = EVENT_TIMEOUT_MS);

    size_t write_count() const;
    boost::json::array last_write_data() const;
    std::string last_write_service_uuid() const;
    std::string last_write_characteristic_uuid() const;

private:
    template <typename Predicate>
    bool wait_until(Predicate predicate, uint32_t timeout_ms)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        return cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms), predicate);
    }

    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::vector<bool> advertising_states_;
    std::vector<bool> connection_states_;
    std::vector<bool> subscription_states_;
    std::vector<uint16_t> mtus_;
    size_t write_count_ = 0;
    size_t error_count_ = 0;
    boost::json::array last_write_data_;
    std::string last_write_service_uuid_;
    std::string last_write_characteristic_uuid_;
    std::vector<service::EventRegistry::SignalConnection> connections_;
};

} // namespace esp_brookesia::test
