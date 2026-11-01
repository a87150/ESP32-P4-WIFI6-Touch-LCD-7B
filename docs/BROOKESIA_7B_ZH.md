# ESP-Brookesia 桌面系统（ESP32-P4-WIFI6-Touch-LCD-7B）

[English](BROOKESIA_7B.md)

本仓库在微雪（Waveshare）官方 7B 开发板 BSP 之上，二次开发了一套 **ESP-Brookesia
桌面系统**。本文档说明它的软件基线、功能、分区、编译与烧录方式、素材格式以及已知限制。

> 本仓库同时保留官方内容（`examples/`、`firmware/brookesia/`、出厂固件等）；
> **本文档描述的是 `firmware/brookesia_master/` 这条主线**。

---

## 1. 概览

| 项目 | 说明 |
| --- | --- |
| 主芯片 | ESP32-P4NRW32，双核 RISC-V 最高 360 MHz + 低功耗核 |
| 内存 / 存储 | 封装内 32 MB PSRAM（HEX，200 MHz）、板载 32 MB NOR Flash |
| 屏幕 | 7 英寸 1024 × 600 IPS，MIPI-DSI（EK79007） |
| 触摸 | GT911 电容触摸，五点 |
| 无线 | 板载 ESP32-C6：2.4 GHz Wi-Fi 6 + Bluetooth 5 (LE)，经 ESP-Hosted over SDIO 接入 P4 |
| 音频 | ES8311 编解码器 + 扬声器接口 |
| ESP-IDF | **v6.1**（Brookesia 0.8 要求 `>=6.0, <=6.2`） |
| ESP-Brookesia | **0.8.x**，以源码形式内置在 `firmware/brookesia_master/`（上游 `espressif/esp-brookesia` master） |
| GUI | LVGL 9 + `esp_lvgl_adapter` |

系统启动后进入 Brookesia 桌面（Super Shell），可在桌面上启动各应用；应用之间可以后台
挂起与恢复，状态栏会显示被挂起的应用。

---

## 2. 目录结构

```
firmware/
  brookesia/                    官方 BSP 示例（registry esp-brookesia 0.5.0，工厂演示同源）
  brookesia_master/             ★ 本项目的 Brookesia 0.8 源码树（vendored）
    hal/brookesia_hal_boards/boards/waveshare/esp32_p4_wifi6_touch_lcd_7b/
    app/brookesia_app_settings/ 设置 App（新增蓝牙页面）
    system/brookesia_system_super/  桌面 Shell、状态栏、最近任务、壁纸
    examples/system_monitor/    ★ 本项目的固件工程（编译入口）
      components/               ★ 自研 App（system_monitor / music / gallery / video + bt_service）
      littlefs/                 打包进 storage 分区的资源（App 包、字体、图片、音乐、视频）
      main/                     启动流程、显示与通用服务
      partitions.csv            自定义分区表
      flash_app.ps1             只烧 app + storage 的脚本
  backups/                      分区表备份（原始 / 新版）
  ESP32-P4-WIFI6-Touch-LCD-7B-FactoryOnly.bin  出厂固件整片镜像（32 MB）
tools/
  make_mjpeg.py                 把 GIF / 图片序列转成 .mjpeg
  make_demo_videos.py           生成内置示例视频（1024 × 524）
scripts/
  pc_agent.py                   向开发板广播 PC 状态（UDP 9999），供系统监视器使用
docs/
  BROOKESIA_7B_ZH.md            本文档
```

> `firmware/brookesia_master/` 是从 `espressif/esp-brookesia`（master 分支）拷入的源码树，
> 由本仓库统一跟踪。发布时其独立仓库元数据被重命名为 `.git-upstream`（已被
> `.gitignore` 忽略）；如需再向上游同步，先用 `Move-Item .git-upstream .git`
> 把它恢复成独立仓库。

---

## 3. 功能

### 3.1 桌面（Super Shell）

- **状态栏**：时间居中；右侧为 Wi-Fi 信号（纯白图标，运行时按信号强度重着色）、蓝牙、
  电量；左上角显示处于后台（Paused）的应用图标，最多 3 个槽位。
