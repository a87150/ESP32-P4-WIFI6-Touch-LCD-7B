/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */
#include "plain_enum_fixture.hpp"

namespace esp_brookesia::lib_utils::test {

FormatArg make_plain_enum_format_arg_without_describe()
{
    return make_format_arg(PlainEnum::Value);
}

} // namespace esp_brookesia::lib_utils::test
