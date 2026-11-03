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

> This page is the secondary English entry point. Built on top of the Waveshare
> ESP32-P4-WIFI6-Touch-LCD-7B BSP, this repository ships a daily-usable
> **ESP-Brookesia 0.8 desktop** (ESP-IDF 6.1 + LVGL 9): status bar, launcher,
> system monitor, music, gallery, video player and settings (Wi-Fi / BLE), with a
> repartitioned 16 MB resource volume. The Chinese page is the primary entry:
> [`README.md`](README.md). Deep dive:
> [`docs/BROOKESIA_7B.md`](docs/BROOKESIA_7B.md).

## ✨ Project overview

The main line of this repository is `firmware/brookesia_master/`, a vendored
ESP-Brookesia 0.8 source tree whose `examples/system_monitor` project builds the
complete desktop firmware. The vendor `examples/`, factory firmware and Arduino
examples are kept as board-level references.

Highlights:

- **Status bar**: centered clock; Wi-Fi signal (runtime-tinted), Bluetooth and
  battery on the right; up to three paused apps on the left.
- **Launcher**: icons laid out left to right across pages; swipe up anywhere to
  return home, press and hold for recents; recents provide **CLEAR ALL**; the
  wallpaper can be set from the gallery.
- **System monitor**: aggregated disk usage (turns red and raises an alarm at
  90%), CPU / GPU temperatures in the gauge center (`HIGH!` above 90 °C), and
  DISK I/O on the network card.
- **Music**: WAV player that keeps playing in the background, with seek, volume
  and metadata.
- **Gallery**: previous / next, slideshow, resolution and size, one-tap
  wallpaper.
- **Video**: hardware-decoded MJPEG at a fixed 12 fps, seek, **double tap on the
  picture to pause / resume**, and a centered control bar at the bottom.
- **Settings**: Wi-Fi scan / connect / disconnect with IP and MAC plus automatic
  reconnect, BLE scan and advertising, display, sound, time, language and device
  info.

## 🖥️ Hardware overview

| Feature | Device / interface |
| --- | --- |
| SoC | ESP32-P4NRW32, dual-core RISC-V HP up to 360 MHz plus an LP core |
| Memory | 32 MB PSRAM in package, 32 MB NOR flash on board |
| Wireless | ESP32-C6-MINI-1 over SDIO: 2.4 GHz Wi-Fi 6 and Bluetooth 5 (LE) |
| Display | 7-inch 1024 × 600 IPS touch panel over MIPI-DSI |
| Touch | GT911 capacitive controller, five-point touch |
| Camera | MIPI-CSI (2-lane), OV5647 optional |
| Audio | ES8311 codec, ES7210 ADC, dual microphones and speaker header |
| Expansion | microSD, USB 2.0 OTG HS, CAN/TWAI, RS485, I2C, UART, GPIO |
| BSP | managed component `waveshare/esp32_p4_wifi6_touch_lcd_7b` 3.0.1 |
| ESP-IDF target | `esp32p4` |

> Pin assignments follow the official product documentation and the board
> support package; this repository does not ship a schematic copy.

## 🚀 Getting started

Prerequisites:

- **ESP-IDF v6.1** (Brookesia 0.8 accepts `>= 6.0` and `<= 6.2`; other versions
  are rejected at configure time).
- A USB data cable on the download port; Windows, Linux and macOS all work.
- Network access on the first build to fetch managed components.

Build:

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

The build produces `build/system_monitor.bin` (application) and
`build/storage.bin` (16 MB littlefs resource volume).

Flash **only `factory` and `storage`**; never erase the whole chip, otherwise
the factory Wi-Fi calibration is lost:

```powershell
python -m esptool --chip esp32p4 -p COMx -b 460800 write-flash `
  0x110000 build/system_monitor.bin 0x1000000 build/storage.bin
