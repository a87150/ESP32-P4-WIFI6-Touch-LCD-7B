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
> 第一次上手先读 [桌面系统主文档](docs/BROOKESIA_7B_ZH.md)。
>
> **English summary** — This repository ships a runnable ESP-Brookesia 0.8 desktop
> for the Waveshare ESP32-P4-WIFI6-Touch-LCD-7B board: status bar, launcher,
> system monitor, WAV / MP3 music player with background playback, image gallery,
> hardware-decoded MJPEG and software-decoded H.264 MP4 video, and a settings
> app with Wi-Fi / BLE. Build with
> `idf.py build` under `firmware/brookesia_master/examples/system_monitor`, then
> flash only `factory (0x110000)` and `storage (0x1000000)`. Full English text:
> [README_EN.md](README_EN.md).

## ✨ 项目概览

本仓库有两条并行的内容线：

1. **主线固件** `firmware/brookesia_master/`：把上游 ESP-Brookesia 源码树固化进来，
   在 `examples/system_monitor` 中实现并打包整套桌面固件。日常开发、烧录、改 UI
   都围绕这条线。
2. **官方资料**：`examples/esp-idf/`、`examples/arduino/`、`firmware/brookesia/`
   与出厂整片镜像，保持原厂形态，用于板级验证与对照。

软件基线：

| 组件 | 版本 / 说明 |
| --- | --- |
| ESP-IDF | **v6.1**（Brookesia 0.8 要求 `>= 6.0` 且 `<= 6.2`） |
| ESP-Brookesia | **0.8.x**，源码形式内置（上游 `espressif/esp-brookesia` master） |
| GUI | LVGL 9 + `esp_lvgl_adapter`（JSON 描述界面，运行时可绑定） |
| 板级支持 | 托管组件 `waveshare/esp32_p4_wifi6_touch_lcd_7b` 3.0.1 + 内置 7B 板级包 |
| 无线 | ESP32-C6：2.4 GHz Wi-Fi 6 + Bluetooth 5 (LE)，ESP-Hosted over SDIO |
| Target | `esp32p4` |

主线能力：

- **状态栏**：时间居中；右侧 Wi-Fi 信号（向上递增的信号条，按 RSSI 分级；未连接时
  显示红色斜线）、蓝牙、电量（USB / BATTERY 供电标签）；左上角显示已挂起到后台的应用图标。
- **桌面**：应用图标从左到右排列、支持多页；任意页面上滑回桌面，按住进入最近任务；
  最近任务支持 **CLEAR ALL**；壁纸可由图片 App 设置。
- **系统监视器**：磁盘全卷聚合使用率（≥ 90 % 变红报警）、环心 CPU / GPU 温度
  （超过 90 °C 显示 `HIGH!` 并报警）、网络卡片附带 DISK I/O。
- **音乐**：WAV / MP3 播放器，离开 App 后继续播放，支持拖动进度、独立音量与元信息。
- **相册**：上一张 / 下一张、自动播放、分辨率与文件大小等详情、一键设为壁纸。
- **视频**：MJPEG（纯 JPEG 串接）硬解 12 fps，以及 MP4（H.264 baseline）软解，
  进度拖动，**双击画面暂停 / 继续**，底部控制栏按钮居中。
- **设置**：Wi-Fi 扫描 / 连接 / 断开（显示 IP、MAC，掉线自动重连）、BLE 扫描与广播、
  显示、声音、时间、语言与设备信息。

与出厂演示固件的差别：出厂固件是原厂 Brookesia 示例，本仓库主线固件新增了中文界面、
状态栏与手势、自研的四个媒体 / 监控 App、蓝牙设置页，以及一套 16 MB 的 littlefs
资源分区（改分区表，见“固件与分区”）。

## 🖥️ 硬件概况