- **图标页**：应用图标从左往右排列，支持多页与滑动。
- **手势**：任意页面**快速上滑**回到桌面；**按住**（长按）进入最近任务。
- **最近任务**：卡片式预览，带 **CLEAR ALL** 一键清理。
- **壁纸**：由图片 App 设置，路径记录在 `/littlefs/system/super/wallpaper.txt`，桌面启动时加载。
- **主题 / 语言**：`light` / `dark`，`zh_CN` / `en`，可在设置中切换。

### 3.2 系统监视器（`brookesia.waveshare.system_monitor`）

- 磁盘：**全卷聚合**统计，使用率 ≥ 90% 变红并报警。
- 环形仪表：圆心同时显示 **CPU / GPU 温度**。
- 网络卡片：除速率外还显示 **DISK I/O**。
- 温度告警阈值 `TEMP_ALARM_C = 85.0`，超限时数值后追加 `HIGH!`。
- 数据来源：板载硬件传感器 + `scripts/pc_agent.py`（每秒向 UDP **9999** 广播
  CPU / 内存 / 磁盘 / GPU / 网络统计）。

### 3.3 音乐（`brookesia.waveshare.music`）

- 仅支持 **WAV**（PCM），JSON 描述的 GUI，独立线程解码播放，**离开 App 后继续播放**。
- 进度条可拖动 **seek**（每 500 ms 应用一次，避免拖动风暴重启 codec）。
- 独立 **音量滑条 0–100**，实时生效。
- 元信息显示采样率 / 声道 / 位深；显示当前曲目序号与总数。
- 一首播完自动下一首；重新进入 App 保留上一首与播放位置。
- 目录：`/littlefs/sounds`、`/sdcard/music`、`/sdcard`。

### 3.4 图片（`brookesia.waveshare.gallery`）

- 目录：`/littlefs/pictures`、`/littlefs/wallpapers`、`/sdcard/pictures`、`/sdcard`。
- 上一张 / 下一张浏览；显示文件名、序号、**分辨率与文件大小**。
- **自动播放**：每 4 秒切一张（按钮与"刷新"并排）。
- **设为壁纸**：写入 `/littlefs/system/super/wallpaper.txt`，返回桌面即生效。
- 图片资源使用固定 id 重新注册，切换图片会释放上一张的解码缓存，长时间浏览不漏内存。

### 3.5 视频（`brookesia.waveshare.video`）

- 格式：**纯 JPEG 串接**的 `.mjpeg` / `.mjpg` / `.mjp`，**无容器、无音轨**。
- 固定 **12 fps** 定速播放（MJPEG 不含时间戳），播完自动下一部，支持暂停 / 上一部 / 下一部 / 拖动进度。
- 硬件 JPEG 解码（`esp_driver_jpeg`）输出 RGB565，双缓冲逐帧切换。
- 显示策略：视频**小于画面区时按原始尺寸 1:1 居中显示**（LVGL 不做缩放，帧率有保证）；
  超过 1024 × 524 才等比缩小。建议素材直接做成 **1024 × 524**，可满屏且 12 fps 稳定。
- 目录：`/littlefs/videos`、`/sdcard/videos`、`/sdcard`。
- 生成工具见第 7 节。

### 3.6 设置（`brookesia.general.settings`）

- Wi-Fi：扫描 / 连接 / 断开，显示 **IP、MAC**，掉线**自动重连**。
- 蓝牙：BLE 扫描与广播（关闭扫描后自动恢复广播）。
- 显示：亮度 / 主题；声音：音量；时间：时区与 SNTP；语言；设备信息。

### 3.7 蓝牙

- 板载 C6 直接提供 **BLE 5**（GAP 广播 + 扫描），不经经典蓝牙。
- 由于 C6 固件只带 BLE，**不支持经典蓝牙 / A2DP**，因此无法把音频投到蓝牙音箱。

---

## 4. 分区表

`firmware/brookesia_master/examples/system_monitor/partitions.csv`：

