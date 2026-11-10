# ESP-Brookesia Desktop System (ESP32-P4-WIFI6-Touch-LCD-7B)

*[中文版：Brookesia 桌面（中文）](BROOKESIA_7B_ZH.md) · [Project home](../README_EN.md)*

This repository builds a second-generation **ESP-Brookesia desktop system** on top
of the Waveshare 7B board support package. This document is the English full text:
software baseline, feature details, partitions, build and flash steps, media
formats, UI development conventions, troubleshooting and known limits.

> The Chinese page [`BROOKESIA_7B_ZH.md`](BROOKESIA_7B_ZH.md) is the primary
> document; this file mirrors it for English readers.
> Vendor material (`examples/`, `firmware/brookesia/`, the factory image) is kept
> as-is; **this document describes the `firmware/brookesia_master/` main line**.

---

## 1. Overview

| Item | Notes |
| --- | --- |
| SoC | ESP32-P4NRW32, dual-core RISC-V up to 360 MHz plus an LP core |
| Memory / storage | 32 MB PSRAM in package (HEX, 200 MHz), 32 MB NOR flash on board |
| Display | 7-inch 1024 × 600 IPS over MIPI-DSI (EK79007) |
| Touch | GT911 capacitive controller, five-point touch |
| Wireless | ESP32-C6: 2.4 GHz Wi-Fi 6 + Bluetooth 5 (LE) over ESP-Hosted SDIO |
| Audio | ES8311 codec with a speaker header |
| ESP-IDF | **v6.1** (Brookesia 0.8 requires `>= 6.0, <= 6.2`) |
| ESP-Brookesia | **0.8.x**, vendored source (upstream `espressif/esp-brookesia` master) |
| GUI | LVGL 9 + `esp_lvgl_adapter` |
| Board support | managed `waveshare/esp32_p4_wifi6_touch_lcd_7b` 3.0.1 plus a local 7B board package |

The system boots into the Brookesia launcher (Super Shell). Apps can be paused in
the background and resumed later; the status bar shows paused apps.

A healthy boot installs six apps (serial log at 115200):

```text
SysCore: : Native app installed: id(1), manifest(brookesia.system.super.shell)
SysCore: : Native app installed: id(2), manifest(brookesia.general.settings)
SysCore: : Native app installed: id(3), manifest(brookesia.waveshare.gallery)
SysCore: : Native app installed: id(4), manifest(brookesia.waveshare.music)
SysCore: : Native app installed: id(5), manifest(brookesia.waveshare.system_monitor)
SysCore: : Native app installed: id(6), manifest(brookesia.waveshare.video)
```

---

## 2. Repository layout

```text
firmware/
  brookesia/                    Vendor BSP example (registry esp-brookesia 0.5.0)
  brookesia_master/             Vendored Brookesia 0.8 tree used by this project
    hal/brookesia_hal_boards/boards/waveshare/esp32_p4_wifi6_touch_lcd_7b/
    app/brookesia_app_settings/ Settings app (with the added Bluetooth page)
    system/brookesia_system_super/  Desktop shell, status bar, recents, wallpaper
    examples/system_monitor/    Firmware project (build entry)
      components/               In-house apps (monitor / music / gallery / video + bt_service)
      littlefs/                 Assets packed into the storage volume
      main/                     Startup flow and shared services
      partitions.csv            Custom partition table
      flash_app.ps1             App + storage flashing script
  backups/                      Partition table backups (original / new)
  ESP32-P4-WIFI6-Touch-LCD-7B-FactoryOnly.bin  Full factory image (32 MB)
tools/
  make_mjpeg.py                 GIF / image sequence to .mjpeg
  make_demo_videos.py           Bundled sample videos (976 × 432)
  make_app_art.py               Music app cover art
scripts/
  pc_agent.py                   PC telemetry broadcast (UDP 9999)
docs/
  BROOKESIA_7B_ZH.md            Primary Chinese document
  BROOKESIA_7B.md               This English full text
```

> `firmware/brookesia_master/` is a copy of `espressif/esp-brookesia` (master).
> Its standalone repository metadata was renamed to `.git-upstream` (ignored by
> `.gitignore`); restore it with `Move-Item .git-upstream .git` when syncing
> against upstream again.

In-house components:

| Component | Package id | Notes |
| --- | --- | --- |
| `brookesia_app_system_monitor` | `brookesia.waveshare.system_monitor` | System monitor |
| `brookesia_app_music` | `brookesia.waveshare.music` | WAV / MP3 music player |
| `brookesia_app_gallery` | `brookesia.waveshare.gallery` | Image browser / wallpaper |
| `brookesia_app_video` | `brookesia.waveshare.video` | MJPEG / MP4 player |
| `bt_service` | —(service, not an app) | BLE scan / advertising |

---

## 3. Features

### 3.1 Desktop (Super Shell)

- **Status bar**: centered clock; on the right the Wi-Fi signal (ascending bars
  driven by RSSI, with a red slash when disconnected), Bluetooth and battery
  (USB / BATTERY power label); on the left up to three paused apps.
- **Icon pages**: apps flow left to right across pages with swipe paging.
- **Gestures**: swipe up quickly on any page to return home; press and hold to
  open recents.
- **Recents**: card previews with a **CLEAR ALL** action.
- **Wallpaper**: set from the gallery, stored in
  `/littlefs/system/super/wallpaper.txt` and loaded when the launcher starts.
- **Theme / language**: `light` / `dark` and `zh_CN` / `en`, switchable in
  settings.

### 3.2 System monitor (`brookesia.waveshare.system_monitor`)

- Disk usage is aggregated over all volumes; at 90 % it turns red and raises an
  alarm.
- The gauge center shows **CPU / GPU temperature**.
- The network card also reports **DISK I/O**.
- The temperature threshold is `TEMP_ALARM_C = 90.0`; values above it get a
  `HIGH!` suffix.
- Data comes from on-board sensors plus `scripts/pc_agent.py`, which broadcasts
  CPU / memory / disk / GPU / network data to UDP **9999** once per second (GPU
  needs an NVIDIA card and `nvidia-smi`; `psutil` improves CPU / memory values).

### 3.3 Music (`brookesia.waveshare.music`)

- The app opens on the **player** and stays silent; the "list" button slides a
  **320 dp picker drawer** over the right side. Tapping a row starts that track
  and closes the drawer, so the main view is never replaced.
- The drawer shows **5 rows per page** (previous / next page) with file name and
  size; the row being played is tagged "playing / paused".
- **PCM WAV and MP3 (MPEG-1/2/2.5 Layer III)**; WAV is parsed from its RIFF
  chunks, MP3 is decoded in software with minimp3. JSON-described UI, decoding
  and playback on a dedicated thread, **keeps playing when the app is in the
  background**.
- Seeking is applied at most every 500 ms so dragging does not restart the codec
  repeatedly. MP3 files are indexed frame by frame (ID3v2 tags are skipped) so
  the seek bar works on them too, and single frames can be jumped to directly.
- Volume is adjusted with **−** / **+** buttons in the bottom bar (10 % steps,
  0–100) and echoed as a percentage on the status line.
- The detail area shows track index / total, sample rate, channel layout, bit
  depth, **bitrate** (WAV: sample rate × channels × bit depth, MP3: average
  bitrate from the frame headers) and file size; returning from the background
  keeps the player page while a track is loaded.
- There is no title bar: the enlarged player card (**300 dp cover**) fills the
  upper area with a vertical gradient and a soft shadow; a **flowing waveform
  line** (a bright line over a wider glow line, ~11 fps) runs next to the cover
  and follows the volume while playing, keeping a slow ripple while paused, with
  the seek bar and time below.
- The bottom control bar keeps **play / pause exactly in the centre**: refresh,
  volume−, volume+ and previous sit on the left, next, auto-next and list on the
  right. **"List" is the rightmost button**, next to the drawer that slides in
  from that side.
- **Auto-next is an optional switch** (off by default): when on, a finished
  track advances to the next one; when off, playback stops on the current track
  with the position reset.
- The page starts right under the status bar (`pageY = 56dp`) so the content gets
  as much vertical room as possible.
- Folders: `/littlefs/sounds`, `/sdcard/music`, `/sdcard`.

### 3.4 Gallery (`brookesia.waveshare.gallery`)

- The app opens on the **preview of the first picture** (only that one is
  decoded, no autoplay); the "list" button slides a **320 dp picker drawer** over
  the right side, and tapping a row switches the preview and closes the drawer.
