/*
 * Host stand-in for SGDK's <genesis.h>: just enough for md/src/game_md.c to compile and run on the
 * development machine against the real core. Hardware calls do nothing or are recorded by
 * md/test/stubs.c; the V-Blank is simulated by SYS_doVBlankProcess().
 */
#ifndef STUB_GENESIS_H
#define STUB_GENESIS_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int8_t s8;
typedef int16_t s16;
typedef int32_t s32;

#define TRUE 1
#define FALSE 0

#define BUTTON_UP 0x0001
#define BUTTON_DOWN 0x0002
#define BUTTON_LEFT 0x0004
#define BUTTON_RIGHT 0x0008
#define BUTTON_B 0x0010
#define BUTTON_C 0x0020
#define BUTTON_A 0x0040
#define BUTTON_START 0x0080
#define BUTTON_Z 0x0100
#define BUTTON_Y 0x0200
#define BUTTON_X 0x0400
#define JOY_1 0
#define IS_PAL_SYSTEM 0
#define TILE_USER_INDEX 16

typedef enum { BG_A, BG_B } VDPPlane;
typedef enum { CPU, DMA, DMA_QUEUE } TransferMethod;

extern volatile u32 vtimer;
void KLog(char *msg);
void JOY_init(void);
u16 JOY_readJoypad(u16 joy);
void SYS_setVBlankCallback(void (*cb)(void));
void SYS_doVBlankProcess(void);
void VDP_setEnable(bool on);
void VDP_setHorizontalScroll(VDPPlane p, s16 v);
void VDP_setVerticalScroll(VDPPlane p, s16 v);
void VDP_clearPlane(VDPPlane p, bool wait);
void VDP_loadTileData(const u32 *d, u16 index, u16 num, TransferMethod tm);
void VDP_setTileMapDataRectEx(VDPPlane p, const u16 *d, u16 base, u16 x, u16 y, u16 w, u16 h, u16 wm, TransferMethod tm);
void PAL_setColors(u16 index, const u16 *pal, u16 count, TransferMethod tm);

/* cartridge SRAM: 32 KB of bytes, backed by an array (stubs.c) */
void SRAM_enable(void);
void SRAM_enableRO(void);
void SRAM_disable(void);
u8 SRAM_readByte(u32 offset);
void SRAM_writeByte(u32 offset, u8 val);
u32 SRAM_readLong(u32 offset);
void SRAM_writeLong(u32 offset, u32 val);


/* what ui_md.c needs: a tilemap that records its cells, text palette, the font resource */
#define TILE_ATTR_FULL(pal, prio, flipV, flipH, index) \
    ((u16)(((flipH) << 11) | ((flipV) << 12) | ((pal) << 13) | ((prio) << 15) | (index)))
#define COMPRESSION_NONE 0
typedef struct {
    u16 compression, numTile;
    u32 *tiles;
} TileSet;
extern const TileSet font_default;
void VDP_setTileMapXY(VDPPlane p, u16 tile, u16 x, u16 y);
void VDP_setTextPalette(u16 pal);
void *MEM_alloc(u16 size);
void unpack(u16 compression, u8 *src, u8 *dst);
extern u16 test_tilemap[2][64][64];

#endif
