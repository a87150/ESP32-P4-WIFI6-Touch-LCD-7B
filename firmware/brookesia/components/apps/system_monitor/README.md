# System Monitor App

新增桌面 app：在出厂 Brookesia 手机界面上实时显示你电脑的 CPU / 内存 / 硬盘 / GPU / 网络 / 开机时长。

## 组成

| 文件 | 说明 |
|---|---|
| `firmware/brookesia/components/apps/system_monitor/SystemMonitor.hpp/.cpp` | 板端 app（图标 + UI + UDP 接收任务） |
| `firmware/brookesia/components/apps/system_monitor/img_app_sysmon.c` | 桌面图标（LVGL ARGB8888，脚本生成） |
| `scripts/pc_agent.py` | 电脑端采集 agent，广播 UDP 9999 |
| `firmware/brookesia/main/main.cpp`（改动） | `installApp(system_monitor)` |
| `firmware/brookesia/components/apps/apps.h`（改动） | include 加一行 |

## 数据流

```
PC (pc_agent.py, 1 秒/包)                         板 (System Monitor app)
  psutil + nvidia-smi                  UDP broadcast 255.255.255.255:9999
        ----------------------------------------------------------->
                                              UDP bind :9999 → JSON 解析
                                              LVGL 定时器 0.4s 刷新 UI
```

无需配置 IP：板子和电脑在同一局域网即可（板子先在出厂系统的 设置 → WLAN 里联网）。

## 烧录

```powershell
cd firmware/brookesia
idf.py set-target esp32p4    # 需要 ESP-IDF v5.5.x
idf.py build
idf.py app-flash -p COM3     # 只烧 app 到 factory 分区，保留其余
```

## 电脑端运行

```powershell
pip install psutil
python scripts/pc_agent.py
```

NVIDIA 显卡自动检测 `nvidia-smi`；没有 GPU 则 GPU 行显示 --。

## 回滚

恢复出厂桌面 UI：

```powershell
esptool write-flash 0x110000 firmware/ESP32-P4-WIFI6-Touch-LCD-7B-FactoryOnly.bin
```
