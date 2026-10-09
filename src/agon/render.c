#include "render.h"

#include <agon/mos.h>
#include <agon/vdp.h>
#include <stdio.h>
#include <string.h>

#include "../core/effect.h"
#include "../core/colors.h"
#include "../core/gen/data.h"
#include "../core/items.h"
#include "../core/names.h"
#include "../core/ride.h"
#include "../core/sight.h"
#include "../core/spells.h"
#include "../core/view.h"

#define SCREEN_MODE 8
#define FORMAT_RGBA2222 1
#define TILE_BUFFER_BASE 0x2000
/* MAP_PX comes from render.h */
#define PANEL_X MAP_PX
#define TEXT_COL_PANEL 27           /* 216 / 8 */
#define TEXT_ROW_MSG 27             /* 216 / 8 */
#define TEXT_COLS 40

#define CURSOR_SPRITE 0
#define HEAD_FONT 0x5000            /* VDP buffer of the heading font */
#define SYSTEM_FONT 0xFFFF

static bool head_font;              /* fonts/head.fnt loaded */

static uint8_t pixels[TILE_PX * TILE_PX];

/* A mount that carries a rider is drawn smaller (M4k) so that the rider
 * stays clearly visible: the mount tiles get a 60 % copy (14 px) built at
 * load time from the full tile - no extra artwork. Copies live in the
 * buffers after the regular tiles, slot = mount * 5 + owner. */
#define MOUNT_PX 18
#define MOUNT_KINDS 4
static const uint8_t MOUNT_KIND[MOUNT_KINDS] = {CR_UNICORN, CR_PEGASUS,
                                                CR_GRYPHON, CR_ELEPHANT};
static uint8_t small_px[MOUNT_PX * MOUNT_PX];

static int mount_slot(uint16_t tile)
{
    uint8_t m;
    for (m = 0; m < MOUNT_KINDS; m++) {
        uint16_t base = CREATURE_TILE[MOUNT_KIND[m]];
        if (tile >= base && tile - base <= OWN_NEUTRAL)
            return m * (OWN_NEUTRAL + 1) + (tile - base);
    }
    return -1;
}

/* 24x24 RGBA2222 -> 14x14: every target pixel looks at its source box;
 * black (the outline) wins so the outline stays closed, otherwise the
 * first opaque pixel counts if at least half of the box is opaque. */
static void shrink_mount(const uint8_t *src, uint8_t *dst)
{
    uint8_t x, y;
    for (y = 0; y < MOUNT_PX; y++)
        for (x = 0; x < MOUNT_PX; x++) {
            uint8_t sx0 = (uint8_t)(x * TILE_PX / MOUNT_PX);
            uint8_t sx1 = (uint8_t)(((x + 1) * TILE_PX + MOUNT_PX - 1) / MOUNT_PX);
            uint8_t sy0 = (uint8_t)(y * TILE_PX / MOUNT_PX);
            uint8_t sy1 = (uint8_t)(((y + 1) * TILE_PX + MOUNT_PX - 1) / MOUNT_PX);
            uint8_t sx, sy, total = 0, opaque = 0, first = 0, black = 0;
            for (sy = sy0; sy < sy1 && sy < TILE_PX; sy++)
                for (sx = sx0; sx < sx1 && sx < TILE_PX; sx++) {
                    uint8_t p = src[sy * TILE_PX + sx];
                    total++;
                    if (p >> 6) {                /* alpha bits */
                        opaque++;
                        if (!first)
                            first = p;
                        if (!(p & 0x3F))
                            black = p;
                    }
                }
            dst[y * MOUNT_PX + x] = (uint8_t)(opaque * 2 >= total
                                                  ? (black ? black : first) : 0);
        }
}

static bool load_tiles(void)
{
    uint8_t fh, head[7], sizes[2 * TILE_COUNT];
    uint16_t count, i;
    uint24_t len;

    fh = mos_fopen("tiles.bin", FA_READ);
    if (!fh) {
        printf("tiles.bin not found\r\n");
        return false;
    }
    if (mos_fread(fh, (char *)head, 7) != 7 || memcmp(head, "LOCT", 4) != 0 || head[4] != 1) {
        printf("tiles.bin: bad header\r\n");
        mos_fclose(fh);
        return false;
    }
    count = (uint16_t)(head[5] | (head[6] << 8));
    if (count != TILE_COUNT) {
        printf("tiles.bin: %u tiles, expected %u\r\n", count, TILE_COUNT);
        mos_fclose(fh);
        return false;
    }
    mos_fread(fh, (char *)sizes, 2u * count);
    for (i = 0; i < count; i++) {
        uint8_t w = sizes[2 * i], h = sizes[2 * i + 1];
        len = (uint24_t)w * h;
        if (len > sizeof pixels || mos_fread(fh, (char *)pixels, len) != len) {
            printf("tiles.bin: entry %u broken\r\n", i);
            mos_fclose(fh);
            return false;
        }
        vdp_adv_clear_buffer(TILE_BUFFER_BASE + i);
        vdp_adv_write_block_data(TILE_BUFFER_BASE + i, (int)len, (char *)pixels);
        vdp_adv_select_bitmap(TILE_BUFFER_BASE + i);
        vdp_adv_bitmap_from_buffer(w, h, FORMAT_RGBA2222);
        if (w == TILE_PX && h == TILE_PX && mount_slot(i) >= 0) {
            uint16_t small_id = (uint16_t)(TILE_BUFFER_BASE + TILE_COUNT +
                                           mount_slot(i));
            shrink_mount(pixels, small_px);
            vdp_adv_clear_buffer(small_id);
            vdp_adv_write_block_data(small_id, MOUNT_PX * MOUNT_PX, (char *)small_px);
            vdp_adv_select_bitmap(small_id);
            vdp_adv_bitmap_from_buffer(MOUNT_PX, MOUNT_PX, FORMAT_RGBA2222);
        }
    }
    mos_fclose(fh);
    return true;
}

