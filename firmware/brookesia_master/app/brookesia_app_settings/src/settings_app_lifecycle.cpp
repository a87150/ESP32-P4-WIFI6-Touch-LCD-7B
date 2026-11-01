/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "private/settings_app_internal.hpp"

namespace esp_brookesia::app::settings {

using namespace detail;

SettingsApp::SettingsApp()
    : impl_(std::make_unique<Impl>())
{
}

SettingsApp::~SettingsApp() = default;

#include "app/lifecycle.ipp"

} // namespace esp_brookesia::app::settings
