#!/usr/bin/env python3
# /// script
# requires-python = ">=3.11"
# dependencies = ["pillow"]
# ///
"""
Convert assets/tiles/*.png + assets/icons/*.png into the Agon tile bank.

    uv run tools/build_tiles.py

Outputs (both generated, not committed):
  build/tiles.bin          tile bank for the SD card (RGBA2222, see below)
  src/core/gen/tiles.h     enum TileId { T_FLOOR_STONE, ... } for the core

tiles.bin layout (little endian):
  "LOCT" | u8 version=1 | u16 count | count * (u8 w, u8 h) | pixel data
  Pixels are RGBA2222, one byte each, rows top to bottom:
  bits 0-1 R, 2-3 G, 4-5 B, 6-7 A (A=0 transparent, 3 opaque).

Derived tiles generated here (GDD 11.2/11.3):
  - owner variants of creatures with key colours: <name>_p1.._p4, _neutral
  - half floors for wall tiles: <floor>_half_n/s/w/e
"""

from __future__ import annotations

import re
import struct
import sys
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools" / "art"))
from night import nightify  # noqa: E402
from palette import KEY_DARK, KEY_LIGHT, OWNERS, PALETTE  # noqa: E402

TILES = ROOT / "assets" / "tiles"
ICONS = ROOT / "assets" / "icons"
OUT_BIN = ROOT / "build" / "tiles.bin"
OUT_H = ROOT / "src" / "core" / "gen" / "tiles.h"

OWNED = tuple(l.split(",", 1)[0] for l in (ROOT / "data" / "creatures.csv")
              .read_text(encoding="utf-8").splitlines()[1:]
              if l and not l.startswith(("#", "id,")))   # every creature has key colours
FLOORS = ("floor_stone", "floor_wood", "floor_grass", "floor_path", "floor_tallgrass",
          "floor_forest", "floor_magicwood", "floor_shadowwood", "floor_swamp",
          "floor_water_0", "floor_rubble")
# Must match the wall split in src/core/view.c and tools/mockup.py.
HALF_BOXES = {"n": (0, 0, 24, 3), "s": (0, 9, 24, 24), "w": (0, 0, 8, 24), "e": (16, 0, 24, 24)}


def rgba2222(r: int, g: int, b: int, a: int) -> int:
    if a == 0:
        return 0
    return (r >> 6) | ((g >> 6) << 2) | ((b >> 6) << 4) | (3 << 6)


def check(name: str, im: Image.Image) -> None:
    for y in range(im.height):
        for x in range(im.width):
            r, g, b, a = im.getpixel((x, y))
            if a not in (0, 255):
                raise SystemExit(f"{name}: semi-transparent pixel at {x},{y}")
            if a and (r, g, b) not in PALETTE:
                raise SystemExit(f"{name}: colour {(r, g, b)} at {x},{y} not in Agon palette")


def owner_variant(im: Image.Image, owner: str) -> Image.Image:
    light, dark = OWNERS[owner]
    out = im.copy()
    for y in range(out.height):
        for x in range(out.width):
            r, g, b, a = out.getpixel((x, y))
            if a and (r, g, b) == KEY_LIGHT:
                out.putpixel((x, y), (*light, 255))
            elif a and (r, g, b) == KEY_DARK:
                out.putpixel((x, y), (*dark, 255))
    return out


def half_floor(im: Image.Image, box) -> Image.Image:
    out = Image.new("RGBA", im.size, (0, 0, 0, 0))
    out.paste(im.crop(box), box[:2])
    return out


def dither(im: Image.Image, keep) -> Image.Image:
    """Keep only the pixels where keep(x, y) holds: a see-through version
    (D80: soft edge of a lifted roof, 50% checker / 25% grid)."""
    out = Image.new("RGBA", im.size, (0, 0, 0, 0))
    for y in range(im.height):
        for x in range(im.width):
            if keep(x, y):
                out.putpixel((x, y), im.getpixel((x, y)))
    return out


def collect() -> list[tuple[str, Image.Image]]:
    entries: list[tuple[str, Image.Image]] = []
    for p in sorted(TILES.glob("*.png")):
        im = Image.open(p).convert("RGBA")
        check(p.name, im)
        im = nightify(p.stem, im)          # D54: the game plays at night
        if im.size != (24, 24):
            raise SystemExit(f"{p.name}: expected 24x24, got {im.size}")
        frame = re.fullmatch(r"(.+)_f[12]", p.stem)     # idle frames of a creature
        if p.stem in OWNED or (frame and frame.group(1) in OWNED):
            for owner in OWNERS:
                entries.append((f"{p.stem}_{owner}", owner_variant(im, owner)))
        else:
            entries.append((p.stem, im))
        if p.stem == "roof":
            entries.append(("roof_half", dither(im, lambda x, y: (x + y) % 2 == 0)))
            entries.append(("roof_faint", dither(im, lambda x, y: x % 2 == 0 and y % 2 == 0)))
        if p.stem in FLOORS:
            for d, box in HALF_BOXES.items():
                entries.append((f"{p.stem}_half_{d}", half_floor(im, box)))
    for p in sorted(ICONS.glob("*.png")):
        im = Image.open(p).convert("RGBA")
        check(p.name, im)
        entries.append((f"icon_{p.stem}", im))
    return entries


def main() -> int:
    entries = collect()
    OUT_BIN.parent.mkdir(parents=True, exist_ok=True)
    OUT_H.parent.mkdir(parents=True, exist_ok=True)

    head = b"LOCT" + struct.pack("<BH", 1, len(entries))
    sizes = b"".join(struct.pack("BB", im.width, im.height) for _, im in entries)
    pixels = bytearray()
    for _, im in entries:
        for y in range(im.height):
            for x in range(im.width):
                pixels.append(rgba2222(*im.getpixel((x, y))))
    OUT_BIN.write_bytes(head + sizes + bytes(pixels))

    lines = [
        "/* GENERATED by tools/build_tiles.py - do not edit. */",
        "#ifndef LOC_GEN_TILES_H", "#define LOC_GEN_TILES_H", "",
        "#define TILE_PX 24", "", "typedef enum {",
    ]
    lines += [f"    T_{name.upper()} = {i}," for i, (name, _) in enumerate(entries)]
    lines += [f"    TILE_COUNT = {len(entries)}", "} TileId;", "", "#endif", ""]
    OUT_H.write_text("\n".join(lines), newline="\n")

    print(f"[tiles] {len(entries)} entries, {OUT_BIN.stat().st_size} bytes -> "
          f"{OUT_BIN.relative_to(ROOT).as_posix()}, {OUT_H.relative_to(ROOT).as_posix()}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
