/*
 * Key mapping by FabGL virtual key (kbuf vkey), measured in the keyboard
 * spike (issue #3, ADR 0007). Movement keys are mapped by vkey only: the
 * ascii field of key-UP events is stale (it repeats the last key down).
 */
#ifndef LOC_INPUT_H
#define LOC_INPUT_H

#include <stdbool.h>
#include <stdint.h>

#include <agon/keyboard.h>

#define VK_ESC 0x7D
#define VK_UP 0x96
#define VK_DOWN 0x98
#define VK_LEFT 0x9A
#define VK_RIGHT 0x9C
#define VK_HOME 0x86
#define VK_END 0x88
#define VK_PGUP 0x93
#define VK_PGDN 0x95
/* FabGL order: VK_SPACE = 1, VK_0..9 = 2..11, VK_KP_0..9 = 12..21, VK_a = 22
 * (= 0x16, measured) - so the numpad with NumLock on is 0x0C + digit. With
 * NumLock off it already sends the arrows and Home/End/PgUp/PgDn. */
#define VK_KP_1 0x0D
#define VK_KP_9 0x15
/* Measured with loc --keytest (M2c): Tab/Shift+Tab share the vkey and
 * differ in kmod, Space is a vkey, not an ASCII hit. */
#define VK_F1 0x9F   /* measured with loc --keytest in the emulator (F2 = 0xA0) */
#define VK_TAB 0x8E
#define VK_SPACE 0x01
#define KMOD_SHIFT 0x02
/* FabGL: VK_a..VK_z = 0x16..0x2F (layout applied, e.g. German y/z swap) */
#define VK_LOWER(c) (0x16 + ((c) - 'a'))

/* kbuf_poll_event with one correction: Esc typed over the USB console
 * arrives as plain ASCII 27 with no VKey, while the keyboard at the Agon
 * sends VK_ESC (AGON-QUIRKS H5). Normalising it in one place makes every
 * Esc test in the game work for both, so a session can be driven from the
 * PC - including quitting, which is what writes loc.log. */
bool input_poll(struct keyboard_event_t *e);

/* ARROW_* bits of an arrow key or a numpad key (1-4, 6-9: diagonals are two
 * bits), else 0. */
uint8_t input_arrow(uint8_t vkey);
/* Diagonal direction mask for Pos1/Bild-auf/Ende/Bild-ab (GDD 5.2), else 0. */
uint8_t input_diagonal(uint8_t vkey);

#endif
