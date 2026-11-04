<div align="center">
  <h1>ESP32-P4-WIFI6-Touch-LCD-7B</h1>
  <p><strong>A 7-inch ESP-Brookesia desktop on ESP32-P4 + ESP32-C6</strong></p>
  <p>
    <a href="https://github.com/a87150/ESP32-P4-WIFI6-Touch-LCD-7B/actions/workflows/esp-idf-examples.yml"><img alt="Build" src="https://github.com/a87150/ESP32-P4-WIFI6-Touch-LCD-7B/actions/workflows/esp-idf-examples.yml/badge.svg"></a>
    <a href="LICENSE"><img alt="License" src="https://img.shields.io/github/license/a87150/ESP32-P4-WIFI6-Touch-LCD-7B"></a>
  </p>
  <p>
    <a href="README.md">简体中文</a> ·
    English ·
    <a href="https://www.waveshare.com/esp32-p4-wifi6-touch-lcd-7b.htm">🌐 Product page</a> ·
    <a href="https://docs.waveshare.com/ESP32-P4-WIFI6-Touch-LCD-7B">📚 Documentation</a> ·
    <a href="examples/esp-idf/">🧩 ESP-IDF examples</a> ·
    <a href="examples/arduino/">🔧 Arduino examples</a> ·
    <a href="firmware/">📦 Firmware</a>
  </p>
  <img src="assets/ESP32-P4-WIFI6-Touch-LCD-7B-details-1.jpg" alt="ESP32-P4-WIFI6-Touch-LCD-7B" width="720">
</div>

---

> **This page is the secondary English entry.** The primary documentation is
> Chinese-first: [`README.md`](README.md) and
> [`docs/BROOKESIA_7B_ZH.md`](docs/BROOKESIA_7B_ZH.md). Built on top of the
> Waveshare ESP32-P4-WIFI6-Touch-LCD-7B BSP, this repository ships a daily-usable
> **ESP-Brookesia 0.8 desktop** (ESP-IDF 6.1 + LVGL 9): status bar, launcher,
> system monitor, music, gallery, video player and settings (Wi-Fi / BLE), with a
> repartitioned 16 MB resource volume.

## ✨ Project overview

The repository has two parallel tracks:

1. **Main firmware line** `firmware/brookesia_master/`: a vendored ESP-Brookesia
   source tree whose `examples/system_monitor` project builds and packages the
   complete desktop firmware. Daily development, flashing and UI work happen here.
2. **Vendor material**: `examples/esp-idf/`, `examples/arduino/`,
   `firmware/brookesia/` and the full factory image are kept in their original
   shape for board-level verification and comparison.

Software baseline:

| Component | Version / notes |
| --- | --- |
| ESP-IDF | **v6.1** (Brookesia 0.8 accepts `>= 6.0` and `<= 6.2`) |
| ESP-Brookesia | **0.8.x**, vendored as source (upstream `espressif/esp-brookesia` master) |
| GUI | LVGL 9 + `esp_lvgl_adapter` (JSON-described UI with runtime bindings) |
| Board support | managed component `waveshare/esp32_p4_wifi6_touch_lcd_7b` 3.0.1 plus a local 7B board package |
| Wireless | ESP32-C6: 2.4 GHz Wi-Fi 6 + Bluetooth 5 (LE) over ESP-Hosted SDIO |
| Target | `esp32p4` |

Main-line capabilities:

- **Status bar**: centered clock; Wi-Fi signal (runtime-tinted white icon),
  Bluetooth and battery on the right; paused apps on the left.
- **Launcher**: icons flow left to right across pages; swipe up on any page to
  return home, press and hold for recents; recents provide **CLEAR ALL**; the
  wallpaper can be set from the gallery.
- **System monitor**: aggregated disk usage (turns red and raises an alarm at
  90 %), CPU / GPU temperatures in the gauge center (`HIGH!` above 90 °C), and
  DISK I/O on the network card.
- **Music**: WAV player that keeps playing in the background, with seek, volume
  and metadata.
