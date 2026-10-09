#!/usr/bin/env python3
# /// script
# requires-python = ">=3.11"
# dependencies = ["pillow"]
# ///
"""
Tiles for the night map (GDD D54): windows, fence and gate, bridges, flower
beds, glowing mushrooms, swamp bubbles.

    uv run tools/art/make_night_set.py [--only NAME_PREFIX]

Writes assets/tiles/*.png (24x24, Agon palette only). The PNGs are the source
afterwards; run this again only for the tiles you want to redraw.
"""

from __future__ import annotations

import argparse
import random
import sys
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent.parent
TILES = ROOT / "assets" / "tiles"

BLACK = (0, 0, 0)
WHITE = (255, 255, 255)
GREY = (170, 170, 170)
DGREY = (85, 85, 85)
DBROWN = (85, 0, 0)
BROWN = (170, 85, 0)
LWOOD = (255, 170, 85)
ORANGE = (255, 170, 0)
YELLOW = (255, 255, 85)
OLIVE = (85, 85, 0)


class Canvas:
    def __init__(self, base: Image.Image | None = None):
        self.im = base.copy() if base else Image.new("RGBA", (24, 24), (0, 0, 0, 0))

    def px(self, x: int, y: int, c) -> None:
        if 0 <= x < 24 and 0 <= y < 24:
            self.im.putpixel((x, y), (*c, 255))

    def rect(self, x0: int, y0: int, x1: int, y1: int, c) -> None:
        """Inclusive rectangle."""
        for y in range(y0, y1 + 1):
            for x in range(x0, x1 + 1):
                self.px(x, y, c)


def load(name: str) -> Image.Image:
    return Image.open(TILES / f"{name}.png").convert("RGBA")


def save(name: str, c: Canvas, only: str | None) -> None:
    if only and not name.startswith(only):
        return
    c.im.save(TILES / f"{name}.png")
    print(f"[night-set] {name}")


# --- windows -------------------------------------------------------------

def pane(c: Canvas, x0: int, y0: int, x1: int, y1: int) -> None:
    """Lit window: frame, warm pane glowing from the bottom, cross mullion."""
    c.rect(x0, y0, x1, y1, DBROWN)
    c.rect(x0 + 1, y0 + 1, x1 - 1, y1 - 1, YELLOW)
    mid = (y0 + y1) // 2
    c.rect(x0 + 1, mid + 1, x1 - 1, y1 - 1, ORANGE)
    c.rect((x0 + x1) // 2, y0 + 1, (x0 + x1) // 2 + 1 if (x1 - x0) % 2 == 0 else (x0 + x1) // 2,
           y1 - 1, DBROWN)
    c.rect(x0 + 1, mid, x1 - 1, mid, DBROWN)


def windows(only):
    c = Canvas(load("wall_10"))              # horizontal wall: bricks, front view
    pane(c, 7, 10, 16, 17)                   # D81: lower than the door (frame top row 6)
    c.rect(6, 18, 17, 18, GREY)              # sill
    save("window_h", c, only)
    c = Canvas(load("wall_05"))              # vertical wall: the narrow column
    pane(c, 9, 7, 14, 16)
    save("window_v", c, only)


# --- fence and gate ------------------------------------------------------

def post(c: Canvas, x: int, y0: int, y1: int) -> None:
    c.rect(x, y0, x + 3, y1, BROWN)
    c.rect(x, y0, x, y1, LWOOD)                  # lit left edge
    c.rect(x + 3, y0, x + 3, y1, DBROWN)         # shaded right edge
    c.rect(x, y0 - 1, x + 3, y0 - 1, LWOOD)      # cap
    c.rect(x + 1, y0 - 2, x + 2, y0 - 2, LWOOD)


def rails_h(c: Canvas, x0: int, x1: int) -> None:
    for y in (9, 15):
        c.rect(x0, y, x1, y, LWOOD)
        c.rect(x0, y + 1, x1, y + 1, BROWN)
        c.rect(x0, y + 2, x1, y + 2, DBROWN)


def fences(only):
    for mask in range(16):
        c = Canvas()
        n, e, s, w = mask & 1, mask & 2, mask & 4, mask & 8
        if e:
            rails_h(c, 12, 23)
        if w:
            rails_h(c, 0, 11)
        if n:                                    # runs away from the viewer: one bar
            c.rect(11, 0, 12, 11, BROWN)
            c.rect(11, 0, 11, 11, LWOOD)
        if s:
            c.rect(11, 12, 12, 23, BROWN)
            c.rect(11, 12, 11, 23, LWOOD)
            c.rect(12, 12, 12, 23, DBROWN)
        post(c, 10, 6, 21)
        save(f"fence_{mask:02d}", c, only)


