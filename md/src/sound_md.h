/*
 * Sound effects (samples through the XGM2 driver's PCM channels, a few tone-only effects on
 * the PSG) and the PSG song player. Both are advanced from the V-Blank callback.
 */
#ifndef LOC_SOUND_MD_H
#define LOC_SOUND_MD_H

#include "loc.h"

typedef enum {
    SND_STEP, SND_HIT, SND_MISS, SND_SPELL, SND_PICKUP, SND_PORTAL, SND_DEATH, SND_SWING,
    SND_BOW, SND_THROW, SND_DOOR, SND_CHEST, SND_SMASH, SND_ROUND, SND_WIN, SND_LOSE, SND_CRIT,
    SND_BOLT, SND_LIGHTNING, SND_SUMMON, SND_TELEPORT, SND_CURSE, SND_DRINK, SND_EAT, SND_FLY,
    SND_MENU, SND_CONFIRM, SND_BACK, SND_ERROR, SND_COUNT
} SoundFx;

typedef enum { SONG_TITLE, SONG_WIN, SONG_LOSE } Song;

void sound_init(void);
void sound_play(u8 fx);
/* Songs on the PSG; the title song loops, the jingles play once. */
void music_start(Song s);
void music_stop(void);
bool music_playing(void);
/* Call once per frame (V-Blank). */
void sound_tick(void);

extern bool sound_on, music_on;

#endif