- **Gallery**: previous / next, slideshow, resolution / size details, one-tap
  wallpaper.
- **Video**: hardware-decoded MJPEG at a fixed 12 fps, seek, **double tap on the
  picture to pause / resume**, and a centered control bar at the bottom.
- **Settings**: Wi-Fi scan / connect / disconnect with IP and MAC plus automatic
  reconnect, BLE scan and advertising, display, sound, time, language and device
  info.

Compared with the vendor demo, the main firmware adds a Chinese UI, the status
bar and gesture set, four in-house media / monitoring apps, a Bluetooth settings
page and a 16 MB littlefs resource volume (partition table change, see
"Firmware and partitions").

## 🖥️ Hardware overview

| Feature | Device / interface |
| --- | --- |
| SoC | ESP32-P4NRW32, dual-core RISC-V HP up to 360 MHz plus an LP core |
| Memory | 32 MB PSRAM in package (HEX, 200 MHz), 32 MB NOR flash on board |
| Wireless | ESP32-C6-MINI-1 over SDIO: 2.4 GHz Wi-Fi 6 and Bluetooth 5 (LE) |
| Display | 7-inch 1024 × 600 IPS touch panel over MIPI-DSI (EK79007) |
| Touch | GT911 capacitive controller, five-point touch |
| Camera | MIPI-CSI (2-lane), OV5647 optional |
| Audio | ES8311 codec, ES7210 ADC, dual microphones and speaker header |
| Expansion | microSD, USB 2.0 OTG HS, CAN/TWAI, RS485, I2C, UART, GPIO |
| Backlight | GPIO 32 |
| BSP | managed component `waveshare/esp32_p4_wifi6_touch_lcd_7b` 3.0.1 |
| ESP-IDF target | `esp32p4` |

Notes:

- The logical resolution is 1024 × 600 and layout constants are expressed in
  `dp`; this repository ships dedicated 1024 × 600 layouts for every app (see
  `constants/1024x600.json`).
- Audio only goes through the on-board ES8311; Bluetooth audio is not available
  (see "Wireless and Bluetooth").
- Pin assignments follow the official product documentation and the board
  support package; this repository does not ship a schematic copy.

## 🚀 Getting started

### Prerequisites

- **ESP-IDF v6.1** (Brookesia 0.8 rejects versions outside `>= 6.0, <= 6.2`).
- A USB data cable on the download port; Windows, Linux and macOS all work.
- Network access on the first build to fetch managed components.

### Build

```powershell
# 1) Enter the ESP-IDF environment (Windows example, adapt the path)
<ESP-IDF>/export.ps1

# 2) Enter the firmware project
cd firmware/brookesia_master/examples/system_monitor

# 3) One-time configuration
idf.py set-target esp32p4
idf.py gen-bmgr-config -b esp32_p4_wifi6_touch_lcd_7b

# 4) Build
idf.py build
```

The build produces two images:

- `build/system_monitor.bin`: application image for the `factory` partition.
- `build/storage.bin`: 16 MB littlefs volume (app packages, fonts, samples).

### Flash

Flash **only `factory` and `storage`**; never erase the whole chip, otherwise the
factory Wi-Fi calibration is lost:

```powershell
python -m esptool --chip esp32p4 -p COMx -b 460800 `
  --before default-reset --after hard-reset `
  write-flash 0x110000 build/system_monitor.bin 0x1000000 build/storage.bin
```

[`flash_app.ps1`](firmware/brookesia_master/examples/system_monitor/flash_app.ps1)
wraps the same command:

```powershell
.\flash_app.ps1 -Port COMx
```

Rules:

- Run esptool as a **single serial process**; do not open `idf.py monitor` or a
  second flasher at the same time.
- Do not use `idf.py flash`; it also writes the bootloader and partition table.
- C++ changes only need the `app` image; changes under `littlefs/` only need
  `storage`.
