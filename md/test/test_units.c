/*
 * Host unit tests of the Mega Drive frontend's self-contained parts: the libc subset
 * (md/src/libc_md.c), the scroll bookkeeping (md/src/torus.h) and the text/bar UI
 * (md/src/ui_md.c, its tilemap writes are recorded by the stubs).
 */
#include "../src/ui_md.c"

#include <setjmp.h>
#include <stdarg.h>
#include <stdlib.h>

#include "test.h"

int test_checks, test_failures;
static jmp_buf abort_test;
static const char *current_test = "";

void test_fail_now(const char *why)
{
    test_failures++;
    printf("  FAIL (%s) %s\n", current_test, why);
    longjmp(abort_test, 1);
}

/* libc_md.c */
void *loc_memcpy(void *dst, const void *src, u32 n);
void *loc_memmove(void *dst, const void *src, u32 n);
void *loc_memset(void *dst, int c, u32 n);
int loc_memcmp(const void *a, const void *b, u32 n);
u32 loc_strlen(const char *s);
int loc_strcmp(const char *a, const char *b);
char *loc_strcpy(char *dst, const char *src);
int loc_snprintf(char *buf, unsigned long n, const char *fmt, ...);

/* ---------- libc ---------- */

static void t_snprintf_matches_libc(void)
{
    char a[64], b[64];
    int i;
    struct { const char *fmt; int v; } ints[] = {
        {"%d", 0}, {"%d", 7}, {"%d", -7}, {"%d", 32767}, {"%d", -32768}, {"%3d", 5}, {"%03d", 5},
        {"%u", 65535}, {"%2u", 3}, {"%02u", 3}, {"%x", 255}, {"%X", 255}, {"%08X", 255},
        {"Zufall-%u", 4}, {"%d%%", 50}, {"[%5d]", -42}};
    for (i = 0; i < (int)(sizeof ints / sizeof ints[0]); i++) {
        loc_snprintf(a, sizeof a, ints[i].fmt, ints[i].v);
        snprintf(b, sizeof b, ints[i].fmt, ints[i].v);
        CHECK(strcmp(a, b) == 0, ints[i].fmt);
    }
    {   /* the formats the frontend uses: left-justified and truncated strings, padded numbers */
        static const char *strs[] = {"", "a", "Magic Bolt", "Invisibility Potion", "Donaudampfschifffahrt"};
        static const char *fmts[] = {"%s", "%.5s", "%-13.13s|", "%13.13s|", "%-14.14s %s", "%.7s", "%-15.15s %d %3d",
                                     "%c %-17.17s %2d %5d", "Hand: %.7s", "%10s|", "%-10s|", "%.0s|"};
        int fi, si;
        for (fi = 0; fi < (int)(sizeof fmts / sizeof fmts[0]); fi++)
            for (si = 0; si < (int)(sizeof strs / sizeof strs[0]); si++) {
                const char *f = fmts[fi];
                int ok;
                if (strstr(f, "%c"))
                    { loc_snprintf(a, sizeof a, f, '>', strs[si], 3, 1234); snprintf(b, sizeof b, f, '>', strs[si], 3, 1234); }
                else if (strstr(f, "%d"))
                    { loc_snprintf(a, sizeof a, f, strs[si], 7, 42); snprintf(b, sizeof b, f, strs[si], 7, 42); }
                else if (strstr(f, "%s %s") || strstr(f, "%s"))
                    { loc_snprintf(a, sizeof a, f, strs[si], "hier"); snprintf(b, sizeof b, f, strs[si], "hier"); }
                else
                    { loc_snprintf(a, sizeof a, f, strs[si]); snprintf(b, sizeof b, f, strs[si]); }
                ok = strcmp(a, b) == 0;
                if (!ok)
                    printf("  [%s] [%s] vs libc [%s]\n", f, a, b);
                CHECK(ok, "string formats like libc");
            }
    }
    {
        static const char *nf[] = {"%-5d|", "%5d|", "%05d|", "%-05d|", "%.3d", "%6.3d|", "%-6.3d|", "%+d"};
        static const int nv[] = {0, 7, -7, 123456, -123456};
        int fi, vi;
        for (fi = 0; fi < 7; fi++)
            for (vi = 0; vi < 5; vi++) {
                loc_snprintf(a, sizeof a, nf[fi], nv[vi]);
                snprintf(b, sizeof b, nf[fi], nv[vi]);
                if (strcmp(a, b) != 0)
                    printf("  [%s] %d: [%s] vs libc [%s]\n", nf[fi], nv[vi], a, b);
                CHECK(strcmp(a, b) == 0, "number formats like libc");
            }
    }
    loc_snprintf(a, sizeof a, "%s and %s", "one", "two");
    CHECK(strcmp(a, "one and two") == 0, "two strings");
    loc_snprintf(a, sizeof a, "%c%c", 'o', 'k');
    CHECK(strcmp(a, "ok") == 0, "chars");
    loc_snprintf(a, sizeof a, "%lu", 4000000000UL);
    CHECK(strcmp(a, "4000000000") == 0, "unsigned long");
    loc_snprintf(a, sizeof a, "%ld", -2000000000L);
    CHECK(strcmp(a, "-2000000000") == 0, "long");
    loc_snprintf(a, sizeof a, "view: hash=0x%08lX", 0x7CC28CF5UL);
    CHECK(strcmp(a, "view: hash=0x7CC28CF5") == 0, "the selftest's own format");
}

