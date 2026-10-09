/*
 * Agon frontend (M2: testland with turns).
 *
 *   loc              play: move the active unit, Tab/Space unit choice,
 *                    Shift+E ends the turn (with confirmation), ESC quits
 *   loc --dump       additionally write map + view hash to loc.log per frame
 *   loc --bench      measure full and partial redraw times -> loc.log
 *   loc --keytest    keyboard spike: show/log every key event (issue #3)
 *   loc --free-round1  lifting of the round 1 movement lock (PM 7) for
 *                    scripted emulator runs
 *   loc --fly        own flyers start airborne (demo; the ISO '<' key is
 *                    not sendable to the emulator, #3)
 */
#include <agon/keyboard.h>
#include <agon/vdp.h>
#include <agon/mos.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "../core/events.h"
#include "../core/populate.h"
#include "../core/effect.h"
#include "../core/ai.h"
#include "../core/area.h"
#include "../core/ride.h"
#include "../core/save.h"
#include "sound.h"
#include "umfont.h"

static void log_push(const char *line);   /* message ring (M4j) */
#include "../core/brew.h"
#include "../core/chord.h"
#include "../core/combat.h"
#include "../core/colors.h"
#include "../core/game.h"
#include "../core/gen/data.h"
#include "../core/items.h"
#include "../core/lexicon.h"
#include "../core/names.h"
#include "../core/sight.h"
#include "../core/turn.h"
#include "../core/tutorial.h"
#include "../core/wizard.h"
#include "screens.h"
#include "../core/view.h"
#include "../core/world.h"
#include "fx.h"
#include "input.h"
#include "keytest.h"
#include "log.h"
#include "mapfile.h"
#include "music.h"
#include "render.h"

#define MAP_SCENARIO "maps/many_coloured_land.map"   /* scenario 1 (GDD 9.1) */
#define MAP_TESTLAND "maps/testland.map"      /* dev map (ADR 0008) */
#define MAP_HOUSE "maps/wizard_house.map"
#define MAP_TUTORIAL "maps/tutorial.map"      /* guided tutorial (M5) */
#define SCN_TUTORIAL "scenarios/tutorial.scn"
#define TURN_SEED 42    /* fixed: emulator runs replay like the selftest */
#define ANIM_CS 40     /* candle flicker period in centiseconds */
#define BLINK_CS 30    /* cursor blink period (Amiga: flashing cursor) */
#define WINDOW_CS 4    /* arrow chord window 40 ms (GDD 5.2, ADR 0007; D82: was 80) */
#define DELAY_CS 35    /* held key: first repeat after 350 ms */
#define REPEAT_CS 20   /* then one step per 200 ms */

static World world;
static Turns turns;
static Game game;                  /* portal and victory points (M3e) */
static Sight p1_sight;             /* hidden map of the human player */
static bool cursor_on = true;
static bool confirm_end = false;   /* Shift+E asks before ending the turn */
static bool quit_ask = false;      /* Esc asks before leaving the game (B3) */
static bool look_mode = false;     /* x: examine any field (GDD 5.1) */
static bool overlay_open;          /* big map / log / help / context */
static bool overlay_map;           /* ... and it is the big map (D83: blinking figure) */
static bool pickup_menu;           /* g: choose what to pick up */
static bool overlay_is_context;    /* the context menu replays keys */
static bool replay_valid;          /* letter to act after menu close */
static char replay_ascii;
static uint8_t replay_vkey;
static int16_t look_x, look_y;
static bool spell_list = false;    /* c: pick a spell (GDD 5.1) */
static bool cast_menu = false;     /* c, step 1: choose spells or summons */
static bool summon_list = false;   /* spell_list filtered on summons */
static uint8_t cast_letters;       /* a.. letters consumed by the list */
static bool targeting = false;     /* aiming (Enter casts/throws/fires) */
static bool game_ended = false;    /* final score shown, only Esc left */
static bool end_pending = false;   /* outcome decided: show the end screen */
typedef enum { TA_SPELL, TA_THROW, TA_FIRE } TargetKind;
static TargetKind target_kind;
static uint8_t target_spell;
static int16_t target_x, target_y;
static bool target_air;               /* spells: CAST-A (true) or CAST-G (F8) */
static Spellbook books[OWN_NEUTRAL];   /* starting books until M3g */
static AiProfile ai_profiles[OWN_NEUTRAL];   /* the AI wizard of the scenario (K10.2) */
static Lexicon lex;                    /* discoveries, kept in lexicon.dat */
static Tutorial tut;                   /* guided tutorial engine (M5) */
static bool tutorial_on;               /* the tutorial scenario is running */
static bool tutorial_wanted;           /* chosen in the menu */

/* The unit the player acts with; start_phase guarantees one of the phase
 * owner's units is active. */
static uint8_t active(void)
{
    if (turns.active < world.unit_count)
        return turns.active;
    return 0;
}

/* ---------- warnings that wait for the next action (D69, D73) ---------- */

static char alert_text[40];            /* bottom line until the player acts */
static uint8_t alert_colour;
static bool alert_on;
static uint8_t seen_ids[32];           /* foreign units in view, bit per id */
#define HEARD_MAX 4
static uint8_t heard_n;                /* where the last noises came from (D69) */
static int16_t heard_x[HEARD_MAX], heard_y[HEARD_MAX];
#define HEAR_FIELDS 16                 /* how far an own figure hears (D69) */

static void alert_set(uint8_t colour, const char *text)
{
    snprintf(alert_text, sizeof alert_text, "%s", text);
    alert_colour = colour;
    alert_on = true;
}

/* A new game: nothing seen or heard yet. */
static void alerts_reset(void)
{
    alert_on = false;
    heard_n = 0;
    memset(seen_ids, 0, sizeof seen_ids);
}

/* "im Nordosten" for a step (dx, dy) on the map (y grows to the south). */
static const char *compass(int16_t dx, int16_t dy)
{
    static const char *const DIR[8] = {
        "im Norden", "im Nordosten", "im Osten", "im Suedosten",
        "im Sueden", "im Suedwesten", "im Westen", "im Nordwesten"};
    int16_t ax = dx < 0 ? (int16_t)-dx : dx, ay = dy < 0 ? (int16_t)-dy : dy;
    if (ax == 0 && ay == 0)
        return "ganz nah";
    if (ax > 2 * ay)
        return DIR[dx > 0 ? 2 : 6];
    if (ay > 2 * ax)
        return DIR[dy > 0 ? 4 : 0];
    if (dx > 0)
        return DIR[dy > 0 ? 3 : 1];
    return DIR[dy > 0 ? 5 : 7];
}

/* Directions are told from the player's wizard (else the active unit). */
static void p1_anchor(int16_t *x, int16_t *y)
{
    uint8_t i;
    for (i = 0; i < world.unit_count; i++)
        if (world.units[i].owner == OWN_P1 &&
            ride_actor_kind(&world.units[i]) == CR_WIZARD) {
            *x = world.units[i].x;
            *y = world.units[i].y;
            return;
        }
    *x = world.units[active()].x;
    *y = world.units[active()].y;
}

/* Targeting cursor colour (GDD 5.1): yellow ground, blue air, red when
   out of range or without a line of sight. A spell shows the aimed
   height (CAST-G/A, F8), a shot or throw the unit standing there. */
static uint8_t target_cursor_colour(void)
{
    const Unit *u = &world.units[active()];
    int16_t dx = (int16_t)(target_x - u->x), dy = (int16_t)(target_y - u->y);
    if (world.wrap) {
        if (dx > world.w / 2) dx = (int16_t)(dx - world.w);
        if (dx < -world.w / 2) dx = (int16_t)(dx + world.w);
        if (dy > world.h / 2) dy = (int16_t)(dy - world.h);
        if (dy < -world.h / 2) dy = (int16_t)(dy + world.h);
    }
    if (target_kind == TA_SPELL) {
        if (!spell_in_range(&world, u, target_spell,
                            books[OWN_P1].level[target_spell], target_x, target_y))
            return CURSOR_RED;
    } else if (target_kind == TA_FIRE) {
        if (world_range(&world, u->x, u->y, target_x, target_y) >
            items_fire_range(&world, active()))
            return CURSOR_RED;
    } else {
        uint8_t wt = u->in_use != NO_ITEM && u->in_use < u->item_count
                         ? OBJECTS[u->items[u->in_use]].weight : 1;
        if (world_range(&world, u->x, u->y, target_x, target_y) >
            items_throw_range(&world, active(), wt))
            return CURSOR_RED;
    }
    (void)dx; (void)dy;
    if (target_kind == TA_SPELL) {
        if (!spell_line_clear(&world, u, target_x, target_y, target_air))
            return CURSOR_RED;
        return target_air ? CURSOR_BLUE : CURSOR_YELLOW;
    }
    if (!sight_has_spell_los(&world, u->x, u->y, target_x, target_y))
        return CURSOR_RED;
    if (world_unit_at(&world, target_x, target_y, UL_AIR) != NO_UNIT)
        return CURSOR_BLUE;
    return CURSOR_YELLOW;
}

static void place_cursor(void)
{
    if (targeting) {
        render_cursor((int16_t)(target_x - view_origin_x()),
                      (int16_t)(target_y - view_origin_y()),
                      target_cursor_colour(), cursor_on);
        return;
    }
    if (look_mode) {
        render_cursor((int16_t)(look_x - view_origin_x()),
                      (int16_t)(look_y - view_origin_y()), CURSOR_WHITE, cursor_on);
        return;
    }
    {
        const Unit *u = &world.units[active()];
        render_cursor((int16_t)(u->x - view_origin_x()), (int16_t)(u->y - view_origin_y()),
                      (u->flags & UF_FLYING) ? CURSOR_BLUE : CURSOR_GREEN, cursor_on);
    }
}

/* The step as a gliding sprite (ADR 0012): the map is drawn without the
 * unit, its tile slides from the old field to the new one, then the
 * normal frame() shows it again. Riders keep the plain jump. */
static void update_sight(void);

static void glide(uint8_t id, int16_t old_x, int16_t old_y)
{
    uint8_t u = world_find_unit(&world, id);
    int16_t ovx, ovy, nvx, nvy;
    const Unit *un;
    if (u == NO_UNIT || !fx_glide_on)
        return;
    un = &world.units[u];
    if (ride_rider_kind(un) < CR_COUNT)
        return;
    /* D82: glide first, over the window as it is. Sight and the scrolled
     * redraw (together ~100+ ms) used to run BEFORE the animation and were
     * the biggest part of the wait between key and movement; now only the
     * two fields of the step are redrawn, the sprite starts at once and the
     * rest follows after it. */
    world_delta(&world, view_origin_x(), view_origin_y(), old_x, old_y, &ovx, &ovy);
    world_delta(&world, view_origin_x(), view_origin_y(), un->x, un->y, &nvx, &nvy);
    if (ovx >= 0 && ovx < VIEW_W && ovy >= 0 && ovy < VIEW_H &&
        nvx >= 0 && nvx < VIEW_W && nvy >= 0 && nvy < VIEW_H) {
        view_hide_unit(id);
        view_update(&world);
        render_fields();
        render_cursor(0, 0, CURSOR_GREEN, false);
        fx_glide((uint16_t)(CREATURE_TILE[un->kind] + un->owner), ovx, ovy, nvx, nvy);
        view_hide_unit(NO_UNIT);
        update_sight();
        view_follow(&world, un->x, un->y);
        return;
    }
    update_sight();
    view_follow(&world, un->x, un->y);
    world_delta(&world, view_origin_x(), view_origin_y(), old_x, old_y, &ovx, &ovy);
    world_delta(&world, view_origin_x(), view_origin_y(), un->x, un->y, &nvx, &nvy);
    view_hide_unit(id);
    view_update(&world);
    render_fields();
    render_cursor(0, 0, CURSOR_GREEN, false);
    fx_glide((uint16_t)(CREATURE_TILE[un->kind] + un->owner), ovx, ovy, nvx, nvy);
    view_hide_unit(NO_UNIT);
}

static void frame(bool dump);
static void panel_keys(void);
static void game_redraw(bool dump);

/* ---------- picking up: own field and neighbours ---------- */

#define PICK_MAX 18
typedef struct { uint8_t x, y; uint16_t tile; } PickEntry;
static PickEntry picks[PICK_MAX];
static uint8_t pick_n;

static const char *pick_where(int16_t dx, int16_t dy)
{
    static const char *const NAMES[9] = {"Nordwest", "Nord", "Nordost",
                                         "West", "hier", "Ost",
                                         "Suedwest", "Sued", "Suedost"};
    return NAMES[(dy + 1) * 3 + (dx + 1)];
}

/* Everything the active unit can reach and sees: its field first. */
static void pick_gather(void)
{
    const Unit *u = &world.units[active()];
    uint8_t i, pass;
    pick_n = 0;
    for (pass = 0; pass < 2; pass++)
        for (i = 0; i < world.object_count && pick_n < PICK_MAX; i++) {
            const Object *o = &world.objects[i];
            bool here = o->x == u->x && o->y == u->y;
            if ((pass == 0) != here)
                continue;
            if (world_distance(&world, u->x, u->y, o->x, o->y) > 1 ||
                !sight_object_visible(&p1_sight, &world, o->x, o->y) ||
                items_kind_of_tile(o->tile) == NO_ITEM)
                continue;
            picks[pick_n].x = o->x;
            picks[pick_n].y = o->y;
            picks[pick_n].tile = o->tile;
            pick_n++;
        }
}

static void draw_pick_menu(void)
{
    const Unit *u = &world.units[active()];
    char buf[40];
    uint8_t i;
    render_menu_clear();
    render_heading(16, 2, C_BRIGHT_YELLOW, "Aufheben");
    for (i = 0; i < pick_n; i++) {
        int16_t dx, dy;
        world_delta(&world, u->x, u->y, picks[i].x, picks[i].y, &dx, &dy);
        snprintf(buf, sizeof buf, "%c %-14.14s %s", 'a' + i,
                 OBJECTS[items_kind_of_tile(picks[i].tile)].name,
                 pick_where(dx, dy));
        render_menu_text(1, (uint8_t)(3 + i), C_BRIGHT_WHITE, buf);
    }
    render_menu_text(1, 23, C_BRIGHT_WHITE, "Leertaste: alles");
    render_menu_text(1, 25, C_GREY, "Buchstabe nimmt, Esc zu.");
}

/* Pick one entry (by position and tile: indices move after a pick). */
static bool pick_entry(uint8_t k)
{
    uint8_t i;
    for (i = 0; i < world.object_count; i++)
        if (world.objects[i].x == picks[k].x && world.objects[i].y == picks[k].y &&
            world.objects[i].tile == picks[k].tile) {
            uint8_t kind = items_kind_of_tile(picks[k].tile);
            if (!items_pick_up_object(&world, active(), i))
                return false;
            if (kind != NO_ITEM)
                lexicon_see_object(&lex, kind);   /* discovery */
            return true;
        }
    return false;
}

/* g: one object on the own field goes straight into the pack, several
 * (or some next door) open the choice. */
static void pick_start(bool dump)
{
    pick_gather();
    if (pick_n == 0) {
        render_message(1, C_BRIGHT_RED, "Nichts aufzuheben.");
        frame(dump);
        return;
    }
    if (pick_n == 1 && picks[0].x == world.units[active()].x &&
        picks[0].y == world.units[active()].y) {
        if (pick_entry(0)) {
            sound_play(SND_PICKUP);
            render_message(1, C_BRIGHT_GREEN, "Aufgehoben.");
        } else
            render_message(1, C_BRIGHT_RED, "Zu schwer, kein Platz oder AP.");
        frame(dump);
        return;
    }
    pickup_menu = true;
    draw_pick_menu();
}

/* A key in the choice: letter = one, Space = all, Esc = close. */
static void pick_key(const struct keyboard_event_t *e, bool dump)
{
    uint8_t got = 0, k;
    if (e->vkey == VK_ESC) {
        pickup_menu = false;
        game_redraw(dump);
        return;
    }
    if (e->vkey == VK_SPACE) {
        for (k = 0; k < pick_n; k++)
            if (pick_entry(k))
                got++;
    } else if (e->ascii >= 'a' && e->ascii < 'a' + pick_n) {
        if (pick_entry((uint8_t)(e->ascii - 'a')))
            got++;
    } else {
        return;
    }
    pickup_menu = false;
    if (got) {
        sound_play(SND_PICKUP);
        render_message(1, C_BRIGHT_GREEN, got > 1 ? "Alles aufgehoben, was ging."
                                                   : "Aufgehoben.");
    } else
        render_message(1, C_BRIGHT_RED, "Zu schwer, kein Platz oder AP.");
    game_redraw(dump);
}

