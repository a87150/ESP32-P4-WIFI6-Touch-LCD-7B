/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "boost/json.hpp"
#include "brookesia/lib_utils/describe_helpers.hpp"
#include "brookesia/lib_utils/test_adapter.hpp"
#include "brookesia/service_helper.hpp"

using namespace esp_brookesia::service;
using namespace esp_brookesia::service::helper;

namespace {

struct HelperSchemaStats {
    size_t helper_count = 0;
    size_t function_count = 0;
    size_t event_count = 0;
};

void assert_function_schemas(std::string_view helper_name, std::span<const FunctionSchema> schemas)
{
    TEST_ASSERT_FALSE_MESSAGE(helper_name.empty(), "helper name must not be empty");
    for (const auto &schema : schemas) {
        TEST_ASSERT_FALSE_MESSAGE(schema.name.empty(), "function schema name must not be empty");
        for (const auto &parameter : schema.parameters) {
            TEST_ASSERT_FALSE_MESSAGE(parameter.name.empty(), "function parameter name must not be empty");
        }
    }
}

void assert_event_schemas(std::span<const EventSchema> schemas)
{
    for (const auto &schema : schemas) {
        TEST_ASSERT_FALSE_MESSAGE(schema.name.empty(), "event schema name must not be empty");
        for (const auto &item : schema.items) {
            TEST_ASSERT_FALSE_MESSAGE(item.name.empty(), "event item name must not be empty");
        }
    }
}

template <typename Helper>
void accumulate_helper(HelperSchemaStats &stats)
{
    const auto name = Helper::get_name();
    const auto function_schemas = Helper::get_function_schemas();
    const auto event_schemas = Helper::get_event_schemas();
    assert_function_schemas(name, function_schemas);
    assert_event_schemas(event_schemas);
    stats.helper_count++;
    stats.function_count += function_schemas.size();
    stats.event_count += event_schemas.size();
}

boost::json::value serialize_and_parse(const auto &value)
{
    return BROOKESIA_DESCRIBE_TO_JSON(value);
}

} // namespace

BROOKESIA_TEST_CASE(
    test_service_helper_all_public_helpers_expose_valid_schemas,
    "Service helper schemas are generated for every public helper",
    "[service][helper][schema]"
)
{
    HelperSchemaStats stats;

    accumulate_helper<AudioPlayback>(stats);
    accumulate_helper<AudioEncoder<0>>(stats);
    accumulate_helper<AudioDecoder<0>>(stats);
    accumulate_helper<Device>(stats);
    accumulate_helper<Display>(stats);
    accumulate_helper<Ble>(stats);
    accumulate_helper<Bt>(stats);
    accumulate_helper<BtSpeaker>(stats);
    accumulate_helper<Http>(stats);
    accumulate_helper<Nes>(stats);
    accumulate_helper<SNTP>(stats);
    accumulate_helper<Storage>(stats);
    accumulate_helper<Wifi>(stats);
    accumulate_helper<ExpressionEmote>(stats);
    accumulate_helper<VideoEncoder<0>>(stats);
    accumulate_helper<VideoDecoder<0>>(stats);
    accumulate_helper<Manager>(stats);
    accumulate_helper<DataFlow>(stats);
    accumulate_helper<Utils>(stats);
    accumulate_helper<AgentManager>(stats);
    accumulate_helper<Coze>(stats);
    accumulate_helper<Openai>(stats);
    accumulate_helper<XiaoZhi>(stats);

    TEST_ASSERT_EQUAL_size_t(23, stats.helper_count);
    TEST_ASSERT_GREATER_THAN(60, stats.function_count);
    TEST_ASSERT_GREATER_THAN(20, stats.event_count);
}