def gate(only):
    # horizontal (between fence posts left/right)
    c = Canvas()
    post(c, 1, 5, 21)
    post(c, 19, 5, 21)
    c.rect(5, 7, 18, 20, BROWN)
    for x in range(5, 19, 4):
        c.rect(x, 7, x, 20, DBROWN)              # plank seams
        c.rect(x + 1, 7, x + 1, 20, LWOOD)
    for i in range(14):                          # diagonal brace
        c.px(5 + i, 19 - i, DBROWN)
        c.px(5 + i, 18 - i, DBROWN)
    c.rect(5, 7, 18, 7, DBROWN)
    c.rect(5, 20, 18, 20, DBROWN)
    c.rect(11, 12, 12, 13, ORANGE)               # latch
    save("gate_h_closed", c, only)

    c = Canvas()
    post(c, 1, 5, 21)
    post(c, 19, 5, 21)
    for x0, step in ((5, 1), (18, -1)):          # leaves swung back along the posts
        for k in range(3):
            c.rect(x0 + step * k if step > 0 else x0 - k, 8, x0 + step * k if step > 0 else x0 - k, 20,
                   BROWN if k < 2 else DBROWN)
    c.rect(5, 8, 5, 20, LWOOD)
    c.rect(18, 8, 18, 20, LWOOD)
    save("gate_h_open", c, only)

    # vertical (fence runs north-south: seen from the side, one narrow leaf)
    c = Canvas()
    post(c, 10, 0, 6)
    post(c, 10, 17, 22)
    c.rect(9, 7, 14, 16, BROWN)
    c.rect(9, 7, 9, 16, LWOOD)
    c.rect(14, 7, 14, 16, DBROWN)
    for y in (9, 12, 14):
        c.rect(10, y, 13, y, DBROWN)
    c.rect(12, 11, 13, 12, ORANGE)
    save("gate_v_closed", c, only)

    c = Canvas()
    post(c, 10, 0, 6)
    post(c, 10, 17, 22)
    c.rect(9, 7, 12, 8, BROWN)                   # leaf swung out, foreshortened
    c.rect(9, 9, 11, 10, LWOOD)
    c.rect(9, 11, 10, 12, BROWN)
    save("gate_v_open", c, only)


# --- bridges -------------------------------------------------------------

def bridge(only):
    # spans east-west: planks run across, rails top and bottom
    c = Canvas()
    c.rect(0, 0, 23, 23, BROWN)
    for x in range(0, 24, 6):
        c.rect(x, 4, x, 19, DBROWN)
        c.rect(x + 1, 4, x + 1, 19, LWOOD)
    for x in (2, 8, 15, 20):
        c.px(x, 8, DBROWN)
        c.px(x + 2, 15, DBROWN)
    for y0, rows in ((0, (LWOOD, BROWN, BROWN, DBROWN)), (20, (LWOOD, BROWN, BROWN, DBROWN))):
        for i, col in enumerate(rows):
            c.rect(0, y0 + i, 23, y0 + i, col)
    for x in (0, 11, 22):                        # rail posts
        c.rect(x, 0, x + 1, 3, LWOOD)
        c.rect(x, 20, x + 1, 23, LWOOD)
    save("floor_bridge_h", c, only)
    # spans north-south
    c = Canvas()
    c.rect(0, 0, 23, 23, BROWN)
    for y in range(0, 24, 6):
        c.rect(4, y, 19, y, DBROWN)
        c.rect(4, y + 1, 19, y + 1, LWOOD)
    for y in (2, 8, 15, 20):
        c.px(8, y, DBROWN)
        c.px(15, y + 2, DBROWN)
    for x0 in (0, 20):
        for i, col in enumerate((LWOOD, BROWN, BROWN, DBROWN)):
            c.rect(x0 + i, 0, x0 + i, 23, col)
    for y in (0, 11, 22):
        c.rect(0, y, 3, y + 1, LWOOD)
        c.rect(20, y, 23, y + 1, LWOOD)
    save("floor_bridge_v", c, only)


# --- decor ---------------------------------------------------------------

FLOWER_COLOURS = [((255, 85, 170), YELLOW), (WHITE, (255, 85, 85)), ((170, 85, 255), YELLOW),
                  (YELLOW, ORANGE)]


