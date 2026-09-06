#!/usr/bin/env python3
"""Generate a 48x48, 16-color avatar and its packed I4 firmware header."""

from __future__ import annotations

import argparse
from pathlib import Path

from PIL import Image, ImageDraw


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_PNG_OUTPUT = ROOT / "assets" / "images" / "avatar-48x48-passport-palette.png"
DEFAULT_HEADER_OUTPUT = ROOT / "main" / "passport_avatar.h"

PALETTE = [
    0x031D17, 0x07352A, 0x0C4C3D, 0x55B88A,
    0xD8E7D4, 0x070A0B, 0x111719, 0x323333,
    0x7A503D, 0xA87250, 0xD89968, 0xF0B37E,
    0xFFD0A0, 0xA34F43, 0x71665B, 0x161C1E,
]


def rgb(value: int) -> tuple[int, int, int]:
    return (value >> 16 & 0xFF, value >> 8 & 0xFF, value & 0xFF)


def default_avatar() -> Image.Image:
    """Return an original geometric robot avatar for the public build."""
    image = Image.new("RGB", (48, 48), rgb(PALETTE[0]))
    draw = ImageDraw.Draw(image)
    draw.rectangle((6, 6, 41, 41), fill=rgb(PALETTE[1]), outline=rgb(PALETTE[3]), width=2)
    draw.line((24, 3, 24, 8), fill=rgb(PALETTE[3]), width=2)
    draw.rectangle((21, 1, 27, 5), fill=rgb(PALETTE[4]))
    draw.rectangle((10, 12, 37, 32), fill=rgb(PALETTE[4]), outline=rgb(PALETTE[3]), width=2)
    draw.rectangle((14, 17, 19, 22), fill=rgb(PALETTE[0]))
    draw.rectangle((28, 17, 33, 22), fill=rgb(PALETTE[0]))
    draw.rectangle((16, 27, 31, 29), fill=rgb(PALETTE[3]))
    draw.rectangle((12, 36, 35, 39), fill=rgb(PALETTE[2]))
    return image


def palette_bytes() -> list[int]:
    values: list[int] = []
    for color in PALETTE:
        values.extend(rgb(color))
    values.extend([0] * (768 - len(values)))
    return values


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", type=Path, help="Portrait image; omit for the public robot avatar")
    parser.add_argument("--png-output", type=Path, default=DEFAULT_PNG_OUTPUT)
    parser.add_argument("--header-output", type=Path, default=DEFAULT_HEADER_OUTPUT)
    args = parser.parse_args()

    if args.input:
        with Image.open(args.input) as source:
            small = source.convert("RGB").resize((48, 48), Image.Resampling.BOX)
    else:
        small = default_avatar()

    palette_rgb = [rgb(value) for value in PALETTE]
    indices: list[int] = []
    for red, green, blue in small.getdata():
        distances = [
            (red - pr) ** 2 + (green - pg) ** 2 + (blue - pb) ** 2
            for pr, pg, pb in palette_rgb
        ]
        indices.append(min(range(16), key=distances.__getitem__))

    indexed = Image.new("P", (48, 48))
    indexed.putpalette(palette_bytes())
    indexed.putdata(indices)
    args.png_output.parent.mkdir(parents=True, exist_ok=True)
    indexed.save(args.png_output, optimize=True)

    packed = bytearray()
    for offset in range(0, len(indices), 2):
        packed.append((indices[offset] << 4) | indices[offset + 1])
    lines = []
    for offset in range(0, len(packed), 16):
        row = ", ".join(f"0x{value:02x}" for value in packed[offset:offset + 16])
        lines.append(f"    {row},")

    args.header_output.write_text(
        "#pragma once\n\n"
        "#include <stdint.h>\n\n"
        "#define PASSPORT_AVATAR_W 48\n"
        "#define PASSPORT_AVATAR_H 48\n\n"
        "static const uint8_t PASSPORT_AVATAR_I4[PASSPORT_AVATAR_W * PASSPORT_AVATAR_H / 2] = {\n"
        + "\n".join(lines)
        + "\n};\n",
        encoding="utf-8",
    )
    print(args.png_output)
    print(args.header_output)


if __name__ == "__main__":
    main()
