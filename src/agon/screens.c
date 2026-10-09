#include "screens.h"

#include <agon/keyboard.h>
#include <agon/mos.h>
#include <stdio.h>
#include <string.h>

#include "../core/colors.h"
#include "../core/gen/data.h"
#include "../core/items.h"
#include "../core/ride.h"
#include "../core/tutorial.h"
#include "input.h"
#include "music.h"
#include "render.h"
#include "sound.h"

/* Strings use the umlaut font codes (umfont.c): \204 ae, \224 oe,
 * \201 ue, \341 ss. */

static void centred(uint8_t row, uint8_t colour, const char *s)
{
    size_t n = strlen(s);
    uint8_t col = n >= 40 ? 0 : (uint8_t)((40 - n) / 2);
    render_menu_text(col, row, colour, s);
}

/* Stat lines: next to the end picture (column 15, narrow labels) or,
 * without it, centred in the old wide layout. */
static uint8_t stat_col = 8, stat_label = 22;

static void line(uint8_t row, const char *label, uint16_t value,
                 uint8_t colour)
{
    char buf[40];
    snprintf(buf, sizeof buf, "%-*s%5u", stat_label, label, value);
    render_menu_text(stat_col, row, colour, buf);
}

bool screen_end(const EndInfo *info)
{
    struct keyboard_event_t e;
    char buf[40];
    bool win = info->outcome == OUT_WIN;
    uint8_t row = 10;

    render_screen_clear();
    if (!music_start(win ? "music/win.bin" : "music/lose.bin"))
        sound_play(win ? SND_WIN : SND_LOSE);   /* music off or missing */
    render_frame(8, 8, 311, 231, win ? C_BRIGHT_YELLOW : C_RED);
    if (win) {
        render_heading_centred(20, C_BRIGHT_YELLOW, "Gl\201ckwunsch!");
        snprintf(buf, sizeof buf, "%.16s entkommt durchs Portal!",
                 info->name);
        centred(5, C_BRIGHT_GREEN, buf);
    } else {
        render_heading_centred(20, C_BRIGHT_RED, "GAME OVER");
        snprintf(buf, sizeof buf, "%.16s ist gefallen.", info->name);
        centred(5, C_BRIGHT_RED, buf);
    }
    if (info->scenario)
        centred(7, C_BRIGHT_CYAN, info->scenario);
    if (render_show_end_picture(win, 16, 76)) {
        render_frame(14, 74, 113, 173, win ? C_YELLOW : C_RED);
        stat_col = 15;
        stat_label = 16;
    } else {
        stat_col = 8;
        stat_label = 22;
    }

    line(row++, "Runden", info->rounds, C_BRIGHT_WHITE);
    line(row++, "Besiegte Gegner", info->kills, C_BRIGHT_WHITE);
    line(row++, "Beute (Punkte)", info->loot_vp, C_BRIGHT_WHITE);
    line(row++, "Siegpunkte", info->vp, C_BRIGHT_YELLOW);
    if (info->campaign) {
        row++;
        line(row++, "Erfahrung neu", info->xp_gain, C_BRIGHT_GREEN);
        line(row++, "Erfahrung ges.", info->xp_total, C_BRIGHT_GREEN);
        snprintf(buf, sizeof buf, "Stufe %u%s", info->level,
                 info->level_up ? " - Aufstieg!" : "");
        render_menu_text(stat_col, row, info->level_up ? C_BRIGHT_YELLOW : C_GREY,
                         buf);
        if (info->level_up)
            sound_play(SND_SUMMON);       /* a level up shimmers */
    } else if (!win) {
        row++;
        render_menu_text(stat_col, row, C_GREY, "Beute verloren.");
    }
    centred(25, C_GREY, "Enter: Hauptmen\201   Esc: Beenden");

    for (;;) {                           /* drain, then wait for a key */
        while (!kbuf_poll_event(&e))
            audio_poll();
        if (!e.isdown)
            continue;
        if (e.vkey == VK_ESC) {
            music_stop();
            return false;
        }
        if (e.ascii == 13 || e.vkey == VK_SPACE) {
            sound_play(SND_CONFIRM);
            return true;                 /* the jingle may ring out */
        }
    }
}

/* ---------- phase screen (original: the others act unseen) ---------- */

