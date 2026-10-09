#include "genesis.h"

#include <setjmp.h>

#include "test.h"

volatile u32 vtimer;
static void (*vblank_cb)(void);
u16 test_pad;                          /* what JOY_readJoypad returns */
const u16 *test_script;                /* optional: pad value per V-Blank, 0xFFFF ends it */
u32 test_vblanks;
u32 test_fuzz_seed;                    /* != 0: the pad is random (a seeded generator) */
static u32 fz_state;
static u16 fz_pad;
static int fz_left;

static u32 fz_rnd(void)
{
    fz_state = fz_state * 1664525u + 1013904223u;
    return fz_state >> 8;
}

/* Random button states held for a few frames: mostly directions and A/B/C, now and then START. */
static u16 fuzz_next(void)
{
    if (--fz_left > 0)
        return fz_pad;
    fz_left = 1 + (int)(fz_rnd() % 7);
    switch (fz_rnd() % 16) {
    case 0: case 1: case 2: fz_pad = 0; break;
    case 3: fz_pad = BUTTON_UP; break;
    case 4: fz_pad = BUTTON_DOWN; break;
    case 5: fz_pad = BUTTON_LEFT; break;
    case 6: fz_pad = BUTTON_RIGHT; break;
    case 7: fz_pad = BUTTON_UP | BUTTON_RIGHT; break;
    case 8: fz_pad = BUTTON_DOWN | BUTTON_LEFT; break;
    case 9: fz_pad = BUTTON_A; break;
    case 10: fz_pad = BUTTON_B; break;
    case 11: fz_pad = BUTTON_C; break;
    case 12: fz_pad = BUTTON_START; fz_left = 1 + (int)(fz_rnd() % 60); break;   /* sometimes a long hold */
    case 13: fz_pad = BUTTON_X; break;
    case 14: fz_pad = BUTTON_Y; break;
    default: fz_pad = BUTTON_Z; break;
    }
    return fz_pad;
}

void test_fuzz_start(u32 seed)
{
    test_fuzz_seed = seed;
    fz_state = seed;
    fz_left = 0;
    fz_pad = 0;
}

void KLog(char *msg) { (void)msg; }
void JOY_init(void) {}
u16 JOY_readJoypad(u16 joy) { (void)joy; return test_pad; }
void SYS_setVBlankCallback(void (*cb)(void)) { vblank_cb = cb; }

/* One simulated V-Blank: the next scripted pad state, the frame counter, the callback. */
void SYS_doVBlankProcess(void)
{
    if (++test_vblanks > 200000UL)
        test_fail_now("simulation ran away (a menu or loop never ended)");
    if (test_fuzz_seed)
        test_pad = fuzz_next();
    else if (test_script) {
        if (*test_script == 0xFFFF)
            test_script = NULL;
        else
            test_pad = *test_script++;
    }
    vtimer++;
    if (vblank_cb)
        vblank_cb();
}

void VDP_setEnable(bool on) { (void)on; }
void VDP_setHorizontalScroll(VDPPlane p, s16 v) { (void)p; (void)v; }
void VDP_setVerticalScroll(VDPPlane p, s16 v) { (void)p; (void)v; }
void VDP_clearPlane(VDPPlane p, bool wait) { (void)p; (void)wait; }
void VDP_loadTileData(const u32 *d, u16 i, u16 n, TransferMethod tm) { (void)d; (void)i; (void)n; (void)tm; }
void VDP_setTileMapDataRectEx(VDPPlane p, const u16 *d, u16 b, u16 x, u16 y, u16 w, u16 h, u16 wm, TransferMethod tm)
{
    (void)p; (void)d; (void)b; (void)x; (void)y; (void)w; (void)h; (void)wm; (void)tm;
}
void PAL_setColors(u16 i, const u16 *p, u16 c, TransferMethod tm) { (void)i; (void)p; (void)c; (void)tm; }

/* ---- SRAM ---- */
u8 test_sram[32768];
jmp_buf *test_power_loss;               /* set by a test: SRAM writes cut off by a longjmp ... */
long test_power_after;                 /* ... after this many more bytes (0 = never) */
bool test_sram_ro;
void SRAM_enable(void) { test_sram_ro = FALSE; }
void SRAM_enableRO(void) { test_sram_ro = TRUE; }
void SRAM_disable(void) {}
u8 SRAM_readByte(u32 o) { return o < sizeof test_sram ? test_sram[o] : 0xFF; }
void SRAM_writeByte(u32 o, u8 v)
{
    if (test_sram_ro)
        test_fail_now("SRAM written while read-only");
    if (test_power_after > 0 && --test_power_after == 0 && test_power_loss)
        longjmp(*test_power_loss, 1);     /* the power goes before this byte is written */
    if (o < sizeof test_sram)
        test_sram[o] = v;
    else
        test_fail_now("SRAM write beyond 32 KB");
}
u32 SRAM_readLong(u32 o)
{
    return ((u32)SRAM_readByte(o) << 24) | ((u32)SRAM_readByte(o + 1) << 16) |
           ((u32)SRAM_readByte(o + 2) << 8) | SRAM_readByte(o + 3);
}
void SRAM_writeLong(u32 o, u32 v)
{
    SRAM_writeByte(o, v >> 24);
    SRAM_writeByte(o + 1, v >> 16);
    SRAM_writeByte(o + 2, v >> 8);
    SRAM_writeByte(o + 3, v);
}


u16 test_tilemap[2][64][64];
static u32 font_tiles[96 * 8];
const TileSet font_default = {0, 96, font_tiles};
void VDP_setTileMapXY(VDPPlane p, u16 tile, u16 x, u16 y)
{
    if (x >= 64 || y >= 64)
        test_fail_now("tilemap write outside the plane");
    test_tilemap[p][y][x] = tile;
}
void VDP_setTextPalette(u16 pal) { (void)pal; }
void *MEM_alloc(u16 size) { static u8 pool[4096]; (void)size; return pool; }
void unpack(u16 c, u8 *src, u8 *dst) { (void)c; (void)src; (void)dst; }
