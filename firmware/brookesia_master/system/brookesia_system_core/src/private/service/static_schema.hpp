/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "brookesia/service_manager/function/definition.hpp"

namespace esp_brookesia::system::core::static_schema {

enum class DefaultValueKind : uint8_t {
    None,
    Boolean,
    Number,
    String,
};

struct DefaultValueSpec {
    DefaultValueKind kind = DefaultValueKind::None;
    bool boolean_value = false;
    double number_value = 0;
    const char *string_value = nullptr;
};

struct FunctionParameterSpec {
    const char *name;
    const char *description;
    service::FunctionValueType type;
    DefaultValueSpec default_value = {};
};

struct FunctionReturnSpec {
    service::FunctionValueType type;
    const char *description;
};

struct FunctionSpec {
    const char *name;
    const char *description;
    std::span<const FunctionParameterSpec> parameters;
    bool require_scheduler;
    const FunctionReturnSpec *return_value = nullptr;
};

std::vector<service::FunctionSchema> materialize_function_schemas(std::span<const FunctionSpec> specs);

} // namespace esp_brookesia::system::core::static_schema