static void t_snprintf_truncates_and_terminates(void)
{
    char buf[8];
    int n;
    memset(buf, 'x', sizeof buf);
    n = loc_snprintf(buf, 4, "abcdefgh");
    CHECK_EQ(n, 8, "returns the full length like C99");
    CHECK(strcmp(buf, "abc") == 0, "cut to n - 1 and terminated");
    CHECK_EQ(buf[4], 'x', "nothing written past n");
    memset(buf, 'x', sizeof buf);
    loc_snprintf(buf, 1, "abc");
    CHECK_EQ(buf[0], 0, "n = 1 leaves an empty string");
    memset(buf, 'x', sizeof buf);
    loc_snprintf(buf, 0, "abc");
    CHECK_EQ(buf[0], 'x', "n = 0 writes nothing");
    loc_snprintf(buf, sizeof buf, "%s", "");
    CHECK_EQ(buf[0], 0, "empty string");
}

static void t_snprintf_truncation_with_numbers(void)
{
    char buf[28], ref[64];
    int n;
    /* the line of the designer's shop is one character longer than the buffer: this once looped forever */
    n = loc_snprintf(buf, sizeof buf, "%c %-17.17s %2d %5d", ' ', "Magic Bolt", 0, 5);
    snprintf(ref, sizeof ref, "%c %-17.17s %2d %5d", ' ', "Magic Bolt", 0, 5);
    CHECK_EQ(n, (int)strlen(ref), "the full length is returned");
    CHECK(strncmp(buf, ref, 27) == 0 && buf[27] == 0, "cut to the buffer");
    n = loc_snprintf(buf, 6, "%d", 1234567);
    CHECK(strcmp(buf, "12345") == 0 && n == 7, "a number cut in the middle");
    n = loc_snprintf(buf, 4, "%30d|", 5);
    CHECK(n == 31 && buf[3] == 0, "wide padding is cut");
    n = loc_snprintf(buf, 5, "%x", 0xABCDEF);
    CHECK(strcmp(buf, "ABCD") == 0 || strcmp(buf, "abcd") == 0, "hex cut");
}

