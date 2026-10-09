#include "render_md.h"
#include "torus.h"

#include "gen/tiles.h"
#include "gen/tiles_md.h"
#include "gen/data.h"

#define MOUNT_PX 18                         /* a mount shrunk behind its rider (render.c) */
#define RIDE_LIFT 3
#define WADE_DROP 8
#define FOE_COLOUR (48 + 1)                 /* bright red, colour6 + 1 */
#define VRAM_BASE TILE_USER_INDEX           /* 9 hardware tiles per field slot */
#define CURSOR_TILE (TILE_USER_INDEX + RING * RING * 9 + GLYPH_MAX + 54)   /* after the UI tiles (ui_md.c) */

static u8 fbuf[FIELD_PX * FIELD_PX];        /* chunky field, colour6 + 1, 0 = nothing */
static u8 small[MOUNT_PX * MOUNT_PX];
static u32 tiles[9][8];                     /* the nine 8x8 blocks of a field, 4 bpp */

/* A tile at (x, y) inside the field buffer, clipped. Only 24x24 tiles. */
static void blit(u16 id, s16 x, s16 y)
{
    const u8 *src = tile_pixels + tile_info[id].off;
    s16 sy, x0 = x < 0 ? -x : 0, x1 = x + FIELD_PX > FIELD_PX ? FIELD_PX - x : FIELD_PX;
    for (sy = 0; sy < FIELD_PX; sy++) {
        s16 dy = y + sy, sx;
        u8 *dst;
        if (dy < 0 || dy >= FIELD_PX)
            continue;
        dst = fbuf + dy * FIELD_PX + x;
        for (sx = x0; sx < x1; sx++) {
            u8 p = src[sy * FIELD_PX + sx];
            if (p)
                dst[sx] = p;
        }
    }
}

/* The 18x18 copy of a mount tile (render.c shrink_mount): black wins so the
 * outline stays closed, otherwise the first opaque pixel when at least half
 * of the source box is opaque. */
static void shrink(u16 id)
{
    const u8 *src = tile_pixels + tile_info[id].off;
    u8 x, y;
    for (y = 0; y < MOUNT_PX; y++)
        for (x = 0; x < MOUNT_PX; x++) {
            u8 sx0 = x * FIELD_PX / MOUNT_PX, sx1 = ((x + 1) * FIELD_PX + MOUNT_PX - 1) / MOUNT_PX;
            u8 sy0 = y * FIELD_PX / MOUNT_PX, sy1 = ((y + 1) * FIELD_PX + MOUNT_PX - 1) / MOUNT_PX;
            u8 sx, sy, total = 0, opaque = 0, first = 0, black = 0;
            for (sy = sy0; sy < sy1 && sy < FIELD_PX; sy++)
                for (sx = sx0; sx < sx1 && sx < FIELD_PX; sx++) {
                    u8 p = src[sy * FIELD_PX + sx];
                    total++;
                    if (p) {
                        opaque++;
                        if (!first)
                            first = p;
                        if (p == 1)             /* colour6 0 = black outline */
                            black = p;
                    }
                }
            small[y * MOUNT_PX + x] = opaque * 2 >= total ? (black ? black : first) : 0;
        }
}

static void blit_small(s16 x, s16 y)
{
    u8 sy, sx;
    for (sy = 0; sy < MOUNT_PX; sy++)
        for (sx = 0; sx < MOUNT_PX; sx++) {
            u8 p = small[sy * MOUNT_PX + sx];
            s16 dx = x + sx, dy = y + sy;
            if (p && dx >= 0 && dx < FIELD_PX && dy >= 0 && dy < FIELD_PX)
                fbuf[dy * FIELD_PX + dx] = p;
        }
}

static bool is_mount(u16 tile)
{
    static const u8 kinds[4] = {CR_UNICORN, CR_PEGASUS, CR_GRYPHON, CR_ELEPHANT};
    u8 m;
    for (m = 0; m < 4; m++) {
        u16 base = CREATURE_TILE[kinds[m]];
        if (tile >= base && tile - base <= OWN_NEUTRAL)
            return TRUE;
    }
    return FALSE;
}

/* Draw the layers of field f. below = 0: the field's own layers at its place;
 * below = 1: only what the field BELOW overhangs into this one (flyers,
 * lifted creatures, riders: they draw up into the field above, GDD 11.3). */