| 功能 | 器件 / 接口 |
| --- | --- |
| 主处理器 | ESP32-P4NRW32，HP 系统双核 RISC-V 最高 360 MHz，并含低功耗单核 |
| 存储 | 封装内 32 MB PSRAM（HEX，200 MHz），板载 32 MB NOR Flash |
| 无线 | ESP32-C6-MINI-1，经 SDIO 提供 2.4 GHz Wi-Fi 6 与 Bluetooth 5 (LE) |
| 显示 | 7 英寸 1024 × 600 IPS 触摸屏，MIPI-DSI（EK79007） |
| 触摸 | GT911 电容触摸控制器，支持五点触控 |
| 摄像头 | MIPI-CSI（2-lane）接口，可选配 OV5647 |
| 音频 | ES8311 编解码器、ES7210 音频 ADC、双麦克风与扬声器接口 |
| 存储与扩展 | microSD、USB 2.0 OTG HS、CAN/TWAI、RS485、I2C、UART 与 GPIO |
| 背光 | GPIO 32 |
| 板级支持 | 托管组件 `waveshare/esp32_p4_wifi6_touch_lcd_7b` 3.0.1 |
| ESP-IDF target | `esp32p4` |

几点说明：

- 屏幕逻辑分辨率 1024 × 600，界面常量以 `dp` 为单位，本仓库已针对 1024 × 600 做了
  专属布局（见各 App 的 `constants/1024x600.json`）。
- 音频只走板载 ES8311，蓝牙音频不可用（详见“无线与蓝牙”）。
- 硬件引脚以官方产品文档与板级支持包为准，仓库内没有原理图副本。

## 🚀 快速开始

### 前置条件

- **ESP-IDF v6.1**（Brookesia 0.8 会拒绝 `> 6.2` 或 `< 6.0` 的版本）。
- USB 数据线连接开发板的下载口，Windows / Linux / macOS 均可。
- 首次编译需要联网拉取托管组件；后续可离线编译。

### 编译

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

产物是两个镜像：

- `build/system_monitor.bin`：应用镜像（`factory` 分区）。
- `build/storage.bin`：16 MB littlefs 资源分区（App 包、字体、示例素材）。

### 烧录

**只烧 `factory` 与 `storage`**，不要整片擦除，避免破坏出厂 Wi-Fi 校准与分区表：

```powershell
python -m esptool --chip esp32p4 -p COMx -b 460800 `
  --before default-reset --after hard-reset `
  write-flash 0x110000 build/system_monitor.bin 0x1000000 build/storage.bin
```

工程内也提供了等价的 [`flash_app.ps1`](firmware/brookesia_master/examples/system_monitor/flash_app.ps1)：

```powershell
.\flash_app.ps1 -Port COMx
```

约定：

- esptool **串行单进程**执行，不要同时开 `idf.py monitor` 或第二个烧录进程。
- 不要用 `idf.py flash`，它会顺带写入 bootloader、分区表等分区。
- 只改 C++ 代码时烧 `app`；只改 `littlefs/` 下的 JSON / 图片 / 素材时烧 `storage`。
- 需要恢复出厂演示时，把
  [`firmware/ESP32-P4-WIFI6-Touch-LCD-7B-FactoryOnly.bin`](firmware/ESP32-P4-WIFI6-Touch-LCD-7B-FactoryOnly.bin)
  烧到 `0x0`。

### 串口日志与验收

烧录后从 USB-Serial/JTAG 口看日志（115200），启动成功的标志是 6 个 App 全部安装：

```text
SysCore: : Native app installed: id(1), manifest(brookesia.system.super.shell)
SysCore: : Native app installed: id(2), manifest(brookesia.general.settings)
SysCore: : Native app installed: id(3), manifest(brookesia.waveshare.gallery)
SysCore: : Native app installed: id(4), manifest(brookesia.waveshare.music)
SysCore: : Native app installed: id(5), manifest(brookesia.waveshare.system_monitor)
SysCore: : Native app installed: id(6), manifest(brookesia.waveshare.video)
```

### 常见编译问题

- **新增组件后不参与编译**：`components/` 下新增目录后先 `idf.py reconfigure`。
- **壁纸整图缓存失败**：确认 `CONFIG_LV_CACHE_DEF_SIZE` 足够大（默认 4 MB）。
- **编译结束出现异常返回码**：只要日志出现 `Project build complete` 即为成功，
  本工程在部分环境下进程返回码非 0，属既有现象。

## 🧪 桌面应用与交互