- To restore the vendor demo, write
  [`firmware/ESP32-P4-WIFI6-Touch-LCD-7B-FactoryOnly.bin`](firmware/ESP32-P4-WIFI6-Touch-LCD-7B-FactoryOnly.bin)
  at `0x0`.

### Serial log and acceptance

After flashing, watch the USB-Serial/JTAG port at 115200. A healthy boot installs
all six apps:

```text
SysCore: : Native app installed: id(1), manifest(brookesia.system.super.shell)
SysCore: : Native app installed: id(2), manifest(brookesia.general.settings)
SysCore: : Native app installed: id(3), manifest(brookesia.waveshare.gallery)
SysCore: : Native app installed: id(4), manifest(brookesia.waveshare.music)
SysCore: : Native app installed: id(5), manifest(brookesia.waveshare.system_monitor)
SysCore: : Native app installed: id(6), manifest(brookesia.waveshare.video)
```

### Common build issues

- **A new component is not compiled**: run `idf.py reconfigure` after adding a
  directory under `components/`.
- **Full-size wallpaper does not fit the cache**: make sure
  `CONFIG_LV_CACHE_DEF_SIZE` is large enough (4 MB by default).
- **Non-zero exit code after a successful build**: if the log ends with
  `Project build complete`, the build is valid. Some environments return a
  non-zero process code.

## 🧪 Desktop apps and interactions

| App | Package id | Description |
| --- | --- | --- |
| Desktop shell | `brookesia.system.super.shell` | Status bar, icon pages, recents, wallpaper, gestures |
| Settings | `brookesia.general.settings` | Wi-Fi, Bluetooth, display, sound, time, language, device info |
| System monitor | `brookesia.waveshare.system_monitor` | CPU / memory / disk / network / temperature dashboard |
| Music | `brookesia.waveshare.music` | PCM WAV only, background playback |
| Gallery | `brookesia.waveshare.gallery` | Browsing, slideshow, set wallpaper |
| Video | `brookesia.waveshare.video` | Hardware MJPEG playback, double-tap pause |

### Status bar and launcher

- Centered clock; Wi-Fi, Bluetooth and battery on the right; up to three paused
  app icons on the left.
- Icons flow left to right across pages and follow swipe gestures.
- Swipe up on any page to go home; press and hold to open recents, which offers
  CLEAR ALL.
- Theme (light / dark) and language (zh_CN / en) are switchable in settings.

### System monitor

- Disk usage is aggregated over all volumes and turns red with an alarm at 90 %.
- The gauge center shows **CPU / GPU temperature**; the threshold is
  `TEMP_ALARM_C = 90 °C` and values above it get a `HIGH!` suffix.
- The network card also reports **DISK I/O**.
- On-board sensors cover the local board; PC metrics come from
  [`scripts/pc_agent.py`](scripts/pc_agent.py), which broadcasts CPU / memory /
  disk / GPU / network data to UDP **9999** once per second (GPU needs an NVIDIA
  card through `nvidia-smi`).

### Music

- The app opens on a **track list** (6 rows per page, paged); nothing plays until a
  row is tapped, so entering the app is silent and instant.
- **PCM WAV only**; decoding and playback run on a dedicated thread, so the music
  keeps playing while the app is in the background.
- The progress slider supports seek (applied once every 500 ms so dragging does
  not restart the codec continuously); a separate 0–100 volume slider applies
  immediately.
- The detail line shows track index / total, sample rate, channel layout, bit
  depth, bitrate (sample rate × channels × bit depth) and file size; tracks
  advance automatically.
- Folders: `/littlefs/sounds`, `/sdcard/music`, `/sdcard`.
- The bottom bar holds list, previous, play / pause, next and refresh, centered.
- The cover is 300 dp; the page starts right under the status bar so the player
  gets as much room as possible.

### Gallery

- The app opens on an **overview table** (6 rows per page with file name,
  resolution and size); tapping a row opens the full preview, and the preview bar
  has a "list" button to come back.
- Folders: `/littlefs/pictures`, `/littlefs/wallpapers`, `/sdcard/pictures`,
  `/sdcard`.