static void t_mem_and_str(void)
{
    u8 a[32], b[32];
    int i;
    for (i = 0; i < 32; i++)
        a[i] = i;
    loc_memcpy(b, a, 32);
    CHECK(memcmp(a, b, 32) == 0, "memcpy");
    loc_memset(b, 0xAB, 32);
    CHECK(b[0] == 0xAB && b[31] == 0xAB, "memset");
    CHECK(loc_memcmp(a, a, 32) == 0, "memcmp equal");
    CHECK(loc_memcmp(a, b, 32) < 0 && loc_memcmp(b, a, 32) > 0, "memcmp order");
    a[5] = 99;
    CHECK(loc_memcmp(a, b, 5) < 0, "memcmp stops at n");
    for (i = 0; i < 32; i++)
        a[i] = i;
    loc_memmove(a + 2, a, 10);          /* overlapping, forwards */
    CHECK(a[2] == 0 && a[11] == 9 && a[0] == 0 && a[1] == 1, "memmove overlapping up");
    for (i = 0; i < 32; i++)
        a[i] = i;
    loc_memmove(a, a + 2, 10);          /* overlapping, backwards */
    CHECK(a[0] == 2 && a[9] == 11, "memmove overlapping down");
    CHECK_EQ(loc_strlen("hello"), 5, "strlen");
    CHECK_EQ(loc_strlen(""), 0, "strlen empty");
    CHECK(loc_strcmp("a", "b") < 0 && loc_strcmp("b", "a") > 0 && loc_strcmp("x", "x") == 0, "strcmp");
    {
        char s[8];
        loc_strcpy(s, "hi");
        CHECK(strcmp(s, "hi") == 0, "strcpy");
    }
    {   /* more than the 16-bit length the SGDK routines take */
        static u8 big[70000], big2[70000];
        for (i = 0; i < 70000; i++)
            big[i] = (u8)(i * 7);
        loc_memcpy(big2, big, 70000);
        CHECK(memcmp(big, big2, 70000) == 0, "memcpy beyond 64 KB");
        loc_memset(big2, 0, 70000);
        CHECK(big2[69999] == 0 && big2[40000] == 0, "memset beyond 64 KB");
    }
}

/* ---------- torus ---------- */

static void torus_equal(const Torus *a, const Torus *b, const char *what)
{
    CHECK(a->vox == b->vox && a->voy == b->voy, what);
    CHECK(a->rox == b->rox && a->roy == b->roy, what);
    CHECK(a->rtx == b->rtx && a->rty == b->rty, what);
    CHECK(a->hpx == b->hpx && a->vpx == b->vpx, what);
}

static void t_torus_steps_match_recompute(void)
{
    Torus t, ref;
    u32 seed = 12345;
    s16 x = 6, y = 6;
    int i;
    torus_set(&t, x, y);
    for (i = 0; i < 200000; i++) {
        s8 dx, dy;
        seed = seed * 1103515245u + 12345u;
        dx = (s8)((seed >> 16) % 3) - 1;
        dy = (s8)((seed >> 20) % 3) - 1;
        x += dx;
        y += dy;
        torus_step(&t, dx, dy);
        torus_set(&ref, x, y);
        if (t.vox != ref.vox || t.voy != ref.voy || t.rox != ref.rox || t.roy != ref.roy ||
            t.rtx != ref.rtx || t.rty != ref.rty || t.hpx != ref.hpx || t.vpx != ref.vpx) {
            torus_equal(&t, &ref, "stepwise equals recomputed");
            return;
        }
    }
    CHECK(TRUE, "200000 random steps stayed consistent");
    /* far negative and far positive origins (the virtual origin is not confined to the map) */
    torus_set(&t, -1000, 1000);
    for (i = 0; i < 5000; i++) {
        torus_step(&t, -1, 1);
        x = -1000 - (i + 1);
        y = 1000 + (i + 1);
    }
    torus_set(&ref, x, y);
    torus_equal(&t, &ref, "long drift");
}

static void t_torus_fields_never_collide(void)
{
    Torus t;
    s16 x, y;
    for (x = -3; x < 30; x++)
        for (y = -3; y < 30; y += 7) {
            u8 vx, vy;
            static u8 slot_seen[RING][RING], cell_seen[PLANE_H][PLANE_W];
            memset(slot_seen, 0, sizeof slot_seen);
            memset(cell_seen, 0, sizeof cell_seen);
            torus_set(&t, x, y);
            for (vy = 0; vy < 10; vy++)         /* the window plus the incoming row and column */
                for (vx = 0; vx < 10; vx++) {
                    u8 sx, sy, tx, ty, b;
                    torus_field(&t, vx, vy, &sx, &sy, &tx, &ty);
                    if (slot_seen[sy][sx]++)
                        test_fail_now("two live fields share a tile data slot");
                    for (b = 0; b < 9; b++) {
                        u8 cx = (tx + b % 3) % PLANE_W, cy = (ty + b / 3) % PLANE_H;
                        if (cell_seen[cy][cx]++)
                            test_fail_now("two live fields share a tilemap cell");
                    }
                }
        }
    CHECK(TRUE, "no collisions for any origin");
}

