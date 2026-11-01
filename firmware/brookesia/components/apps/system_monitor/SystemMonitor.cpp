#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "lwip/sockets.h"
#include "esp_brookesia.hpp"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "SystemMonitor.hpp"

LV_IMG_DECLARE(img_app_sysmon);

static const char TAG[] = "SysMon";

#define SYSMON_UDP_PORT         9999
#define SYSMON_RX_MAX           (4 * 1024)
#define SYSMON_TASK_STACK       (10 * 1024)
#define SYSMON_TASK_PRIO        3
#define SYSMON_LIVE_TIMEOUT_MS  (6 * 1000)
#define SYSMON_REFRESH_MS       400

#define COLOR_BG        lv_color_hex(0x17171f)
#define COLOR_ROW       lv_color_hex(0x222230)
#define COLOR_TEXT      lv_color_hex(0xE8E8EC)
#define COLOR_TEXT_DIM  lv_color_hex(0x9098A8)
#define COLOR_ACCENT    lv_color_hex(0x2E86DE)
#define COLOR_GREEN     lv_color_hex(0x27AE60)
#define COLOR_ORANGE    lv_color_hex(0xE67E22)

SystemMonitor::SystemMonitor():
    ESP_Brookesia_PhoneApp("System Monitor", &img_app_sysmon, true)
{
    panel_root = nullptr;
    label_status = nullptr;
    label_cpu_value = nullptr;
    bar_cpu = nullptr;
    label_mem_value = nullptr;
    bar_mem = nullptr;
    label_disk_value = nullptr;
    bar_disk = nullptr;
    label_gpu_value = nullptr;
    bar_gpu = nullptr;
    label_net_value = nullptr;
    label_uptime_value = nullptr;
    task_handle = nullptr;
    ui_timer = nullptr;
    scr = nullptr;
    task_should_run = false;
}

SystemMonitor::~SystemMonitor()
{
}

