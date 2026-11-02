# ESP-Brookesia Desktop System (ESP32-P4-WIFI6-Touch-LCD-7B)

[简体中文](BROOKESIA_7B_ZH.md)

This repository carries a second-development **ESP-Brookesia desktop system** on top
of the official Waveshare 7B board support package. This document covers the software
baseline, features, partitions, build/flash flow, media formats and known limitations.

> The repository still ships the official content (`examples/`, `firmware/brookesia/`,
> factory firmware and so on). **This document describes the `firmware/brookesia_master/`
> track**, which is the main line of the customisation.

---

## 1. Overview

| Item | Detail |
| --- | --- |
| SoC | ESP32-P4NRW32, dual-core RISC-V up to 360 MHz + LP core |
| Memory / storage | 32 MB PSRAM in package (HEX, 200 MHz), 32 MB NOR flash on board |
| Display | 7-inch 1024 x 600 IPS, MIPI-DSI (EK79007) |
| Touch | GT911 capacitive, 5 points |
| Wireless | On-board ESP32-C6: 2.4 GHz Wi-Fi 6 + Bluetooth 5 (LE) through ESP-Hosted over SDIO |
| Audio | ES8311 codec + speaker header |
| ESP-IDF | **v6.1** (Brookesia 0.8 requires `>=6.0, <=6.2`) |
| ESP-Brookesia | **0.8.x**, vendored as source in `firmware/brookesia_master/` (upstream `espressif/esp-brookesia`, master) |
| GUI | LVGL 9 + `esp_lvgl_adapter` |

The board boots into the Brookesia Super Shell. Apps can be paused and resumed in the
background; the status bar shows paused apps.

---

## 2. Layout

```
firmware/
  brookesia/                    Official BSP example (registry esp-brookesia 0.5.0, factory demo lineage)
  brookesia_master/             Brookesia 0.8 source tree (vendored)
    hal/brookesia_hal_boards/boards/waveshare/esp32_p4_wifi6_touch_lcd_7b/
    app/brookesia_app_settings/ Settings app (adds the Bluetooth page)
    system/brookesia_system_super/  Shell, status bar, recents, wallpaper
    examples/system_monitor/    Firmware project of this customisation (build entry)
      components/               Custom apps (system_monitor / music / gallery / video, bt_service)
      littlefs/                 Assets packed into the storage partition
      main/                     Boot flow, display and general services
      partitions.csv            Custom partition table
      flash_app.ps1             Flash app + storage only
  backups/                      Partition table backups (original / custom)
  ESP32-P4-WIFI6-Touch-LCD-7B-FactoryOnly.bin  Full 32 MB factory image
tools/
  make_mjpeg.py                 Convert a GIF or image sequence into .mjpeg
  make_demo_videos.py           Regenerate the bundled sample movies (1024 x 524)
scripts/
  pc_agent.py                   Broadcast PC stats to the board (UDP 9999)
docs/
  BROOKESIA_7B.md               This document
```

> `firmware/brookesia_master/` is a source copy of `espressif/esp-brookesia` (master
> branch) tracked by this repository. Its standalone repository metadata was renamed to
> `.git-upstream` (ignored by `.gitignore`); to sync with upstream again, rename it back
> with `Move-Item .git-upstream .git`.

---

## 3. Features

### 3.1 Shell / desktop

- **Status bar**: clock centred; Wi-Fi strength (white icon recoloured at runtime),
  Bluetooth and battery on the right; up to three icons of paused apps on the left.
- **Launcher**: icons flow left to right, multiple pages, swipeable.
- **Gestures**: a quick swipe up from any page returns to the desktop; a long press opens recents.
- **Recents**: card previews with a **CLEAR ALL** button.
- **Wallpaper**: set from the Gallery app, stored in `/littlefs/system/super/wallpaper.txt`
  and loaded when the shell starts.
- **Theme / language**: `light` / `dark`, `zh_CN` / `en`, switchable in Settings.

### 3.2 System Monitor (`brookesia.waveshare.system_monitor`)

- Disk usage aggregated over all volumes; turns red and raises an alarm above 90 %.
- Ring gauges show **CPU and GPU temperature** in the centre.
- Network card also reports **DISK I/O**.
- `TEMP_ALARM_C = 90.0`; above the limit the value is suffixed with `HIGH!`.
- Data sources: on-board sensors plus `scripts/pc_agent.py`, which broadcasts
  CPU / memory / disk / GPU / network stats to UDP **9999** once per second.

### 3.3 Music (`brookesia.waveshare.music`)

- **WAV (PCM) only**, JSON-described GUI, playback on a dedicated thread and it **keeps
  playing while the app is in the background**.
- Draggable progress slider (seek applied every 500 ms so dragging does not restart the codec).
- Separate **volume slider 0-100** that takes effect immediately.
- Metadata shows sample rate / channels / bit depth, plus track index and count.
- Auto-advance to the next track; reopening the app keeps the last track and position.
- Directories: `/littlefs/sounds`, `/sdcard/music`, `/sdcard`.