/* A vine-like border: green frame with blue buds every 8 pixels. */
static void ornate_frame(int x0, int y0, int x1, int y1)
{
    int x, y;
    render_frame(x0, y0, x1, y1, C_GREEN);
    render_frame(x0 + 4, y0 + 4, x1 - 4, y1 - 4, C_GREEN);
    for (x = x0 + 4; x <= x1 - 4; x += 8) {
        render_dot(x, y0 + 2, C_BRIGHT_BLUE);
        render_dot(x, y1 - 2, C_BRIGHT_BLUE);
    }
    for (y = y0 + 4; y <= y1 - 4; y += 8) {
        render_dot(x0 + 2, y, C_BRIGHT_BLUE);
        render_dot(x1 - 2, y, C_BRIGHT_BLUE);
    }
}

void screen_phase(const char *who, uint8_t round, uint8_t n,
                  const char *const *names, const uint16_t *vp)
{
    char buf[40];
    uint8_t i, row = 12;
    render_screen_clear();
    ornate_frame(24, 20, 295, 190);
    render_heading_centred(30, C_BRIGHT_MAGENTA, ">>> * <<<");
    snprintf(buf, sizeof buf, "%-11s %s", "Am Zug:", who);
    render_menu_text(6, 7, C_YELLOW, buf);
    snprintf(buf, sizeof buf, "%-11s %u", "Runde:", round);
    render_menu_text(6, 9, C_YELLOW, buf);
    render_menu_text(6, 11, C_YELLOW, "Siegpunkte:");
    for (i = 0; i < n && row < 21; i++, row++) {
        char dots[40];
        uint8_t len = (uint8_t)strlen(names[i]), k;
        if (len > 16)
            len = 16;
        for (k = 0; k < 20 - len; k++)    /* name + dots = 20 columns */
            dots[k] = '.';
        dots[k] = 0;
        snprintf(buf, sizeof buf, "%.16s%s %4u", names[i], dots, vp[i]);
        render_menu_text(6, (uint8_t)(row + 1), C_YELLOW, buf);
    }
    centred(26, C_GREY, "- man hoert nur, was geschieht -");
}

/* ---------- help pages from the SD card (M5, ADR 0011) ---------- */

#define HELP_MAX 4096                  /* keys.hlp is ~3.2 KB */
#define HELP_PAGES_MAX 72
#define LEXICON_MAX 7168

#define SPELLS_MAX 4600

/* One arena for the big buffers that are never needed together (QUIRK
 * S6): saving and loading borrow all of it; the help, spell and lexicon
 * texts live side by side the rest of the time. A save drops the cached
 * texts, they reload from the SD card the next time they are shown. */
static union {
    struct {
        SaveGame state;
        uint8_t buf[SAVE_BUF_SIZE];
    } save;
    struct {
        uint8_t help[HELP_MAX];
        uint8_t lex[LEXICON_MAX];        /* lexicon.hlp is ~6 KB */
        uint8_t spells[SPELLS_MAX];
    } text;
} arena;
#define help_buf arena.text.help
#define lex_buf arena.text.lex
#define spells_buf arena.text.spells

static uint24_t lex_len;                 /* bytes of lexicon.hlp read */
static uint16_t help_page[HELP_PAGES_MAX];
static uint8_t help_count;

/* Parse a .hlp image in place: page offsets into page_off, returns the
 * page count (0 when the image is invalid or truncated). */
static uint8_t help_parse(uint8_t *buf, uint24_t len, uint16_t *page_off)
{
    uint16_t pages, off, i;
    uint8_t n;
    if (len < 8 || memcmp(buf, "LOCH", 4) != 0 || buf[4] != 1)
        return 0;
    pages = (uint16_t)(buf[5] | (buf[6] << 8));
    if (pages == 0 || pages > HELP_PAGES_MAX)
        return 0;
    off = 7;
    for (n = 0; n < pages; n++) {
        if (off >= len)
            return 0;
        page_off[n] = off;
        off = (uint16_t)(off + 1 + buf[off]);        /* title */
        if (off >= len)
            return 0;
        {
            uint8_t lines = buf[off];
            off++;
            for (i = 0; i < lines; i++) {
                if (off >= len)
                    return 0;
                off = (uint16_t)(off + 1 + buf[off]);/* one text line */
                if (off > len)
                    return 0;
            }
        }
    }
    return (uint8_t)pages;
}

