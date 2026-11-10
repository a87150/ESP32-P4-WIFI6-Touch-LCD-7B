# ESP-Brookesia 桌面系统（ESP32-P4-WIFI6-Touch-LCD-7B）

[English full text](BROOKESIA_7B.md) · [项目主页](../README.md)

本仓库在微雪（Waveshare）官方 7B 开发板 BSP 之上，二次开发了一套 **ESP-Brookesia
桌面系统**。本文档是这套系统的主文档：软件基线、功能细节、分区、编译烧录、素材
格式、界面开发约定、排障与已知限制。

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
| 板级支持 | 托管组件 `waveshare/esp32_p4_wifi6_touch_lcd_7b` 3.0.1 + 内置 7B 板级包 |

系统启动后进入 Brookesia 桌面（Super Shell），可在桌面上启动各应用；应用之间可以
后台挂起与恢复，状态栏会显示被挂起的应用。

启动成功的验收标志（串口 115200）是 6 个 App 全部安装：

```text
SysCore: : Native app installed: id(1), manifest(brookesia.system.super.shell)
SysCore: : Native app installed: id(2), manifest(brookesia.general.settings)
SysCore: : Native app installed: id(3), manifest(brookesia.waveshare.gallery)
SysCore: : Native app installed: id(4), manifest(brookesia.waveshare.music)
SysCore: : Native app installed: id(5), manifest(brookesia.waveshare.system_monitor)
SysCore: : Native app installed: id(6), manifest(brookesia.waveshare.video)
```

---

## 2. 目录结构

```text
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
  make_demo_videos.py           生成内置示例视频（976 × 432）
  make_app_art.py               生成音乐 App 的封面图
scripts/
  pc_agent.py                   向开发板广播 PC 状态（UDP 9999），供系统监视器使用
docs/
  BROOKESIA_7B_ZH.md            本文档（中文主文档）
  BROOKESIA_7B.md               英文全文（次要入口）
```

> `firmware/brookesia_master/` 是从 `espressif/esp-brookesia`（master 分支）拷入的源码树，
> 由本仓库统一跟踪。发布时其独立仓库元数据被重命名为 `.git-upstream`（已被
> `.gitignore` 忽略）；如需再向上游同步，先用 `Move-Item .git-upstream .git`
> 把它恢复成独立仓库。

自研组件一览：

| 组件 | 包 id | 说明 |
| --- | --- | --- |
| `brookesia_app_system_monitor` | `brookesia.waveshare.system_monitor` | 系统监视器 |
| `brookesia_app_music` | `brookesia.waveshare.music` | WAV / MP3 音乐播放器 |
| `brookesia_app_gallery` | `brookesia.waveshare.gallery` | 图片浏览 / 壁纸 |
| `brookesia_app_video` | `brookesia.waveshare.video` | MJPEG / MP4 视频播放器 |
| `bt_service` | —（服务，不是 App） | BLE 扫描 / 广播服务 |

---

## 3. 功能

### 3.1 桌面（Super Shell）

- **状态栏**：时间居中；右侧为 Wi-Fi 信号（向上递增的信号条，按 RSSI 分级；未连接时
  显示红色斜线）、蓝牙、电量（USB / BATTERY 供电标签）；左上角显示处于后台（Paused）
  的应用图标，最多 3 个槽位。
- **图标页**：应用图标从左往右排列，支持多页与滑动。
- **手势**：任意页面**快速上滑**回到桌面；**按住**（长按）进入最近任务。
- **最近任务**：卡片式预览，带 **CLEAR ALL** 一键清理。
- **壁纸**：由图片 App 设置，路径记录在 `/littlefs/system/super/wallpaper.txt`，桌面启动时加载。
- **主题 / 语言**：`light` / `dark`，`zh_CN` / `en`，可在设置中切换。

### 3.2 系统监视器（`brookesia.waveshare.system_monitor`）