/* Create one metric row: [name][value] / bar */
lv_obj_t *SystemMonitor::createRow(lv_obj_t *parent, const char *name,
                                   lv_obj_t **label_value, lv_obj_t **bar)
{
    lv_obj_t *cont = lv_obj_create(parent);
    lv_obj_set_size(cont, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(cont, COLOR_ROW, 0);
    lv_obj_set_style_bg_opa(cont, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(cont, 12, 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_style_pad_all(cont, 10, 0);
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);

    /* header: name left, value right */
    lv_obj_t *head = lv_obj_create(cont);
    lv_obj_set_size(head, LV_PCT(100), 26);
    lv_obj_set_style_bg_opa(head, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(head, 0, 0);
    lv_obj_set_style_pad_all(head, 0, 0);
    lv_obj_clear_flag(head, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *label_name = lv_label_create(head);
    lv_label_set_text(label_name, name);
    lv_obj_set_style_text_color(label_name, COLOR_TEXT, 0);
    lv_obj_set_style_text_font(label_name, &lv_font_montserrat_18, 0);
    lv_obj_align(label_name, LV_ALIGN_LEFT_MID, 0, 0);

    lv_obj_t *val = lv_label_create(head);
    lv_label_set_text(val, "--");
    lv_obj_set_style_text_color(val, COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(val, &lv_font_montserrat_18, 0);
    lv_obj_align(val, LV_ALIGN_RIGHT_MID, 0, 0);
    if (label_value != nullptr) {
        *label_value = val;
    }

    lv_obj_t *prog = lv_bar_create(cont);
    lv_obj_set_size(prog, LV_PCT(100), 12);
    lv_bar_set_range(prog, 0, 100);
    lv_bar_set_value(prog, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(prog, lv_color_hex(0x101018), LV_PART_MAIN);
    lv_obj_set_style_border_width(prog, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(prog, COLOR_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_radius(prog, 6, 0);
    if (bar != nullptr) {
        *bar = prog;
    }
    return cont;
}

/* Simple JSON field lookup (float) */
bool SystemMonitor::parseJsonField(const char *json, const char *key, float *out)
{
    char pattern[48];
    snprintf(pattern, sizeof(pattern), "\"%s\":", key);

    const char *p = strstr(json, pattern);
    if (p == nullptr) {
        return false;
    }
    p += strlen(pattern);
    while (*p == ' ' || *p == '\t') {
        p++;
    }
    *out = strtof(p, nullptr);
    return true;
}

/* Simple JSON field lookup (string) */
bool SystemMonitor::parseJsonString(const char *json, const char *key,
                                    char *out, size_t out_size)
{
    char pattern[48];
    snprintf(pattern, sizeof(pattern), "\"%s\":\"", key);

    const char *p = strstr(json, pattern);
    if (p == nullptr) {
        return false;
    }
    p += strlen(pattern);
    for (size_t i = 0; i < out_size - 1 && *p != '\0'; i++) {
        if (*p == '"') {
            out[i] = '\0';
            return true;
        }
        out[i] = *p;
        p++;
    }
    out[out_size - 1] = '\0';
    return true;
}

void SystemMonitor::netTask(void *arg)
{
    SystemMonitor *self = (SystemMonitor *)arg;

    uint8_t *rx = (uint8_t *)malloc(SYSMON_RX_MAX);
    if (rx == nullptr) {
        ESP_LOGE(TAG, "No mem for rx buffer");
        vTaskDelay(portMAX_DELAY);
    }

    while (self->task_should_run) {
        sockaddr_in local_addr = {0};
        sockaddr_in src_addr = {0};
        socklen_t addr_len = sizeof(src_addr);

        int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (sock < 0) {
            ESP_LOGE(TAG, "socket failed");
            vTaskDelay(pdMS_TO_TICKS(3000));
            continue;
        }
        int enable = 1;
        setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable));
        struct timeval tv = { .tv_sec = 2, .tv_usec = 0 };
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        local_addr.sin_family = AF_INET;
        local_addr.sin_port = htons(SYSMON_UDP_PORT);
        local_addr.sin_addr.s_addr = htonl(INADDR_ANY);
        if (bind(sock, (const sockaddr *)&local_addr, sizeof(local_addr)) != 0) {
            ESP_LOGE(TAG, "bind %d failed", SYSMON_UDP_PORT);
            ::close(sock);
            vTaskDelay(pdMS_TO_TICKS(3000));
            continue;
        }
        ESP_LOGI(TAG, "Listening UDP:%d", SYSMON_UDP_PORT);

        while (self->task_should_run) {
            int len = recvfrom(sock, rx, SYSMON_RX_MAX - 1, 0,
                               (sockaddr *)&src_addr, &addr_len);
            if (len <= 0) {
                continue;
            }
            rx[len] = '\0';
            if (strncmp((char *)rx, "{\"agent\"", 8) != 0) {
                continue;
            }

            {
                std::lock_guard<std::mutex> lock(self->data_mutex);
                float f = 0;
                if (parseJsonField((char *)rx, "cpu", &f)) {
                    self->data.cpu = f;
                }
                if (parseJsonField((char *)rx, "mem_used", &f)) {
                    self->data.mem_used = f;
                }
                if (parseJsonField((char *)rx, "mem_total", &f)) {
                    self->data.mem_total = f;
                }
                if (parseJsonField((char *)rx, "disk_used", &f)) {
                    self->data.disk_used = f;
                }
                if (parseJsonField((char *)rx, "disk_total", &f)) {
                    self->data.disk_total = f;
                }
                if (parseJsonField((char *)rx, "gpu_util", &f)) {
                    self->data.gpu_util = f;
                }
                if (parseJsonField((char *)rx, "gpu_mem_used", &f)) {
                    self->data.gpu_mem_used = f;
                }
                if (parseJsonField((char *)rx, "gpu_mem_total", &f)) {
                    self->data.gpu_mem_total = f;
                }
                if (parseJsonField((char *)rx, "net_down", &f)) {
                    self->data.net_down_kbps = f;
                }
                if (parseJsonField((char *)rx, "net_up", &f)) {
                    self->data.net_up_kbps = f;
                }
                if (parseJsonField((char *)rx, "uptime_s", &f)) {
                    self->data.uptime_s = (uint32_t)f;
                }
                parseJsonString((char *)rx, "pc", self->data.pc, sizeof(self->data.pc));
                parseJsonString((char *)rx, "gpu_name", self->data.gpu_name,
                                sizeof(self->data.gpu_name));
                self->data.last_rx = (uint32_t)(esp_timer_get_time() / 1000);
                self->data.live = true;
            }
        }
        ::close(sock);
    }
    free(rx);
    self->task_handle = nullptr;
    vTaskDelete(nullptr);
}

void SystemMonitor::refreshUi(void)
{
    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
    bool is_live;
    float cpu, mem_used, mem_total, disk_used, disk_total;
    float gpu_util, gpu_mem_used, gpu_mem_total, net_down, net_up;
    uint32_t uptime;
    char pc_name[32] = {0};
    char gpu_name[48] = {0};

    {
        std::lock_guard<std::mutex> lock(data_mutex);
        is_live = data.live && (now - data.last_rx) < SYSMON_LIVE_TIMEOUT_MS;
        cpu = data.cpu;
        mem_used = data.mem_used;
        mem_total = data.mem_total;
        disk_used = data.disk_used;
        disk_total = data.disk_total;
        gpu_util = data.gpu_util;
        gpu_mem_used = data.gpu_mem_used;
        gpu_mem_total = data.gpu_mem_total;
        net_down = data.net_down_kbps;
        net_up = data.net_up_kbps;
        uptime = data.uptime_s;
        if (is_live) {
            snprintf(pc_name, sizeof(pc_name), "%s", data.pc);
            snprintf(gpu_name, sizeof(gpu_name), "%s", data.gpu_name);
        }
    }

    if (!is_live) {
        lv_label_set_text(label_status,
                          "No PC agent detected. Run pc_agent.py on your PC (UDP:9999)");
        lv_obj_set_style_text_color(label_status, COLOR_TEXT_DIM, 0);
        lv_label_set_text(label_cpu_value, "--");
        lv_label_set_text(label_mem_value, "--");
        lv_label_set_text(label_disk_value, "--");
        lv_label_set_text(label_gpu_value, "--");
        lv_label_set_text(label_net_value, "down: --    up: --");
        lv_label_set_text(label_uptime_value, "--");
        lv_bar_set_value(bar_cpu, 0, LV_ANIM_ON);
        lv_bar_set_value(bar_mem, 0, LV_ANIM_ON);
        lv_bar_set_value(bar_disk, 0, LV_ANIM_ON);
        lv_bar_set_value(bar_gpu, 0, LV_ANIM_ON);
        return;
    }

    lv_label_set_text_fmt(label_status, "Connected: %s", pc_name);
    lv_obj_set_style_text_color(label_status, COLOR_GREEN, 0);

    lv_bar_set_value(bar_cpu, cpu, LV_ANIM_ON);
    lv_label_set_text_fmt(label_cpu_value, "%.1f %%", (double)cpu);

    if (mem_total > 0.1f) {
        lv_bar_set_value(bar_mem, mem_used * 100.0f / mem_total, LV_ANIM_ON);
        lv_label_set_text_fmt(label_mem_value, "%.1f / %.1f GB", (double)(mem_used / 1024.0f),
                              (double)(mem_total / 1024.0f));
    }

    if (disk_total > 0.1f) {
        lv_bar_set_value(bar_disk, disk_used * 100.0f / disk_total, LV_ANIM_ON);
        lv_label_set_text_fmt(label_disk_value, "%.1f / %.1f GB", (double)(disk_used / 1024.0f),
                              (double)(disk_total / 1024.0f));
    }

    if (gpu_mem_total > 0.1f) {
        lv_bar_set_value(bar_gpu, gpu_util, LV_ANIM_ON);
        lv_label_set_text_fmt(label_gpu_value, "%.0f %%  (%.1f/%.1f GB)",
                              (double)gpu_util, (double)(gpu_mem_used / 1024.0f),
                              (double)(gpu_mem_total / 1024.0f));
        (void)gpu_name; /* full name visible in GPU row header on PC side */
    }

    lv_label_set_text_fmt(label_net_value, "down: %.2f Mbps        up: %.2f Mbps",
                          (double)net_down, (double)net_up);

    if (uptime > 0) {
        lv_label_set_text_fmt(label_uptime_value, "%u d %02u:%02u",
                              (unsigned)(uptime / 86400), (unsigned)((uptime / 3600) % 24),
                              (unsigned)((uptime / 60) % 60));
    }
}

void SystemMonitor::uiTimerCb(lv_timer_t *timer)
{
    SystemMonitor *self = (SystemMonitor *)timer->user_data;
    if (self->panel_root == nullptr) {
        return;
    }
    self->refreshUi();
}

bool SystemMonitor::run(void)
{
    lv_area_t area = getVisualArea();
    _width = area.x2 - area.x1;
    _height = area.y2 - area.y1;

    scr = lv_scr_act();
    panel_root = lv_obj_create(scr);
    lv_obj_set_size(panel_root, _width, _height);
    lv_obj_align(panel_root, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(panel_root, COLOR_BG, 0);
    lv_obj_set_style_bg_opa(panel_root, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(panel_root, 0, 0);
    lv_obj_set_style_radius(panel_root, 0, 0);
    lv_obj_set_style_pad_all(panel_root, 12, 0);
    lv_obj_set_flex_flow(panel_root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(panel_root, 10, 0);
    lv_obj_clear_flag(panel_root, LV_OBJ_FLAG_SCROLLABLE);

    /* status line */
    label_status = lv_label_create(panel_root);
    lv_label_set_text(label_status, "Waiting for PC agent (UDP:9999)...");
    lv_obj_set_style_text_color(label_status, COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(label_status, &lv_font_montserrat_20, 0);

    /* metric rows */
    createRow(panel_root, LV_SYMBOL_CHARGE " CPU",
              &label_cpu_value, &bar_cpu);
    createRow(panel_root, LV_SYMBOL_SD_CARD " Memory",
              &label_mem_value, &bar_mem);
    createRow(panel_root, LV_SYMBOL_SD_CARD " Disk (C:)",
              &label_disk_value, &bar_disk);
    createRow(panel_root, LV_SYMBOL_SETTINGS " GPU",
              &label_gpu_value, &bar_gpu);

    /* network row (text only) */
    lv_obj_t *net_cont = lv_obj_create(panel_root);
    lv_obj_set_size(net_cont, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(net_cont, COLOR_ROW, 0);
    lv_obj_set_style_bg_opa(net_cont, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(net_cont, 12, 0);
    lv_obj_set_style_border_width(net_cont, 0, 0);
    lv_obj_set_style_pad_all(net_cont, 10, 0);
    lv_obj_clear_flag(net_cont, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *label_net_name = lv_label_create(net_cont);
    lv_label_set_text(label_net_name, LV_SYMBOL_WIFI " Network");
    lv_obj_set_style_text_color(label_net_name, COLOR_TEXT, 0);
    lv_obj_set_style_text_font(label_net_name, &lv_font_montserrat_18, 0);
    lv_obj_align(label_net_name, LV_ALIGN_TOP_LEFT, 0, 0);

    label_net_value = lv_label_create(net_cont);
    lv_label_set_text(label_net_value, "down: --    up: --");
    lv_obj_set_style_text_color(label_net_value, COLOR_TEXT, 0);
    lv_obj_set_style_text_font(label_net_value, &lv_font_montserrat_20, 0);
    lv_obj_align(label_net_value, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    label_uptime_value = lv_label_create(net_cont);
    lv_label_set_text(label_uptime_value, "--");
    lv_obj_set_style_text_color(label_uptime_value, COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(label_uptime_value, &lv_font_montserrat_18, 0);
    lv_obj_align(label_uptime_value, LV_ALIGN_BOTTOM_RIGHT, 0, 0);

    /* data task */
    data.live = false;
    task_should_run = true;
    if (xTaskCreate(netTask, "sysmon", SYSMON_TASK_STACK, this,
                    SYSMON_TASK_PRIO, &task_handle) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create net task");
        task_should_run = false;
    }

    ui_timer = lv_timer_create(uiTimerCb, SYSMON_REFRESH_MS, this);
    refreshUi();
    return true;
}

bool SystemMonitor::back(void)
{
    notifyCoreClosed();
    return true;
}

bool SystemMonitor::close(void)
{
    task_should_run = false;
    task_handle = nullptr;
    if (scr != nullptr) {
        lv_obj_clean(panel_root);
        panel_root = nullptr;
    }
    data.live = false;
    return true;
}
