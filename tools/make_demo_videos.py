#!/usr/bin/env python3
"""Generate the two sample movies that ship in ``littlefs/videos``.

The ESP32-P4 video app plays concatenated JPEG pictures and draws them 1:1 when
they fit the screen area (976x432 on the 7B board), so the samples are rendered
at exactly that size for a full-bleed, fast playback.

    python tools/make_demo_videos.py

Requires Pillow (``pip install pillow``).
"""

from __future__ import annotations

import io
import math
from pathlib import Path

from PIL import Image

WIDTH = 976
HEIGHT = 432

ROOT = Path(__file__).resolve().parents[1]
OUT_DIR = ROOT / "firmware" / "brookesia_master" / "examples" / "system_monitor" / "littlefs" / "videos"


def encode_jpeg(image: Image.Image, quality: int) -> bytes:
    buffer = io.BytesIO()
    image.save(buffer, format="JPEG", quality=quality, optimize=True, progressive=False)
    return buffer.getvalue()


def write_movie(path: Path, frames: list[Image.Image], quality: int) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    total = 0
    with path.open("wb") as handle:
        for frame in frames:
            data = encode_jpeg(frame, quality)
            handle.write(data)
            total += len(data)
    print(f"{path.name}: {len(frames)} frame(s), {WIDTH}x{HEIGHT}, {total / 1024:.0f} KiB")


def aurora_frames(count: int) -> list[Image.Image]:
    """Soft moving light ribbons over a night sky."""
    ribbons = [
        (0.0016, 2.1, (40, 220, 190), 0.00),
        (0.0023, 1.4, (70, 160, 255), 0.35),
        (0.0011, 3.0, (150, 110, 255), 0.68),
    ]
    frames: list[Image.Image] = []
    for index in range(count):
        phase = index / count
        image = Image.new("RGB", (WIDTH, HEIGHT))
        pixels = image.load()
        for y in range(HEIGHT):
            sky = y / HEIGHT
            base_r = int(6 + 10 * sky)
            base_g = int(10 + 16 * sky)
            base_b = int(26 + 30 * sky)
            for x in range(WIDTH):
                r = base_r
                g = base_g
                b = base_b
                for frequency, speed, color, offset in ribbons:
                    wave = math.sin(x * frequency + phase * speed * math.tau + offset * math.tau)
                    wave += 0.45 * math.sin(x * frequency * 2.3 - phase * speed * 1.7 + offset * 5.0)
                    distance = abs(y - (HEIGHT * (0.35 + 0.18 * math.sin(offset * 6.0 + phase * 2.0))))
                    glow = max(0.0, 1.0 - distance / (HEIGHT * 0.55))
                    strength = max(0.0, wave) * glow * glow
                    r += int(color[0] * strength * 0.55)
                    g += int(color[1] * strength * 0.55)
                    b += int(color[2] * strength * 0.55)
                pixels[x, y] = (min(r, 255), min(g, 255), min(b, 255))
        frames.append(image)
    return frames


def color_bars_frames(count: int) -> list[Image.Image]:
    """Static colour bars with a sweeping highlight, handy for checking colour order."""
    colors = [
        (235, 235, 235),
        (235, 235, 40),
        (40, 235, 235),
        (40, 200, 60),
        (220, 60, 220),
        (225, 45, 45),
        (45, 70, 225),
        (18, 18, 18),
    ]
    frames: list[Image.Image] = []
    bar_width = WIDTH // len(colors)
    for index in range(count):
        image = Image.new("RGB", (WIDTH, HEIGHT), (0, 0, 0))
        pixels = image.load()
        sweep = (index / count) * WIDTH
        for bar, color in enumerate(colors):
            x0 = bar * bar_width
            x1 = WIDTH if bar == len(colors) - 1 else x0 + bar_width
            for x in range(x0, x1):
                boost = max(0.0, 1.0 - abs(x - sweep) / 90.0) * 60.0
                tint = (
                    min(255, int(color[0] + boost)),
                    min(255, int(color[1] + boost)),
                    min(255, int(color[2] + boost)),
                )
                for y in range(HEIGHT):
                    pixels[x, y] = tint
        frames.append(image)
    return frames


def main() -> None:
    write_movie(OUT_DIR / "aurora_demo.mjpeg", aurora_frames(48), quality=62)
    write_movie(OUT_DIR / "color_bars.mjpeg", color_bars_frames(12), quality=82)


if __name__ == "__main__":
    main()
