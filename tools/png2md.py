#!/usr/bin/env python3
# /// script
# requires-python = ">=3.11"
# dependencies = ["numpy"]
# ///
"""
Tile bank for the Mega Drive frontend (md/): build/tiles.bin -> SGDK resources.

    uv run tools/png2md.py            # after tools/build_tiles.py

The Agon tiles are 24x24 RGBA2222 with 39 distinct opaque colours; the Mega
Drive shows 15 colours per palette line and one line per 8x8 hardware tile.
The renderer composes a field's layers as 8-bit chunky pixels (global colour
index = colour6 + 1, 0 = transparent) and then picks, for each 8x8 block, the
palette line that holds the block's colours. This tool

  1. writes the chunky pixels of all tiles (md/res/tile_pixels.bin, BIN resource) and,
     for the fast field painter, per 24x24 tile: the 4 bpp data of its nine 8x8 blocks
     for each map palette line (tile_blocks.bin), which pixels of each block are
     opaque (tile_op.bin, one byte per row) and the colour set of each block
     (tile_cm.bin, two u32: colour6 0..31, 32..63),
  2. chooses the four palette lines (14 colours each; nibble 0 is always black,
     nibble 15 always white) so that as few composed blocks as possible need a
     colour outside their line, judged on every tile alone and on every
     non-floor tile over every floor,
  3. writes md/src/gen/tiles_md.c with the tile table, the lines (MD 9-bit
     colours), their colour masks and the per-line colour lookup. A colour
     missing from a line maps to the nearest colour the line has.

Outputs are generated and not committed (md/res/tile_pixels.bin, md/src/gen/);
md/res/resources.res (committed) pulls tile_pixels.bin in as a BIN resource.
"""
from __future__ import annotations

import re
import struct
import sys
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parent.parent
TILES_BIN = ROOT / "build" / "tiles.bin"
TILES_H = ROOT / "src" / "core" / "gen" / "tiles.h"
OUT_PIX = ROOT / "md" / "res" / "tile_pixels.bin"
OUT_BLK = ROOT / "md" / "res" / "tile_blocks.bin"
OUT_OP = ROOT / "md" / "res" / "tile_op.bin"
OUT_CM = ROOT / "md" / "res" / "tile_cm.bin"
OUT_C = ROOT / "md" / "src" / "gen" / "tiles_md.c"
OUT_H = ROOT / "md" / "src" / "gen" / "tiles_md.h"

BLACK, WHITE = 0, 63            # colour6 of black / white
LINES, SLOTS = 3, 14            # map palette lines; free nibbles 1..14 per line (line 3 is the UI line)
# MD DAC levels for the Agon's four channel levels 0/85/170/255 (3 bit: 0..7)
MD_LEVEL = (0, 2, 5, 7)
# UI line (palette line 3): the logical colours of colors.h (C_BLACK .. C_BRIGHT_WHITE),
# 3-bit MD levels per channel: normal = 5, grey = 2, bright = 7. Index 0 is transparent
# (the map shows through); index 5 (C_MAGENTA's slot) is an OPAQUE black for text backgrounds.
UI_LINE = [(0, 0, 0), (5, 0, 0), (0, 5, 0), (5, 5, 0), (0, 0, 5), (0, 0, 0), (0, 5, 5), (5, 5, 5),
           (2, 2, 2), (7, 0, 0), (0, 7, 0), (7, 7, 0), (0, 0, 7), (7, 0, 7), (0, 7, 7), (7, 7, 7)]


def load_tiles():
    d = TILES_BIN.read_bytes()
    assert d[:4] == b"LOCT"
    n = d[5] | d[6] << 8
    sizes = [(d[7 + 2 * i], d[8 + 2 * i]) for i in range(n)]
    off = 7 + 2 * n
    tiles = []
    for w, h in sizes:
        raw = np.frombuffer(d[off:off + w * h], dtype=np.uint8)
        off += w * h
        g = np.where(raw >> 6 != 0, (raw & 63).astype(np.int16) + 1, 0).astype(np.uint8)
        tiles.append((w, h, g.reshape(h, w)))
    return tiles


def load_names():
    names = re.findall(r"T_([A-Z0-9_]+) = (\d+)", TILES_H.read_text())
    out = {}
    for name, idx in names:
        out[int(idx)] = name.lower()
    return out


def rgb(c6):
    return (c6 & 3, (c6 >> 2) & 3, (c6 >> 4) & 3)       # 2-bit r, g, b (agon RGBA2222 bitmaps: R bits 0-1, G 2-3, B 4-5)


def md_colour(c6):
    r, g, b = rgb(c6)
    return (MD_LEVEL[b] << 9) | (MD_LEVEL[g] << 5) | (MD_LEVEL[r] << 1)