static void draw_layers(const FieldLayers *f, u8 vx, u8 vy, bool below)
{
    u8 i, n = f->n;
    s16 base = below ? FIELD_PX : 0;
    for (i = 0; i < n; i++) {
        s16 x = 0, y = 0;
        u8 bob = view_bob(vx, vy);
        if (f->air & (1u << i))
            y -= 3;
        if ((bob & 15) == i + 1)
            y -= bob >> 4;
        if (f->wade == i + 1)
            y += WADE_DROP;
        if ((f->ride & (1u << i)) && i + 1 < n) {
            s16 my = y;
            y -= RIDE_LIFT;
            if (vy == 0 && !below && y < 0)
                y = 0;
            if (!below || y < 0)
                blit(f->id[i], x, base + y);
            if (!below) {
                if (is_mount(f->id[i + 1])) {
                    shrink(f->id[i + 1]);
                    blit_small((FIELD_PX - MOUNT_PX) / 2, my + FIELD_PX - MOUNT_PX);
                } else {
                    blit(f->id[i + 1], 0, my);
                }
            }
            i++;
            continue;
        }
        if (below) {
            if (y < 0)
                blit(f->id[i], x, base + y);
            continue;
        }
        if (y < 0 && vy == 0)
            y = 0;
        blit(f->id[i], x, y);
    }
}

static void frame_foe(void)
{
    u8 k;
    for (k = 0; k < FIELD_PX; k++) {
        fbuf[k] = fbuf[(FIELD_PX - 1) * FIELD_PX + k] = FOE_COLOUR;
        fbuf[k * FIELD_PX] = fbuf[k * FIELD_PX + FIELD_PX - 1] = FOE_COLOUR;
    }
}

/* One 8x8 block of fbuf -> tiles[blk] (4 bpp) and its palette line. */
static u8 convert_block(u8 bx, u8 by, u32 *out)
{
    const u8 *src = fbuf + by * 8 * FIELD_PX + bx * 8;
    u32 lo = 0, hi = 0;
    u8 r, c, l, line = 0, best = 255;
    const u8 *lut;
    for (r = 0; r < 8; r++)
        for (c = 0; c < 8; c++) {
            u8 g = src[r * FIELD_PX + c];
            if (g) {
                if (g <= 32)
                    lo |= 1UL << (g - 1);
                else
                    hi |= 1UL << (g - 33);
            }
        }
    for (l = 0; l < MAP_LINES; l++) {
        u32 ml = lo & ~line_mask[l][0], mh = hi & ~line_mask[l][1];
        u8 miss = 0;
        while (ml) { ml &= ml - 1; miss++; }
        while (mh) { mh &= mh - 1; miss++; }
        if (miss < best) {
            best = miss;
            line = l;
            if (!miss)
                break;
        }
    }
    lut = line_lut[line];
    for (r = 0; r < 8; r++) {
        const u8 *p = src + r * FIELD_PX;
        out[r] = ((u32)lut[p[0]] << 28) | ((u32)lut[p[1]] << 24) | ((u32)lut[p[2]] << 20) |
                 ((u32)lut[p[3]] << 16) | ((u32)lut[p[4]] << 12) | ((u32)lut[p[5]] << 8) |
                 ((u32)lut[p[6]] << 4) | lut[p[7]];
    }
    return line;
}

/* ---------- hardware scrolling ----------
 * Plane B is a torus: the field at virtual position (fx, fy) (the window origin counts
 * continuously, also across a wrap of the world) lives at tilemap tile (3 fx, 3 fy) modulo
 * the plane, and its tile data in slot (fx mod 10, fy mod 10). Scrolling is a change of the
 * plane's scroll registers plus painting only the fields that came into the window. */
static Torus tor;                           /* where the window is on the plane */
static u16 cur_base;                        /* tile data slot of the field being painted */
static u8 cur_tx, cur_ty;                   /* its tilemap cell */