/* Copy the n-th text line of a page (n = 0 is the first body line). */
static bool help_line(char *out, uint8_t cap, uint8_t page, uint8_t n)
{
    uint16_t off;
    uint8_t title_len, lines, i, len;
    if (page >= help_count)
        return false;
    off = help_page[page];
    title_len = help_buf[off];
    off = (uint16_t)(off + 1 + title_len);
    lines = help_buf[off++];
    for (i = 0; i < lines; i++) {
        len = help_buf[off++];
        if (i == n) {
            if (len >= cap)
                len = (uint8_t)(cap - 1);
            memcpy(out, help_buf + off, len);
            out[len] = 0;
            return true;
        }
        off = (uint16_t)(off + len);
    }
    return false;
}

static void help_draw(uint8_t page)
{
    char buf[40];
    uint16_t off = help_page[page];
    uint8_t title_len, lines, i, row = 2;

    render_screen_clear();
    title_len = help_buf[off++];
    memcpy(buf, help_buf + off, title_len);
    buf[title_len] = 0;
    off = (uint16_t)(off + title_len);
    render_heading_centred(0, C_BRIGHT_YELLOW, buf);
    lines = help_buf[off++];
    for (i = 0; i < lines && row < 28; i++) {
        uint8_t len = help_buf[off++];
        memcpy(buf, help_buf + off, len);
        buf[len] = 0;
        off = (uint16_t)(off + len);
        render_menu_text(1, row++, C_BRIGHT_WHITE, buf);
    }
    snprintf(buf, sizeof buf, "Blatt %u/%u   Esc zur\201ck", page + 1,
             help_count);
    centred(29, C_GREY, buf);
}

bool screen_help(const char *file)
{
    struct keyboard_event_t e;
    uint8_t fh;
    uint24_t len;
    uint8_t page = 0;

    fh = mos_fopen(file, FA_READ);
    if (!fh)
        return false;
    len = mos_fread(fh, (char *)help_buf, sizeof help_buf);
    mos_fclose(fh);
    help_count = help_parse(help_buf, len, help_page);
    if (!help_count)
        return false;

    help_draw(page);
    for (;;) {
        while (!kbuf_poll_event(&e))
            audio_poll();
        if (!e.isdown)
            continue;
        if (e.vkey == VK_ESC || e.ascii == 13 || e.vkey == VK_SPACE) {
            sound_play(SND_BACK);
            return true;
        }
        if (e.vkey == VK_LEFT)
            page = page ? (uint8_t)(page - 1) : (uint8_t)(help_count - 1);
        else if (e.vkey == VK_RIGHT)
            page = (uint8_t)((page + 1) % help_count);
        else
            continue;
        sound_play(SND_MENU);
        help_draw(page);
    }
}

/* ---------- title screen (M5d) ---------- */

bool screen_title(void)
{
    struct keyboard_event_t e;

    music_start("music/title.bin");
    render_screen_clear();
    if (!render_show_title()) {           /* SD missing: plain text */
        render_heading_centred(28, C_BRIGHT_YELLOW, "LORDS OF CHAOS");
        centred(6, C_BRIGHT_CYAN, "Ein Remake f\201r den Agon Light");
    }
    centred(28, C_GREY, "- Taste dr\201cken -");
    for (;;) {
        audio_poll();
        while (kbuf_poll_event(&e)) {
            if (e.isdown) {               /* the song plays on in the menu */
                render_screen_clear();
                return true;
            }
        }
    }
}

/* ---------- tutorial hints (loaded once per program run) ---------- */

#define TUT_HINT_MAX (TUT_COUNT - 1)   /* one hint line per step */
static char tut_hint[TUT_HINT_MAX][40];

bool tutorial_hints_load(const char *file)
{
    uint8_t fh;
    uint24_t len;
    uint16_t page_off[HELP_PAGES_MAX];
    uint8_t pages, i;

    fh = mos_fopen(file, FA_READ);
    if (!fh)
        return false;
    len = mos_fread(fh, (char *)help_buf, sizeof help_buf);
    mos_fclose(fh);
    pages = help_parse(help_buf, len, page_off);
    if (pages < 2)
        return false;
    memset(tut_hint, 0, sizeof tut_hint);
    help_count = pages;                  /* borrow the shared tables */
    memcpy(help_page, page_off, sizeof help_page);
    for (i = 0; i < TUT_HINT_MAX && i + 1 < pages; i++)
        help_line(tut_hint[i], sizeof tut_hint[i], (uint8_t)(i + 1), 0);
    help_count = 0;
    return true;
}