def blocks_of(img24):
    """Colour6 sets (as 64-bit masks, black/white left out) of the 8x8 blocks of a 24x24 image."""
    out = []
    for by in range(3):
        for bx in range(3):
            blk = img24[by * 8:by * 8 + 8, bx * 8:bx * 8 + 8]
            m = 0
            for g in np.unique(blk):
                if g and g - 1 not in (BLACK, WHITE):
                    m |= 1 << int(g - 1)
            out.append(m)
    return out


def compose(floor, over):
    return np.where(over != 0, over, floor)


def choose_lines(tiles, names):
    floors = [i for i, n in names.items() if n.startswith("floor_") and "_half_" not in n]
    weights: dict[int, float] = {}

    def add(img, wgt):
        for m in blocks_of(img):
            if m:
                weights[m] = weights.get(m, 0.0) + wgt

    for i, (w, h, g) in enumerate(tiles):
        if w != 24:
            continue
        is_floor = names[i].startswith("floor_")
        add(g, 6.0 if is_floor else 1.0)
        if not is_floor and (g == 0).any():
            for f in floors:
                add(compose(tiles[f][2], g), 1.0 / len(floors) * 4)
    colours = sorted({c for m in weights for c in range(64) if m >> c & 1})
    cidx = {c: k for k, c in enumerate(colours)}
    masks = np.zeros((len(weights), len(colours)), dtype=bool)
    wv = np.zeros(len(weights))
    for r, (m, wgt) in enumerate(weights.items()):
        wv[r] = wgt
        for c in colours:
            if m >> c & 1:
                masks[r, cidx[c]] = True
    k = len(colours)
    print(f"[png2md] {len(colours)} free colours, {len(weights)} distinct block sets")

    rng = np.random.default_rng(7)

    def missing(line):
        return (masks & ~line).sum(axis=1)

    def cost(ls):
        return float((np.min([missing(l) for l in ls], axis=0) * wv).sum())

    best_all, best_cost = None, 1e18
    for restart in range(6):
        lines = []
        for _ in range(LINES):
            l = np.zeros(k, dtype=bool)
            l[rng.choice(k, SLOTS, replace=False)] = True
            lines.append(l)
        cur = cost(lines)
        improved = True
        while improved:
            improved = False
            for li in range(LINES):
                ins = [j for j in range(k) if lines[li][j]]
                outs = [j for j in range(k) if not lines[li][j]]
                rng.shuffle(ins)
                for a in ins:
                    bestmove, bestc = None, cur
                    for b in outs:
                        lines[li][a], lines[li][b] = False, True
                        c = cost(lines)
                        lines[li][a], lines[li][b] = True, False
                        if c < bestc - 1e-9:
                            bestmove, bestc = b, c
                    if bestmove is not None:
                        lines[li][a], lines[li][bestmove] = False, True
                        outs = [j for j in range(k) if not lines[li][j]]
                        cur, improved = bestc, True
        print(f"[png2md] restart {restart}: uncovered colour-slots {cur:.1f}")
        if cur < best_cost:
            best_cost, best_all = cur, [l.copy() for l in lines]
    total = float((masks.sum(axis=1) * wv).sum())
    miss = np.min([missing(l) for l in best_all], axis=0)
    exact = float(wv[miss == 0].sum() / wv.sum())
    print(f"[png2md] best: {best_cost:.1f}; blocks fully inside one line: {exact * 100:.1f} % (weighted)")
    return [[colours[j] for j in range(k) if l[j]] for l in best_all]