/* Red messages are refusals: they get the error buzz (render.c hook). */
static void error_sound(void)
{
    sound_play(SND_ERROR);
}

/* Message line 0: whose unit is active (handover M2c). */
static void show_status(void)
{
    char buf[48];
    snprintf(buf, sizeof buf, "Runde %u - %s: %s", turns.round,
             name_owner(turns.phase), name_unit(&world.units[active()]));
    render_message(0, C_BRIGHT_WHITE, buf);
}

static void frame(bool dump)
{
    if (tutorial_on) {                    /* step conditions, then hint (M5) */
        if (tutorial_update(&tut, &world, &game) >= TUT_DONE)
            tutorial_on = false;          /* everything shown */
    }
    if (look_mode || targeting) {       /* free cursor over the map */
        int16_t cx = targeting ? target_x : look_x;
        int16_t cy = targeting ? target_y : look_y;
        char buf[24];
        /* Roofs lift for the active figure's eyes, not for the cursor (D41). */
        view_set_roof_viewer(world.units[active()].x, world.units[active()].y);
        view_set_active_unit(world.units[active()].id);
        view_follow(&world, cx, cy);
        view_update(&world);
        render_fields();
        cursor_on = true;
        place_cursor();
        render_panel_at(&world, &p1_sight, cx, cy);
        describe_field(&world, &p1_sight, cx, cy, buf, sizeof buf);
        render_message(1, C_BRIGHT_CYAN, buf);
        if (targeting)
            render_message(2, C_GREY, target_kind == TA_SPELL
                               ? (target_air ? "Ziel LUFT  < Luft > Boden  Enter, Esc"
                                             : "Ziel BODEN  < Luft > Boden  Enter, Esc")
                               : "Enter wirkt, Esc bricht ab.");
        if (tutorial_on)
            render_message(2, C_BRIGHT_CYAN, tutorial_hint_line(tut.step));
        if (dump)
            log_frame(&world, view_hash());
        return;
    }
    {
        const Unit *u = &world.units[active()];
        view_set_roof_viewer(u->x, u->y);
        view_set_active_unit(u->id);
        view_follow(&world, u->x, u->y);
        view_update(&world);
        render_fields();
        cursor_on = true;
        place_cursor();
        render_panel(&world, active());
        render_status_tag(auto_end_mode == AUTO_ON ? "<auto>" : "<man>");
        panel_keys();
        show_status();
        if (alert_on)                      /* D69/D73: stays until an action */
            render_message(tutorial_on ? 1 : 2, alert_colour, alert_text);
        if (tutorial_on)
            render_message(2, C_BRIGHT_CYAN, tutorial_hint_line(tut.step));
        if (dump)
            log_frame(&world, view_hash());
    }
    fx_drain_play(&world, &p1_sight);    /* swings, hits, deaths (M5c) */
}

/* Back from an overlay or a full-screen page: the panel and the message
 * lines only repaint their own cells, so everything else is blanked
 * first - otherwise text of the page survives in the gaps. */
static void game_redraw(bool dump)
{
    render_screen_clear();
    view_invalidate();
    render_messages_redraw();
    frame(dump);
}

/* Sight changes with every own move and at the round boundary (enemy
 * movement enters or leaves view); recomputing is cheap enough to do
 * exactly then, not per frame. */
/* Where an enemy wizard was last actually seen (C9). Hidden movement stays
 * intact: this only remembers what the player's own units caught sight of,
 * so the map marker is memory, not knowledge the player has not earned. */
static int16_t foe_wiz_x = -1, foe_wiz_y;
static uint16_t foe_wiz_round;

static void update_sight(void)
{
    sight_compute(&world, &p1_sight);
    if (game.eye_rounds > 0)
        sight_add_eye(&p1_sight, &world, game.eye_x, game.eye_y, game.eye_range);
    lexicon_watch(&lex, &world, &p1_sight);   /* discoveries (M5) */
    {
        uint8_t i;
        for (i = 0; i < world.unit_count; i++) {
            const Unit *u = &world.units[i];
            if (ride_actor_kind(u) != CR_WIZARD || u->owner == OWN_P1 ||
                (u->flags & UF_INVISIBLE))
                continue;
            if (sight_unit_visible(&p1_sight, &world, u)) {
                foe_wiz_x = u->x;
                foe_wiz_y = u->y;
                foe_wiz_round = turns.round;
            }
        }
    }
    {   /* D73: a foreign creature comes into view - warn until the next action */
        uint8_t now_ids[32], i, newcomer = NO_UNIT, extra = 0;
        memset(now_ids, 0, sizeof now_ids);
        for (i = 0; i < world.unit_count; i++) {
            const Unit *u = &world.units[i];
            if (u->owner == OWN_P1 || (u->flags & UF_INVISIBLE) ||
                !sight_unit_visible(&p1_sight, &world, u))
                continue;
            now_ids[u->id >> 3] |= (uint8_t)(1u << (u->id & 7));
            if (seen_ids[u->id >> 3] & (1u << (u->id & 7)))
                continue;                  /* in view already */
            if (newcomer == NO_UNIT)
                newcomer = i;
            else
                extra++;
        }
        memcpy(seen_ids, now_ids, sizeof seen_ids);
        if (newcomer != NO_UNIT) {
            char line[40];
            int16_t ax, ay, dx, dy;
            p1_anchor(&ax, &ay);
            world_delta(&world, ax, ay, world.units[newcomer].x,
                        world.units[newcomer].y, &dx, &dy);
            if (extra)
                snprintf(line, sizeof line, "%s %s! (+%u)",
                         name_unit(&world.units[newcomer]), compass(dx, dy), extra);
            else
                snprintf(line, sizeof line, "%s %s!",
                         name_unit(&world.units[newcomer]), compass(dx, dy));
            alert_set(C_BRIGHT_RED, line);
        }
    }
}

/* D69: what the player's figures heard since his last phase - fights,
 * spells, deaths within HEAR_FIELDS that none of them saw. The loudest
 * (death, then fight, then spell), nearest one becomes the bottom line
 * unless a sighting already took it; up to HEARD_MAX go on the big map. */
static void report_noises(void)
{
    static const char *const WHAT[3] = {"Kampflaerm", "Magie knistert", "Todesschrei"};
    static const uint8_t LOUD[3] = {1, 0, 2};   /* NOISE_FIGHT, _SPELL, _DEATH */
    uint8_t k, best = 0xFF, best_d = 0xFF, heard = 0;
    int16_t ax, ay;
    p1_anchor(&ax, &ay);
    heard_n = 0;
    for (k = 0; k < world.noise_n; k++) {
        const Noise *n = &world.noises[k];
        uint8_t i, d;
        bool near = false;
        if (sight_visible(&p1_sight, &world, n->x, n->y))
            continue;                      /* seen, not just heard */
        for (i = 0; i < world.unit_count && !near; i++)
            near = world.units[i].owner == OWN_P1 &&
                   world_distance(&world, world.units[i].x, world.units[i].y,
                                  n->x, n->y) <= HEAR_FIELDS;
        if (!near)
            continue;
        heard++;
        if (heard_n < HEARD_MAX) {         /* roughly: a 3x3 block on the map */
            heard_x[heard_n] = (int16_t)(n->x - n->x % 3);
            heard_y[heard_n] = (int16_t)(n->y - n->y % 3);
            heard_n++;
        }
        d = (uint8_t)world_distance(&world, ax, ay, n->x, n->y);
        if (best == 0xFF || LOUD[n->kind] > LOUD[world.noises[best].kind] ||
            (LOUD[n->kind] == LOUD[world.noises[best].kind] && d < best_d)) {
            best = k;
            best_d = d;
        }
    }
    if (best != 0xFF && !alert_on) {
        char line[40];
        int16_t dx, dy;
        const Noise *n = &world.noises[best];
        world_delta(&world, ax, ay, n->x, n->y, &dx, &dy);
        snprintf(line, sizeof line, heard > 1 ? "%s %s, %u Felder +%u"
                                              : "%s %s, %u Felder",
                 WHAT[n->kind], compass(dx, dy), best_d, (unsigned)(heard - 1));
        alert_set(C_BRIGHT_YELLOW, line);
    }
    world_noise_clear(&world);             /* the next listening period */
}

/* After any action that may kill: credit the logged kills (M3e) and
 * find the active unit again - deaths reorder the unit list. */
static void settle(void)
{
    game_credit_kills(&game, &world);
    turn_revalidate(&turns, &world);
    if (!game_ended && game_outcome(&game, &world, OWN_P1) != OUT_RUNNING)
        end_pending = true;              /* escaped or fallen (M5a) */
}

/* Round hook for turn_end_phase: the portal opens on its round, also
 * in rounds the AI plays on its own. */
static void on_round(Turns *t, World *w, void *ctx)
{
    area_round_end(w, &t->rng);          /* area effects tick (M4d) */
    (void)w;
    game_new_round((Game *)ctx, t->round);
}

/* After an AI phase (or the independents' steps): show what happened
 * there (M5c) - the event ring carries swings, hits and deaths. */
/* ---------- the others' phases: phase screen and sounds ---------- */

static bool phase_screen_on;           /* the map is hidden right now */
static bool fight_shown;               /* a visible fight wants a moment (D72) */
static uint8_t snap_owner;
static uint8_t snap_n, snap_id[MAX_UNITS], snap_x[MAX_UNITS], snap_y[MAX_UNITS];

/* C8: the phase screen is for what you cannot see. Does the moving side
 * have anything in view right now? Then the map stays up and its turn is
 * played out in the open instead - watching a creature you can plainly see
 * being moved behind a curtain was the complaint. */
static bool owner_in_view(const World *w, uint8_t owner)
{
    uint8_t i;
    for (i = 0; i < w->unit_count; i++)
        if (w->units[i].owner == owner && !(w->units[i].flags & UF_INVISIBLE) &&
            sight_unit_visible(&p1_sight, w, &w->units[i]))
            return true;
    return false;
}

static void on_phase(Turns *t, World *w, uint8_t owner, void *ctx)
{
    const char *names[OWN_NEUTRAL];
    uint16_t vp[OWN_NEUTRAL];
    uint8_t i, n = 0, o;
    (void)ctx;
    if (owner_in_view(w, owner)) {       /* visible: no curtain (C8, D45) */
        char line[40];
        if (phase_screen_on) {           /* the curtain still covers the map:
                                            only dirty fields would repaint
                                            over it - rebuild the whole map */
            phase_screen_on = false;
            game_redraw(false);          /* on_phase is never wired in dumps */
        }
        snprintf(line, sizeof line, "%s ist am Zug.",
                 owner == OWN_NEUTRAL ? "Die Unabhaengigen" : name_owner(owner));
        render_message(0, C_BRIGHT_CYAN, line);
        return;
    }
    for (o = OWN_P1; o < OWN_NEUTRAL; o++) {
        bool present = o == OWN_P1;
        for (i = 0; i < w->unit_count && !present; i++)
            present = w->units[i].owner == o &&
                      ride_actor_kind(&w->units[i]) == CR_WIZARD;
        if (!present && game.vp[o] == 0)
            continue;
        names[n] = o == OWN_P1 ? wizard_slots[0].name : name_owner(o);
        vp[n] = game.vp[o];
        n++;
    }
    snap_owner = owner;                   /* who moved: footsteps later */
    snap_n = 0;
    for (i = 0; i < w->unit_count; i++)
        if (w->units[i].owner == owner) {
            snap_id[snap_n] = w->units[i].id;
            snap_x[snap_n] = w->units[i].x;
            snap_y[snap_n] = w->units[i].y;
            snap_n++;
        }
    if (fight_shown) {                    /* let the player see the attack */
        fight_shown = false;
        fx_pause(120);
    }
    phase_screen_on = true;
    screen_phase(owner == OWN_NEUTRAL ? "Unabhaengige" : name_owner(owner),
                 t->round, n, names, vp);
    fx_pause(turn_humans_present(t, w) ? 35 : 5);   /* shorter (D72) */
}

static void on_ai_events(Turns *t, World *w, void *ctx)
{
    (void)ctx;
    if (phase_screen_on) {                /* unseen: only listen */
        uint8_t i, moved = 0;
        for (i = 0; i < snap_n; i++) {
            uint8_t u = world_find_unit(w, snap_id[i]);
            if (u != NO_UNIT && (w->units[u].x != snap_x[i] ||
                                 w->units[u].y != snap_y[i]))
                moved++;
        }
        if (moved > 4)
            moved = 4;
        for (i = 0; i < moved; i++) {     /* footsteps of whoever walked */
            sound_play(SND_STEP);
            fx_pause(8);
        }
        fx_drain_sounds();
        if (turn_humans_present(t, w))
            fx_pause(12);
        return;
    }
    view_update(w);                      /* their moves, before the show */
    render_fields();
    if (fx_drain_play(w, &p1_sight))
        fight_shown = true;
}

/* Throw or fire at the aimed field (direction = first step towards it). */
static void throw_or_fire(bool dump)
{
    const Unit *u = &world.units[active()];
    int16_t dx = (int16_t)(target_x - u->x), dy = (int16_t)(target_y - u->y);
    int8_t sx, sy;
    if (dx > 0) sx = 1; else if (dx < 0) sx = -1; else sx = 0;
    if (dy > 0) sy = 1; else if (dy < 0) sy = -1; else sy = 0;
    if (target_kind == TA_THROW) {
        if (brew_throw_vial(&world, &turns.rng, active(), sx, sy) ||
            items_throw(&world, &turns.rng, active(), sx, sy)) {
            render_message(1, C_BRIGHT_YELLOW, "Geworfen!");
        }
        else
            render_message(1, C_BRIGHT_RED, "Nichts zu werfen.");
        settle();
    } else {
        uint8_t dmg = 0;
        if (items_fire(&world, &turns.rng, active(), target_x, target_y, &dmg)) {
            if (dmg)
                render_message(1, C_BRIGHT_YELLOW, "Schuss trifft!");
            else
                render_message(1, C_GREY, "Schuss daneben.");
            settle();
        } else {
            render_message(1, C_BRIGHT_RED, "Kein Ziel in Reichweite.");
        }
    }
    update_sight();
    frame(dump);
}