const char *tutorial_hint_line(uint8_t step)
{
    if (step >= TUT_HINT_MAX)
        return "";
    return tut_hint[step];
}

/* ---------- lexicon (M5) ---------- */

/* Three list sections: creatures, objects 0..19, objects 20..39. */
#define LEX_SECTIONS 3
#define LEX_TITLE_ROW 0
#define LEX_LIST_ROW 2
#define LEX_COLS 2
#define LEX_COL_X 1
#define LEX_COL_W 19

static const char *lexicon_section_title(uint8_t section)
{
    if (section == 0)
        return "Kreaturen";
    return section == 1 ? "Objekte I" : "Objekte II";
}

static uint8_t lexicon_section_entries(uint8_t section)
{
    if (section == 0)
        return CR_COUNT;
    return (uint8_t)((OBJ_COUNT + 1) / 2);
}

/* Section and index of an entry for the flat entry number (creatures
 * first, then objects). */
static void lexicon_entry(uint16_t entry, uint8_t *section, uint16_t *idx)
{
    if (entry < CR_COUNT) {
        *section = 0;
        *idx = entry;
    } else {
        *section = 1 + (entry - CR_COUNT) / ((OBJ_COUNT + 1) / 2);
        *idx = (entry - CR_COUNT) % ((OBJ_COUNT + 1) / 2);
    }
}

static uint16_t lexicon_entry_of(uint8_t section, uint16_t idx)
{
    if (section == 0)
        return idx;
    return (uint16_t)(CR_COUNT + (section - 1) * ((OBJ_COUNT + 1) / 2) + idx);
}

/* One list row: name or ??? plus a mark when discovered. */
static void lexicon_row(const Lexicon *lex, uint8_t section, uint16_t idx,
                        uint8_t col, uint8_t row, bool cursor)
{
    char buf[24];
    uint16_t entry = lexicon_entry_of(section, idx);
    bool seen = entry < CR_COUNT ? lexicon_seen_creature(lex, (uint8_t)entry)
                                 : lexicon_seen_object(lex,
                                       (uint8_t)(entry - CR_COUNT));
    const char *name = entry < CR_COUNT
        ? CREATURES[entry].name : OBJECTS[entry - CR_COUNT].name;
    snprintf(buf, sizeof buf, "%c%c%-16.16s", cursor ? '>' : ' ',
             seen ? '*' : ' ', seen ? name : "???");
    render_menu_text((uint8_t)(LEX_COL_X + col * LEX_COL_W), row,
                     seen ? C_BRIGHT_WHITE : C_GREY, buf);
}

static void lexicon_draw_list(const Lexicon *lex, uint8_t section,
                              uint16_t cursor)
{
    uint16_t total, i;
    uint8_t entries = lexicon_section_entries(section);
    uint8_t half = (uint8_t)((entries + LEX_COLS - 1) / LEX_COLS);
    char buf[40];
    uint16_t seen_all = 0;
    uint16_t e;

    total = (uint16_t)(CR_COUNT + OBJ_COUNT);
    for (e = 0; e < total; e++)
        seen_all += e < CR_COUNT ? lexicon_seen_creature(lex, (uint8_t)e)
                                 : lexicon_seen_object(lex, (uint8_t)(e - CR_COUNT));
    render_screen_clear();
    snprintf(buf, sizeof buf, "Lexikon  %u/%u entdeckt", seen_all, total);
    render_heading_centred(LEX_TITLE_ROW, C_BRIGHT_YELLOW, buf);
    for (i = 0; i < entries; i++) {
        uint8_t col = (uint8_t)(i / half);
        uint16_t row_idx = (uint16_t)(i % half);
        if (col >= LEX_COLS)
            break;
        lexicon_row(lex, section, i, col,
                    (uint8_t)(LEX_LIST_ROW + row_idx), i == cursor);
    }
    snprintf(buf, sizeof buf, "%s   Blatt %u/3", lexicon_section_title(section),
             section + 1);
    centred(28, C_GREY, buf);
    centred(29, C_GREY, "Enter Detail   Esc zur\201ck");
}

/* The description of lexicon page `entry` (creatures first, then objects),
 * one line per row from `row` up to (excluding) `last`. Nothing when the
 * texts are not loaded. */
