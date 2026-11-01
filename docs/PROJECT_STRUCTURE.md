# Project Structure

[简体中文](PROJECT_STRUCTURE_ZH.md)

This repository is a board-specific collection of examples and firmware for
ESP32-P4-WIFI6-Touch-LCD-7B.

| Path | Purpose |
| --- | --- |
| `README.md` | Repository overview and quick start |
| `examples/esp-idf/` | Standalone ESP-IDF examples |
| `examples/arduino/` | Arduino sketches and bundled board libraries |
| `firmware/` | Inventoried delivery source and prebuilt factory firmware |
| `firmware/brookesia/` | ESP-Brookesia factory-style firmware project |
| `firmware/brookesia_master/` | Vendored ESP-Brookesia 0.8 tree and the 7B desktop firmware project (this customisation) |
| `config/` | Shared ESP32-P4 revision overlays |
| `docs/` | Maintainer and user documentation |
| `.github/` | CI workflows, discovery, and Markdown audit scripts |

## ESP-IDF Projects

Use the lowercase `examples/esp-idf/` path. Linux CI treats case-only paths as
different directories, so do not add another ESP-IDF example root with different
capitalization.

Each project should contain:

- `CMakeLists.txt`
- `main/`
- `sdkconfig.defaults`
- `README.md` for hardware-facing examples

Use the managed 7B BSP component,
`waveshare/esp32_p4_wifi6_touch_lcd_7b` 3.0.1, for board display, touch, audio,
SD card, and wireless integration. Display applications use the BSP's public
LVGL adapter configuration and locking APIs directly. Keep generated outputs
out of git.

## Arduino Projects

Use `examples/arduino/` for Arduino-ESP32 sketches. Each first-party sketch
lives in `examples/arduino/examples/<name>/` and has exactly one `.ino` file
with the same basename as its directory. `libraries/displays/` carries the
LCD-7B EK79007 DSI, GT911, and backlight configuration; the bundled
`GFX_Library_for_Arduino` and `lvgl` directories provide reproducible display
and LVGL builds. The RS485 and TWAI examples are deliberately standalone and
do not initialize the display.

## Firmware Source

`firmware/brookesia` is an inventoried delivery-source project and does not
enter default example CI. The factory BIN is an immutable delivery artifact.
Firmware-source changes and validation require an explicitly scoped
firmware-maintenance workflow.

## Brookesia Desktop System

`firmware/brookesia_master/` vendors ESP-Brookesia 0.8 as source and adds the
7B board package plus the desktop firmware project in
`examples/system_monitor/`. It is an independent ESP-IDF project (ESP-IDF 6.1,
`esp32p4` target) that owns its own partition table, littlefs image, and
components. Build it with `idf.py` from that directory; see
[BROOKESIA_7B.md](BROOKESIA_7B.md) for the full description. Unlike
`firmware/brookesia`, it does not use the managed
`waveshare/esp32_p4_wifi6_touch_lcd_7b` component: the board support lives in
the vendored HAL board package so the newer ESP-Brookesia version can be used.