| 名称 | 类型 | 偏移 | 大小 | 用途 |
| --- | --- | --- | --- | --- |
| `nvsfactory` | data/nvs | `0x9000` | 200 KB | 出厂 Wi-Fi 校准，**不要动** |
| `nvs` | data/nvs | `0x3B000` | 840 KB | 用户 NVS |
| `otadata` | data/ota | `0x10D000` | 8 KB | 保留（空，引导回落到 factory） |
| `phy_init` | data/phy | `0x10F000` | 4 KB | PHY 校准数据 |
| `factory` | app/factory | `0x110000` | 14.94 MB | 应用镜像 |
| `storage` | data/0x82 | `0x1000000` | 16 MB | littlefs：App 包、字体、壁纸、音乐、视频 |

与官方表的差异：**删除了 `ota_0` / `ota_1` / `assets`**，把空间并入 `factory`；
`storage` 从 `0x1AF0000` 移到 16 MB 边界 `0x1000000`。改动前一定要先备份
`0x8000` 处的分区表：

- 原始：`firmware/backups/partition-table-0x8000-original.bin`
- 当前：`firmware/backups/partition-table-0x8000-new.bin`

整片还原出厂演示：把 `firmware/ESP32-P4-WIFI6-Touch-LCD-7B-FactoryOnly.bin` 烧到 `0x0`。

---

## 5. 编译

### 5.1 前置条件

- **ESP-IDF v6.1**（`>=6.0` 且 `<=6.2`，其他版本 Brookesia 0.8 会拒绝编译）。
- Windows / Linux / macOS 均可；首次编译需要联网拉取组件。

### 5.2 步骤

```powershell
# 1) 进入 IDF 环境（示例：Windows 安装路径）
C:\esp\v6.1\esp-idf\export.ps1

# 2) 进入固件工程
cd firmware\brookesia_master\examples\system_monitor

# 3) 首次配置（只需一次）
idf.py set-target esp32p4
idf.py gen-bmgr-config -b esp32_p4_wifi6_touch_lcd_7b

# 4) 编译
idf.py build
```

产物：`build/system_monitor.bin`（应用）与 `build/storage.bin`（16 MB littlefs 镜像）。

### 5.3 常见问题

- **新增组件（`components/xxx`）后必须先 `idf.py reconfigure`**，否则 IDF 不会编译它。
- 本项目要求 `CONFIG_LV_CACHE_DEF_SIZE` 足够大（默认 4 MB），否则 1024 × 600 壁纸无法整图缓存。
- 编译结束打印 `Project build complete` 即为成功（本工程在部分环境下进程返回码非 0，可忽略）。

---

## 6. 烧录

**只烧 `factory` 与 `storage` 两个分区**，不要整片烧录，避免破坏出厂 Wi-Fi 校准和分区表：

| 内容 | 偏移 | 何时需要烧 |
| --- | --- | --- |
| `build/system_monitor.bin` | `0x110000` | 每次改代码 |
| `build/storage.bin` | `0x1000000` | 改了 `littlefs/` 里的 App 包 / 素材 |
| `build/partition_table/partition-table.bin` | `0x8000` | **仅在修改分区表时** |
| 出厂整片镜像 | `0x0` | 需要恢复出厂演示时 |

```powershell
# 一次烧 app + storage（脚本已内置上面的地址）
.\flash_app.ps1 -Port COM3

# 或者手动执行
python -m esptool --chip esp32p4 -p COM3 -b 460800 `
    --before default-reset --after hard-reset `
    write-flash 0x110000 build\system_monitor.bin 0x1000000 build\storage.bin
```

> esptool 请**串行单进程**执行（不要同时开 monitor 或多个烧录进程），
> 也不要用 `idf.py flash`（它会顺带写入 bootloader 等分区）。

日志：USB-Serial/JTAG 口，115200 波特率。

---

## 7. 素材

### 7.1 littlefs 布局（`examples/system_monitor/littlefs/`）

| 路径 | 内容 |
| --- | --- |
| `apps/<package id>/res/` | 运行时 App 的 JSON 界面包（编译时自动生成，勿手改） |
| `system/super/shell/` | 桌面 Shell 的界面包（图标、状态栏、最近任务、主题） |
| `system/super/wallpaper.txt` | 当前壁纸路径（由图片 App 写入；文件不存在则用默认背景） |
| `system/fonts/` | `NotoSansSC-Regular.subset.ttf`（中文子集）与 `Telex-Regular.ttf` |
| `pictures/` | 内置示例图片 |
| `sounds/` | 内置示例音频 |
| `videos/` | 内置示例视频 |