| 应用 | 包 id | 说明 |
| --- | --- | --- |
| 桌面 Shell | `brookesia.system.super.shell` | 状态栏、图标页、最近任务、壁纸、手势 |
| 设置 | `brookesia.general.settings` | Wi-Fi、蓝牙、显示、声音、时间、语言、设备信息 |
| 系统监视器 | `brookesia.waveshare.system_monitor` | CPU / 内存 / 磁盘 / 网络 / 温度仪表盘 |
| 音乐 | `brookesia.waveshare.music` | PCM WAV / MP3，后台继续播放 |
| 相册 | `brookesia.waveshare.gallery` | 浏览 / 自动播放 / 设为壁纸 |
| 视频 | `brookesia.waveshare.video` | MJPEG 硬解 + MP4（H.264）软解，双击暂停 |

### 状态栏与桌面

- 时间居中；Wi-Fi、蓝牙、电量在右；左上角显示后台应用图标（最多 3 个槽位）。
- 图标页从左往右排列，支持多页与滑动翻页。
- 任意页面**快速上滑**回桌面，**按住**进入最近任务；最近任务可一键 CLEAR ALL。
- 主题（light / dark）与语言（zh_CN / en）可在设置中切换。

### 系统监视器

- 磁盘使用率为**全卷聚合**统计，达到 90 % 变红并触发报警。
- 环形仪表圆心同时显示 **CPU / GPU 温度**，阈值 `TEMP_ALARM_C = 90 °C`，
  超限数值后追加 `HIGH!`。
- 网络卡片除上下行速率外，还显示 **DISK I/O**。
- 板载传感器提供本机数据；PC 侧指标由
  [`scripts/pc_agent.py`](scripts/pc_agent.py) 每秒向 UDP **9999** 广播
  CPU / 内存 / 磁盘 / GPU / 网络数据（GPU 需 NVIDIA 显卡，依赖 `nvidia-smi`）。

### 音乐

- 进入 App 直接显示**播放器主界面**，不会自动出声；点「列表」按钮，右侧滑出
  宽 320 dp 的**选曲抽屉**，点某一首才开始播放并自动收起，主界面始终保留。
- 抽屉内每页 5 首，可翻页；行内显示文件名与大小，当前曲目标「播放中 / 已暂停」。
- 支持 **PCM WAV 与 MP3**；解码与播放跑在独立线程，离开 App 后继续播放。
  首次播放 MP3 时会跳过 ID3v2 标签并逐帧建立索引，拖动进度可直接跳帧。
- 进度条可拖动 seek（每 500 ms 应用一次，避免拖动风暴重启 codec）；
  音量用底部「−」「+」按钮调节（每步 10%），状态行回显百分比。
- 详情区显示序号 / 总数、采样率、声道、位深、码率（WAV 按采样率 × 声道 × 位深，
  MP3 取帧头平均码率）与文件大小。
- 目录：`/littlefs/sounds`、`/sdcard/music`、`/sdcard`。
- 播放器卡片带纵向渐变与柔和投影，封面旁有一条**流动的波形线**（双层描边，约 11 fps，
  播放时随音量起伏，暂停时缓慢低幅波动）。
- 底部为独立控制栏：**「播放 / 暂停」固定在正中**，左侧为刷新、音量−、音量+、上一首，
  右侧为下一首、连播、列表；**「列表」在最右侧**，紧邻右侧抽屉。
- **连播**是可选开关（默认关闭）：打开后一首播完自动下一首，关闭则停在当前曲目。
- 封面 300 dp，页面顶部只留状态栏高度，内容区尽量放大。

### 相册

- 进入 App 直接显示**第一张图片的大图预览**（只解码这一张，不自动轮播）；
  点「列表」按钮，右侧滑出宽 320 dp 的**图片抽屉**，点某一行切换预览并收起。
- 抽屉内每页 5 张，显示文件名、分辨率与大小，右侧标签为格式（PNG / JPEG）。
- 目录：`/littlefs/pictures`、`/littlefs/wallpapers`、`/sdcard/pictures`、`/sdcard`。
- 支持 PNG / JPEG；详情行显示分辨率、文件大小、格式与位深（如 `1024x600 · 1.2 MB · PNG · 24-bit`）。
- 布局：上方为接近满宽的预览，底部为信息 + 居中的操作按钮 + 状态行；
  按钮顺序为上一张、下一张、设为壁纸、自动播放、刷新、列表，**「列表」在最右侧**。
- 上一张 / 下一张浏览；**自动播放**为可选开关（默认关闭），打开后每 4 秒切一张；
  **设为壁纸**写入 `/littlefs/system/super/wallpaper.txt`，返回桌面即生效。