/* Cast the aimed spell at (target_x, target_y); messages on the outcome. */
static void cast_targeted(bool dump)
{
    SpellShot shot;
    char msg[48];
    uint8_t wiz = active();
    bool ok;
    uint8_t count_before = world.unit_count;

    if (target_spell == SP_MAGIC_BOLT || target_spell == SP_MAGIC_LIGHTNING) {
        if (target_spell == SP_MAGIC_LIGHTNING)
            ok = spell_lightning(&world, &books[OWN_P1], wiz, target_x, target_y, target_air,
                                 &turns.rng, &shot);
        else
            ok = spell_bolt(&world, &books[OWN_P1], wiz, target_spell, target_x,
                            target_y, target_air, &turns.rng, &shot);
        if (!ok) {
            render_message(1, C_BRIGHT_RED, "Ausser Reichweite oder Sicht.");
            return;
        }
        if (tutorial_on)
            tutorial_notify(&tut, TUT_SPELL);   /* bolt / lightning cast */
    } else {
        CastResult cr = spell_apply(&world, &books[OWN_P1], wiz, target_spell,
                                    target_x, target_y, target_air, &turns.rng, &shot);
        if (cr == CAST_REJECTED) {
            render_message(1, C_BRIGHT_RED, "Ausser Reichweite oder Sicht.");
            return;
        }
        if (cr == CAST_BAD_TERRAIN) {
            render_message(1, C_BRIGHT_RED, "Das Ziel nimmt das nicht an.");
            return;
        }
        if (tutorial_on)
            tutorial_notify(&tut, TUT_SPELL);   /* the spell took hold */
        if (cr == CAST_NO_RES) {
            render_message(1, C_GREY, "Das Ziel widersteht.");
            settle();
            update_sight();
            frame(dump);
            return;
        }
        if (target_spell == SP_MAGIC_FIRE || target_spell == SP_GOOEY_BLOB ||
            target_spell == SP_TANGLE_VINE || target_spell == SP_FLOOD) {
            AreaKind kind = target_spell == SP_MAGIC_FIRE ? AREA_FIRE
                          : target_spell == SP_GOOEY_BLOB ? AREA_BLOB
                          : target_spell == SP_TANGLE_VINE ? AREA_VINE
                          : AREA_FLOOD;
            /* range, terrain dice and payment: spell_apply */
            if (!shot.hit) {
                render_message(1, C_GREY, "Nichts faengt an.");
                frame(dump);
                return;
            }
            render_message(1, C_BRIGHT_MAGENTA,
                           kind == AREA_FIRE ? "Es brennt!"
                           : kind == AREA_BLOB ? "Klebriger Brei!"
                           : kind == AREA_VINE ? "Ranken wachsen!"
                           : "Die Flut steigt!");
            update_sight();
            frame(dump);
            return;
        }
        if (target_spell == SP_MAGIC_EYE) {
            game.eye_x = target_x;      /* reveal from there (GDD 7.2) */
            game.eye_y = target_y;
            game.eye_rounds = 1;
            game.eye_range = shot.eye_range;
            render_message(1, C_BRIGHT_CYAN, "Auge eroeffnet.");
            settle();
            update_sight();
            frame(dump);
            return;
        }
        if (target_spell == SP_MAGIC_SHIELD) {
            render_message(1, C_BRIGHT_CYAN, "Schild aktiv.");
            settle();
            update_sight();
            frame(dump);
            return;
        }
        if (target_spell == SP_TELEPORT) {
            render_message(1, C_BRIGHT_CYAN, "Teleportiert!");
            update_sight();
            frame(dump);
            return;
        }
        if (target_spell == SP_SUBVERSION) {
            render_message(1, C_BRIGHT_YELLOW, "Die Kreatur wechselt die Seite!");
            update_sight();
            frame(dump);
            return;
        }
        if (target_spell == SP_CURSE) {
            render_message(1, C_BRIGHT_YELLOW, "Toedliche Wunde!");
            frame(dump);
            return;
        }
        if (target_spell == SP_MAGIC_ATTACK) {
            snprintf(msg, sizeof msg, "Magie trifft %u Kreaturen.",
                     shot.splash_hits);
            render_message(1, C_BRIGHT_YELLOW, msg);
            settle();
            update_sight();
            frame(dump);
            return;
        }
        if (target_spell == SP_ENCHANT) {
            render_message(1, C_BRIGHT_CYAN, "Waffen verzaubert.");
            frame(dump);
            return;
        }
        return;
    }
    if (shot.hit) {
        snprintf(msg, sizeof msg, "Zauber trifft: %u Schaden.", shot.damage);
    } else
        snprintf(msg, sizeof msg, "Zauber verpufft.");
    render_message(1, shot.hit ? C_BRIGHT_YELLOW : C_GREY, msg);
    if (shot.terrain_smashed)
        render_message(2, C_BRIGHT_YELLOW, "Blitz schlaegt das Terrain ein!");
    if (world.unit_count < count_before)
        render_message(2, C_BRIGHT_RED, "Mindestens eine Kreatur stirbt.");
    settle();
    update_sight();
    frame(dump);
}

#define LOG_RING 20
static char log_ring[LOG_RING][40];
static uint8_t log_head;

static void log_push(const char *line)
{
    strncpy(log_ring[log_head], line, sizeof log_ring[0]);
    log_ring[log_head][sizeof log_ring[0] - 1] = 0;
    log_head = (uint8_t)((log_head + 1) % LOG_RING);
}

/* D83: the log holds every event except walking. Fights, spells and deaths
 * come from the core's event ring (what the player may see), everything
 * else from the message lines: good news and findings, no refusals, no
 * walking (those lines are empty or grey) and nothing the events already
 * said. */
static const char *whose(uint8_t owner)
{
    return owner == OWN_P1 ? "Dein " : owner == OWN_P2 ? "Feind " : "";
}

static void log_event(const GameEvent *e)
{
    char msg[40];
    if (e->owner != OWN_P1 && e->type != EV_SMASH &&
        !sight_visible(&p1_sight, &world, e->x, e->y))
        return;                          /* happened in the fog */
    switch (e->type) {
    case EV_HIT:
        snprintf(msg, sizeof msg, "%s%s -%u", whose(e->owner), CREATURES[e->kind].name, e->a);
        break;
    case EV_WOUND:
        snprintf(msg, sizeof msg, "%s%s: Wunde!", whose(e->owner), CREATURES[e->kind].name);
        break;
    case EV_MISS:
        snprintf(msg, sizeof msg, "%s%s verfehlt", whose(e->owner), CREATURES[e->kind].name);
        break;
    case EV_DEATH:
        snprintf(msg, sizeof msg, "%s%s %s", whose(e->owner), CREATURES[e->kind].name,
                 e->a ? "verblutet" : "stirbt");
        break;
    case EV_SPELL:
        snprintf(msg, sizeof msg, "%s: %s", e->owner == OWN_P1 ? "Du" : "Gegner",
                 SPELLS[e->kind].name);
        break;
    case EV_SMASH:
        snprintf(msg, sizeof msg, "%s zerstoert", name_feature(e->kind));
        break;
    default:
        return;                          /* swings and projectiles say nothing new */
    }
    log_push(msg);
}

static void log_message(uint8_t line, uint8_t colour, const char *text)
{
    static char last[40];
    if (colour != C_BRIGHT_GREEN && colour != C_BRIGHT_YELLOW &&
        colour != C_BRIGHT_CYAN && colour != C_BRIGHT_MAGENTA)
        return;
    if (line == 0 && strncmp(text, "Runde", 5) == 0)
        return;                          /* the turn banner */
    if (!strncmp(text, "Treffer", 7) || !strncmp(text, "Zauber trifft", 13) ||
        !strncmp(text, "Toedliche", 9) || strstr(text, "stirbt"))
        return;                          /* the events said it */
    if (strncmp(text, last, sizeof last - 1) == 0)
        return;                          /* the same line again */
    strncpy(last, text, sizeof last - 1);
    last[sizeof last - 1] = 0;
    log_push(text);
}

/* Move the active unit in direction mask m (chord.h); messages on failure. */
/* `a` + direction (GDD 5.1, C2): close an open door, lock a closed one
 * (carried key), unlock a locked one, open a chest. */
static bool apply_pending;

static void do_apply(int8_t dx, int8_t dy, bool dump)
{
    uint8_t me = active();
    int16_t x = (int16_t)(world.units[me].x + dx);
    int16_t y = (int16_t)(world.units[me].y + dy);
    uint8_t fe = world_feature(&world, x, y);
    if (!turn_may_move(&turns)) {
        render_message(1, C_BRIGHT_RED, "Runde 1: nur Zaubern (PM 7).");
        return;
    }
    switch (fe) {
    case FE_DOOR_OPEN:
        if (world_close_door(&world, me, x, y)) {
            sound_play(SND_DOOR);
            render_message(1, C_BRIGHT_GREEN, "Tuer geschlossen.");
        } else if (world_unit_at(&world, x, y, UL_GROUND) != NO_UNIT ||
                   world_unit_at(&world, x, y, UL_AIR) != NO_UNIT)
            render_message(1, C_BRIGHT_RED, "Jemand steht in der Tuer.");
        else
            render_message(1, C_BRIGHT_RED, "Zu wenig AP oder keine Haende.");
        break;
    case FE_DOOR_CLOSED:
        if (world_lock_door(&world, me, x, y)) {
            sound_play(SND_DOOR);
            render_message(1, C_BRIGHT_GREEN, "Tuer abgeschlossen.");
        } else if (!world_has_key(&world, me))
            render_message(1, C_BRIGHT_RED, "Kein Schluessel zum Abschliessen.");
        else
            render_message(1, C_BRIGHT_RED, "Zu wenig AP oder keine Haende.");
        break;
    case FE_DOOR_LOCKED:
        if (world_unlock_door(&world, me, x, y)) {
            sound_play(SND_DOOR);
            render_message(1, C_BRIGHT_GREEN, "Tuer aufgeschlossen.");
        } else if (!world_has_key(&world, me))
            render_message(1, C_BRIGHT_RED, "Abgeschlossen: Schluessel oder Gewalt.");
        else
            render_message(1, C_BRIGHT_RED, "Zu wenig AP oder keine Haende.");
        break;
    case FE_CHEST:
    case FE_CHEST_FREE:
        if (items_open_chest(&world, &turns.rng, me, x, y)) {
            sound_play(SND_CHEST);
            render_message(1, C_BRIGHT_YELLOW, "Truhe geoeffnet!");
        } else
            render_message(1, C_BRIGHT_RED, "Truhe laesst sich nicht oeffnen.");
        break;
    default:
        render_message(1, C_BRIGHT_RED, "Dort gibt es nichts zu benutzen.");
        break;
    }
    update_sight();
    frame(dump);
}

static void step(uint8_t m, bool dump)
{
    int8_t dx, dy;
    if (!chord_to_step(m, &dx, &dy))
        return;
    if (apply_pending) {
        apply_pending = false;
        do_apply(dx, dy, dump);
        return;
    }
    if (look_mode || targeting) {       /* free cursor, no costs */
        int16_t *cx = targeting ? &target_x : &look_x;
        int16_t *cy = targeting ? &target_y : &look_y;
        *cx = (int16_t)(*cx + dx);
        *cy = (int16_t)(*cy + dy);
        if (!world_wrap(&world, cx, cy)) {
            *cx = (int16_t)(*cx - dx);
            *cy = (int16_t)(*cy - dy);
        }
        frame(dump);
        return;
    }
    if (!turn_may_move(&turns)) {
        render_message(1, C_BRIGHT_RED, "Runde 1: nur Zaubern (PM 7).");
    } else {
        char msg[48];
        uint8_t mover_id = world.units[active()].id;
        int16_t old_x = world.units[active()].x, old_y = world.units[active()].y;
        if (!world_move_unit(&world, active(), dx, dy))
            goto bump;                     /* not moved: classify the bump */
        sound_play(SND_STEP);
        if (!dump)
            glide(mover_id, old_x, old_y);
        if (game_try_enter_portal(&game, &world, active())) {
            sound_play(SND_PORTAL);
            snprintf(msg, sizeof msg, "Gerettet! Zauberer-1: %u VP.",
                     game.vp[OWN_P1]);
            render_message(0, C_BRIGHT_MAGENTA, msg);
            settle();
        } else
            render_message(1, C_GREY, "");
        update_sight();
        frame(dump);
        return;
    }
bump:
    {
        int16_t nx = (int16_t)(world.units[active()].x + dx);
        int16_t ny = (int16_t)(world.units[active()].y + dy);
        uint8_t other;
        switch (world_bump_kind(&world, active(), dx, dy)) {
        case BUMP_DOOR:
            if (world_open_door(&world, active(), nx, ny)) {
                sound_play(SND_DOOR);
                render_message(1, C_BRIGHT_GREEN, "Tuer geoeffnet.");
                update_sight();        /* the open door changes lines of sight */
            } else if (!(CREATURES[ride_actor_kind(&world.units[active()])].flags & CF_USE)) {
                render_message(1, C_BRIGHT_RED, "Keine Haende fuer die Tuer.");
            } else {
                render_message(1, C_BRIGHT_RED, "Zu wenig AP fuer die Tuer.");
            }
            frame(dump);
            return;
        case BUMP_NO_AP:
            render_message(1, C_BRIGHT_RED, "Zu wenig AP - Leertaste/Tab weiter.");
            return;
        case BUMP_BOUND:
            render_message(1, C_BRIGHT_RED, "Gebunden: Gegner nebenan - kaempfen.");
            return;
        case BUMP_HELD: {              /* blob and vine: tear through (K8.4) */
            bool torn = false;
            uint8_t hit = combat_terrain(&world, &turns.rng, active(), nx, ny, &torn);
            if (!hit)
                render_message(1, C_BRIGHT_RED, "Brei oder Ranken: zu schwer zu zerreissen.");
            else if (torn)
                render_message(1, C_BRIGHT_YELLOW, "Zerrissen!");
            else
                render_message(1, C_GREY, "Es haelt noch.");
            settle();
            update_sight();
            frame(dump);
            return;
        }
        case BUMP_UNIT: {
            CombatResult r;
            char msg[48], name[16], aname[16];
            uint8_t att = active();
            other = world_unit_at(&world, nx, ny,
                                  (world.units[att].flags & UF_FLYING) ? UL_AIR : UL_GROUND);
            if (other == NO_UNIT || world.units[other].owner == OWN_P1) {
                render_message(1, C_BRIGHT_RED, "Da geht es nicht weiter.");
                return;
            }
            if (!world_can_pay(&world, att, ACT_MELEE)) {
                snprintf(msg, sizeof msg, "Zu wenig AP/Ausdauer: Angriff kostet %u.",
                         ACTIONS[ACT_MELEE].ap);
                render_message(1, C_BRIGHT_RED, msg);
                return;
            }
            snprintf(name, sizeof name, "%s", name_unit(&world.units[other]));
            snprintf(aname, sizeof aname, "%s", name_unit(&world.units[att]));
            if (!combat_melee(&world, &turns.rng, att, other, &r)) {
                render_message(1, C_BRIGHT_RED, "Angriff nicht moeglich.");
                return;
            }
            if (r.died)
                snprintf(msg, sizeof msg, "%s stirbt!", name);
            else if (r.wound)
                snprintf(msg, sizeof msg, "Treffer: %u. Toedliche Wunde!",
                         r.damage);
            else if (r.hit)
                snprintf(msg, sizeof msg, "Treffer: %u Schaden.", r.damage);
            else
                snprintf(msg, sizeof msg, "Verfehlt.");
            render_message(1, r.hit || r.died ? C_BRIGHT_YELLOW : C_GREY, msg);
            if (r.returned) {
                if (r.attacker_died) {
                    snprintf(msg, sizeof msg, "Rueckschlag toetet %s!", aname);
                } else if (r.return_hit) {
                    snprintf(msg, sizeof msg, "Rueckschlag: %u Schaden.",
                             r.return_damage);
                } else {
                    snprintf(msg, sizeof msg, "Rueckschlag: daneben.");
                }
                render_message(2, r.attacker_died || r.return_hit ? C_BRIGHT_RED
                               : C_GREY, msg);
            }
            settle();
            update_sight();
            frame(dump);
            return;
        }
        case BUMP_TERRAIN: {
            bool destroyed;
            char msg[48];
            if (world_feature(&world, nx, ny) == FE_DOOR_LOCKED &&
                world_unlock_door(&world, active(), nx, ny)) {
                sound_play(SND_DOOR);
                render_message(1, C_BRIGHT_GREEN, "Tuer aufgeschlossen.");
                frame(dump);
                return;
            }
            if (world_feature(&world, nx, ny) == FE_CHEST ||
                world_feature(&world, nx, ny) == FE_CHEST_FREE) {
                if (items_open_chest(&world, &turns.rng, active(), nx, ny)) {
                    sound_play(SND_CHEST);
                    render_message(1, C_BRIGHT_YELLOW, "Truhe geoeffnet!");
                    update_sight();
                } else {
                    render_message(1, C_BRIGHT_RED, "Truhe laesst sich nicht oeffnen.");
                }
                frame(dump);
                return;
            }
            {
            uint8_t dmg = combat_terrain(&world, &turns.rng, active(), nx, ny, &destroyed);
            if (dmg == 0) {
                if (world_blocks(&world, nx, ny) &&
                    FEATURE_TOUGH[world_feature(&world, nx, ny)] == 0)
                    render_message(1, C_BRIGHT_RED, "Unzerstoerbar.");
                else if (world_blocks(&world, nx, ny))
                    render_message(1, C_BRIGHT_RED, "Zu schwach oder zu wenig AP/Ausdauer.");
                else
                    render_message(1, C_BRIGHT_RED, "Da geht es nicht weiter.");
            } else if (destroyed) {
                snprintf(msg, sizeof msg, "%s zerstoert!", name_feature(world_feature(&world, nx, ny)));
                render_message(1, C_BRIGHT_YELLOW, msg);
                update_sight();            /* rubble changes lines of sight */
            } else {
                snprintf(msg, sizeof msg, "%u Schaden.", dmg);
                render_message(1, C_GREY, msg);
            }
            }
            frame(dump);
            return;
        }
        default:
            render_message(1, C_BRIGHT_RED, "Da geht es nicht weiter.");
            return;
        }
    }
}

/* Times N full redraws and N single-field redraws (candle flicker). */
/* Bench results also belong on the USB console. log_line() alone writes
 * them into loc.log, which only reaches the card when the game is closed,
 * and Esc cannot be sent over USB (AGON-QUIRKS H5) - so half the numbers
 * used to be unreachable without someone at the keyboard. Collected here
 * and printed at the end, so the printing never lands inside a
 * measured interval. */
#define BENCH_LINES 8
static char bench_buf[BENCH_LINES][48];
static uint8_t bench_n;

static void bench_line(const char *s)
{
    log_line(s);
    if (bench_n < BENCH_LINES)
        snprintf(bench_buf[bench_n++], sizeof bench_buf[0], "%s", s);
}

