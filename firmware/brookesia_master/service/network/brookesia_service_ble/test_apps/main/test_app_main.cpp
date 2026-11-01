/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */
#if !defined(ESP_PLATFORM)
#   define BROOKESIA_LIB_UTILS_TEST_ADAPTER_MAIN
#endif
#include "general.hpp"

#if defined(ESP_PLATFORM)
#include "esp_system.h"
#include "unity_test_utils.h"

void setUp(void)
{
    unity_utils_set_leak_level(64);
    unity_utils_record_free_mem();
}

void tearDown(void)
{
    esp_reent_cleanup();
    unity_utils_evaluate_leaks();
}

extern "C" void app_main(void)
{
    unity_run_menu();
}
#else

namespace {

bool init_unit_test()
{
    return true;
}

} // namespace

int main(int argc, char **argv)
{
    return boost::unit_test::unit_test_main(&init_unit_test, argc, argv);
}
#endif
