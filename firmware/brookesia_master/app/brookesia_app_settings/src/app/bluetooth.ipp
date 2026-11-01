/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

namespace {

constexpr const char *BT_STATE_PATH = "/bluetooth/page/info_card/state_value";
constexpr const char *BT_ADDRESS_PATH = "/bluetooth/page/info_card/address_value";
constexpr const char *BT_SCAN_LABEL_PATH = "/bluetooth/page/device_card/scan_button/scan_label";
constexpr const char *BT_EMPTY_PATH = "/bluetooth/page/device_card/empty";
constexpr size_t BT_VISIBLE_ROWS = 5;
constexpr int BT_REFRESH_INTERVAL_MS = 1000;
constexpr uint32_t BT_SCAN_DURATION_MS = 8000;

std::string make_bt_row_path(size_t index, std::string_view leaf)
{
    return std::string("/bluetooth/page/device_card/rows/row") + std::to_string(index) + "/" +
           std::string(leaf);
}

} // namespace

void SettingsApp::cancel_bluetooth_refresh_timer(system::core::AppContext &context)
{
    if (bluetooth_refresh_timer_id_ == system::core::INVALID_TIMER_ID) {
        return;
    }
    if (!context.timer().stop(bluetooth_refresh_timer_id_)) {
        BROOKESIA_LOGW(
            "Failed to stop Settings Bluetooth refresh timer: %1%", bluetooth_refresh_timer_id_
        );
    }
    bluetooth_refresh_timer_id_ = system::core::INVALID_TIMER_ID;
}

void SettingsApp::schedule_bluetooth_refresh(system::core::AppContext &context)
{
    if (bluetooth_refresh_timer_id_ != system::core::INVALID_TIMER_ID) {
        return;
    }
    auto timer = context.timer().start_periodic(BLUETOOTH_REFRESH_TIMER_NAME, BT_REFRESH_INTERVAL_MS);
    if (!timer) {
        BROOKESIA_LOGW("Failed to start Settings Bluetooth refresh timer: %1%", timer.error());
        return;
    }
    bluetooth_refresh_timer_id_ = *timer;
}

std::expected<void, std::string> SettingsApp::refresh_bluetooth_page(system::core::AppContext &context)
{
    bt_service_status_t status = {};
    const bool status_valid = bt_service_get_status(&status);
    const bool scanning = bt_service_scan_is_running();

    std::string state_text;
    if (!status_valid || !status.ready) {
        state_text = "正在启动";
    } else if (scanning) {
        state_text = "扫描中";
    } else if (status.connections > 0) {
        state_text = "已连接 " + std::to_string(status.connections) + " 个设备";
    } else if (status.advertising) {
        state_text = "正在广播";
    } else {
        state_text = "未广播";
    }

    std::vector<gui::BindingValueUpdate> updates;
    add_binding_update(updates, BT_STATE_PATH, "labelProps.text", state_text);
    add_binding_update(
        updates, BT_ADDRESS_PATH, "labelProps.text",
        (status_valid && status.address[0] != '\0') ? status.address : "--"
    );
    add_binding_update(
        updates, BT_SCAN_LABEL_PATH, "labelProps.text", scanning ? "停止" : "扫描"
    );

    bt_service_scan_item_t items[BT_VISIBLE_ROWS] = {};
    const size_t item_count = bt_service_scan_get_results(items, BT_VISIBLE_ROWS);
    for (size_t i = 0; i < BT_VISIBLE_ROWS; ++i) {
        const bool has_item = i < item_count;
        add_binding_update(
            updates, make_bt_row_path(i, "row"), "commonProps.hidden",
            has_item ? "false" : "true"
        );
        if (!has_item) {
            continue;
        }
        std::string name = items[i].name[0] != '\0' ? items[i].name : "(未知设备)";
        add_binding_update(updates, make_bt_row_path(i, "name"), "labelProps.text", name);
        add_binding_update(
            updates, make_bt_row_path(i, "address"), "labelProps.text", items[i].address
        );
        add_binding_update(
            updates, make_bt_row_path(i, "rssi"), "labelProps.text",
            std::to_string(static_cast<int>(items[i].rssi)) + " dBm"
        );
    }
    add_binding_update(
        updates, BT_EMPTY_PATH, "commonProps.hidden", item_count > 0 ? "true" : "false"
    );

    bluetooth_last_scan_generation_ = bt_service_scan_generation();
    return context.gui().set_binding_values(updates);
}

std::expected<void, std::string> SettingsApp::handle_bluetooth_action(
    system::core::AppContext &context,
    std::string_view action
)
{
    if (action != ACTION_BLUETOOTH_SCAN) {
        return {};
    }

    if (bt_service_scan_is_running()) {
        bt_service_scan_stop();
    } else if (!bt_service_scan_start(BT_SCAN_DURATION_MS)) {
        BROOKESIA_LOGW("Failed to start BLE scan");
    }
    return refresh_bluetooth_page(context);
}