/* Batched bitmap output (ADR 0006, lever 1; audit B4/B5).
 *
 * A full redraw is ~260 bitmaps at 12 bytes each: 5 for
 * `VDU 23,27,&20,bufferId;` and 7 for `VDU 23,27,3,x;y;`. Sent through the
 * libagon helpers, every one of those bytes pays call overhead. MOS
 * RST 18h takes a length in BC and ships a whole block in one call
 * (mos_puts with size != 0; the delimiter only applies when size is 0),
 * so render_fields() collects a frame and sends it in few calls.
 *
 * Two rules keep this honest:
 *  - Only the tile path writes into the buffer, and only while a batch is
 *    open. Everything else (text, rectangles, sprites) still goes out
 *    directly, so nothing can be reordered behind a half-filled buffer.
 *  - `sel` remembers the bitmap the VDP has selected, so repeated tiles
 *    skip their select. It is only valid inside one batch and is reset
 *    when a batch opens. */
#define VDU_BUF 512
static uint8_t vdu_buf[VDU_BUF];
static uint16_t vdu_len;
static bool vdu_batch;
static uint16_t vdu_sel;             /* bitmap selected on the VDP, or NO_SEL */
#define NO_SEL 0xFFFF

static void vdu_flush(void)
{
    if (vdu_len) {
        mos_puts((const char *)vdu_buf, vdu_len, 0);
        vdu_len = 0;
    }
}

static void batch_begin(void)
{
    vdu_len = 0;
    vdu_sel = NO_SEL;
    vdu_batch = true;
}

static void batch_end(void)
{
    vdu_flush();
    vdu_batch = false;
}

/* Select (when needed) and draw one bitmap by its VDP buffer id. */
static void emit_bitmap(uint16_t buf, int x, int y)
{
    uint8_t *p;
    if (!vdu_batch) {
        vdp_adv_select_bitmap(buf);
        vdp_draw_bitmap(x, y);
        return;
    }
    if (vdu_len + 12u > VDU_BUF)
        vdu_flush();
    if (buf != vdu_sel) {
        p = &vdu_buf[vdu_len];
        p[0] = 23; p[1] = 27; p[2] = 0x20;
        p[3] = (uint8_t)buf;
        p[4] = (uint8_t)(buf >> 8);
        vdu_len = (uint16_t)(vdu_len + 5);
        vdu_sel = buf;
    }
    p = &vdu_buf[vdu_len];
    p[0] = 23; p[1] = 27; p[2] = 3;
    p[3] = (uint8_t)(uint16_t)x;
    p[4] = (uint8_t)((uint16_t)x >> 8);
    p[5] = (uint8_t)(uint16_t)y;
    p[6] = (uint8_t)((uint16_t)y >> 8);
    vdu_len = (uint16_t)(vdu_len + 7);
}

static void draw_tile(uint16_t id, int x, int y)
{
    emit_bitmap((uint16_t)(TILE_BUFFER_BASE + id), x, y);
}

uint16_t render_tile_buffer(uint16_t id)
{
    return (uint16_t)(TILE_BUFFER_BASE + id);
}

/* Public single-tile draw for non-map screens (lexicon portraits, M5). */
void render_draw_tile(uint16_t id, int x, int y)
{
    if (id < TILE_COUNT)
        draw_tile(id, x, y);
}

/* The small copy of a mount tile, centred and standing on the field's
 * baseline (x, y = top-left of the full tile). */
static void draw_small_mount(uint16_t id, int x, int y)
{
    int slot = mount_slot(id);
    if (slot < 0) {
        draw_tile(id, x, y);
        return;
    }
    emit_bitmap((uint16_t)(TILE_BUFFER_BASE + TILE_COUNT + slot),
                x + (TILE_PX - MOUNT_PX) / 2, y + (TILE_PX - MOUNT_PX));
}

/* The 8x16 heading font (tools/build_font.py): 256 glyphs x 16 bytes,
 * streamed into a VDP buffer and registered as a font (ADR 0012, S3). */