- PNG / JPEG are supported; the detail line shows resolution, file size, format
  and color depth (for example `1024x600 · 1.2 MB · PNG · 24-bit`).
- Layout: a near-full-width preview on top, then a bottom bar with details,
  centered action buttons and a status line.
- Previous / next browsing; **slideshow** advances every 4 s; **set wallpaper**
  writes `/littlefs/system/super/wallpaper.txt` and takes effect on the launcher.
- Images reuse one resource id, so switching releases the previous decoded frame
  and long browsing sessions do not leak memory.

### Video

- The app opens on a **movie list** (6 rows per page with file name and size);
  a movie is only read and decoded after a row is tapped.
- Format: `.mjpeg` / `.mjpg` / `.mjp`, i.e. concatenated JPEG frames with no
  container and no audio track.
- Fixed **12 fps** playback (MJPEG carries no timestamps); the next movie starts
  automatically.
- **Double tap on the picture pauses / resumes**: the JSON DSL has no double-click
  event, so `video.tap` merges two clicks inside a 400 ms window.
- Hardware JPEG decoding (`esp_driver_jpeg`) outputs RGB565 with double
  buffering; both loading and switching run asynchronously on the playback worker
  and do not block the UI.
- The stage is 1024 × 524; smaller movies are centered at their native 1:1 size
  (LVGL does not upscale, which keeps the frame rate). The bottom 76 dp bar holds
  centered buttons on the first row and the progress slider plus time on the
  second.
- Folders: `/littlefs/videos`, `/sdcard/videos`, `/sdcard`.

### Settings

- Wi-Fi: scan, connect and disconnect, showing IPv4 and MAC, with automatic
  reconnect after a drop.
- Bluetooth: BLE scan and advertising; advertising pauses during a scan and
  resumes afterwards.
- Display: brightness and theme; sound: volume; time: time zone and SNTP;
  language; device info.

## 📡 Wireless and Bluetooth

- **Wi-Fi**: the on-board ESP32-C6 provides 2.4 GHz Wi-Fi 6 through ESP-Hosted
  over SDIO. Settings supports scan, connect and disconnect, shows the IPv4
  address and MAC, and reconnects automatically after a drop.
- The link is reported as `Connected` as soon as it is established (without
  waiting for DHCP), which avoids the 15 s service state-machine timeout that
  used to make Wi-Fi flap.
- **Bluetooth**: the C6 firmware only ships BLE 5, so GAP advertising and
  scanning are available; advertising pauses during a scan and resumes after.
- Classic Bluetooth is not part of the C6 firmware, therefore **A2DP is not
  available** and audio only plays through the on-board ES8311.

## 📦 XiaoZhi AI (optional firmware)

- **Yes, it can be installed.** The Waveshare wiki's published-firmware table
  marks **ESP32-P4-WIFI6-Touch-LCD-7B as ✅**, and the tutorial provides both a
  ready-made image and an ESP-IDF project to build yourself.
- **It cannot coexist with this desktop.** XiaoZhi and the Brookesia desktop are
  two independent firmwares; the official FAQ states they cannot be switched with
  one click. Flashing XiaoZhi replaces the current app and partition table, so
  coming back requires re-flashing
  `firmware/ESP32-P4-WIFI6-Touch-LCD-7B-FactoryOnly.bin` (at `0x0`) or a full
  factory + storage flash.
