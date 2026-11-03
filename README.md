<div align="center">
  <h1>ESP32-P4-WIFI6-Touch-LCD-7B</h1>
  <p><strong>基于 ESP-Brookesia 的 7 英寸桌面系统 · ESP32-P4 + ESP32-C6</strong></p>
  <p>
    <a href="https://github.com/a87150/ESP32-P4-WIFI6-Touch-LCD-7B/actions/workflows/esp-idf-examples.yml"><img alt="Build" src="https://github.com/a87150/ESP32-P4-WIFI6-Touch-LCD-7B/actions/workflows/esp-idf-examples.yml/badge.svg"></a>
    <a href="LICENSE"><img alt="License" src="https://img.shields.io/github/license/a87150/ESP32-P4-WIFI6-Touch-LCD-7B"></a>
  </p>
  <p>
    简体中文 ·
    <a href="README_EN.md">English</a> ·
    <a href="https://www.waveshare.com/esp32-p4-wifi6-touch-lcd-7b.htm">🌐 产品页</a> ·
    <a href="https://docs.waveshare.com/ESP32-P4-WIFI6-Touch-LCD-7B">📚 文档</a> ·
    <a href="examples/esp-idf/">🧩 ESP-IDF 示例</a> ·
    <a href="examples/arduino/">🔧 Arduino 示例</a> ·
    <a href="firmware/">📦 固件</a>
  </p>
  <img src="assets/ESP32-P4-WIFI6-Touch-LCD-7B-details-1.jpg" alt="ESP32-P4-WIFI6-Touch-LCD-7B" width="720">
</div>

---

> 本仓库在微雪 ESP32-P4-WIFI6-Touch-LCD-7B 官方 BSP 之上，做了一套可日常使用的
> **ESP-Brookesia 0.8 桌面系统**（ESP-IDF 6.1 + LVGL 9）：状态栏、桌面、系统监视器、
> 音乐、相册、视频播放器与设置（Wi-Fi / BLE），并重做分区表留出 16 MB 资源分区。
> 先看 [`docs/BROOKESIA_7B_ZH.md`](docs/BROOKESIA_7B_ZH.md) 即可上手。

## ✨ 项目概览

本仓库的主线是 `firmware/brookesia_master/`：把上游 ESP-Brookesia 源码树固化进来，
在 `examples/system_monitor` 里实现并打包整套桌面固件。官方 `examples/`、出厂固件与
Arduino 示例仍然保留，作为板级参考。

主要能力：

- **状态栏**：时间居中；右侧 Wi-Fi 信号（纯白图标，按信号强度运行时着色）、蓝牙、电量；
  左上角显示已挂起到后台的应用图标。
- **桌面**：应用图标从左到右排列、支持多页；任意页面上滑回桌面，按住进入最近任务；
  最近任务支持 **CLEAR ALL**；壁纸可由图片 App 设置。
- **系统监视器**：磁盘全卷聚合使用率（≥ 90% 变红报警）、环心 CPU / GPU 温度
  （超过 90 °C 显示 `HIGH!` 并报警）、网络卡片附带 DISK I/O。
- **音乐**：WAV 播放器，离开 App 后继续播放，支持拖动进度、独立音量与元信息。
- **相册**：上一张 / 下一张、自动播放、显示分辨率与文件大小、一键设为壁纸。
- **视频**：MJPEG（纯 JPEG 串接）硬解播放，12 fps 定速，进度拖动，
  **双击画面暂停 / 继续**，底部控制栏按钮居中。
- **设置**：Wi-Fi 扫描 / 连接 / 断开（显示 IP、MAC，掉线自动重连）、BLE 扫描与广播、
  显示、声音、时间、语言与设备信息。

## 🖥️ 硬件概况

| 功能 | 器件 / 接口 |
| --- | --- |
| 主处理器 | ESP32-P4NRW32，HP 系统双核 RISC-V 最高 360 MHz，并含低功耗单核 |
| 存储 | 封装内 32 MB PSRAM，板载 32 MB NOR Flash |
| 无线 | ESP32-C6-MINI-1，经 SDIO 提供 2.4 GHz Wi-Fi 6 与 Bluetooth 5 (LE) |
| 显示 | 7 英寸 1024 × 600 IPS 触摸屏，MIPI-DSI |
| 触摸 | GT911 电容触摸控制器，支持五点触控 |
| 摄像头 | MIPI-CSI（2-lane）接口，可选配 OV5647 |
| 音频 | ES8311 编解码器、ES7210 音频 ADC、双麦克风与扬声器接口 |
| 存储与扩展 | microSD、USB 2.0 OTG HS、CAN/TWAI、RS485、I2C、UART 与 GPIO |
| 板级支持 | 托管组件 `waveshare/esp32_p4_wifi6_touch_lcd_7b` 3.0.1 |
| ESP-IDF target | `esp32p4` |