/* Upload the nine tiles of tiles[] and point the field's tilemap cells at them. */
static void commit(const u8 *lines)
{
    u16 base = cur_base;
    u8 b;
    u16 attr[9];
    VDP_loadTileData((const u32 *)tiles, base, 9, CPU);
    for (b = 0; b < 9; b++)
        attr[b] = TILE_ATTR_FULL(lines[b], FALSE, FALSE, FALSE, base + b);
    if (cur_tx + 3 <= PLANE_W && cur_ty + 3 <= PLANE_H) {
        VDP_setTileMapDataRect(BG_B, attr, cur_tx, cur_ty, 3, 3, 3, CPU);
    } else {                                /* the field straddles the plane's wrap */
        for (b = 0; b < 9; b++)
            VDP_setTileMapXY(BG_B, attr[b], (cur_tx + b % 3) % PLANE_W, (cur_ty + b / 3) % PLANE_H);
    }
}

static void paint_slow(u8 vx, u8 vy)
{
    u8 lines[9];
    u8 bx, by;
    memset(fbuf, 0, sizeof fbuf);
    draw_layers(view_field(vx, vy), vx, vy, FALSE);
    if (vy + 1 < VIEW_H)
        draw_layers(view_field(vx, vy + 1), vx, (u8)(vy + 1), TRUE);
    if (view_field(vx, vy)->foe)
        frame_foe();
    for (by = 0; by < 3; by++)
        for (bx = 0; bx < 3; bx++) {
            lines[by * 3 + bx] = convert_block(bx, by, tiles[by * 3 + bx]);
        }
    commit(lines);
}

/* ---------- fast painter ----------
 * A field whose layers all sit at the field's own origin (no flyer, lift, rider, wade,
 * foe frame, nothing overhanging from below) is composed per 8x8 block straight from the
 * 4 bpp block data of its layers (tools/png2md.py): the palette line is chosen from the
 * layers' colour sets, layers hidden under an opaque layer are skipped. A block whose
 * colours fit no line exactly takes the exact chunky route. */
#ifndef EXACT_MISS
#define EXACT_MISS 99                       /* colours a block may lack in its line before it is composed exactly */
#endif
static u32 nibmask[256];

static bool fast_ok(const FieldLayers *f, u8 vx, u8 vy)
{
    u8 i;
    if (f->air || f->ride || f->wade || f->foe || view_bob(vx, vy))
        return FALSE;
    for (i = 0; i < f->n; i++)
        if (tile_info[f->id[i]].w != FIELD_PX)
            return FALSE;
    if (vy + 1 < VIEW_H) {
        const FieldLayers *b = view_field(vx, vy + 1);
        if (b->air || b->ride || view_bob(vx, vy + 1))
            return FALSE;
    }
    return TRUE;
}

/* The line with the fewest colours missing for a colour set; *exact = fits completely. */
static u8 pick_line(u32 lo, u32 hi, bool *exact)
{
    /* *exact is also true when only one colour is missing: the line's nearest-colour data is
     * used as it is, which is cheaper than composing the block exactly */
    u8 l, line = 0, best = 255;
    for (l = 0; l < MAP_LINES; l++) {
        u32 ml = lo & ~line_mask[l][0], mh = hi & ~line_mask[l][1];
        u8 miss = 0;
        while (ml) { ml &= ml - 1; miss++; }
        while (mh) { mh &= mh - 1; miss++; }
        if (miss < best) {
            best = miss;
            line = l;
            if (!miss)
                break;
        }
    }
    *exact = best <= EXACT_MISS;
    return line;
}

/* One block the slow way: exact colours of the visible pixels of layers s..n-1. */
static u8 exact_block(const FieldLayers *f, u8 s, u8 b, u32 *out)
{
    u8 blk[64], i, r, c, line;
    u32 lo = 0, hi = 0;
    const u8 *lut;
    bool exact;
    u8 bx = b % 3, by = b / 3;
    memset(blk, 0, sizeof blk);
    for (i = s; i < f->n; i++) {
        const u8 *src = tile_pixels + tile_info[f->id[i]].off + (by * 8) * FIELD_PX + bx * 8;
        for (r = 0; r < 8; r++)
            for (c = 0; c < 8; c++) {
                u8 p = src[r * FIELD_PX + c];
                if (p)
                    blk[r * 8 + c] = p;
            }
    }
    for (r = 0; r < 64; r++)
        if (blk[r]) {
            if (blk[r] <= 32)
                lo |= 1UL << (blk[r] - 1);
            else
                hi |= 1UL << (blk[r] - 33);
        }
    line = pick_line(lo, hi, &exact);
    lut = line_lut[line];
    for (r = 0; r < 8; r++) {
        const u8 *p = blk + r * 8;
        out[r] = ((u32)lut[p[0]] << 28) | ((u32)lut[p[1]] << 24) | ((u32)lut[p[2]] << 20) |
                 ((u32)lut[p[3]] << 16) | ((u32)lut[p[4]] << 12) | ((u32)lut[p[5]] << 8) |
                 ((u32)lut[p[6]] << 4) | lut[p[7]];
    }
    return line;
}