- The drawer shows **5 rows per page** with file name, resolution and size, and
  the format (PNG / JPEG) as the right-hand tag.
- Folders: `/littlefs/pictures`, `/littlefs/wallpapers`, `/sdcard/pictures`,
  `/sdcard`.
- **PNG / JPEG** are supported; the detail line shows resolution, file size,
  format and color depth, for example `1024x600 · 1.2 MB · PNG · 24-bit`
  (PNG: IHDR bit depth × color channels, JPEG: SOF precision × components).
- Layout: a near-full-width preview without a title bar, then a bottom card with
  index + file name + details, centered action buttons and a status line. Button
  order: previous, next, set wallpaper, slideshow, refresh, list, with **"List"
  on the far right**.
- Previous / next browsing; **slideshow is an optional switch** (off by default)
  and changes the image every 4 seconds while enabled.
- **Set wallpaper** writes `/littlefs/system/super/wallpaper.txt` and takes
  effect when the launcher is shown again.
- One stable image resource id is reused, so switching images releases the
  previous decoded frame and long sessions do not leak memory.

### 3.5 Video (`brookesia.waveshare.video`)

- The app opens on the **player** (empty stage); the "list" button slides a
  **320 dp picker drawer** over the right side, and a movie is only read and
  decoded after a row is tapped. Loading runs on the playback worker, so the UI
  stays responsive.
- The drawer shows **5 rows per page** (previous / next page) with file name and
  size; the movie being played is tagged "playing".
- Formats: `.mjpeg` / `.mjpg` / `.mjp` (concatenated JPEG frames, no container
  and no audio) and `.mp4` / `.m4v` / `.mov` (ISO BMFF container with
  **H.264/AVC constrained baseline** video).
- MJPEG is decoded by the hardware JPEG codec at a fixed **12 fps** (MJPEG has
  no timestamps). MP4 is demuxed by the app and decoded in software with the
  `esp_h264` tinyH264 decoder — the ESP32-P4 only has a hardware H.264
  **encoder** — then converted from I420 to RGB565. MP4 audio tracks (AAC) are
  ignored, so MP4 playback is silent; the frame rate follows the container and
  is limited by software decoding (about 10 fps at 976 × 432 on the 360 MHz
  P4). A seek snaps to the nearest IDR keyframe.
- Pause / previous / next / seek are supported. **Auto-next is an optional
  switch** (off by default): when on, a finished movie advances to the next
  one; when off, it stops on the last frame.
- **Double tap on the picture pauses / resumes**; the GUI has no double-click
  event, so `video.tap` merges two clicks inside a 400 ms window.
- Hardware JPEG decoding (`esp_driver_jpeg`) outputs RGB565 with double
  buffering.
- Movie switching runs asynchronously on the playback worker, so previous / next
  never blocks the UI.
- Display policy: movies smaller than the stage are shown **centered at native
  1:1 scale** (LVGL does not upscale, which protects the frame rate); the stage is
  976 × 432 and larger movies are scaled down. The bottom 76 dp is a separate
  control bar: progress and time on the first row, with **play / pause pinned in
  the centre of the second row** (label "Play" while paused, "Pause" while
  playing); refresh, volume−, volume+ and previous on its left, next, auto-next
  and list on its right, **"List" on the far right** next to the drawer. The volume buttons drive the
  board codec (neither MJPEG nor MP4 carries played audio; they mainly serve
  background music and keep the controls consistent).
  Authoring MJPEG at **976 × 432** gives full-screen 1:1 playback at 12 fps.
- Folders: `/littlefs/videos`, `/sdcard/videos`, `/sdcard`.

### 3.6 Settings (`brookesia.general.settings`)

- Wi-Fi: scan, connect, disconnect, show **IP / MAC**, reconnect automatically.
- Bluetooth: BLE scan and advertising (advertising resumes after a scan).
- Display: brightness / theme; sound: volume; time: time zone and SNTP;
  language; device info.

### 3.7 Bluetooth

- The on-board C6 provides **BLE 5** (GAP advertising + scanning).
- The C6 firmware has no classic Bluetooth, so **A2DP is unavailable** and audio
  only goes through the on-board ES8311.

---

## 4. Partition table

`firmware/brookesia_master/examples/system_monitor/partitions.csv`:

| Name | Type | Offset | Size | Purpose |
| --- | --- | --- | --- | --- |
| `nvsfactory` | data/nvs | `0x9000` | 200 KB | Factory Wi-Fi calibration, **do not erase** |
| `nvs` | data/nvs | `0x3B000` | 840 KB | User NVS |
| `otadata` | data/ota | `0x10D000` | 8 KB | Reserved (empty, boots factory) |
| `phy_init` | data/phy | `0x10F000` | 4 KB | PHY calibration |
| `factory` | app/factory | `0x110000` | 14.94 MB | Application image |
| `storage` | data/0x82 | `0x1000000` | 16 MB | littlefs: app packages, fonts, wallpaper, music, video |

Compared with the vendor table, `ota_0`, `ota_1` and `assets` are removed and
their space merged into `factory`; `storage` moved from `0x1AF0000` to the 16 MB
boundary `0x1000000`. Back up the table at `0x8000` before changing it:

- Original: `firmware/backups/partition-table-0x8000-original.bin`
- Current: `firmware/backups/partition-table-0x8000-new.bin`

Flashing matrix:

| Content | Offset | When |
| --- | --- | --- |
| `build/system_monitor.bin` | `0x110000` | Every C++ change |
| `build/storage.bin` | `0x1000000` | Changes under `littlefs/` |
| `build/partition_table/partition-table.bin` | `0x8000` | **Only when the partition table changes** |
| Full factory image | `0x0` | Restoring the vendor demo |

---

## 5. Build

### 5.1 Prerequisites

- **ESP-IDF v6.1** (`>= 6.0` and `<= 6.2`; other versions are rejected).
- Windows / Linux / macOS; the first build needs network access for managed
  components.

### 5.2 Steps

```powershell
# 1) Enter the ESP-IDF environment (Windows example)
<ESP-IDF>/export.ps1

# 2) Enter the firmware project
cd firmware\brookesia_master\examples\system_monitor

# 3) One-time configuration
idf.py set-target esp32p4
idf.py gen-bmgr-config -b esp32_p4_wifi6_touch_lcd_7b

# 4) Build
idf.py build
```

Outputs: `build/system_monitor.bin` (application) and `build/storage.bin`
(16 MB littlefs image).

### 5.3 Common issues

- **A new component is ignored**: run `idf.py reconfigure` after adding
  `components/xxx`.
- `CONFIG_LV_CACHE_DEF_SIZE` must be large enough (4 MB by default) to cache a
  full 1024 × 600 wallpaper.
- `Project build complete` means success even if the process returns non-zero
  (seen in some environments).
- Changing only JSON / images still repacks `storage.bin`; no full clean needed.
- Only a `partitions.csv` change requires reflashing `partition_table` at
  `0x8000`.

---

## 6. Flash

Flash **only `factory` and `storage`**; never erase the whole chip so the factory
Wi-Fi calibration and partition table stay intact:

```powershell
# app + storage in one run (offsets are built into the script)
.\flash_app.ps1 -Port COMx

# or manually
python -m esptool --chip esp32p4 -p COMx -b 460800 `
    --before default-reset --after hard-reset `
    write-flash 0x110000 build\system_monitor.bin 0x1000000 build\storage.bin
```

> Run esptool as a **single serial process** (no monitor, no second flasher), and
> do not use `idf.py flash` (it also writes the bootloader and other partitions).

Serial log: USB-Serial/JTAG, 115200 baud.

---

## 7. Media

### 7.1 littlefs layout (`examples/system_monitor/littlefs/`)

| Path | Content |
| --- | --- |
| `apps/<package id>/res/` | Runtime app packages (generated at build time, do not edit) |
| `system/super/shell/` | Shell UI package (icons, status bar, recents, theme) |
| `system/super/wallpaper.txt` | Current wallpaper path (written by the gallery app) |
| `system/fonts/` | `NotoSansSC-Regular.subset.ttf` (Chinese subset) and `Telex-Regular.ttf` |
| `pictures/` | Bundled sample pictures |
| `sounds/` | Bundled sample audio |
| `videos/` | Bundled sample videos |

### 7.2 Music

- **PCM WAV and MP3 (Layer III)**; put files in `/littlefs/sounds` (reflash
  `storage`) or on the SD card under `/music` or the root directory.
