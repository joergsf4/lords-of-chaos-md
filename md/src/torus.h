/*
 * Bookkeeping of the scrolling map plane (plane B as a torus), kept free of hardware so that it
 * can be tested on the host (md/test/test_units.c). The window origin counts continuously (also
 * across the wrap of the world); every quantity the renderer needs is a residue of it:
 *   slot  = field coordinate mod RING      the field's tile data slot in VRAM
 *   cell  = 3 x field mod plane size       its first tilemap cell on the plane
 *   scroll = 24 x field mod plane pixels   the plane's scroll registers
 * They are updated by one-field steps without any division.
 */
#ifndef LOC_TORUS_H
#define LOC_TORUS_H

#include <genesis.h>

#define RING 10                             /* field slots per axis in VRAM */
#define PLANE_W 64                          /* plane B size in tiles */
#define PLANE_H 32
#define FIELD_PX 24

typedef struct {
    s16 vox, voy;                           /* virtual position of window field (0, 0); only compared */
    u8 rox, roy;                            /* vox mod RING, voy mod RING */
    u8 rtx, rty;                            /* 3 vox mod PLANE_W, 3 voy mod PLANE_H */
    u16 hpx, vpx;                           /* 24 vox mod 512, 24 voy mod 256 */
} Torus;

static inline s32 torus_mod(s32 a, s32 n)
{
    s32 r = a % n;
    return r < 0 ? r + n : r;
}

/* The origin is set afresh. */
static inline void torus_set(Torus *t, s16 x, s16 y)
{
    t->vox = x;
    t->voy = y;
    t->rox = torus_mod(x, RING);
    t->roy = torus_mod(y, RING);
    t->rtx = torus_mod(x * 3, PLANE_W);
    t->rty = torus_mod(y * 3, PLANE_H);
    t->hpx = torus_mod(x * FIELD_PX, PLANE_W * 8);
    t->vpx = torus_mod(y * FIELD_PX, PLANE_H * 8);
}

/* The origin moved by one field per axis at most (dx, dy in -1..1). */
static inline void torus_step(Torus *t, s8 dx, s8 dy)
{
    t->vox += dx;
    t->voy += dy;
    if (dx) {
        t->rox = dx > 0 ? (t->rox + 1 == RING ? 0 : t->rox + 1) : (t->rox ? t->rox - 1 : RING - 1);
        t->rtx = (t->rtx + (dx > 0 ? 3 : PLANE_W - 3)) % PLANE_W;
        t->hpx = (t->hpx + (dx > 0 ? FIELD_PX : PLANE_W * 8 - FIELD_PX)) % (PLANE_W * 8);
    }
    if (dy) {
        t->roy = dy > 0 ? (t->roy + 1 == RING ? 0 : t->roy + 1) : (t->roy ? t->roy - 1 : RING - 1);
        t->rty = (t->rty + (dy > 0 ? 3 : PLANE_H - 3)) % PLANE_H;
        t->vpx = (t->vpx + (dy > 0 ? FIELD_PX : PLANE_H * 8 - FIELD_PX)) % (PLANE_H * 8);
    }
}

/* Data slot and first tilemap cell of window field (vx, vy). */
static inline void torus_field(const Torus *t, u8 vx, u8 vy, u8 *sx, u8 *sy, u8 *tx, u8 *ty)
{
    u8 x = t->rox + vx, y = t->roy + vy, cx = t->rtx + vx * 3, cy = t->rty + vy * 3;
    *sx = x >= RING ? x - RING : x;
    *sy = y >= RING ? y - RING : y;
    *tx = cx >= PLANE_W ? cx - PLANE_W : cx;
    *ty = cy >= PLANE_H ? cy - PLANE_H : cy;
}

#endif