static void lexicon_draw_text(uint16_t entry, uint8_t row, uint8_t last)
{
    char buf[40];
    uint16_t off;
    uint8_t title_len, lines, i;
    uint16_t page_off[HELP_PAGES_MAX];
    if (!lex_len || (uint16_t)entry >= help_parse(lex_buf, lex_len, page_off))
        return;
    off = page_off[entry];
    title_len = lex_buf[off];
    off = (uint16_t)(off + 1 + title_len);
    lines = lex_buf[off++];
    for (i = 0; i < lines && row < last; i++) {
        uint8_t len = lex_buf[off++];
        memcpy(buf, lex_buf + off, len);
        buf[len] = 0;
        off = (uint16_t)(off + len);
        render_menu_text(1, row++, C_BRIGHT_WHITE, buf);
    }
}

/* Detail page: portrait, table values, description from lexicon.hlp. */
static void lexicon_draw_detail(uint16_t entry)
{
    char buf[40];
    uint8_t row = 2;
    bool is_creature = entry < CR_COUNT;

    render_screen_clear();
    if (is_creature) {
        const CreatureDef *c = &CREATURES[entry];
        render_frame(0, 12, 31, 39, C_BRIGHT_BLUE);
        render_draw_tile((uint16_t)(CREATURE_TILE[entry]), 4, 16);
        snprintf(buf, sizeof buf, "%s", c->name);
        render_menu_text(5, 2, C_BRIGHT_WHITE, buf);
        snprintf(buf, sizeof buf, "Kampf %-3u  Vert. %-3u", c->combat,
                 c->defence);
        render_menu_text(5, 4, C_BRIGHT_WHITE, buf);
        snprintf(buf, sizeof buf, "Magieres. %u  Widerst. -", c->magic_res);
        render_menu_text(5, 5, C_GREY, buf);
        snprintf(buf, sizeof buf, "Kons %u  Ausd %u  AP %u", c->con, c->stamina,
                 c->ap);
        render_menu_text(5, 6, C_GREY, buf);
        snprintf(buf, sizeof buf, "Wert %u VP  Tragen %u", c->vp, c->carry);
        render_menu_text(5, 7, C_GREY, buf);
        buf[0] = 0;
        if (c->flags & CF_UNDEAD)
            strcat(buf, "Untot ");
        if (c->flags & CF_MOUNT)
            strcat(buf, "Reittier ");
        if (c->flags & CF_RIDE)
            strcat(buf, "reitet ");
        if (c->flags & CF_WEAPONS)
            strcat(buf, "Waffen ");
        if (c->flags & CF_USE)
            strcat(buf, "H\204nde");
        render_menu_text(5, 8, C_BRIGHT_CYAN, buf);
        row = 11;
    } else {
        uint8_t k = (uint8_t)(entry - CR_COUNT);
        const ObjectDef *o = &OBJECTS[k];
        render_frame(0, 12, 31, 39, C_BRIGHT_BLUE);
        render_draw_tile(o->tile, 4, 16);
        snprintf(buf, sizeof buf, "%s", o->name);
        render_menu_text(5, 2, C_BRIGHT_WHITE, buf);
        snprintf(buf, sizeof buf, "Gewicht %u  Wert %u VP", o->weight, o->vp);
        render_menu_text(5, 4, C_GREY, buf);
        if (o->weapon != WEAPON_NONE) {
            const WeaponDef *wd = &WEAPONS[o->weapon];
            snprintf(buf, sizeof buf, "Waffe: Kampf +%u Vert. +%u",
                     wd->combat, wd->defence);
            render_menu_text(5, 5, C_BRIGHT_CYAN, buf);
            if (wd->ranged)
                snprintf(buf, sizeof buf, "Fernkampf, Reichw. %u", wd->ranged);
            else
                snprintf(buf, sizeof buf, "Nahkampf");
            render_menu_text(5, 6, C_GREY, buf);
        }
        if (o->eat_con || o->eat_mana) {
            snprintf(buf, sizeof buf, "Essen: +%u Kons +%u Mana", o->eat_con,
                     o->eat_mana);
            render_menu_text(5, 7, C_BRIGHT_GREEN, buf);
        }
        row = 9;
    }

    lexicon_draw_text(entry, row, 26);
    centred(29, C_GREY, "Esc zur\201ck");
}

/* ---------- spell descriptions (designer shop, M5) ---------- */

static uint24_t spells_len;
static uint8_t spell_count;              /* pages = spells, csv order */
static uint16_t spell_page[HELP_PAGES_MAX];