static bool load_head_font(void)
{
    uint8_t fh, chunk[256];
    uint16_t sent = 0;
    fh = mos_fopen("fonts/head.fnt", FA_READ);
    if (!fh)
        return false;
    vdp_adv_clear_buffer(HEAD_FONT);
    while (sent < 4096) {
        if (mos_fread(fh, (char *)chunk, sizeof chunk) != sizeof chunk)
            break;
        vdp_adv_write_block_data(HEAD_FONT, sizeof chunk, (char *)chunk);
        sent = (uint16_t)(sent + sizeof chunk);
    }
    mos_fclose(fh);
    if (sent != 4096)
        return false;
    vdp_adv_consolidate(HEAD_FONT);
    vdp_font_create(HEAD_FONT, 8, 16, 14, 0);
    return true;
}

bool render_init(void)
{
    vdp_mode(SCREEN_MODE);
    vdp_cursor_enable(false);
    vdp_clear_screen();
    vdp_set_pixel_coordinates();
    printf("Loading tiles...");
    if (!load_tiles())
        return false;
    vdp_clear_screen();
    view_invalidate();
    head_font = load_head_font();

    /* Cursor sprite: one frame per colour, in CursorColour order. */
    vdp_select_sprite(CURSOR_SPRITE);
    vdp_clear_sprite();
    vdp_adv_add_sprite_bitmap(TILE_BUFFER_BASE + T_CURSOR_GREEN);
    vdp_adv_add_sprite_bitmap(TILE_BUFFER_BASE + T_CURSOR_WHITE);
    vdp_adv_add_sprite_bitmap(TILE_BUFFER_BASE + T_CURSOR_YELLOW);
    vdp_adv_add_sprite_bitmap(TILE_BUFFER_BASE + T_CURSOR_RED);
    vdp_adv_add_sprite_bitmap(TILE_BUFFER_BASE + T_CURSOR_BLUE);
    vdp_activate_sprites(1);
    vdp_hide_sprite();
    vdp_refresh_sprites();
    return true;
}

void render_cursor(int16_t vx, int16_t vy, uint8_t colour, bool visible)
{
    vdp_select_sprite(CURSOR_SPRITE);
    if (visible && vx >= 0 && vy >= 0 && vx < VIEW_W && vy < VIEW_H) {
        vdp_nth_sprite_frame(colour);
        vdp_move_sprite_to(vx * TILE_PX, vy * TILE_PX);
        vdp_show_sprite();
    } else {
        vdp_hide_sprite();
    }
    vdp_refresh_sprites();
}

/* Rider drawn behind its (smaller) mount (M4k): lifted a little so that
 * torso and head show over the mount's back while the mount's body hides
 * legs and feet. The offset depends on the mount, read from the mount tile
 * that follows. */
#define RIDE_LIFT 3
static void ride_offset(uint16_t mount_tile, int *dx, int *dy)
{
    static const struct { uint8_t kind; int8_t dx, dy; } MOUNTS[] = {
        { CR_UNICORN, 0, RIDE_LIFT },
        { CR_PEGASUS, 0, RIDE_LIFT },
        { CR_GRYPHON, 0, RIDE_LIFT },
        { CR_ELEPHANT, 0, RIDE_LIFT },
    };
    uint8_t i;
    *dx = 0;
    *dy = RIDE_LIFT;
    for (i = 0; i < sizeof MOUNTS / sizeof MOUNTS[0]; i++) {
        uint16_t base = CREATURE_TILE[MOUNTS[i].kind];
        if (mount_tile >= base && mount_tile - base <= OWN_NEUTRAL) {
            *dx = MOUNTS[i].dx;
            *dy = MOUNTS[i].dy;
            return;
        }
    }
}

/* Enemy units get a thin red frame (B4). Collected during the pass and
 * drawn after the batch: rectangles are not bitmaps, so they must not be
 * mixed into the buffered stream, and drawn last they sit on top. */
/* Figures in deep water sit lower (C4): the bottom WADE_DROP pixels spill
 * into the field below, and that field is repainted over them - fields
 * draw top-down - so only the upper body shows. */
#define WADE_DROP 8
#define FOE_MAX 24
static uint8_t foe_x[FOE_MAX], foe_y[FOE_MAX];

uint8_t render_fields(void)
{
    uint8_t vx, vy, i, n = 0, foes = 0;
    batch_begin();
    for (vy = 0; vy < VIEW_H; vy++) {
        for (vx = 0; vx < VIEW_W; vx++) {
            const FieldLayers *f;
            if (!view_dirty(vx, vy))
                continue;
            f = view_field(vx, vy);
            for (i = 0; i < f->n; i++) {
                int x = vx * TILE_PX;
                int y = vy * TILE_PX;
                if (f->air & (1u << i))         /* flyer, slightly higher */
                    y -= 3;
                if ((view_bob(vx, vy) & 15) == i + 1)   /* idle creature rises */
                    y -= view_bob(vx, vy) >> 4;
                if (f->wade == i + 1 && vy + 1 < VIEW_H) {
                    y += WADE_DROP;             /* waist-deep: the legs spill  */
                    view_mark_dirty(vx, (uint8_t)(vy + 1));   /* into the field */
                }                               /* below, which covers them */
                if ((f->ride & (1u << i)) && i + 1 < f->n) {
                    int dx, dy, my = y;         /* rider behind the mount */
                    ride_offset(f->id[i + 1], &dx, &dy);
                    x += dx;
                    y -= dy;
                    if (y < 0)
                        y = 0;
                    if (x < 0)
                        x = 0;
                    draw_tile(f->id[i], x, y);
                    draw_small_mount(f->id[i + 1], vx * TILE_PX, my);
                    i++;                        /* the mount layer is done */
                    continue;
                }
                if (y < 0)
                    y = 0;
                draw_tile(f->id[i], x, y);
                if ((f->foe & (1u << i)) && foes < FOE_MAX) {
                    foe_x[foes] = vx;
                    foe_y[foes] = vy;
                    foes++;
                }
            }
            n++;
        }
    }
    batch_end();                 /* the frame goes out before anything else */
    for (i = 0; i < foes; i++) {
        int fx = foe_x[i] * TILE_PX, fy = foe_y[i] * TILE_PX;
        vdp_gcol(0, C_BRIGHT_RED);
        vdp_rectangle(fx, fy, fx + TILE_PX - 1, fy + TILE_PX - 1);
    }
    view_clean();
    if (n)
        vdp_refresh_sprites();   /* keep the cursor on top of new tiles */
    return n;
}

