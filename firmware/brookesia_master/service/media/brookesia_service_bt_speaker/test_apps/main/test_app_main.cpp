/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */
#if defined(ESP_PLATFORM)

#include "unity.h"

#include "brookesia/service_helper/framework/manager.hpp"
#include "brookesia/service_helper/media/bt_speaker.hpp"
#include "brookesia/service_helper/network/bt.hpp"
#include "brookesia/service_manager.hpp"

namespace {

using BtHelper = esp_brookesia::service::helper::Bt;
using Helper = esp_brookesia::service::helper::BtSpeaker;
using ManagerHelper = esp_brookesia::service::helper::Manager;
using esp_brookesia::service::ServiceManager;

bool get_bt_state(BtHelper::State &state)
{
    const auto result = BtHelper::call_function_sync<boost::json::object>(BtHelper::FunctionId::GetState);
    if (!result) {
        return false;
    }
    return BROOKESIA_DESCRIBE_FROM_JSON(*result, state);
}

bool get_speaker_state(Helper::State &state)
{
    const auto result = Helper::call_function_sync<boost::json::object>(Helper::FunctionId::GetState);
    if (!result) {
        return false;
    }
    return BROOKESIA_DESCRIBE_FROM_JSON(*result, state);
}

} // namespace

TEST_CASE("BtSpeaker starts through the real Bt service", "[service][bt_speaker]")
{
    TEST_ASSERT_EQUAL_STRING("BtSpeaker", Helper::get_name().data());
    TEST_ASSERT_TRUE(esp_brookesia::service::ServiceRegistry::has_plugin("Bt"));
    TEST_ASSERT_TRUE(esp_brookesia::service::ServiceRegistry::has_plugin("BtSpeaker"));

    auto &manager = ServiceManager::get_instance();
    TEST_ASSERT_TRUE(manager.init());
    TEST_ASSERT_TRUE(manager.start());
    auto speaker_binding = manager.bind(Helper::get_name().data());
    TEST_ASSERT_TRUE(speaker_binding.is_valid());

    const auto info = ManagerHelper::get_service_info(Helper::get_name().data(), 1000);
    TEST_ASSERT_TRUE(info.has_value());
    TEST_ASSERT_EQUAL_STRING("BtSpeaker", info->name.c_str());
    const auto schema = ManagerHelper::get_service_schema(Helper::get_name().data(), 1000);
    TEST_ASSERT_TRUE(schema.has_value());
    TEST_ASSERT_EQUAL_UINT(12, schema->function_names.size());
    TEST_ASSERT_EQUAL_UINT(7, schema->event_names.size());

    const auto capabilities_result = BtHelper::call_function_sync<boost::json::object>(
                                         BtHelper::FunctionId::GetCapabilities
                                     );
    TEST_ASSERT_TRUE(capabilities_result.has_value());
    BtHelper::Capabilities capabilities;
    TEST_ASSERT_TRUE(BROOKESIA_DESCRIBE_FROM_JSON(*capabilities_result, capabilities));

    const Helper::Config config{
        .device = {
            .device_name = "ESP-Brookesia-BtSpeaker-Test",
            .discoverable = true,
            .connectable = true,
        },
        .stop_local_playback_on_connect = true,
    };
    const auto set_config_result = Helper::call_function_sync(
                                       Helper::FunctionId::SetConfig,
                                       BROOKESIA_DESCRIBE_TO_JSON(config).as_object()
                                   );
    TEST_ASSERT_TRUE(set_config_result.has_value());

    const auto start_result = Helper::call_function_sync(Helper::FunctionId::Start);
    Helper::State speaker_state;
    BtHelper::State bt_state;
    TEST_ASSERT_TRUE(get_speaker_state(speaker_state));
    TEST_ASSERT_TRUE(get_bt_state(bt_state));
    if (capabilities.classic_supported) {
        TEST_ASSERT_TRUE(start_result.has_value());
        TEST_ASSERT_EQUAL(
            static_cast<int>(Helper::GeneralState::Started), static_cast<int>(speaker_state.general_state)
        );
        TEST_ASSERT_TRUE(speaker_state.is_started);
        TEST_ASSERT_TRUE(bt_state.is_started);
    } else {
        TEST_ASSERT_FALSE(start_result.has_value());
        TEST_ASSERT_FALSE(start_result.error().empty());
        TEST_ASSERT_EQUAL(
            static_cast<int>(Helper::GeneralState::Error), static_cast<int>(speaker_state.general_state)
        );
        TEST_ASSERT_FALSE(speaker_state.is_supported);
        TEST_ASSERT_FALSE(speaker_state.is_started);
        TEST_ASSERT_FALSE(bt_state.is_started);
    }
    TEST_ASSERT_FALSE(speaker_state.is_connected);
    TEST_ASSERT_FALSE(speaker_state.is_music_active);

    const auto stop_result = Helper::call_function_sync(Helper::FunctionId::Stop);
    TEST_ASSERT_TRUE(stop_result.has_value());
    TEST_ASSERT_TRUE(get_speaker_state(speaker_state));
    TEST_ASSERT_TRUE(get_bt_state(bt_state));
    TEST_ASSERT_EQUAL(
        static_cast<int>(Helper::GeneralState::Ready), static_cast<int>(speaker_state.general_state)
    );
    TEST_ASSERT_FALSE(bt_state.is_started);

    speaker_binding.release();
    manager.stop();
    manager.deinit();
}

extern "C" void app_main()
{
    unity_run_menu();
}

#endif // ESP_PLATFORM