### 3.4 Gallery (`brookesia.waveshare.gallery`)

- Directories: `/littlefs/pictures`, `/littlefs/wallpapers`, `/sdcard/pictures`, `/sdcard`.
- Previous / next browsing with file name, index, **resolution and file size**.
- **Slideshow**: advances every 4 seconds (button sits next to "refresh").
- **Set wallpaper**: writes `/littlefs/system/super/wallpaper.txt`, applied on the next launch.
- A single image resource id is re-registered per picture, so the previous decoded bitmap
  is released instead of accumulating in PSRAM.

### 3.5 Video (`brookesia.waveshare.video`)

- Format: **concatenated JPEG pictures** (`.mjpeg` / `.mjpg` / `.mjp`), no container, no audio.
- Fixed **12 fps** pacing (MJPEG carries no timestamps), auto-advance, pause, previous/next,
  and draggable seek. Movie switching is performed asynchronously by the playback thread, so
  tapping next/previous never blocks the UI.
- Hardware JPEG decode (`esp_driver_jpeg`) into RGB565 double buffers, one `set_view_src`
  per frame.
- Display policy: a movie **smaller than the video area is shown 1:1 and centred** (no LVGL
  scaling, which is what keeps the frame rate); larger movies are scaled down to fit the
  1024 x 524 video area. A 76dp control bar (progress, buttons, duration) sits below it
  instead of covering the picture.
  Authoring at **1024 x 524** gives 1:1 full-area playback at a steady 12 fps.
- Directories: `/littlefs/videos`, `/sdcard/videos`, `/sdcard`.

### 3.6 Settings (`brookesia.general.settings`)

- Wi-Fi: scan / connect / disconnect, shows **IP and MAC**, reconnects automatically after a drop.
- Bluetooth: BLE scan and advertising (advertising resumes when scanning stops).
- Display (brightness / theme), sound (volume), time zone and SNTP, language, device info.

### 3.7 Bluetooth

- The on-board C6 provides **BLE 5** (GAP advertising and scanning).
- Classic Bluetooth / A2DP is **not available** (the C6 firmware only ships BLE), so audio
  cannot be routed to a Bluetooth speaker.

---

## 4. Partition table

`firmware/brookesia_master/examples/system_monitor/partitions.csv`:

| Name | Type | Offset | Size | Purpose |
| --- | --- | --- | --- | --- |
| `nvsfactory` | data/nvs | `0x9000` | 200 KB | Factory Wi-Fi calibration, never touch |
| `nvs` | data/nvs | `0x3B000` | 840 KB | User NVS |
| `otadata` | data/ota | `0x10D000` | 8 KB | Kept (empty, bootloader falls back to factory) |
| `phy_init` | data/phy | `0x10F000` | 4 KB | PHY calibration data |
| `factory` | app/factory | `0x110000` | 14.94 MB | Application image |
| `storage` | data/0x82 | `0x1000000` | 16 MB | littlefs: app packages, fonts, wallpaper, music, videos |

Unlike the stock table, **`ota_0` / `ota_1` / `assets` were removed** and merged into
`factory`; `storage` moved from `0x1AF0000` to the 16 MB boundary `0x1000000`. Always keep
a backup of the table at `0x8000`:

- Original: `firmware/backups/partition-table-0x8000-original.bin`
- Current: `firmware/backups/partition-table-0x8000-new.bin`

To restore the factory demo, flash `firmware/ESP32-P4-WIFI6-Touch-LCD-7B-FactoryOnly.bin` at `0x0`.

---

## 5. Build

### 5.1 Requirements

- **ESP-IDF v6.1** (`>=6.0` and `<=6.2`; other versions are rejected by Brookesia 0.8).
- Windows / Linux / macOS; the first build downloads components.

### 5.2 Steps

```bash
# 1) Enter the IDF environment (example path on Windows)
. C:\esp\v6.1\esp-idf\export.ps1

# 2) Enter the firmware project
cd firmware/brookesia_master/examples/system_monitor

# 3) One-time configuration
idf.py set-target esp32p4
idf.py gen-bmgr-config -b esp32_p4_wifi6_touch_lcd_7b

# 4) Build
idf.py build
```

Artifacts: `build/system_monitor.bin` (application) and `build/storage.bin` (16 MB littlefs image).

### 5.3 Pitfalls

- **Run `idf.py reconfigure` after adding a component under `components/`**, otherwise IDF
  never compiles it.
- `CONFIG_LV_CACHE_DEF_SIZE` must stay large (4 MB by default) or a 1024 x 600 wallpaper
  cannot be cached as one entry.
- `Project build complete` means success (the process exit code is non-zero on some hosts; ignore it).

---

## 6. Flashing

Flash **only the `factory` and `storage` partitions**; do not flash the whole chip unless you
want to restore the factory demo:

| Content | Offset | When |
| --- | --- | --- |
| `build/system_monitor.bin` | `0x110000` | Every code change |
| `build/storage.bin` | `0x1000000` | When `littlefs/` (app packages or assets) changed |
| `build/partition_table/partition-table.bin` | `0x8000` | **Only when the table changes** |
| Factory full image | `0x0` | Restore the factory demo |