typedef struct {
    const u32 *cm;                          /* colour sets of the nine blocks */
    const u8 *op;                           /* opaque rows */
    const u32 *blk;                         /* 4 bpp block data, all map lines */
    u16 opaque;                             /* blocks without a transparent pixel */
} LRef;

static void paint_fast(u8 vx, u8 vy, const FieldLayers *f)
{
    LRef ref[VIEW_MAX_LAYERS];
    u8 lines[9], n = f->n, i, b;
    (void)vx;
    (void)vy;
    for (i = 0; i < n; i++) {
        u16 id = f->id[i];
        ref[i].cm = tile_cm + id * 18;
        ref[i].op = tile_op + id * 72;
        ref[i].blk = tile_blocks + id * (MAP_LINES * 72);
        ref[i].opaque = tile_info[id].opaque;
    }
    for (b = 0; b < 9; b++) {
        u32 *out = tiles[b];
        u32 lo = 0, hi = 0;
        u8 s = 0, r, line;
        bool exact;
        for (i = n; i > 0; i--)
            if (ref[i - 1].opaque & (1u << b)) {
                s = i - 1;
                break;
            }
        for (i = s; i < n; i++) {
            lo |= ref[i].cm[b * 2];
            hi |= ref[i].cm[b * 2 + 1];
        }
        line = pick_line(lo, hi, &exact);
        if (!exact) {
            line = exact_block(f, s, b, out);
        } else {
            for (i = s; i < n; i++) {
                const u32 *src = ref[i].blk + line * 72 + b * 8;
                if (i == s && (ref[i].opaque & (1u << b))) {
                    for (r = 0; r < 8; r++)
                        out[r] = src[r];
                } else {
                    const u8 *op = ref[i].op + b * 8;
                    if (i == s)
                        for (r = 0; r < 8; r++)
                            out[r] = 0;
                    for (r = 0; r < 8; r++) {
                        u32 m = nibmask[op[r]];
                        out[r] = (out[r] & ~m) | src[r];
                    }
                }
            }
            if (s >= n)
                for (r = 0; r < 8; r++)
                    out[r] = 0;
        }
        lines[b] = line;
    }
    commit(lines);
}

static void paint_field(u8 vx, u8 vy)
{
    const FieldLayers *f = view_field(vx, vy);
    if (fast_ok(f, vx, vy))
        paint_fast(vx, vy, f);
    else
        paint_slow(vx, vy);
}

void render_init(void)
{
    u8 l;
    u16 v, k;
    for (v = 0; v < 256; v++) {
        u32 m = 0;
        for (k = 0; k < 8; k++)
            if (v & (0x80 >> k))
                m |= 0xFUL << (28 - 4 * k);
        nibmask[v] = m;
    }
    for (l = 0; l < 4; l++)
        PAL_setColors(l * 16, md_palette[l], 16, CPU);
    VDP_setBackgroundColor(0);
    VDP_clearPlane(BG_A, TRUE);
    VDP_clearPlane(BG_B, TRUE);
    VDP_resetSprites();
    VDP_clearSprites();
}

/* What a slot shows, to skip repainting a field that only moved with the scroll. */
typedef struct {
    s16 fx, fy;                             /* virtual field painted here; fx = 0x7FFF = nothing */
    FieldLayers f;
    u8 bob, below;                          /* lift of its creature; the field below overhangs into it */
} Painted;
static Painted painted[RING][RING];

static const World *rworld;
static s16 last_ox = -1, last_oy = -1;

void render_set_world(const World *w)
{
    rworld = w;
    last_ox = -1;                           /* the next render_fields() starts afresh */
}

static void forget_all(void)
{
    u8 x, y;
    for (y = 0; y < RING; y++)
        for (x = 0; x < RING; x++)
            painted[y][x].fx = 0x7FFF;
}