### 7.2 音乐

- 只接受 **WAV（PCM）**；把文件放进 `/littlefs/sounds`（需重新烧 `storage`），
  或放进 SD 卡的 `/music`、根目录（插卡即用）。
- 采样率 / 声道 / 位深会在界面显示，推荐 44.1 kHz / 16 bit。

### 7.3 视频

```bash
# 把 GIF 或图片文件夹转成 mjpeg（默认 12 fps 定速播放）
python tools/make_mjpeg.py frames/ video.mjpeg --size 1024x524 --quality 70
python tools/make_mjpeg.py clip.gif video.mjpeg --size 640x360

# 重新生成内置示例视频
python tools/make_demo_videos.py
```

- 用 `--size 1024x524` 可获得满屏 + 流畅的最佳效果。
- 非 1024 × 524 的素材会按原始尺寸居中显示（小尺寸留黑边），超过画面区才缩放。
- 无 ffmpeg 时，可先用其他工具把视频导出为图片序列，再用 `make_mjpeg.py` 合并。

### 7.4 壁纸

用图片 App 选中图片后点"设为壁纸"即可，系统会把路径写入
`/littlefs/system/super/wallpaper.txt`，下次进入桌面生效。

---

## 8. 与上游的差异（关键改动）

| 位置 | 改动 |
| --- | --- |
| `hal/brookesia_hal_boards/.../esp32_p4_wifi6_touch_lcd_7b/` | 新增 7B 板级包：1024 × 600 MIPI-DSI、GT911、背光 GPIO 32、HEX PSRAM 200 MHz、C6 SDIO 引脚（RST 54 / CLK 18 / CMD 19 / D0–D3 14–17，40 MHz 4-bit） |
| `hal/brookesia_hal_adaptor/src/wifi/backend.cpp` | **链路建立即上报 Connected**（原本要等 DHCP 拿到 IP，会撞上服务状态机 15 s 超时，表现为 Wi-Fi 反复抖动） |
| `system/brookesia_system_super/` | 状态栏（时间居中 / Wi-Fi 强度图标 / 蓝牙 / 电量 / 后台 App 图标）、图标页从左往右、上滑回桌面、长按进最近任务、CLEAR ALL、壁纸加载 |
| `system/brookesia_system_core/src/app/manager.cpp` | App 回首页后重开、pause/resume 生命周期修正 |
| `app/brookesia_app_settings/` | 新增蓝牙（BLE 扫描/广播）页面，Wi-Fi 页显示 IP/MAC 并自动重连 |
| `examples/system_monitor/components/` | 自研 App：系统监视器、音乐、图片、视频，以及 BLE 服务 |
| `examples/system_monitor/partitions.csv` | 删除 OTA / assets，`factory` 扩到 14.94 MB，`storage` 移到 `0x1000000` |

---

## 9. 已知限制

- **未插 SD 卡时**启动日志会打印 `sdmmc_init_ocr ... 0x107` / `Failed to initialize SD card`，属预期噪声。
- `swap_xy is not supported` / `disp_on_off is not supported`：该 MIPI 屏不支持这两个调用，无害。
- **不支持经典蓝牙 / A2DP**（C6 仅 BLE 5）。
- 视频 12 fps 定速、无音轨；MJPEG 越大越吃带宽，建议 ≤ 1024 × 524。
- 电池电量依赖硬件采样，部分批次读数仅供参考。
- 系统监视器的 CPU/GPU 真实温度需要 PC 端配合（`scripts/pc_agent.py`）。
- 屏幕与触摸的实际观感（颜色、触控校准）需要上机确认。

---

## 10. 相关文档

- [工程结构](PROJECT_STRUCTURE_ZH.md)
- [固件说明](firmware_ZH.md)
- [示例指南](EXAMPLES_GUIDE_ZH.md)
- [烧录与 CI](CI_ZH.md)
- 固件工程内说明：[`firmware/brookesia_master/examples/system_monitor/README.md`](../firmware/brookesia_master/examples/system_monitor/README.md)
