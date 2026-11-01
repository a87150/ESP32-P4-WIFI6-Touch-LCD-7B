# 工程结构

[English](PROJECT_STRUCTURE.md)

本仓库是面向 ESP32-P4-WIFI6-Touch-LCD-7B 的开发板专用示例和固件集合。

| 路径 | 用途 |
| --- | --- |
| `README.md` | 仓库概览与快速开始 |
| `examples/esp-idf/` | 独立 ESP-IDF 示例 |
| `examples/arduino/` | Arduino 草图和随仓库板级库 |
| `firmware/` | 仅盘点的交付源码与预编译出厂固件 |
| `firmware/brookesia/` | ESP-Brookesia 出厂风格固件工程 |
| `firmware/brookesia_master/` | 内置的 ESP-Brookesia 0.8 源码树与 7B 桌面系统固件工程（本项目主线） |
| `config/` | 共享 ESP32-P4 芯片版本叠加配置 |
| `docs/` | 维护者与用户文档 |
| `.github/` | CI 工作流、发现与 Markdown 审计脚本 |

## ESP-IDF 工程

请使用小写路径 `examples/esp-idf/`。Linux CI 会把仅大小写不同的路径视为不同目录，
因此不要添加另一个大小写不同的 ESP-IDF 示例根目录。

每个工程应包含：

- `CMakeLists.txt`
- `main/`
- `sdkconfig.defaults`
- 面向硬件用户的示例应提供 `README.md`

开发板的显示、触摸、音频、SD 卡和无线集成应使用管理型 7B BSP 组件
`waveshare/esp32_p4_wifi6_touch_lcd_7b` 3.0.1。显示应用直接使用 BSP 公开的
LVGL adapter 配置与加锁 API。不要把生成输出提交到 Git。

## Arduino 工程

请使用 `examples/arduino/` 存放 Arduino-ESP32 草图。每个一方草图位于
`examples/arduino/examples/<name>/`，并且只包含一个与目录同名的 `.ino` 文件。
`libraries/displays/` 提供 LCD-7B EK79007 DSI、GT911 和背光配置；随仓库的
`GFX_Library_for_Arduino` 与 `lvgl` 目录提供可复现的显示和 LVGL 构建。RS485 与
TWAI 示例刻意保持独立，不初始化显示。

## 固件源码

`firmware/brookesia` 是单独盘点的交付源码工程，不进入默认示例 CI。出厂 BIN 是
不可变的交付工件。只有在明确限定的固件维护流程中，才会修改和验证固件源码。

## Brookesia 桌面系统

`firmware/brookesia_master/` 以源码形式内置 ESP-Brookesia 0.8，并加入 7B 板级包与
`examples/system_monitor/` 桌面固件工程。它是一个独立的 ESP-IDF 工程（ESP-IDF 6.1，
`esp32p4` 目标），自带分区表、littlefs 镜像与组件；在该目录下用 `idf.py` 编译即可，
完整说明见 [BROOKESIA_7B_ZH.md](BROOKESIA_7B_ZH.md)。与 `firmware/brookesia` 不同，
它不使用托管组件 `waveshare/esp32_p4_wifi6_touch_lcd_7b`：板级支持放在内置 HAL
板级包里，以便使用更新的 ESP-Brookesia 版本。