def flower(c: Canvas, x: int, y: int, petal, heart) -> None:
    """One flower with stem, leaf and a little mound of earth."""
    c.rect(x - 1, y + 5, x + 1, y + 5, DBROWN)
    c.rect(x, y + 1, x, y + 4, (0, 170, 0))
    c.px(x + 1, y + 3, (0, 85, 0))
    c.px(x - 1, y + 2, (0, 85, 0))
    c.px(x - 1, y, petal)
    c.px(x + 1, y, petal)
    c.px(x, y - 1, petal)
    c.px(x, y + 1, petal if False else (0, 170, 0))
    c.px(x, y, heart)


def flowers(only):
    rng = random.Random(54)
    for v in range(3):
        c = Canvas()
        spots = [(4, 6), (11, 4), (18, 7), (7, 13), (15, 12), (20, 17), (3, 18), (11, 18)]
        rng.shuffle(spots)
        for i, (x, y) in enumerate(spots[:7]):
            petal, heart = FLOWER_COLOURS[(i + v) % len(FLOWER_COLOURS)]
            flower(c, x + rng.randint(-1, 1), y, petal, heart)
        save(f"decor_flowers_{v}", c, only)


def mushrooms(only):
    for f in range(2):
        c = Canvas()
        cap, dot = ((255, 85, 255), WHITE) if f == 0 else ((255, 170, 255), (170, 255, 255))
        for (x, y, s) in ((7, 14, 3), (16, 10, 2), (13, 18, 2)):
            c.rect(x - 1, y + 1, x, y + s + 2, (170, 170, 170))        # stalk
            c.rect(x - s, y - 1, x + s - 1, y, cap)                       # cap
            c.rect(x - s + 1, y - 2, x + s - 2, y - 2, cap)
            c.px(x - 1, y - 1, dot)
            c.px(x + 1, y - 2, dot)
            c.rect(x - s, y + 1, x + s - 1, y + 1, (170, 0, 170))        # gills
        if f == 1:                                # spores drift up
            for (x, y) in ((6, 6), (17, 4), (10, 9)):
                c.px(x, y, (170, 255, 255))
        save(f"decor_mush_{f}", c, only)


def bubbles(only):
    spots = [[(6, 16, 2), (15, 10, 3), (11, 20, 1)], [(7, 12, 2), (15, 6, 3), (11, 15, 1)]]
    for f, sp in enumerate(spots):
        c = Canvas()
        for (x, y, r) in sp:
            ring = (85, 255, 255) if r > 1 else (0, 170, 170)
            for dx in range(-r, r + 1):
                for dy in range(-r, r + 1):
                    d2 = dx * dx + dy * dy
                    if r * r - r <= d2 <= r * r + r:
                        c.px(x + dx, y + dy, ring)
            c.px(x - 1, y - 1, WHITE if r > 1 else ring)
        save(f"decor_bubble_{f}", c, only)


# --- woods ---------------------------------------------------------------
# The dead wood (shadow wood floor) is a graveyard of bare trees, stumps and
# bones; the enchanted wood (magic wood floor) has twisted violet trunks with
# glowing teal crowns and glow mushrooms. Both are full 24x24 floor tiles.

def line(c: Canvas, x0: int, y0: int, x1: int, y1: int, col) -> None:
    n = max(abs(x1 - x0), abs(y1 - y0), 1)
    for i in range(n + 1):
        c.px(round(x0 + (x1 - x0) * i / n), round(y0 + (y1 - y0) * i / n), col)


def bare_tree(c: Canvas, x: int, base: int, h: int, rng: random.Random) -> None:
    """A dead tree: forked trunk, no leaves."""
    top = base - h
    for y in range(top + 3, base + 1):           # trunk, 2 px wide
        c.px(x, y, DGREY)
        c.px(x + 1, y, (85, 85, 85) if y % 3 else BLACK)
        c.px(x - 1 if y > base - 3 else x, y, DGREY)   # root flare
    c.px(x, top + 3, GREY)
    for side in (-1, 1):                          # two main limbs, each forking
        fx = x + (1 if side > 0 else 0)
        fy = top + 4 + rng.randint(0, 2)
        ex, ey = fx + side * rng.randint(4, 6), top - rng.randint(0, 2)
        line(c, fx, fy, ex, ey, GREY)
        mx, my = (fx + ex) // 2, (fy + ey) // 2
        line(c, mx, my, mx + side * 3, my - 3, DGREY)
        line(c, ex, ey, ex - side, ey - 3, DGREY)
    line(c, x, top + 3, x, top - 1, GREY)         # leader


