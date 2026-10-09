#!/usr/bin/env python3
# /// script
# requires-python = ">=3.11"
# dependencies = ["pillow", "numpy"]
# ///
"""
Title picture and map variants for the Mega Drive frontend (md/).

    uv run tools/md_assets.py            # after tools/build_tiles.py and tools/gen_variants.py

Title: assets/title/title.png (320x240, 28 colours) is cut to 320x224 (the MD screen) and turned
into 4 bpp tiles: two palette lines of 15 colours (nibble 0 = black), every 8x8 tile uses one of
them, identical tiles are stored once.

  md/res/title_tiles.bin   unique tiles, 32 bytes each
  md/res/title_map.bin     40x28 big-endian tilemap words (palette line in bits 13/14, tile index
                           counted from 0; the frontend adds the VRAM base)
  md/res/title_pal.bin     two lines of 16 MD colours
  md/res/pic_win_*.bin, pic_lose_*.bin   the 96x96 end pictures, same layout (12x12 tilemap)
  md/src/gen/title_md.h    TITLE_TILES, PIC_WIN_TILES, PIC_LOSE_TILES

Maps: the 16 terrain variants of scenario 1 (build/maps/mcl_vNN.map) become BIN resources
(md/res/mcl_vNN.bin); md/src/gen/maps_md.[ch] holds their lengths and a table.
"""
from __future__ import annotations

import struct
import sys
from pathlib import Path

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
RES = ROOT / "md" / "res"
GEN = ROOT / "md" / "src" / "gen"
MD_LEVEL = (0, 2, 5, 7)            # same mapping of the Agon's 0/85/170/255 as tools/png2md.py
LINES, SLOTS = 2, 15
VARIANTS = 16
SCREEN_W, SCREEN_H, CROP_TOP = 320, 224, 8


def md_colour(rgb):
    lvl = {0: 0, 85: 1, 170: 2, 255: 3}
    r, g, b = (MD_LEVEL[lvl[min(lvl, key=lambda k: abs(k - c))]] for c in rgb)
    return (b << 9) | (g << 5) | (r << 1)