/* mos_putstring goes straight out through MOS RST 18h; printf("%s") would
 * drag the whole libc formatting machinery in for a plain string, and the
 * panel writes a dozen of these per frame (audit B7). */
static void text_at(uint8_t col, uint8_t row, uint8_t colour, const char *s)
{
    vdp_cursor_tab(col, row);
    vdp_set_text_colour(colour);
    vdp_set_text_bg_colour(C_BLACK);
    mos_putstring(s);
}

/* Fill colour and outline colour per bar (Amiga order, B2.4). The icons
 * gave way to letters (B6), two per bar in one row (D63). */
static const uint8_t BAR_FILL[6] = {C_BRIGHT_GREEN, C_BRIGHT_YELLOW, C_BRIGHT_RED,
                                    C_WHITE, C_BRIGHT_BLUE, C_BRIGHT_MAGENTA};
static const uint8_t BAR_EDGE[6] = {C_GREEN, C_YELLOW, C_RED, C_GREY, C_BLUE, C_MAGENTA};
static const char *const BAR_LABEL[6] = {"AP", "AU", "LE", "KA", "VE", "MA"};
/* Status icons (PM 11) in UF_* bit order. */
static const uint16_t STATUS_ICON[5] = {T_ICON_ST_UNDEAD, T_ICON_ST_FLY, T_ICON_ST_MOUNT,
                                       T_ICON_ST_WOUND, T_ICON_ST_INVISIBLE};
/* Under the bars: one row of two-letter labels (D63), then the keys that
 * act right now; "Am Boden:" starts at text row 23 and cannot move. */
#define BAR_TOP 58
#define BAR_BOTTOM 152
#define BAR_LABEL_ROW 20
#define KEYS_ROW 21
#define BAR_BUFF_SPACE 10         /* reserved above the bar for a buff */

static void black(int x0, int y0, int x1, int y1)
{
    vdp_gcol(0, C_BLACK);
    vdp_filled_rectangle(x0, y0, x1, y1);
}

/* The yardstick every bar is drawn against (B5): the best maximum among
 * the player's own figures. A figure with half the best maximum gets a
 * half-high bar, and spending only empties it - so one glance shows how
 * strong a creature is and how battered. */
static void panel_caps(const World *w, uint8_t owner, uint8_t *cap)
{
    uint8_t i;
    for (i = 0; i < 6; i++)
        cap[i] = 1;
    for (i = 0; i < w->unit_count; i++) {
        const Unit *u = &w->units[i];
        uint8_t apm;
        if (u->owner != owner)
            continue;
        apm = u->ap_max > u->ap_fly ? u->ap_max : u->ap_fly;
        if (apm > cap[0]) cap[0] = apm;
        if (u->sta_max > cap[1]) cap[1] = u->sta_max;
        if (u->con_max > cap[2]) cap[2] = u->con_max;
        if (u->com > cap[3]) cap[3] = u->com;
        if (u->def > cap[4]) cap[4] = u->def;
        if (u->mana_max > cap[5]) cap[5] = u->mana_max;
    }
}

/* What a timed effect or a carried object adds to a bar, shown as the extra
 * segment on top (D77: the weapon in hand on Combat, the best carried
 * shield/weapon on Defence). */
static uint8_t panel_bonus(const Unit *u, uint8_t i)
{
    uint8_t k, sum = 0;
    for (k = 0; k < UNIT_EFFECTS; k++) {
        uint8_t kind = u->effects[k].kind;
        if (u->effects[k].rounds == 0)
            continue;
        if ((i == 0 && kind == EFF_SPEED) ||
            (i == 3 && (kind == EFF_STRENGTH || kind == EFF_MAGIC_WEAPON)) ||
            (i == 4 && (kind == EFF_SHIELD || kind == EFF_PROTECT)))
            sum = (uint8_t)(sum + effect_power(u, kind));
    }
    if (i == 3)
        sum = (uint8_t)(sum + items_combat_bonus(u));
    else if (i == 4)
        sum = (uint8_t)(sum + items_defence_bonus(u));
    return sum;
}

