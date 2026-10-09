#include "sound_md.h"

#include "resources.h"

bool sound_on = TRUE, music_on = TRUE;

/* ---------- effects ---------- */

typedef struct {
    const u8 *pcm;                      /* NULL: tone steps only */
    u16 len;
} Sample;

#define SMP(n) {sfx_##n, sizeof sfx_##n}
static const Sample SAMPLES[16] = {
    SMP(00), SMP(01), SMP(02), SMP(03), SMP(04), SMP(05), SMP(06), SMP(07),
    SMP(08), SMP(09), SMP(10), SMP(11), SMP(12), SMP(13), SMP(14), SMP(15)};

/* sfx_gen.h ids: STEP 0 WHOOSH 1 CLANG 2 THUD 3 GROAN 4 THUNDER 5 ZAP 6 CREAK 7 LID 8 SPARKLE 9
 * SUMMON 10 BLIP 11 BUBBLE 12 CRASH 13 PLUCK 14 DRUM 15 */
#define NONE 0xFF
typedef struct {
    u8 sample;                          /* index into SAMPLES, or NONE */
    u8 steps;                           /* tone steps after (or instead of) the sample */
    u16 hz[3];
    u8 frames[3];
} Fx;

static const Fx FX[SND_COUNT] = {
    [SND_STEP] = {0, 0},
    [SND_HIT] = {3, 0},
    [SND_MISS] = {1, 0},
    [SND_SPELL] = {9, 0},
    [SND_PICKUP] = {11, 0},
    [SND_PORTAL] = {10, 1, {784}, {13}},
    [SND_DEATH] = {4, 0},
    [SND_SWING] = {1, 0},
    [SND_BOW] = {1, 0},
    [SND_THROW] = {1, 0},
    [SND_DOOR] = {7, 0},
    [SND_CHEST] = {8, 0},
    [SND_SMASH] = {13, 0},
    [SND_ROUND] = {NONE, 2, {392, 523}, {5, 7}},
    [SND_WIN] = {NONE, 3, {523, 659, 1047}, {7, 7, 16}},
    [SND_LOSE] = {NONE, 3, {300, 250, 200}, {10, 10, 18}},
    [SND_CRIT] = {2, 0},
    [SND_BOLT] = {6, 0},
    [SND_LIGHTNING] = {5, 0},
    [SND_SUMMON] = {10, 0},
    [SND_TELEPORT] = {6, 1, {1047}, {9}},
    [SND_CURSE] = {NONE, 2, {220, 165}, {7, 12}},
    [SND_DRINK] = {12, 0},
    [SND_EAT] = {3, 0},
    [SND_FLY] = {1, 0},
    [SND_MENU] = {NONE, 1, {660}, {2}},
    [SND_CONFIRM] = {11, 0},
    [SND_BACK] = {NONE, 2, {440, 330}, {2, 3}},
    [SND_ERROR] = {NONE, 1, {140}, {7}},
};

static struct {
    u8 fx, step, left;                  /* the tone sequence running; left frames of the step */
    bool on;
} tone;

#define TONE_CH 2                       /* PSG channel for tone effects (songs use it, so not then) */

void sound_play(u8 fx)
{
    const Fx *f;
#ifdef LOC_MD_MUTE                      /* whine hunt: no sound at all */
    (void)fx;
    return;
#endif
    if (!sound_on || fx >= SND_COUNT)
        return;
    f = &FX[fx];
#ifndef LOC_MD_NOPCM
    if (f->sample != NONE && SAMPLES[f->sample].pcm)
        XGM2_playPCM(SAMPLES[f->sample].pcm, SAMPLES[f->sample].len, SOUND_PCM_CH_AUTO);
#endif
    if (f->steps && !music_playing()) {
        tone.fx = fx;
        tone.step = 0;
        tone.left = f->frames[0];
        tone.on = TRUE;
        PSG_setFrequency(TONE_CH, f->hz[0]);
        PSG_setEnvelope(TONE_CH, 4);
    }
}

static void tone_tick(void)
{
    const Fx *f;
    if (!tone.on)
        return;
    f = &FX[tone.fx];
    if (--tone.left)
        return;
    if (++tone.step >= f->steps) {
        tone.on = FALSE;
        PSG_setEnvelope(TONE_CH, PSG_ENVELOPE_MIN);
        return;
    }
    tone.left = f->frames[tone.step];
    PSG_setFrequency(TONE_CH, f->hz[tone.step]);
}

/* ---------- songs ----------
 * Format (tools/gen_music.py): "LOCM" ver(2) u16le ms_per_unit, u8 channels, u8 flags (1 = loop),
 * per channel: instrument, fallback, volume, attack/4, decay/4, sustain, release/4, u16le notes,
 * notes: u16le Hz (0 = rest), u8 units. All channels step in units of one eighth note. */