def main():
    if not TILES_BIN.exists() or not TILES_H.exists():
        sys.exit("run tools/build_tiles.py first")
    tiles = load_tiles()
    names = load_names()
    assert len(tiles) == len(names)

    OUT_PIX.parent.mkdir(parents=True, exist_ok=True)
    OUT_C.parent.mkdir(parents=True, exist_ok=True)
    blob = bytearray()
    table = []
    for w, h, g in tiles:
        table.append((len(blob), w, h))
        blob += g.tobytes()
    OUT_PIX.write_bytes(bytes(blob))

    lines = choose_lines(tiles, names)
    # palette: nibble 0 black, 1..len free colours, 15 white
    pal, mask_lo, mask_hi, lut = [], [], [], []
    for line in lines:
        cols = [BLACK] + line + [None] * (SLOTS - len(line)) + [WHITE]
        pal.append([md_colour(c if c is not None else BLACK) for c in cols])
        have = {c: n for n, c in enumerate(cols) if c is not None}
        mask = 0
        for c in have:
            mask |= 1 << c
        mask_lo.append(mask & 0xFFFFFFFF)
        mask_hi.append(mask >> 32)
        row = [0] * 65                      # index: global colour (colour6 + 1), 0 = transparent -> black
        for c6 in range(64):
            if c6 in have:
                row[c6 + 1] = have[c6]
                continue
            r0, g0, b0 = rgb(c6)
            bestn, bestd = 0, 1e9
            for c, n in have.items():
                r1, g1, b1 = rgb(c)
                d = 3 * (r0 - r1) ** 2 + 4 * (g0 - g1) ** 2 + 2 * (b0 - b1) ** 2
                if d < bestd:
                    bestn, bestd = n, d
            row[c6 + 1] = bestn
        lut.append(row)

    pal.append([(b << 9) | (g << 5) | (r << 1) for r, g, b in UI_LINE])
    # fast-painter data: 4 bpp blocks per line, opaque rows, colour sets
    blk, op, cm = bytearray(), bytearray(), bytearray()
    for w, h, g in tiles:
        if w != 24:
            blk += bytes(LINES * 9 * 32)
            op += bytes(9 * 8)
            cm += bytes(9 * 8)
            continue
        for l in range(LINES):
            nib = np.array(lut[l], dtype=np.uint32)[g]
            for by in range(3):
                for bx in range(3):
                    for r in range(8):
                        v = 0
                        for cx in range(8):
                            v = (v << 4) | int(nib[by * 8 + r, bx * 8 + cx])
                        blk += struct.pack(">I", v)
        for by in range(3):
            for bx in range(3):
                lo = hi = 0
                for r in range(8):
                    bits = 0
                    for cx in range(8):
                        pv = int(g[by * 8 + r, bx * 8 + cx])
                        bits = (bits << 1) | (1 if pv else 0)
                        if pv:
                            c6 = pv - 1
                            if c6 < 32:
                                lo |= 1 << c6
                            else:
                                hi |= 1 << (c6 - 32)
                    op.append(bits)
                cm += struct.pack(">II", lo, hi)
    # bit b of a tile's mask: all 64 pixels of block b are opaque (a layer above hides what is below)
    opaque = []
    for i, (w, h, g) in enumerate(tiles):
        m = 0
        if w == 24:
            for b in range(9):
                if all(op[(i * 9 + b) * 8 + r] == 0xFF for r in range(8)):
                    m |= 1 << b
        opaque.append(m)
    OUT_BLK.write_bytes(bytes(blk))
    OUT_OP.write_bytes(bytes(op))
    OUT_CM.write_bytes(bytes(cm))

    c = ["/* GENERATED by tools/png2md.py - do not edit. */", '#include "tiles_md.h"', ""]
    c.append(f"const TileInfo tile_info[TILE_MD_COUNT] = {{")
    for (off, w, h), opq in zip(table, opaque):
        c.append(f"    {{{off}UL, {w}, {h}, 0x{opq:03X}}},")
    c.append("};")
    c.append("")
    c.append("const u16 md_palette[4][16] = {")
    for p in pal:
        c.append("    {" + ", ".join(f"0x{v:03X}" for v in p) + "},")
    c.append("};")
    c.append("")
    c.append("/* bit n of line_mask[l][n / 32]: colour6 n is in palette line l (black and white always) */")
    c.append("const u32 line_mask[MAP_LINES][2] = {")
    for lo, hi in zip(mask_lo, mask_hi):
        c.append(f"    {{0x{lo:08X}UL, 0x{hi:08X}UL}},")
    c.append("};")
    c.append("")
    c.append("const u8 line_lut[MAP_LINES][65] = {")
    for row in lut:
        c.append("    {" + ",".join(str(v) for v in row) + "},")
    c.append("};")
    OUT_C.write_text("\n".join(c) + "\n")

    OUT_H.write_text(f"""/* GENERATED by tools/png2md.py - do not edit. */
#ifndef LOC_TILES_MD_H
#define LOC_TILES_MD_H

#include <genesis.h>

#define TILE_MD_COUNT {len(tiles)}
#define MAP_LINES {LINES}      /* palette lines 0..{LINES - 1} hold map colours, line {LINES} the UI colours */

typedef struct {{
    u32 off;                    /* into tile_pixels */
    u8 w, h;
    u16 opaque;                 /* bit b: block b (24x24 tiles) has no transparent pixel */
}} TileInfo;

/* chunky pixels of all tiles: colour6 + 1, 0 = transparent (BIN resource) */
extern const u8 tile_pixels[];
/* fast painter: [tile][line][block][row] 4 bpp longs; [tile][block][row] opaque bits (px 0 = bit 7);
 * [tile][block] {lo, hi} colour6 set (see tools/png2md.py) */
extern const u32 tile_blocks[];
extern const u8 tile_op[];
extern const u32 tile_cm[];
extern const TileInfo tile_info[TILE_MD_COUNT];
extern const u16 md_palette[4][16];
extern const u32 line_mask[MAP_LINES][2];
extern const u8 line_lut[MAP_LINES][65];

#endif
""")
    print(f"[png2md] {len(tiles)} tiles, {len(blob)} bytes -> {OUT_PIX.relative_to(ROOT)}, {OUT_C.relative_to(ROOT)}")


if __name__ == "__main__":
    main()