> 硬件引脚以官方产品文档和板级支持包为准；仓库内没有原理图副本。

## 🚀 快速开始

前置条件：

- **ESP-IDF v6.1**（Brookesia 0.8 要求 `>= 6.0` 且 `<= 6.2`，其他版本会被拒绝编译）。
- USB 数据线连接开发板的下载口，Windows / Linux / macOS 均可。
- 首次编译需要联网拉取托管组件。

编译：

```powershell
# 1) 进入 ESP-IDF 环境（Windows 示例，路径按本机安装位置替换）
<ESP-IDF>/export.ps1

# 2) 进入固件工程
cd firmware/brookesia_master/examples/system_monitor

# 3) 首次配置（只需一次）
idf.py set-target esp32p4
idf.py gen-bmgr-config -b esp32_p4_wifi6_touch_lcd_7b

# 4) 编译
idf.py build
```

产物是两个镜像：`build/system_monitor.bin`（应用）与 `build/storage.bin`
（16 MB littlefs 资源分区）。

烧录（**只烧 `factory` 与 `storage`**，不要整片擦除，避免破坏出厂 Wi-Fi 校准）：

```powershell
python -m esptool --chip esp32p4 -p COMx -b 460800 write-flash `
  0x110000 build/system_monitor.bin 0x1000000 build/storage.bin
```

工程内也提供了等价的 [`flash_app.ps1`](firmware/brookesia_master/examples/system_monitor/flash_app.ps1)。
需要恢复出厂演示时，把
[`firmware/ESP32-P4-WIFI6-Touch-LCD-7B-FactoryOnly.bin`](firmware/ESP32-P4-WIFI6-Touch-LCD-7B-FactoryOnly.bin)
烧到 `0x0` 即可整片还原。

## 🧪 桌面应用与交互

| 应用 | 说明 |
| --- | --- |
| 系统监视器 | CPU / 内存 / 磁盘 / 网络 / 温度仪表盘，数据来自板载传感器与 `scripts/pc_agent.py` |
| 音乐 | 仅支持 PCM WAV；后台继续播放，离开再回来保留曲目与进度 |
| 相册 | 浏览 `/littlefs/pictures` 与 SD 卡图片，可自动播放、设为壁纸 |
| 视频 | `.mjpeg` / `.mjpg` / `.mjp`，硬件 JPEG 解码，异步换片不卡界面 |
| 设置 | Wi-Fi、蓝牙、显示、声音、时间、语言、设备信息 |

交互约定：

- 任意页面上滑回桌面，按住进入最近任务；最近任务可一键 CLEAR ALL。
- 桌面图标从左往右排列，翻页与滑动跟随手势。
- 视频画面区单击无副作用，**双击暂停 / 继续**；底部控制栏集中放置
  上一部、暂停、下一部、刷新，进度条与时长位于其下一行。
- 音乐与相册的操作按钮同样集中在页面底部并水平居中。
- 应用切到后台时会被挂起（Paused），状态栏左上角最多显示 3 个后台应用图标。

## 📡 无线与蓝牙

- **Wi-Fi**：由板载 ESP32-C6 提供 2.4 GHz Wi-Fi 6，经 ESP-Hosted over SDIO 接入 P4；
  设置页支持扫描、连接、断开，显示 IPv4 与 MAC，掉线后自动重连。
- **蓝牙**：C6 固件只带 BLE 5，支持 GAP 广播与扫描；扫描期间暂停广播，结束后自动恢复。
- C6 固件不包含经典蓝牙，因此 **A2DP / 蓝牙音频不可用**，音频只走板载 ES8311。

## ✅ 已验证与已知限制

已验证：

- `idf.py build` 通过，烧录 `factory + storage` 后可正常启动，6 个 App 全部 installed。
- 桌面图标顶部不再被裁切；上滑 / 按住手势、最近任务、状态栏图标显示正常。
- 视频换片由播放线程异步执行，切换时不阻塞 UI；12 fps 定速播放。
- Wi-Fi 掉线自动重连；BLE 扫描与广播互斥切换正常。

已知限制：

- SD 卡未插入时挂载会失败，此时媒体只读取 `/littlefs` 下的内置素材。
- 视频必须是 MJPEG（无容器、无音轨），可用 [`tools/make_mjpeg.py`](tools/make_mjpeg.py) 生成；
  内置示例素材由 [`tools/make_demo_videos.py`](tools/make_demo_videos.py) 生成。
- 音乐仅支持 PCM WAV。
- 电池电量与实际温度受硬件限制，系统监视器中的 PC 指标需要
  [`scripts/pc_agent.py`](scripts/pc_agent.py) 向 UDP 9999 广播。
- 部分环境编译结束时进程返回码非 0，但日志出现 `Project build complete` 即为成功。

## 📦 固件与分区

分区表见
[`partitions.csv`](firmware/brookesia_master/examples/system_monitor/partitions.csv)：

| 名称 | 类型 | 偏移 | 大小 | 用途 |
| --- | --- | --- | --- | --- |
| `nvsfactory` | data/nvs | `0x9000` | 200 KB | 出厂 Wi-Fi 校准，**不要擦** |
| `nvs` | data/nvs | `0x3B000` | 840 KB | 用户 NVS |
| `otadata` | data/ota | `0x10D000` | 8 KB | 保留（空，引导回落到 factory） |
| `phy_init` | data/phy | `0x10F000` | 4 KB | PHY 校准数据 |
| `factory` | app/factory | `0x110000` | 14.94 MB | 应用镜像 |
| `storage` | data/0x82 | `0x1000000` | 16 MB | littlefs：App 包、字体、壁纸、音乐、视频 |

与官方分区表的差异：删除 `ota_0` / `ota_1` / `assets`，把空间并入 `factory`，
并把 `storage` 移到 16 MB 边界，因此资源分区有充足空间继续放素材。

资源目录（littlefs）：

- `/littlefs/music`、`/sdcard/music`：WAV 音乐。
- `/littlefs/videos`、`/sdcard/videos`：MJPEG 视频。
- `/littlefs/pictures`、`/littlefs/wallpapers`：图片与壁纸候选。
- `/littlefs/system/super/wallpaper.txt`：当前壁纸路径。
- `/littlefs/apps`：各应用的 JSON UI 包与图标。

## 🗂️ 目录结构

```text
docs/                                  项目文档（中英对照）
examples/esp-idf/                      官方 ESP-IDF 示例（板级验证）
examples/arduino/                      官方 Arduino 示例
firmware/brookesia/                    官方 Brookesia BSP 示例
firmware/brookesia_master/            本项目 Brookesia 0.8 源码树
  examples/system_monitor/            桌面固件工程（编译入口）
    components/                       自研 App 与蓝牙服务
    littlefs/                         打包进 storage 分区的资源
    main/                             启动流程与通用服务
