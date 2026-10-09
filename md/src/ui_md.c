#include "ui_md.h"

#include "colors.h"
#include "gen/tiles_md.h"
#include "render_md.h"

/*
 * UI on plane A over the map on plane B. Text cells are tilemap entries that
 * point at shared glyph tiles (one VRAM tile per character and colour, made on
 * first use); the nibble 5 of palette line 3 is an opaque black, so a text cell
 * hides the map under it. Opening a menu over the map therefore costs only
 * tilemap writes and the map underneath stays intact.
 */
#define BLACK_SOLID 5
#define GLYPH_COLOURS 16
#define GLYPH_BASE (TILE_USER_INDEX + RING * RING * 9)
#define BAR_BASE (GLYPH_BASE + GLYPH_MAX)
#define BAR_ROW0 3
#define BAR_CELLS 9
#define BAR_BUFF 6                      /* reserved above the bar for a buff */
#define MSG1_ROW 18
#define MSG1_ROWS 4
#define MSG2_ROW 22
#define MSG2_ROWS 5
#define SCREEN_COLS 40
#define SCREEN_ROWS 28

static const u8 BAR_FILL[6] = {C_BRIGHT_GREEN, C_BRIGHT_YELLOW, C_BRIGHT_RED,
                               C_WHITE, C_BRIGHT_BLUE, C_BRIGHT_MAGENTA};
static const u8 BAR_EDGE[6] = {C_GREEN, C_YELLOW, C_RED, C_GREY, C_BLUE, C_CYAN};

static const u32 *font;                 /* 96 glyph tiles, ASCII 32.. (4 bpp) */
static u16 glyph_slot[96 * GLYPH_COLOURS];     /* tile index per char/colour, 0 = not made yet */
static u16 next_glyph;
static u16 cell_attr[SCREEN_ROWS][SCREEN_COLS];    /* what each tilemap cell holds */
static u16 bar_state[6];                /* hash of what each bar shows, to skip identical redraws */

static void glyph_pixels(char ch, u8 colour, u32 *out)
{
    u8 r;
    const u32 *src = font + (u16)(ch - 32) * 8;
    u32 bg = 0x11111111UL * BLACK_SOLID, fg = 0x11111111UL * colour;
    for (r = 0; r < 8; r++) {
        u32 w = src[r];
        u32 m = ((w | (w >> 1) | (w >> 2) | (w >> 3)) & 0x11111111UL) * 15;      /* glyph pixels */
        out[r] = (fg & m) | (bg & ~m);
    }
}

static u16 glyph_tile(char ch, u8 colour)
{
    u16 key;
    if (ch < 32 || (u8)ch > 127)
        ch = '?';
    if (ch == ' ')
        colour = C_BLACK;
    key = (u16)(ch - 32) * GLYPH_COLOURS + (colour & 15);
    if (!glyph_slot[key]) {
        u32 tile[8];
        u16 idx;
        if (next_glyph >= GLYPH_MAX)
            next_glyph = 0;             /* full: reuse from the start (rare, glitchy) */
        idx = GLYPH_BASE + next_glyph++;
        glyph_pixels(ch, colour, tile);
        VDP_loadTileData(tile, idx, 1, CPU);
        glyph_slot[key] = idx + 1;      /* +1: 0 means "not made" */
    }
    return glyph_slot[key] - 1;
}

static void set_cell(u8 col, u8 row, u16 attr)
{
    if (cell_attr[row][col] != attr) {
        VDP_setTileMapXY(BG_A, attr, col, row);
        cell_attr[row][col] = attr;
    }
}

void ui_init(void)
{
    u16 i;
    if (font_default.compression == COMPRESSION_NONE) {
        font = font_default.tiles;
    } else {
        u32 *buf = MEM_alloc(font_default.numTile * 32);
        unpack(font_default.compression, (u8 *)font_default.tiles, (u8 *)buf);
        font = buf;
    }
    memset(glyph_slot, 0, sizeof glyph_slot);
    next_glyph = 0;
    for (i = 0; i < 6; i++)
        bar_state[i] = 0xFFFF;
    ui_overlay_close();
    VDP_setTextPalette(3);
}

void ui_reset(void)
{
    u8 i;
    memset(cell_attr, 0, sizeof cell_attr);
    for (i = 0; i < 6; i++)
        bar_state[i] = 0xFFFF;
    VDP_clearPlane(BG_A, TRUE);
}

