/*
 * Combat and spell effects (M5c, GDD 11.4): drain the core event ring
 * and play every event as a tile overlay or as VDP sprites (projectiles,
 * spell effects, rising damage numbers - ADR 0012) plus a sound. Short timed
 * frames; keys that arrive during the show are swallowed so nothing
 * ghosts into the next input (K5).
 */
#ifndef LOC_FX_H
#define LOC_FX_H

#include "../core/events.h"
#include "../core/sight.h"
#include "../core/world.h"

/* Scripted runs (dump/bench) disable the show: the waits would swallow
 * the scripted keys. Enabled by default. */
void fx_set_enabled(bool on);
/* Effect sprites 1..8 next to the cursor sprite; after render_init. */
void fx_init(void);
/* Slide a unit tile as a sprite from one view field to the next (the
 * caller hides the unit in the view meanwhile, view_hide_unit). */
void fx_glide(uint16_t tile, int16_t vx0, int16_t vy0, int16_t vx1, int16_t vy1);
extern bool fx_glide_on;                 /* setup switch */
/* Next key release (vkey) swallowed by a show, 0 when none: the main
 * loop feeds them to the chord logic so no arrow stays "held". */
uint8_t fx_take_release(void);
/* Unseen phases (phase screen): play the queued events as sounds only,
 * one after the other. */
void fx_drain_sounds(void);
/* Every drained event is shown to this hook first (D83: the log). */
void fx_set_event_hook(void (*fn)(const GameEvent *e));
/* Wait (keys are dropped, releases kept). */
void fx_pause(uint8_t cs);
/* Drain the core event ring and play every event as a tile overlay plus
 * a sound. */
/* Returns true when a fight (swing, hit, miss or death) was shown. */
bool fx_drain_play(World *w, const Sight *s);

#endif