static void bar(uint8_t i, uint8_t value, uint8_t max, uint8_t cap,
                uint8_t bonus)
{
    int x = PANEL_X + 8 + i * 16;
    int span = BAR_BOTTOM - BAR_TOP - BAR_BUFF_SPACE - 2;
    int outline, fill, top, extra;
    black(x, BAR_TOP, x + 7, BAR_BOTTOM);
    if (max == 0 || cap == 0)       /* e.g. mana of a non-wizard: no bar */
        return;
    if (value > max)
        value = max;
    outline = max >= cap ? span : span * max / cap;
    if (outline < 3)
        outline = 3;                /* a weakling still gets a visible bar */
    top = BAR_BOTTOM - outline;
    vdp_gcol(0, BAR_EDGE[i]);
    vdp_rectangle(x, top, x + 7, BAR_BOTTOM);
    fill = (outline - 2) * value / max;
    if (fill > 0) {
        vdp_gcol(0, BAR_FILL[i]);
        vdp_filled_rectangle(x + 1, BAR_BOTTOM - 1 - fill, x + 6, BAR_BOTTOM - 1);
    }
    if (bonus) {                    /* buff as an extra contingent on top */
        extra = span * bonus / cap;
        if (extra < 2)
            extra = 2;
        if (extra > BAR_BUFF_SPACE)
            extra = BAR_BUFF_SPACE;
        /* slow pulse so a buff catches the eye without flickering */
        vdp_gcol(0, (getsysvar_time() >> 5) & 1 ? BAR_FILL[i] : C_WHITE);
        vdp_filled_rectangle(x + 1, top - extra, x + 6, top - 1);
    }
}

/* The letters that replaced the icons (B6): two under each bar (D63). */
static void bar_label(uint8_t i)
{
    text_at((uint8_t)((PANEL_X + 8 + i * 16) / 8), BAR_LABEL_ROW, BAR_EDGE[i],
            BAR_LABEL[i]);
}

void render_panel_keys(const char *keys)
{
    char buf[16];
    snprintf(buf, sizeof buf, "%-13.13s", keys);
    text_at(TEXT_COL_PANEL, KEYS_ROW, C_BRIGHT_CYAN, buf);
}

/* Panel (GDD 11.1), 104 px = text columns 27..39:
 *   portrait 24x24 in a frame, right of it level + status icons,
 *   name, AP/mana figures, 6 bars with icons, "Am Boden" list. */
void render_panel(const World *w, uint8_t unit)
{
    char buf[16];
    const Unit *u = &w->units[unit];
    const char *ground[GROUND_MAX];
    uint8_t i, n;

    vdp_gcol(0, C_BRIGHT_BLUE);
    vdp_rectangle(PANEL_X + 4, 4, PANEL_X + 31, 31);
    black(PANEL_X + 5, 5, PANEL_X + 30, 30);
    {   /* a rider shows behind his mount, lifted a little (M4k) */
        uint8_t rk = ride_rider_kind(u);
        int dx = 0, dy = 0;
        uint16_t mount_tile = (uint16_t)(CREATURE_TILE[u->kind] + u->owner);
        if (rk < CR_COUNT) {
            ride_offset(mount_tile, &dx, &dy);
            draw_tile((uint16_t)(CREATURE_TILE[rk] + u->owner), PANEL_X + 6 + dx,
                      6 - dy);
            draw_small_mount(mount_tile, PANEL_X + 6, 6);
        } else {
            draw_tile(mount_tile, PANEL_X + 6, 6);
        }
    }
    text_at(32, 1, C_GREY, ride_actor_kind(u) == CR_WIZARD ? "Stufe 1" : "       ");
    for (i = 0; i < 5; i++) {
        int x = 256 + i * 9;
        black(x, 16, x + 7, 23);
        if (u->flags & (1u << i))
            draw_tile(STATUS_ICON[i], x, 16);
    }
    {   /* timed effects (M4b): shield, strength, speed */
        uint8_t k, slot = 5;
        for (k = 0; k < UNIT_EFFECTS; k++) {
            int x;
            if (u->effects[k].rounds == 0)
                continue;
            x = 256 + slot * 9;
            if (slot >= 8)
                break;
            if (u->effects[k].kind == EFF_SHIELD || u->effects[k].kind == EFF_PROTECT)
                draw_tile(T_ICON_SHIELD, x, 16);
            else if (u->effects[k].kind == EFF_STRENGTH)
                draw_tile(T_ICON_SWORD, x, 16);
            else if (u->effects[k].kind == EFF_SPEED)
                draw_tile(T_ICON_STAR, x, 16);
            else if (u->effects[k].kind == EFF_MAGIC_WEAPON)
                draw_tile(T_ICON_BOLT, x, 16);
            else
                draw_tile(T_ICON_ST_INVISIBLE, x, 16);
            slot++;
        }
    }

    snprintf(buf, sizeof buf, "%-13.13s", name_unit(u));
    text_at(TEXT_COL_PANEL, 5, C_BRIGHT_WHITE, buf);
    {   /* the object in use (GDD 8, M4j polish): weapons act only in hand */
        /* always 13 columns: a shorter line must cover the longer one */
        if (u->in_use != NO_ITEM && u->in_use < u->item_count)
            snprintf(buf, sizeof buf, "Hand: %-7.7s",
                     OBJECTS[u->items[u->in_use]].name);
        else
            snprintf(buf, sizeof buf, "Hand: %-7s", "-");
        text_at(TEXT_COL_PANEL, 4, C_BRIGHT_YELLOW, buf);
    }
    snprintf(buf, sizeof buf, "AP %2u  ", u->ap);
    text_at(TEXT_COL_PANEL, 6, C_BRIGHT_GREEN, buf);
    if (u->mana_max)
        snprintf(buf, sizeof buf, "Ma%3u", u->mana);
    else
        snprintf(buf, sizeof buf, "     ");
    text_at(34, 6, C_BRIGHT_MAGENTA, buf);

    {
        uint8_t cap[6];
        panel_caps(w, u->owner, cap);
        bar(0, u->ap, (u->flags & UF_FLYING) ? u->ap_fly : u->ap_max, cap[0],
            panel_bonus(u, 0));
        bar(1, u->sta, u->sta_max, cap[1], 0);
        bar(2, u->con, u->con_max, cap[2], 0);
        bar(3, u->com, u->com, cap[3], panel_bonus(u, 3));
        bar(4, u->def, u->def, cap[4], panel_bonus(u, 4));
        bar(5, u->mana, u->mana_max, cap[5], 0);
        for (i = 0; i < 6; i++)
            bar_label(i);
    }

    text_at(TEXT_COL_PANEL, 23, C_GREY, "Am Boden:");
    n = ground_names(w, u->x, u->y, ground);
    for (i = 0; i < GROUND_MAX; i++) {
        snprintf(buf, sizeof buf, "%-13.13s", i < n ? ground[i] : "");
        text_at(TEXT_COL_PANEL, (uint8_t)(24 + i), C_BRIGHT_WHITE, buf);
    }
}