- 图片资源使用固定 id 重新注册，切换图片会释放上一张的解码缓存，长期浏览不漏内存。

### 视频

- 进入 App 直接显示**播放器主界面**（空态提示）；点「列表」按钮，右侧滑出
  宽 320 dp 的**选片抽屉**，点某一部才开始载入播放并收起抽屉。
- 抽屉内每页 5 部，显示文件名与大小，当前影片标「播放中」。
- 支持两种格式：
  - `.mjpeg` / `.mjpg` / `.mjp`：**纯 JPEG 串接**，无容器、无音轨，固定 **12 fps**；
  - `.mp4` / `.m4v` / `.mov`：**H.264/AVC constrained baseline**。P4 只有硬件 H.264
    编码器，所以 MP4 由 `esp_h264` 的 tinyH264 **软解**（I420 → RGB565），
    976 × 432 约 10 fps、720p 约 5 fps；AAC 音轨忽略（无声）；
    拖动进度会吸附到最近的 IDR 关键帧。
- **连播**为可选开关（默认关闭），打开后一部播完自动接下一部，关闭则停在最后一帧。
- **双击画面暂停 / 继续**：GUI 没有双击事件，由 `video.tap` 在 400 ms 窗口内
  合并两次点击。
- 硬件 JPEG 解码（`esp_driver_jpeg`）输出 RGB565，双缓冲逐帧切换；
  切换影片与载入文件由播放线程异步完成，不阻塞界面。
- 画面区 976 × 432，小于画面区的素材按原始尺寸 1:1 居中显示（LVGL 不放大，
  保证帧率）；底部 76 dp 控制栏：第一行进度条，第二行**播放 / 暂停固定在正中**
  （暂停时显示「播放」），左侧为刷新、音量−、音量+、上一部，右侧为下一部、连播、
  列表；「列表」在最右侧（音量按钮控制板载 codec，MJPEG / MP4 都没有实际播放的音轨）。
- 目录：`/littlefs/videos`、`/sdcard/videos`、`/sdcard`。

### 设置

- Wi-Fi：扫描、连接、断开，显示 IPv4 与 MAC，掉线自动重连。
- 蓝牙：BLE 扫描与广播，扫描期间暂停广播，结束后自动恢复。
- 显示：亮度、主题；声音：音量；时间：时区与 SNTP；语言；设备信息。

## 📡 无线与蓝牙

- **Wi-Fi**：由板载 ESP32-C6 提供 2.4 GHz Wi-Fi 6，经 ESP-Hosted over SDIO 接入 P4；
  设置页支持扫描 / 连接 / 断开，显示 IPv4 与 MAC，掉线后自动重连。
- 链路建立即上报 `Connected`（不等待 DHCP），避免服务状态机 15 s 超时导致的
  Wi-Fi 反复抖动。
- **蓝牙**：C6 固件只带 BLE 5，支持 GAP 广播与扫描；扫描期间暂停广播，结束后自动恢复。
- C6 固件不包含经典蓝牙，因此 **A2DP / 蓝牙音频不可用**，音频只走板载 ES8311。

## 📦 小智 AI（可选固件）

- **结论：可以装。** 微雪 wiki 的「已发布固件产品列表」里，
  **ESP32-P4-WIFI6-Touch-LCD-7B 明确标注为 ✅**，既有现成固件，也有可编译的 ESP-IDF 工程。
- **但不能与本桌面共存**：小智和本项目的 Brookesia 桌面是两个独立固件，
  官方 FAQ 明确「不能一键切换」。刷小智会覆盖当前 app 与分区表；
  想回到本仓库，需要重新烧 `firmware/ESP32-P4-WIFI6-Touch-LCD-7B-FactoryOnly.bin`（@0x0）
  或完整的 factory + storage。