- 磁盘：**全卷聚合**统计，使用率 ≥ 90 % 变红并报警。
- 环形仪表：圆心同时显示 **CPU / GPU 温度**。
- 网络卡片：除速率外还显示 **DISK I/O**。
- 温度告警阈值 `TEMP_ALARM_C = 90.0`，超限时数值后追加 `HIGH!`。
- 数据来源：板载硬件传感器 + `scripts/pc_agent.py`（每秒向 UDP **9999** 广播
  CPU / 内存 / 磁盘 / GPU / 网络统计；GPU 需要 NVIDIA 显卡与 `nvidia-smi`，
  CPU / 内存等指标建议安装 `psutil`）。

### 3.3 音乐（`brookesia.waveshare.music`）

- 进入 App 直接显示**播放器主界面**，不自动播放；点底部「列表」按钮，右侧会滑出
  宽 320 dp 的**选曲抽屉**，点某一行才开始播放并自动收起，不遮挡主画面。
- 抽屉内每页 5 首（可上一页 / 下一页），行内显示文件名与文件大小，正在播放的一行标记
  「播放中 / 已暂停」。
- 支持 **WAV（PCM）与 MP3（MPEG-1/2/2.5 Layer III）**：WAV 解析 RIFF 块，MP3 由
  minimp3 软解；JSON 描述的 GUI，独立线程解码播放，**离开 App 后继续播放**。
- 进度条可拖动 **seek**（每 500 ms 应用一次，避免拖动风暴重启 codec）；MP3 会预先
  逐帧建立索引（跳过 ID3v2 标签），因此可以直接跳到目标帧，不必全文件扫描。
- 音量改用底部 **「−」「+」按钮**调节（每步 10%，0–100），状态行回显当前百分比。
- 详情区显示：曲目序号 / 总数、采样率、声道、位深、**码率**（WAV 为
  采样率 × 声道 × 位深，MP3 取帧头平均码率）与文件大小；从后台返回时若仍有曲目在播，
  直接回到播放页。
- 界面没有标题栏：整页上方是放大后的播放器卡片（**封面 300 dp**），卡片带纵向渐变
  背景与柔和投影；封面右侧是一条**流动的波形线**（双层描边，约 11 fps），播放时
  随音量起伏、暂停时缓慢低幅波动，卡片底部是进度条与时间。
- 底部为独立控制栏：**「播放 / 暂停」固定在正中**，左侧是刷新 / 音量− / 音量+ / 上一首，
  右侧是下一首 / 连播 / 列表；**「列表」放在最右侧**，紧邻右侧滑出的抽屉。
- **连播**是可选开关（默认关闭）：打开后一首播完自动播放下一首，关闭则停在
  当前曲目（进度归零），按播放从头重放。
- 页面顶部只保留状态栏高度（`pageY = 56dp`），把纵向空间尽量留给内容。
- 目录：`/littlefs/sounds`、`/sdcard/music`、`/sdcard`。

### 3.4 图片（`brookesia.waveshare.gallery`）

- 进入 App 直接显示**第一张图片的大图预览**（只解码这一张，不自动轮播）；
  点底部「列表」按钮，右侧滑出宽 320 dp 的**图片抽屉**，点某一行即切换预览并收起。
- 抽屉内每页 5 张，行内显示文件名、分辨率与大小，右侧标签为格式（PNG / JPEG）。
- 目录：`/littlefs/pictures`、`/littlefs/wallpapers`、`/sdcard/pictures`、`/sdcard`。
- 支持 **PNG / JPEG**；详情行显示分辨率、文件大小、格式与位深，例如
  `1024x600 · 1.2 MB · PNG · 24-bit`（PNG 由 IHDR 的位深 × 颜色通道推算，
  JPEG 由 SOF 的精度 × 分量数推算）。
- 布局：顶部为接近满宽的预览（无标题栏），底部卡片包含
  序号 + 文件名 + 详情、居中的操作按钮与状态行；按钮顺序为
  上一张 / 下一张 / 设为壁纸 / 自动播放 / 刷新 / 列表，**「列表」在最右侧**。