scripts/                              辅助脚本（PC 状态广播、示例发现、烧录）
tools/                                素材生成脚本（MJPEG、示例视频、App 封面）
firmware/ESP32-P4-WIFI6-Touch-LCD-7B-FactoryOnly.bin   出厂整片镜像
```

## 📚 文档索引

| 文档 | 内容 |
| --- | --- |
| [`docs/BROOKESIA_7B_ZH.md`](docs/BROOKESIA_7B_ZH.md) | 桌面系统主文档：功能、分区、编译、烧录、素材格式 |
| [`docs/PROJECT_STRUCTURE_ZH.md`](docs/PROJECT_STRUCTURE_ZH.md) | 仓库结构说明 |
| [`docs/GETTING_STARTED_ZH.md`](docs/GETTING_STARTED_ZH.md) | 开发环境与首次上手 |
| [`docs/EXAMPLES_GUIDE_ZH.md`](docs/EXAMPLES_GUIDE_ZH.md) | 板级示例索引与学习顺序 |
| [`docs/firmware_ZH.md`](docs/firmware_ZH.md) | 固件交付与烧录边界 |
| [`docs/CI_ZH.md`](docs/CI_ZH.md) | CI 与仓库检查规则 |
| [`CONTRIBUTING_ZH.md`](CONTRIBUTING_ZH.md) | 贡献指南 |

次要入口（原厂资料与英文版）：

- [`README_EN.md`](README_EN.md)：本页的英文版本，含板卡原始介绍。
- [微雪产品页](https://www.waveshare.com/esp32-p4-wifi6-touch-lcd-7b.htm) 与
  [官方文档站](https://docs.waveshare.com/ESP32-P4-WIFI6-Touch-LCD-7B)。
- [`examples/README_ZH.md`](examples/README_ZH.md)：官方示例总览。
- [`firmware/README_ZH.md`](firmware/README_ZH.md)：固件目录说明。

## 🤝 参与贡献

- 提交前请阅读 [`CONTRIBUTING_ZH.md`](CONTRIBUTING_ZH.md)（或
  [`CONTRIBUTING.md`](CONTRIBUTING.md)），保持中英文文档同步。
- 编译或运行问题请附上 ESP-IDF 版本、`idf.py` 日志片段与复现步骤。
- 板级硬件问题请优先查阅官方文档与 BSP 仓库。

## 📄 许可证

本仓库以 [Apache-2.0](LICENSE) 许可发布；`firmware/brookesia_master/` 内的
上游源码保留其原始许可证与版权声明。