- Sample rate, channels, bit depth, bitrate and file size are shown; 44.1 kHz /
  16-bit / stereo is recommended (MP3 is decoded to 16-bit PCM by minimp3).
- The cover is a generated **300 × 300** PNG; regenerate it with
  `python tools/make_app_art.py`.
- The track drawer shows 5 rows per page; tapping anywhere on a row starts that
  track and closes the drawer, while "refresh" only rescans and never starts
  playback.
- MP3 files are indexed once per track: the ID3v2 tag is skipped, then every
  valid MPEG Layer III frame is located and validated, so seeking jumps straight
  to a frame instead of scanning the whole file.

### 7.3 Pictures

- PNG / JPEG; place them under `/littlefs/pictures`, `/littlefs/wallpapers` or an
  SD card directory.
- Large images are scaled with `innerAlign: contain` inside the preview and are
  never cropped.
- Setting a wallpaper writes `/littlefs/system/super/wallpaper.txt`, which the
  launcher loads next time.

### 7.4 Video

```bash
# GIF or image folder to mjpeg (played back at a fixed 12 fps)
python tools/make_mjpeg.py frames/ video.mjpeg --size 976x432 --quality 70
python tools/make_mjpeg.py clip.gif video.mjpeg --size 640x360

# Regenerate the bundled sample videos
python tools/make_demo_videos.py

# H.264 baseline MP4 (silent) from the same MJPEG frames
ffmpeg -f mjpeg -framerate 12 -i video.mjpeg \
       -c:v libx264 -profile:v baseline -level 3.1 -pix_fmt yuv420p \
       -crf 24 -g 24 -keyint_min 12 -sc_threshold 0 -an \
       -movflags +faststart video.mp4

# MP3 sample for the music app
ffmpeg -f lavfi -i "sine=frequency=440:duration=9" -ac 2 -ar 44100 \
       -c:a libmp3lame -b:a 128k sound.mp3
```

- Use `--size 976x432` for the best full-screen 1:1 result.
- Other sizes are shown centered at native scale (with black borders); only
  material larger than 976 × 432 is scaled down.
- MP4 must be **H.264 constrained baseline** (8-bit 4:2:0). High profile,
  10-bit, interlaced and HEVC/VP9/AV1 files are rejected with an error message.
  Keep MP4 at or below 720p and prefer 976 × 432: software decoding runs at
  about 10 fps for 976 × 432 and roughly 5 fps for 720p, so the picture is
  smoother with smaller frames.
- Without ffmpeg, export the video to an image sequence with another tool and
  merge it with `make_mjpeg.py`.

### 7.5 Wallpaper

Select an image in the gallery and tap "set wallpaper"; the path is written to
`/littlefs/system/super/wallpaper.txt` and applied the next time the launcher is
shown.

---

## 8. UI development conventions

All in-house app screens are JSON-described; C++ only provides data and actions.
Most UI work touches only JSON and constants.

### 8.1 Package layout

```text
components/brookesia_app_xxx/package/
  res/root.json               Asset manifest + resolution variants
  res/screens/*.json          Screen (viewScreen) documents
  res/constants/default.json  Default constants
  res/constants/1024x600.json 7B screen overrides
  res/images/index.json       Static image index
  res/flows/*.json            Page flows (navigation / lifecycle)
```

`root.json` appends constants per screen size, for example:

```json
"variants": [
    {
        "when": "${expr(${env.widthDp} == 1024dp && ${env.heightDp} == 600dp)}",
        "assets": ["constants/1024x600.json"]
    }
]
```

Later files override earlier keys, so shared values live in the default file and
screen-specific sizes live in the variant.

### 8.2 Layout and style