def tile_sets(img):
    sets = []
    for ty in range(SCREEN_H // 8):
        for tx in range(SCREEN_W // 8):
            blk = img[ty * 8:(ty + 1) * 8, tx * 8:(tx + 1) * 8]
            sets.append(frozenset(int(v) for v in np.unique(blk)))
    return sets


def choose_lines(sets, ncol):
    """Two lines (sets of colour indices, black excluded) so that most tiles fit into one."""
    rng = np.random.default_rng(3)
    weights = {}
    for s in sets:
        s = frozenset(c for c in s if c != 0)
        weights[s] = weights.get(s, 0) + 1
    items = list(weights.items())

    def cost(lines):
        total = 0
        for s, w in items:
            total += w * min(len(s - l) for l in lines)
        return total

    cols = list(range(1, ncol))
    best, bestc = None, 1 << 30
    for _ in range(40):
        lines = [set(), set()]
        order = list(cols)
        rng.shuffle(order)
        for i, c in enumerate(order):
            lines[i % 2].add(c)
        c0 = cost(lines)
        improved = True
        while improved:
            improved = False
            for a in cols:
                for li in (0, 1):
                    if a in lines[li]:
                        other = 1 - li
                        # move a to the other line when it has room, or swap with one of its colours
                        if len(lines[other]) < SLOTS:
                            lines[li].discard(a)
                            lines[other].add(a)
                            c = cost(lines)
                            if c < c0:
                                c0, improved = c, True
                            else:
                                lines[other].discard(a)
                                lines[li].add(a)
                        for b in list(lines[other]):
                            lines[li].discard(a)
                            lines[other].discard(b)
                            lines[li].add(b)
                            lines[other].add(a)
                            c = cost(lines)
                            if c < c0:
                                c0, improved = c, True
                                break
                            lines[li].discard(b)
                            lines[other].discard(a)
                            lines[li].add(a)
                            lines[other].add(b)
        if c0 < bestc and all(len(l) <= SLOTS for l in lines):
            best, bestc = [set(l) for l in lines], c0
    return best, bestc


def convert(name, src, width, height):
    """An RGB image of width x height pixels (multiples of 8) -> tiles, tilemap and two palette lines."""
    px = np.array(src)
    uniq = sorted({tuple(int(v) for v in p) for p in px.reshape(-1, 3)}, key=lambda c: (sum(c), c))
    black = (0, 0, 0)
    if black in uniq:
        uniq.remove(black)
    uniq.insert(0, black)                      # colour index 0 = black
    idx = {c: i for i, c in enumerate(uniq)}
    ind = np.zeros((height, width), dtype=np.uint8)
    for y in range(height):
        for x in range(width):
            ind[y, x] = idx[tuple(int(v) for v in px[y, x])]
    cols = width // 8
    sets = []
    for ty in range(height // 8):
        for tx in range(cols):
            blk = ind[ty * 8:(ty + 1) * 8, tx * 8:(tx + 1) * 8]
            sets.append(frozenset(int(v) for v in np.unique(blk)))
    lines, cost = choose_lines(sets, len(uniq))
    print(f"[md_assets] {name}: {len(uniq)} colours, uncovered colour slots {cost}")
    nib = []
    for l in lines:
        order = sorted(l)
        m = {0: 0}
        for n, c in enumerate(order, start=1):
            m[c] = n
        nib.append((order, m))

    def nearest(c, order):
        r, g, b = uniq[c]
        best, bd = 0, 1 << 30
        for k, o in enumerate([0] + order):
            rr, gg, bb = uniq[o]
            d = (r - rr) ** 2 + (g - gg) ** 2 + (b - bb) ** 2
            if d < bd:
                best, bd = k, d
        return best

    tiles, tmap = {}, []
    out = bytearray()
    for ti, s in enumerate(sets):
        ty, tx = divmod(ti, cols)
        blk = ind[ty * 8:(ty + 1) * 8, tx * 8:(tx + 1) * 8]
        want = {c for c in s if c != 0}
        li = min((0, 1), key=lambda k: len(want - lines[k]))
        order, m = nib[li]
        data = bytearray()
        for r in range(8):
            v = 0
            for c in range(8):
                col = int(blk[r, c])
                n = m.get(col)
                if n is None:
                    n = nearest(col, order)
                v = (v << 4) | n
            data += struct.pack(">I", v)
        key = (li, bytes(data))
        if key not in tiles:
            tiles[key] = len(tiles)
            out += data
        tmap.append((li << 13) | tiles[key])
    n_tiles = len(tiles)
    assert n_tiles <= 880, f"{n_tiles} tiles: too many for the VRAM budget"
    (RES / f"{name}_tiles.bin").write_bytes(bytes(out))
    (RES / f"{name}_map.bin").write_bytes(b"".join(struct.pack(">H", v) for v in tmap))
    pal = bytearray()
    for order, _ in nib:
        cl = [black] + [uniq[c] for c in order]
        cl += [black] * (16 - len(cl))
        for c in cl:
            pal += struct.pack(">H", md_colour(c))
    (RES / f"{name}_pal.bin").write_bytes(bytes(pal))
    print(f"[md_assets] {name}: {n_tiles} unique tiles")
    return n_tiles


def title():
    src = Image.open(ROOT / "assets" / "title" / "title.png").convert("RGB")
    assert src.size == (320, 240)
    src = src.crop((0, CROP_TOP, SCREEN_W, CROP_TOP + SCREEN_H))
    n = convert("title", src, SCREEN_W, SCREEN_H)
    pics = {"win": convert("pic_win", Image.open(ROOT / "assets" / "title" / "win.png").convert("RGB"), 96, 96),
            "lose": convert("pic_lose", Image.open(ROOT / "assets" / "title" / "lose.png").convert("RGB"), 96, 96)}
    (GEN / "title_md.h").write_text(
        "/* GENERATED by tools/md_assets.py - do not edit. */\n"
        "#ifndef LOC_TITLE_MD_H\n#define LOC_TITLE_MD_H\n"
        f"#define TITLE_TILES {n}\n#define PIC_WIN_TILES {pics['win']}\n#define PIC_LOSE_TILES {pics['lose']}\n"
        "extern const unsigned char title_tiles[], title_map[], title_pal[];\n"
        "extern const unsigned char pic_win_tiles[], pic_win_map[], pic_win_pal[];\n"
        "extern const unsigned char pic_lose_tiles[], pic_lose_map[], pic_lose_pal[];\n#endif\n")


def variants():
    lens = []
    for v in range(VARIANTS):
        data = (ROOT / "build" / "maps" / f"mcl_v{v:02d}.map").read_bytes()
        (RES / f"mcl_v{v:02d}.bin").write_bytes(data)
        lens.append(len(data))
    h = ["/* GENERATED by tools/md_assets.py - do not edit. */", "#ifndef LOC_MAPS_MD_H", "#define LOC_MAPS_MD_H", "",
         f"#define MCL_VARIANTS {VARIANTS}", "extern const unsigned char *const mcl_variant[MCL_VARIANTS];",
         "extern const unsigned short mcl_variant_len[MCL_VARIANTS];", "", "#endif", ""]
    (GEN / "maps_md.h").write_text("\n".join(h))
    c = ["/* GENERATED by tools/md_assets.py - do not edit. */", '#include "maps_md.h"', ""]
    for v in range(VARIANTS):
        c.append(f"extern const unsigned char mcl_v{v:02d}[];")
    c.append("const unsigned char *const mcl_variant[MCL_VARIANTS] = {")
    c += [f"    mcl_v{v:02d}," for v in range(VARIANTS)]
    c.append("};")
    c.append("const unsigned short mcl_variant_len[MCL_VARIANTS] = {" + ", ".join(str(n) for n in lens) + "};")
    (GEN / "maps_md.c").write_text("\n".join(c) + "\n")
    print(f"[md_assets] {VARIANTS} map variants, {sum(lens)} bytes")


def main():
    RES.mkdir(parents=True, exist_ok=True)
    GEN.mkdir(parents=True, exist_ok=True)
    title()
    variants()


if __name__ == "__main__":
    sys.exit(main())
