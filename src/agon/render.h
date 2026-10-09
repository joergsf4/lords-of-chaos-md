/*
 * Agon renderer (GDD 11, ADR 0005): MODE 8, 24x24 tiles as VDP bitmaps,
 * 9x9 map window drawn field by field (only dirty fields), info panel and
 * message lines.
 */
#ifndef LOC_RENDER_H
#define LOC_RENDER_H

#define MAP_PX 216   /* 9 fields x 24 px (matches render.c) */

#include <stdbool.h>
#include <stdint.h>

#include "../core/sight.h"
#include "../core/spells.h"
#include "../core/world.h"

/* Screen mode, cursor off, upload tiles.bin to the VDP. False on error
 * (message already printed). */
bool render_init(void);
/* Draw all dirty view fields (layer by layer) and clean them. Returns the
 * number of fields drawn. */
uint8_t render_fields(void);
/* Info panel for one unit: portrait, name, 6 bars, ground info. */
void render_panel(const World *w, uint8_t unit);
/* Look mode: the examined field (unit panel when one is visible). */
/* The keys that act for the active unit right now, one row under the
 * bar labels (D63); "" clears the row. */
void render_panel_keys(const char *keys);
void render_panel_at(const World *w, const Sight *s, int16_t x, int16_t y);
/* Cursor frame as a VDP sprite (GDD 11.2): drawn over the map, so moving
 * or blinking it redraws no fields. Colours follow the Amiga code. */
/* Frame order = upload order in render_init(); blue = unit in the air
 * (GDD 11.2). */
typedef enum { CURSOR_GREEN, CURSOR_WHITE, CURSOR_YELLOW, CURSOR_RED,
               CURSOR_BLUE } CursorColour;
void render_cursor(int16_t vx, int16_t vy, uint8_t colour, bool visible);

/* Spell list overlay (GDD 5.1: lists over the map window). Only spells
 * with a level left; letters a.. pick, Esc closes. Redraw via
 * view_invalidate + render_fields afterwards. */
extern uint8_t render_list_summons;   /* group filter for the list */
void render_spell_list(const Spellbook *book);
/* First step of the c-menu: ask what to cast - spells (Z) or summons
 * (B). Missing groups are not offered. */
void render_cast_menu(uint8_t have_spells, uint8_t have_summons,
                      uint8_t n_spells, uint8_t n_summons);
/* One of the three message lines (0..2) below the map. The text is
 * remembered; render_messages_redraw() repaints all three after a
 * full-screen page. */
void render_message(uint8_t line, uint8_t colour, const char *text);
void render_messages_redraw(void);
/* Called for every red (refusal) message, e.g. to play an error sound. */
void render_set_error_hook(void (*fn)(void));
/* Called for every message line that is set (D83: main.c feeds the log). */
void render_set_message_hook(void (*fn)(uint8_t line, uint8_t colour,
                                        const char *text));
/* Menu helpers (M4f): black out the map window (and hide the cursor
 * sprite), write one text cell. Text drawn after render_menu_clear must
 * stay within columns 0..26, or it survives the next clear. */
void render_menu_clear(void);
/* Full-width text line for full-screen menus: cut at column 38 and
 * padded with blanks, so a shorter redraw overwrites the old text. */
void render_menu_line(uint8_t col, uint8_t row, uint8_t colour,
                      const char *text);
/* Black out whole text rows row0..row1 (all 40 columns). */
void render_clear_rows(uint8_t row0, uint8_t row1);
/* Black out the side panel (columns 27..39, rows 0..26) for a text page
 * beside an overlay, e.g. the log beside the big map (D74). */
void render_side_clear(void);
/* Small tag in the panel's top right corner, e.g. "<auto>" (D72). */
void render_status_tag(const char *tag);
void render_menu_text(uint8_t col, uint8_t row, uint8_t colour,
                      const char *text);
/* Full-screen helpers for the title/end/help screens (M5a): the whole
 * 320x240 screen black, and a 2 px frame in pixel coordinates. After a
 * full-screen screen call view_invalidate() and redraw the game. */
void render_screen_clear(void);
void render_frame(int x0, int y0, int x1, int y1, uint8_t colour);
/* A 3x3 dot (ornaments). */
void render_dot(int x, int y, uint8_t colour);
/* Headings in the 8x16 display font (fonts/head.fnt) at pixel x/y (top
 * left), transparent with a shadow; system font when the file is missing.
 * Blank the area first when redrawing over old text. */
void render_heading(int x, int y, uint8_t colour, const char *text);
void render_heading_centred(int y, uint8_t colour, const char *text);
/* One tile by id at a pixel position (lexicon portraits, M5). */
void render_draw_tile(uint16_t id, int x, int y);
/* VDP buffer of a tile (sprite frames, fx.c). */
uint16_t render_tile_buffer(uint16_t id);
/* Stream /loc/title.bin (RGBA2222, ADR 0011) into a VDP buffer and show
 * it as a 320x240 bitmap. False when the file is missing or invalid. */
bool render_show_title(void);
/* Draw the title bitmap again (menu backdrop); false when it was never
 * loaded. */
bool render_title_backdrop(void);
/* The 96x96 end screen picture (win.bin / lose.bin) at x/y; false when
 * missing. */
bool render_show_end_picture(bool win, int x, int y);
/* Black box with a double blue frame (pixel coordinates). */
void render_box(int x0, int y0, int x1, int y1);
void render_shutdown(void);

#endif