SaveGame *screens_borrow_save(uint8_t **buf)
{
    lex_len = 0;                         /* the texts are overwritten */
    spells_len = 0;
    *buf = arena.save.buf;
    return &arena.save.state;
}

bool spells_texts_load(void)
{
    uint8_t fh;
    uint24_t len = 0;
    uint8_t pages;
    if (spells_len)
        return true;
    fh = mos_fopen("help/spells_de.hlp", FA_READ);
    if (fh) {
        len = mos_fread(fh, (char *)spells_buf, (uint24_t)sizeof spells_buf);
        mos_fclose(fh);
    }
    if (len == 0)
        return false;
    pages = help_parse(spells_buf, len, spell_page);
    if (pages < SPELL_COUNT) {           /* one page per spell, csv order */
        spells_len = 0;
        return false;
    }
    spells_len = len;
    spell_count = pages;
    return true;
}

/* Category + mana + damage dice + description of one spell. */
void spell_panel(uint8_t spell, uint8_t top)
{
    static const char *const CAT[4] = {"Beschwoerung", "Trank", "Flaeche",
                                       "Zauber"};
    const SpellDef *s;
    char buf[40];
    uint16_t off;
    uint8_t lines, i, row = (uint8_t)(top + 3);

    if (spell >= SPELL_COUNT)
        return;
    s = &SPELLS[spell];
    render_frame(0, (int)(top * 8) - 2, 319, (int)((top + 11) * 8),
                 C_BRIGHT_BLUE);
    snprintf(buf, sizeof buf, "%.20s (%s)", s->name, CAT[s->category]);
    render_menu_text(1, top, C_BRIGHT_YELLOW, buf);
    snprintf(buf, sizeof buf, "Mana L1:%u  x(Stufe+1)", s->mana);
    render_menu_text(1, (uint8_t)(top + 1), C_BRIGHT_WHITE, buf);
    if (spell == SP_MAGIC_BOLT)
        snprintf(buf, sizeof buf, "Angriff 4xSt+25");
    else if (spell == SP_MAGIC_LIGHTNING)
        snprintf(buf, sizeof buf, "Angriff 4xSt+30, Fels 3x3");
    else
        snprintf(buf, sizeof buf, "kein Direktschaden");
    render_menu_text(1, (uint8_t)(top + 2), C_BRIGHT_CYAN, buf);
    if (!spells_len || spell >= spell_count)
        return;
    off = spell_page[spell];
    off = (uint16_t)(off + 1 + spells_buf[off]);   /* skip the title */
    lines = spells_buf[off++];
    for (i = 0; i < lines && row < top + 11; i++) {
        uint8_t len = spells_buf[off++];
        memcpy(buf, spells_buf + off, len);
        buf[len] = 0;
        off = (uint16_t)(off + len);
        render_menu_text(1, row++, C_BRIGHT_WHITE, buf);
    }
}

bool lexicon_texts_load(void)
{
    uint8_t fh;
    uint24_t len = 0;
    if (lex_len)
        return true;                       /* already loaded this run */
    fh = mos_fopen("help/lexicon_de.hlp", FA_READ);
    if (fh) {
        len = mos_fread(fh, (char *)lex_buf, (uint24_t)sizeof lex_buf);
        mos_fclose(fh);
    }
    if (len == 0 || help_parse(lex_buf, len, help_page) == 0) {
        lex_len = 0;
        return false;
    }
    lex_len = len;
    return true;
}

/* Portrait + main values + lexicon description of one creature, drawn
 * into the menu window (designer spell shop detail). */
