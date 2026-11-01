/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */
#include "general.hpp"

using namespace esp_brookesia;
using namespace esp_brookesia::test;

BROOKESIA_TEST_CASE(
    lazy_host_and_restart_lifecycle, "Test ServiceBle - lazy host and restart lifecycle",
    "[service][ble][lifecycle]"
)
{
    service::ServiceBinding binding;
    std::shared_ptr<FakeBlePeripheral> peripheral;
    TEST_ASSERT_TRUE(start_ble_service(binding, peripheral));
    lib_utils::FunctionGuard cleanup([&]() {
        stop_ble_service(binding);
    });

    auto state_result = BleHelper::call_function_sync<boost::json::object>(BleHelper::FunctionId::GetState);
    TEST_ASSERT_TRUE(state_result.has_value());
    BleHelper::State state;
    TEST_ASSERT_TRUE(BROOKESIA_DESCRIBE_FROM_JSON(*state_result, state));
    TEST_ASSERT_EQUAL(
        BROOKESIA_DESCRIBE_ENUM_TO_NUM(BleHelper::GeneralState::Ready),
        BROOKESIA_DESCRIBE_ENUM_TO_NUM(state.general_state)
    );
    TEST_ASSERT_FALSE(state.is_configured);
    TEST_ASSERT_FALSE(state.is_advertising);

    auto set_result = BleHelper::call_function_sync(
                          BleHelper::FunctionId::SetPeripheralConfig,
                          BROOKESIA_DESCRIBE_TO_JSON(make_test_config()).as_object()
                      );
    TEST_ASSERT_TRUE(set_result.has_value());
    auto counters = peripheral->get_counters();
    TEST_ASSERT_EQUAL_size_t(0, counters.configure);
    TEST_ASSERT_EQUAL_size_t(0, counters.init);
    TEST_ASSERT_EQUAL_size_t(0, counters.start);

    auto get_config_result = BleHelper::call_function_sync<boost::json::object>(
                                 BleHelper::FunctionId::GetPeripheralConfig
                             );
    TEST_ASSERT_TRUE(get_config_result.has_value());
    BleHelper::PeripheralConfig returned_config;
    TEST_ASSERT_TRUE(BROOKESIA_DESCRIBE_FROM_JSON(*get_config_result, returned_config));
    TEST_ASSERT_TRUE(returned_config == make_test_config());

    TEST_ASSERT_TRUE(BleHelper::call_function_sync(BleHelper::FunctionId::TriggerAdvertisingStart).has_value());
    TEST_ASSERT_TRUE(peripheral->is_advertising());
    counters = peripheral->get_counters();
    TEST_ASSERT_EQUAL_size_t(1, counters.configure);
    TEST_ASSERT_EQUAL_size_t(1, counters.init);
    TEST_ASSERT_EQUAL_size_t(1, counters.start);
    TEST_ASSERT_EQUAL_size_t(1, counters.start_advertising);

    TEST_ASSERT_TRUE(BleHelper::call_function_sync(BleHelper::FunctionId::TriggerAdvertisingStart).has_value());
    TEST_ASSERT_EQUAL_size_t(1, peripheral->get_counters().start_advertising);

    TEST_ASSERT_TRUE(BleHelper::call_function_sync(BleHelper::FunctionId::TriggerAdvertisingStop).has_value());
    TEST_ASSERT_FALSE(peripheral->is_advertising());
    TEST_ASSERT_TRUE(BleHelper::call_function_sync(BleHelper::FunctionId::TriggerAdvertisingStart).has_value());
    TEST_ASSERT_TRUE(peripheral->is_advertising());
    TEST_ASSERT_EQUAL_size_t(1, peripheral->get_counters().configure);
    TEST_ASSERT_EQUAL_size_t(2, peripheral->get_counters().start_advertising);

    binding.release();
    counters = peripheral->get_counters();
    TEST_ASSERT_FALSE(peripheral->is_advertising());
    TEST_ASSERT_FALSE(peripheral->has_callbacks());
    TEST_ASSERT_EQUAL_size_t(1, counters.clear_callbacks);
    TEST_ASSERT_EQUAL_size_t(1, counters.stop);
    TEST_ASSERT_EQUAL_size_t(1, counters.deinit);

    binding = service::ServiceManager::get_instance().bind(BleHelper::get_name().data());
    TEST_ASSERT_TRUE(binding.is_valid());
    TEST_ASSERT_TRUE(BleHelper::call_function_sync(BleHelper::FunctionId::TriggerAdvertisingStart).has_value());
    counters = peripheral->get_counters();
    TEST_ASSERT_EQUAL_size_t(2, counters.configure);
    TEST_ASSERT_EQUAL_size_t(2, counters.init);
    TEST_ASSERT_EQUAL_size_t(2, counters.start);
}

