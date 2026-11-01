/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <string>

#include "brookesia/runtime_manager/types.hpp"
#include "brookesia/service_manager/common.hpp"

namespace esp_brookesia::system::core {

inline constexpr const char *RUNTIME_APP_ID_CALL_CONTEXT_KEY = "brookesia.system.runtime_app_id";
inline constexpr const char *RUNTIME_BLOCKING_SERVICE_CALL_CONTEXT_KEY =
    "brookesia.system.runtime_blocking_service_call";

enum class RuntimeServiceCallMode {
    Blocking,
    Asynchronous,
};

inline service::CallContext make_runtime_service_call_context(runtime::AppId app_id, RuntimeServiceCallMode mode)
{
    auto context = service::get_current_call_context();
    context[RUNTIME_APP_ID_CALL_CONTEXT_KEY] = std::to_string(app_id);
    if (mode == RuntimeServiceCallMode::Blocking) {
        context[RUNTIME_BLOCKING_SERVICE_CALL_CONTEXT_KEY] = "true";
    } else {
        // Never let an inherited blocking marker make an async request bypass the SystemApp strand.
        context.erase(RUNTIME_BLOCKING_SERVICE_CALL_CONTEXT_KEY);
    }
    return context;
}

inline bool is_blocking_runtime_service_call_context()
{
    return service::get_current_call_context_value(RUNTIME_BLOCKING_SERVICE_CALL_CONTEXT_KEY).has_value();
}

} // namespace esp_brookesia::system::core
