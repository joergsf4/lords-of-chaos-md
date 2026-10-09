/*
 * Arrow-key chords for diagonal movement without a numpad (GDD 5.2,
 * Cherry G84-4100). Two neighbouring arrows pressed within a short window
 * give a diagonal; a single arrow fires on release or when the window ends
 * while it is still held. Held keys repeat from the clock (the Agon kbuf
 * delivers no auto-repeat events): first after delay_cs, then every
 * repeat_cs.
 *
 * Platform-free: the frontend maps its key codes to ARROW_* bits and feeds
 * key events plus a centisecond clock.
 */
#ifndef LOC_CHORD_H
#define LOC_CHORD_H

#include <stdbool.h>
#include <stdint.h>

enum { ARROW_UP = 1, ARROW_DOWN = 2, ARROW_LEFT = 4, ARROW_RIGHT = 8 };

typedef struct {
    uint8_t held;      /* arrows currently down */
    uint8_t pending;   /* arrow waiting for a possible chord partner */
    uint8_t last;      /* last emitted direction (for repeats) */
    uint8_t repeating; /* held keys already repeated once */
    uint16_t t0;       /* when pending was pressed */
    uint16_t t_emit;   /* when the last direction was emitted */
    uint8_t window_cs, delay_cs, repeat_cs;
} Chord;

void chord_init(Chord *c, uint8_t window_cs, uint8_t delay_cs, uint8_t repeat_cs);
/* Feed an arrow key event; returns a direction mask to move now, or 0. */
uint8_t chord_key(Chord *c, uint8_t arrow, bool down, uint16_t now_cs);
/* Feed several arrows at once (a numpad diagonal is two): every bit of mask
 * as one key event at the same instant. Returns the direction to move now. */
uint8_t chord_keys(Chord *c, uint8_t mask, bool down, uint16_t now_cs);
/* Call regularly: fires a held single arrow once the window has passed and
 * repeats held directions. */
uint8_t chord_poll(Chord *c, uint16_t now_cs);
/* Call after the step of an emitted direction has been played (it can take
 * a quarter of a second): the delay before the FIRST repeat then runs from
 * the end of the step, not from its start. Otherwise a slow step eats the
 * delay and a second step fires before a finger can let go (D79). */
void chord_done(Chord *c, uint16_t now_cs);
/* Direction mask -> step. False for 0 or contradictory masks. */
bool chord_to_step(uint8_t mask, int8_t *dx, int8_t *dy);

#endif