/* Look mode (GDD 5.1): the examined field instead of a unit. With a
 * (visible) unit it degenerates to the normal unit panel. */
void render_panel_at(const World *w, const Sight *s, int16_t x, int16_t y)
{
    char buf[24], desc[24];
    const char *ground[GROUND_MAX];
    uint8_t i, n, u;
    int16_t wx = x, wy = y;

    if (!world_wrap(w, &wx, &wy))
        return;                          /* outside: keep the last panel */
    u = world_unit_at(w, wx, wy, UL_GROUND);
    if (u == NO_UNIT)
        u = world_unit_at(w, wx, wy, UL_AIR);
    if (u != NO_UNIT) {
        const Unit *un = &w->units[u];
        if (!s || un->owner == s->owner ||
            (sight_unit_visible(s, w, un) && !(un->flags & UF_INVISIBLE))) {
            render_panel(w, u);
            return;
        }
    }

    vdp_gcol(0, C_BRIGHT_BLUE);
    vdp_rectangle(PANEL_X + 4, 4, PANEL_X + 31, 31);
    black(PANEL_X + 5, 5, PANEL_X + 30, 30);
    text_at(32, 1, C_GREY, "       ");
    for (i = 0; i < 5; i++) {
        int px = 256 + i * 9;
        black(px, 16, px + 7, 23);
    }
    describe_field(w, s, wx, wy, desc, sizeof desc);
    snprintf(buf, sizeof buf, "%-13.13s", desc);
    text_at(TEXT_COL_PANEL, 5, C_BRIGHT_WHITE, buf);
    text_at(TEXT_COL_PANEL, 6, C_GREY, "             ");
    for (i = 0; i < 6; i++) {
        bar(i, 0, 0, 1, 0);
        bar_label(i);
    }
    render_panel_keys("");
    text_at(TEXT_COL_PANEL, 23, C_GREY, "Am Boden:");
    n = ground_names(w, wx, wy, ground);
    for (i = 0; i < GROUND_MAX; i++) {
        snprintf(buf, sizeof buf, "%-13.13s", i < n ? ground[i] : "");
        text_at(TEXT_COL_PANEL, (uint8_t)(24 + i), C_BRIGHT_WHITE, buf);
    }
}

/* The cast list: render_list_summons selects the group - letters a..
 * pick within it. Set from main.c before the call. */
uint8_t render_list_summons;

/* Names longer than the list column lose their " Potion" to "Pot." so
 * that "Invisibility Potion" stays readable instead of being cut. */
static void short_spell_name(char *out, size_t cap, const char *name)
{
    size_t n = strlen(name);
    static const char SUFFIX[] = " Potion";
    size_t sl = sizeof SUFFIX - 1;
    if (n + 1 > cap && n > sl && n - sl + 6 <= cap &&
        strcmp(name + n - sl, SUFFIX) == 0) {
        memcpy(out, name, n - sl);
        memcpy(out + n - sl, " Pot.", 6);         /* with the terminator */
    } else {
        snprintf(out, cap, "%s", name);
    }
}

