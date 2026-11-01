/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "esp_hosted.h"

#if CONFIG_BT_ENABLED && CONFIG_BT_NIMBLE_ENABLED && CONFIG_ESP_HOSTED_ENABLE_BT_NIMBLE

#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_hs_adv.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

#define BT_SERVICE_ENABLED 1

#else

#define BT_SERVICE_ENABLED 0

#endif

#include "bt_service.h"

static const char *TAG = "BtSvc";

#define BT_SERVICE_DEVICE_NAME          "ESP32-P4-Brookesia"
#define BT_SERVICE_DEFAULT_SCAN_MS      8000
#define BT_SERVICE_HOST_TASK_STACK      4096
#define BT_SERVICE_SYNC_TIMEOUT_MS      15000

#if BT_SERVICE_ENABLED

static SemaphoreHandle_t s_lock;
static bt_service_status_t s_status;
static uint8_t s_own_addr_type;
static uint32_t s_scan_generation;
static bool s_scanning;
static bt_service_scan_item_t s_items[BT_SERVICE_MAX_SCAN_ITEMS];
static size_t s_item_count;
static bool s_started;

static void bt_service_format_addr(const uint8_t addr[6], char out[BT_SERVICE_ADDR_MAX])
{
    snprintf(out, BT_SERVICE_ADDR_MAX, "%02x:%02x:%02x:%02x:%02x:%02x",
             addr[5], addr[4], addr[3], addr[2], addr[1], addr[0]);
}

static void bt_service_start_advertising(void)
{
    struct ble_hs_adv_fields fields;
    struct ble_gap_adv_params adv_params;
    const char *name = ble_svc_gap_device_name();
    int rc;

    memset(&fields, 0, sizeof(fields));
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    if (name != NULL && name[0] != '\0') {
        fields.name = (uint8_t *)name;
        fields.name_len = (uint8_t)strlen(name);
        fields.name_is_complete = 1;
    }

    rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGW(TAG, "ble_gap_adv_set_fields failed, rc=%d", rc);
        return;
    }

    memset(&adv_params, 0, sizeof(adv_params));
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;

    rc = ble_gap_adv_start(s_own_addr_type, NULL, BLE_HS_FOREVER, &adv_params,
                           NULL, NULL);
    if (rc != 0 && rc != BLE_HS_EALREADY) {
        ESP_LOGW(TAG, "ble_gap_adv_start failed, rc=%d", rc);
        return;
    }

    if (s_lock != NULL) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        s_status.advertising = true;
        xSemaphoreGive(s_lock);
    }
    ESP_LOGI(TAG, "BLE advertising as \"%s\"", name != NULL ? name : "(unnamed)");
}

static void bt_service_stop_advertising(void)
{
    int rc = ble_gap_adv_stop();
    if (rc != 0 && rc != BLE_HS_EALREADY) {
        ESP_LOGW(TAG, "ble_gap_adv_stop failed, rc=%d", rc);
    }
    if (s_lock != NULL) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        s_status.advertising = false;
        xSemaphoreGive(s_lock);
    }
}

static int bt_service_find_item_by_addr(const char *addr)
{
    for (size_t i = 0; i < s_item_count; ++i) {
        if (strcmp(s_items[i].address, addr) == 0) {
            return (int)i;
        }
    }
    return -1;
}

static void bt_service_insert_item(const char *addr, const char *name, int8_t rssi)
{
    bt_service_scan_item_t item;
    memset(&item, 0, sizeof(item));
    snprintf(item.address, sizeof(item.address), "%s", addr);
    if (name != NULL) {
        snprintf(item.name, sizeof(item.name), "%s", name);
    }
    item.rssi = rssi;

    /* Insertion sort, strongest signal first. */
    size_t pos = 0;
    while ((pos < s_item_count) && (s_items[pos].rssi >= item.rssi)) {
        ++pos;
    }
    if (pos >= BT_SERVICE_MAX_SCAN_ITEMS) {
        return;
    }
    if (s_item_count < BT_SERVICE_MAX_SCAN_ITEMS) {
        ++s_item_count;
    }
    size_t last = s_item_count - 1;
    while (last > pos) {
        s_items[last] = s_items[last - 1];
        --last;
    }
    s_items[pos] = item;
    ++s_scan_generation;
}

