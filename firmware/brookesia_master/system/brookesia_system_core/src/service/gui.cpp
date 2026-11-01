/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "brookesia/system_core/macro_configs.h"
#if !BROOKESIA_SYSTEM_CORE_SERVICE_ENABLE_DEBUG_LOG
#   define BROOKESIA_LOG_DISABLE_DEBUG_TRACE 1
#endif
#include "private/utils.hpp"
#include "private/heap_trace.hpp"
#include "private/service/static_schema.hpp"
#include "private/service/utils.hpp"

#include <array>
#include <utility>
#include <variant>

namespace esp_brookesia::system::core {

namespace {

std::expected<int32_t, std::string> get_int_param_or(
    const FunctionParameterMap &params,
    std::string_view name,
    int32_t default_value
)
{
    auto it = params.find(std::string(name));
    if (it == params.end()) {
        return default_value;
    }
    const auto *value = std::get_if<double>(&it->second);
    if (value == nullptr) {
        return std::unexpected("Parameter must be number: " + std::string(name));
    }
    return static_cast<int32_t>(*value);
}

std::expected<bool, std::string> get_bool_param_or(
    const FunctionParameterMap &params,
    std::string_view name,
    bool default_value
)
{
    auto it = params.find(std::string(name));
    if (it == params.end()) {
        return default_value;
    }
    const auto *value = std::get_if<bool>(&it->second);
    if (value == nullptr) {
        return std::unexpected("Parameter must be bool: " + std::string(name));
    }
    return *value;
}

std::expected<std::vector<std::string>, std::string> parse_string_array(
    const boost::json::array &array,
    std::string_view name)
{
    std::vector<std::string> values;
    values.reserve(array.size());
    for (const auto &value : array) {
        if (!value.is_string()) {
            return std::unexpected("Array parameter must contain strings only: " + std::string(name));
        }
        values.emplace_back(value.as_string().c_str());
    }
    return values;
}

using static_schema::DefaultValueKind;
using static_schema::FunctionParameterSpec;
using static_schema::FunctionReturnSpec;
using static_schema::FunctionSpec;

constexpr std::array<FunctionParameterSpec, 3> SET_BINDING_PARAMETERS = {{
        {"Path", "View absolute path", FunctionValueType::String},
        {"Key", "Binding key", FunctionValueType::String},
        {"Value", "Binding value", FunctionValueType::String},
    }
};
constexpr std::array<FunctionParameterSpec, 1> SET_BINDINGS_PARAMETERS = {{
        {"Updates", "Binding updates", FunctionValueType::Array},
    }
};
constexpr std::array<FunctionParameterSpec, 2> GET_BINDING_PARAMETERS = {{
        {"Path", "View absolute path", FunctionValueType::String},
        {"Key", "Binding key", FunctionValueType::String},
    }
};
constexpr std::array<FunctionParameterSpec, 1> GET_CONSTANT_PARAMETERS = {{
        {"Path", "Constant dot path", FunctionValueType::String},
    }
};
constexpr std::array<FunctionParameterSpec, 2> SET_TEXT_PARAMETERS = {{
        {"Path", "View absolute path", FunctionValueType::String},
        {"Text", "Text", FunctionValueType::String},
    }
};
constexpr std::array<FunctionParameterSpec, 2> SET_VALUE_PARAMETERS = {{
        {"Path", "View absolute path", FunctionValueType::String},
        {"Value", "Value", FunctionValueType::Number},
    }
};
constexpr std::array<FunctionParameterSpec, 2> SET_CHECKED_PARAMETERS = {{
        {"Path", "View absolute path", FunctionValueType::String},
        {"Checked", "Checked", FunctionValueType::Boolean},
    }
};
constexpr std::array<FunctionParameterSpec, 3> CREATE_VIEW_PARAMETERS = {{
        {"TemplateId", "Template id", FunctionValueType::String},
        {"ParentPath", "Parent absolute path", FunctionValueType::String},
        {"InstanceId", "Instance id", FunctionValueType::String},
    }
};
constexpr std::array<FunctionParameterSpec, 1> DESTROY_VIEW_PARAMETERS = {{
        {"Path", "View absolute path", FunctionValueType::String},
    }
};
constexpr std::array<FunctionParameterSpec, 1> SUBSCRIBE_ACTION_PARAMETERS = {{
        {"Action", "Action name", FunctionValueType::String},
    }
};
constexpr std::array<FunctionParameterSpec, 2> TRIGGER_SCREEN_FLOW_PARAMETERS = {{
        {"ScreenFlow", "Screen flow id", FunctionValueType::String},
        {"Action", "Screen flow action", FunctionValueType::String},
    }
};
constexpr std::array<FunctionParameterSpec, 1> GET_SCREEN_FLOW_STATE_PARAMETERS = {{
        {"ScreenFlow", "Screen flow id", FunctionValueType::String},
    }
};
constexpr std::array<FunctionParameterSpec, 1> GET_VIEW_FRAME_PARAMETERS = {{
        {"Path", "View absolute path", FunctionValueType::String},
    }
};
constexpr std::array<FunctionParameterSpec, 2> SET_VIEW_SRC_PARAMETERS = {{
        {"Path", "Image view absolute path", FunctionValueType::String},
        {"Src", "Image resource id or path", FunctionValueType::String},
    }
};
constexpr std::array<FunctionParameterSpec, 1> IMAGE_IDS_PARAMETERS = {{
        {"Ids", "Image resource id array", FunctionValueType::Array},
    }
};
constexpr std::array<FunctionParameterSpec, 2> START_VIEW_ANIMATION_PARAMETERS = {{
        {"Path", "View absolute path", FunctionValueType::String},
        {"Animation", "Animation object", FunctionValueType::Object},
    }
};
constexpr std::array<FunctionParameterSpec, 1> STOP_ANIMATION_PARAMETERS = {{
        {"AnimationId", "Animation subscription id", FunctionValueType::Number},
    }
};
constexpr std::array<FunctionParameterSpec, 4> SCROLL_TO_PARAMETERS = {{
        {"Path", "View absolute path", FunctionValueType::String},
        {
            "X",
            "Scroll X offset",
            FunctionValueType::Number,
            {.kind = DefaultValueKind::Number, .number_value = 0},
        },
        {
            "Y",
            "Scroll Y offset",
            FunctionValueType::Number,
            {.kind = DefaultValueKind::Number, .number_value = 0},
        },
        {
            "Animated",
            "Whether to animate scrolling",
            FunctionValueType::Boolean,
            {.kind = DefaultValueKind::Boolean, .boolean_value = true},
        },
    }
};
constexpr std::array<FunctionParameterSpec, 2> SCROLL_TO_VIEW_PARAMETERS = {{
        {"Path", "View absolute path", FunctionValueType::String},
        {"Animated", "Whether to animate scrolling", FunctionValueType::Boolean},
    }
};
constexpr std::array<FunctionParameterSpec, 1> EXECUTE_BATCH_PARAMETERS = {{
        {"Commands", "GUI command array", FunctionValueType::Array},
    }
};

constexpr FunctionReturnSpec GET_BINDING_RETURN = {
    FunctionValueType::String,
    "Binding value string",
};
constexpr FunctionReturnSpec GET_CONSTANT_RETURN = {
    FunctionValueType::Object,
    "Constant value object. Example: {\"Value\":\"example\"}",
};
constexpr FunctionReturnSpec GET_SCREEN_FLOW_STATE_RETURN = {
    FunctionValueType::Object,
    "Screen flow state object. Example: {\"State\":\"screen_id\"}",
};
constexpr FunctionReturnSpec GET_VIEW_FRAME_RETURN = {
    FunctionValueType::Object,
    "View frame object. Example: {\"x\":0,\"y\":0,\"width\":320,\"height\":240}",
};
constexpr FunctionReturnSpec START_VIEW_ANIMATION_RETURN = {
    FunctionValueType::Number,
    "Animation subscription id",
};
constexpr FunctionReturnSpec EXECUTE_BATCH_RETURN = {
    FunctionValueType::Object,
    "Batch result object. Example: {\"success\":true,\"results\":[]}",
};

constexpr std::array<FunctionSpec, static_cast<size_t>(SystemGuiHelper::FunctionId::Max)> GUI_FUNCTION_SPECS = {{
        {
            "SetBinding",
            "Set a binding in the caller app GUI document",
            SET_BINDING_PARAMETERS,
            false,
        },
        {
            "SetBindings",
            "Set multiple bindings in the caller app GUI document",
            SET_BINDINGS_PARAMETERS,
            false,
        },
        {
            "GetBinding",
            "Get a binding from the caller app GUI document",
            GET_BINDING_PARAMETERS,
            false,
            &GET_BINDING_RETURN,
        },
        {
            "GetConstant",
            "Get a constant from the caller app GUI document",
            GET_CONSTANT_PARAMETERS,
            false,
            &GET_CONSTANT_RETURN,
        },
        {
            "SetText",
            "Set label text in the caller app GUI document",
            SET_TEXT_PARAMETERS,
            false,
        },
        {
            "SetValue",
            "Set numeric value in the caller app GUI document",
            SET_VALUE_PARAMETERS,
            false,
        },
        {
            "SetChecked",
            "Set checked state in the caller app GUI document",
            SET_CHECKED_PARAMETERS,
            false,
        },
        {
            "CreateView",
            "Create a view in the caller app GUI document",
            CREATE_VIEW_PARAMETERS,
            false,
        },
        {
            "DestroyView",
            "Destroy a view in the caller app GUI document",
            DESTROY_VIEW_PARAMETERS,
            false,
        },
        {
            "SubscribeAction",
            "Forward a GUI action to brookesia_app.on_action(action)",
            SUBSCRIBE_ACTION_PARAMETERS,
            false,
        },
        {
            "TriggerScreenFlow",
            "Trigger a screen flow transition in the caller app GUI document",
            TRIGGER_SCREEN_FLOW_PARAMETERS,
            false,
        },
        {
            "GetScreenFlowState",
            "Get a running screen flow state from the caller app GUI document",
            GET_SCREEN_FLOW_STATE_PARAMETERS,
            false,
            &GET_SCREEN_FLOW_STATE_RETURN,
        },
        {
            "GetViewFrame",
            "Get a view frame from the caller app GUI document",
            GET_VIEW_FRAME_PARAMETERS,
            false,
            &GET_VIEW_FRAME_RETURN,
        },
        {
            "SetViewSrc",
            "Set an image source in the caller app GUI document",
            SET_VIEW_SRC_PARAMETERS,
            false,
        },
        {
            "PreloadImages",
            "Decode and cache caller app GUI image resources by id",
            IMAGE_IDS_PARAMETERS,
            false,
        },
        {
            "ReleaseImages",
            "Release manually preloaded caller app GUI image resources by id",
            IMAGE_IDS_PARAMETERS,
            false,
        },
        {
            "StartViewAnimation",
            "Start a view animation in the caller app GUI document",
            START_VIEW_ANIMATION_PARAMETERS,
            false,
            &START_VIEW_ANIMATION_RETURN,
        },
        {
            "StopAnimation",
            "Stop a caller app GUI animation",
            STOP_ANIMATION_PARAMETERS,
            false,
        },
        {
            "ScrollTo",
            "Scroll a view to an absolute scroll offset",
            SCROLL_TO_PARAMETERS,
            false,
        },
        {
            "ScrollToView",
            "Scroll a view into the visible area of its scrollable parent",
            SCROLL_TO_VIEW_PARAMETERS,
            false,
        },
        {
            "ExecuteBatch",
            "Execute GUI commands in one caller app GUI scheduler task",
            EXECUTE_BATCH_PARAMETERS,
            false,
            &EXECUTE_BATCH_RETURN,
        },
    }
};

static_assert(GUI_FUNCTION_SPECS.size() == static_cast<size_t>(SystemGuiHelper::FunctionId::Max));

} // namespace

std::span<const service::FunctionSchema> SystemGuiHelper::get_function_schemas()
{
    static const std::vector<service::FunctionSchema> SCHEMAS =
        static_schema::materialize_function_schemas(GUI_FUNCTION_SPECS);
    return std::span<const service::FunctionSchema>(SCHEMAS);
}

std::span<const service::EventSchema> SystemGuiHelper::get_event_schemas()
{
    static const std::array<service::EventSchema, BROOKESIA_DESCRIBE_ENUM_TO_NUM(EventId::Max)> SCHEMAS = {{
            {
                .name = BROOKESIA_DESCRIBE_TO_STR(EventId::Action),
                .description = "GUI action forwarded for an app",
                .require_scheduler = false,
            },
        }
    };
    return std::span<const service::EventSchema>(SCHEMAS);
}

GuiService::GuiService(System &system)
    : ServiceBase({
    .name = SystemGuiHelper::get_name().data(),
    .description = "Expose System Core GUI operations to applications.",
    .version = make_version(
        BROOKESIA_SYSTEM_CORE_VER_MAJOR, BROOKESIA_SYSTEM_CORE_VER_MINOR, BROOKESIA_SYSTEM_CORE_VER_PATCH
    ),
})
, system_(system)
{}

std::vector<service::FunctionSchema> GuiService::get_function_schemas()
{
    return static_schema::materialize_function_schemas(GUI_FUNCTION_SPECS);
}

std::vector<service::EventSchema> GuiService::get_event_schemas()
{
    auto schemas = SystemGuiHelper::get_event_schemas();
    return std::vector<service::EventSchema>(schemas.begin(), schemas.end());
}

service::ServiceBase::FunctionHandlerMap GuiService::get_function_handlers()
{
    return {
        [this](FunctionParameterMap &&params)
        {
            return set_binding(std::move(params));
        },
        [this](FunctionParameterMap &&params)
        {
            return set_bindings(std::move(params));
        },
        [this](FunctionParameterMap &&params)
        {
            return get_binding(std::move(params));
        },
        [this](FunctionParameterMap &&params)
        {
            return get_constant(std::move(params));
        },
        [this](FunctionParameterMap &&params)
        {
            return set_text(std::move(params));
        },
        [this](FunctionParameterMap &&params)
        {
            return set_value(std::move(params));
        },
        [this](FunctionParameterMap &&params)
        {
            return set_checked(std::move(params));
        },
        [this](FunctionParameterMap &&params)
        {
            return create_view(std::move(params));
        },
        [this](FunctionParameterMap &&params)
        {
            return destroy_view(std::move(params));
        },
        [this](FunctionParameterMap &&params)
        {
            return subscribe_action(std::move(params));
        },
        [this](FunctionParameterMap &&params)
        {
            return trigger_screen_flow(std::move(params));
        },
        [this](FunctionParameterMap &&params)
        {
            return get_screen_flow_state(std::move(params));
        },
        [this](FunctionParameterMap &&params)
        {
            return get_view_frame(std::move(params));
        },
        [this](FunctionParameterMap &&params)
        {
            return set_view_src(std::move(params));
        },
        [this](FunctionParameterMap &&params)
        {
            return preload_images(std::move(params));
        },
        [this](FunctionParameterMap &&params)
        {
            return release_images(std::move(params));
        },
        [this](FunctionParameterMap &&params)
        {
            return start_view_animation(std::move(params));
        },
        [this](FunctionParameterMap &&params)
        {
            return stop_animation(std::move(params));
        },
        [this](FunctionParameterMap &&params)
        {
            return scroll_to(std::move(params));
        },
        [this](FunctionParameterMap &&params)
        {
            return scroll_to_view(std::move(params));
        },
        [this](FunctionParameterMap &&params)
        {
            return execute_batch(std::move(params));
        },
    };
}

FunctionResult GuiService::set_binding(FunctionParameterMap &&params)
{
    auto app_id = system_.get_current_runtime_app_owner();
    if (!app_id) {
        return make_error(app_id.error());
    }
    auto path = get_string_param(params, BROOKESIA_DESCRIBE_TO_STR(SystemGuiHelper::BindingParam::Path));
    auto key = get_string_param(params, BROOKESIA_DESCRIBE_TO_STR(SystemGuiHelper::BindingParam::Key));
    auto value = get_string_param(params, BROOKESIA_DESCRIBE_TO_STR(SystemGuiHelper::BindingParam::Value));
    if (!path || !key || !value) {
        return make_error(!path ? path.error() : (!key ? key.error() : value.error()));
    }
    return service::ServiceBase::to_function_result(system_.gui_set_binding_value(*app_id, *path, *key, *value));
}

FunctionResult GuiService::set_bindings(FunctionParameterMap &&params)
{
    auto app_id = system_.get_current_runtime_app_owner();
    if (!app_id) {
        return make_error(app_id.error());
    }
    auto updates_array = get_array_param(params, BROOKESIA_DESCRIBE_TO_STR(SystemGuiHelper::BindingsParam::Updates));
    if (!updates_array) {
        return make_error(updates_array.error());
    }

    auto updates = parse_binding_updates(*updates_array);
    if (!updates) {
        return make_error(updates.error());
    }

    return service::ServiceBase::to_function_result(system_.gui_set_binding_values(*app_id, *updates));
}

FunctionResult GuiService::get_binding(FunctionParameterMap &&params)
{
    auto app_id = system_.get_current_runtime_app_owner();
    if (!app_id) {
        return make_error(app_id.error());
    }
    auto path = get_string_param(params, BROOKESIA_DESCRIBE_TO_STR(SystemGuiHelper::BindingParam::Path));
    auto key = get_string_param(params, BROOKESIA_DESCRIBE_TO_STR(SystemGuiHelper::BindingParam::Key));
    if (!path || !key) {
        return make_error(!path ? path.error() : key.error());
    }
    return make_success(system_.gui_get_binding_value(*app_id, *path, *key).value_or(std::string()));
}

FunctionResult GuiService::get_constant(FunctionParameterMap &&params)
{
    auto app_id = system_.get_current_runtime_app_owner();
    if (!app_id) {
        return make_error(app_id.error());
    }
    auto path = get_string_param(params, BROOKESIA_DESCRIBE_TO_STR(SystemGuiHelper::PathParam::Path));
    if (!path) {
        return make_error(path.error());
    }
    auto value = system_.gui_get_constant_value(*app_id, *path);
    if (!value) {
        return make_error(value.error());
    }

    boost::json::object result;
    result["Value"] = std::move(*value);
    return make_success(std::move(result));
}

FunctionResult GuiService::set_text(FunctionParameterMap &&params)
{
    auto app_id = system_.get_current_runtime_app_owner();
    if (!app_id) {
        return make_error(app_id.error());
    }
    auto path = get_string_param(params, BROOKESIA_DESCRIBE_TO_STR(SystemGuiHelper::TextParam::Path));
    auto text = get_string_param(params, BROOKESIA_DESCRIBE_TO_STR(SystemGuiHelper::TextParam::Text));
    if (!path || !text) {
        return make_error(!path ? path.error() : text.error());
    }
    return service::ServiceBase::to_function_result(system_.gui_set_text(*app_id, *path, *text));
}

FunctionResult GuiService::set_value(FunctionParameterMap &&params)
{
    auto app_id = system_.get_current_runtime_app_owner();
    if (!app_id) {
        return make_error(app_id.error());
    }
    auto path = get_string_param(params, BROOKESIA_DESCRIBE_TO_STR(SystemGuiHelper::ValueParam::Path));
    auto value = get_number_param(params, BROOKESIA_DESCRIBE_TO_STR(SystemGuiHelper::ValueParam::Value));
    if (!path || !value) {
        return make_error(!path ? path.error() : value.error());
    }
    return service::ServiceBase::to_function_result(system_.gui_set_value(*app_id, *path, static_cast<int32_t>(*value)));
}

FunctionResult GuiService::set_checked(FunctionParameterMap &&params)
{
    auto app_id = system_.get_current_runtime_app_owner();
    if (!app_id) {
        return make_error(app_id.error());
    }
    auto path = get_string_param(params, BROOKESIA_DESCRIBE_TO_STR(SystemGuiHelper::CheckedParam::Path));
    auto checked = get_bool_param(params, BROOKESIA_DESCRIBE_TO_STR(SystemGuiHelper::CheckedParam::Checked));
    if (!path || !checked) {
        return make_error(!path ? path.error() : checked.error());
    }
    return service::ServiceBase::to_function_result(system_.gui_set_checked(*app_id, *path, *checked));
}

FunctionResult GuiService::create_view(FunctionParameterMap &&params)
{
    auto app_id = system_.get_current_runtime_app_owner();
    if (!app_id) {
        return make_error(app_id.error());
    }
    auto template_id = get_string_param(params, BROOKESIA_DESCRIBE_TO_STR(SystemGuiHelper::CreateViewParam::TemplateId));
    auto parent_path = get_string_param(params, BROOKESIA_DESCRIBE_TO_STR(SystemGuiHelper::CreateViewParam::ParentPath));
    auto instance_id = get_string_param(params, BROOKESIA_DESCRIBE_TO_STR(SystemGuiHelper::CreateViewParam::InstanceId));
    if (!template_id || !parent_path || !instance_id) {
        return make_error(!template_id ? template_id.error() : (!parent_path ? parent_path.error() : instance_id.error()));
    }
    auto result = system_.gui_create_view(*app_id, *template_id, *parent_path, *instance_id);
    if (!result) {
        return make_error(result.error());
    }
    return make_success();
}

FunctionResult GuiService::destroy_view(FunctionParameterMap &&params)
{
    auto app_id = system_.get_current_runtime_app_owner();
    if (!app_id) {
        return make_error(app_id.error());
    }
    auto path = get_string_param(params, BROOKESIA_DESCRIBE_TO_STR(SystemGuiHelper::PathParam::Path));
    if (!path) {
        return make_error(path.error());
    }
    return system_.gui_destroy_view(*app_id, *path) ? make_success() : make_error("Failed to destroy view");
}

FunctionResult GuiService::subscribe_action(FunctionParameterMap &&params)
{
    auto app_id = system_.get_current_runtime_app_owner();
    if (!app_id) {
        return make_error(app_id.error());
    }
    auto action = get_string_param(params, BROOKESIA_DESCRIBE_TO_STR(SystemGuiHelper::ActionParam::Action));
    if (!action) {
        return make_error(action.error());
    }
    auto heap_before_subscribe = heap_trace::capture();
    if constexpr (heap_trace::enabled) {
        const auto app_id_string = std::to_string(*app_id);
        const auto details = "app_id=" + app_id_string + " function=SubscribeAction action=" + *action;
        heap_trace::log_detail(
            "system.service.gui",
            "SubscribeAction before system subscribe",
            app_id_string,
            details,
            heap_before_subscribe
        );
    }
    auto result = system_.gui_subscribe_action(*app_id, *action);
    if constexpr (heap_trace::enabled) {
        const auto app_id_string = std::to_string(*app_id);
        const auto details = "app_id=" + app_id_string + " function=SubscribeAction action=" + *action +
                             " success=" + std::to_string(result.has_value());
        heap_trace::log_detail(
            "system.service.gui",
            "SubscribeAction after system subscribe",
            app_id_string,
            details,
            heap_trace::capture(),
            &heap_before_subscribe
        );
    }
    return service::ServiceBase::to_function_result(result);
}

FunctionResult GuiService::trigger_screen_flow(FunctionParameterMap &&params)
{
    auto app_id = system_.get_current_runtime_app_owner();
    if (!app_id) {
        return make_error(app_id.error());
    }
    auto screen_flow = get_string_param(params, BROOKESIA_DESCRIBE_TO_STR(SystemGuiHelper::ScreenFlowParam::ScreenFlow));
    if (!screen_flow) {
        return make_error(screen_flow.error());
    }
    auto action = get_string_param(params, BROOKESIA_DESCRIBE_TO_STR(SystemGuiHelper::ScreenFlowParam::Action));
    if (!action) {
        return make_error(action.error());
    }
    return service::ServiceBase::to_function_result(system_.gui_trigger_screen_flow(*app_id, *screen_flow, *action));
}

FunctionResult GuiService::get_screen_flow_state(FunctionParameterMap &&params)
{
    auto app_id = system_.get_current_runtime_app_owner();
    if (!app_id) {
        return make_error(app_id.error());
    }
    auto screen_flow = get_string_param(params, BROOKESIA_DESCRIBE_TO_STR(SystemGuiHelper::ScreenFlowParam::ScreenFlow));
    if (!screen_flow) {
        return make_error(screen_flow.error());
    }
    auto state = system_.gui_get_screen_flow_state(*app_id, *screen_flow);
    if (!state) {
        return make_error("Failed to get screen flow state");
    }
    boost::json::object object;
    object["State"] = *state;
    return make_success(std::move(object));
}

FunctionResult GuiService::get_view_frame(FunctionParameterMap &&params)
{
    auto app_id = system_.get_current_runtime_app_owner();
    if (!app_id) {
        return make_error(app_id.error());
    }
    auto path = get_string_param(params, BROOKESIA_DESCRIBE_TO_STR(SystemGuiHelper::PathParam::Path));
    if (!path) {
        return make_error(path.error());
    }
    auto frame = system_.gui_get_view_frame(*app_id, *path);
    if (!frame) {
        return make_error("Failed to get view frame");
    }
    boost::json::object object;
    object["x"] = frame->x;
    object["y"] = frame->y;
    object["width"] = frame->width;
    object["height"] = frame->height;
    return make_success(std::move(object));
}

FunctionResult GuiService::set_view_src(FunctionParameterMap &&params)
{
    auto app_id = system_.get_current_runtime_app_owner();
    if (!app_id) {
        return make_error(app_id.error());
    }
    auto path = get_string_param(params, BROOKESIA_DESCRIBE_TO_STR(SystemGuiHelper::SourceParam::Path));
    auto src = get_string_param(params, BROOKESIA_DESCRIBE_TO_STR(SystemGuiHelper::SourceParam::Src));
    if (!path || !src) {
        return make_error(!path ? path.error() : src.error());
    }
    return service::ServiceBase::to_function_result(system_.gui_set_view_src(*app_id, *path, *src));
}

FunctionResult GuiService::preload_images(FunctionParameterMap &&params)
{
    auto app_id = system_.get_current_runtime_app_owner();
    if (!app_id) {
        return make_error(app_id.error());
    }
    auto ids_array = get_array_param(params, BROOKESIA_DESCRIBE_TO_STR(SystemGuiHelper::ImageIdsParam::Ids));
    if (!ids_array) {
        return make_error(ids_array.error());
    }
    auto ids = parse_string_array(*ids_array, BROOKESIA_DESCRIBE_TO_STR(SystemGuiHelper::ImageIdsParam::Ids));
    if (!ids) {
        return make_error(ids.error());
    }
    return service::ServiceBase::to_function_result(system_.gui_preload_images(*app_id, *ids));
}

FunctionResult GuiService::release_images(FunctionParameterMap &&params)
{
    auto app_id = system_.get_current_runtime_app_owner();
    if (!app_id) {
        return make_error(app_id.error());
    }
    auto ids_array = get_array_param(params, BROOKESIA_DESCRIBE_TO_STR(SystemGuiHelper::ImageIdsParam::Ids));
    if (!ids_array) {
        return make_error(ids_array.error());
    }
    auto ids = parse_string_array(*ids_array, BROOKESIA_DESCRIBE_TO_STR(SystemGuiHelper::ImageIdsParam::Ids));
    if (!ids) {
        return make_error(ids.error());
    }
    return service::ServiceBase::to_function_result(system_.gui_release_images(*app_id, *ids));
}

FunctionResult GuiService::start_view_animation(FunctionParameterMap &&params)
{
    auto app_id = system_.get_current_runtime_app_owner();
    if (!app_id) {
        return make_error(app_id.error());
    }
    auto path = get_string_param(params, BROOKESIA_DESCRIBE_TO_STR(SystemGuiHelper::AnimationParam::Path));
    auto animation_object = get_object_param(params, BROOKESIA_DESCRIBE_TO_STR(SystemGuiHelper::AnimationParam::Animation));
    if (!path || !animation_object) {
        return make_error(!path ? path.error() : animation_object.error());
    }
    auto animation = animation_from_json(*animation_object);
    if (!animation) {
        return make_error(animation.error());
    }
    auto animation_id = system_.gui_start_view_animation_with_id(*app_id, *path, *animation);
    if (!animation_id) {
        return make_error(animation_id.error());
    }
    return make_success(static_cast<double>(*animation_id));
}

FunctionResult GuiService::stop_animation(FunctionParameterMap &&params)
{
    auto app_id = system_.get_current_runtime_app_owner();
    if (!app_id) {
        return make_error(app_id.error());
    }
    auto animation_id = get_number_param(
                            params,
                            BROOKESIA_DESCRIBE_TO_STR(SystemGuiHelper::AnimationIdParam::AnimationId)
                        );
    if (!animation_id) {
        return make_error(animation_id.error());
    }
    return system_.gui_stop_animation(*app_id, static_cast<gui::SubscriptionId>(*animation_id)) ?
           make_success() :
           make_error("Failed to stop animation");
}

FunctionResult GuiService::scroll_to_view(FunctionParameterMap &&params)
{
    auto app_id = system_.get_current_runtime_app_owner();
    if (!app_id) {
        return make_error(app_id.error());
    }
    auto path = get_string_param(params, BROOKESIA_DESCRIBE_TO_STR(SystemGuiHelper::ScrollParam::Path));
    auto animated = get_bool_param(params, BROOKESIA_DESCRIBE_TO_STR(SystemGuiHelper::ScrollParam::Animated));
    if (!path || !animated) {
        return make_error(!path ? path.error() : animated.error());
    }
    return service::ServiceBase::to_function_result(system_.gui_scroll_to_view(*app_id, *path, *animated));
}

FunctionResult GuiService::scroll_to(FunctionParameterMap &&params)
{
    auto app_id = system_.get_current_runtime_app_owner();
    if (!app_id) {
        return make_error(app_id.error());
    }
    auto path = get_string_param(params, BROOKESIA_DESCRIBE_TO_STR(SystemGuiHelper::ScrollToParam::Path));
    auto x = get_int_param_or(params, BROOKESIA_DESCRIBE_TO_STR(SystemGuiHelper::ScrollToParam::X), 0);
    auto y = get_int_param_or(params, BROOKESIA_DESCRIBE_TO_STR(SystemGuiHelper::ScrollToParam::Y), 0);
    auto animated = get_bool_param_or(
                        params,
                        BROOKESIA_DESCRIBE_TO_STR(SystemGuiHelper::ScrollToParam::Animated),
                        true
                    );
    if (!path || !x || !y || !animated) {
        return make_error(!path ? path.error() : (!x ? x.error() : (!y ? y.error() : animated.error())));
    }
    return service::ServiceBase::to_function_result(system_.gui_scroll_to(*app_id, *path, *x, *y, *animated));
}

FunctionResult GuiService::execute_batch(FunctionParameterMap &&params)
{
    auto app_id = system_.get_current_runtime_app_owner();
    if (!app_id) {
        return make_error(app_id.error());
    }
    auto commands_array = get_array_param(params, BROOKESIA_DESCRIBE_TO_STR(SystemGuiHelper::BatchParam::Commands));
    if (!commands_array) {
        return make_error(commands_array.error());
    }
    auto commands = parse_gui_batch_commands(*commands_array);
    if (!commands) {
        return make_error(commands.error());
    }
    auto result = system_.gui_execute_batch(*app_id, *commands);
    if (!result) {
        return make_error(result.error());
    }
    return make_success(gui_batch_result_to_json(*result));
}

} // namespace esp_brookesia::system::core