void render_spell_list(const Spellbook *book)
{
    uint8_t row = 0, letter = 'a';
    uint16_t i;
    render_menu_clear();
    /* left of the stat panel: letter, 17 name, count, mana */
    text_at(0, 0, C_BRIGHT_YELLOW, render_list_summons
            ? "  Kreatur          Stf Mana"   /* summons: level (D34) */
            : "  Zauber           Anz Mana");
    /* values end in column 25: one blank column before the panel */
    for (i = 0; i < SPELL_COUNT && letter <= 'z'; i++) {
        char line[28], name[18];
        if (book->level[i] == 0)
            continue;
        if (render_list_summons != (SPELLS[i].category == SPC_SUMMON))
            continue;
        short_spell_name(name, sizeof name, SPELLS[i].name);
        snprintf(line, sizeof line, "%c %-17.17s %2u %3u", letter, name,
                 book->level[i], spell_cast_mana((uint8_t)i, book->level[i]));
        text_at(0, (uint8_t)(1 + row), C_BRIGHT_WHITE, line);
        row++;
        letter++;
    }
    /* 25 summons fill rows 1..25; the hint takes the last map row */
    text_at(0, 26, C_GREY, "Taste a-z wirkt, Esc zu.");
}

/* The c-menu first asks what to cast: spells or the summoned creatures. */
void render_cast_menu(uint8_t have_spells, uint8_t have_summons,
                      uint8_t n_spells, uint8_t n_summons)
{
    render_menu_clear();
    text_at(0, 0, C_BRIGHT_YELLOW, "  Was wirken?");
    if (have_spells) {
        text_at(2, 3, C_BRIGHT_WHITE, "Z  Zauber");
        {
            char line[28];
            snprintf(line, sizeof line, "   %u Spruenge verfuegbar", n_spells);
            text_at(2, 4, C_GREY, line);
        }
    }
    if (have_summons) {
        text_at(2, have_spells ? 7 : 3, C_BRIGHT_WHITE, "B  Beschwoeren");
        {
            char line[28];
            snprintf(line, sizeof line, "   %u Beschwoerungen", n_summons);
            text_at(2, (uint8_t)((have_spells ? 7 : 3) + 1), C_GREY, line);
        }
    }
    text_at(0, 22, C_GREY, "Z/B waehlen, Esc bricht ab.");
}

static void hide_cursor(void)
{
    vdp_select_sprite(CURSOR_SPRITE);
    vdp_hide_sprite();
    vdp_refresh_sprites();
}

/* Overlays cover the map window: the cursor sprite would float above
 * them, so it goes too (frame() shows it again). */
void render_menu_clear(void)
{
    hide_cursor();
    black(0, 0, MAP_PX - 1, MAP_PX - 1);
}

void render_menu_line(uint8_t col, uint8_t row, uint8_t colour,
                      const char *text)
{
    char buf[TEXT_COLS];
    uint8_t width, i;
    if (col >= TEXT_COLS - 1)
        return;
    width = (uint8_t)(TEXT_COLS - 1 - col);       /* never column 39 (V4) */
    for (i = 0; i < width && text[i]; i++)
        buf[i] = text[i];
    for (; i < width; i++)
        buf[i] = ' ';
    buf[width] = 0;
    text_at(col, row, colour, buf);
}

void render_clear_rows(uint8_t row0, uint8_t row1)
{
    black(0, row0 * 8, 319, row1 * 8 + 7);
}

void render_side_clear(void)
{
    black(PANEL_X, 0, 319, TEXT_ROW_MSG * 8 - 1);
}

void render_status_tag(const char *tag)
{
    char buf[8];
    snprintf(buf, sizeof buf, "%-6.6s", tag);
    text_at(33, 0, C_GREY, buf);
}

void render_menu_text(uint8_t col, uint8_t row, uint8_t colour,
                      const char *text)
{
    text_at(col, row, colour, text);
}

void render_screen_clear(void)
{
    hide_cursor();
    black(0, 0, 319, 239);
}

/* Title bitmap (M5d): streamed into its own VDP buffer far above the
 * tile bank (0x2000 + tiles + mounts). 320x240 RGBA2222 = 76 800 bytes,
 * written in chunks through the small staging buffer; repeated
 * write_block_data calls append to the buffer. The bitmap stays in VDP
 * RAM for the program run - the menu redraws over it. */
#define TITLE_BUFFER 0x4000
static bool title_loaded;            /* the bitmap is in the VDP */

bool render_title_backdrop(void)
{
    if (!title_loaded)
        return false;
    vdp_adv_select_bitmap(TITLE_BUFFER);
    vdp_draw_bitmap(0, 0);
    return true;
}

void render_box(int x0, int y0, int x1, int y1)
{
    black(x0, y0, x1, y1);
    vdp_gcol(0, C_BLUE);
    vdp_rectangle(x0, y0, x1, y1);
    vdp_gcol(0, C_BRIGHT_BLUE);
    vdp_rectangle(x0 + 2, y0 + 2, x1 - 2, y1 - 2);
}

/* Stream a LOCB picture (RGBA2222) into its own VDP buffer and draw it
 * at x/y. False when the file is missing, invalid or not w x h. */
