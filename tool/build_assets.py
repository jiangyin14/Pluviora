"""Generate Pluviora's neutral preview atlas and notification sounds."""

from __future__ import annotations

import math
from pathlib import Path
import struct
import wave

from PIL import Image, ImageDraw


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "files" / "resources" / "default"
ATLAS = OUTPUT / "preview_atlas.png"
SOUNDS = OUTPUT / "sounds"


def rounded_sprite(size: tuple[int, int], colors: tuple[str, str]) -> Image.Image:
    width, height = size
    image = Image.new("RGBA", size, (0, 0, 0, 0))
    draw = ImageDraw.Draw(image)
    margin = max(6, min(width, height) // 12)
    draw.rounded_rectangle(
        (margin, margin, width - margin, height - margin),
        radius=max(12, height // 3),
        fill=colors[0],
        outline=colors[1],
        width=max(4, height // 18),
    )
    draw.line(
        (margin * 2, height // 2, width - margin * 2, height // 2),
        fill=(255, 255, 255, 210),
        width=max(3, height // 28),
    )
    return image


def paste_center(atlas: Image.Image, sprite: Image.Image, box: tuple[int, int, int, int]) -> None:
    x, y, width, height = box
    sprite.thumbnail((width, height), Image.Resampling.LANCZOS)
    atlas.alpha_composite(sprite, (x + (width - sprite.width) // 2, y + (height - sprite.height) // 2))


def build_atlas() -> None:
    atlas = Image.new("RGBA", (2048, 1024), (0, 0, 0, 0))

    marker = Image.new("RGBA", (192, 192), (0, 0, 0, 0))
    marker_draw = ImageDraw.Draw(marker)
    marker_draw.ellipse((18, 18, 174, 174), fill=(44, 86, 122, 220), outline=(160, 226, 255, 255), width=10)
    marker_draw.ellipse((66, 66, 126, 126), fill=(225, 249, 255, 245))
    paste_center(atlas, marker, (0, 0, 192, 192))

    pause = Image.new("RGBA", (128, 128), (0, 0, 0, 0))
    pause_draw = ImageDraw.Draw(pause)
    pause_draw.rounded_rectangle((14, 14, 114, 114), radius=28, fill=(17, 32, 54, 230), outline=(151, 218, 255, 255), width=6)
    pause_draw.rounded_rectangle((42, 34, 55, 94), radius=5, fill=(238, 250, 255, 255))
    pause_draw.rounded_rectangle((73, 34, 86, 94), radius=5, fill=(238, 250, 255, 255))
    paste_center(atlas, pause, (192, 0, 128, 128))

    compact = [
        ((320, 0, 192, 192), ("#4c8aa8", "#c5f1ff")),
        ((512, 0, 192, 192), ("#4777a8", "#d8ecff")),
        ((704, 0, 192, 192), ("#635d9e", "#ebe4ff")),
        ((896, 0, 192, 192), ("#87638f", "#ffe0f8")),
        ((1088, 0, 192, 192), ("#9a6b69", "#ffe4db")),
    ]
    for box, colors in compact:
        paste_center(atlas, rounded_sprite((192, 192), colors), box)

    wide = [
        ((0, 256, 576, 192), ("#315f7c", "#c4efff")),
        ((576, 256, 576, 192), ("#4e5688", "#e4e2ff")),
        ((1152, 256, 576, 192), ("#76556f", "#ffe0f4")),
        ((0, 448, 576, 192), ("#775a4d", "#ffe8d7")),
    ]
    for box, colors in wide:
        paste_center(atlas, rounded_sprite((576, 192), colors), box)

    OUTPUT.mkdir(parents=True, exist_ok=True)
    atlas.save(ATLAS, optimize=True)


def build_sound(path: Path, frequency: float) -> None:
    sample_rate = 48_000
    duration = 0.085
    frames = int(sample_rate * duration)
    path.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(path), "wb") as output:
        output.setnchannels(2)
        output.setsampwidth(2)
        output.setframerate(sample_rate)
        for index in range(frames):
            time = index / sample_rate
            envelope = math.exp(-42.0 * time) * min(1.0, index / 48.0)
            sample = int(11_500 * envelope * math.sin(2.0 * math.pi * frequency * time))
            output.writeframesraw(struct.pack("<hh", sample, sample))


def main() -> None:
    build_atlas()
    build_sound(SOUNDS / "primary.wav", 920.0)
    build_sound(SOUNDS / "secondary.wav", 620.0)


if __name__ == "__main__":
    main()
