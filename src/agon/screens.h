/*
 * Full-screen screens (M5a): the end screen after a won or lost game.
 * M5 adds the help page viewer, the lexicon and (later) the title screen.
 */
#ifndef LOC_SCREENS_H
#define LOC_SCREENS_H

#include <stdbool.h>
#include <stdint.h>

#include "../core/game.h"
#include "../core/lexicon.h"
#include "../core/save.h"

typedef struct {
    const char *name;        /* the player's wizard */
    const char *scenario;    /* scenario title, NULL on test maps */
    GameOutcome outcome;     /* OUT_WIN or OUT_LOSE */
    uint8_t rounds;
    uint16_t vp;             /* total victory points */
    uint16_t loot_vp;        /* of which carried treasure */
    uint8_t kills;
    bool campaign;           /* XP / level lines are shown */
    uint16_t xp_total;       /* XP of the wizard after the game */
    uint16_t xp_gain;
    uint8_t level;
    bool level_up;
} EndInfo;

/* Show the end screen and wait for a key. True: back to the main menu
 * (Enter/Space), false: quit the program (Esc). */
bool screen_end(const EndInfo *info);

/* Paged help from /loc/help/<name>.hlp: Left/Right turn pages, Esc (or
 * Enter) returns. False when the file is missing or invalid (the caller
 * may fall back to the built-in key list). The screen is cleared; redraw
 * the game with view_invalidate() afterwards. */
bool screen_help(const char *file);

/* The tutorial hint line for the current step (first body line of the
 * step's page in /loc/help/tutorial.hlp). "" while unset. Load the file
 * once with tutorial_hints_load() before the scenario starts. */
bool tutorial_hints_load(const char *file);
const char *tutorial_hint_line(uint8_t step);

/* The lexicon of discoveries: list of creatures and objects, Enter shows
 * the detail page (portrait, values, description). Blocking, Esc leaves. */
void screen_lexicon(const Lexicon *lex);
/* D77: the active unit's pack (i): arrows choose, the lexicon text says what
 * an object is for, Enter/w takes it in the hand (ACT_CHANGE). */
void screen_inventory(World *w, uint8_t unit);

/* Load /loc/help/lexicon.hlp once for the designer's spell shop detail
 * (creature pages are 0..CR_COUNT-1 in csv order). False when missing. */
bool lexicon_texts_load(void);
/* Portrait, main attributes and the short description of a creature,
 * drawn into the menu window starting at text row `top` (needs one
 * lexicon_texts_load first). */
void lexicon_creature_panel(uint8_t kind, uint8_t top);
/* Load /loc/help/spells.hlp (one page per spell, csv order) and draw
 * the spell detail panel - category, mana, damage dice, description -
 * into the menu window at text row `top`. False when missing. */
bool spells_texts_load(void);
void spell_panel(uint8_t spell, uint8_t top);

/* Title screen (M5d): the streamed title bitmap (/loc/title.bin) and the
 * title music (/loc/title.bin's neighbour music/title.bin). Any key
 * stops the music and returns. */
bool screen_title(void);

/* Between the turns (original style): who acts now, the round and the
 * victory points; the map stays hidden, the frontend only plays sounds. */
void screen_phase(const char *who, uint8_t round, uint8_t n,
                  const char *const *names, const uint16_t *vp);

/* Borrow the shared buffer arena for a save or load (QUIRK S6): the
 * SaveGame image and SAVE_BUF_SIZE bytes in *buf. Drops the cached spell
 * and lexicon texts. */
SaveGame *screens_borrow_save(uint8_t **buf);

#endif