#define MAX_TONE 3
typedef struct {
    const u8 *notes;
    u16 count, idx;
    u8 units_left, psg, noise, vol, age, sample;
    bool sounding;
} MCh;

static MCh mch[4];
static u8 mch_n;
static bool song_on, song_loop;
static u16 unit_ms;
static u32 acc;                         /* milliseconds x 100 into the current unit */
static const u8 *song_data;

static const u8 *const SONGS[3] = {music_title, music_win, music_lose};

static u16 rd16(const u8 *p)
{
    return p[0] | (p[1] << 8);
}

static void ch_note(MCh *c)
{
    const u8 *n = c->notes + c->idx * 3;
    u16 hz = rd16(n);
    c->units_left = n[2];
    c->age = 0;
    c->idx++;
    c->sounding = hz != 0;
    if (!hz) {
        PSG_setEnvelope(c->psg, PSG_ENVELOPE_MIN);
        return;
    }
    if (c->noise) {
        PSG_setNoise(PSG_NOISE_TYPE_WHITE, PSG_NOISE_FREQ_CLOCK4);
    } else {
        PSG_setFrequency(c->psg, hz);
    }
    PSG_setEnvelope(c->psg, c->vol);
}

void music_start(Song s)
{
    const u8 *d = SONGS[s];
    const u8 *p = d + 9;
    u8 i, tones = 0, noises = 0;
    PSG_reset();
    song_on = FALSE;
    tone.on = FALSE;
#ifdef LOC_MD_MUTE
    return;
#endif
    if (!music_on)
        return;
    unit_ms = rd16(d + 5);
    song_loop = d[8] & 1;
    mch_n = 0;
    for (i = 0; i < d[7]; i++) {
        u8 inst = p[0], vol = p[2];
        u16 count = rd16(p + 7);
        bool noise = inst == 4 || inst == 5 || inst == 0x8F;
        MCh *c = &mch[mch_n];
        u16 loud = (u16)vol * 13 / 100;       /* the songs use volumes up to ~110 */
        u8 att = 15 - (u8)(loud > 13 ? 13 : loud);
        if (noise ? noises >= 1 : tones >= MAX_TONE) {
            p += 9 + count * 3;
            continue;
        }
        c->notes = p + 9;
        c->count = count;
        c->idx = 0;
        c->units_left = 0;
        c->noise = noise;
        c->psg = noise ? 3 : tones;
        c->sample = inst & 0x80;
        c->vol = att;
        c->sounding = FALSE;
        if (noise)
            noises++;
        else
            tones++;
        mch_n++;
        p += 9 + count * 3;
    }
    song_data = d;
    acc = 0;
    song_on = mch_n != 0;
    for (i = 0; i < mch_n; i++)
        ch_note(&mch[i]), mch[i].units_left--;   /* the first unit sounds now */
}

void music_stop(void)
{
    song_on = FALSE;
    PSG_reset();
}

bool music_playing(void)
{
    return song_on;
}

static void song_tick(void)
{
    u8 i;
    bool finished = TRUE;
    if (!song_on)
        return;
    acc += IS_PAL_SYSTEM ? 2000 : 1667;
    /* decay of sample instruments: plucks fade, drums are a short burst */
    for (i = 0; i < mch_n; i++) {
        MCh *c = &mch[i];
        c->age++;
        if (c->sounding && c->sample) {
            u8 att = c->vol + (c->age >> (c->noise ? 0 : 1));
            PSG_setEnvelope(c->psg, att > 15 ? 15 : att);
        }
    }
    if (acc < (u32)unit_ms * 100)
        return;
    acc -= (u32)unit_ms * 100;
    for (i = 0; i < mch_n; i++)
        if (mch[i].units_left || mch[i].idx < mch[i].count)
            finished = FALSE;
    if (finished) {                     /* the last unit has been played out */
        if (song_loop)
            music_start((Song)(song_data == music_title ? SONG_TITLE : song_data == music_win ? SONG_WIN : SONG_LOSE));
        else
            music_stop();
        return;
    }
    for (i = 0; i < mch_n; i++) {
        MCh *c = &mch[i];
        if (c->units_left == 0) {
            if (c->idx >= c->count)
                continue;
            ch_note(c);
        }
        c->units_left--;
    }
}

void sound_tick(void)
{
    tone_tick();
    song_tick();
}

void sound_init(void)
{
#if !defined(LOC_MD_MUTE) && !defined(LOC_MD_NOPCM)
    XGM2_loadDriver(TRUE);
#endif
    PSG_reset();
}
