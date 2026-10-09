/*
 * Mega Drive renderer: the 9x9 window of 24x24 fields on plane A.
 *
 * A field is the bottom-to-top layer list of the core's view (view.h). The
 * layers are composed as 8-bit chunky pixels, cut into nine 8x8 blocks, and
 * each block gets the palette line that holds its colours (tools/png2md.py);
 * the nine tiles of a field live at a fixed place in VRAM.
 */
#ifndef LOC_RENDER_MD_H
#define LOC_RENDER_MD_H

#include "loc.h"
#include "view.h"
#include "torus.h"

#define GLYPH_MAX 440                       /* text glyph tiles (ui_md.c) */
#define MAP_TILES (VIEW_W * 3)          /* window width/height in hardware tiles */

/* Palette lines, clear plane A; call once after SYS init. */
void render_init(void);
/* The world the view shows (its size and wrap decide how the window origin moves). Call after
 * loading a map, before the next render_fields(). */
void render_set_world(const World *w);
/* Compose and upload all dirty view fields, then view_clean(). Returns how many. */
u8 render_fields(void);

/* The cursor sprite over field (vx, vy) of the window in the colour of cursor tile
 * T_CURSOR_*; invisible when not visible or outside the window. */
void render_cursor(s16 vx, s16 vy, u16 tile, bool visible);

/* The whole map as a picture: colour[y * w + x] is a nibble of palette line 3 (0 = black). Replaces the
 * map window on plane B; after viewing it call render_overview_end() and repaint (frame()). */
void render_overview(const u8 *colour, u8 w, u8 h);
void render_overview_end(void);

#endif