```powershell
# App + storage in one go (addresses are baked into the script)
.\flash_app.ps1 -Port COM3

# Or manually
python -m esptool --chip esp32p4 -p COM3 -b 460800 `
    --before default-reset --after hard-reset `
    write-flash 0x110000 build\system_monitor.bin 0x1000000 build\storage.bin
```

> Run esptool as a **single foreground process** (no monitor and no parallel flashers), and do
> not use `idf.py flash`, which also writes the bootloader and other partitions.

Console: USB-Serial/JTAG, 115200 baud.

---

## 7. Assets

### 7.1 littlefs layout (`examples/system_monitor/littlefs/`)

| Path | Content |
| --- | --- |
| `apps/<package id>/res/` | Runtime app UI packages (generated at build time, do not edit) |
| `system/super/shell/` | Shell UI package (icons, status bar, recents, themes) |
| `system/super/wallpaper.txt` | Current wallpaper path written by the Gallery app |
| `system/fonts/` | `NotoSansSC-Regular.subset.ttf` (Chinese subset) and `Telex-Regular.ttf` |
| `pictures/` | Bundled sample pictures |
| `sounds/` | Bundled sample audio |
| `videos/` | Bundled sample movies |

### 7.2 Music

- **WAV (PCM)** only. Copy files into `/littlefs/sounds` (requires reflashing `storage`) or
  into `/music` / the SD card root, which work as soon as a card is inserted.
- Sample rate / channels / bit depth are shown in the UI; 44.1 kHz / 16-bit is recommended.
- The cover tile is a generated PNG; regenerate or restyle it with
  `python tools/make_app_art.py`.

### 7.3 Video

```bash
# Convert a GIF or an image folder (playback is a fixed 12 fps)
python tools/make_mjpeg.py frames/ video.mjpeg --size 1024x524 --quality 70
python tools/make_mjpeg.py clip.gif video.mjpeg --size 640x360

# Regenerate the bundled samples
python tools/make_demo_videos.py
```

- `--size 1024x524` gives full-screen, smooth playback.
- Anything else is shown centred at its native size (letterboxed when smaller) and only scaled
  down when larger than the video area.
- Without ffmpeg, export the clip to an image sequence with any other tool and merge it with
  `make_mjpeg.py`.

### 7.4 Wallpaper

Pick a picture in the Gallery app and press "set wallpaper"; the path is stored in
`/littlefs/system/super/wallpaper.txt` and applied the next time the shell starts.

---

## 8. Differences from upstream

| Location | Change |
| --- | --- |
| `hal/brookesia_hal_boards/.../esp32_p4_wifi6_touch_lcd_7b/` | New 7B board package: 1024 x 600 MIPI-DSI, GT911, backlight GPIO 32, 200 MHz HEX PSRAM, C6 SDIO pins (RST 54 / CLK 18 / CMD 19 / D0-D3 14-17, 40 MHz 4-bit) |
| `hal/brookesia_hal_adaptor/src/wifi/backend.cpp` | Report **Connected as soon as the link is up** (waiting for DHCP hit a 15 s service timeout and made Wi-Fi flap) |
| `system/brookesia_system_super/` | Status bar (centred clock, Wi-Fi strength icon, Bluetooth, battery, paused-app icons), left-to-right launcher, swipe-up to desktop, long-press recents, CLEAR ALL, wallpaper loading |
| `system/brookesia_system_core/src/app/manager.cpp` | Reopening an app after going home, pause/resume lifecycle fixes |
| `app/brookesia_app_settings/` | New Bluetooth (BLE scan/advertise) page; Wi-Fi page shows IP/MAC and reconnects automatically |
| `examples/system_monitor/components/` | Custom apps: system monitor, music, gallery, video, plus the BLE service |
| `examples/system_monitor/partitions.csv` | OTA/assets removed, `factory` grown to 14.94 MB, `storage` moved to `0x1000000` |

---

## 9. Known limitations

- Without an SD card the boot log prints `sdmmc_init_ocr ... 0x107` / `Failed to initialize SD card`; this is expected.
- `swap_xy is not supported` / `disp_on_off is not supported`: the MIPI panel does not implement
  these calls, harmless.
- **No classic Bluetooth / A2DP** (the C6 only provides BLE 5).
- Video is fixed at 12 fps with no audio track; keep MJPEG sources at or below 1024 x 524.
- Battery level relies on hardware sampling and may only be indicative on some batches.
- Real CPU/GPU temperatures in the system monitor require `scripts/pc_agent.py` on the PC.
- Colour and touch behaviour should always be confirmed on the physical panel.

---

## 10. Related documentation

- [Project structure](PROJECT_STRUCTURE.md)
- [Firmware](firmware.md)
- [Examples guide](EXAMPLES_GUIDE.md)
- [CI](CI.md)
- Firmware project notes: [`firmware/brookesia_master/examples/system_monitor/README.md`](../firmware/brookesia_master/examples/system_monitor/README.md)