static bool show_picture(const char *file, uint16_t buffer, uint16_t want_w,
                         uint16_t want_h, int x, int y)
{
    uint8_t fh, head[9];                 /* "LOCB" u8 version u16 w u16 h */
    uint16_t w, h;
    uint24_t total, sent = 0, n;
    fh = mos_fopen(file, FA_READ);
    if (!fh)
        return false;
    if (mos_fread(fh, (char *)head, 9) != 9 || memcmp(head, "LOCB", 4) != 0 ||
        head[4] != 1) {
        mos_fclose(fh);
        return false;
    }
    w = (uint16_t)(head[5] | (head[6] << 8));
    h = (uint16_t)(head[7] | (head[8] << 8));
    total = (uint24_t)w * h;
    if (w != want_w || h != want_h) {
        mos_fclose(fh);
        return false;
    }
    vdp_adv_clear_buffer(buffer);
    do {
        n = mos_fread(fh, (char *)pixels, sizeof pixels);
        if (n == 0)
            break;
        vdp_adv_write_block_data(buffer, (int)n, (char *)pixels);
        sent += n;
    } while (sent < total);
    mos_fclose(fh);
    if (sent != total)
        return false;
    /* every write appended a block; a bitmap needs one contiguous block */
    vdp_adv_consolidate(buffer);
    vdp_adv_select_bitmap(buffer);
    vdp_adv_bitmap_from_buffer(w, h, FORMAT_RGBA2222);
    vdp_draw_bitmap(x, y);
    return true;
}

bool render_show_title(void)
{
    title_loaded = show_picture("title.bin", TITLE_BUFFER, 320, 240, 0, 0);
    return title_loaded;
}

bool render_show_end_picture(bool win, int x, int y)
{
    return show_picture(win ? "win.bin" : "lose.bin",
                        (uint16_t)(TITLE_BUFFER + (win ? 1 : 2)), 96, 96, x, y);
}

/* Headings in the 8x16 display font, drawn at the graphics cursor: no
 * background box, a shadow one pixel down-right (red under yellow, else
 * blue). Without the font file: the system font at the nearest cell. */
void render_heading(int x, int y, uint8_t colour, const char *text)
{
    if (!head_font) {
        text_at((uint8_t)(x / 8), (uint8_t)((y + 4) / 8), colour, text);
        return;
    }
    vdp_font_select(HEAD_FONT, 0);
    vdp_write_at_graphics_cursor();
    vdp_gcol(0, colour == C_BRIGHT_YELLOW ? C_RED : C_BLUE);
    vdp_move_to(x + 1, y + 1);
    mos_putstring(text);
    vdp_gcol(0, colour);
    vdp_move_to(x, y);
    mos_putstring(text);
    vdp_write_at_text_cursor();
    vdp_font_select(SYSTEM_FONT, 0);
}

void render_heading_centred(int y, uint8_t colour, const char *text)
{
    int n = (int)strlen(text);
    render_heading((320 - n * 8) / 2, y, colour, text);
}

void render_dot(int x, int y, uint8_t colour)
{
    vdp_gcol(0, colour);
    vdp_filled_rectangle(x - 1, y - 1, x + 1, y + 1);
}

void render_frame(int x0, int y0, int x1, int y1, uint8_t colour)
{
    vdp_gcol(0, colour);
    vdp_rectangle(x0, y0, x1, y1);
    vdp_rectangle(x0 + 2, y0 + 2, x1 - 2, y1 - 2);
}

/* The message lines are remembered so that a full redraw after a
 * full-screen page can bring them back (render_messages_redraw). */
#define MSG_LINES 3
static char msg_text[MSG_LINES][TEXT_COLS];
static uint8_t msg_colour[MSG_LINES];

static void (*error_hook)(void);

void render_set_error_hook(void (*fn)(void))
{
    error_hook = fn;
}

static void (*message_hook)(uint8_t, uint8_t, const char *);

void render_set_message_hook(void (*fn)(uint8_t, uint8_t, const char *))
{
    message_hook = fn;
}

void render_message(uint8_t line, uint8_t colour, const char *text)
{
    char *buf;
    if (line >= MSG_LINES)
        return;
    if (message_hook && text[0])
        message_hook(line, colour, text);
    if (colour == C_BRIGHT_RED && error_hook && text[0])
        error_hook();                     /* refusals buzz (main.c) */
    buf = msg_text[line];
    /* Pad to 39 columns: writing column 39 of the last row would scroll. */
    snprintf(buf, TEXT_COLS, "%-39.39s", text);
    msg_colour[line] = colour;
    text_at(0, (uint8_t)(TEXT_ROW_MSG + line), colour, buf);
}

void render_messages_redraw(void)
{
    uint8_t i;
    for (i = 0; i < MSG_LINES; i++)
        if (msg_text[i][0])
            text_at(0, (uint8_t)(TEXT_ROW_MSG + i), msg_colour[i], msg_text[i]);
}

void render_shutdown(void)
{
    vdp_select_sprite(CURSOR_SPRITE);
    vdp_hide_sprite();
    vdp_activate_sprites(0);
    vdp_mode(0);
    vdp_cursor_enable(true);
}