```

[`flash_app.ps1`](firmware/brookesia_master/examples/system_monitor/flash_app.ps1)
wraps the same command. To restore the vendor demo, write
[`firmware/ESP32-P4-WIFI6-Touch-LCD-7B-FactoryOnly.bin`](firmware/ESP32-P4-WIFI6-Touch-LCD-7B-FactoryOnly.bin)
at `0x0`.

## 🧪 Desktop apps and interactions

| App | Description |
| --- | --- |
| System monitor | CPU / memory / disk / network / temperature dashboard fed by on-board sensors and `scripts/pc_agent.py` |
| Music | PCM WAV only; background playback resumes the previous track and position |
| Gallery | Browses `/littlefs/pictures` and SD card images, slideshow, set wallpaper |
| Video | `.mjpeg` / `.mjpg` / `.mjp`, hardware JPEG decoding, asynchronous track switching |
| Settings | Wi-Fi, Bluetooth, display, sound, time, language, device info |

Interaction rules:

- Swipe up on any page to go home; press and hold to open recents, which offers
  CLEAR ALL.
- Launcher icons flow left to right; pages follow the swipe gesture.
- In the video app, a single tap does nothing and a **double tap pauses or
  resumes**; the bottom bar holds previous, pause, next and refresh, with the
  progress slider and time on the line below.
- Music and gallery keep their action buttons in a centered bar at the bottom.
- Background apps are paused and represented by up to three icons in the status
  bar.

## 📡 Wireless and Bluetooth

- **Wi-Fi**: the on-board ESP32-C6 provides 2.4 GHz Wi-Fi 6 through ESP-Hosted
  over SDIO. Settings supports scan, connect and disconnect, shows the IPv4
  address and MAC, and reconnects automatically after a drop.
- **Bluetooth**: the C6 firmware only ships BLE 5, so GAP advertising and
  scanning are available; advertising pauses during a scan and resumes after.
- Classic Bluetooth is not part of the C6 firmware, therefore **A2DP is not
  available** and audio only plays through the on-board ES8311.

## ✅ Verified behaviour and known limits

Verified:

- `idf.py build` succeeds and a `factory + storage` flash boots with all six apps
  installed.
- Launcher icons are no longer clipped at the top; gestures, recents and status
  bar icons behave as expected.
- Video track switching runs on the playback worker and never blocks the UI;
  playback is fixed at 12 fps.
- Wi-Fi reconnects after a drop; BLE scanning and advertising switch cleanly.

Known limits:

- Without an SD card the mount fails and media is read from `/littlefs` only.
- Video must be MJPEG (no container, no audio track); use
  [`tools/make_mjpeg.py`](tools/make_mjpeg.py) to produce it. The bundled samples
  come from [`tools/make_demo_videos.py`](tools/make_demo_videos.py).
- Music supports PCM WAV only.
- Battery level and real temperatures depend on the hardware; the PC metrics in
  the system monitor require [`scripts/pc_agent.py`](scripts/pc_agent.py)
  broadcasting to UDP 9999.
- Some environments return a non-zero process code at the end of a build even
  though `Project build complete` is printed; the build is still valid.

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

littlefs layout:

- `/littlefs/music`, `/sdcard/music`: WAV tracks.
- `/littlefs/videos`, `/sdcard/videos`: MJPEG movies.
- `/littlefs/pictures`, `/littlefs/wallpapers`: photos and wallpaper candidates.
- `/littlefs/system/super/wallpaper.txt`: current wallpaper path.
- `/littlefs/apps`: JSON UI packages and icons for every app.

## 🗂️ Repository layout

```text
docs/                                  Project documentation (EN / ZH pairs)
examples/esp-idf/                      Vendor ESP-IDF examples (board checks)
examples/arduino/                      Vendor Arduino examples
firmware/brookesia/                    Vendor Brookesia BSP examples
firmware/brookesia_master/            Brookesia 0.8 source tree used here
  examples/system_monitor/            Desktop firmware project (build entry)
    components/                       Project apps and Bluetooth service
    littlefs/                         Resources packed into the storage volume
    main/                             Startup flow and shared services
scripts/                              Helper scripts (PC telemetry, flash, discovery)
tools/                                Asset generators (MJPEG, demo videos, app art)
firmware/ESP32-P4-WIFI6-Touch-LCD-7B-FactoryOnly.bin   Full factory image
```

## 📚 Documentation index

| Document | Content |
| --- | --- |
| [`docs/BROOKESIA_7B.md`](docs/BROOKESIA_7B.md) | Desktop system guide: features, partitions, build, flash, media |
| [`docs/PROJECT_STRUCTURE.md`](docs/PROJECT_STRUCTURE.md) | Repository layout |
| [`docs/GETTING_STARTED.md`](docs/GETTING_STARTED.md) | Toolchain and first run |
| [`docs/EXAMPLES_GUIDE.md`](docs/EXAMPLES_GUIDE.md) | Board example index and learning order |
| [`docs/firmware.md`](docs/firmware.md) | Firmware delivery and flashing boundaries |
| [`docs/CI.md`](docs/CI.md) | CI and repository checks |
| [`CONTRIBUTING.md`](CONTRIBUTING.md) | Contribution guide |

Secondary entries:

- [`README.md`](README.md): the primary Chinese page for this project.
- [Waveshare product page](https://www.waveshare.com/esp32-p4-wifi6-touch-lcd-7b.htm)
  and the [official documentation site](https://docs.waveshare.com/ESP32-P4-WIFI6-Touch-LCD-7B).
- [`examples/README.md`](examples/README.md): vendor example overview.
- [`firmware/README.md`](firmware/README.md): vendor firmware folder notes.

## 🤝 Contributing

- Read [`CONTRIBUTING.md`](CONTRIBUTING.md) before opening a pull request and
  keep English and Chinese pages in sync.
- For build or runtime issues, include the ESP-IDF version, the relevant
  `idf.py` output and reproduction steps.
- Board-level hardware questions belong to the official documentation and BSP
  repository.

## 📄 License

Released under [Apache-2.0](LICENSE). Upstream sources under
`firmware/brookesia_master/` keep their original license and copyright notices.