def stump(c: Canvas, x: int, y: int) -> None:
    c.rect(x, y, x + 4, y + 3, (85, 85, 85))
    c.rect(x, y, x + 4, y, GREY)
    c.rect(x + 1, y + 1, x + 3, y + 1, DBROWN)    # the cut face
    c.px(x - 1, y + 3, (85, 85, 85))
    c.px(x + 5, y + 3, (85, 85, 85))


def bones(c: Canvas, x: int, y: int) -> None:
    c.rect(x, y, x + 3, y + 1, WHITE)             # skull
    c.px(x, y + 1, BLACK)
    c.px(x + 2, y + 1, BLACK)
    line(c, x - 3, y + 4, x + 6, y + 3, GREY)     # a long bone
    c.px(x - 3, y + 3, GREY)
    c.px(x + 6, y + 4, GREY)


def deadwood(variant: int, only):
    rng = random.Random(540 + variant)
    c = Canvas()
    c.rect(0, 0, 23, 23, BLACK)
    for _ in range(14):                           # dark earth specks
        c.px(rng.randint(0, 23), rng.randint(0, 23), DBROWN)
    layouts = [[(6, 21, 15), (17, 15, 12)], [(5, 15, 11), (14, 23, 16), (20, 12, 8)],
               [(8, 20, 14), (19, 22, 13), (2, 12, 8)]]
    for (x, base, h) in layouts[variant]:
        bare_tree(c, x, base, h, rng)
    if variant == 0:
        stump(c, 14, 19)
    elif variant == 1:
        bones(c, 3, 19)
    else:
        stump(c, 2, 20)
        bones(c, 14, 4)
    name = "floor_shadowwood" if variant == 0 else f"floor_shadowwood_{variant}"
    save(name, c, only)


def glow_tree(c: Canvas, x: int, base: int, r: int, rng: random.Random) -> None:
    """A twisted trunk under a glowing teal crown with sparkles."""
    for y in range(base - 9, base + 1):
        sway = 1 if y < base - 5 else 0
        c.px(x + sway, y, (170, 85, 255))
        c.px(x + sway + 1, y, (85, 0, 170))
    line(c, x + 1, base - 7, x - 3, base - 11, (170, 85, 255))
    line(c, x + 1, base - 6, x + 5, base - 10, (170, 85, 255))
    cy = base - 12
    for dy in range(-r + 1, r):                   # the crown: a lumpy disc
        for dx in range(-r - 2, r + 3):
            if dx * dx / ((r + 2) ** 2) + dy * dy / (r * r) <= 1:
                col = (0, 170, 170)
                if dy + dx // 2 < -r // 2:
                    col = (0, 255, 255)
                elif dy > r // 2:
                    col = (0, 85, 85)
                c.px(x + dx, cy + dy, col)
    for _ in range(7):                            # sparkles
        c.px(x + rng.randint(-r - 3, r + 3), cy + rng.randint(-r - 2, r + 3), WHITE)
    c.rect(x - 1, base + 1, x + 2, base + 1, (0, 85, 85))


def glow_mushroom(c: Canvas, x: int, y: int) -> None:
    c.px(x, y + 1, GREY)
    c.rect(x - 1, y, x + 1, y, (255, 85, 255))
    c.px(x, y - 1, (255, 85, 255))
    c.px(x - 1, y, WHITE)


def enchanted(variant: int, only):
    rng = random.Random(780 + variant)
    c = Canvas()
    c.rect(0, 0, 23, 23, BLACK)
    for _ in range(16):
        c.px(rng.randint(0, 23), rng.randint(0, 23), (0, 85, 85) if rng.random() < .5 else (0, 0, 85))
    layouts = [[(7, 22, 5), (17, 17, 4)], [(5, 17, 4), (15, 23, 5)], [(10, 21, 5), (19, 14, 3)]]
    for (x, base, r) in layouts[variant]:
        glow_tree(c, x, base, r, rng)
    for (x, y) in ((3, 22), (13, 22), (20, 19))[: 2 + variant % 2]:
        glow_mushroom(c, x, y)
    name = "floor_magicwood" if variant == 0 else f"floor_magicwood_{variant}"
    save(name, c, only)


def woods(only):
    for v in range(3):
        deadwood(v, only)
        enchanted(v, only)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--only", help="redraw tiles whose name starts with this")
    a = ap.parse_args()
    for fn in (windows, fences, gate, bridge, flowers, mushrooms, bubbles, woods):
        fn(a.only)
    return 0


if __name__ == "__main__":
    sys.exit(main())