- 无开发环境刷写：
  1. 打开[小智 AI 应用参考教程](https://www.waveshare.net/wiki/%E5%B0%8F%E6%99%BAAI%E5%BA%94%E7%94%A8%E5%8F%82%E8%80%83%E6%95%99%E7%A8%8B)，
     在「方式一 / 方式二」中展开「已发布固件地址」；
  2. 找到 7B 对应固件，按 wiki 的 Flash 教程烧录（会一并写入 bootloader 与分区表）；
  3. 首次开机连接 `Xiaozhi-xxxxxx` 热点，浏览器打开 `http://192.168.4.1` 配网（仅 2.4 GHz）；
  4. 在 `https://xiaozhi.me` 后台输入设备播报的 6 位验证码完成激活。
- 自行编译：下载 wiki 中的「XiaoZhi AI Chatbot2」工程，选定设备后编译烧录；
  工程自带 7B 板级配置，无需改本项目代码。
- 已知限制：小智必须联网（不能离线）；目前不识别 SD 卡文件；
  想接 DeepSeek / ChatGPT 等其他模型需要自建服务端。

## ✅ 已验证与已知限制

### 验证记录

- `idf.py build` 通过；烧录 `factory + storage` 后可正常启动，6 个 App 全部 installed。
- 桌面图标顶部不再被裁切；上滑 / 按住手势、最近任务、状态栏图标显示正常。
- 视频换片由播放线程异步执行，切换时不阻塞 UI；MJPEG 12 fps 定速。
- Wi-Fi 掉线自动重连；BLE 扫描与广播互斥切换正常。
- 音乐后台播放、进度拖动、音量调节正常；相册自动播放与设为壁纸正常。
- MP3 软解、MP4（H.264 baseline）软解与上屏已验证：内置 976 × 432 MP4
  48 帧全部解出，MP3 347 帧全部解出。
- 相册 / 音乐 / 视频进入即显示主界面（不自动播放），点「列表」在右侧弹出抽屉，
  点选后才切换或播放，抽屉每页 5 项；主画面不被替换。

### 已知限制

- **未插 SD 卡时**启动日志会打印 `sdmmc_init_ocr ... 0x107` /
  `Failed to initialize SD card`，此时媒体只读取 `/littlefs` 下的内置素材。
- 视频支持 MJPEG（无容器、无音轨，硬件解码，可用
  [`tools/make_mjpeg.py`](tools/make_mjpeg.py) 生成）与 MP4（H.264 constrained
  baseline，软解无声，AAC 音轨忽略）；内置示例素材由
  [`tools/make_demo_videos.py`](tools/make_demo_videos.py) 生成，MP4 / MP3 示例见
  [桌面系统主文档](docs/BROOKESIA_7B_ZH.md) 第 7.4 节的 ffmpeg 命令。
- MP4 软解速度：976 × 432 约 10 fps，720p 约 5 fps；分辨率越小越流畅。
- 音乐支持 PCM WAV 与 MP3。
- 电池电量与实际温度受硬件限制，读数仅供参考；CPU / GPU 真实温度需要 PC 端配合。
- 无法把音频投到经典蓝牙音箱（C6 仅 BLE）。
- 屏幕与触摸的最终观感（颜色、触控校准）需要上机确认。

### 日志与排障速查

| 日志 | 含义 / 处理 |
| --- | --- |
| `sdmmc_init_ocr: send_op_cond (1) returned 0x107` | 未插卡或卡不识别，属预期噪声 |
| `swap_xy is not supported` | 该 MIPI 屏不支持交换 XY，无害 |
| `disp_on_off is not supported` | 面板不支持软件开关屏，无害 |
| `Backlight control GPIO not found` | 板级包用 GPIO 32 背光，出现该提示说明用了其他板级配置 |
| `System init failed` | JSON 界面包出错（常见于 `imageProps.src` 未写 `${image.<id>}`） |
| 某 App 未 installed | 对应 `littlefs/apps/<包 id>/res/` 不完整，重烧 `storage` |

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

烧录矩阵：

| 内容 | 偏移 | 何时需要烧 |
| --- | --- | --- |
| `build/system_monitor.bin` | `0x110000` | 每次改 C++ 代码 |
| `build/storage.bin` | `0x1000000` | 改了 `littlefs/` 里的 App 包 / 素材 |
| `build/partition_table/partition-table.bin` | `0x8000` | **仅在修改分区表时** |
| 出厂整片镜像 | `0x0` | 需要恢复出厂演示时 |

资源目录（littlefs）：

- `/littlefs/sounds`、`/sdcard/music`：WAV / MP3 音乐。
- `/littlefs/videos`、`/sdcard/videos`：MJPEG / H.264 baseline MP4 视频。
- `/littlefs/pictures`、`/littlefs/wallpapers`：图片与壁纸候选。
- `/littlefs/system/super/wallpaper.txt`：当前壁纸路径。
- `/littlefs/apps`：各应用的 JSON UI 包与图标。

## 🗂️ 目录结构

```text
docs/                                  项目文档（中文为主，英文全文作为次要入口）
examples/esp-idf/                      官方 ESP-IDF 示例（板级验证）
examples/arduino/                      官方 Arduino 示例
firmware/brookesia/                    官方 Brookesia BSP 示例
firmware/brookesia_master/            本项目 Brookesia 0.8 源码树
  examples/system_monitor/            桌面固件工程（编译入口）
    components/                       自研 App 与蓝牙服务
    littlefs/                         打包进 storage 分区的资源
    main/                             启动流程与通用服务
    partitions.csv                    自定义分区表
  system/brookesia_system_super/      桌面 Shell、状态栏、最近任务
  app/brookesia_app_settings/         设置 App（含蓝牙页）
  hal/brookesia_hal_boards/           7B 板级包
scripts/                              辅助脚本（PC 状态广播、示例发现）
tools/                                素材生成脚本（MJPEG、示例视频、App 封面）
firmware/ESP32-P4-WIFI6-Touch-LCD-7B-FactoryOnly.bin   出厂整片镜像
```

关键文件：

- [`firmware/brookesia_master/examples/system_monitor/README.md`](firmware/brookesia_master/examples/system_monitor/README.md)：
  固件工程速查（App 清单、板级包、资源目录）。
- `components/*/package/res/`：各自研 App 的 JSON 界面与常量；构建时会同步到
  `littlefs/apps/<包 id>/res/`，不要手改同步后的副本。
- [`scripts/pc_agent.py`](scripts/pc_agent.py)：PC 指标广播。
- [`tools/make_app_art.py`](tools/make_app_art.py)：重新生成音乐封面。

## 📚 文档索引

| 文档 | 内容 |
| --- | --- |
| [`docs/BROOKESIA_7B_ZH.md`](docs/BROOKESIA_7B_ZH.md) | 桌面系统主文档：功能、分区、编译、烧录、素材格式、排障 |
| [`docs/BROOKESIA_7B.md`](docs/BROOKESIA_7B.md) | 主文档的英文全文（次要入口） |
| [`docs/PROJECT_STRUCTURE_ZH.md`](docs/PROJECT_STRUCTURE_ZH.md) | 仓库结构说明 |
| [`docs/GETTING_STARTED_ZH.md`](docs/GETTING_STARTED_ZH.md) | 开发环境与首次上手 |
| [`docs/EXAMPLES_GUIDE_ZH.md`](docs/EXAMPLES_GUIDE_ZH.md) | 板级示例索引与学习顺序 |
| [`docs/firmware_ZH.md`](docs/firmware_ZH.md) | 固件交付与烧录边界 |
| [`docs/CI_ZH.md`](docs/CI_ZH.md) | CI 与仓库检查规则 |
| [`CONTRIBUTING_ZH.md`](CONTRIBUTING_ZH.md) | 贡献指南 |

原厂资料与英文入口：

- [`README_EN.md`](README_EN.md)：本页的英文版本，含板卡原始介绍。
- [微雪产品页](https://www.waveshare.com/esp32-p4-wifi6-touch-lcd-7b.htm) 与
  [官方文档站](https://docs.waveshare.com/ESP32-P4-WIFI6-Touch-LCD-7B)。
- [`examples/README_ZH.md`](examples/README_ZH.md)：官方示例总览。
- [`firmware/README_ZH.md`](firmware/README_ZH.md)：固件目录说明。

## 🤝 参与贡献

- 提交前请阅读 [`CONTRIBUTING_ZH.md`](CONTRIBUTING_ZH.md)（或
  [`CONTRIBUTING.md`](CONTRIBUTING.md)）。
- 仓库文档以中文为主、英文为辅：新增或修改中文正文时，同步更新英文全文或文末摘要。
- 编译或运行问题请附上 ESP-IDF 版本、`idf.py` 日志片段与复现步骤。
- 板级硬件问题请优先查阅官方文档与 BSP 仓库。

## 📄 许可证

本仓库以 [Apache-2.0](LICENSE) 许可发布；`firmware/brookesia_master/` 内的
上游源码保留其原始许可证与版权声明。