BROOKESIA_TEST_CASE(
    startup_failure_cleanup_and_retry, "Test ServiceBle - startup failure cleanup and retry",
    "[service][ble][lifecycle][failure]"
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
    TEST_ASSERT_TRUE(
        BleHelper::call_function_sync(
            BleHelper::FunctionId::SetPeripheralConfig,
            BROOKESIA_DESCRIBE_TO_JSON(make_test_config()).as_object()
        ).has_value()
    );

    peripheral->fail_next_init();
    auto first_start = BleHelper::call_function_sync(BleHelper::FunctionId::TriggerAdvertisingStart);
    TEST_ASSERT_FALSE(first_start.has_value());
    TEST_ASSERT_TRUE(collector.wait_for_error_count(1));
    auto counters = peripheral->get_counters();
    TEST_ASSERT_EQUAL_size_t(1, counters.configure);
    TEST_ASSERT_EQUAL_size_t(1, counters.init);
    TEST_ASSERT_EQUAL_size_t(0, counters.start);
    TEST_ASSERT_EQUAL_size_t(1, counters.clear_callbacks);
    TEST_ASSERT_FALSE(peripheral->has_callbacks());
    TEST_ASSERT_FALSE(peripheral->is_advertising());

    auto state_result = BleHelper::call_function_sync<boost::json::object>(BleHelper::FunctionId::GetState);
    TEST_ASSERT_TRUE(state_result.has_value());
    BleHelper::State state;
    TEST_ASSERT_TRUE(BROOKESIA_DESCRIBE_FROM_JSON(*state_result, state));
    TEST_ASSERT_EQUAL(
        BROOKESIA_DESCRIBE_ENUM_TO_NUM(BleHelper::GeneralState::Error),
        BROOKESIA_DESCRIBE_ENUM_TO_NUM(state.general_state)
    );

    TEST_ASSERT_TRUE(BleHelper::call_function_sync(BleHelper::FunctionId::TriggerAdvertisingStart).has_value());
    TEST_ASSERT_TRUE(peripheral->is_advertising());
    counters = peripheral->get_counters();
    TEST_ASSERT_EQUAL_size_t(2, counters.configure);
    TEST_ASSERT_EQUAL_size_t(2, counters.init);
    TEST_ASSERT_EQUAL_size_t(1, counters.start);

    TEST_ASSERT_TRUE(BleHelper::call_function_sync(BleHelper::FunctionId::TriggerAdvertisingStop).has_value());
    peripheral->fail_next_advertising_start();
    auto advertising_start = BleHelper::call_function_sync(BleHelper::FunctionId::TriggerAdvertisingStart);
    TEST_ASSERT_FALSE(advertising_start.has_value());
    TEST_ASSERT_TRUE(collector.wait_for_error_count(2));
    TEST_ASSERT_FALSE(peripheral->is_advertising());
    TEST_ASSERT_FALSE(peripheral->has_callbacks());

    counters = peripheral->get_counters();
    TEST_ASSERT_EQUAL_size_t(2, counters.clear_callbacks);
    TEST_ASSERT_EQUAL_size_t(1, counters.stop);
    TEST_ASSERT_EQUAL_size_t(1, counters.deinit);

    state_result = BleHelper::call_function_sync<boost::json::object>(BleHelper::FunctionId::GetState);
    TEST_ASSERT_TRUE(state_result.has_value());
    TEST_ASSERT_TRUE(BROOKESIA_DESCRIBE_FROM_JSON(*state_result, state));
    TEST_ASSERT_EQUAL(
        BROOKESIA_DESCRIBE_ENUM_TO_NUM(BleHelper::GeneralState::Error),
        BROOKESIA_DESCRIBE_ENUM_TO_NUM(state.general_state)
    );
    TEST_ASSERT_FALSE(state.is_advertising);

    TEST_ASSERT_TRUE(BleHelper::call_function_sync(BleHelper::FunctionId::TriggerAdvertisingStart).has_value());
    TEST_ASSERT_TRUE(peripheral->is_advertising());

    TEST_ASSERT_TRUE(BleHelper::call_function_sync(BleHelper::FunctionId::TriggerAdvertisingStop).has_value());
    peripheral->defer_next_advertising_start();
    const auto start_count = peripheral->get_counters().start_advertising;
    TEST_ASSERT_TRUE(BleHelper::call_function_sync(BleHelper::FunctionId::TriggerAdvertisingStart).has_value());
    TEST_ASSERT_FALSE(peripheral->is_advertising());
    TEST_ASSERT_TRUE(BleHelper::call_function_sync(BleHelper::FunctionId::TriggerAdvertisingStart).has_value());
    TEST_ASSERT_EQUAL_size_t(start_count + 1, peripheral->get_counters().start_advertising);
    TEST_ASSERT_TRUE(BleHelper::call_function_sync(BleHelper::FunctionId::TriggerAdvertisingStop).has_value());
    TEST_ASSERT_FALSE(peripheral->is_advertising());
}
