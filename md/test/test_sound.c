/*
 * Host test of the sound player (md/src/sound_md.c) against a model of the SN76489 (PSG): what matters on
 * real hardware is that every tone and every song ends in silence. (In SGDK a PSG envelope of 0 is the
 * LOUDEST level and 15 is off; PSG_ENVELOPE_MAX is 0. A tone effect that "silenced" the channel with
 * PSG_ENVELOPE_MAX whined on the console until reset, which BlastEm does not make you notice.)
 */
#include "loc.h"
#include "test.h"

#include <setjmp.h>
#include <stdlib.h>
#include <string.h>

/* the SGDK sound API, modelled */
#define PSG_ENVELOPE_MIN 15
#define PSG_ENVELOPE_MAX 0
#define PSG_NOISE_TYPE_WHITE 1
#define PSG_NOISE_FREQ_CLOCK4 2
#define SOUND_PCM_CH_AUTO 0
static u8 psg_att[4] = {15, 15, 15, 15};        /* attenuation per channel, 15 = silent */
static u16 psg_tone[4];
static u16 pcm_plays;
static void PSG_reset(void)
{
    int i;
    for (i = 0; i < 4; i++) {
        psg_att[i] = 15;
        psg_tone[i] = 0;
    }
}
static void PSG_setEnvelope(u8 ch, u8 v) { psg_att[ch & 3] = v & 15; }
static void PSG_setFrequency(u8 ch, u16 hz) { psg_tone[ch & 3] = hz; }
static void PSG_setNoise(u8 type, u8 freq) { (void)type; (void)freq; psg_tone[3] = 1; }
static void XGM2_loadDriver(bool pal) { (void)pal; }
static void XGM2_playPCM(const u8 *p, u16 len, u8 ch) { (void)p; (void)len; (void)ch; pcm_plays++; }

/* resources.h of rescomp: the samples (their size is all the player needs) and the songs */
#define SFX(n) static const u8 sfx_##n[8] = {0}
SFX(00); SFX(01); SFX(02); SFX(03); SFX(04); SFX(05); SFX(06); SFX(07);
SFX(08); SFX(09); SFX(10); SFX(11); SFX(12); SFX(13); SFX(14); SFX(15);
extern const unsigned char music_title[], music_win[], music_lose[];

#include "../src/sound_md.c"

int test_checks, test_failures;
static jmp_buf abort_test;
static const char *current_test = "";
void test_fail_now(const char *why)
{
    test_failures++;
    printf("  FAIL (%s) %s\n", current_test, why);
    longjmp(abort_test, 1);
}

static void run_frames(u16 n)
{
    while (n--)
        sound_tick();
}

static bool all_silent(void)
{
    u8 c;
    for (c = 0; c < 4; c++)
        if (psg_att[c] != 15)
            return FALSE;
    return TRUE;
}

static void t_every_effect_ends_in_silence(void)
{
    u8 fx;
    for (fx = 0; fx < SND_COUNT; fx++) {
        PSG_reset();
        music_on = TRUE;
        sound_on = TRUE;
        sound_play(fx);
        run_frames(120);
        CHECK(all_silent(), "after an effect the PSG is silent (envelope 15, not 0)");
    }
}

static void t_tone_effect_sounds_while_it_runs(void)
{
    PSG_reset();
    sound_on = TRUE;
    sound_play(SND_MENU);
    CHECK(psg_att[TONE_CH] < 15, "a tone effect is audible while it plays");
    CHECK(psg_tone[TONE_CH] != 0, "at its frequency");
}

static void t_every_song_ends_in_silence(void)
{
    u8 s;
    for (s = SONG_TITLE; s <= SONG_LOSE; s++) {
        u16 f;
        PSG_reset();
        music_on = TRUE;
        sound_on = TRUE;
        music_start((Song)s);
        CHECK(music_playing(), "the song starts");
        if (s == SONG_TITLE) {              /* loops: stopped by hand */
            run_frames(600);
            CHECK(music_playing(), "the title song loops");
            music_stop();
        } else {
            for (f = 0; f < 60 * 120 && music_playing(); f++)
                sound_tick();
            CHECK(!music_playing(), "a jingle ends");
        }
        CHECK(all_silent(), "after a song every channel is silent");
    }
}

static void t_rests_are_silent(void)
{
    u8 song, c, tested = 0;
    for (song = SONG_TITLE; song <= SONG_LOSE; song++) {
        PSG_reset();
        music_on = TRUE;
        music_start((Song)song);
        for (c = 0; c < mch_n; c++) {
            u16 k;
            MCh *ch = &mch[c];
            /* a rest (0 Hz) must silence its channel, whatever sounded before */
            for (k = 0; k < ch->count; k++)
                if (rd16(ch->notes + k * 3) == 0) {
                    psg_att[ch->psg] = 3;
                    ch->idx = k;
                    ch->units_left = 0;
                    ch_note(ch);
                    CHECK_EQ(psg_att[ch->psg], 15, "a rest silences its channel");
                    tested++;
                    break;
                }
        }
        music_stop();
    }
    CHECK(tested > 0, "the songs contain rests to test");
}

static void t_sound_off_makes_no_noise(void)
{
    PSG_reset();
    pcm_plays = 0;
    sound_on = FALSE;
    sound_play(SND_MENU);
    sound_play(SND_HIT);
    CHECK(all_silent(), "no tone with the sound off");
    CHECK_EQ(pcm_plays, 0, "no sample with the sound off");
    sound_on = TRUE;
}

typedef struct {
    const char *name;
    void (*fn)(void);
} Test;
#define T(n) {#n, n}
static const Test TESTS[] = {
    T(t_every_effect_ends_in_silence), T(t_tone_effect_sounds_while_it_runs), T(t_every_song_ends_in_silence),
    T(t_rests_are_silent), T(t_sound_off_makes_no_noise),
};

int main(int argc, char **argv)
{
    unsigned i, ran = 0, failed = 0;
    const char *only = argc > 1 ? argv[1] : NULL;
    setvbuf(stdout, NULL, _IONBF, 0);
    for (i = 0; i < sizeof TESTS / sizeof TESTS[0]; i++) {
        int before;
        if (only && *only && !strstr(TESTS[i].name, only))
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
