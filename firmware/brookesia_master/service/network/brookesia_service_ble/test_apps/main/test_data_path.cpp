/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */
#include "general.hpp"

using namespace esp_brookesia;
using namespace esp_brookesia::test;

BROOKESIA_TEST_CASE(
    callback_and_notification_data_path, "Test ServiceBle - callback and notification data path",
    "[service][ble][data]"
)
{
    service::ServiceBinding binding;
    std::shared_ptr<FakeBlePeripheral> peripheral;
    TEST_ASSERT_TRUE(start_ble_service(binding, peripheral));
    lib_utils::FunctionGuard cleanup([&]() {
        stop_ble_service(binding);
    });

    BleEventCollector collector;
    TEST_ASSERT_TRUE(collector.start());
    TEST_ASSERT_TRUE(configure_and_start_advertising());
    TEST_ASSERT_TRUE(collector.wait_for_advertising(true));

    TEST_ASSERT_TRUE(peripheral->inject_connect(CONNECTION_ID, "01:23:45:67:89:ab", 247));
    TEST_ASSERT_TRUE(collector.wait_for_advertising(false));
    TEST_ASSERT_TRUE(collector.wait_for_connection(true));
    TEST_ASSERT_FALSE(peripheral->is_advertising());

    TEST_ASSERT_TRUE(peripheral->inject_mtu(CONNECTION_ID, 100));
    TEST_ASSERT_TRUE(collector.wait_for_mtu(100));

    boost::json::array binary_data = {0, 127, 128, 255};
    auto unsubscribed = BleHelper::call_function_sync(
                            BleHelper::FunctionId::Notify, static_cast<double>(CONNECTION_ID),
                            std::string(SERVICE_UUID), std::string(TX_UUID), binary_data
                        );
    TEST_ASSERT_FALSE(unsubscribed.has_value());

    const auto tx = make_tx_characteristic();
    TEST_ASSERT_TRUE(peripheral->inject_subscription(CONNECTION_ID, tx, true));
    TEST_ASSERT_TRUE(collector.wait_for_subscription(true));

    auto notify_result = BleHelper::call_function_sync(
                             BleHelper::FunctionId::Notify, static_cast<double>(CONNECTION_ID),
                             std::string(SERVICE_UUID), std::string(TX_UUID), binary_data
                         );
    TEST_ASSERT_TRUE(notify_result.has_value());
    const auto notifications = peripheral->get_notifications();
    TEST_ASSERT_EQUAL_size_t(1, notifications.size());
    TEST_ASSERT_EQUAL_UINT16(CONNECTION_ID, notifications.front().connection_id);
    TEST_ASSERT_TRUE(notifications.front().characteristic == tx);
    TEST_ASSERT_EQUAL_size_t(4, notifications.front().data.size());
    TEST_ASSERT_EQUAL_UINT8(0, notifications.front().data[0]);
    TEST_ASSERT_EQUAL_UINT8(255, notifications.front().data[3]);

    boost::json::array oversized_data;
    for (size_t index = 0; index < 98; ++index) {
        oversized_data.emplace_back(0);
    }
    TEST_ASSERT_FALSE(
        BleHelper::call_function_sync(
            BleHelper::FunctionId::Notify, static_cast<double>(CONNECTION_ID),
            std::string(SERVICE_UUID), std::string(TX_UUID), oversized_data
        ).has_value()
    );
    TEST_ASSERT_FALSE(
        BleHelper::call_function_sync(
            BleHelper::FunctionId::Notify, static_cast<double>(CONNECTION_ID),
            std::string(SERVICE_UUID), std::string(RX_UUID), binary_data
        ).has_value()
    );
    TEST_ASSERT_FALSE(
        BleHelper::call_function_sync(
            BleHelper::FunctionId::Notify, static_cast<double>(CONNECTION_ID),
            std::string(SERVICE_UUID), std::string(TX_UUID), boost::json::array{256}
        ).has_value()
    );
    TEST_ASSERT_FALSE(
        BleHelper::call_function_sync(
            BleHelper::FunctionId::Notify, static_cast<double>(CONNECTION_ID),
            std::string(SERVICE_UUID), std::string(TX_UUID), boost::json::array{1.5}
        ).has_value()
    );

    const hal::bluetooth::ble::ByteArray written_data = {0x00, 0x80, 0xff, 0x41};
    TEST_ASSERT_TRUE(peripheral->inject_write(CONNECTION_ID, make_rx_characteristic(), written_data));
    TEST_ASSERT_TRUE(collector.wait_for_write_count(1));
    TEST_ASSERT_EQUAL_STRING(SERVICE_UUID.data(), collector.last_write_service_uuid().c_str());
    TEST_ASSERT_EQUAL_STRING(RX_UUID.data(), collector.last_write_characteristic_uuid().c_str());
    const auto event_data = collector.last_write_data();
    TEST_ASSERT_EQUAL_size_t(written_data.size(), event_data.size());
    TEST_ASSERT_EQUAL_UINT8(0x00, event_data[0].as_uint64());
    TEST_ASSERT_EQUAL_UINT8(0x80, event_data[1].as_uint64());
    TEST_ASSERT_EQUAL_UINT8(0xff, event_data[2].as_uint64());

    auto connections_result = BleHelper::call_function_sync<boost::json::array>(
                                  BleHelper::FunctionId::GetConnections
                              );
    TEST_ASSERT_TRUE(connections_result.has_value());
    std::vector<hal::bluetooth::ble::ConnectionInfo> connections;
    TEST_ASSERT_TRUE(BROOKESIA_DESCRIBE_FROM_JSON(*connections_result, connections));
    TEST_ASSERT_EQUAL_size_t(1, connections.size());
    TEST_ASSERT_EQUAL_UINT16(100, connections.front().mtu);

    TEST_ASSERT_TRUE(
        BleHelper::call_function_sync(
            BleHelper::FunctionId::Disconnect, static_cast<double>(CONNECTION_ID)
        ).has_value()
    );
    TEST_ASSERT_TRUE(collector.wait_for_connection(false));
    TEST_ASSERT_TRUE(peripheral->is_advertising());

    auto busy_config = make_test_config();
    TEST_ASSERT_TRUE(peripheral->inject_connect(CONNECTION_ID, "01:23:45:67:89:ab", 100));
    TEST_ASSERT_FALSE(
        BleHelper::call_function_sync(
            BleHelper::FunctionId::SetPeripheralConfig,
            BROOKESIA_DESCRIBE_TO_JSON(busy_config).as_object()
        ).has_value()
    );
}

BROOKESIA_TEST_CASE(
    late_callback_is_dropped, "Test ServiceBle - late callback is dropped",
    "[service][ble][lifecycle][callback]"
)
{
    service::ServiceBinding binding;
    std::shared_ptr<FakeBlePeripheral> peripheral;
    TEST_ASSERT_TRUE(start_ble_service(binding, peripheral));
    lib_utils::FunctionGuard cleanup([&]() {
        stop_ble_service(binding);
    });

    BleEventCollector collector;
    TEST_ASSERT_TRUE(collector.start());
    TEST_ASSERT_TRUE(configure_and_start_advertising());

    binding.release();
    TEST_ASSERT_FALSE(peripheral->has_callbacks());
    const auto before = collector.write_count();
    peripheral->emit_late_write({
        .connection_id = CONNECTION_ID,
        .characteristic = make_rx_characteristic(),
        .data = {0x01, 0x02, 0x03},
    });
    lib_utils::test_adapter::delay_ms(100);
    TEST_ASSERT_EQUAL_size_t(before, collector.write_count());
}