static void bt_service_handle_adv_report(const struct ble_gap_disc_desc *disc)
{
    struct ble_hs_adv_fields fields;
    char addr[BT_SERVICE_ADDR_MAX];
    char name[BT_SERVICE_NAME_MAX] = {0};

    if (ble_hs_adv_parse_fields(&fields, disc->data, disc->length_data) != 0) {
        return;
    }
    if (fields.name != NULL && fields.name_len > 0) {
        size_t len = fields.name_len;
        if (len >= sizeof(name)) {
            len = sizeof(name) - 1;
        }
        memcpy(name, fields.name, len);
        name[len] = '\0';
    }

    bt_service_format_addr(disc->addr.val, addr);

    if (s_lock == NULL) {
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    int index = bt_service_find_item_by_addr(addr);
    if (index >= 0) {
        if (name[0] != '\0') {
            snprintf(s_items[index].name, sizeof(s_items[index].name), "%s", name);
        }
        if (disc->rssi != 0) {
            s_items[index].rssi = disc->rssi;
        }
        ++s_scan_generation;
    } else if (name[0] != '\0') {
        /* Only remember named devices: an unnamed flood from dozens of
         * anonymous advertisers would make the list unusable. */
        bt_service_insert_item(addr, name, disc->rssi);
    }
    xSemaphoreGive(s_lock);
}

static int bt_service_gap_event(struct ble_gap_event *event, void *arg)
{
    (void)arg;

    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            if (s_lock != NULL) {
                xSemaphoreTake(s_lock, portMAX_DELAY);
                if (s_status.connections < UINT8_MAX) {
                    ++s_status.connections;
                }
                s_status.advertising = false;
                xSemaphoreGive(s_lock);
            }
            ESP_LOGI(TAG, "BLE connected, status=%d", event->connect.status);
        } else {
            ESP_LOGW(TAG, "BLE connect failed, status=%d", event->connect.status);
            bt_service_start_advertising();
        }
        return 0;

    case BLE_GAP_EVENT_DISCONNECT:
        if (s_lock != NULL) {
            xSemaphoreTake(s_lock, portMAX_DELAY);
            if (s_status.connections > 0) {
                --s_status.connections;
            }
            xSemaphoreGive(s_lock);
        }
        ESP_LOGI(TAG, "BLE disconnected, reason=0x%x", event->disconnect.reason);
        bt_service_start_advertising();
        return 0;

    case BLE_GAP_EVENT_ADV_COMPLETE:
        if (s_lock != NULL) {
            xSemaphoreTake(s_lock, portMAX_DELAY);
            s_status.advertising = false;
            xSemaphoreGive(s_lock);
        }
        return 0;

    case BLE_GAP_EVENT_DISC:
        bt_service_handle_adv_report(&event->disc);
        return 0;

    case BLE_GAP_EVENT_DISC_COMPLETE:
        if (s_lock != NULL) {
            xSemaphoreTake(s_lock, portMAX_DELAY);
            s_scanning = false;
            xSemaphoreGive(s_lock);
        }
        ESP_LOGI(TAG, "BLE scan complete, %u device(s)", (unsigned)s_item_count);
        if (s_status.connections == 0) {
            bt_service_start_advertising();
        }
        return 0;

    default:
        return 0;
    }
}

static void bt_service_on_sync(void)
{
    uint8_t addr[6] = {0};

    if (ble_hs_util_ensure_addr(0) != 0) {
        ESP_LOGW(TAG, "ble_hs_util_ensure_addr failed");
    }
    if (ble_hs_id_infer_auto(0, &s_own_addr_type) != 0) {
        ESP_LOGW(TAG, "unable to infer BLE address type");
        return;
    }

    if (s_lock != NULL) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        s_status.ready = true;
        if (ble_hs_id_copy_addr(s_own_addr_type, addr, NULL) == 0) {
            bt_service_format_addr(addr, s_status.address);
        }
        xSemaphoreGive(s_lock);
    }
    ESP_LOGI(TAG, "BLE controller ready, address %s", s_status.address);

    bt_service_start_advertising();
}

static void bt_service_on_reset(int reason)
{
    ESP_LOGW(TAG, "BLE host reset, reason=%d", reason);
    if (s_lock != NULL) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        s_status.ready = false;
        s_status.advertising = false;
        s_status.connections = 0;
        xSemaphoreGive(s_lock);
    }
}

static void bt_service_host_task(void *param)
{
    (void)param;
    ESP_LOGI(TAG, "BLE host task started");
    nimble_port_run();
    nimble_port_freertos_deinit();
}

static void bt_service_task(void *param)
{
    (void)param;

    /* Wait for the SDIO link to the co-processor (esp_hosted comes up with
     * Wi-Fi around 5.5 s). The old co-processor firmware does not answer the
     * version RPC, so do not use it as an "is the link up" probe - just wait. */
    vTaskDelay(pdMS_TO_TICKS(10000));

    esp_err_t err = nimble_port_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nimble_port_init failed: %s", esp_err_to_name(err));
        vTaskDelete(NULL);
        return;
    }

    ble_hs_cfg.reset_cb = bt_service_on_reset;
    ble_hs_cfg.sync_cb = bt_service_on_sync;
    ble_hs_cfg.store_status_cb = NULL;

    ble_svc_gap_init();
    ble_svc_gatt_init();
    ble_svc_gap_device_name_set(BT_SERVICE_DEVICE_NAME);

    nimble_port_freertos_init(bt_service_host_task);

    vTaskDelete(NULL);
}