BROOKESIA_TEST_CASE(
    test_service_helper_names_and_enum_descriptions_are_stable,
    "Service helper public names and described enums are stable",
    "[service][helper][describe]"
)
{
    TEST_ASSERT_EQUAL_STRING("AudioPlayback", AudioPlayback::get_name().data());
    TEST_ASSERT_EQUAL_STRING("AudioEncoder0", AudioEncoder<0>::get_name().data());
    TEST_ASSERT_EQUAL_STRING("AudioDecoder0", AudioDecoder<0>::get_name().data());
    TEST_ASSERT_EQUAL_STRING("VideoEncoder0", VideoEncoder<0>::get_name().data());
    TEST_ASSERT_EQUAL_STRING("VideoDecoder0", VideoDecoder<0>::get_name().data());
    TEST_ASSERT_EQUAL_STRING("Wifi", Wifi::get_name().data());
    TEST_ASSERT_EQUAL_STRING("Ble", Ble::get_name().data());
    TEST_ASSERT_EQUAL_STRING("Bt", Bt::get_name().data());
    TEST_ASSERT_EQUAL_STRING("BtSpeaker", BtSpeaker::get_name().data());
    TEST_ASSERT_EQUAL_STRING("Manager", Manager::get_name().data());
    TEST_ASSERT_EQUAL_STRING("DataFlow", DataFlow::get_name().data());
    TEST_ASSERT_EQUAL_STRING("Utils", Utils::get_name().data());
    TEST_ASSERT_EQUAL_STRING("AgentManager", AgentManager::get_name().data());
    TEST_ASSERT_EQUAL_STRING("Coze", Coze::get_name().data());
    TEST_ASSERT_EQUAL_STRING("OpenAI", Openai::get_name().data());
    TEST_ASSERT_EQUAL_STRING("XiaoZhi", XiaoZhi::get_name().data());

    TEST_ASSERT_EQUAL_STRING("Connect", BROOKESIA_DESCRIBE_ENUM_TO_STR(Wifi::GeneralAction::Connect).c_str());
    TEST_ASSERT_EQUAL_STRING("Request", BROOKESIA_DESCRIBE_ENUM_TO_STR(Http::FunctionId::Request).c_str());
    TEST_ASSERT_EQUAL_STRING("Notify", BROOKESIA_DESCRIBE_ENUM_TO_STR(Ble::FunctionId::Notify).c_str());
    TEST_ASSERT_EQUAL_STRING("Pause", BROOKESIA_DESCRIBE_ENUM_TO_STR(BtSpeaker::FunctionId::Pause).c_str());
    TEST_ASSERT_EQUAL_STRING("Open", BROOKESIA_DESCRIBE_ENUM_TO_STR(Video::EncoderFunctionId::Open).c_str());
    TEST_ASSERT_EQUAL_STRING(
        "Activate", BROOKESIA_DESCRIBE_ENUM_TO_STR(AgentManager::GeneralAction::Activate).c_str()
    );

    const auto *ble_notify = Ble::get_function_schema(Ble::FunctionId::Notify);
    TEST_ASSERT_NOT_NULL(ble_notify);
    TEST_ASSERT_EQUAL_size_t(4, ble_notify->parameters.size());
    TEST_ASSERT_EQUAL_STRING("ConnectionId", ble_notify->parameters[0].name.c_str());
    TEST_ASSERT_EQUAL_STRING("ServiceUuid", ble_notify->parameters[1].name.c_str());
    TEST_ASSERT_EQUAL_STRING("CharacteristicUuid", ble_notify->parameters[2].name.c_str());
    TEST_ASSERT_EQUAL_STRING("Data", ble_notify->parameters[3].name.c_str());
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(FunctionValueType::Array), static_cast<int>(ble_notify->parameters[3].type)
    );

    const auto *ble_write = Ble::get_event_schema(Ble::EventId::CharacteristicWritten);
    TEST_ASSERT_NOT_NULL(ble_write);
    TEST_ASSERT_EQUAL_size_t(4, ble_write->items.size());
    TEST_ASSERT_EQUAL_STRING("Data", ble_write->items[3].name.c_str());
    TEST_ASSERT_EQUAL_INT(static_cast<int>(EventItemType::Array), static_cast<int>(ble_write->items[3].type));

    const auto *dataflow_create = DataFlow::get_function_schema(DataFlow::FunctionId::CreateOperation);
    TEST_ASSERT_NOT_NULL(dataflow_create);
    TEST_ASSERT_EQUAL_size_t(1, dataflow_create->parameters.size());
    TEST_ASSERT_EQUAL_STRING("Config", dataflow_create->parameters[0].name.c_str());
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(FunctionValueType::Object), static_cast<int>(dataflow_create->parameters[0].type)
    );

    const auto *dataflow_active = DataFlow::get_function_schema(DataFlow::FunctionId::SetActiveSource);
    TEST_ASSERT_NOT_NULL(dataflow_active);
    TEST_ASSERT_EQUAL_size_t(3, dataflow_active->parameters.size());
    TEST_ASSERT_EQUAL_STRING("SourceName", dataflow_active->parameters[2].name.c_str());

    const auto *dataflow_state = DataFlow::get_event_schema(DataFlow::EventId::OperationStateChanged);
    TEST_ASSERT_NOT_NULL(dataflow_state);
    TEST_ASSERT_EQUAL_size_t(1, dataflow_state->items.size());
    TEST_ASSERT_EQUAL_STRING("Info", dataflow_state->items[0].name.c_str());

    const auto bt_speaker_functions = BtSpeaker::get_function_schemas();
    const auto bt_speaker_events = BtSpeaker::get_event_schemas();
    TEST_ASSERT_EQUAL_size_t(12, bt_speaker_functions.size());
    TEST_ASSERT_EQUAL_size_t(7, bt_speaker_events.size());

    const auto *bt_speaker_set_config = BtSpeaker::get_function_schema(BtSpeaker::FunctionId::SetConfig);
    TEST_ASSERT_NOT_NULL(bt_speaker_set_config);
    TEST_ASSERT_EQUAL_size_t(1, bt_speaker_set_config->parameters.size());
    TEST_ASSERT_EQUAL_STRING("Config", bt_speaker_set_config->parameters[0].name.c_str());
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(FunctionValueType::Object), static_cast<int>(bt_speaker_set_config->parameters[0].type)
    );

    const auto *bt_speaker_state = BtSpeaker::get_event_schema(BtSpeaker::EventId::StateChanged);
    TEST_ASSERT_NOT_NULL(bt_speaker_state);
    TEST_ASSERT_EQUAL_size_t(1, bt_speaker_state->items.size());
    TEST_ASSERT_EQUAL_STRING("State", bt_speaker_state->items[0].name.c_str());
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(EventItemType::Object), static_cast<int>(bt_speaker_state->items[0].type)
    );

    const auto *bt_capabilities = Bt::get_function_schema(Bt::FunctionId::GetCapabilities);
    TEST_ASSERT_NOT_NULL(bt_capabilities);
    TEST_ASSERT_TRUE(bt_capabilities->return_value.has_value());
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(FunctionValueType::Object),
        static_cast<int>(bt_capabilities->return_value->type)
    );
    const auto *bt_volume = Bt::get_function_schema(Bt::FunctionId::A2dpGetVolume);
    TEST_ASSERT_NOT_NULL(bt_volume);
    TEST_ASSERT_TRUE(bt_volume->return_value.has_value());
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(FunctionValueType::Number), static_cast<int>(bt_volume->return_value->type)
    );
}