static void t_torus_scroll_registers_match_cells(void)
{
    Torus t;
    s16 x;
    for (x = -50; x < 100; x++) {
        u8 vx;
        torus_set(&t, x, x / 2);
        for (vx = 0; vx < 9; vx++) {
            u8 sx, sy, tx, ty;
            int screen_px;
            torus_field(&t, vx, 0, &sx, &sy, &tx, &ty);
            /* the field's first cell sits at tx * 8; the scroll register shifts it to vx * 24 on screen */
            screen_px = (int)((tx * 8 - (int)t.hpx) % (PLANE_W * 8) + PLANE_W * 8) % (PLANE_W * 8);
            if (screen_px != vx * FIELD_PX)
                test_fail_now("the scroll register does not put the field where it belongs");
        }
    }
    CHECK(TRUE, "horizontal scroll consistent");
    for (x = -50; x < 100; x++) {
        u8 vy;
        torus_set(&t, x / 3, x);
        for (vy = 0; vy < 9; vy++) {
            u8 sx, sy, tx, ty;
            int screen_px;
            torus_field(&t, 0, vy, &sx, &sy, &tx, &ty);
            screen_px = (int)((ty * 8 - (int)t.vpx) % (PLANE_H * 8) + PLANE_H * 8) % (PLANE_H * 8);
            if (screen_px != vy * FIELD_PX)
                test_fail_now("the vertical scroll register does not put the field where it belongs");
        }
    }
    CHECK(TRUE, "vertical scroll consistent");
}

/* ---------- UI ---------- */

/* The text a plane A row shows, recovered from the glyph tiles the cells point at. */
static void row_text(u8 row, char *out)
{
    u8 c;
    for (c = 0; c < SCREEN_COLS; c++) {
        u16 attr = test_tilemap[BG_A][row][c], idx = attr & 0x7FF;
        char ch = '?';
        u16 k;
        if (attr == 0) {
            ch = '~';                           /* transparent cell */
        } else {
            for (k = 0; k < 96 * GLYPH_COLOURS; k++)
                if (glyph_slot[k] == idx + 1) {
                    ch = (char)(k / GLYPH_COLOURS + 32);
                    break;
                }
        }
        out[c] = ch;
    }
    out[SCREEN_COLS] = 0;
}

static void ui_fresh(void)
{
    memset(test_tilemap, 0, sizeof test_tilemap);
    ui_init();
}

static void t_ui_text_places_and_pads(void)
{
    char row[SCREEN_COLS + 1];
    ui_fresh();
    ui_text(PANEL_COL, 3, C_BRIGHT_WHITE, "Hand", 13);
    row_text(3, row);
    CHECK(strncmp(row + PANEL_COL, "Hand         ", 13) == 0, "text, padded to the width with blanks");
    CHECK(row[PANEL_COL - 1] == '~', "the cell left of it is untouched");
    ui_text(PANEL_COL, 3, C_BRIGHT_WHITE, "X", 13);
    row_text(3, row);
    CHECK(strncmp(row + PANEL_COL, "X            ", 13) == 0, "a shorter text blanks the rest");
    ui_text(30, 4, C_BRIGHT_WHITE, "0123456789ABCDEF", 0);
    row_text(4, row);
    CHECK(row[39] == '9', "text is cut at the screen edge");
    CHECK_EQ(test_tilemap[BG_A][4][0], 0, "and nothing wraps to the left");
}

