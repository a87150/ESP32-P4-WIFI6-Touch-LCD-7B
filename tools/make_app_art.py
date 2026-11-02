#!/usr/bin/env python3
"""Generate the decorative cover art used by the bundled music player app.

The player has no embedded cover art (WAV files carry none), so the UI shows a
fixed, generated tile.  Keep this script around so the PNG can be regenerated
instead of being an unexplained binary blob.

Usage:
    python tools/make_app_art.py
"""

from __future__ import annotations

import math
from pathlib import Path

from PIL import Image, ImageDraw

SIZE = 200
OUTPUT = (
    Path(__file__).resolve().parent.parent
    / "firmware/brookesia_master/examples/system_monitor/components"
    / "brookesia_app_music/package/res/images/music_cover.png"
)

START = (99, 102, 241)     # indigo
END = (34, 211, 238)       # cyan


def gradient(size: int) -> Image.Image:
    image = Image.new("RGB", (size, size))
    draw = ImageDraw.Draw(image)
    for step in range(size * 2):
        ratio = step / float(size * 2 - 1)
        color = tuple(
            int(round(START[channel] + (END[channel] - START[channel]) * ratio))
            for channel in range(3)
        )
        draw.line([(step, 0), (0, step)], fill=color, width=1)
    return image


def draw_disc(draw: ImageDraw.ImageDraw, size: int) -> None:
    center = size / 2.0
    for index, radius in enumerate(range(76, 7, -7)):
        shade = 12 + index * 2
        draw.ellipse(
            [center - radius, center - radius, center + radius, center + radius],
            fill=(10, 14, 26),
            outline=(shade * 2, shade * 3, shade * 4 + 20),
            width=1,
        )
    label = 30
    draw.ellipse(
        [center - label, center - label, center + label, center + label],
        fill=(16, 22, 38),
        outline=(80, 96, 140),
        width=1,
    )
    draw.ellipse([center - 8, center - 8, center + 8, center + 8], fill=(34, 211, 238))
    # A soft highlight makes the disc read as a physical record rather than a
    # flat circle at small sizes.
    for step in range(3):
        radius = 68 - step * 6
        start_angle = math.radians(-140 + step * 4)
        end_angle = math.radians(-60 + step * 4)
        draw.arc(
            [center - radius, center - radius, center + radius, center + radius],
            start=math.degrees(start_angle),
            end=math.degrees(end_angle),
            fill=(140 + step * 20, 170 + step * 20, 210 + step * 15),
            width=2,
        )


def main() -> None:
    image = gradient(SIZE).convert("RGBA")
    draw_disc(ImageDraw.Draw(image), SIZE)
    # Re-apply the rounded alpha corner mask so the tile blends with the card.
    mask = Image.new("L", (SIZE, SIZE), 0)
    ImageDraw.Draw(mask).rounded_rectangle([0, 0, SIZE - 1, SIZE - 1], radius=36, fill=255)
    image.putalpha(mask)
    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    image.save(OUTPUT)
    print(f"wrote {OUTPUT} ({OUTPUT.stat().st_size} bytes)")


if __name__ == "__main__":
    main()