void lexicon_creature_panel(uint8_t kind, uint8_t top)
{
    const CreatureDef *c;
    char buf[40];
    uint16_t off;
    uint8_t lines, i, row = (uint8_t)(top + 3);

    if (kind >= CR_COUNT)
        return;
    c = &CREATURES[kind];
    render_frame(0, (int)(top * 8) - 2, 319, (int)((top + 11) * 8), C_BRIGHT_BLUE);
    render_draw_tile((uint16_t)(CREATURE_TILE[kind]), 4, (int)(top * 8) + 4);
    snprintf(buf, sizeof buf, "%.14s", c->name);
    render_menu_text(5, top, C_BRIGHT_YELLOW, buf);
    snprintf(buf, sizeof buf, "K%u V%u  MR%u", c->combat, c->defence,
             c->magic_res);
    render_menu_text(5, (uint8_t)(top + 1), C_BRIGHT_WHITE, buf);
    snprintf(buf, sizeof buf, "L%u A%u AP%u", c->con, c->stamina, c->ap);
    render_menu_text(5, (uint8_t)(top + 2), C_BRIGHT_WHITE, buf);
    snprintf(buf, sizeof buf, "VP %u", c->vp);
    render_menu_text(17, (uint8_t)(top + 2), C_GREY, buf);
    if (!lex_len || (uint16_t)kind >= help_count)
        return;
    off = help_page[kind];
    off = (uint16_t)(off + 1 + lex_buf[off]);   /* skip the title */
    lines = lex_buf[off++];
    for (i = 0; i < lines && row < top + 11; i++) {
        uint8_t len = lex_buf[off++];
        memcpy(buf, lex_buf + off, len);
        buf[len] = 0;
        off = (uint16_t)(off + len);
        render_menu_text(1, row++, C_BRIGHT_WHITE, buf);
    }
}

void screen_lexicon(const Lexicon *lex)
{
    struct keyboard_event_t e;
    uint8_t section = 0, fh;
    uint16_t cursor[LEX_SECTIONS] = {0, 0, 0};
    bool detail = false;
    uint16_t entry = 0;
    uint24_t len;

    fh = mos_fopen("help/lexicon_de.hlp", FA_READ);
    if (fh) {
        len = mos_fread(fh, (char *)lex_buf, (uint24_t)sizeof lex_buf);
        mos_fclose(fh);
        if (help_parse(lex_buf, len, help_page) == 0)
            len = 0;
    } else {
        len = 0;
    }
    lex_len = len;

    lexicon_draw_list(lex, section, cursor[section]);
    for (;;) {
        while (!kbuf_poll_event(&e))
            audio_poll();
        if (!e.isdown)
            continue;
        if (e.vkey == VK_ESC) {
            sound_play(SND_BACK);
            if (detail) {
                detail = false;
                lexicon_draw_list(lex, section, cursor[section]);
                continue;
            }
            return;
        }
        if (detail) {
            if (e.ascii == 13 || e.vkey == VK_SPACE) {
                detail = false;
                lexicon_draw_list(lex, section, cursor[section]);
            }
            continue;
        }
        {
            uint8_t entries = lexicon_section_entries(section);
            if (e.vkey == VK_LEFT)
                section = section ? (uint8_t)(section - 1) : LEX_SECTIONS - 1;
            else if (e.vkey == VK_RIGHT)
                section = (uint8_t)((section + 1) % LEX_SECTIONS);
            else if (e.vkey == VK_UP)
                cursor[section] = cursor[section] ? cursor[section] - 1
                                                  : entries - 1;
            else if (e.vkey == VK_DOWN)
                cursor[section] = (uint16_t)((cursor[section] + 1) % entries);
            else if (e.ascii == 13 || e.vkey == VK_SPACE) {
                sound_play(SND_CONFIRM);
                detail = true;
                entry = lexicon_entry_of(section, cursor[section]);
                lexicon_draw_detail(entry);
                continue;
            } else
                continue;
            sound_play(SND_MENU);
            lexicon_draw_list(lex, section, cursor[section]);
        }
    }
}

/* ---------- inventory (D77) ---------- */

#define INV_LIST_ROW 4
#define INV_INFO_ROW 12

/* What the selected object is for, in numbers (the lexicon page says the rest). */
static void inventory_facts(uint8_t kind)
{
    const ObjectDef *o = &OBJECTS[kind];
    char buf[40];
    snprintf(buf, sizeof buf, "Gewicht %u", o->weight);
    render_menu_text(5, INV_INFO_ROW + 1, C_GREY, buf);
    if (o->weapon == WEAPON_SHIELD) {
        snprintf(buf, sizeof buf, "Getragen: Vert. +%u", WEAPONS[o->weapon].defence);
        render_menu_text(5, INV_INFO_ROW + 2, C_BRIGHT_CYAN, buf);
    } else if (o->weapon != WEAPON_NONE) {
        const WeaponDef *wd = &WEAPONS[o->weapon];
        snprintf(buf, sizeof buf, "In der Hand: Kampf +%u", wd->combat);
        render_menu_text(5, INV_INFO_ROW + 2, C_BRIGHT_CYAN, buf);
        snprintf(buf, sizeof buf, "Getragen: Vert. +%u", wd->defence);
        render_menu_text(5, INV_INFO_ROW + 3, C_BRIGHT_CYAN, buf);
    } else if (o->eat_con || o->eat_mana) {
        snprintf(buf, sizeof buf, "Essen (e): +%u Kons +%u Mana", o->eat_con,
                 o->eat_mana);
        render_menu_text(5, INV_INFO_ROW + 2, C_BRIGHT_GREEN, buf);
    } else if (o->vp) {
        snprintf(buf, sizeof buf, "Wert %u VP durchs Portal", o->vp);
        render_menu_text(5, INV_INFO_ROW + 2, C_BRIGHT_YELLOW, buf);
    }
}

