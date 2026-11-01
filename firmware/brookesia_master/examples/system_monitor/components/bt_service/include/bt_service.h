/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Minimal BLE service for the ESP32-P4 desktop.
 *
 * The ESP32-P4 has no radio of its own: BLE works through the on-board
 * ESP32-C6 co-processor over ESP-Hosted VHCI. The co-processor firmware
 * currently flashed on the C6 runs its BT controller from boot, so the host
 * only has to bring up NimBLE (no BT_INIT RPC).
 *
 * The service keeps a connectable advertiser running ("ESP32-P4-Brookesia")
 * and offers an on-demand GAP scan that reports nearby BLE devices.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BT_SERVICE_NAME_MAX 32
#define BT_SERVICE_ADDR_MAX 18
#define BT_SERVICE_MAX_SCAN_ITEMS 8

typedef struct {
    /** NimBLE host is synced with the controller and ready for GAP calls. */
    bool ready;
    /** A connectable advertisement is currently active. */
    bool advertising;
    /** Advertising device name. */
    char name[BT_SERVICE_NAME_MAX];
    /** Local controller address, "aa:bb:cc:dd:ee:ff". */
    char address[BT_SERVICE_ADDR_MAX];
    /** Number of active BLE connections. */
    uint8_t connections;
} bt_service_status_t;

typedef struct {
    char name[BT_SERVICE_NAME_MAX];
    char address[BT_SERVICE_ADDR_MAX];
    int8_t rssi;
} bt_service_scan_item_t;

/** Start the BLE host (idempotent). Returns immediately; work happens in a task. */
void bt_service_start(void);

/** Query the live status. Returns false when the status is not available yet. */
bool bt_service_get_status(bt_service_status_t *status);

/** Start a GAP scan for `duration_ms` milliseconds. Results are kept sorted by RSSI. */
bool bt_service_scan_start(uint32_t duration_ms);

/** Stop an ongoing scan. Safe to call when no scan is running. */
void bt_service_scan_stop(void);

bool bt_service_scan_is_running(void);

/** Copy up to `max_items` scan results (strongest signal first). Returns the copied count. */
size_t bt_service_scan_get_results(bt_service_scan_item_t *items, size_t max_items);

/** Monotonic counter bumped whenever the scan result set changes. */
uint32_t bt_service_scan_generation(void);

#ifdef __cplusplus
}
#endif