static void bench_dump(void)
{
    uint8_t i;
    for (i = 0; i < bench_n; i++) {
        mos_putstring(bench_buf[i]);
        putch(13);
        putch(10);
    }
}

static void bench(void)
{
    char buf[64];
    uint32_t t0, full_cs, part_cs;
    uint8_t i, fields = 0;
    const uint8_t n = 10;

    t0 = getsysvar_time();
    for (i = 0; i < n; i++) {
        view_invalidate();
        view_update(&world);
        fields = render_fields();
    }
    full_cs = getsysvar_time() - t0;

    t0 = getsysvar_time();
    for (i = 0; i < n; i++) {
        view_animate(i);
        render_fields();
    }
    part_cs = getsysvar_time() - t0;

    t0 = getsysvar_time();
    for (i = 0; i < n; i++)
        render_cursor(3, 4, CURSOR_GREEN, (i & 1) != 0);
    snprintf(buf, sizeof buf, "BENCH cursor blink: %lu ms",
             (unsigned long)((getsysvar_time() - t0) * 10 / n));
    bench_line(buf);

    t0 = getsysvar_time();
    for (i = 0; i < n; i++)
        view_update(&world);                 /* compose only, nothing changes */
    snprintf(buf, sizeof buf, "BENCH compose 81 fields: %lu ms",
             (unsigned long)((getsysvar_time() - t0) * 10 / n));
    bench_line(buf);
    render_message(2, C_BRIGHT_YELLOW, buf);

    t0 = getsysvar_time();
    for (i = 0; i < n; i++)                  /* line of sight, both p1 units */
        sight_compute(&world, &p1_sight);
    snprintf(buf, sizeof buf, "BENCH sight compute: %lu ms",
             (unsigned long)((getsysvar_time() - t0) * 10 / n));
    bench_line(buf);

    {   /* four areas over their life cycle (M4d: target < 500 ms) */
        uint8_t k2;
        Rng brng;
        area_reset();
        area_set(&world, AREA_FIRE, 4, OWN_P1, 5, 20);
        area_set(&world, AREA_BLOB, 4, OWN_P2, 13, 20);
        area_set(&world, AREA_VINE, 4, OWN_NEUTRAL, 25, 20);
        area_set(&world, AREA_FLOOD, 4, OWN_P2, 30, 20);
        rng_seed(&brng, 4);
        t0 = getsysvar_time();
        for (k2 = 0; k2 < 20; k2++)
            area_round_end(&world, &brng);
        snprintf(buf, sizeof buf, "BENCH area tick x20: %lu ms",
                 (unsigned long)((getsysvar_time() - t0) * 10));
        bench_line(buf);
        area_reset();
    }

    {   /* one full AI wizard phase (M4h budget: <= 2000 ms) */
        uint32_t t1;
        Turns bt;
        Game bg;
        Spellbook bbooks[OWN_NEUTRAL];
        AiCtx bctx;
        memset(bbooks, 0, sizeof bbooks);
        bbooks[OWN_P2].level[SP_GOBLIN] = 2;
        game_init(&bg, -1, -1, 1, 1, &turns.rng);
        bctx.books = bbooks;
        bctx.game = &bg;
        memset(&bt, 0, sizeof bt);
        bt.phase = OWN_P2;
        bt.rng = turns.rng;
        t1 = getsysvar_time();
        ai_wizard_phase(&bt, &world, &bctx);
        snprintf(buf, sizeof buf, "BENCH ai phase: %lu ms",
                 (unsigned long)((getsysvar_time() - t1) * 10));
        bench_line(buf);
    }

    snprintf(buf, sizeof buf, "BENCH full %u fields: %lu ms/frame",
             fields, (unsigned long)(full_cs * 10 / n));
    bench_line(buf);
    render_message(0, C_BRIGHT_YELLOW, buf);
    snprintf(buf, sizeof buf, "BENCH candles only: %lu ms/frame",
             (unsigned long)(part_cs * 10 / n));
    bench_line(buf);
    render_message(1, C_BRIGHT_YELLOW, buf);
    bench_dump();                /* every number to the USB console */
}

/* ---------- save game (GDD 2.3, M4i) ---------- */

#define LOADS_LIMIT 5   /* GDD 2.3; F8 lets the setup switch it off */
static uint8_t loads_left = LOADS_LIMIT;
static bool loads_unlimited = false;   /* F8: setup toggle */
static char saved_map[32];             /* map of the stored savegame */
static bool save_loaded;              /* the menu restored the savegame */

/* Bounded copy that also tolerates src == dst. */
static void copy_name(char *dst, size_t cap, const char *src)
{
    size_t n = strlen(src);
    if (n >= cap)
        n = cap - 1;
    memmove(dst, src, n);
    dst[n] = 0;
}

static void save_fill(SaveGame *sg)
{
    memset(sg, 0, sizeof *sg);
    sg->world = world;
    sg->turns = turns;
    sg->game = game;
    memcpy(sg->books, books, sizeof books);
    sg->loads_left = loads_unlimited ? 0xFF : loads_left;
    copy_name(sg->world.save_map, sizeof sg->world.save_map, saved_map);
    memcpy(sg->explored, p1_sight.explored, sizeof sg->explored);
    sg->area_count = area_export(sg->areas, SAVE_AREAS);
}

static void save_apply(const SaveGame *sg)
{
    world = sg->world;
    turns = sg->turns;
    game = sg->game;
    memcpy(books, sg->books, sizeof books);
    loads_unlimited = sg->loads_left == 0xFF;     /* the savegame decides */
    loads_left = loads_unlimited ? LOADS_LIMIT : sg->loads_left;
    copy_name(saved_map, sizeof saved_map, sg->world.save_map);
    area_import(sg->areas, sg->area_count);
    sight_init(&p1_sight, OWN_P1);
    memcpy(p1_sight.explored, sg->explored, sizeof sg->explored);
    view_set_portal(game.portal_open ? game.portal_x : -1, game.portal_y);
    view_invalidate();
}

static bool save_to_sd(void)
{
    uint8_t *buf;
    SaveGame *sg = screens_borrow_save(&buf);
    uint16_t len;
    save_fill(sg);
    len = save_serialize(sg, buf, SAVE_BUF_SIZE);
    return len && savegame_write(buf, len);
}

static bool load_from_sd(void)
{
    uint8_t *buf;
    SaveGame *sg = screens_borrow_save(&buf);
    uint16_t len = savegame_read(buf, SAVE_BUF_SIZE);
    if (!len || !save_deserialize(sg, buf, len))
        return false;
    if (!save_may_load(sg))
        return false;                    /* no charges left (GDD 2.3) */
    save_apply(sg);             /* 0xFF = unlimited stays */
    if (!loads_unlimited)
        loads_left--;                    /* this load uses a charge */
    return true;
}

/* ---------- main menu (GDD 2.3, M4f) ---------- */

static const char *const MENU_ITEMS[] = {
    "The Many Coloured Land (St. 1)",
    "Slayer's Dungeon (St. 2)",
    "Ragaril's Domain (St. 3)",
    "Zufaellige Karte",
    "Spielstand laden",
    "Zauberer entwerfen",
    "Zauberer zuruecksetzen",
    "Setup (Ton, Musik, Zufall)",
    "Hilfe",
    "Lexikon",
    "Tutorial",
    "Spiel beenden",
};
#define MENU_COUNT 12
#define MENU_SCENARIOS 3
#define MENU_RANDOM 3               /* starts one of the generated maps (D58) */
/* Scenario 1 comes in MCL_VARIANTS terrain variants (tools/gen_variants.py,
 * maps/mcl_vNN.map, D57); every new game picks one, never twice in a row.
 * MCL_VARIANTS must be a power of two and match VARIANTS in the tool. */
#define MCL_VARIANTS 16
#define MCL_PREFIX "maps/mcl_v"          /* + two digits + ".map" */
#define MCL_PREFIX_LEN 10

static const char *scenario1_map(void)
{
    static char path[MCL_PREFIX_LEN + 7];   /* "maps/mcl_v07.map" */
    static uint8_t last = 0xFF;
    uint8_t v = (uint8_t)(getsysvar_time() & (MCL_VARIANTS - 1));
    if (v == last)
        v = (uint8_t)((v + 1) & (MCL_VARIANTS - 1));
    strcpy(path, MCL_PREFIX);
    path[MCL_PREFIX_LEN] = (char)('0' + v / 10);
    path[MCL_PREFIX_LEN + 1] = (char)('0' + v % 10);
    strcpy(path + MCL_PREFIX_LEN + 2, ".map");
    if (!mapfile_exists(path))
        return "maps/many_coloured_land.map";   /* an SD package without variants */
    last = v;
    return path;
}

/* map + book set per scenario (GDD 9.1); the .scn carries the books */
static const char *menu_scenario_map(uint8_t pick)
{
    static const char *const MAPS[MENU_SCENARIOS] = {
        "maps/many_coloured_land.map",
        "maps/slayers_dungeon.map",
        "maps/ragarils_domain.map",
    };
    static const char *const SCNS[MENU_SCENARIOS] = {
        "scenarios/many_coloured_land.scn",
        "scenarios/slayers_dungeon.scn",
        "scenarios/ragarils_domain.scn",
    };
    scnfile_load(books, SCNS[pick]);
    return pick == 0 ? scenario1_map() : MAPS[pick];
}

/* ---------- overlays (GDD 5.1/11.1, M4j) ---------- */

static void draw_context_menu(void);
static void draw_big_map(void);
static void draw_log(void);
static void draw_help(void);

/* Could the active unit do this right now? The real action runs on a
 * scratch copy of the world, so the menu shows exactly what the key would
 * do (items on the field, free hands, AP, free landing field, ...). */
static bool action_possible(char key)
{
    static World scratch;                      /* the probe's own copy */
    World *trial = &scratch;
    uint8_t a = active();
    const Unit *u = &world.units[a];
    bool in_hand = u->in_use != NO_ITEM && u->in_use < u->item_count;
    uint8_t weapon = in_hand ? OBJECTS[u->items[u->in_use]].weapon : WEAPON_NONE;
    switch (key) {
    case ' ':
    case 'x':
    case 'E':
        return true;
    case 'c':                          /* from the air too (F8) */
        return ride_actor_kind(u) == CR_WIZARD &&
               world_can_pay(&world, a, ACT_CAST);
    case 'f':
        return items_can_fire(&world, a) && world_can_pay(&world, a, ACT_FIRE);
    case 't':
        return in_hand && world_can_pay(&world, a, ACT_THROW);
    default:
        break;
    }
    *trial = world;
    switch (key) {
    case '>':
        return world_land(trial, a);
    case '<':
        return world_take_off(trial, a);
    case 'b':
        return (u->flags & UF_RIDDEN) ? ride_dismount(trial, a)
                                      : ride_mount_adjacent(trial, a);
    case 'g':
        return items_pick_up(trial, a);
    case 'd':
        return items_drop(trial, a);
    case 'w':
        return u->item_count > 0 && items_cycle(trial, a);
    case 'e':
        return items_eat(trial, a);
    case 'r':
        return items_read(trial, a) != NULL;
    case 'q':
        return brew_drink_vial(trial, a) || brew_drink(trial, a);
    case 'v':
        return brew_fill(trial, a);
    default:
        return false;
    }
}

/* The keys that act right now, back to back under the bars (D63). Every
 * probe copies the world, so the row is only worked out again when the
 * unit, its AP, pack, field, the map or the round changed. */
static void panel_keys(void)
{
    static const char KEYS[] = "gdwetqvrfcb<>";
    static uint32_t last_sig = 0xFFFFFFFFUL;
    static char row[2 * sizeof KEYS];
    const Unit *u = &world.units[active()];
    uint32_t sig = (uint32_t)u->id;
    uint8_t i, n = 0;
    sig = sig * 31u + u->ap;
    sig = sig * 31u + u->item_count;
    sig = sig * 31u + u->in_use;
    sig = sig * 31u + u->flags;
    sig = sig * 31u + (uint32_t)(u->x * 64u + u->y);
    sig = sig * 31u + world.object_count;
    sig = sig * 31u + world.generation;
    sig = sig * 31u + turns.round;
    if (sig != last_sig) {
        last_sig = sig;
        char found[sizeof KEYS];
        uint8_t k = 0;
        for (i = 0; KEYS[i]; i++)
            if (action_possible(KEYS[i]))
                found[k++] = KEYS[i];
        for (i = 0; i < k; i++) {        /* spaced while they fit (13 cols) */
            if (i && k <= 7)
                row[n++] = ' ';
            row[n++] = found[i];
        }
        row[n] = 0;
    }
    render_panel_keys(row);
}

/* The overlay area is the 27 text columns (216 px) left of the stat panel:
 * every line below stays within column 26. */
static void draw_context_menu(void)
{
    static const struct {
        char key;
        const char *name;
        int8_t act;                      /* ActionId for the AP, -1 = free */
    } ITEMS[] = {
        { ' ', "Fertig", -1 },           /* shown as "_" (Space) */
        { 'g', "Aufheben", ACT_PICK_UP },
        { 'd', "Fallen lassen", ACT_DROP },
        { 'w', "Wechseln", ACT_CHANGE },
        { 'e', "Essen", ACT_EAT },
        { 'q', "Trinken", ACT_DRINK },
        { 'v', "Fuellen", ACT_FILL },
        { 'r', "Lesen", ACT_READ },
        { 't', "Werfen", ACT_THROW },
        { 'f', "Bogen feuern", ACT_FIRE },
        { 'c', "Zauber wirken", ACT_CAST },
        { 'b', "Reittier", ACT_RIDE },
        { '<', "Aufsteigen", ACT_TAKE_OFF },
        { '>', "Landen", ACT_LAND },
        { 'x', "Untersuchen", -1 },
        { 'E', "Zug beenden", -1 },
    };
    char buf[40];
    uint8_t i, row = 3;
    const Unit *u = &world.units[active()];
    overlay_map = false;
    render_menu_clear();
    render_heading(16, 2, C_BRIGHT_YELLOW, "Aktionen");
    for (i = 0; i < sizeof ITEMS / sizeof ITEMS[0]; i++) {
        const char *name = ITEMS[i].name;
        int8_t act = ITEMS[i].act;
        if (!action_possible(ITEMS[i].key))
            continue;
        if (ITEMS[i].key == 'b' && (u->flags & UF_RIDDEN)) {
            name = "Absteigen";
            act = ACT_DISMOUNT;
        }
        snprintf(buf, sizeof buf, "%c %-13.13s %2u AP",
                 ITEMS[i].key == ' ' ? '_' : ITEMS[i].key, name,
                 act < 0 ? 0 : ACTIONS[act].ap);
        render_menu_text(2, row++, C_BRIGHT_WHITE, buf);
    }
    render_menu_text(2, 24, C_GREY, "Taste wirkt, Esc zu.");
}

static void draw_log_side(uint8_t col);

static uint8_t map_cell;

/* What a field looks like on the big map (D83): walls, doors and windows
 * of the houses, paths and bridges, water, forest, grass - not just grey. */
static uint8_t map_colour(int16_t x, int16_t y)
{
    switch (world_feature(&world, x, y)) {
    case FE_WALL:
    case FE_ROCK: return C_WHITE;
    case FE_WINDOW: return C_CYAN;
    case FE_DOOR_CLOSED:
    case FE_DOOR_LOCKED: return C_BRIGHT_YELLOW;
    case FE_DOOR_OPEN: return C_RED;
    case FE_FENCE: return C_RED;
    case FE_TREE: return C_GREEN;
    default: break;
    }
    switch (world_floor(&world, x, y)) {
    case FL_WATER: return C_BLUE;
    case FL_PATH:
    case FL_BRIDGE: return C_YELLOW;
    case FL_WOOD: return C_RED;
    case FL_GRASS:
    case FL_TALL_GRASS: return C_GREEN;
    case FL_FOREST:
    case FL_MAGIC_WOOD:
    case FL_SHADOW_WOOD: return C_BRIGHT_GREEN;
    case FL_SWAMP: return C_MAGENTA;
    default: return C_GREY;
    }
}

static void map_cell_paint(int16_t x, int16_t y, uint8_t colour)
{
    int px = 2 + x * map_cell, py = 16 + y * map_cell;
    vdp_gcol(0, colour);
    vdp_filled_rectangle(px, py, px + map_cell - 1, py + map_cell - 1);
}

/* The active figure blinks on the open map (white / green, the cursor's
 * rhythm). */
static void map_blink(void)
{
    const Unit *u = &world.units[active()];
    map_cell_paint(u->x, u->y, cursor_on ? C_BRIGHT_WHITE : C_BRIGHT_GREEN);
}