- `layout`: flex container with `flexFlow` / `mainAlign` / `crossAlign` / `gap`.
- `placement`: `flow` (participates in layout), `absolute` (relative to the
  parent's top-left) or `relative` (supports `align`, e.g. `TopRight`);
  `flexGrow: 1` consumes the remaining space.
- `style`: colors, radius, border, padding, font size, `textAlign`, `clipCorner`.
- Units: `dp` for sizes, `sp` for fonts, `#RRGGBB` for colors.

### 8.3 Bindings and actions

- `bindings` connect widget properties to runtime data, e.g.
  `"labelProps.text": "track_detail"`.
- C++ updates them with `context.gui().set_binding_value(path, binding, value)`
  where `path` is the absolute document path such as
  `/music/page/player_card/info/track_detail`.
- `events` map widget events to action names (`music.seek`, `gallery.prev`,
  `video.tap`, ...), subscribed in C++ with `context.gui().subscribe_actions`.

### 8.4 Image resources

- Static images must be written as `${image.<id>}` (id from `images/index.json`),
  not as a file path.
- Runtime images are registered with `register_image()` using a stable id (which
  releases the previous image), then assigned through a binding to
  `imageProps.src`.

### 8.5 Build-time sync

The build copies `components/*/package/res/` into
`littlefs/apps/<package id>/res/` and packs it into `storage.bin`, so:

- reflashing `storage` is enough to apply UI changes;
- never edit the copies under `littlefs/apps/` (they are overwritten);
- JSON is validated when an app is installed, so a bad document shows up as
  `System init failed` or a missing app at boot.

### 8.6 How double-tap pause works

The GUI only exposes `clicked`, so the video app:

1. marks `stage` as `clickable` and maps `clicked` to `video.tap`;
2. records the previous tap time in C++ and treats a second tap within 400 ms as a
   double tap, calling `toggle_playback()`;
3. does nothing on a single tap, which also keeps the shell gestures (raw touch
   events from the display service) unaffected.

---

## 9. Differences from upstream

| Location | Change |
| --- | --- |
| `hal/brookesia_hal_boards/.../esp32_p4_wifi6_touch_lcd_7b/` | New 7B board package: 1024 × 600 MIPI-DSI, GT911, backlight GPIO 32, HEX PSRAM 200 MHz, C6 SDIO pins (RST 54 / CLK 18 / CMD 19 / D0–D3 14–17, 40 MHz 4-bit) |
| `hal/brookesia_hal_adaptor/src/wifi/backend.cpp` | **Report Connected as soon as the link is up** (previously it waited for DHCP, hitting the 15 s service state-machine timeout and making Wi-Fi flap) |
| `system/brookesia_system_super/` | Status bar (centered clock / Wi-Fi strength icon / Bluetooth / battery / paused apps), left-to-right launcher, swipe-up home, long-press recents, CLEAR ALL, wallpaper loading |
| `system/brookesia_system_core/src/app/manager.cpp` | Reopen apps after returning home, pause / resume lifecycle fixes |
| `app/brookesia_app_settings/` | New Bluetooth page (BLE scan / advertising), Wi-Fi page shows IP / MAC and reconnects automatically |
| `examples/system_monitor/components/` | In-house apps: system monitor, music, gallery, video, plus the BLE service |
| `examples/system_monitor/partitions.csv` | OTA / assets removed, `factory` grown to 14.94 MB, `storage` moved to `0x1000000` |

---

## 10. Performance and memory

- Video uses hardware JPEG decoding with RGB565 double buffering at a fixed
  12 fps; the stage is 976 × 432 and no upscaling is performed.
- The gallery reuses one image resource id, releasing the previous decoded frame
  on every switch; full-wallpaper caching depends on `CONFIG_LV_CACHE_DEF_SIZE`
  (4 MB by default).
- Music decodes on its own thread; the UI timer refreshes progress every 500 ms
  and merged seeks avoid restarting the codec repeatedly.
- Background apps stop painting the GUI but keep audio and worker threads alive,
  which is what enables background music.

---

## 11. Verification checklist

1. **Boot**: six `Native app installed` lines, no `System init failed`, no panic.
2. **Launcher**: icons not clipped at the top; paging; swipe-up home; long-press
   recents; CLEAR ALL.
3. **Status bar**: centered clock, signal-tinted Wi-Fi icon, paused apps on the
   left.
4. **System monitor**: disk / CPU / memory data; CPU / GPU temperatures in the
   gauge center; `HIGH!` above 90 °C; PC metrics and DISK I/O after starting
   `scripts/pc_agent.py`.
5. **Music**: opens on the player and stays silent; "list" (rightmost button)
   opens the 5-row drawer (over the main view) and a row starts playback and
   closes it; play / pause (pinned in the centre) / previous / next / seek /
   volume− volume+; the waveform line flows; auto-next is off by default and
   advances when enabled; detail line shows sample rate, bit depth, bitrate and
   size; playback continues on the launcher.
6. **Gallery**: opens on the first picture; "list" (rightmost button) opens the
   5-row drawer and a row switches the preview and closes it; previous / next;
   resolution / size / format / depth details; slideshow is off by default;
   wallpaper applies on the launcher.
7. **Video**: opens on the empty player; "list" (rightmost button) opens the
   5-row drawer and a row loads and plays that movie; double tap pauses /
   resumes; play / pause pinned in the centre (label "Play" while paused) with
   the other buttons on both sides; volume− / volume+ work; auto-next is off by
   default; seek; previous / next switching without stutter.
8. **Settings**: Wi-Fi shows IP / MAC and reconnects; BLE scan and advertising
   are mutually exclusive.

---

## 12. Troubleshooting

### 12.1 Log reference

| Log | Meaning / action |
| --- | --- |
| `sdmmc_init_ocr: send_op_cond (1) returned 0x107` | No SD card or unrecognised card; expected noise, media falls back to `/littlefs` |
| `Failed to initialize SD card` | Same as above |
| `swap_xy is not supported` | The MIPI panel does not swap XY; harmless |
| `disp_on_off is not supported` | The panel has no software on/off; harmless |
| `Backlight control GPIO not found` | The 7B package drives backlight on GPIO 32; this hints at a different board config |
| `Version mismatch: Host [2.12.0] > Co-proc [0.0.0]` | ESP-Hosted version notice; ignore when everything works |
| `System init failed` | Invalid JSON UI package; commonly `imageProps.src` missing `${image.<id>}` |
| An app is not installed | `littlefs/apps/<package id>/res/` is incomplete; reflash `storage` |

### 12.2 Common failures

- **Black screen after flashing**: confirm only `factory + storage` were written,
  and that `storage` was built from the same source tree; if the partition table
  changed, flash `0x8000` too.
- **Wi-Fi connected without internet**: check IP / MAC in settings; slow DHCP is
  handled by automatic reconnection.
- **Video stutters**: material larger than 976 × 432 is scaled; regenerate with
  `--size 976x432`. MP4 is software-decoded, so it is slower than MJPEG: 720p
  runs at roughly 5 fps, 976 × 432 at about 10 fps.
- **MP4 does not open**: only H.264 constrained baseline MP4 with 4-byte NAL
  lengths is supported; re-encode with the `ffmpeg` line in section 7.4. MP4 has
  no audio playback (AAC is ignored).
- **No music sound**: make sure the file is PCM WAV or MP3 and volume is not
  zero; A2DP is unavailable so Bluetooth speakers cannot be used.
- **Picture does not open**: only PNG / JPEG are supported; long names are
  truncated in the UI but still load.

---

## 13. Known limits

- Without an SD card the boot log prints `sdmmc_init_ocr ... 0x107` /
  `Failed to initialize SD card`; this is expected.
- No classic Bluetooth / A2DP (the C6 ships BLE 5 only).
- Video: MJPEG is fixed at 12 fps with no audio; MP4 (H.264 constrained
  baseline) is software-decoded at about 10 fps for 976 × 432 and about 5 fps
  for 720p, and its audio track is ignored. Keep MJPEG at or below 976 × 432.
- The battery pill has no fuel-gauge data: it shows `USB` when the USB port
  supplies power and `BATTERY` when it does not.
- Real CPU / GPU temperatures on the monitor need `scripts/pc_agent.py`.
- Final color and touch-calibration quality must be checked on the device.
- This firmware and **XiaoZhi AI** are mutually exclusive: the Waveshare wiki
  already lists this board (✅) with a ready-made XiaoZhi image, but flashing it
  replaces the desktop and partition table, and the official FAQ states the two
  cannot be switched in one click. Restoring this project means re-flashing
  `firmware/ESP32-P4-WIFI6-Touch-LCD-7B-FactoryOnly.bin` (at `0x0`).

---

## 14. Related documents

- [Project home](../README_EN.md)
- [Chinese primary document](BROOKESIA_7B_ZH.md)
- [Repository structure](PROJECT_STRUCTURE.md)
- [Firmware notes](firmware.md)
- [Examples guide](EXAMPLES_GUIDE.md)
- [Flashing and CI](CI.md)
- Firmware project notes:
  [`firmware/brookesia_master/examples/system_monitor/README.md`](../firmware/brookesia_master/examples/system_monitor/README.md)
