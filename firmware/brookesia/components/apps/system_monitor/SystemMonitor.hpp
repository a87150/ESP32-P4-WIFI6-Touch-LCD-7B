#pragma once

#include <stdint.h>
#include <mutex>
#include "lvgl.h"
#include "esp_brookesia.hpp"

class SystemMonitor: public ESP_Brookesia_PhoneApp
{
public:
    SystemMonitor();
    ~SystemMonitor();

    bool run(void);
    bool back(void);
    bool close(void);

private:
    /* ---- UI ---- */
    lv_obj_t *panel_root;
    lv_obj_t *label_status;
    lv_obj_t *label_cpu_value;
    lv_obj_t *bar_cpu;
    lv_obj_t *label_mem_value;
    lv_obj_t *bar_mem;
    lv_obj_t *label_disk_value;
    lv_obj_t *bar_disk;
    lv_obj_t *label_gpu_value;
    lv_obj_t *bar_gpu;
    lv_obj_t *label_net_value;
    lv_obj_t *label_uptime_value;
    uint16_t _width;
    uint16_t _height;

    /* ---- data ---- */
    struct Data {
        float cpu = 0;
        float mem_used = 0, mem_total = 0;
        float disk_used = 0, disk_total = 0;
        float gpu_util = 0, gpu_mem_used = 0, gpu_mem_total = 0;
        float net_down_kbps = 0, net_up_kbps = 0;
        uint32_t uptime_s = 0;
        uint32_t last_rx = 0;       /* ms tick of last received packet */
        char pc[32] = {0};
        char gpu_name[48] = {0};
        bool live = false;
    };

    Data data;
    std::mutex data_mutex;
    TaskHandle_t task_handle;
    volatile bool task_should_run;
    lv_timer_t *ui_timer;
    lv_obj_t *scr;

    static void netTask(void *arg);
    static void uiTimerCb(lv_timer_t *timer);
    void refreshUi(void);
    static lv_obj_t *createRow(lv_obj_t *parent, const char *name,
                               lv_obj_t **label_value, lv_obj_t **bar);
    static bool parseJsonField(const char *json, const char *key, float *out);
    static bool parseJsonString(const char *json, const char *key,
                                char *out, size_t out_size);
};