static void draw_big_map(void)
{
    char head[40];
    int16_t x, y;
    uint8_t log_col;
    /* 5 px a field fits 36 fields into the overlay (216 x 190 px), the
     * 46x46 map gets 4 (D64) */
    uint8_t cell = (world.w > 36 || world.h > 36) ? 4 : 5;
    overlay_map = true;
    map_cell = cell;
    render_menu_clear();
    snprintf(head, sizeof head, "Gesamtkarte  Runde %u", turns.round);
    render_menu_text(2, 0, C_BRIGHT_YELLOW, head);
    for (y = 0; y < world.h; y++)
        for (x = 0; x < world.w; x++) {
            uint8_t colour;
            uint8_t u = world_unit_at(&world, x, y, UL_GROUND);
            if (!sight_explored(&p1_sight, &world, x, y))
                continue;                  /* unexplored stays black (GDD 3.4) */
            if (u == NO_UNIT)
                u = world_unit_at(&world, x, y, UL_AIR);
            /* enemies only where the player sees them right now */
            if (u != NO_UNIT && world.units[u].owner != OWN_P1 &&
                !sight_visible(&p1_sight, &world, x, y))
                u = NO_UNIT;
            if (u != NO_UNIT)
                colour = world.units[u].owner == OWN_P1 ? C_BRIGHT_WHITE
                         : world.units[u].owner == OWN_NEUTRAL ? C_BRIGHT_CYAN
                         : C_BRIGHT_RED;           /* D86: you / neutral / enemy */
            else if (game.portal_open && x == game.portal_x &&
                     y == game.portal_y)
                colour = C_BRIGHT_MAGENTA;
            else
                colour = map_colour(x, y);
            map_cell_paint(x, y, colour);
        }
    {   /* D69: where the last noises came from, roughly */
        uint8_t k;
        vdp_gcol(0, C_YELLOW);
        for (k = 0; k < heard_n; k++) {
            int px = 2 + heard_x[k] * cell, py = 16 + heard_y[k] * cell;
            vdp_rectangle(px, py, px + 3 * cell - 1, py + 3 * cell - 1);
        }
    }
    log_col = (uint8_t)((2 + world.w * cell + 7) / 8);   /* right at the map's edge */
    if (log_col > 27)
        log_col = 27;
    render_menu_text(2, 27, C_BRIGHT_WHITE, "Du");       /* D86: legend of the figures */
    render_menu_text(5, 27, C_BRIGHT_RED, "Feind");
    render_menu_text(11, 27, C_BRIGHT_CYAN, "Neutral");
    draw_log_side(log_col);              /* D74: what happened, beside it */
    map_blink();
    if (foe_wiz_x >= 0) {                /* C9: where he was last seen */
        int px = 2 + foe_wiz_x * cell, py = 16 + foe_wiz_y * cell;
        vdp_gcol(0, C_BRIGHT_YELLOW);
        vdp_rectangle(px - 1, py - 1, px + cell - 1, py + cell - 1);
        snprintf(head, sizeof head, "Gegner zuletzt Runde %u gesehen",
                 foe_wiz_round);
        render_menu_text(2, 26, C_BRIGHT_YELLOW, head);
    } else {
        render_menu_text(2, 26, C_GREY, "Gegnerischer Zauberer noch ungesehen.");
    }
}

static void draw_log(void)
{
    uint8_t i, idx;
    char buf[40];
    overlay_map = false;
    render_menu_clear();
    render_heading(16, 2, C_BRIGHT_YELLOW, "Nachrichten");
    for (i = 0; i < LOG_RING; i++) {
        idx = (uint8_t)((log_head + i) % LOG_RING);
        snprintf(buf, sizeof buf, "%-26.26s", log_ring[idx]);
        render_menu_text(1, (uint8_t)(3 + i), C_BRIGHT_WHITE, buf);
    }
    render_menu_text(2, 24, C_GREY, "Esc zurueck.");
}

/* D74/D83: the log beside the big map. It starts at the top, newest entry
 * first, and sits right at the map's edge (col = the first free text
 * column); a long entry wraps into up to three lines. */
static void draw_log_side(uint8_t col)
{
    uint8_t row = 1, k, width = (uint8_t)(39 - col);
    render_side_clear();
    render_menu_text(col, 0, C_BRIGHT_YELLOW, "Nachrichten");
    for (k = 0; k < LOG_RING && row < 26; k++) {
        const char *line = log_ring[(log_head + LOG_RING - 1 - k) % LOG_RING];
        uint8_t colour = k == 0 ? C_BRIGHT_WHITE : C_GREY;
        const char *rest = line;
        uint8_t shown = 0;
        while (*rest && row < 26 && shown < 3) {       /* wrap at spaces */
            uint8_t n = (uint8_t)strlen(rest), cut;
            char buf[40];
            if (n > width) {
                cut = width;
                while (cut > 0 && rest[cut] != ' ')
                    cut--;
                if (cut == 0)
                    cut = width;                   /* no space: hard cut */
            } else {
                cut = n;
            }
            memcpy(buf, rest, cut);
            buf[cut] = 0;
            render_menu_line(col, row++, colour, buf);
            rest += cut;
            while (*rest == ' ')
                rest++;
            shown++;
        }
    }
}

static void draw_help(void)
{
    overlay_map = false;
    static const char *const LINES[] = {
        "Pfeile+Akkorde  Bewegen",
        "Pos1 Ende Bild  Diagonal",
        "Tab  naechste Einheit",
        "Leertaste  Einheit fertig",
        "Shift+E  Zug beenden",
        "Shift+A  Rundenwechsel auto",
        "Enter  Aktionsmenue",
        "c Zauber  f Bogen",
        "t Werfen  g Aufheben",
        "d Fallenlassen  w Wechsel",
        "e Essen  q Trinken",
        "v Fuellen  r Lesen",
        "b Reiten  < > Fliegen",
        "x Untersuchen",
        "m Karte  l Nachrichten",
        "Esc Abbrechen/Beenden",
    };
    uint8_t i;
    render_menu_clear();
    render_heading(16, 2, C_BRIGHT_YELLOW, "Hilfe (F1)");
    for (i = 0; i < sizeof LINES / sizeof LINES[0]; i++)
        render_menu_text(1, (uint8_t)(3 + i), C_BRIGHT_WHITE, LINES[i]);
    render_menu_text(2, 24, C_GREY, "Esc zurueck.");
}

/* Menu layout: with the title picture still in the VDP its top band
 * (logo, dragon, vortex) stays visible above a framed box; without it the
 * plain text layout. menu_top is the first item row, menu_msg the row of
 * the one message line. */
static uint8_t menu_top = 5, menu_msg = 20;

/* Cursor mark of one item; moving the cursor repaints two cells instead of
 * the whole screen (a full clear + redraw flickered on the real Agon). */
/* D75: only world 1 is playable for now; worlds 2 and 3 are shown grey
 * and the cursor skips them. */
static bool menu_disabled(uint8_t item)
{
    return item == 1 || item == 2;
}

static void draw_menu_mark(uint8_t item, bool on)
{
    render_menu_text(4, (uint8_t)(menu_top + item), C_BRIGHT_WHITE, on ? ">" : " ");
}

static void menu_message(uint8_t colour, const char *text)
{
    render_menu_line(2, menu_msg, colour, text);
}

static void draw_menu(uint8_t cursor)
{
    uint8_t i;
    render_screen_clear();               /* pages before it used all 40 cols */
    if (render_title_backdrop()) {
        menu_top = 10;
        menu_msg = 22;
        render_box(2, 74, 317, 199);
    } else {
        menu_top = 5;
        menu_msg = 20;
        render_heading(16, 12, C_BRIGHT_YELLOW, "LORDS OF CHAOS");
    }
    for (i = 0; i < MENU_COUNT; i++) {
        draw_menu_mark(i, i == cursor);
        render_menu_text(6, (uint8_t)(menu_top + i),
                         menu_disabled(i) ? C_GREY : C_BRIGHT_WHITE, MENU_ITEMS[i]);
    }
    render_menu_text(2, (uint8_t)(menu_msg + 1), C_GREY, "Pfeile + Enter");
}

/* The wizard designer: the chooser first (Attribute / Zauber /
 * Kreaturen), every page edits in place - Left/Right spends and
 * refunds XP, Esc returns to the chooser, Esc there leaves. */
#define PG_ATTRS 0
#define PG_SPELLS 1
#define PG_CREATURES 2

static void designer_attrs(Wizard *w)
{
    struct keyboard_event_t e;
    static const char *const ATTRS[WA_COUNT] = {
        "Kampf", "Verteidigung", "Magieresistenz", "Konstitution", "Ausdauer"};
    uint8_t cursor = 0;
    bool running = true;
    char buf[48];
    render_screen_clear();               /* lines below overwrite in place */
    while (running) {
        uint8_t i;
        render_heading(8, 0, C_BRIGHT_YELLOW, "Attribute");
        snprintf(buf, sizeof buf, "%s  Stufe %u  XP %u", w->name, w->level,
                 w->xp);
        render_menu_line(1, 2, C_BRIGHT_WHITE, buf);
        for (i = 0; i < WA_COUNT; i++) {
            snprintf(buf, sizeof buf, "%c %-14.14s %3u  %u XP",
                     i == cursor ? '>' : ' ', ATTRS[i],
                     wizard_attr(w, (WizardAttr)i),
                     wizard_attr_cost((WizardAttr)i, wizard_attr(w, (WizardAttr)i)));
            render_menu_line(3, (uint8_t)(4 + i), C_BRIGHT_WHITE, buf);
        }
        snprintf(buf, sizeof buf, "%c %-14.14s %3u  %u XP",
                 cursor == WA_COUNT ? '>' : ' ', "Mana", w->mana_max,
                 wizard_mana_cost(w));
        render_menu_line(3, (uint8_t)(4 + WA_COUNT), C_BRIGHT_WHITE, buf);
        snprintf(buf, sizeof buf, "%c %-14.14s %3u  %u XP",
                 cursor == WA_COUNT + 1 ? '>' : ' ', "Aktionspunkte", w->ap,
                 wizard_ap_cost(w));
        render_menu_line(3, (uint8_t)(5 + WA_COUNT), C_BRIGHT_WHITE, buf);
        render_menu_line(1, 14, C_GREY, "Links/Rechts: -/+   Esc: zurueck");
        while (!input_poll(&e))
            audio_poll();
        if (!e.isdown)
            continue;
        if (e.vkey == VK_ESC) {
            sound_play(SND_BACK);
            running = false;
        } else if (e.vkey == VK_UP) {
            sound_play(SND_MENU);
            cursor = cursor ? (uint8_t)(cursor - 1) : (uint8_t)(WA_COUNT + 1);
        } else if (e.vkey == VK_DOWN) {
            sound_play(SND_MENU);
            cursor = (uint8_t)((cursor + 1) % (WA_COUNT + 2));
        } else if (e.vkey == VK_RIGHT || e.ascii == '+') {
            sound_play(SND_CONFIRM);
            if (cursor == WA_COUNT)
                wizard_mana_raise(w);
            else if (cursor == WA_COUNT + 1)
                wizard_ap_raise(w);
            else
                wizard_raise(w, (WizardAttr)cursor);
        } else if (e.vkey == VK_LEFT || e.ascii == '-') {
            sound_play(SND_BACK);
            if (cursor == WA_COUNT)
                wizard_mana_lower(w);
            else if (cursor == WA_COUNT + 1)
                wizard_ap_lower(w);
            else
                wizard_lower(w, (WizardAttr)cursor);
        }
    }
}

/* The spell list page: the whole grimoire (PG_SPELLS) or only the
 * buyable summons (PG_CREATURES). Buying with Right, refunds with
 * Left; the panel below shows creature or spell details. */
static void designer_shop(Wizard *w, uint8_t page)
{
    struct keyboard_event_t e;
    uint8_t scursor = 0, stop = 0;
    bool running = true;
    char buf[40];
    uint8_t shop[SPELL_COUNT];
    uint8_t shop_n = 0;
    {
        uint16_t i;
        for (i = 0; i < SPELL_COUNT; i++) {
            if (page == PG_CREATURES &&
                SPELLS[i].category != SPC_SUMMON)
                continue;              /* creatures page: summons only */
            if (page == PG_SPELLS &&
                SPELLS[i].category == SPC_SUMMON)
                continue;              /* spells page: no duplication */
            shop[shop_n++] = (uint8_t)i;
        }
    }
#define SHOP_ROWS 9
    render_screen_clear();               /* lines overwrite in place */
    while (running) {
        uint8_t row;
        if (scursor < stop)
            stop = scursor;
        if (scursor >= stop + SHOP_ROWS)
            stop = (uint8_t)(scursor - SHOP_ROWS + 1);
        render_heading(8, 0, C_BRIGHT_YELLOW,
                       page == PG_CREATURES ? "Kreaturen" : "Zauber");
        snprintf(buf, sizeof buf, "%s  Stufe %u  XP %u", w->name, w->level,
                 w->xp);
        render_menu_line(1, 2, C_BRIGHT_WHITE, buf);
        snprintf(buf, sizeof buf, "  %-20.20s %s Preis", "Name",
                 "Anz");   /* the level is the number of creatures per cast (K5.3) */
        render_menu_line(2, 3, C_BRIGHT_YELLOW, buf);
        for (row = 0; row < SHOP_ROWS; row++) {
            uint8_t s;
            uint16_t cost;
            if (stop + row >= shop_n) {
                render_menu_line(2, (uint8_t)(4 + row), C_GREY, "");
                continue;
            }
            s = shop[stop + row];
            cost = wizard_spell_next_cost(w, s);
            snprintf(buf, sizeof buf, "%c %-20.20s %3u %5u",
                     stop + row == scursor ? '>' : ' ', SPELLS[s].name,
                     w->book.level[s], cost);
            render_menu_line(2, (uint8_t)(4 + row),
                             cost && w->xp >= cost ? C_BRIGHT_WHITE : C_GREY,
                             buf);
        }
        render_clear_rows(13, 25);       /* the detail panel varies */
        if (shop_n) {
            uint8_t sel = shop[scursor];
            uint8_t k = SUMMON_KIND[sel];
            if (page == PG_CREATURES && k < CR_COUNT)
                lexicon_creature_panel(k, 14);
            else
                spell_panel(sel, 14);
        }
        render_menu_line(1, 27, C_GREY, "Rechts kauft, Links erstattet, Esc");
        while (!input_poll(&e))
            audio_poll();
        if (!e.isdown)
            continue;
        if (e.vkey == VK_ESC) {
            sound_play(SND_BACK);
            running = false;
        } else if (e.vkey == VK_UP) {
            sound_play(SND_MENU);
            scursor = scursor ? (uint8_t)(scursor - 1) : (uint8_t)(shop_n - 1);
        } else if (e.vkey == VK_DOWN) {
            sound_play(SND_MENU);
            scursor = (uint8_t)((scursor + 1) % shop_n);
        } else if (e.vkey == VK_RIGHT || e.ascii == '+') {
            sound_play(SND_CONFIRM);
            wizard_spell_raise(w, shop[scursor]);
        } else if (e.vkey == VK_LEFT || e.ascii == '-') {
            sound_play(SND_BACK);
            wizard_spell_lower(w, shop[scursor]);
        }
    }
#undef SHOP_ROWS
}

static void designer_loop(uint8_t slot)
{
    struct keyboard_event_t e;
    Wizard *w = &wizard_slots[slot];
    uint8_t cursor = 0;
    bool running = true, full = true;
    char buf[48];
    lexicon_texts_load();   /* creature descriptions for the shop panel */
    spells_texts_load();
    while (running) {
        static const char *const PAGES[3] = {"Attribute verteilen",
                                             "Zauber erlernen",
                                             "Kreaturen beschwoeren"};
        uint8_t i;
        if (full) {                      /* entry or back from a page */
            render_screen_clear();
            full = false;
        }
        render_heading(16, 4, C_BRIGHT_YELLOW, "Zauberer gestalten");
        snprintf(buf, sizeof buf, "%s  Stufe %u  XP %u", w->name, w->level,
                 w->xp);
        render_menu_line(2, 4, C_BRIGHT_WHITE, buf);
        for (i = 0; i < 3; i++) {
            snprintf(buf, sizeof buf, "%c %s", i == cursor ? '>' : ' ',
                     PAGES[i]);
            render_menu_line(5, (uint8_t)(7 + i * 2),
                             i == cursor ? C_BRIGHT_WHITE : C_GREY, buf);
        }
        render_menu_line(4, 22, C_GREY, "Enter waehlt, Esc verlaesst.");
        while (!input_poll(&e))
            audio_poll();
        if (!e.isdown)
            continue;
        if (e.vkey == VK_ESC) {
            sound_play(SND_BACK);
            running = false;
        } else if (e.vkey == VK_UP) {
            sound_play(SND_MENU);
            cursor = cursor ? (uint8_t)(cursor - 1) : 2;
        } else if (e.vkey == VK_DOWN) {
            sound_play(SND_MENU);
            cursor = (uint8_t)((cursor + 1) % 3);
        } else if (e.ascii == 13 || e.vkey == VK_SPACE) {
            sound_play(SND_CONFIRM);
            if (cursor == PG_ATTRS)
                designer_attrs(w);
            else
                designer_shop(w, cursor);
            full = true;
        }
    }
}

