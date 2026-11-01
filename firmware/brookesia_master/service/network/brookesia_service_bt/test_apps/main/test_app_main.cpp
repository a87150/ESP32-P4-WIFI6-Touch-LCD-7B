/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */
#if defined(ESP_PLATFORM)

#include "unity.h"

#include "brookesia/service_helper/framework/manager.hpp"
#include "brookesia/service_helper/network/bt.hpp"
#include "brookesia/service_manager.hpp"

namespace {

using Helper = esp_brookesia::service::helper::Bt;
using ManagerHelper = esp_brookesia::service::helper::Manager;
using esp_brookesia::service::ServiceManager;

bool get_state(Helper::State &state)
{
    const auto result = Helper::call_function_sync<boost::json::object>(Helper::FunctionId::GetState);
    if (!result) {
        return false;
    }
    return BROOKESIA_DESCRIBE_FROM_JSON(*result, state);
}

} // namespace

TEST_CASE("Bt starts with the platform Bluetooth service", "[service][bt]")
{
    TEST_ASSERT_EQUAL_STRING("Bt", Helper::get_name().data());
    TEST_ASSERT_TRUE(esp_brookesia::service::ServiceRegistry::has_plugin("Bt"));

    auto &manager = ServiceManager::get_instance();
    TEST_ASSERT_TRUE(manager.init());
    TEST_ASSERT_TRUE(manager.start());
    auto binding = manager.bind(Helper::get_name().data());
    TEST_ASSERT_TRUE(binding.is_valid());

    const auto info = ManagerHelper::get_service_info(Helper::get_name().data(), 1000);
    TEST_ASSERT_TRUE(info.has_value());
    TEST_ASSERT_EQUAL_STRING("Bt", info->name.c_str());
    const auto schema = ManagerHelper::get_service_schema(Helper::get_name().data(), 1000);
    TEST_ASSERT_TRUE(schema.has_value());
    TEST_ASSERT_EQUAL_UINT(17, schema->function_names.size());
    TEST_ASSERT_EQUAL_UINT(8, schema->event_names.size());

    const auto capabilities_result = Helper::call_function_sync<boost::json::object>(
                                         Helper::FunctionId::GetCapabilities
                                     );
    TEST_ASSERT_TRUE(capabilities_result.has_value());
    Helper::Capabilities capabilities;
    TEST_ASSERT_TRUE(BROOKESIA_DESCRIBE_FROM_JSON(*capabilities_result, capabilities));

    const Helper::DeviceConfig config{
        .device_name = "ESP-Brookesia-Bt-Test",
        .discoverable = true,
        .connectable = true,
    };
    const auto set_config_result = Helper::call_function_sync(
                                       Helper::FunctionId::SetDeviceConfig,
                                       BROOKESIA_DESCRIBE_TO_JSON(config).as_object()
                                   );
    TEST_ASSERT_TRUE(set_config_result.has_value());

    const auto start_result = Helper::call_function_sync(Helper::FunctionId::A2dpSinkStart);
    Helper::State state;
    TEST_ASSERT_TRUE(get_state(state));
    if (capabilities.classic_supported) {
        TEST_ASSERT_TRUE(start_result.has_value());
        TEST_ASSERT_EQUAL(static_cast<int>(Helper::HostState::Started), static_cast<int>(state.host_state));
        TEST_ASSERT_TRUE(state.is_started);
    } else {
        TEST_ASSERT_FALSE(start_result.has_value());
        TEST_ASSERT_FALSE(start_result.error().empty());
        TEST_ASSERT_EQUAL(static_cast<int>(Helper::HostState::Error), static_cast<int>(state.host_state));
        TEST_ASSERT_FALSE(state.is_started);
    }

    const auto connections_result = Helper::call_function_sync<boost::json::array>(
                                        Helper::FunctionId::GetConnections
                                    );
    TEST_ASSERT_TRUE(connections_result.has_value());
    TEST_ASSERT_TRUE(connections_result->empty());

    const auto stop_result = Helper::call_function_sync(Helper::FunctionId::A2dpSinkStop);
    TEST_ASSERT_TRUE(stop_result.has_value());
    TEST_ASSERT_TRUE(get_state(state));
    TEST_ASSERT_EQUAL(static_cast<int>(Helper::HostState::Idle), static_cast<int>(state.host_state));

    binding.release();
    manager.stop();
    manager.deinit();
}

extern "C" void app_main()
{
    unity_run_menu();
}

#endif // ESP_PLATFORM
