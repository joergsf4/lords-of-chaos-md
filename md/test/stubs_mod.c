#include "genesis.h"

#include "render_md.h"
#include "sound_md.h"
#include "ui_md.h"
#include "gen/title_md.h"
#include "gen/maps.h"
#include "test.h"

/* ---- render / ui / sound recorders ---- */
void render_init(void) {}
unsigned char test_overview[46 * 46];
int test_overview_w, test_overview_h, test_overview_ended;
void render_overview(const u8 *colour, u8 w, u8 h)
{
    memcpy(test_overview, colour, (size_t)w * h);
    test_overview_w = w;
    test_overview_h = h;
}
void render_overview_end(void) { test_overview_ended++; }
u8 render_fields(void) { return 0; }
void render_set_world(const World *w) { (void)w; }
s16 test_cursor_x = -99, test_cursor_y = -99;
bool test_cursor_visible;
void render_cursor(s16 vx, s16 vy, u16 tile, bool visible)
{
    (void)tile;
    test_cursor_x = vx;
    test_cursor_y = vy;
    test_cursor_visible = visible;
}

void ui_init(void) {}
void ui_reset(void) {}
char test_msg[3][64];                  /* the last text of message line 0, 1, 2 */
u8 test_msg_colour[3];
char test_msg_log[64][64];             /* every non-empty message line 1/2, oldest first */
u16 test_msg_log_n;
char test_panel_name[32];
char test_text[28][40];
char test_text_ever[65536];            /* every string ui_text received since the last reset */
unsigned test_text_ever_n;                /* what ui_text put into column 0 of each row (menus, pages) */
void ui_message(u8 line, u8 colour, const char *text)
{
    if (line > 2)
        return;
    snprintf(test_msg[line], sizeof test_msg[line], "%s", text);
    test_msg_colour[line] = colour;
    if (line > 0 && text[0]) {
        snprintf(test_msg_log[test_msg_log_n % 64], 64, "%s", text);
        test_msg_log_n++;
    }
}
void ui_text(u8 col, u8 row, u8 colour, const char *s, u8 width)
{
    (void)colour; (void)width;
    if (col == 0 && row < 28)
        snprintf(test_text[row], sizeof test_text[row], "%s", s);
    if (test_text_ever_n + strlen(s) + 2 < sizeof test_text_ever) {
        memcpy(test_text_ever + test_text_ever_n, s, strlen(s));
        test_text_ever_n += (unsigned)strlen(s);
        test_text_ever[test_text_ever_n++] = '\n';
        test_text_ever[test_text_ever_n] = 0;
    }
    if (col == PANEL_COL && row == 0)
        snprintf(test_panel_name, sizeof test_panel_name, "%s", s);
}
void ui_bar(u8 i, u8 v, u8 m, u8 c, u8 b) { (void)i; (void)v; (void)m; (void)c; (void)b; }
void ui_overlay_clear(void) {}
void ui_overlay_close(void) {}
void ui_clear_panel_rows(u8 a, u8 b) { (void)a; (void)b; }
void ui_cells_transparent(u8 c, u8 r, u8 w, u8 h) { (void)c; (void)r; (void)w; (void)h; }

bool sound_on = TRUE, music_on = TRUE;
u8 test_sounds[256];                   /* how often each effect was played */
u8 test_last_sound = 0xFF;
void sound_init(void) {}
void sound_play(u8 fx) { test_last_sound = fx; if (test_sounds[fx] < 255) test_sounds[fx]++; }
void music_start(Song s) { (void)s; }
void music_stop(void) {}
bool music_playing(void) { return FALSE; }
void sound_tick(void) {}

/* ---- generated resources the game refers to ---- */
#define MCL_VARIANTS 16
/* every variant is the base map; the lengths are filled in by main() (a const variable is no constant) */
const unsigned char *mcl_variant[MCL_VARIANTS];
unsigned short mcl_variant_len[MCL_VARIANTS];
void test_set_variants(const unsigned char *map, unsigned short len)
{
    int i;
    for (i = 0; i < MCL_VARIANTS; i++) {
        mcl_variant[i] = map;
        mcl_variant_len[i] = len;
    }
}
const unsigned char title_tiles[4] = {0}, title_map[4] = {0}, title_pal[4] = {0};
const unsigned char pic_win_tiles[4] = {0}, pic_win_map[4] = {0}, pic_win_pal[4] = {0};
const unsigned char pic_lose_tiles[4] = {0}, pic_lose_map[4] = {0}, pic_lose_pal[4] = {0};