/* The menu: returns the chosen map path or NULL to quit. Slot 0 is the
 * player wizard (loaded from SD, else stock). */
/* Setup panel (GDD 2.2, F8/F9): roll the random wizard and the load
 * limit toggle. The original has a single random wizard (K3.2), so there
 * is no strength to choose (F15). */
static void designer_setup_loop(void)
{
    struct keyboard_event_t e;
    char buf[40];
    bool running = true;
    render_screen_clear();               /* lines overwrite in place */
    while (running) {
        render_heading(16, 8, C_BRIGHT_YELLOW, "Setup");
        render_menu_line(2, 6, C_BRIGHT_WHITE, "Zufalls-Zauberer neu wuerfeln (Z)");
        snprintf(buf, sizeof buf, "5-Ladungen-Regel: %s  (L)",
                 loads_unlimited ? "aus" : "an");
        render_menu_line(2, 8, C_BRIGHT_WHITE, buf);
        snprintf(buf, sizeof buf, "Musik: %s  (M)", music_on ? "an" : "aus");
        render_menu_line(2, 10, C_BRIGHT_WHITE, buf);
        snprintf(buf, sizeof buf, "Toneffekte: %s  (T)", sound_on ? "an" : "aus");
        render_menu_line(2, 12, C_BRIGHT_WHITE, buf);
        snprintf(buf, sizeof buf, "Gleitende Schritte: %s  (G)",
                 fx_glide_on ? "an" : "aus");
        render_menu_line(2, 14, C_BRIGHT_WHITE, buf);
        render_menu_line(2, 17, C_GREY, "Esc zurueck ins Menue.");
        while (!input_poll(&e))
            audio_poll();
        if (!e.isdown)
            continue;
        if (e.vkey == VK_ESC) {
            running = false;
        } else if (e.ascii == 'z' || e.ascii == 'Z') {
            Rng setup_rng;               /* not the game RNG (F9) */
            rng_seed(&setup_rng, getsysvar_time());
            wizard_slot_random(3, &setup_rng);
            sound_play(SND_CONFIRM);
        } else if (e.ascii == 'l' || e.ascii == 'L') {
            loads_unlimited = !loads_unlimited;
        } else if (e.ascii == 'm' || e.ascii == 'M') {
            music_on = !music_on;
            if (music_on)
                music_start("music/title.bin");
            else
                music_stop();
            sound_settings_save();
        } else if (e.ascii == 'g' || e.ascii == 'G') {
            fx_glide_on = !fx_glide_on;
            sound_settings_save();
        } else if (e.ascii == 't' || e.ascii == 'T') {
            sound_on = !sound_on;
            sound_settings_save();
            sound_play(SND_CONFIRM);      /* audible only when switched on */
        }
    }
}

static const char *menu_loop(bool *free_round1)
{
    struct keyboard_event_t e;
    uint8_t cursor = 0;
    bool running = true;
    bool confirm_reset = false;
    if (!wizards_load()) {
        uint8_t i;
        for (i = 0; i < WIZARD_SLOTS; i++)
            wizard_slot_reset(i);
    }
    bool full = true;                    /* whole screen needs painting */
    uint8_t drawn = 0;                   /* where the '>' is on screen */
    if (!music_playing())                /* back from a game: theme again */
        music_start("music/title.bin");
    while (running) {
        if (full) {
            draw_menu(cursor);
            full = false;
            drawn = cursor;
        } else if (drawn != cursor) {
            draw_menu_mark(drawn, false);
            draw_menu_mark(cursor, true);
            drawn = cursor;
        }
        for (;;) {                        /* idle: keep the music fed (M5d) */
            audio_poll();
            if (input_poll(&e))
                break;
        }
        if (!e.isdown)
            continue;
        menu_message(C_GREY, "");         /* old message line */
        if (e.vkey != VK_SPACE && e.ascii != 13)
            confirm_reset = false;       /* any other key cancels the ask */
        if (e.vkey == VK_UP) {
            sound_play(SND_MENU);
            do
                cursor = cursor ? (uint8_t)(cursor - 1) : MENU_COUNT - 1;
            while (menu_disabled(cursor));
        } else if (e.vkey == VK_DOWN) {
            sound_play(SND_MENU);
            do
                cursor = (uint8_t)((cursor + 1) % MENU_COUNT);
            while (menu_disabled(cursor));
        } else if (e.ascii == 13 || e.vkey == VK_SPACE) {
            sound_play(SND_CONFIRM);
            if (cursor <= MENU_RANDOM) {   /* 0-2 scenarios, 3 random map */
                const char *map = menu_scenario_map(cursor < MENU_SCENARIOS
                                                        ? cursor : 0);
                wizard_apply_to_world(&wizard_slots[0], &world, active());
                memcpy(&books[OWN_P1], wizard_book(&wizard_slots[0]),
                       sizeof(Spellbook));
                wizards_save();
                *free_round1 = false;
                copy_name(saved_map, sizeof saved_map, map);
                loads_left = loads_unlimited ? loads_left : LOADS_LIMIT;
                return map;
            }
            switch (cursor) {
            case 4:                       /* Spielstand laden (M4i) */
                if (load_from_sd()) {
                    *free_round1 = false;     /* the saved lock stays */
                    save_loaded = true;   /* world is already restored */
                    wizards_save();
                    return saved_map[0] ? saved_map
                                        : "maps/many_coloured_land.map";
                }
                menu_message(C_BRIGHT_RED,
                                 "Kein Spielstand / keine Ladungen.");
                continue;
            case 5:
                designer_loop(0);
                full = true;
                break;
            case 6:
                if (!confirm_reset) {   /* destructive: ask once */
                    confirm_reset = true;
                    menu_message(C_BRIGHT_RED,
                                     "Nochmal Enter loescht den Zauberer.");
                    continue;
                }
                confirm_reset = false;
                wizard_slot_reset(0);   /* stock wizard over the slot */
                wizards_save();
                full = true;
                break;
            case 7:                       /* Setup: random wizard, F8 loads */
                designer_setup_loop();
                full = true;
                break;
            case 8:                       /* Hilfe: pages from the SD (M5) */
                if (!screen_help("help/keys.hlp"))
                    menu_message(C_BRIGHT_RED,
                                     "help/keys.hlp fehlt auf der SD.");
                full = true;
                break;
            case 9:                       /* Lexikon (M5) */
                screen_lexicon(&lex);
                full = true;
                break;
            case 10: {                    /* guided tutorial (M5) */
                const char *map = MAP_TUTORIAL;
                scnfile_load(books, SCN_TUTORIAL);
                wizard_apply_to_world(&wizard_slots[0], &world, active());
                memcpy(&books[OWN_P1], wizard_book(&wizard_slots[0]),
                       sizeof(Spellbook));
                wizards_save();
                tutorial_wanted = true;
                *free_round1 = false;
                copy_name(saved_map, sizeof saved_map, map);
                loads_left = loads_unlimited ? loads_left : LOADS_LIMIT;
                return map;
            }
            default:
                return NULL;
            }
        }
    }
    return NULL;
}

/* ---------- end of the game (M5a) ---------- */

static const char *const SCEN_MAP[3] = {
    "maps/many_coloured_land.map", "maps/slayers_dungeon.map",
    "maps/ragarils_domain.map",
};
static const char *const SCEN_TITLE[3] = {
    "The Many Coloured Land", "Slayer's Dungeon", "Ragaril's Domain",
};

/* 1..3 for the campaign scenarios, 0 for test maps. */
static uint8_t scenario_number(const char *map)
{
    uint8_t i;
    if (map && strncmp(map, MCL_PREFIX, MCL_PREFIX_LEN) == 0)
        return 1;                        /* a terrain variant of scenario 1 */
    for (i = 0; map && i < 3; i++)
        if (strcmp(map, SCEN_MAP[i]) == 0)
            return (uint8_t)(i + 1);
    return 0;
}

/* Clear everything the finished game left in the frontend. */
static void reset_play_state(void)
{
    uint8_t i;
    game_ended = false;
    end_pending = false;
    confirm_end = false;
    look_mode = false;
    spell_list = false;
    pickup_menu = false;
    targeting = false;
    overlay_open = false;
    overlay_is_context = false;
    replay_valid = false;
    cursor_on = true;
    save_loaded = false;
    tutorial_on = false;
    area_reset();
    for (i = 0; i < 3; i++)              /* no stale lines in the next game */
        render_message(i, C_GREY, "");
    view_invalidate();                   /* the end screen blanked the map */
}

/* The outcome is decided: book the campaign result, show the end screen.
 * True = back to the main menu, false = quit. */
static bool end_flow(const char *map)
{
    EndInfo info;
    Wizard *w = &wizard_slots[0];
    uint8_t sc = scenario_number(map);
    uint8_t level = w->level;

    memset(&info, 0, sizeof info);
    info.name = w->name;
    info.scenario = sc ? SCEN_TITLE[sc - 1] : NULL;
    info.outcome = (game.escaped & (1u << OWN_P1)) ? OUT_WIN : OUT_LOSE;
    info.rounds = turns.round;
    info.vp = game.vp[OWN_P1];
    info.loot_vp = game.loot_vp[OWN_P1];
    info.kills = game.kills[OWN_P1];
    if (info.outcome == OUT_WIN && sc) {     /* VP -> XP, level up (GDD 9) */
        wizard_campaign_result(w, info.vp, sc);
        wizards_save();
        info.campaign = true;
        info.xp_gain = info.vp;
        info.xp_total = w->xp;
        info.level = w->level;
        info.level_up = w->level > level;
    }
    render_cursor(0, 0, CURSOR_GREEN, false);    /* sprite stays above the screen */
    lexicon_save(&lex);                     /* discoveries survive the game (M5) */
    return screen_end(&info);
}

/* Bind the tutorial engine to a freshly loaded tutorial map (M5). */
static void start_tutorial(void)
{
    tutorial_wanted = false;
    tutorial_on = true;
    if (!tutorial_hints_load("help/tutorial.hlp"))
        log_line("TUT hints missing");
    tutorial_init(&tut, &world);
    turns.round1_lock = false;   /* the tutorial teaches movement at once */
}

/* Shift+E, or Space once every unit is done: the AI plays, the round
 * hook runs (portal), the outcome is checked and the game autosaved. */
/* D72: on the first round change, ask whether rounds should end on their
 * own once every unit is done. Asked once, kept in settings.dat; A
 * switches later. Never in scripted runs (they feed keys). */
static void ask_auto_end(bool dump)
{
    struct keyboard_event_t e;
    if (dump || auto_end_mode != AUTO_UNASKED)
        return;
    render_message(1, C_BRIGHT_YELLOW, "Automatischer Rundenwechsel? (J/N)");
    render_message(2, C_GREY, "Dann endet die Runde, wenn alle fertig.");
    for (;;) {
        while (!input_poll(&e))
            audio_poll();
        if (!e.isdown)
            continue;
        if (e.ascii == 'j' || e.ascii == 'J' || e.ascii == 13) {
            auto_end_mode = AUTO_ON;
            break;
        }
        if (e.ascii == 'n' || e.ascii == 'N' || e.vkey == VK_ESC) {
            auto_end_mode = AUTO_OFF;
            break;
        }
    }
    sound_settings_save();
    render_message(2, C_GREY, "A schaltet um: <auto> / <man>.");
}

static void end_turn(bool dump, const char *map_path)
{
    confirm_end = false;
    alert_on = false;                    /* ending the turn is an action */
    ask_auto_end(dump);
    if (!turn_humans_present(&turns, &world))
        render_message(1, C_BRIGHT_YELLOW, "Die KI spielt zu Ende ...");
    turn_end_phase(&turns, &world);    /* round hook: portal */
    game_credit_kills(&game, &world);
    view_set_portal(game.portal_open ? game.portal_x : -1, game.portal_y);
    if (game_over(&game, &world) || !turn_humans_present(&turns, &world) ||
        game_outcome(&game, &world, OWN_P1) != OUT_RUNNING) {
        end_pending = true;            /* shown by the main loop */
    } else if (game.portal_open && turns.round == game.portal_round) {
        render_message(0, C_BRIGHT_MAGENTA, "Das Portal oeffnet sich!");
        sound_play(SND_PORTAL);
    } else {
        render_message(1, C_BRIGHT_GREEN, "Neue Runde.");
        sound_play(SND_ROUND);
    }
    if (!game_ended && !end_pending) { /* autosave (GDD 2.3) */
        copy_name(saved_map, sizeof saved_map, map_path);
        if (save_to_sd())
            render_message(2, C_GREY, "Gespeichert.");
    }
    update_sight();
    report_noises();                     /* D69: what was heard meanwhile */
    fight_shown = false;
    if (phase_screen_on) {               /* back from the unseen phases */
        phase_screen_on = false;
        game_redraw(dump);
    } else
        frame(dump);
}