- 上一张 / 下一张浏览；**自动播放**为可选开关（默认关闭）：打开后每 4 秒切一张，
  再点一次停止。
- **设为壁纸**：写入 `/littlefs/system/super/wallpaper.txt`，返回桌面即生效。
- 图片资源使用固定 id 重新注册，切换图片会释放上一张的解码缓存，长时间浏览不漏内存。

### 3.5 视频（`brookesia.waveshare.video`）

- 进入 App 直接显示**播放器主界面**（空态提示），点底部「列表」按钮，
  右侧滑出宽 320 dp 的**选片抽屉**，点某一部才开始载入并播放，抽屉自动收起。
- 抽屉内每页 5 部，行内显示文件名与大小，正在播放的一部标记「播放中」。
- 支持两种格式：
  - `.mjpeg` / `.mjpg` / `.mjp`：**纯 JPEG 串接**，**无容器、无音轨**；
  - `.mp4` / `.m4v` / `.mov`：ISO BMFF 容器 + **H.264/AVC constrained baseline**
    视频。P4 只有硬件 H.264 **编码器**，所以 MP4 由 `esp_h264` 的 tinyH264
    **软解**，再把 I420 转成 RGB565；AAC 音轨会被忽略（MP4 无声）。
- MJPEG 固定 **12 fps** 定速播放（不含时间戳）；MP4 按容器帧率播放，速度受软解
  限制：976 × 432 约 10 fps，720p 约 5 fps。MP4 拖动进度会吸附到最近的 IDR 关键帧。
- 支持暂停 / 上一部 / 下一部 / 拖动进度；
  **连播**为可选开关（默认关闭），打开后一部播完自动接下一部，关闭则停在最后一帧。
- **双击画面暂停 / 继续**（GUI 没有双击事件，由 `video.tap` 在 400 ms 窗口内合并两次点击）。
- 硬件 JPEG 解码（`esp_driver_jpeg`）输出 RGB565，双缓冲逐帧切换。
- 切换影片由播放线程异步完成，点击「上一部 / 下一部」只投递请求，不会卡住界面。
- 显示策略：视频**小于画面区时按原始尺寸 1:1 居中显示**（LVGL 不做缩放，帧率有保证）；
  画面区为 976 × 432（左右各留 24 dp、上下避开状态栏与底栏），超出才等比缩小。
  底部 76 dp 为独立控制栏：上一行是进度条与时长，下一行**「播放 / 暂停」固定在正中**，
  左侧是刷新 / 音量− / 音量+ / 上一部，右侧是下一部 / 连播 / 列表，
  「列表」在最右侧，不遮挡画面。暂停时按钮文字为「播放」，播放时为「暂停」。
  音量按钮控制板载 codec（MJPEG 与 MP4 都没有实际播放的音轨，主要用于后台音乐与
  统一操作习惯）。建议素材直接做成 **976 × 432**，可 1:1 满幅播放且 12 fps 稳定。
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

烧录矩阵：

| 内容 | 偏移 | 何时需要烧 |
| --- | --- | --- |
| `build/system_monitor.bin` | `0x110000` | 每次改 C++ 代码 |
| `build/storage.bin` | `0x1000000` | 改了 `littlefs/` 里的 App 包 / 素材 |
| `build/partition_table/partition-table.bin` | `0x8000` | **仅在修改分区表时** |
| 出厂整片镜像 | `0x0` | 需要恢复出厂演示时 |

整片还原出厂演示：把 `firmware/ESP32-P4-WIFI6-Touch-LCD-7B-FactoryOnly.bin` 烧到 `0x0`。

---

## 5. 编译

### 5.1 前置条件

- **ESP-IDF v6.1**（`>=6.0` 且 `<=6.2`，其他版本 Brookesia 0.8 会拒绝编译）。
- Windows / Linux / macOS 均可；首次编译需要联网拉取组件。