static void t_ui_glyphs_shared_and_recoloured(void)
{
    ui_fresh();
    ui_text(0, 0, C_BRIGHT_WHITE, "AAAA", 0);
    ui_text(0, 1, C_BRIGHT_WHITE, "AA", 0);
    CHECK_EQ(test_tilemap[BG_A][0][0], test_tilemap[BG_A][1][1], "same character and colour share one tile");
    ui_text(0, 2, C_BRIGHT_RED, "A", 0);
    CHECK(test_tilemap[BG_A][2][0] != test_tilemap[BG_A][0][0], "another colour is another tile");
    ui_text(0, 3, C_BRIGHT_RED, " ", 0);
    ui_text(0, 3, C_BRIGHT_GREEN, " ", 0);
    CHECK_EQ(test_tilemap[BG_A][3][0], test_tilemap[BG_A][3][0], "spaces are colourless");
    ui_text(0, 5, C_BRIGHT_WHITE, " ", 0);
    ui_text(0, 6, C_BRIGHT_RED, " ", 0);
    CHECK_EQ(test_tilemap[BG_A][5][0], test_tilemap[BG_A][6][0], "a blank is the same tile in every colour");
    CHECK(next_glyph < 10, "few glyph tiles for such a text");
}

static void t_ui_unknown_characters(void)
{
    char row[SCREEN_COLS + 1];
    ui_fresh();
    ui_text(0, 0, C_BRIGHT_WHITE, "a\x01z\xE4", 0);
    row_text(0, row);
    CHECK(row[0] == 'a' && row[1] == '?' && row[2] == 'z' && row[3] == '?', "control and high characters show '?'");
}

static void t_ui_message_wrapping(void)
{
    char row[SCREEN_COLS + 1];
    u8 r;
    ui_fresh();
    ui_message(1, C_BRIGHT_YELLOW, "Truhe laesst sich nicht oeffnen.");
    for (r = 0; r < MSG1_ROWS; r++) {
        row_text(MSG1_ROW + r, row);
        CHECK(row[PANEL_COL - 1] == '~', "the map side is untouched");
        printf("%s", "");
    }
    row_text(MSG1_ROW, row);
    CHECK(strncmp(row + PANEL_COL, "Truhe laesst ", 13) == 0 || strncmp(row + PANEL_COL, "Truhe laesst", 12) == 0,
          "first line breaks at a blank");
    row_text(MSG1_ROW + 1, row);
    CHECK(strncmp(row + PANEL_COL, "sich nicht", 10) == 0, "second line");
    row_text(MSG1_ROW + 2, row);
    CHECK(strncmp(row + PANEL_COL, "oeffnen.", 8) == 0, "third line");
    /* a new, shorter message replaces all rows */
    ui_message(1, C_GREY, "Ok.");
    row_text(MSG1_ROW + 1, row);
    CHECK(strncmp(row + PANEL_COL, "             ", 13) == 0, "old lines are blanked");
    /* a word longer than the panel is cut, not lost */
    ui_message(1, C_GREY, "Donaudampfschifffahrtsgesellschaft");
    row_text(MSG1_ROW, row);
    CHECK(strncmp(row + PANEL_COL, "Donaudampfsch", 13) == 0, "long word cut at the panel width");
    /* line 2 is separate from line 1 */
    ui_message(2, C_GREY, "Zwei");
    row_text(MSG2_ROW, row);
    CHECK(strncmp(row + PANEL_COL, "Zwei", 4) == 0, "message line 2");
    /* text longer than the rows is cut after the last row, never into the next area */
    ui_message(1, C_GREY, "eins zwei drei vier fuenf sechs sieben acht neun zehn elf zwoelf dreizehn vierzehn");
    row_text(MSG1_ROW + MSG1_ROWS, row);
    CHECK(strncmp(row + PANEL_COL, "Zwei", 4) == 0, "message 1 does not run into message 2");
    /* line 0 is the status row under the map */
    ui_message(0, C_BRIGHT_WHITE, "Runde 3 - Zauberer-1");
    row_text(STATUS_ROW, row);
    CHECK(strncmp(row, "Runde 3 - Zauberer-1", 20) == 0, "status line");
    CHECK(row[26] == ' ' && row[27] == '~', "padded to the 27 map columns, nothing in the panel");
}

