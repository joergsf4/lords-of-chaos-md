/*
 * Text and bars on the right of the map (palette line 3 = the 16 logical
 * colours of colors.h). Every text cell owns a VRAM tile; glyphs are drawn
 * into it in the wanted colour, so any cell can have any colour. The panel
 * is the 13 columns right of the 27-column map window; row 27 also has the
 * 27 map columns.
 */
#ifndef LOC_UI_MD_H
#define LOC_UI_MD_H

#include "loc.h"

#define PANEL_COL 27
#define PANEL_COLS 13
#define STATUS_ROW 27

void ui_init(void);
/* Blank all of plane A (a new screen): forgets what the cells showed. */
void ui_reset(void);
/* Text at a tile position, padded with blanks to width (0 = as long as the text). */
void ui_text(u8 col, u8 row, u8 colour, const char *s, u8 width);
/* Menus over the map window (27x27 cells): clear it, draw with ui_text (columns 0..26,
 * rows 0..26); the close call makes the map repaint everything on the next render_fields(). */
void ui_overlay_clear(void);
void ui_overlay_close(void);
/* Blank rows row0..row1 of the panel. */
void ui_clear_panel_rows(u8 row0, u8 row1);
void ui_cells_transparent(u8 col, u8 row, u8 w, u8 h);   /* plane A cells that let plane B show */
/* One panel bar (index 0..5) in rows BAR_ROW0.. (see ui_md.c), value of max, drawn
 * against the yardstick cap; bonus = buff segment on top. max 0 = empty. */
void ui_bar(u8 index, u8 value, u8 max, u8 cap, u8 bonus);
/* Message line 0 (status row), 1 and 2 (wrapped in the panel's message rows). */
void ui_message(u8 line, u8 colour, const char *text);

#endif