void bt_service_start(void)
{
    if (s_started) {
        return;
    }
    s_started = true;

    if (s_lock == NULL) {
        s_lock = xSemaphoreCreateMutex();
    }
    memset(&s_status, 0, sizeof(s_status));
    snprintf(s_status.name, sizeof(s_status.name), "%s", BT_SERVICE_DEVICE_NAME);

    BaseType_t created = xTaskCreate(bt_service_task, "bt_service", 6144, NULL, 3, NULL);
    ESP_LOGI(TAG, "bt_service task create=%d free_heap=%u", (int)created,
             (unsigned)xPortGetFreeHeapSize());
}

bool bt_service_get_status(bt_service_status_t *status)
{
    if (status == NULL) {
        return false;
    }
    if (s_lock == NULL) {
        memset(status, 0, sizeof(*status));
        return false;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *status = s_status;
    xSemaphoreGive(s_lock);
    return true;
}

bool bt_service_scan_start(uint32_t duration_ms)
{
    struct ble_gap_disc_params params;

    if (s_lock == NULL) {
        return false;
    }
    if (s_status.ready == false) {
        ESP_LOGW(TAG, "scan requested before the host is ready");
        return false;
    }
    if (duration_ms == 0) {
        duration_ms = BT_SERVICE_DEFAULT_SCAN_MS;
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_scanning) {
        xSemaphoreGive(s_lock);
        return true;
    }
    s_item_count = 0;
    s_scanning = true;
    ++s_scan_generation;
    xSemaphoreGive(s_lock);

    /* Advertising and scanning at the same time makes the (old) co-processor
     * firmware drop scan reports, so pause the advertisement while scanning;
     * it is restarted from BLE_GAP_EVENT_DISC_COMPLETE. */
    bt_service_stop_advertising();

    memset(&params, 0, sizeof(params));
    params.itvl = 0;
    params.window = 0;
    params.passive = 0;
    params.filter_duplicates = 1;

    int rc = ble_gap_disc(s_own_addr_type, (int32_t)duration_ms, &params,
                          bt_service_gap_event, NULL);
    if (rc != 0) {
        ESP_LOGW(TAG, "ble_gap_disc failed, rc=%d", rc);
        xSemaphoreTake(s_lock, portMAX_DELAY);
        s_scanning = false;
        xSemaphoreGive(s_lock);
        bt_service_start_advertising();
        return false;
    }

    ESP_LOGI(TAG, "BLE scan started for %" PRIu32 " ms", duration_ms);
    return true;
}

void bt_service_scan_stop(void)
{
    if (s_lock == NULL) {
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool scanning = s_scanning;
    xSemaphoreGive(s_lock);
    if (!scanning) {
        return;
    }
    int rc = ble_gap_disc_cancel();
    if (rc != 0 && rc != BLE_HS_EALREADY) {
        ESP_LOGW(TAG, "ble_gap_disc_cancel failed, rc=%d", rc);
    }
}

bool bt_service_scan_is_running(void)
{
    if (s_lock == NULL) {
        return false;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool scanning = s_scanning;
    xSemaphoreGive(s_lock);
    return scanning;
}

size_t bt_service_scan_get_results(bt_service_scan_item_t *items, size_t max_items)
{
    if ((s_lock == NULL) || (items == NULL) || (max_items == 0)) {
        return 0;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    size_t count = s_item_count < max_items ? s_item_count : max_items;
    memcpy(items, s_items, count * sizeof(bt_service_scan_item_t));
    xSemaphoreGive(s_lock);
    return count;
}

uint32_t bt_service_scan_generation(void)
{
    if (s_lock == NULL) {
        return 0;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    uint32_t generation = s_scan_generation;
    xSemaphoreGive(s_lock);
    return generation;
}

#else  /* !BT_SERVICE_ENABLED */

void bt_service_start(void)
{
    ESP_LOGI(TAG, "Bluetooth stack not compiled in (CONFIG_BT_ENABLED / NimBLE hosted)");
}

bool bt_service_get_status(bt_service_status_t *status)
{
    if (status != NULL) {
        memset(status, 0, sizeof(*status));
    }
    return false;
}

bool bt_service_scan_start(uint32_t duration_ms)
{
    (void)duration_ms;
    return false;
}

void bt_service_scan_stop(void)
{
}

bool bt_service_scan_is_running(void)
{
    return false;
}

size_t bt_service_scan_get_results(bt_service_scan_item_t *items, size_t max_items)
{
    (void)items;
    (void)max_items;
    return 0;
}

uint32_t bt_service_scan_generation(void)
{
    return 0;
}

#endif