### 5.2 步骤

```powershell
# 1) 进入 IDF 环境（示例：Windows 安装路径）
<ESP-IDF>/export.ps1

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
- 只改 `littlefs/` 下的 JSON / 图片时，构建仍会重新打包 `storage.bin`，不必清理整个 build 目录。
- 改了 `partitions.csv` 才需要重新生成并烧 `partition_table`，其余情况不要动 `0x8000`。

---

## 6. 烧录

**只烧 `factory` 与 `storage` 两个分区**，不要整片烧录，避免破坏出厂 Wi-Fi 校准和分区表：

```powershell
# 一次烧 app + storage（脚本已内置上面的地址）
.\flash_app.ps1 -Port COMx

# 或者手动执行
python -m esptool --chip esp32p4 -p COMx -b 460800 `
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

- 接受 **WAV（PCM）与 MP3（Layer III）**；把文件放进 `/littlefs/sounds`（需重新烧
  `storage`），或放进 SD 卡的 `/music`、根目录（插卡即用）。
- 采样率 / 声道 / 位深 / 码率 / 文件大小会在界面显示，推荐 44.1 kHz / 16 bit / 立体声；
  MP3 由 minimp3 解成 16 bit PCM 后送 codec。
- 界面封面是内置生成的 PNG（**300 × 300**），可用 `python tools/make_app_art.py` 重新生成或替换。
- 曲目抽屉每页 5 首，点击行内任意位置即开始播放并收起抽屉；「刷新」只重新扫描，不会自动播放。
- 首次播放某首 MP3 时会先建立帧索引：跳过 ID3v2 标签，逐个校验 MPEG Layer III 帧头并
  记录偏移与帧长，之后拖动进度可直接跳到目标帧。

### 7.3 图片

- 支持 PNG / JPEG；放到 `/littlefs/pictures`、`/littlefs/wallpapers` 或 SD 卡目录。
- 大图会按预览区等比缩放（`innerAlign: contain`），不会裁切。
- 设为壁纸后写入 `/littlefs/system/super/wallpaper.txt`，桌面下次显示时加载。

### 7.4 视频

```bash
# 把 GIF 或图片文件夹转成 mjpeg（默认 12 fps 定速播放）
python tools/make_mjpeg.py frames/ video.mjpeg --size 976x432 --quality 70
python tools/make_mjpeg.py clip.gif video.mjpeg --size 640x360

# 重新生成内置示例视频
python tools/make_demo_videos.py

# 生成 H.264 baseline 的无声 MP4（与 mjpeg 同源）
ffmpeg -f mjpeg -framerate 12 -i video.mjpeg \
       -c:v libx264 -profile:v baseline -level 3.1 -pix_fmt yuv420p \
       -crf 24 -g 24 -keyint_min 12 -sc_threshold 0 -an \
       -movflags +faststart video.mp4

# 生成音乐 App 用的 MP3
ffmpeg -f lavfi -i "sine=frequency=440:duration=9" -ac 2 -ar 44100 \
       -c:a libmp3lame -b:a 128k sound.mp3