static u8 below_over(u8 vx, u8 vy)
{
    if (vy + 1 >= VIEW_H)
        return 0;
    return (view_field(vx, vy + 1)->air | view_field(vx, vy + 1)->ride) != 0 || view_bob(vx, vy + 1) != 0;
}

/* The window moved by at most one field per axis: step the virtual origin, otherwise start afresh. */
static void follow_origin(void)
{
    s16 ox = view_origin_x(), oy = view_origin_y(), dx, dy;
    if (last_ox < 0) {
        torus_set(&tor, ox, oy);
        forget_all();
    } else {
        dx = ox - last_ox;
        dy = oy - last_oy;
        if (rworld && rworld->wrap) {
            if (dx > rworld->w / 2) dx -= rworld->w;
            if (dx < -rworld->w / 2) dx += rworld->w;
            if (dy > rworld->h / 2) dy -= rworld->h;
            if (dy < -rworld->h / 2) dy += rworld->h;
        }
        if (dx > 1 || dx < -1 || dy > 1 || dy < -1) {
            torus_set(&tor, tor.vox + dx, tor.voy + dy);         /* a jump: everything is painted again */
            forget_all();
        } else {
            torus_step(&tor, dx, dy);
        }
    }
    last_ox = ox;
    last_oy = oy;
}

u8 render_fields(void)
{
    u8 vx, vy, n = 0;
    s16 ox = view_origin_x(), oy = view_origin_y();
    bool scrolled = last_ox < 0 || ox != last_ox || oy != last_oy;
    if (scrolled)
        follow_origin();
    /* a repainted field redraws what its lower neighbour overhangs into it,
     * so the neighbour itself needs no repaint; the core marks the field
     * above when an overhang changes (view.c, overhangs()). After a scroll
     * every window field is checked against what its slot already shows. */
    for (vy = 0; vy < VIEW_H; vy++)
        for (vx = 0; vx < VIEW_W; vx++) {
            const FieldLayers *f;
            Painted *p;
            u8 bob, below, sx, sy, tx, ty;
            s16 fx, fy;
            if (!scrolled && !view_dirty(vx, vy))
                continue;
            fx = tor.vox + vx;
            fy = tor.voy + vy;
            torus_field(&tor, vx, vy, &sx, &sy, &tx, &ty);
            f = view_field(vx, vy);
            bob = view_bob(vx, vy);
            below = below_over(vx, vy);
            p = &painted[sy][sx];
            if (p->fy == fy && p->fx == fx && p->bob == bob && p->below == below &&
                p->f.n == f->n && p->f.air == f->air && p->f.ride == f->ride && p->f.foe == f->foe &&
                p->f.wade == f->wade) {
                u8 k = f->n;
                while (k && p->f.id[k - 1] == f->id[k - 1])
                    k--;
                if (!k)
                    continue;
            }
            cur_base = VRAM_BASE + (sy * RING + sx) * 9;
            cur_tx = tx;
            cur_ty = ty;
            paint_field(vx, vy);
            p->fx = fx;
            p->fy = fy;
            p->f = *f;
            p->bob = bob;
            p->below = below;
            n++;
        }
    view_clean();
#ifdef RENDER_FORCE_REPAINT
    last_ox = -1;                           /* test build: every call paints everything afresh */
#endif
    VDP_setHorizontalScroll(BG_B, -(s16)tor.hpx);
    VDP_setVerticalScroll(BG_B, (s16)tor.vpx);
    return n;
}

/* ---------- cursor sprite ----------
 * The cursor is a 24x24 hardware sprite (3x3 tiles) in palette line 3, like the
 * Agon's VDP sprite: moving or blinking it repaints no field. Its tiles are the
 * cursor tile's pixels mapped to the 16 UI colours, reloaded when the colour changes. */
static u16 cursor_loaded = 0xFFFF;

static const u8 UI_RGB[16][3] = {   /* 2-bit channels of the UI line (png2md.py UI_LINE, 5 = normal level) */
    {0, 0, 0}, {2, 0, 0}, {0, 2, 0}, {2, 2, 0}, {0, 0, 2}, {0, 0, 0}, {0, 2, 2}, {2, 2, 2},
    {1, 1, 1}, {3, 0, 0}, {0, 3, 0}, {3, 3, 0}, {0, 0, 3}, {3, 0, 3}, {0, 3, 3}, {3, 3, 3}};

