#include "chord.h"

#define VERTICAL (ARROW_UP | ARROW_DOWN)
#define HORIZONTAL (ARROW_LEFT | ARROW_RIGHT)

static bool is_diagonal(uint8_t m)
{
    uint8_t v = m & VERTICAL, h = m & HORIZONTAL;
    return (v == ARROW_UP || v == ARROW_DOWN) && (h == ARROW_LEFT || h == ARROW_RIGHT);
}

static bool is_valid(uint8_t m)
{
    uint8_t v = m & VERTICAL, h = m & HORIZONTAL;
    return m != 0 && v != VERTICAL && h != HORIZONTAL;
}

static uint8_t emit(Chord *c, uint8_t mask, uint16_t now)
{
    c->last = mask;
    c->t_emit = now;
    return mask;
}

void chord_init(Chord *c, uint8_t window_cs, uint8_t delay_cs, uint8_t repeat_cs)
{
    c->held = c->pending = c->last = c->repeating = 0;
    c->t0 = c->t_emit = 0;
    c->window_cs = window_cs;
    c->delay_cs = delay_cs;
    c->repeat_cs = repeat_cs;
}

uint8_t chord_key(Chord *c, uint8_t arrow, bool down, uint16_t now)
{
    uint8_t first;

    c->repeating = 0;
    if (!down) {
        c->held = (uint8_t)(c->held & ~arrow);
        if (c->pending == arrow) {          /* quick tap: fire on release */
            c->pending = 0;
            return emit(c, arrow, now);
        }
        c->t_emit = now;                    /* remaining keys: restart delay */
        return 0;
    }

    if (c->held & arrow) {                  /* keyboard auto-repeat */
        if (c->pending)
            return 0;
        if ((uint16_t)(now - c->t_emit) < c->repeat_cs)
            return 0;
        return emit(c, is_diagonal(c->held) ? c->held : arrow, now);
    }

    c->held = (uint8_t)(c->held | arrow);
    if (c->pending) {
        if (is_diagonal(c->pending | arrow) &&
            (uint16_t)(now - c->t0) <= c->window_cs) {
            uint8_t m = (uint8_t)(c->pending | arrow);
            c->pending = 0;
            return emit(c, m, now);
        }
        first = c->pending;                 /* no chord: fire the first one */
        c->pending = arrow;
        c->t0 = now;
        return emit(c, first, now);
    }
    c->pending = arrow;
    c->t0 = now;
    return 0;
}

uint8_t chord_keys(Chord *c, uint8_t mask, bool down, uint16_t now)
{
    uint8_t bit, m = 0, r;
    for (bit = 1; bit <= ARROW_RIGHT; bit = (uint8_t)(bit << 1))
        if (mask & bit) {
            r = chord_key(c, bit, down, now);
            if (r)
                m = r;
        }
    return m;
}

uint8_t chord_poll(Chord *c, uint16_t now)
{
    if (c->pending) {
        if ((uint16_t)(now - c->t0) > c->window_cs) {
            uint8_t m = c->pending;
            c->pending = 0;
            return emit(c, m, now);
        }
        return 0;
    }
    if (is_valid(c->held) &&
        (uint16_t)(now - c->t_emit) >= (c->repeating ? c->repeat_cs : c->delay_cs)) {
        c->repeating = 1;
        return emit(c, c->held, now);
    }
    return 0;
}

void chord_done(Chord *c, uint16_t now)
{
    if (!c->repeating && c->held)
        c->t_emit = now;
}

bool chord_to_step(uint8_t mask, int8_t *dx, int8_t *dy)
{
    uint8_t v = mask & VERTICAL, h = mask & HORIZONTAL;
    *dx = 0;
    *dy = 0;
    if (v == VERTICAL || h == HORIZONTAL || mask == 0)
        return false;
    if (v == ARROW_UP) *dy = -1;
    if (v == ARROW_DOWN) *dy = 1;
    if (h == ARROW_LEFT) *dx = -1;
    if (h == ARROW_RIGHT) *dx = 1;
    return true;
}