void ui_text(u8 col, u8 row, u8 colour, const char *s, u8 width)
{
    u8 n = 0;
    while (*s || n < width) {
        char ch = *s ? *s++ : ' ';
        if (col + n >= SCREEN_COLS)
            break;
        set_cell(col + n, row, TILE_ATTR_FULL(3, FALSE, FALSE, FALSE, glyph_tile(ch, colour)));
        n++;
    }
}

void ui_overlay_clear(void)
{
    u8 r;
    for (r = 0; r < MAP_TILES; r++)
        ui_text(0, r, C_BLACK, "", MAP_TILES);
}

void ui_overlay_close(void)
{
    u8 r, c;
    for (r = 0; r < MAP_TILES; r++)
        for (c = 0; c < MAP_TILES; c++)
            set_cell(c, r, 0);                  /* transparent: the map shows */
}

void ui_cells_transparent(u8 col, u8 row, u8 w, u8 h)
{
    u8 r, c;
    for (r = row; r < row + h; r++)
        for (c = col; c < col + w; c++)
            set_cell(c, r, 0);
}

void ui_clear_panel_rows(u8 row0, u8 row1)
{
    u8 r;
    for (r = 0; r < 6; r++)
        bar_state[r] = 0xFFFF;                  /* the bar cells are overwritten */
    for (r = row0; r <= row1; r++)
        ui_text(PANEL_COL, r, C_BLACK, "", PANEL_COLS);
}

void ui_bar(u8 index, u8 value, u8 max, u8 cap, u8 bonus)
{
    u8 cell;
    s16 span = BAR_CELLS * 8 - BAR_BUFF - 2;
    s16 outline = 0, top = 0, fill = 0, extra = 0;
    u8 col = PANEL_COL + 1 + index * 2;
    u8 efg = BAR_EDGE[index], ffg = BAR_FILL[index];
    u16 state = (u16)((value << 8) | max) ^ ((u16)cap << 5) ^ ((u16)bonus << 11);
    if (bar_state[index] == state)
        return;
    bar_state[index] = state;
    if (max && cap) {
        if (value > max)
            value = max;
        outline = max >= cap ? span : span * max / cap;
        if (outline < 3)
            outline = 3;
        top = BAR_CELLS * 8 - outline;                      /* first row of the outline */
        fill = (outline - 2) * value / max;
        if (bonus) {
            extra = span * bonus / cap;
            if (extra < 2)
                extra = 2;
            if (extra > BAR_BUFF)
                extra = BAR_BUFF;
        }
    }
    for (cell = 0; cell < BAR_CELLS; cell++) {
        u32 tile[8];
        u16 idx = BAR_BASE + index * BAR_CELLS + cell;
        u8 y;
        for (y = 0; y < 8; y++) {
            s16 Y = cell * 8 + y;
            u32 row = 0x11111111UL * BLACK_SOLID;
            if (outline && Y >= top) {
                if (Y == top || Y == BAR_CELLS * 8 - 1)
                    row = 0x11111111UL * efg;
                else
                    row = ((u32)efg << 28) | efg |
                          (Y >= BAR_CELLS * 8 - 1 - fill ? 0x01111110UL * ffg : 0x01111110UL * BLACK_SOLID);
            } else if (extra && Y >= top - extra && Y < top) {
                row = ((u32)BLACK_SOLID << 28) | BLACK_SOLID | 0x01111110UL * ffg;
            }
            tile[y] = row;
        }
        VDP_loadTileData(tile, idx, 1, CPU);
        set_cell(col, BAR_ROW0 + cell, TILE_ATTR_FULL(3, FALSE, FALSE, FALSE, idx));
    }
}

static void put_wrapped(u8 row0, u8 rows, u8 colour, const char *text)
{
    char line[PANEL_COLS + 1];
    u8 r = 0;
    while (r < rows) {
        u8 n = 0, brk = 0;
        while (*text == ' ')
            text++;
        while (text[n] && n < PANEL_COLS) {
            if (text[n] == ' ')
                brk = n;
            n++;
        }
        if (text[n] && brk)                 /* cut at the last blank */
            n = brk;
        memcpy(line, text, n);
        line[n] = 0;
        ui_text(PANEL_COL, row0 + r, colour, line, PANEL_COLS);
        text += n;
        r++;
    }
}

void ui_message(u8 line, u8 colour, const char *text)
{
    if (line == 0)
        ui_text(0, STATUS_ROW, colour, text, 27);
    else if (line == 1)
        put_wrapped(MSG1_ROW, MSG1_ROWS, colour, text);
    else
        put_wrapped(MSG2_ROW, MSG2_ROWS, colour, text);
}