int main(int argc, char **argv)
{
    struct keyboard_event_t e;
    bool dump = false, do_bench = false, free_round1 = false, do_fly = false;
    bool running = true;
    const char *map_path = MAP_SCENARIO;
    uint32_t next_anim, next_blink;
    uint8_t phase = 0, m, i;
    uint16_t now;
    Chord chord;

    if (argc > 1 && (strcmp(argv[1], "--endscreen") == 0 ||
                     strcmp(argv[1], "--endscreen-lose") == 0)) {
        EndInfo demo;                    /* dev: look at the end screen */
        memset(&demo, 0, sizeof demo);
        demo.name = "Zauberer";
        demo.scenario = SCEN_TITLE[0];
        demo.outcome = strcmp(argv[1], "--endscreen") == 0 ? OUT_WIN : OUT_LOSE;
        demo.rounds = 21;
        demo.vp = 143;
        demo.loot_vp = 90;
        demo.kills = 4;
        demo.campaign = demo.outcome == OUT_WIN;
        demo.xp_gain = 143;
        demo.xp_total = 183;
        demo.level = 2;
        demo.level_up = true;
        render_init();
        umfont_install();
        sound_init();
        kbuf_init(16);
        screen_end(&demo);
        kbuf_deinit();
        render_shutdown();
        return 0;
    }
    if (argc > 1 && strcmp(argv[1], "--fxdemo") == 0) {
        struct keyboard_event_t e;       /* dev: every fx tile at once */
        mapfile_load(&world, MAP_HOUSE);
        sight_init(&p1_sight, OWN_P1);
        sight_compute(&world, &p1_sight);
        view_set_sight(NULL);            /* everything visible */
        if (!render_init())
            return 1;
        umfont_install();
        kbuf_init(16);
        view_invalidate();
        view_update(&world);
        render_fields();
        fx_init();
        sound_init();
        for (i = 0; i < 12; i++) {        /* every sprite effect, a few times */
            events_reset();
            events_push(EV_PROJECTILE, 1, 6, PJ_BOLT, OWN_P1, 5, (uint8_t)-4);
            events_push(EV_HIT, 6, 2, CR_GOBLIN, OWN_P2, 12, 1);
            events_push(EV_PROJECTILE, 1, 1, PJ_ARROW, OWN_P1, 6, 2);
            events_push(EV_PROJECTILE, 7, 6, PJ_THROWN, OWN_P2, (uint8_t)-5, 0);
            events_push(EV_HIT, 2, 6, CR_GOBLIN, OWN_P2, 7, 0);
            events_push(EV_PROJECTILE, 7, 7, PJ_LIGHTNING, OWN_P2, (uint8_t)-6, (uint8_t)-5);
            events_push(EV_SPELL, 4, 4, SP_GOBLIN, OWN_P1, 0, 0);
            events_push(EV_SPELL, 5, 3, SP_TELEPORT, OWN_P1, 0, 0);
            events_push(EV_SPELL, 3, 5, SP_CURSE, OWN_P1, 0, 0);
            events_push(EV_SPELL, 4, 2, SP_MAGIC_SHIELD, OWN_P1, 0, 0);
            fx_drain_play(&world, NULL);
        }
        do {                              /* the tiles stay on screen */
            while (!input_poll(&e))
                audio_poll();
        } while (!e.isdown);
        kbuf_deinit();
        render_shutdown();
        return 0;
    }
    if (argc > 1 && (strcmp(argv[1], "--helppage") == 0 ||
                     strcmp(argv[1], "--lexicon") == 0)) {
        render_init();                    /* dev: look at the M5 screens */
        umfont_install();
        kbuf_init(16);
        if (strcmp(argv[1], "--helppage") == 0) {
            screen_help("help/keys.hlp");
        } else {
            Lexicon demo;                 /* everything seen: detail pages */
            uint16_t k;
            lexicon_init(&demo);
            for (k = 0; k < CR_COUNT; k++)
                lexicon_see_creature(&demo, (uint8_t)k);
            for (k = 0; k < OBJ_COUNT; k++)
                lexicon_see_object(&demo, (uint8_t)k);
            screen_lexicon(&demo);
        }
        kbuf_deinit();
        render_shutdown();
        return 0;
    }
    if (argc > 1 && strcmp(argv[1], "--keytest") == 0) {
        log_open(true);
        keytest_run();
        log_close();
        render_shutdown();
        return 0;
    }
    for (i = 1; i < (uint8_t)argc; i++) {
        if (strcmp(argv[i], "--dump") == 0)
            dump = true;
        else if (strcmp(argv[i], "--bench") == 0)
            do_bench = true;
        else if (strcmp(argv[i], "--house") == 0)
            map_path = MAP_HOUSE;
        else if (strcmp(argv[i], "--testland") == 0)
            map_path = MAP_TESTLAND;
        else if (strcmp(argv[i], "--tutorial") == 0) {
            map_path = MAP_TUTORIAL;      /* guided tutorial (M5) */
            tutorial_wanted = true;
        } else if (strcmp(argv[i], "--free-round1") == 0)
            free_round1 = true;
        else if (strcmp(argv[i], "--fly") == 0)
            do_fly = true;
    }

    log_open(dump || do_bench);
    log_line("BOOT");
    fx_set_enabled(!dump && !do_bench);  /* waits would eat scripted keys */
    {
        uint32_t t0 = getsysvar_time();
        bool ok = mapfile_load(&world, map_path);
        char buf[48];
        snprintf(buf, sizeof buf, "MAP %s %s in %lu ms", map_path, ok ? "loaded" : "FAILED",
                 (unsigned long)((getsysvar_time() - t0) * 10));
        log_line(buf);
        if (!ok) {
            printf("%s\r\n", buf);
            log_close();
            return 1;
        }
    }
    {   /* spellbooks from the scenario file (M4a); test maps fall back
         * to an empty book */
        bool ok = scnfile_load(books, tutorial_wanted
                                      ? SCN_TUTORIAL
                                      : "scenarios/many_coloured_land.scn");
        log_line(ok ? "SCN loaded" : "SCN missing - empty books");
    }
    brew_register_map_cauldrons(&world);
    turn_init(&turns, &world, TURN_SEED, 1u << OWN_P1);
    {
        static AiCtx ai_ctx;              /* books + game for the wizard AI */
        ai_ctx.books = books;
        ai_ctx.game = &game;
        ai_ctx.profiles = ai_profiles;
        turns.ai = ai_wizard_phase;
        turns.ai_ctx = &ai_ctx;
        turns.on_round = on_round;
        turns.on_ai = on_ai_events;
        turns.on_ai_ctx = 0;
        turns.on_phase = (dump || do_bench) ? NULL : on_phase;
        turns.round_ctx = &game;
    }
    game_init(&game, world.portal_x, world.portal_y, world.portal_rmin,
              world.portal_rmax, &turns.rng);   /* portal from the map (v5) */
    game_set_portal_span(&game, world.portal_span);
    game_set_wizard_level(&game, OWN_P1, wizard_slots[0].level);
    if (scnfile_ai(&world, ai_profiles))
        ai_profile_apply(ai_profiles, &world, &game, OWN_P2);
    game_new_round(&game, turns.round);
    view_set_portal(game.portal_open ? game.portal_x : -1, game.portal_y);
    if (free_round1)
        turns.round1_lock = false;
    if (do_fly) {                    /* the ISO key is not sendable yet (#3) */
        uint8_t k;
        for (k = 0; k < world.unit_count; k++)
            if (world.units[k].owner == OWN_P1 && world.units[k].ap_fly)
                world.units[k].flags |= UF_FLYING;
    }
    sight_init(&p1_sight, OWN_P1);
    update_sight();
    view_set_sight(&p1_sight);
    if (!render_init()) {
        log_line("ERR render_init");
        log_close();
        return 1;
    }
    umfont_install();                    /* ae/oe/ue/ss for the UI (M4j) */
    fx_init();                           /* effect sprites (ADR 0012) */
    sound_settings_load();
    if (!dump && !do_bench && !sound_init())   /* samples from the SD */
        log_line("SFX missing - waveform fallback");
    render_set_error_hook(error_sound);
    render_set_message_hook(log_message);
    fx_set_event_hook(log_event);
    kbuf_init(16);
    if (!lexicon_load(&lex))             /* discoveries from the last run */
        lexicon_init(&lex);
    if (!dump && !do_bench) {
        screen_title();                  /* picture + music; the song
                                            plays on under the menu */
    }
menu_start:
    if (!dump && !do_bench) {            /* main menu (GDD 2.3, M4f) */
        const char *chosen = menu_loop(&free_round1);
        music_stop();                    /* the game itself is quiet */
        if (!chosen) {
            lexicon_save(&lex);
            kbuf_deinit();
            render_shutdown();
            log_close();
            return 0;
        }
        if (!save_loaded) {      /* every new game reloads (fresh random world) */
            if (!mapfile_load(&world, chosen)) {
                kbuf_deinit();
                render_shutdown();
                log_close();
                return 1;
            }
            map_path = chosen;
            /* everything derived from the map must be rebuilt */
            brew_register_map_cauldrons(&world);
            /* every game differs (D35): the seed comes from the clock,
             * which ran for however long the menu was open */
            turn_init(&turns, &world, TURN_SEED ^ getsysvar_time(), 1u << OWN_P1);
            if (scenario_number(chosen)) {   /* campaign: random world */
                populate_scenario(&world, &turns.rng);
                turns.wildlife = true;
            }
            game_init(&game, world.portal_x, world.portal_y, world.portal_rmin,
                      world.portal_rmax, &turns.rng);
            game_set_portal_span(&game, world.portal_span);
            game_set_wizard_level(&game, OWN_P1, wizard_slots[0].level);
            if (scnfile_ai(&world, ai_profiles))
                ai_profile_apply(ai_profiles, &world, &game, OWN_P2);
            game_new_round(&game, turns.round);
            view_set_portal(game.portal_open ? game.portal_x : -1, game.portal_y);
            if (free_round1)
                turns.round1_lock = false;
            {
                static AiCtx ai_ctx2;
                ai_ctx2.books = books;
                ai_ctx2.game = &game;
                ai_ctx2.profiles = ai_profiles;
                turns.ai = ai_wizard_phase;
                turns.ai_ctx = &ai_ctx2;
                turns.on_round = on_round;
                turns.on_ai = on_ai_events;
                turns.on_ai_ctx = 0;
                turns.on_phase = dump ? NULL : on_phase;
                turns.round_ctx = &game;
            }
            sight_init(&p1_sight, OWN_P1);
            alerts_reset();
            update_sight();
            view_set_sight(&p1_sight);
        }
        if (!save_loaded) {
            /* the designer wizard becomes unit 0 (F5: no items, own book) */
            wizard_apply_to_world(&wizard_slots[0], &world, active());
            memcpy(&books[OWN_P1], wizard_book(&wizard_slots[0]),
                   sizeof(Spellbook));
        } else {
            /* the savegame is the state: only bind what it cannot hold */
            static AiCtx ai_ctx3;
            map_path = saved_map;
            ai_ctx3.books = books;
            ai_ctx3.game = &game;
            turns.ai = ai_wizard_phase;
            turns.ai_ctx = &ai_ctx3;
            turns.on_round = on_round;
            turns.on_ai = on_ai_events;
            turns.on_ai_ctx = 0;
            turns.on_phase = dump ? NULL : on_phase;
            turns.round_ctx = &game;
            alerts_reset();
            update_sight();
            view_set_sight(&p1_sight);
        }
    }
    if (tutorial_wanted)
        start_tutorial();
    else if (!dump && !do_bench) {       /* empty book? offer the set (M5) */
        uint16_t k, sum = 0;
        for (k = 0; k < SPELL_COUNT; k++)
            sum += wizard_slots[0].book.level[k];
        if (sum == 0) {
            struct keyboard_event_t e;
            render_screen_clear();
            render_menu_text(2, 6, C_BRIGHT_YELLOW,
                             "Dein Zauberer hat keine Zauber.");
            render_menu_text(2, 9, C_BRIGHT_WHITE,
                             "Mit Standardset starten?");
            render_menu_text(4, 12, C_BRIGHT_WHITE, "J = Ja, sinnvolles Set");
            render_menu_text(4, 13, C_BRIGHT_WHITE, "N = Nein, ganz ohne");
            for (;;) {
                while (!input_poll(&e))
                    audio_poll();
                if (!e.isdown)
                    continue;
                if (e.ascii == 'j' || e.ascii == 'J' || e.ascii == 13) {
                    wizard_apply_standard_set(&wizard_slots[0]);
                    memcpy(&books[OWN_P1], wizard_book(&wizard_slots[0]),
                           sizeof(Spellbook));
                    wizard_apply_to_world(&wizard_slots[0], &world, active());
                    render_message(1, C_BRIGHT_GREEN,
                                   "Standardset angewandt.");
                    break;
                }
                if (e.ascii == 'n' || e.ascii == 'N' || e.vkey == VK_ESC) {
                    render_message(1, C_BRIGHT_RED,
                                   "Ohne Zauber - viel Glueck!");
                    break;
                }
            }
        }
    }
    render_screen_clear();               /* menu/dialog text must not stay */
    view_invalidate();
    frame(dump);
    render_message(1, C_BRIGHT_YELLOW, tutorial_on
                   ? "Tutorial: Folge der Hinweiszeile."
                   : "Willkommen! F1 zeigt die Hilfe.");
    render_message(2, C_BRIGHT_BLUE, "Tab Einheit  Leertaste fertig  E Zugende");
    if (do_bench)
        bench();

    chord_init(&chord, WINDOW_CS, DELAY_CS, REPEAT_CS);
    view_set_idle(true);   /* creatures play their idle now and then (D53) */
    next_anim = getsysvar_time() + ANIM_CS;
    next_blink = getsysvar_time() + BLINK_CS;
    while (running) {
        /* replayed key from the context menu (M4j): the letter closes
         * the menu and acts through the normal dispatch below */
        if (replay_valid) {
            memset(&e, 0, sizeof e);
            e.isdown = 1;
            e.ascii = replay_ascii;
            e.vkey = replay_vkey;
            replay_valid = false;
            now = (uint16_t)getsysvar_time();
            goto dispatch;
        }
        /* Drain every queued key event first: drawing a step can take longer
         * than the repeat delay, and a stale "held" state would otherwise
         * repeat keys that were already released (ADR 0007). */
        /* D72: every unit done - the round ends by itself (not in scripts) */
        if (auto_end_mode == AUTO_ON && !dump && !game_ended && !end_pending &&
            !overlay_open && !spell_list && !cast_menu && !targeting &&
            !look_mode && !pickup_menu && !quit_ask && turns.phase == OWN_P1 &&
            world.unit_count && !turn_units_left(&turns, &world)) {
            render_message(1, C_BRIGHT_GREEN, "Alle fertig - die Runde endet.");
            end_turn(dump, map_path);
        }
        while (running && input_poll(&e)) {
            now = (uint16_t)getsysvar_time();
            if (game_ended && !(e.isdown && e.vkey == VK_ESC))
                continue;                        /* game over: Esc only */
dispatch:
            /* D73: the warning line stays until the player acts; looking,
             * Tab, the map, the log and help are not actions */
            if (alert_on && e.isdown && !look_mode && !targeting && !overlay_open &&
                e.vkey != VK_TAB && e.vkey != VK_ESC && e.vkey != VK_F1 &&
                e.ascii != 'x' && e.ascii != 'm' && e.ascii != 'l' &&
                e.ascii != 'i' && e.ascii != 'k' && e.ascii != 'A' && e.ascii != 13) {
                alert_on = false;
                render_message(2, C_GREY, "");
            }
            if (quit_ask && e.isdown) {          /* Esc asked, now the answer */
                if (e.ascii == 'j' || e.ascii == 'J') {
                    quit_ask = false;
                    copy_name(saved_map, sizeof saved_map, map_path);
                    save_to_sd();
                    running = false;
                } else if (e.ascii == 'b' || e.ascii == 'B') {
                    quit_ask = false;
                    running = false;
                } else if (e.ascii == 'n' || e.ascii == 'N' || e.vkey == VK_ESC) {
                    quit_ask = false;
                    render_message(1, C_GREY, "");
                    render_message(2, C_GREY, "");
                }
                continue;
            }
            if (cast_menu && e.isdown) {         /* c, step 1: what to cast */
                uint16_t k;
                uint8_t have_spells = 0, have_summons = 0, n_spells = 0,
                        n_summons = 0;
                for (k = 0; k < SPELL_COUNT; k++) {
                    if (books[OWN_P1].level[k] == 0)
                        continue;
                    if (SPELLS[k].category == SPC_SUMMON) {
                        have_summons = 1;
                        n_summons++;
                    } else {
                        have_spells = 1;
                        n_spells++;
                    }
                }
                if (e.vkey == VK_ESC) {
                    cast_menu = false;
                    game_redraw(dump);
                } else if ((e.ascii == 'z' || e.ascii == 'y') && have_spells) {
                    cast_menu = false;
                    summon_list = false;
                    spell_list = true;
                    cast_letters = (uint8_t)('a' + n_spells - 1);
                    render_list_summons = 0;
                    render_spell_list(&books[OWN_P1]);
                } else if ((e.ascii == 'b' || e.ascii == 'B') && have_summons) {
                    cast_menu = false;
                    summon_list = true;
                    spell_list = true;
                    cast_letters = (uint8_t)('a' + n_summons - 1);
                    render_list_summons = 1;
                    render_spell_list(&books[OWN_P1]);
                }
                continue;
            }
            if (pickup_menu && e.isdown) {       /* g: what to pick up */
                pick_key(&e, dump);
                continue;
            }
            if (spell_list && e.isdown) {        /* letters pick (a is WASD too) */
                if (e.vkey == VK_ESC) {
                    spell_list = false;
                    summon_list = false;
                    game_redraw(dump);
                } else if (e.ascii >= 'a' && e.ascii <= cast_letters) {
                    uint16_t pick = e.ascii - 'a';
                    uint16_t i, n = 0;
                    bool from_summons = summon_list;   /* the list shown */
                    spell_list = false;
                    summon_list = false;
                    view_invalidate();
                    for (i = 0; i < SPELL_COUNT; i++) {
                        if (books[OWN_P1].level[i] == 0)
                            continue;
                        if (from_summons !=
                            (SPELLS[i].category == SPC_SUMMON))
                            continue;          /* wrong list for this pick */
                        if (n == pick)
                            break;
                        n++;
                    }
                    if (i < SPELL_COUNT) {
                        uint8_t wiz = active();
                        if (ride_actor_kind(&world.units[wiz]) != CR_WIZARD) {
                            render_message(1, C_BRIGHT_RED, "Nur Zauberer zaubern.");
                        } else if (SPELLS[i].category == SPC_POTION) {
                            if (brew_cast(&world, &books[OWN_P1], wiz, (uint8_t)i)) {
                                if (tutorial_on)
                                    tutorial_notify(&tut, TUT_SPELL);
                                render_message(1, C_BRIGHT_GREEN,
                                               "Der Kessel brodelt.");
                            } else
                                render_message(1, C_BRIGHT_RED,
                                               "Brauen braucht Kessel und Zutat.");
                        } else if (SPELLS[i].category == SPC_SUMMON) {
                            uint8_t got = spell_summon(&world, &books[OWN_P1], wiz, (uint8_t)i, &turns.rng);
                            if (got) {
                                if (tutorial_on)
                                    tutorial_notify(&tut, TUT_SPELL);
                                render_message(1, C_BRIGHT_GREEN, "Beschworen!");
                            } else
                                render_message(1, C_BRIGHT_RED, "Kein Platz - Mana verloren.");
                            update_sight();
                        } else {
                            /* everything else aims through the cursor */
                            targeting = true;
                            target_kind = TA_SPELL;
                            target_spell = (uint8_t)i;
                            target_x = world.units[wiz].x;
                            target_y = world.units[wiz].y;
                            /* aim at the caster's own height first */
                            target_air = (world.units[wiz].flags & UF_FLYING) != 0;
                        }
                    }
                    frame(dump);
                }
                continue;
            }
            if (targeting && e.isdown && !input_arrow(e.vkey) &&
                !input_diagonal(e.vkey)) {        /* aim: Enter casts, Esc ends */
                if (e.vkey == VK_ESC) {
                    targeting = false;
                    render_message(1, C_GREY, "");
                    render_message(2, C_GREY, "");
                    frame(dump);
                } else if (target_kind == TA_SPELL &&
                           (e.ascii == '<' || e.ascii == '>')) {
                    target_air = e.ascii == '<';   /* CAST-A / CAST-G (F8) */
                    frame(dump);
                } else if (e.ascii == 13 || e.vkey == VK_SPACE) {
                    /* aiming at the caster cancels without cost (GDD 7.1)
                     * - except for spells that target the caster himself */
                    if (target_kind == TA_SPELL &&
                        target_spell != SP_MAGIC_SHIELD &&
                        target_spell != SP_ENCHANT &&
                        target_x == world.units[active()].x &&
                        target_y == world.units[active()].y) {
                        targeting = false;
                        log_line("SELF cancel");
                        render_message(1, C_GREY, "Abgebrochen.");
                        render_message(2, C_GREY, "");
                        frame(dump);
                    } else {
                        targeting = false;
                        if (target_kind == TA_SPELL)
                            cast_targeted(dump);
                        else
                            throw_or_fire(dump);
                    }
                }
                continue;
            }
            if (input_arrow(e.vkey)) {           /* movement by vkey */
                if (!spell_list) {
                    /* An open overlay swallows the step, never the key: the
                     * chord still has to see every release, or a tapped
                     * arrow stays "held" and the unit walks on by itself
                     * (AGON-QUIRKS K6). */
                    m = chord_keys(&chord, input_arrow(e.vkey), e.isdown != 0, now);
                    if (m && !overlay_open) {
                        step(m, dump);
                        chord_done(&chord, (uint16_t)getsysvar_time());
                    }
                }
                continue;
            }
            if (!e.isdown)
                continue;
            if (input_diagonal(e.vkey)) {
                if (!spell_list && !overlay_open)
                    step(input_diagonal(e.vkey), dump);
            } else if (overlay_open) {
                if (e.vkey == VK_ESC) {
                    overlay_open = false;
                    overlay_is_context = false;
                    game_redraw(dump);
                } else if (overlay_is_context && !input_arrow(e.vkey) &&
                           !input_diagonal(e.vkey) &&
                           (e.ascii || e.vkey == VK_SPACE)) {
                    /* a letter: close and act through the normal path */
                    overlay_open = false;
                    overlay_is_context = false;
                    game_redraw(dump);
                    replay_valid = true;
                    replay_ascii = e.ascii;
                    replay_vkey = e.vkey == VK_SPACE ? VK_SPACE : 0;
                } else if (e.vkey == VK_F1) {
                    if (screen_help("help/keys.hlp")) {   /* pages (M5) */
                        overlay_open = false;    /* the page replaced it */
                        overlay_is_context = false;
                        game_redraw(dump);
                    } else
                        draw_help();
                } else if (e.ascii == 'm') {
                    overlay_open = false;        /* m closes it again (B1) */
                    overlay_is_context = false;
                    game_redraw(dump);
                } else if (e.ascii == 'l') {
                    draw_log();
                }
            } else if (e.vkey == VK_F1) {
                if (screen_help("help/keys.hlp")) {       /* pages (M5) */
                    game_redraw(dump);
                } else {
                    overlay_open = true;
                    draw_help();
                }
            } else if (e.ascii == 'm') {
                overlay_open = true;
                draw_big_map();
            } else if (e.ascii == 'l') {
                overlay_open = true;
                draw_log();
            } else if (e.ascii == 'k') {           /* lexicon (M5, D87: was i) */
                screen_lexicon(&lex);
                game_redraw(dump);
            } else if (e.ascii == 'i') {           /* inventory (D77, D87) */
                confirm_end = false;
                screen_inventory(&world, active());
                update_sight();
                game_redraw(dump);
            } else if (e.ascii == 13 && !spell_list && !targeting) {
                overlay_open = true;               /* context menu (GDD 5.1) */
                overlay_is_context = true;
                draw_context_menu();
            } else if (look_mode) {                /* x: examine (GDD 5.1) */
                if (e.vkey == VK_ESC || e.ascii == 'x') {
                    look_mode = false;
                    render_message(1, C_GREY, "");
                    frame(dump);
                }
            } else if (e.ascii == 'c') {         /* cast menu (GDD 5.1, M5) */
                confirm_end = false;
                if (ride_actor_kind(&world.units[active()]) == CR_WIZARD) {
                    uint16_t k;
                    uint8_t have_spells = 0, have_summons = 0, n_spells = 0,
                            n_summons = 0;
                    for (k = 0; k < SPELL_COUNT; k++) {
                        if (books[OWN_P1].level[k] == 0)
                            continue;
                        if (SPELLS[k].category == SPC_SUMMON) {
                            have_summons = 1;
                            n_summons++;
                        } else {
                            have_spells = 1;
                            n_spells++;
                        }
                    }
                    if (!have_spells && !have_summons) {
                        render_message(1, C_BRIGHT_RED,
                                       "Keine Zauber im Buch (Designer).");
                    } else if (have_spells && have_summons) {
                        cast_menu = true;    /* choose first (M5) */
                        render_cast_menu(1, 1, n_spells, n_summons);
                    } else {
                        summon_list = have_summons != 0;
                        spell_list = true;
                        cast_letters = (uint8_t)('a' +
                            (have_summons ? n_summons : n_spells) - 1);
                        render_list_summons = have_summons != 0 ? 1 : 0;
                        render_spell_list(&books[OWN_P1]);
                    }
                } else {
                    render_message(1, C_BRIGHT_RED, "Nur Zauberer zaubern.");
                }
            } else if (e.ascii == 'a') {            /* apply: door/chest + direction */
                confirm_end = false;
                apply_pending = true;
                render_message(1, C_BRIGHT_CYAN, "Benutzen: Richtung?");
            } else if (e.ascii == 'g') {            /* pick up (choice) */
                confirm_end = false;
                pick_start(dump);
            } else if (e.ascii == 'd') {            /* drop in use */
                confirm_end = false;
                if (items_drop(&world, active()))
                    render_message(1, C_BRIGHT_GREEN, "Fallen gelassen.");
                else
                    render_message(1, C_BRIGHT_RED, "Kein Objekt in der Hand.");
                update_sight();
                frame(dump);
            } else if (e.ascii == 'e') {            /* eat in-use food */
                confirm_end = false;
                if (items_eat(&world, active())) {
                    sound_play(SND_EAT);
                    render_message(1, C_BRIGHT_GREEN, "Gegessen.");
                }
                else
                    render_message(1, C_BRIGHT_RED, "Kein Essen in der Hand.");
                frame(dump);
            } else if (e.ascii == 'r') {            /* read scroll in use */
                confirm_end = false;
                {
                    const char *txt = items_read(&world, active());
                    if (txt)
                        render_message(1, C_BRIGHT_CYAN, txt);
                    else
                        render_message(1, C_BRIGHT_RED, "Keine Schriftrolle in der Hand.");
                }
                frame(dump);
            } else if (e.ascii == 'b') {            /* board / dismount */
                confirm_end = false;
                if (world.units[active()].flags & UF_RIDDEN) {
                    if (ride_dismount(&world, active())) {
                        sound_play(SND_FLY);
                        render_message(1, C_BRIGHT_GREEN, "Abgestiegen.");
                    }
                    else
                        render_message(1, C_BRIGHT_RED, "Kein Platz zum Absteigen.");
                } else {
                    if (ride_mount_adjacent(&world, active())) {
                        sound_play(SND_FLY);
                        turn_revalidate(&turns, &world);
                        render_message(1, C_BRIGHT_GREEN, "Aufgesessen!");
                    } else
                        render_message(1, C_BRIGHT_RED,
                                       "Kein Reittier in Reichweite.");
                }
                update_sight();
                frame(dump);
            } else if (e.ascii == 'q') {            /* quaff: vial or cauldron */
                confirm_end = false;
                if (brew_drink_vial(&world, active())) {
                    sound_play(SND_DRINK);
                    render_message(1, C_BRIGHT_GREEN, "Phiole getrunken.");
                } else if (brew_drink(&world, active())) {
                    sound_play(SND_DRINK);
                    render_message(1, C_BRIGHT_GREEN, "Aus dem Kessel getrunken.");
                }
                else
                    render_message(1, C_BRIGHT_RED, "Nichts zu trinken hier.");
                frame(dump);
            } else if (e.ascii == 'v') {            /* fill the empty vial */
                confirm_end = false;
                if (brew_fill(&world, active())) {
                    sound_play(SND_DRINK);
                    render_message(1, C_BRIGHT_GREEN, "Phiole gefuellt.");
                }
                else
                    render_message(1, C_BRIGHT_RED, "Kein Kessel oder keine leere Phiole.");
                frame(dump);
            } else if (e.ascii == 'w') {            /* wield next */
                confirm_end = false;
                if (items_cycle(&world, active())) {
                    const Unit *u = &world.units[active()];
                    if (u->in_use != NO_ITEM)
                        render_message(1, C_BRIGHT_GREEN,
                                       OBJECTS[u->items[u->in_use]].name);
                    else
                        render_message(1, C_GREY, "Leere Haende.");
                } else if (!world_can_pay(&world, active(), ACT_CHANGE)) {
                    render_message(1, C_BRIGHT_RED, "Zu wenig AP.");
                } else {
                    render_message(1, C_BRIGHT_RED,
                                   "Keine Waffe zum Fuehren (Schild zaehlt getragen).");
                }
                frame(dump);
            } else if (e.ascii == 't') {            /* throw in use */
                confirm_end = false;
                if (world.units[active()].in_use == NO_ITEM)
                    render_message(1, C_BRIGHT_RED, "Kein Objekt in der Hand.");
                else {
                    targeting = true;
                    target_kind = TA_THROW;
                    target_spell = 0;
                    target_x = world.units[active()].x;
                    target_y = world.units[active()].y;
                }
                frame(dump);
            } else if (e.ascii == 'f') {            /* fire bow in use */
                confirm_end = false;
                {
                    if (!items_can_fire(&world, active()))
                        render_message(1, C_BRIGHT_RED, "Kein Bogen in der Hand.");
                    else {
                        targeting = true;
                        target_kind = TA_FIRE;
                        target_x = world.units[active()].x;
                        target_y = world.units[active()].y;
                    }
                }
                frame(dump);
            } else if (e.ascii == 'x') {
                confirm_end = false;
                look_mode = true;
                look_x = world.units[active()].x;
                look_y = world.units[active()].y;
                frame(dump);
            } else if (e.vkey == VK_TAB) {       /* next/previous own unit */
                confirm_end = false;
                turn_next_unit(&turns, &world, (e.kmod & KMOD_SHIFT) != 0);
                if (tutorial_on)
                    tutorial_notify(&tut, TUT_SWITCH);
                render_message(1, C_GREY, "");
                if (!turn_units_left(&turns, &world))
                    render_message(1, C_BRIGHT_YELLOW,
                                   "Alle fertig - Leertaste/E: Zugende.");
                frame(dump);
            } else if (e.vkey == VK_SPACE) {     /* unit finished */
                confirm_end = false;
                if (!turn_units_left(&turns, &world)) {
                    end_turn(dump, map_path);    /* all done: Space ends */
                    continue;
                }
                turn_finish_unit(&turns, &world);
                if (turn_units_left(&turns, &world))
                    render_message(1, C_GREY, "");
                else
                    render_message(1, C_BRIGHT_YELLOW,
                                   "Alle fertig - Leertaste/E: Zugende.");
                frame(dump);
            } else if (e.ascii == 'E') {         /* Shift+E: turn end at once */
                end_turn(dump, map_path);
            } else if (e.ascii == 'A') {         /* Shift+A: round change mode (D72) */
                auto_end_mode = auto_end_mode == AUTO_ON ? AUTO_OFF : AUTO_ON;
                sound_settings_save();
                render_message(1, C_BRIGHT_GREEN, auto_end_mode == AUTO_ON
                               ? "Rundenwechsel automatisch."
                               : "Rundenwechsel von Hand.");
                frame(dump);
            } else if (e.ascii == '<') {              /* take off */
                confirm_end = false;
                if (world_take_off(&world, active())) {
                    sound_play(SND_FLY);
                    render_message(1, C_BRIGHT_GREEN, "Steigt auf.");
                }
                else if (world.units[active()].flags & UF_FLYING)
                    render_message(1, C_BRIGHT_RED, "Du fliegst schon.");
                else if (world.units[active()].ap_fly == 0 &&
                         !effect_active(&world.units[active()], EFF_FLYING))
                    render_message(1, C_BRIGHT_RED, "Diese Kreatur fliegt nicht.");
                else if (world.units[active()].ap < ACTIONS[ACT_TAKE_OFF].ap)
                    render_message(1, C_BRIGHT_RED, "Zu wenig AP zum Aufsteigen.");
                else
                    render_message(1, C_BRIGHT_RED, "Da fliegt schon einer.");
                update_sight();
                frame(dump);
            } else if (e.ascii == '>') {              /* land */
                confirm_end = false;
                if (world_land(&world, active())) {
                    sound_play(SND_FLY);
                    render_message(1, C_BRIGHT_GREEN, "Landet.");
                }
                else if (!(world.units[active()].flags & UF_FLYING))
                    render_message(1, C_BRIGHT_RED, "Die Kreatur fliegt nicht.");
                else if (world.units[active()].ap < ACTIONS[ACT_LAND].ap)
                    render_message(1, C_BRIGHT_RED, "Zu wenig AP zum Landen.");
                else
                    render_message(1, C_BRIGHT_RED, "Kein Platz zum Landen.");
                update_sight();
                frame(dump);
            } else if (e.vkey == VK_ESC) {
                if (apply_pending) {
                    apply_pending = false;
                    render_message(1, C_GREY, "");
                } else if (confirm_end) {
                    confirm_end = false;
                    render_message(1, C_GREY, "");
                } else {                         /* B3: never quit on one key */
                    quit_ask = true;
                    render_message(1, C_BRIGHT_YELLOW, "Spiel wirklich beenden?");
                    render_message(2, C_GREY,
                                   "J speichern und beenden, B ohne, N weiter");
                }
            }
        }
        if (end_pending) {                       /* outcome decided (M5a) */
            end_pending = false;
            if (dump || do_bench) {              /* scripted runs: message only */
                char msg[48];
                snprintf(msg, sizeof msg,
                         "Spielende! Zauberer-1: %u VP  Zauberer-2: %u VP",
                         game.vp[OWN_P1], game.vp[OWN_P2]);
                render_message(0, C_BRIGHT_YELLOW, msg);
                render_message(1, C_GREY, "Esc beendet.");
                game_ended = true;
            } else if (end_flow(map_path)) {
                reset_play_state();
                map_path = NULL;                 /* force the map reload */
                goto menu_start;
            } else {
                running = false;
            }
        }
        audio_poll();                            /* effect step lists */
        now = (uint16_t)getsysvar_time();
        {   /* releases that arrived during an animation (fx_take_release) */
            uint8_t vk;
            while ((vk = fx_take_release()) != 0) {
                uint8_t arrow = input_arrow(vk);
                if (arrow) {
                    m = chord_keys(&chord, arrow, false, now);
                    if (m) {
                        step(m, dump);
                        chord_done(&chord, (uint16_t)getsysvar_time());
                    }
                }
            }
        }
        m = chord_poll(&chord, now);
        if (m) {
            step(m, dump);
            chord_done(&chord, (uint16_t)getsysvar_time());
        }
        if (getsysvar_time() >= next_anim) {   /* candle and water animation */
            next_anim += ANIM_CS;
            if (!overlay_open && !spell_list && !cast_menu && !pickup_menu) {
                view_animate(++phase);
                render_fields();
            }
        }
        if (getsysvar_time() >= next_blink) {  /* blinking cursor sprite */
            next_blink += BLINK_CS;
            cursor_on = !cursor_on;
            if (overlay_open && overlay_map)
                map_blink();
            if (!overlay_open && !spell_list && !cast_menu && !pickup_menu &&
                !game_ended)
                place_cursor();
        }
    }
    kbuf_deinit();

    lexicon_save(&lex);                     /* discoveries survive (M5) */
    render_shutdown();
    log_line("EXIT");
    log_close();
    return 0;
}
