/*
 * Mega Drive frontend entry (SGDK).
 *
 * Default: the game (game_md.c). -DLOC_MD_VIEWER (EXTRA_FLAGS): the map viewer.
 * Milestone 2: map viewer. The platform-free core loads one of the built-in
 * maps; the d-pad moves a cursor over it (the window scrolls with it), START
 * switches to the next map. Build with SELFTEST=1 (md/build.sh) for the
 * core self-test ROM instead.
 */
#include "loc.h"

#include "gen/maps.h"
#include "render_md.h"
#include "view.h"

#ifdef LOC_MD_SELFTEST
#include "selftest.h"

/* read by tools/mdtest.py via the BlastEm debugger (symbol.txt) */
volatile u16 g_selftest_fails = 0xFFFF;
volatile u16 g_selftest_done;

static u16 line_no;

static void log_line(const char *line)
{
    KLog((char *)line);
    if (line_no < 26)
        VDP_drawText(line, 0, line_no++);
}

int main(bool hard)
{
    u16 fails;
    (void)hard;
    selftest_set_verbose(FALSE);
    fails = core_selftest(log_line);
    g_selftest_fails = fails;
    g_selftest_done = 1;
    VDP_drawText(fails ? "=== TEST FAIL ===" : "=== TEST PASS ===", 0, 27);
    while (TRUE)
        SYS_doVBlankProcess();
    return 0;
}

#elif !defined(LOC_MD_VIEWER)

#include "game_md.h"

int main(bool hard)
{
    (void)hard;
    game_run();
    return 0;
}

#else

typedef struct {
    const char *name;
    const u8 *data;
    const u16 *len;
} MapEntry;

static const MapEntry maps[] = {
    {"testland", MAPBIN_TESTLAND, &MAPBIN_TESTLAND_LEN},
    {"many colours", MAPBIN_MANY_COLOURED_LAND, &MAPBIN_MANY_COLOURED_LAND_LEN},
    {"wizard house", MAPBIN_WIZARD_HOUSE, &MAPBIN_WIZARD_HOUSE_LEN},
    {"ragarils", MAPBIN_RAGARILS_DOMAIN, &MAPBIN_RAGARILS_DOMAIN_LEN},
    {"slayers", MAPBIN_SLAYERS_DUNGEON, &MAPBIN_SLAYERS_DUNGEON_LEN},
    {"tutorial", MAPBIN_TUTORIAL, &MAPBIN_TUTORIAL_LEN},
};
#define MAP_COUNT (sizeof maps / sizeof maps[0])

#define INFO_COL 28                 /* text column right of the 27-tile window */
#define REPEAT_FIRST 14
#define REPEAT_NEXT 5

static World world;
static s16 cx, cy;
static u8 cur_map;

static void load_map(u8 k)
{
    cur_map = k;
    world_load_bin(&world, maps[k].data, *maps[k].len);
    view_set_sight(NULL);               /* everything visible */
    view_invalidate();
    cx = world.units[0].x;
    cy = world.units[0].y;
    view_follow(&world, cx, cy);
    VDP_clearTextArea(INFO_COL, 0, 40 - INFO_COL, 28);
    VDP_drawText(maps[k].name, INFO_COL, 0);
}

static void show_cursor(bool blink_on)
{
    view_set_cursor(cx, cy, blink_on ? T_CURSOR_GREEN : T_CURSOR_WHITE);
    view_follow(&world, cx, cy);
    view_update(&world);
    render_fields();
}

static void info(void)
{
    char buf[16];
    sprintf(buf, "%2d,%2d", cx, cy);
    VDP_drawText(buf, INFO_COL, 2);
}

static void move(s16 dx, s16 dy)
{
    s16 nx = cx + dx, ny = cy + dy;
    if (world.wrap) {
        nx = (nx + world.w) % world.w;
        ny = (ny + world.h) % world.h;
    } else if (nx < 0 || ny < 0 || nx >= world.w || ny >= world.h) {
        return;
    }
    cx = nx;
    cy = ny;
}

int main(bool hard)
{
    u16 prev = 0, frame = 0;
    u8 held = 0;
    (void)hard;
    JOY_init();
    render_init();
    load_map(0);
    show_cursor(TRUE);
    info();
    while (TRUE) {
        u16 pad = JOY_readJoypad(JOY_1);
        u16 pressed = pad & ~prev;
        s16 dx = 0, dy = 0;
        bool moved = FALSE;
        if (pad & (BUTTON_UP | BUTTON_DOWN | BUTTON_LEFT | BUTTON_RIGHT)) {
            if (pressed || held >= (pressed ? 0 : REPEAT_NEXT)) {
                /* held counts frames since the last step; first repeat is longer */
            }
        }
        if (pressed & (BUTTON_UP | BUTTON_DOWN | BUTTON_LEFT | BUTTON_RIGHT))
            held = 0;
        else if (pad & (BUTTON_UP | BUTTON_DOWN | BUTTON_LEFT | BUTTON_RIGHT))
            held++;
        if ((pressed & (BUTTON_UP | BUTTON_DOWN | BUTTON_LEFT | BUTTON_RIGHT)) ||
            (held >= REPEAT_FIRST && ((held - REPEAT_FIRST) % REPEAT_NEXT) == 0 &&
             (pad & (BUTTON_UP | BUTTON_DOWN | BUTTON_LEFT | BUTTON_RIGHT)))) {
            if (pad & BUTTON_UP)
                dy = -1;
            else if (pad & BUTTON_DOWN)
                dy = 1;
            if (pad & BUTTON_LEFT)
                dx = -1;
            else if (pad & BUTTON_RIGHT)
                dx = 1;
            move(dx, dy);
            moved = TRUE;
        }
        if (pressed & BUTTON_START) {
            load_map((cur_map + 1) % MAP_COUNT);
            moved = TRUE;
        }
        frame++;
        if (moved || (frame & 31) == 0) {
            show_cursor((frame & 32) == 0 || moved);
            if (moved)
                info();
        }
        prev = pad;
        SYS_doVBlankProcess();
    }
    return 0;
}

#endif