static u8 ui_nibble(u8 colour6)
{
    u8 r = (colour6 >> 4) & 3, g = (colour6 >> 2) & 3, b = colour6 & 3, n, best = 0;
    u16 bestd = 0xFFFF;
    for (n = 1; n < 16; n++) {
        s16 dr, dg, db;
        u16 d;
        if (n == 5)
            continue;                           /* opaque black: not a drawing colour */
        dr = r - UI_RGB[n][0];
        dg = g - UI_RGB[n][1];
        db = b - UI_RGB[n][2];
        d = dr * dr + dg * dg + db * db;
        if (d < bestd) {
            bestd = d;
            best = n;
        }
    }
    return best;
}

static void load_cursor(u16 tile)
{
    const u8 *src = tile_pixels + tile_info[tile].off;
    u32 data[9][8];
    u8 bx, by, r, c;
    for (by = 0; by < 3; by++)
        for (bx = 0; bx < 3; bx++)
            for (r = 0; r < 8; r++) {
                u32 w = 0;
                for (c = 0; c < 8; c++) {
                    u8 p = src[(by * 8 + r) * FIELD_PX + bx * 8 + c];
                    w = (w << 4) | (p ? (p == 1 ? 5 : ui_nibble(p - 1)) : 0);
                }
                data[bx * 3 + by][r] = w;       /* sprite tiles run down each column */
            }
    VDP_loadTileData(&data[0][0], CURSOR_TILE, 9, CPU);
    cursor_loaded = tile;
}

void render_cursor(s16 vx, s16 vy, u16 tile, bool visible)
{
    if (visible && vx >= 0 && vy >= 0 && vx < VIEW_W && vy < VIEW_H) {
        if (tile != cursor_loaded)
            load_cursor(tile);
        VDP_setSpriteFull(0, vx * FIELD_PX, vy * FIELD_PX, SPRITE_SIZE(3, 3),
                          TILE_ATTR_FULL(3, FALSE, FALSE, FALSE, CURSOR_TILE), 0);
    } else {
        VDP_setSpriteFull(0, -64, -64, SPRITE_SIZE(3, 3), TILE_ATTR_FULL(3, FALSE, FALSE, FALSE, CURSOR_TILE), 0);
    }
    VDP_updateSprites(1, CPU);
}

/* ---------- overview (whole map) ----------
 * One colour (nibble of palette line 3) per world field, 4x4 pixels each (3x3 coloured, one black gap),
 * drawn on plane B from tile row 2; the tiles are borrowed from the field slots, so the map has to be
 * painted again afterwards (render_overview_end). */
void render_overview(const u8 *colour, u8 w, u8 h)
{
    u8 tx, ty, r, hx = (w + 1) / 2, hy = (h + 1) / 2;
    VDP_clearPlane(BG_B, TRUE);             /* the map window goes */
    VDP_setHorizontalScroll(BG_B, 0);
    VDP_setVerticalScroll(BG_B, 0);
    for (ty = 0; ty < hy; ty++)
        for (tx = 0; tx < hx; tx++) {
            u32 tile[8];
            u16 idx = VRAM_BASE + ty * hx + tx;
            for (r = 0; r < 8; r++) {
                u8 cy = ty * 2 + r / 4, cx = tx * 2;
                u32 word = 0;
                if ((r & 3) != 3) {                 /* the fourth pixel row is the gap */
                    u8 c0 = cy < h && cx < w ? colour[cy * w + cx] : 0;
                    u8 c1 = cy < h && cx + 1 < w ? colour[cy * w + cx + 1] : 0;
                    word = ((u32)c0 << 28) | ((u32)c0 << 24) | ((u32)c0 << 20) |
                           ((u32)c1 << 12) | ((u32)c1 << 8) | ((u32)c1 << 4);
                }
                tile[r] = word;
            }
            VDP_loadTileData(tile, idx, 1, CPU);
            VDP_setTileMapXY(BG_B, TILE_ATTR_FULL(3, FALSE, FALSE, FALSE, idx), tx, 2 + ty);
        }
}

/* The map window is painted afresh the next time. */
void render_overview_end(void)
{
    last_ox = -1;
}