```

- 用 `--size 976x432` 可获得 1:1 满幅 + 流畅的最佳效果。
- 非 976 × 432 的素材会按原始尺寸居中显示（小尺寸留黑边），超过画面区（976 × 432）才缩放。
- MP4 必须是 **H.264 constrained baseline**、8 bit 4:2:0；High profile、10 bit、隔行、
  HEVC / VP9 / AV1 会被拒绝并给出错误提示。建议不超过 720p，且优先 976 × 432：
  软解在 976 × 432 约 10 fps，720p 约 5 fps，分辨率越小越流畅。
- 无 ffmpeg 时，可先用其他工具把视频导出为图片序列，再用 `make_mjpeg.py` 合并。

### 7.5 壁纸

用图片 App 选中图片后点"设为壁纸"即可，系统会把路径写入
`/littlefs/system/super/wallpaper.txt`，下次进入桌面生效。

---

## 8. 界面开发约定

自研 App 的界面全部由 JSON 描述，C++ 只负责数据与动作。改 UI 通常只改 JSON 与常量，
不需要动编译逻辑。

### 8.1 资源包结构

```text
components/brookesia_app_xxx/package/
  res/root.json             资源清单 + 分辨率变体
  res/screens/*.json        屏幕（viewScreen）定义
  res/constants/default.json  默认常量
  res/constants/1024x600.json 7B 屏幕的覆盖常量
  res/images/index.json     静态图片索引
  res/flows/*.json          页面流（页面跳转 / 生命周期）
```

`root.json` 中的 `variants` 按屏幕尺寸追加常量文件，例如：

```json
"variants": [
    {
        "when": "${expr(${env.widthDp} == 1024dp && ${env.heightDp} == 600dp)}",
        "assets": ["constants/1024x600.json"]
    }
]
```

后加载的常量按键覆盖 `default.json`，因此公共键放默认文件，屏幕相关尺寸放变体文件。

### 8.2 布局与样式

- `layout`：`flex` 容器，`flexFlow` / `mainAlign` / `crossAlign` / `gap`。
- `placement`：`flow`（参与布局）、`absolute`（相对父容器左上角）、
  `relative`（可用 `align` 对齐，例如 `TopRight`）；`flexGrow: 1` 吃掉剩余空间。
- `style`：颜色、圆角、边框、内边距、字号、`textAlign`、`clipCorner` 等。
- 单位：尺寸用 `dp`，字号用 `sp`，颜色用 `#RRGGBB`。

### 8.3 绑定与动作

- `bindings` 把控件属性绑到运行时数据，例如
  `"labelProps.text": "track_detail"`。
- C++ 侧用 `context.gui().set_binding_value(path, binding, value)` 更新；
  `path` 是文档内的绝对路径（如 `/music/page/player_card/info/track_detail`）。
- `events` 把控件事件映射到动作名（如 `music.seek`、`gallery.prev`、`video.tap`），
  C++ 侧用 `context.gui().subscribe_actions` 订阅。

### 8.4 图片资源

- 静态图片写 `${image.<id>}`（id 来自 `images/index.json`），不能直接写路径。
- 运行时图片先 `register_image()` 注册（用稳定 id 覆盖旧图以释放内存），
  再通过绑定把 id 赋给 `imageProps.src`。

### 8.5 构建同步

构建会把 `components/*/package/res/` 同步到 `littlefs/apps/<包 id>/res/`，
再整体打包成 `storage.bin`。因此：

- 改界面只烧 `storage` 即可生效；
- 不要直接改 `littlefs/apps/` 下的副本（下次构建会被覆盖）；
- JSON 语义在 App install 阶段校验，写错会在启动日志里出现
  `System init failed` 或对应 App 未 installed。

### 8.6 双击暂停的实现

GUI 只提供 `clicked` 事件，视频 App 的处理方式是：

1. `stage` 设为 `clickable`，`clicked` 触发 `video.tap`；
2. C++ 记录上一次点击时间，400 ms 内第二次点击视为双击，调用 `toggle_playback()`；
3. 单击不做任何操作，因此不会和「上滑 / 按住」手势冲突（Shell 手势来自
   Display 服务的原始触摸事件，与控件点击互不影响）。

---

## 9. 与上游的差异（关键改动）

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

## 10. 性能与内存

- 视频走硬件 JPEG 解码 + RGB565 双缓冲，12 fps 定速；画面区 976 × 432，
  不做放大缩放，避免软缩放吃掉帧率。
- 图片 App 复用固定资源 id，切换图片即释放上一张解码结果；壁纸整图依赖
  `CONFIG_LV_CACHE_DEF_SIZE`（默认 4 MB）。
- 音乐解码在独立线程，GUI 定时器每 500 ms 刷新一次进度，拖动 seek 会合并到一次应用。
- 应用切到后台只停 GUI 绘制，音频与业务线程继续运行（音乐后台播放即依赖此机制）。

---

## 11. 验证清单

上机验收建议按下列顺序走一遍：

1. **启动**：串口出现 6 个 `Native app installed`，无 `System init failed`、无 panic。
2. **桌面**：图标顶部不被裁切；左右翻页；上滑回桌面；按住进最近任务；CLEAR ALL 正常。
3. **状态栏**：时间居中；Wi-Fi 图标随信号强度变色；后台 App 图标出现在左上角。
4. **系统监视器**：磁盘/CPU/内存有数据；环心显示 CPU / GPU 温度；超过 90 °C 出现 `HIGH!`；
   运行 `scripts/pc_agent.py` 后网络卡片有 PC 指标与 DISK I/O。
5. **音乐**：进入即显示播放器（不出声）；点「列表」（按钮排最右）右侧弹出抽屉（每页 5 首），
   点行开始播放并收起；播放 / 暂停（固定在按钮排正中）/ 上下一首 / 拖动进度 / 音量−音量+；
   波形线随播放流动；「连播」默认关，打开后一首播完自动下一首；详情行显示采样率、位深、码率、大小；
   返回桌面后仍在播放。
6. **图片**：进入即显示第一张预览；点「列表」（按钮排最右）右侧弹出抽屉（每页 5 张），
   点行切换预览并收起；上一张 / 下一张；详情行显示分辨率 / 大小 / 格式 / 位深；
   自动播放每 4 秒切换（默认关）；设为壁纸后返回桌面生效。
7. **视频**：进入即显示播放器（空态）；点「列表」（按钮排最右）右侧弹出抽屉（每页 5 部），
   点行才开始载入；双击画面暂停 / 继续；播放 / 暂停固定在按钮排正中（暂停时显示「播放」），
   两侧分布其余按钮；音量−/音量+ 有效；「连播」默认关；上一部 / 下一部切换不卡界面。
8. **设置**：Wi-Fi 扫描连接后显示 IP/MAC，断开后自动重连；BLE 扫描与广播互斥正常。

---

## 12. 排障

### 12.1 日志速查

| 日志 | 含义 / 处理 |
| --- | --- |
| `sdmmc_init_ocr: send_op_cond (1) returned 0x107` | 未插卡或卡不识别，属预期噪声；媒体回落到 `/littlefs` |
| `Failed to initialize SD card` | 同上 |
| `swap_xy is not supported` | 该 MIPI 屏不支持交换 XY，无害 |
| `disp_on_off is not supported` | 面板不支持软件开关屏，无害 |
| `Backlight control GPIO not found` | 板级包用 GPIO 32 背光，出现该提示说明板级配置不是 7B |
| `Version mismatch: Host [2.12.0] > Co-proc [0.0.0]` | ESP-Hosted 版本提示，功能正常时可忽略 |
| `System init failed` | JSON 界面包出错；常见是 `imageProps.src` 没写 `${image.<id>}` |
| 某 App 未 installed | `littlefs/apps/<包 id>/res/` 不完整，重烧 `storage` |

### 12.2 常见故障

- **烧录后黑屏 / 卡在启动**：确认只烧了 `factory + storage`，且 `storage` 与当前固件
  同批构建；分区表改过的话需要同时烧 `0x8000`。
- **Wi-Fi 连上但没网**：先在设置页确认 IP 与 MAC；DHCP 较慢时可直接等待自动重连。
- **视频卡顿**：素材超过 976 × 432 会触发缩放，建议按 `--size 976x432` 重新生成。
  MP4 是软解，比 MJPEG 慢：976 × 432 约 10 fps，720p 约 5 fps。
- **MP4 打不开**：只支持 H.264 constrained baseline、4 字节 NAL 长度的 MP4；
  可用第 7.4 节的 `ffmpeg` 命令重新编码。MP4 的 AAC 音轨会被忽略（无声）。
- **音乐没声音**：确认是 PCM WAV 或 MP3、音量不为 0；A2DP 不可用，蓝牙音箱无法出声。
- **图片打不开**：只支持 PNG / JPEG；文件名过长会被截断显示，但不影响加载。

---

## 13. 已知限制

- **未插 SD 卡时**启动日志会打印 `sdmmc_init_ocr ... 0x107` / `Failed to initialize SD card`，属预期噪声。
- **不支持经典蓝牙 / A2DP**（C6 仅 BLE 5）。
- 视频：MJPEG 为 12 fps 定速、无音轨；MP4（H.264 constrained baseline）为软解，
  976 × 432 约 10 fps、720p 约 5 fps，AAC 音轨会被忽略。MJPEG 越大越吃带宽，
  建议 ≤ 976 × 432。
- 电池电量依赖硬件采样，部分批次读数仅供参考。
- 系统监视器的 CPU/GPU 真实温度需要 PC 端配合（`scripts/pc_agent.py`）。
- 屏幕与触摸的实际观感（颜色、触控校准）需要上机确认。
- 本仓库固件与「小智 AI」固件**互斥**：微雪 wiki 的已发布固件列表已支持本板（✅），
  可以直接刷小智；但刷入会覆盖当前桌面与分区表，官方说明**不能一键切换**，
  想恢复需重新烧 `firmware/ESP32-P4-WIFI6-Touch-LCD-7B-FactoryOnly.bin`（@0x0）。

---

## 14. 相关文档

- [项目主页](../README.md)
- [英文全文](BROOKESIA_7B.md)
- [工程结构](PROJECT_STRUCTURE_ZH.md)
- [固件说明](firmware_ZH.md)
- [示例指南](EXAMPLES_GUIDE_ZH.md)
- [烧录与 CI](CI_ZH.md)
- 固件工程内说明：[`firmware/brookesia_master/examples/system_monitor/README.md`](../firmware/brookesia_master/examples/system_monitor/README.md)

---

## English Overview

This document is Chinese-first; the full English text lives in
[`BROOKESIA_7B.md`](BROOKESIA_7B.md). Quick summary:

- **What it is**: an ESP-Brookesia 0.8 desktop for the Waveshare
  ESP32-P4-WIFI6-Touch-LCD-7B (1024 × 600 MIPI-DSI, GT911 touch, ESP32-C6
  Wi-Fi 6 / BLE over ESP-Hosted SDIO), built with ESP-IDF 6.1 and LVGL 9.
- **Apps**: desktop shell (status bar, launcher, recents, wallpaper), settings
  (Wi-Fi / BLE / display / sound / time / language), system monitor (90 %
  disk alarm, 90 °C temperature alarm, DISK I/O), WAV / MP3 music player with
  background playback, PNG/JPEG gallery with slideshow and wallpaper support,
  and a video player for 12 fps hardware-decoded MJPEG plus software-decoded
  H.264 baseline MP4, both with double-tap pause.
- **Partitions**: `factory` at `0x110000` (14.94 MB) and a 16 MB littlefs
  `storage` volume at `0x1000000`; OTA and assets partitions were removed.
  Flash only these two images (or `flash_app.ps1`), never the whole chip.
- **Build**: `idf.py set-target esp32p4`, `idf.py gen-bmgr-config -b
  esp32_p4_wifi6_touch_lcd_7b`, then `idf.py build` under
  `firmware/brookesia_master/examples/system_monitor`.
- **UI**: screens, layout constants and bindings are JSON files under
  `components/*/package/res/`; the build copies them into `littlefs/apps/` and
  packs them into `storage.bin`.
- **Known limits**: no SD card means littlefs-only media, no classic Bluetooth /
  A2DP, MP4 support is limited to H.264 baseline without audio (software decoded,
  ~10 fps at 976 × 432), and real CPU / GPU temperatures on the monitor need
  `scripts/pc_agent.py` broadcasting to UDP 9999.
