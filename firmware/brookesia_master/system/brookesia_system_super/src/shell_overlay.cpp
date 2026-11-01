/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "private/shell_impl.hpp"

namespace esp_brookesia::system::super {

void ShellApp::disconnect_overlay_actions()
{
    for (auto &connection : overlay_action_connections_) {
        connection.disconnect();
    }
    overlay_action_connections_.clear();
}

}