BROOKESIA_TEST_CASE(
    test_service_helper_described_payload_roundtrips,
    "Service helper described payloads serialize and deserialize",
    "[service][helper][payload]"
)
{
    Video::EncoderConfig encoder_config = {
        .sinks = {
            {
                .format = Video::EncoderSinkFormat::MJPEG,
                .width = 320,
                .height = 240,
                .fps = 15,
            },
        },
        .enable_stream_mode = true,
        .display = Video::EncoderDisplayConfig{
            .output_name = "main",
            .source_name = "video",
            .source_role = "preview",
            .x = 4,
            .y = 8,
            .draw_timeout_ms = 100,
            .publish_sink_event = true,
            .activate_source = true,
            .sink_index = 0,
        },
    };
    auto encoder_json = serialize_and_parse(encoder_config);
    Video::EncoderConfig parsed_encoder_config;
    TEST_ASSERT_TRUE(BROOKESIA_DESCRIBE_FROM_JSON(encoder_json, parsed_encoder_config));
    TEST_ASSERT_EQUAL_size_t(1, parsed_encoder_config.sinks.size());
    TEST_ASSERT_TRUE(parsed_encoder_config.enable_stream_mode);
    TEST_ASSERT_TRUE(parsed_encoder_config.display.has_value());
    TEST_ASSERT_EQUAL_STRING("main", parsed_encoder_config.display->output_name.c_str());

    Storage::KvNameResult kv_name = {
        .name = "app.theme",
        .original_name = "theme",
        .hashed = false,
        .warning = "",
    };
    auto kv_json = serialize_and_parse(kv_name);
    Storage::KvNameResult parsed_kv_name;
    TEST_ASSERT_TRUE(BROOKESIA_DESCRIBE_FROM_JSON(kv_json, parsed_kv_name));
    TEST_ASSERT_EQUAL_STRING(kv_name.name.c_str(), parsed_kv_name.name.c_str());
    TEST_ASSERT_EQUAL_STRING(kv_name.original_name.c_str(), parsed_kv_name.original_name.c_str());

    Ble::PeripheralConfig ble_config = {
        .device_name = "AmapBridge-ESP32",
        .preferred_mtu = 247,
        .max_connections = 1,
        .auto_restart_advertising = true,
        .advertised_service_uuids = {"7a5a0001-0000-1000-8000-00805f9b4a10"},
        .services = {{
                .uuid = "7a5a0001-0000-1000-8000-00805f9b4a10",
                .characteristics = {
                    {
                        .uuid = "7a5a0002-0000-1000-8000-00805f9b4a10",
                        .write = true,
                        .write_without_response = true,
                        .notify = false,
                    },
                    {
                        .uuid = "7a5a0003-0000-1000-8000-00805f9b4a10",
                        .write = false,
                        .write_without_response = false,
                        .notify = true,
                    },
                },
            }
        },
    };
    auto ble_json = serialize_and_parse(ble_config);
    Ble::PeripheralConfig parsed_ble_config;
    TEST_ASSERT_TRUE(BROOKESIA_DESCRIBE_FROM_JSON(ble_json, parsed_ble_config));
    TEST_ASSERT_EQUAL_STRING(ble_config.device_name.c_str(), parsed_ble_config.device_name.c_str());
    TEST_ASSERT_EQUAL_UINT16(247, parsed_ble_config.preferred_mtu);
    TEST_ASSERT_EQUAL_UINT8(1, parsed_ble_config.max_connections);
    TEST_ASSERT_EQUAL_size_t(2, parsed_ble_config.services.front().characteristics.size());

    DataFlow::OperationConfig dataflow_config = {
        .owner = "test-app",
        .provider_id = "display.fake",
        .model = DataFlow::Model::Visual,
        .source = {
            .name = "navigation",
            .role = "app",
            .preferred_outputs = {"main"},
            .priority = 1,
        },
        .output_name = "main",
        .request_output = true,
        .activate_source = true,
    };
    auto dataflow_json = serialize_and_parse(dataflow_config);
    DataFlow::OperationConfig parsed_dataflow_config;
    TEST_ASSERT_TRUE(BROOKESIA_DESCRIBE_FROM_JSON(dataflow_json, parsed_dataflow_config));
    TEST_ASSERT_EQUAL_STRING("display.fake", parsed_dataflow_config.provider_id.c_str());
    TEST_ASSERT_EQUAL_STRING("navigation", parsed_dataflow_config.source.name.c_str());
    TEST_ASSERT_TRUE(parsed_dataflow_config.request_output);

    BtSpeaker::Config bt_speaker_config = {
        .device = {
            .device_name = "ESP-Brookesia",
            .discoverable = true,
            .connectable = true,
        },
        .stop_local_playback_on_connect = true,
    };
    auto bt_speaker_config_json = serialize_and_parse(bt_speaker_config);
    BtSpeaker::Config parsed_bt_speaker_config;
    TEST_ASSERT_TRUE(BROOKESIA_DESCRIBE_FROM_JSON(bt_speaker_config_json, parsed_bt_speaker_config));
    TEST_ASSERT_EQUAL_STRING("ESP-Brookesia", parsed_bt_speaker_config.device.device_name.c_str());
    TEST_ASSERT_TRUE(parsed_bt_speaker_config.stop_local_playback_on_connect);

    BtSpeaker::State bt_speaker_state = {
        .general_state = BtSpeaker::GeneralState::Started,
        .is_configured = true,
        .is_supported = true,
        .is_started = true,
        .is_connected = true,
        .is_music_active = true,
        .connection_state = BtSpeaker::ConnectionState::Connected,
        .stream_state = BtSpeaker::StreamState::Started,
        .playback_status = BtSpeaker::PlaybackStatus::Playing,
        .volume = 73,
        .connection = BtSpeaker::PeerInfo{
            .connection_id = 1,
            .address = "01:23:45:67:89:ab",
            .name = "Phone",
        },
        .metadata = {
            .title = "Track",
            .artist = "Artist",
            .album = "Album",
            .duration_ms = 180000,
        },
    };
    auto bt_speaker_state_json = serialize_and_parse(bt_speaker_state);
    BtSpeaker::State parsed_bt_speaker_state;
    TEST_ASSERT_TRUE(BROOKESIA_DESCRIBE_FROM_JSON(bt_speaker_state_json, parsed_bt_speaker_state));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(BtSpeaker::GeneralState::Started),
        static_cast<int>(parsed_bt_speaker_state.general_state)
    );
    TEST_ASSERT_TRUE(parsed_bt_speaker_state.is_music_active);
    TEST_ASSERT_TRUE(parsed_bt_speaker_state.connection.has_value());
    TEST_ASSERT_EQUAL_STRING("Phone", parsed_bt_speaker_state.connection->name.c_str());
    TEST_ASSERT_EQUAL_UINT8(73, parsed_bt_speaker_state.volume);
}