static void inventory_draw(const World *w, uint8_t unit, uint8_t cursor,
                           const char *msg)
{
    const Unit *u = &w->units[unit];
    char buf[40];
    uint8_t i;
    render_screen_clear();
    render_heading_centred(0, C_BRIGHT_YELLOW, "Inventar");
    snprintf(buf, sizeof buf, "Tragkraft %u/%u", items_weight(w, unit),
             CREATURES[ride_actor_kind(u)].carry);
    render_menu_text(1, 2, C_GREY, buf);
    if (u->item_count == 0)
        render_menu_text(1, INV_LIST_ROW, C_GREY, "Nichts getragen.");
    for (i = 0; i < u->item_count; i++) {
        bool hand = u->in_use == i;
        snprintf(buf, sizeof buf, "%c%c %-18.18s%s", i == cursor ? '>' : ' ',
                 'a' + i, OBJECTS[u->items[i]].name, hand ? " Hand" : "");
        render_menu_text(1, (uint8_t)(INV_LIST_ROW + i),
                         hand ? C_BRIGHT_YELLOW : C_BRIGHT_WHITE, buf);
    }
    if (u->item_count) {
        uint8_t kind = u->items[cursor];
        render_frame(0, INV_INFO_ROW * 8 - 4, 319, 27 * 8, C_BRIGHT_BLUE);
        render_draw_tile(OBJECTS[kind].tile, 4, INV_INFO_ROW * 8 + 4);
        render_menu_text(5, INV_INFO_ROW, C_BRIGHT_WHITE, OBJECTS[kind].name);
        inventory_facts(kind);
        lexicon_draw_text((uint16_t)(CR_COUNT + kind), INV_INFO_ROW + 5, 27);
    }
    if (msg)
        render_menu_text(1, 27, C_BRIGHT_GREEN, msg);
    centred(29, C_GREY, "Pfeile w\204hlen  Enter/w Hand  Esc zur\201ck");
}

void screen_inventory(World *w, uint8_t unit)
{
    struct keyboard_event_t e;
    Unit *u = &w->units[unit];
    uint8_t cursor = u->in_use < u->item_count ? u->in_use : 0;
    const char *msg = NULL;

    lexicon_texts_load();
    inventory_draw(w, unit, cursor, msg);
    for (;;) {
        while (!kbuf_poll_event(&e))
            audio_poll();
        if (!e.isdown)
            continue;
        if (e.vkey == VK_ESC || e.ascii == 'i') {
            sound_play(SND_BACK);
            return;
        }
        if (!u->item_count)
            continue;
        msg = NULL;
        if (e.vkey == VK_UP)
            cursor = cursor ? (uint8_t)(cursor - 1) : (uint8_t)(u->item_count - 1);
        else if (e.vkey == VK_DOWN)
            cursor = (uint8_t)((cursor + 1) % u->item_count);
        else if (e.ascii >= 'a' && e.ascii < 'a' + u->item_count && e.ascii != 'w')
            cursor = (uint8_t)(e.ascii - 'a');
        else if (e.ascii == 13 || e.ascii == 'w' || e.vkey == VK_SPACE) {
            if (OBJECTS[u->items[cursor]].weapon == WEAPON_SHIELD)
                msg = "Das Schild wird getragen, nicht gef\201hrt.";
            else if (u->in_use == cursor)    /* in hand already: put it away */
                msg = items_wield(w, unit, NO_ITEM) ? "Leere H\204nde."
                                                    : "Zu wenig AP.";
            else
                msg = items_wield(w, unit, cursor) ? "In die Hand genommen."
                                                   : "Zu wenig AP.";
            sound_play(SND_CONFIRM);
        } else
            continue;
        sound_play(SND_MENU);
        inventory_draw(w, unit, cursor, msg);
    }
}
