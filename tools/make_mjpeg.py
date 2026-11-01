#!/usr/bin/env python3
"""Build MJPEG movie files for the ESP32-P4 video app.

The board's video app plays plain concatenated JPEG pictures (``.mjpeg`` /
``.mjpg``).  This script turns an animated GIF, a folder of pictures, or a
single picture into such a file.

Examples
--------
    python tools/make_mjpeg.py frames/ video.mjpeg --size 640x360
    python tools/make_mjpeg.py clip.gif video.mjpeg --fps 12

Frame timing is not stored inside the file: the player paces pictures at a
fixed rate (12 fps by default), so pick ``--fps`` accordingly when extracting.

Requires Pillow (``pip install pillow``).
"""

from __future__ import annotations

import argparse
import io
import sys
from pathlib import Path

try:
    from PIL import Image
except ImportError:  # pragma: no cover - friendly message for the user
    sys.exit("Pillow is required: pip install pillow")


IMAGE_SUFFIXES = {".png", ".jpg", ".jpeg", ".bmp", ".webp", ".gif", ".tif", ".tiff"}


def parse_size(text: str) -> tuple[int, int] | None:
    if not text:
        return None
    try:
        width, height = text.lower().split("x", 1)
        return int(width), int(height)
    except ValueError as error:
        raise argparse.ArgumentTypeError("size must look like 640x360") from error


def collect_inputs(source: Path) -> list[Path]:
    if source.is_dir():
        files = [p for p in sorted(source.iterdir()) if p.suffix.lower() in IMAGE_SUFFIXES]
        if not files:
            raise SystemExit(f"no pictures found in {source}")
        return files
    if not source.is_file():
        raise SystemExit(f"input not found: {source}")
    return [source]


def fit(image: Image.Image, size: tuple[int, int] | None) -> Image.Image:
    image = image.convert("RGB")
    if size is None:
        return image
    # Keep the aspect ratio and letterbox on black so nothing is distorted.
    target_w, target_h = size
    scale = min(target_w / image.width, target_h / image.height)
    resized = image.resize(
        (max(1, round(image.width * scale)), max(1, round(image.height * scale))),
        Image.Resampling.LANCZOS,
    )
    canvas = Image.new("RGB", (target_w, target_h), (0, 0, 0))
    canvas.paste(resized, ((target_w - resized.width) // 2, (target_h - resized.height) // 2))
    return canvas


def encode(frames, output: Path, quality: int) -> tuple[int, int]:
    count = 0
    total = 0
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("wb") as handle:
        for frame in frames:
            buffer = io.BytesIO()
            frame.save(buffer, format="JPEG", quality=quality, optimize=True, progressive=False)
            data = buffer.getvalue()
            handle.write(data)
            count += 1
            total += len(data)
    return count, total


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("source", type=Path, help="animated GIF/WebP, a picture, or a folder of pictures")
    parser.add_argument("output", type=Path, help="destination .mjpeg file")
    parser.add_argument("--size", type=parse_size, default=None, help="scale/letterbox to WxH")
    parser.add_argument("--quality", type=int, default=70, help="JPEG quality (1-95, default 70)")
    parser.add_argument("--max-frames", type=int, default=0, help="stop after N frames (0 = all)")
    args = parser.parse_args()

    inputs = collect_inputs(args.source)
    if len(inputs) == 1 and inputs[0].suffix.lower() in {".gif", ".webp"}:
        with Image.open(inputs[0]) as animation:
            frames = []
            for index in range(getattr(animation, "n_frames", 1)):
                animation.seek(index)
                frames.append(fit(animation, args.size))
                if args.max_frames and len(frames) >= args.max_frames:
                    break
        if len(frames) <= 1 and not getattr(animation, "is_animated", False):
            frames = [fit(Image.open(inputs[0]), args.size)]
    else:
        frames = []
        for path in inputs:
            if args.max_frames and len(frames) >= args.max_frames:
                break
            with Image.open(path) as picture:
                frames.append(fit(picture, args.size))

    count, total = encode(frames, args.output, args.quality)
    width, height = frames[0].size
    print(
        f"{args.output}: {count} frame(s), {width}x{height}, "
        f"{total / 1024:.0f} KiB ({total / 1024 / 1024 * 8:.1f} Mbit)"
    )


if __name__ == "__main__":
    main()