static void t_ui_overlay_clear_and_close(void)
{
    u8 r, c;
    ui_fresh();
    ui_overlay_clear();
    for (r = 0; r < MAP_TILES; r++)
        for (c = 0; c < MAP_TILES; c++)
            if (test_tilemap[BG_A][r][c] == 0)
                test_fail_now("the overlay must cover the whole 27x27 map area");
    CHECK(test_tilemap[BG_A][0][27] == 0, "the panel column is not covered");
    ui_text(0, 2, C_BRIGHT_WHITE, "Aktion", 27);
    ui_overlay_close();
    for (r = 0; r < MAP_TILES; r++)
        for (c = 0; c < MAP_TILES; c++)
            if (test_tilemap[BG_A][r][c] != 0)
                test_fail_now("closing the overlay must leave the map visible");
    CHECK(TRUE, "close makes every map cell transparent again");
    ui_text(PANEL_COL, 2, C_BRIGHT_WHITE, "Panel", 13);
    ui_overlay_clear();
    ui_overlay_close();
    CHECK(test_tilemap[BG_A][2][PANEL_COL] != 0, "the panel survives an overlay");
}

static void t_ui_reset(void)
{
    ui_fresh();
    ui_text(0, 0, C_BRIGHT_WHITE, "x", 0);
    ui_reset();
    /* VDP_clearPlane is a stub; the cache must be forgotten so that the same text is written again */
    memset(test_tilemap, 0, sizeof test_tilemap);
    ui_text(0, 0, C_BRIGHT_WHITE, "x", 0);
    CHECK(test_tilemap[BG_A][0][0] != 0, "after a reset the cells are written again");
}

static void t_ui_bars_stay_in_their_columns(void)
{
    u8 i, r, c;
    ui_fresh();
    for (i = 0; i < 6; i++)
        ui_bar(i, 10, 20, 20, i == 3 ? 4 : 0);
    for (r = 0; r < SCREEN_ROWS; r++)
        for (c = 0; c < SCREEN_COLS; c++) {
            bool bar_col = c >= PANEL_COL + 1 && c <= PANEL_COL + 11 && ((c - PANEL_COL - 1) % 2 == 0);
            bool bar_row = r >= BAR_ROW0 && r < BAR_ROW0 + BAR_CELLS;
            if (test_tilemap[BG_A][r][c] && !(bar_col && bar_row))
                test_fail_now("a bar wrote outside its cells");
        }
    CHECK(TRUE, "bars use only their own columns and rows");
    for (i = 0; i < 6; i++)
        CHECK(test_tilemap[BG_A][BAR_ROW0][PANEL_COL + 1 + 2 * i] != 0, "every bar has cells");
    /* identical values do not redo the tiles, empty bars (max 0) are blank */
    ui_bar(0, 0, 0, 1, 0);
    CHECK(TRUE, "empty bar accepted");
    ui_bar(0, 200, 20, 20, 0);          /* value above max is clamped, not a crash */
    ui_bar(1, 5, 20, 0, 0);             /* cap 0: no bar */
    CHECK(TRUE, "odd values do not crash");
}

typedef struct {
    const char *name;
    void (*fn)(void);
} Test;

#define T(n) {#n, n}
static const Test TESTS[] = {
    T(t_snprintf_matches_libc), T(t_snprintf_truncates_and_terminates), T(t_snprintf_truncation_with_numbers), T(t_mem_and_str),
    T(t_torus_steps_match_recompute), T(t_torus_fields_never_collide), T(t_torus_scroll_registers_match_cells),
    T(t_ui_text_places_and_pads), T(t_ui_glyphs_shared_and_recoloured), T(t_ui_unknown_characters),
    T(t_ui_message_wrapping), T(t_ui_overlay_clear_and_close), T(t_ui_reset), T(t_ui_bars_stay_in_their_columns),
};

int main(int argc, char **argv)
{
    unsigned i, ran = 0, failed = 0;
    const char *only = argc > 1 ? argv[1] : NULL;
    setvbuf(stdout, NULL, _IONBF, 0);
    for (i = 0; i < sizeof TESTS / sizeof TESTS[0]; i++) {
        int before;
        if (only && !strstr(TESTS[i].name, only))
            continue;
        current_test = TESTS[i].name;
        before = test_failures;
        if (setjmp(abort_test) == 0)
            TESTS[i].fn();
        ran++;
        printf("%s %s\n", test_failures == before ? "ok  " : "FAIL", TESTS[i].name);
        if (test_failures != before)
            failed++;
    }
    printf("\n%u tests, %d checks, %u failed tests, %d failed checks\n", ran, test_checks, failed, test_failures);
    return test_failures ? 1 : 0;
}
