/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */
#pragma once

#include "brookesia/lib_utils/log.hpp"

namespace esp_brookesia::lib_utils::test {

enum class PlainEnum : int {
    Value = 7,
};

FormatArg make_plain_enum_format_arg_without_describe();

} // namespace esp_brookesia::lib_utils::test