- Flashing without a toolchain:
  1. Open the [XiaoZhi AI tutorial](https://www.waveshare.net/wiki/%E5%B0%8F%E6%99%BAAI%E5%BA%94%E7%94%A8%E5%8F%82%E8%80%83%E6%95%99%E7%A8%8B)
     and expand "published firmware" under method one or two;
  2. pick the 7B image and follow the wiki flash guide (it also writes bootloader
     and partition table);
  3. on first boot connect to `Xiaozhi-xxxxxx`, open `http://192.168.4.1` and join
     a 2.4 GHz network;
  4. enter the six-digit code on `https://xiaozhi.me` to activate the device.
- Building from source: download the "XiaoZhi AI Chatbot2" project linked from the
  wiki, select the board and flash; the project already ships the 7B board config.
- Limits: XiaoZhi needs a network connection; it does not read SD card files; and
  using DeepSeek / ChatGPT instead of the default backend requires your own
  server.

## ✅ Verified behaviour and known limits

### Verification log

- `idf.py build` succeeds and a `factory + storage` flash boots with all six apps
  installed.
- Launcher icons are no longer clipped at the top; gestures, recents and status
  bar icons behave as expected.
- Video track switching runs on the playback worker and never blocks the UI;
  playback is fixed at 12 fps.
- Wi-Fi reconnects after a drop; BLE scanning and advertising switch cleanly.
- Background music playback, seek and volume work; gallery slideshow and
  wallpaper work.
- Gallery, music and video all open on a list first and only load/play after a
  row is tapped; long lists are paged six rows at a time.

### Known limits

- **Without an SD card** the boot log prints `sdmmc_init_ocr ... 0x107` /
  `Failed to initialize SD card` and media is read from `/littlefs` only.
- Video must be MJPEG (no container, no audio track); use
  [`tools/make_mjpeg.py`](tools/make_mjpeg.py) to produce it. The bundled samples
  come from [`tools/make_demo_videos.py`](tools/make_demo_videos.py).
- Music supports PCM WAV only.
- Battery level and temperatures depend on the hardware and are indicative only;
  real CPU / GPU temperatures need the PC side.
- Audio cannot be routed to a classic Bluetooth speaker (BLE only on the C6).
- Final color and touch-calibration quality must be checked on the device.

### Log and troubleshooting quick reference

| Log | Meaning / action |
| --- | --- |
| `sdmmc_init_ocr: send_op_cond (1) returned 0x107` | No SD card or unrecognised card; expected noise |
| `swap_xy is not supported` | The MIPI panel does not swap XY; harmless |
| `disp_on_off is not supported` | The panel has no software on/off; harmless |
| `Backlight control GPIO not found` | The 7B board package drives backlight on GPIO 32; this hints at a different board config |
| `System init failed` | A JSON UI package is invalid (commonly `imageProps.src` missing `${image.<id>}`) |
| An app is not installed | The matching `littlefs/apps/<package id>/res/` is incomplete; reflash `storage` |

## 📦 Firmware and partitions

See
[`partitions.csv`](firmware/brookesia_master/examples/system_monitor/partitions.csv):

| Name | Type | Offset | Size | Purpose |
| --- | --- | --- | --- | --- |
| `nvsfactory` | data/nvs | `0x9000` | 200 KB | Factory Wi-Fi calibration, **do not erase** |
| `nvs` | data/nvs | `0x3B000` | 840 KB | User NVS |
| `otadata` | data/ota | `0x10D000` | 8 KB | Reserved (empty, boots factory) |
| `phy_init` | data/phy | `0x10F000` | 4 KB | PHY calibration |
| `factory` | app/factory | `0x110000` | 14.94 MB | Application image |
| `storage` | data/0x82 | `0x1000000` | 16 MB | littlefs: app packages, fonts, wallpaper, music, video |

Compared with the vendor table, `ota_0`, `ota_1` and `assets` are removed and
their space merged into `factory`; `storage` starts at the 16 MB boundary so the
resource volume has room to grow.

Flashing matrix:

| Content | Offset | When |
| --- | --- | --- |
| `build/system_monitor.bin` | `0x110000` | Every C++ change |
| `build/storage.bin` | `0x1000000` | Changes under `littlefs/` (app packages, media) |
| `build/partition_table/partition-table.bin` | `0x8000` | **Only when the partition table changes** |
| Full factory image | `0x0` | Restoring the vendor demo |

littlefs layout:

- `/littlefs/music`, `/sdcard/music`: WAV tracks.
- `/littlefs/videos`, `/sdcard/videos`: MJPEG movies.
- `/littlefs/pictures`, `/littlefs/wallpapers`: photos and wallpaper candidates.
- `/littlefs/system/super/wallpaper.txt`: current wallpaper path.
- `/littlefs/apps`: JSON UI packages and icons for every app.

## 🗂️ Repository layout

```text
docs/                                  Project documentation (Chinese-first, English full text)
examples/esp-idf/                      Vendor ESP-IDF examples (board checks)
examples/arduino/                      Vendor Arduino examples
firmware/brookesia/                    Vendor Brookesia BSP examples
firmware/brookesia_master/            Brookesia 0.8 source tree used here
  examples/system_monitor/            Desktop firmware project (build entry)
    components/                       Project apps and Bluetooth service
    littlefs/                         Resources packed into the storage volume
    main/                             Startup flow and shared services
    partitions.csv                    Custom partition table
  system/brookesia_system_super/      Desktop shell, status bar, recents
  app/brookesia_app_settings/         Settings app (with the Bluetooth page)
  hal/brookesia_hal_boards/           7B board package
scripts/                              Helper scripts (PC telemetry, example discovery)
tools/                                Asset generators (MJPEG, demo videos, app art)
firmware/ESP32-P4-WIFI6-Touch-LCD-7B-FactoryOnly.bin   Full factory image
```

Key files:

- [`firmware/brookesia_master/examples/system_monitor/README.md`](firmware/brookesia_master/examples/system_monitor/README.md):
  firmware project quick reference (apps, board package, assets).
- `components/*/package/res/`: JSON screens and constants for each in-house app;
  the build copies them to `littlefs/apps/<package id>/res/`, so do not edit the
  copied tree.
- [`scripts/pc_agent.py`](scripts/pc_agent.py): PC telemetry broadcaster.
- [`tools/make_app_art.py`](tools/make_app_art.py): regenerates the music cover.

## 📚 Documentation index

| Document | Content |
| --- | --- |
| [`docs/BROOKESIA_7B.md`](docs/BROOKESIA_7B.md) | Desktop system guide: features, partitions, build, flash, media, troubleshooting |
| [`docs/BROOKESIA_7B_ZH.md`](docs/BROOKESIA_7B_ZH.md) | Primary Chinese version of the guide |
| [`docs/PROJECT_STRUCTURE.md`](docs/PROJECT_STRUCTURE.md) | Repository layout |
| [`docs/GETTING_STARTED.md`](docs/GETTING_STARTED.md) | Toolchain and first run |
| [`docs/EXAMPLES_GUIDE.md`](docs/EXAMPLES_GUIDE.md) | Board example index and learning order |
| [`docs/firmware.md`](docs/firmware.md) | Firmware delivery and flashing boundaries |
| [`docs/CI.md`](docs/CI.md) | CI and repository checks |
| [`CONTRIBUTING.md`](CONTRIBUTING.md) | Contribution guide |

Vendor and primary entries:

- [`README.md`](README.md): the primary Chinese page for this project.
- [Waveshare product page](https://www.waveshare.com/esp32-p4-wifi6-touch-lcd-7b.htm)
  and the [official documentation site](https://docs.waveshare.com/ESP32-P4-WIFI6-Touch-LCD-7B).
- [`examples/README.md`](examples/README.md): vendor example overview.
- [`firmware/README.md`](firmware/README.md): vendor firmware folder notes.

## 🤝 Contributing

- Read [`CONTRIBUTING.md`](CONTRIBUTING.md) before opening a pull request.
- Documentation is Chinese-first with English as the secondary text: when you
  change the Chinese body, update the English full text or its summary too.
- For build or runtime issues, include the ESP-IDF version, the relevant
  `idf.py` output and reproduction steps.
- Board-level hardware questions belong to the official documentation and BSP
  repository.

## 📄 License

Released under [Apache-2.0](LICENSE). Upstream sources under
`firmware/brookesia_master/` keep their original license and copyright notices.
