/*
 * Mega Drive game loop: scenario 1 with the stock wizard, turns, movement,
 * melee, doors, chests, spells, items, mounts, flying, look mode, portal,
 * AI phases, panel and messages. A port of the parts of src/agon/main.c that
 * the platform-free core does not own; the designer, saving, lexicon,
 * tutorial, sound and effects follow.
 *
 * Pad: d-pad moves/attacks (two directions held = diagonal), C next unit,
 * A = action menu (only what is possible now, the last action preselected, so
 * A A repeats it), START tap = unit done (all done: ends the turn), START held
 * = end the turn, B = back/cancel everywhere. On a six-button pad X casts,
 * Y picks up, Z looks. While aiming or looking: d-pad moves the cursor,
 * C confirms, B cancels.
 */
#include "loc.h"

#include "ai.h"
#include "area.h"
#include "brew.h"
#include "chord.h"
#include "colors.h"
#include "combat.h"
#include "effect.h"
#include "game.h"
#include "gen/data.h"
#include "gen/maps.h"
#include "gen/maps_md.h"
#include "gen/scenarios.h"
#include "gen/title_md.h"
#include "gen/tiles.h"
#include "items.h"
#include "lexicon.h"
#include "names.h"
#include "populate.h"
#include "render_md.h"
#include "sound_md.h"
#include "ride.h"
#include "sight.h"
#include "spells.h"
#include "tutorial.h"
#include "turn.h"
#include "resources.h"
#include "ui_md.h"
#include "view.h"
#include "wizard.h"
#include "game_md.h"
#include <stdio.h>

#define WINDOW_CS 8
#define DELAY_CS 35
#define REPEAT_CS 20
#define BLINK_FRAMES 18
#define HOLD_CS 60                      /* START held this long ends the turn */
#define ANIM_FRAMES 24

static World world;
static Turns turns;
static Game game;
static Sight p1_sight;
static Spellbook books[OWN_NEUTRAL];
static AiProfile ai_profiles[OWN_NEUTRAL];
static AiCtx ai_ctx;
static bool end_pending, game_ended, confirm_end, apply_pending, cursor_on = TRUE;

typedef enum { MODE_PLAY, MODE_LOOK, MODE_TARGET } Mode;
typedef enum { TA_SPELL, TA_THROW, TA_FIRE } TargetKind;
static Mode mode;
static TargetKind target_kind;
static bool auto_end;                   /* D72: the round ends by itself when every unit is done */
static u8 target_spell;
static bool target_air;                 /* spells: CAST-A (air) or CAST-G (ground), F8 */
static s16 look_x, look_y;              /* free cursor: look mode and aiming */

/* debug counter, read through the BlastEm debugger (tools/emushot.py) */
volatile u16 g_loop;

/* Read through the BlastEm debugger by tools/e2e (symbol g_dbg): what the game is doing now. */
enum { SCREEN_BOOT, SCREEN_TITLE, SCREEN_MENU, SCREEN_PLAY, SCREEN_END };
volatile struct {
    u16 magic;                          /* 0x4C44 once the game has started */
    u16 frames;                         /* play frames so far */
    u16 screen, menus;                  /* SCREEN_*; open list menus */
    u16 round, mode, units, scenario;
    s16 x, y;                           /* the active unit */
    u16 ap, hp, mana;
    u16 flags;                          /* 1 game_ended, 2 end_pending, 4 quit_game, 8 apply_pending, 16 save valid */
    u16 msg1;                           /* hash of the last message line 1 */
    u16 hash;                           /* hash of the world (positions, hit points, items) */
    u16 acts_n;                         /* entries of the action menu built last (ids in g_acts) */
    u16 sel;                            /* selected entry of the open list menu */
    u16 items, spell;                   /* items carried by the active unit; the spell being aimed */
} g_dbg;
volatile u8 g_acts[24];
volatile u8 g_spells[SPELL_COUNT];      /* spell ids of the open spell list (e2e tests) */

static u16 clock_cs(void)
{
    return (u16)((u32)vtimer * (IS_PAL_SYSTEM ? 2 : 5) / (IS_PAL_SYSTEM ? 1 : 3));
}

/* Pad events are queued in the V-Int handler: a frame that takes longer than a
 * button tap (full repaints, AI phases) must not lose it. */
typedef struct { u16 changed, state, cs; } PadEv;
#define EVQ 32
static volatile PadEv evq[EVQ];
static volatile u8 ev_head, ev_tail;

/* V-Blank callback: the pad state read this frame, as an event when it changed. */
static u16 pad_last;                    /* pad state of the previous V-Blank */

#ifdef LOC_MD_DEMO
/* Hardware demo (EverDrive + camera): a scripted pad, so a ROM on the real console plays itself. From
 * frame t on the pad state is b, until the next row. Times are V-Blanks (60 per second on NTSC). */
#if LOC_MD_DEMO == 2                                     /* second run: continue the saved game */
static const struct { u16 t; u16 b; } DEMO[] = {
    {0, 0}, {240, BUTTON_START}, {250, 0},
    {330, BUTTON_DOWN}, {336, 0}, {346, BUTTON_DOWN}, {352, 0}, {362, BUTTON_DOWN}, {368, 0},
    {378, BUTTON_DOWN}, {384, 0}, {400, BUTTON_A}, {410, 0},     /* "Spielstand laden" */
};
#else
static const struct { u16 t; u16 b; } DEMO[] = {
    {0, 0}, {240, BUTTON_START}, {250, 0},              /* title -> menu */
    {330, BUTTON_A}, {340, 0},                          /* scenario 1 */
    {600, BUTTON_START}, {720, 0},                      /* hold: end turn (round 1 lock) */
    {1500, BUTTON_RIGHT}, {1900, 0},                    /* walk and scroll east */
    {2000, BUTTON_DOWN}, {2400, 0},                     /* ... and south */
    {2500, BUTTON_A}, {2510, 0},                        /* the action menu */
    {2600, BUTTON_DOWN}, {2610, 0}, {2650, BUTTON_B}, {2660, 0},
    {2800, BUTTON_LEFT}, {3200, 0},                     /* back west */
    {3300, BUTTON_START}, {3420, 0},                    /* hold: next turn */
};
#endif
static u16 demo_t;

static u16 demo_pad(void)
{
    u8 i;
    u16 b = 0;
    for (i = 0; i < sizeof DEMO / sizeof DEMO[0] && DEMO[i].t <= demo_t; i++)
        b = DEMO[i].b;
    demo_t++;
    return b;
}
#define READ_PAD() demo_pad()
#else
#define READ_PAD() JOY_readJoypad(JOY_1)
#endif

static void on_vblank(void)
{
    u16 pad = READ_PAD(), changed = pad ^ pad_last;
    u8 next = (ev_head + 1) & (EVQ - 1);
    sound_tick();
    if (!changed || next == ev_tail)
        return;
    evq[ev_head].changed = changed;
    evq[ev_head].state = pad;
    evq[ev_head].cs = clock_cs();
    ev_head = next;
    pad_last = pad;
}

static bool ev_pop(PadEv *e)
{
    if (ev_tail == ev_head)
        return FALSE;
    e->changed = evq[ev_tail].changed;
    e->state = evq[ev_tail].state;
    e->cs = evq[ev_tail].cs;
    ev_tail = (ev_tail + 1) & (EVQ - 1);
    return TRUE;
}

/* Button presses (not releases) since the last call. */
static u16 pad_pressed(void)
{
    PadEv e;
    u16 pressed = 0;
    while (ev_pop(&e))
        pressed |= e.changed & e.state;
    return pressed;
}

static bool save_known_valid;           /* a save was written or found */

/* ---------- scenarios ---------- */

typedef struct {
    const char *title;
    const u8 *map;
    const u16 *map_len;
    const u8 *scn;
    const u16 *scn_len;
    bool variants;                      /* scenario 1: a random terrain variant (D57) */
    bool tutorial;                      /* the guided tutorial map */
} Scenario;

#define SCENARIO_COUNT 4
#define TUTORIAL_INDEX 3
static const Scenario SCENARIOS[SCENARIO_COUNT] = {
    {"The Many Coloured Land", MAPBIN_MANY_COLOURED_LAND, &MAPBIN_MANY_COLOURED_LAND_LEN,
     SCN_MANY_COLOURED_LAND, &SCN_MANY_COLOURED_LAND_LEN, TRUE, FALSE},
    {"Slayer's Dungeon", MAPBIN_SLAYERS_DUNGEON, &MAPBIN_SLAYERS_DUNGEON_LEN,
     SCN_SLAYERS_DUNGEON, &SCN_SLAYERS_DUNGEON_LEN, FALSE, FALSE},
    {"Ragaril's Domain", MAPBIN_RAGARILS_DOMAIN, &MAPBIN_RAGARILS_DOMAIN_LEN,
     SCN_RAGARILS_DOMAIN, &SCN_RAGARILS_DOMAIN_LEN, FALSE, FALSE},
    {"Tutorial", MAPBIN_TUTORIAL, &MAPBIN_TUTORIAL_LEN, SCN_TUTORIAL, &SCN_TUTORIAL_LEN, FALSE, TRUE},
};
static const Scenario *cur_sc;          /* the running scenario */
static int16_t foe_x = -1, foe_y;          /* the enemy wizard, last seen there ... */
static u8 foe_round;                    /* ... in this round */
static Lexicon lex;                     /* what the player has seen (saved with the settings) */
static Tutorial tut;
static bool tutorial_on;
static const char *tutorial_hint(u8 step);
static bool quit_game;                  /* back to the main menu */

/* ---------- helpers ---------- */

static u8 active(void)
{
    return turns.active < world.unit_count ? turns.active : 0;
}

#define LOG_N 16
static char log_ring[LOG_N][48];
static u8 log_head, log_count;

static void log_push(const char *text)
{
    snprintf(log_ring[log_head], sizeof log_ring[0], "%s", text);
    log_head = (log_head + 1) % LOG_N;
    if (log_count < LOG_N)
        log_count++;
}

static void msg(u8 line, u8 colour, const char *text)
{
    if (line && text[0] && colour != C_GREY)
        log_push(text);
    if (colour == C_BRIGHT_RED && text[0])
        sound_play(SND_ERROR);
    if (line == 1) {
        u16 h = 0;
        const char *c;
        for (c = text; *c; c++)
            h = (u16)((h * 31u + (u8)*c) ^ (h >> 7));
        g_dbg.msg1 = h;
    }
    ui_message(line, colour, text);
}

static void frame(void);
static void frame_aim(void);
static bool save_game(void);
static void wizards_save(void);
static s8 menu_run_text3(const char *title, const char *hint, u8 start, const char *const *set);

/* ---------- what is seen and heard (D69, D73) ---------- */

#define HEARD_MAX 4
#define HEAR_FIELDS 16                  /* how far an own figure hears */
static u8 seen_ids[32];                 /* foreign units in view, bit per unit id */
static u8 heard_n;                      /* where the last noises came from: 3x3 blocks on the big map */
static s16 heard_x[HEARD_MAX], heard_y[HEARD_MAX];

static const char *compass(s16 dx, s16 dy)
{
    static const char *const DIR[8] = {"im Norden", "im Nordosten", "im Osten", "im Suedosten",
                                       "im Sueden", "im Suedwesten", "im Westen", "im Nordwesten"};
    s16 ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
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
static void p1_anchor(s16 *x, s16 *y)
{
    u8 i, a = turns.active < world.unit_count ? turns.active : 0;
    for (i = 0; i < world.unit_count; i++)
        if (world.units[i].owner == OWN_P1 && ride_actor_kind(&world.units[i]) == CR_WIZARD) {
            *x = world.units[i].x;
            *y = world.units[i].y;
            return;
        }
    *x = world.units[a].x;
    *y = world.units[a].y;
}

static void alerts_reset(void)
{
    heard_n = 0;
    memset(seen_ids, 0, sizeof seen_ids);
}

static void update_sight(void)
{
    sight_compute(&world, &p1_sight);
    if (game.eye_rounds > 0)
        sight_add_eye(&p1_sight, &world, game.eye_x, game.eye_y, game.eye_range);
    lexicon_watch(&lex, &world, &p1_sight);           /* discoveries (M5) */
    {   /* where an enemy wizard was last actually seen (C9): memory, not knowledge */
        u8 i;
        for (i = 0; i < world.unit_count; i++) {
            const Unit *u = &world.units[i];
            if (ride_actor_kind(u) != CR_WIZARD || u->owner == OWN_P1 || (u->flags & UF_INVISIBLE))
                continue;
            if (sight_unit_visible(&p1_sight, &world, u)) {
                foe_x = u->x;
                foe_y = u->y;
                foe_round = turns.round;
            }
        }
    }
    {   /* D73: a foreign creature comes into view - a warning line with its direction */
        u8 now_ids[32], i, newcomer = NO_UNIT, extra = 0;
        memset(now_ids, 0, sizeof now_ids);
        for (i = 0; i < world.unit_count; i++) {
            const Unit *u = &world.units[i];
            if (u->owner == OWN_P1 || (u->flags & UF_INVISIBLE) || !sight_unit_visible(&p1_sight, &world, u))
                continue;
            now_ids[u->id >> 3] |= (u8)(1u << (u->id & 7));
            if (seen_ids[u->id >> 3] & (1u << (u->id & 7)))
                continue;                                       /* in view already */
            if (newcomer == NO_UNIT)
                newcomer = i;
            else
                extra++;
        }
        memcpy(seen_ids, now_ids, sizeof seen_ids);
        if (newcomer != NO_UNIT) {
            char line[44];
            s16 ax, ay, dx, dy;
            p1_anchor(&ax, &ay);
            world_delta(&world, ax, ay, world.units[newcomer].x, world.units[newcomer].y, &dx, &dy);
            if (extra)
                snprintf(line, sizeof line, "%.18s %s! (+%d)", name_unit(&world.units[newcomer]), compass(dx, dy), extra);
            else
                snprintf(line, sizeof line, "%.18s %s!", name_unit(&world.units[newcomer]), compass(dx, dy));
            msg(2, C_BRIGHT_RED, line);
        }
    }
}

/* D69: what the player's figures heard since his last phase - fights, spells, deaths within HEAR_FIELDS
 * that none of them saw. The loudest (death, fight, spell), nearest one becomes the warning line; up to
 * HEARD_MAX go on the big map. Returns whether a line was written. */
static bool report_noises(void)
{
    static const char *const WHAT[3] = {"Kampflaerm", "Magie knistert", "Todesschrei"};
    static const u8 LOUD[3] = {1, 0, 2};            /* NOISE_FIGHT, _SPELL, _DEATH */
    u8 k, best = 0xFF, best_d = 0xFF, heard = 0;
    bool wrote = FALSE;
    s16 ax, ay;
    p1_anchor(&ax, &ay);
    heard_n = 0;
    for (k = 0; k < world.noise_n; k++) {
        const Noise *n = &world.noises[k];
        u8 i, d;
        bool near = FALSE;
        if (sight_visible(&p1_sight, &world, n->x, n->y))
            continue;                                           /* seen, not just heard */
        for (i = 0; i < world.unit_count && !near; i++)
            near = world.units[i].owner == OWN_P1 &&
                   world_distance(&world, world.units[i].x, world.units[i].y, n->x, n->y) <= HEAR_FIELDS;
        if (!near)
            continue;
        heard++;
        if (heard_n < HEARD_MAX) {
            heard_x[heard_n] = n->x - n->x % 3;
            heard_y[heard_n] = n->y - n->y % 3;
            heard_n++;
        }
        d = (u8)world_distance(&world, ax, ay, n->x, n->y);
        if (best == 0xFF || LOUD[n->kind] > LOUD[world.noises[best].kind] ||
            (LOUD[n->kind] == LOUD[world.noises[best].kind] && d < best_d)) {
            best = k;
            best_d = d;
        }
    }
    if (best != 0xFF) {
        char line[44];
        s16 dx, dy;
        const Noise *n = &world.noises[best];
        world_delta(&world, ax, ay, n->x, n->y, &dx, &dy);
        if (heard > 1)
            snprintf(line, sizeof line, "%s %s, %d Felder +%d", WHAT[n->kind], compass(dx, dy), best_d, heard - 1);
        else
            snprintf(line, sizeof line, "%s %s, %d Felder", WHAT[n->kind], compass(dx, dy), best_d);
        msg(2, C_BRIGHT_YELLOW, line);
        wrote = TRUE;
    }
    world_noise_clear(&world);                                  /* the next listening period */
    return wrote;
}

static void settle(void)
{
    game_credit_kills(&game, &world);
    turn_revalidate(&turns, &world);
    if (!game_ended && game_outcome(&game, &world, OWN_P1) != OUT_RUNNING)
        end_pending = TRUE;
}

static void on_round(Turns *t, World *w, void *ctx)
{
    area_round_end(w, &t->rng);
    game_new_round((Game *)ctx, t->round);
}

/* ---------- panel ---------- */

static void panel_caps(u8 owner, u8 *cap)
{
    u8 i;
    for (i = 0; i < 6; i++)
        cap[i] = 1;
    for (i = 0; i < world.unit_count; i++) {
        const Unit *u = &world.units[i];
        u8 apm;
        if (u->owner != owner)
            continue;
        apm = u->ap_max > u->ap_fly ? u->ap_max : u->ap_fly;
        if (apm > cap[0]) cap[0] = apm;
        if (u->sta_max > cap[1]) cap[1] = u->sta_max;
        if (u->con_max > cap[2]) cap[2] = u->con_max;
        if (u->com > cap[3]) cap[3] = u->com;
        if (u->def > cap[4]) cap[4] = u->def;
        if (u->mana_max > cap[5]) cap[5] = u->mana_max;
    }
}

static u8 panel_bonus(const Unit *u, u8 i)
{
    u8 k, sum = 0;
    for (k = 0; k < UNIT_EFFECTS; k++) {
        u8 kind = u->effects[k].kind;
        if (u->effects[k].rounds == 0)
            continue;
        if ((i == 0 && kind == EFF_SPEED) ||
            (i == 3 && (kind == EFF_STRENGTH || kind == EFF_MAGIC_WEAPON)) ||
            (i == 4 && (kind == EFF_SHIELD || kind == EFF_PROTECT)))
            sum += effect_power(u, kind);
    }
    return sum;
}

static void panel_ground(int16_t x, int16_t y)
{
    const char *ground[GROUND_MAX];
    u8 i, n = ground_names(&world, x, y, ground);
    ui_text(PANEL_COL, 14, C_GREY, "Am Boden:", PANEL_COLS);
    for (i = 0; i < GROUND_MAX; i++)
        ui_text(PANEL_COL, 15 + i, C_BRIGHT_WHITE, i < n ? ground[i] : "", PANEL_COLS);
}

static void panel_labels(void)
{
    static const char *const LABEL[6] = {"AP", "AU", "LE", "KA", "VE", "MA"};
    static const u8 EDGE[6] = {C_GREEN, C_YELLOW, C_RED, C_GREY, C_BLUE, C_CYAN};
    u8 i;
    for (i = 0; i < 6; i++)
        ui_text(PANEL_COL + 1 + i * 2, 12, EDGE[i], LABEL[i], 2);
}

static void panel_unit(const Unit *u)
{
    char buf[16];
    u8 cap[6];

    ui_text(PANEL_COL, 0, C_BRIGHT_WHITE, name_unit(u), PANEL_COLS);
    if (u->in_use != NO_ITEM && u->in_use < u->item_count)
        snprintf(buf, sizeof buf, "Hand: %.7s", OBJECTS[u->items[u->in_use]].name);
    else
        sprintf(buf, "Hand: -");
    ui_text(PANEL_COL, 1, C_BRIGHT_YELLOW, buf, PANEL_COLS);
    sprintf(buf, "AP %2d", u->ap);
    ui_text(PANEL_COL, 2, C_BRIGHT_GREEN, buf, 7);
    if (u->mana_max) {
        sprintf(buf, "Ma%3d", u->mana);
        ui_text(PANEL_COL + 8, 2, C_BRIGHT_MAGENTA, buf, 5);
    } else {
        ui_text(PANEL_COL + 8, 2, C_BLACK, "", 5);
    }
    panel_caps(u->owner, cap);
    ui_bar(0, u->ap, (u->flags & UF_FLYING) ? u->ap_fly : u->ap_max, cap[0], panel_bonus(u, 0));
    ui_bar(1, u->sta, u->sta_max, cap[1], 0);
    ui_bar(2, u->con, u->con_max, cap[2], 0);
    ui_bar(3, u->com, u->com, cap[3], panel_bonus(u, 3));
    ui_bar(4, u->def, u->def, cap[4], panel_bonus(u, 4));
    ui_bar(5, u->mana, u->mana_max, cap[5], 0);
    panel_labels();
    ui_text(PANEL_COL, 13, C_BRIGHT_CYAN, "A Menu C Wahl", PANEL_COLS);
    panel_ground(u->x, u->y);
}

/* Look mode: the examined field; with a visible unit it is the unit panel. */
static void panel_at(int16_t x, int16_t y)
{
    char desc[24];
    u8 i, u;
    int16_t wx = x, wy = y;
    if (!world_wrap(&world, &wx, &wy))
        return;
    u = world_unit_at(&world, wx, wy, UL_GROUND);
    if (u == NO_UNIT)
        u = world_unit_at(&world, wx, wy, UL_AIR);
    if (u != NO_UNIT) {
        const Unit *un = &world.units[u];
        if (un->owner == OWN_P1 ||
            (sight_unit_visible(&p1_sight, &world, un) && !(un->flags & UF_INVISIBLE))) {
            panel_unit(un);
            ui_text(PANEL_COL, 13, C_BRIGHT_CYAN, "C Ziel B Zurueck", PANEL_COLS);
            return;
        }
    }
    describe_field(&world, &p1_sight, wx, wy, desc, sizeof desc);
    ui_text(PANEL_COL, 0, C_BRIGHT_WHITE, desc, PANEL_COLS);
    ui_text(PANEL_COL, 1, C_BLACK, "", PANEL_COLS);
    ui_text(PANEL_COL, 2, C_BLACK, "", PANEL_COLS);
    for (i = 0; i < 6; i++)
        ui_bar(i, 0, 0, 1, 0);
    panel_labels();
    ui_text(PANEL_COL, 13, C_BRIGHT_CYAN, "C Ziel B Zurueck", PANEL_COLS);
    panel_ground(wx, wy);
}

static void status(void)
{
    char buf[40];
    sprintf(buf, "Runde %d - %s", turns.round, name_owner(turns.phase));
    msg(0, C_BRIGHT_WHITE, buf);
}

/* ---------- frame ---------- */

static u16 target_cursor_tile(void)
{
    const Unit *u = &world.units[active()];
    if (target_kind == TA_SPELL) {
        if (!spell_in_range(&world, u, target_spell, books[OWN_P1].level[target_spell],
                            look_x, look_y) ||
            !spell_line_clear(&world, u, look_x, look_y, target_air))
            return T_CURSOR_RED;
        return target_air ? T_CURSOR_BLUE : T_CURSOR_YELLOW;
    } else if (target_kind == TA_FIRE) {
        if (world_range(&world, u->x, u->y, look_x, look_y) > items_fire_range(&world, active()))
            return T_CURSOR_RED;
    } else {
        u8 wt = u->in_use != NO_ITEM && u->in_use < u->item_count
                    ? OBJECTS[u->items[u->in_use]].weight : 1;
        if (world_range(&world, u->x, u->y, look_x, look_y) > items_throw_range(&world, active(), wt))
            return T_CURSOR_RED;
    }
    if (!sight_has_spell_los(&world, u->x, u->y, look_x, look_y))
        return T_CURSOR_RED;
    if (world_unit_at(&world, look_x, look_y, UL_AIR) != NO_UNIT)
        return T_CURSOR_BLUE;
    return T_CURSOR_YELLOW;
}

/* The cursor is a sprite: moving or blinking it repaints nothing. */
static void draw_cursor(void)
{
    const Unit *u = &world.units[active()];
    int16_t cx, cy, vx, vy;
    u16 tile;
    bool visible = TRUE;
    if (mode == MODE_TARGET) {
        cx = look_x;
        cy = look_y;
        tile = target_cursor_tile();
    } else if (mode == MODE_LOOK) {
        cx = look_x;
        cy = look_y;
        tile = T_CURSOR_WHITE;
    } else {
        cx = u->x;
        cy = u->y;
        tile = (u->flags & UF_FLYING) ? T_CURSOR_BLUE : T_CURSOR_GREEN;
        visible = cursor_on;
    }
    world_delta(&world, view_origin_x(), view_origin_y(), cx, cy, &vx, &vy);
    render_cursor(vx, vy, tile, visible);
}

static void panel_mode(void)
{
    const Unit *u = &world.units[active()];
    if (mode == MODE_PLAY) {
        panel_unit(u);
        status();
        if (tutorial_on) {              /* step conditions, then the hint (M5) */
            if (tutorial_update(&tut, &world, &game) >= TUT_DONE)
                tutorial_on = FALSE;
            msg(2, C_BRIGHT_CYAN, tutorial_on ? tutorial_hint(tut.step) : "Tutorial geschafft!");
        }
    } else {
        char buf[24];
        panel_at(look_x, look_y);
        describe_field(&world, &p1_sight, look_x, look_y, buf, sizeof buf);
        msg(1, C_BRIGHT_CYAN, buf);
        if (mode == MODE_TARGET)
            msg(2, C_GREY, target_kind != TA_SPELL ? "C wirkt, B bricht ab."
                           : target_air ? "LUFT  A Boden  C wirkt" : "BODEN A Luft  C wirkt");
    }
}

static void frame(void)
{
    const Unit *u = &world.units[active()];
    view_set_roof_viewer(u->x, u->y);
    view_set_active_unit(u->id);
    if (mode == MODE_PLAY)
        view_follow(&world, u->x, u->y);
    else
        view_follow(&world, look_x, look_y);
    view_update(&world);
    render_fields();
    draw_cursor();
    panel_mode();
}

/* The free cursor moved: the map only changes when the window scrolls. */
static void frame_aim(void)
{
    int16_t ox = view_origin_x(), oy = view_origin_y();
    view_follow(&world, look_x, look_y);
    if (view_origin_x() != ox || view_origin_y() != oy) {
        view_update(&world);
        render_fields();
    }
    draw_cursor();
    panel_mode();
}

/* ---------- menus over the map ---------- */

typedef void (*MenuLabel)(u8 i, char *out);

/* A list: d-pad up/down selects, A/C picks (index), B/START cancels (-1). */
/* start = the entry selected first. */
static s8 menu_run(const char *title, const char *hint, u8 n, u8 start, MenuLabel label)
{
    u8 sel = start < n ? start : 0, top = 0, rows = 24, i;
    bool redraw = TRUE;
    pad_pressed();
    g_dbg.menus++;
    render_cursor(0, 0, 0, FALSE);
    ui_overlay_clear();
    ui_text(0, 0, C_BRIGHT_YELLOW, title, 27);
    ui_text(0, 26, C_GREY, hint, 27);
    for (;;) {
        u16 pressed = pad_pressed();
        if (pressed & (BUTTON_B | BUTTON_START)) {
            sound_play(SND_BACK);
            ui_overlay_close();
            g_dbg.menus--;
            return -1;
        }
        if (pressed & (BUTTON_A | BUTTON_C)) {
            sound_play(SND_CONFIRM);
            ui_overlay_close();
            g_dbg.menus--;
            return (s8)sel;
        }
        if (pressed & BUTTON_UP) {
            sel = sel ? sel - 1 : n - 1;
            redraw = TRUE;
            sound_play(SND_MENU);
        }
        if (pressed & BUTTON_DOWN) {
            sel = sel + 1 < n ? sel + 1 : 0;
            redraw = TRUE;
            sound_play(SND_MENU);
        }
        if (redraw) {
            g_dbg.sel = sel;
            if (sel < top)
                top = sel;
            if (sel >= top + rows)
                top = sel - rows + 1;
            for (i = 0; i < rows; i++) {
                char line[28], buf[28];
                u8 k = top + i;
                if (k < n) {
                    label(k, buf);
                    sprintf(line, "%c %.25s", k == sel ? '>' : ' ', buf);
                    ui_text(0, 1 + i, k == sel ? C_BRIGHT_YELLOW : C_BRIGHT_WHITE, line, 27);
                } else {
                    ui_text(0, 1 + i, C_BLACK, "", 27);
                }
            }
            redraw = FALSE;
        }
        SYS_doVBlankProcess();
    }
}

/* ---------- actions ---------- */

static void do_apply(s8 dx, s8 dy)
{
    u8 me = active();
    int16_t x = world.units[me].x + dx, y = world.units[me].y + dy;
    u8 fe = world_feature(&world, x, y);
    if (!turn_may_move(&turns)) {
        msg(1, C_BRIGHT_RED, "Runde 1: nur Zaubern (PM 7).");
        return;
    }
    switch (fe) {
    case FE_DOOR_OPEN:
        if (world_close_door(&world, me, x, y)) {
            sound_play(SND_DOOR);
            msg(1, C_BRIGHT_GREEN, "Tuer geschlossen.");
        }
        else if (world_unit_at(&world, x, y, UL_GROUND) != NO_UNIT ||
                 world_unit_at(&world, x, y, UL_AIR) != NO_UNIT)
            msg(1, C_BRIGHT_RED, "Jemand steht in der Tuer.");
        else
            msg(1, C_BRIGHT_RED, "Zu wenig AP oder keine Haende.");
        break;
    case FE_DOOR_CLOSED:
        if (world_lock_door(&world, me, x, y)) {
            sound_play(SND_DOOR);
            msg(1, C_BRIGHT_GREEN, "Tuer abgeschlossen.");
        }
        else if (!world_has_key(&world, me))
            msg(1, C_BRIGHT_RED, "Kein Schluessel zum Abschliessen.");
        else
            msg(1, C_BRIGHT_RED, "Zu wenig AP oder keine Haende.");
        break;
    case FE_DOOR_LOCKED:
        if (world_unlock_door(&world, me, x, y)) {
            sound_play(SND_DOOR);
            msg(1, C_BRIGHT_GREEN, "Tuer aufgeschlossen.");
        }
        else if (!world_has_key(&world, me))
            msg(1, C_BRIGHT_RED, "Abgeschlossen: Schluessel oder Gewalt.");
        else
            msg(1, C_BRIGHT_RED, "Zu wenig AP oder keine Haende.");
        break;
    case FE_CHEST:
    case FE_CHEST_FREE:
        if (items_open_chest(&world, &turns.rng, me, x, y)) {
            sound_play(SND_CHEST);
            msg(1, C_BRIGHT_YELLOW, "Truhe geoeffnet!");
        }
        else
            msg(1, C_BRIGHT_RED, "Truhe laesst sich nicht oeffnen.");
        break;
    default:
        msg(1, C_BRIGHT_RED, "Dort gibt es nichts zu benutzen.");
        break;
    }
    update_sight();
    frame();
}

static void do_step(u8 mask)
{
    s8 dx, dy;
    char m[48];
    u8 att = active();
    int16_t nx, ny;
    u8 other;

    if (!chord_to_step(mask, &dx, &dy))
        return;
    if (apply_pending) {
        apply_pending = FALSE;
        do_apply(dx, dy);
        return;
    }
    if (mode != MODE_PLAY) {            /* free cursor, no costs */
        int16_t cx = look_x + dx, cy = look_y + dy;
        if (world_wrap(&world, &cx, &cy)) {
            look_x = cx;
            look_y = cy;
        }
        frame_aim();
        return;
    }
    if (!turn_may_move(&turns)) {
        msg(1, C_BRIGHT_RED, "Runde 1: nur Zaubern (PM 7).");
        return;
    }
    if (world_move_unit(&world, att, dx, dy)) {
        sound_play(SND_STEP);
        if (game_try_enter_portal(&game, &world, att)) {
            sound_play(SND_PORTAL);
            sprintf(m, "Gerettet! Zauberer-1: %d VP.", game.vp[OWN_P1]);
            msg(0, C_BRIGHT_MAGENTA, m);
            settle();
        } else {
            msg(1, C_GREY, "");
        }
        update_sight();
        frame();
        return;
    }
    nx = world.units[att].x + dx;
    ny = world.units[att].y + dy;
    switch (world_bump_kind(&world, att, dx, dy)) {
    case BUMP_DOOR:
        if (world_open_door(&world, att, nx, ny)) {
            sound_play(SND_DOOR);
            msg(1, C_BRIGHT_GREEN, "Tuer geoeffnet.");
            update_sight();
        } else if (!(CREATURES[ride_actor_kind(&world.units[att])].flags & CF_USE)) {
            msg(1, C_BRIGHT_RED, "Keine Haende fuer die Tuer.");
        } else if (world_door_jammed(&world, nx, ny, world.units[att].x, world.units[att].y)) {
            msg(1, C_BRIGHT_RED, "Die Tuer klemmt: kein Platz.");
        } else {
            msg(1, C_BRIGHT_RED, "Zu wenig AP fuer die Tuer.");
        }
        frame();
        return;
    case BUMP_NO_AP:
        msg(1, C_BRIGHT_RED, "Zu wenig AP - C: andere Einheit.");
        return;
    case BUMP_BOUND:
        msg(1, C_BRIGHT_RED, "Gebunden: Gegner nebenan - kaempfen.");
        return;
    case BUMP_HELD: {
        bool torn = FALSE;
        u8 hit = combat_terrain(&world, &turns.rng, att, nx, ny, &torn);
        if (!hit)
            msg(1, C_BRIGHT_RED, "Brei oder Ranken: zu schwer zu zerreissen.");
        else if (torn) {
            sound_play(SND_HIT);
            msg(1, C_BRIGHT_YELLOW, "Zerrissen!");
        }
        else
            msg(1, C_GREY, "Es haelt noch.");
        settle();
        update_sight();
        frame();
        return;
    }
    case BUMP_UNIT: {
        CombatResult r;
        char name[16], aname[16];
        other = world_unit_at(&world, nx, ny, (world.units[att].flags & UF_FLYING) ? UL_AIR : UL_GROUND);
        if (other == NO_UNIT || world.units[other].owner == OWN_P1) {
            msg(1, C_BRIGHT_RED, "Da geht es nicht weiter.");
            return;
        }
        if (!world_can_pay(&world, att, ACT_MELEE)) {
            sprintf(m, "Zu wenig AP/Ausdauer: Angriff kostet %d.", ACTIONS[ACT_MELEE].ap);
            msg(1, C_BRIGHT_RED, m);
            return;
        }
        snprintf(name, sizeof name, "%s", name_unit(&world.units[other]));
        snprintf(aname, sizeof aname, "%s", name_unit(&world.units[att]));
        if (!combat_melee(&world, &turns.rng, att, other, &r)) {
            msg(1, C_BRIGHT_RED, "Angriff nicht moeglich.");
            return;
        }
        sound_play(r.died ? SND_DEATH : (r.hit || r.wound) ? SND_HIT : SND_MISS);
        if (r.died)
            sprintf(m, "%s stirbt!", name);
        else if (r.wound)
            sprintf(m, "Treffer: %d. Toedliche Wunde!", r.damage);
        else if (r.hit)
            sprintf(m, "Treffer: %d Schaden.", r.damage);
        else
            sprintf(m, "Verfehlt.");
        msg(1, r.hit || r.died ? C_BRIGHT_YELLOW : C_GREY, m);
        if (r.returned) {
            if (r.attacker_died)
                sprintf(m, "Rueckschlag toetet %s!", aname);
            else if (r.return_hit)
                sprintf(m, "Rueckschlag: %d Schaden.", r.return_damage);
            else
                sprintf(m, "Rueckschlag: daneben.");
            msg(2, (r.attacker_died || r.return_hit) ? C_BRIGHT_RED : C_GREY, m);
        }
        settle();
        update_sight();
        frame();
        return;
    }
    case BUMP_TERRAIN: {
        bool destroyed;
        u8 fe = world_feature(&world, nx, ny), dmg;
        if (fe == FE_DOOR_LOCKED && world_unlock_door(&world, att, nx, ny)) {
            sound_play(SND_DOOR);
            msg(1, C_BRIGHT_GREEN, "Tuer aufgeschlossen.");
            frame();
            return;
        }
        if (fe == FE_CHEST || fe == FE_CHEST_FREE) {
            if (items_open_chest(&world, &turns.rng, att, nx, ny)) {
                sound_play(SND_CHEST);
                msg(1, C_BRIGHT_YELLOW, "Truhe geoeffnet!");
                update_sight();
            } else {
                msg(1, C_BRIGHT_RED, "Truhe laesst sich nicht oeffnen.");
            }
            frame();
            return;
        }
        dmg = combat_terrain(&world, &turns.rng, att, nx, ny, &destroyed);
        if (dmg == 0) {
            if (world_blocks(&world, nx, ny) && FEATURE_TOUGH[fe] == 0)
                msg(1, C_BRIGHT_RED, "Unzerstoerbar.");
            else if (world_blocks(&world, nx, ny))
                msg(1, C_BRIGHT_RED, "Zu schwach oder zu wenig AP/Ausdauer.");
            else
                msg(1, C_BRIGHT_RED, "Da geht es nicht weiter.");
        } else if (destroyed) {
            sound_play(SND_SMASH);
            sprintf(m, "%s zerstoert!", name_feature(fe));
            msg(1, C_BRIGHT_YELLOW, m);
            update_sight();
        } else {
            sprintf(m, "%d Schaden.", dmg);
            msg(1, C_GREY, m);
        }
        frame();
        return;
    }
    default:
        msg(1, C_BRIGHT_RED, "Da geht es nicht weiter.");
        return;
    }
}

static void end_turn(void)
{
    bool heard;
    confirm_end = FALSE;
    if (!turn_humans_present(&turns, &world))
        msg(1, C_BRIGHT_YELLOW, "Die KI spielt zu Ende ...");
    else
        msg(1, C_BRIGHT_YELLOW, "Zauberer-2 ist am Zug ...");    /* E-8: shown while the AI thinks */
    msg(2, C_GREY, "");
    SYS_doVBlankProcess();                      /* the line is on screen before the AI blocks */
    turn_end_phase(&turns, &world);
    game_credit_kills(&game, &world);
    view_set_portal(game.portal_open ? game.portal_x : -1, game.portal_y);
    if (game_over(&game, &world) || !turn_humans_present(&turns, &world) ||
        game_outcome(&game, &world, OWN_P1) != OUT_RUNNING)
        end_pending = TRUE;
    else if (game.portal_open && turns.round == game.portal_round) {
        sound_play(SND_PORTAL);
        msg(1, C_BRIGHT_MAGENTA, "Das Portal oeffnet sich!");
    }
    else
    {
        sound_play(SND_ROUND);
        msg(1, C_BRIGHT_GREEN, "Neue Runde.");
    }
    update_sight();
    frame();
    heard = report_noises();                    /* D69: what was heard meanwhile (clears the noise list) */
    if (!end_pending && save_game() && !heard)  /* autosave at the round end (GDD 2.3) */
        msg(2, C_GREY, "Gespeichert.");
}

/* A page of text over the map area; any button closes it. */
static void text_page(const char *title, const char *const *lines, u8 n)
{
    u8 i;
    pad_pressed();
    render_cursor(0, 0, 0, FALSE);
    ui_overlay_clear();
    ui_text(0, 0, C_BRIGHT_YELLOW, title, 27);
    for (i = 0; i < n; i++)
        ui_text(0, 2 + i, C_BRIGHT_WHITE, lines[i], 27);
    ui_text(0, 26, C_GREY, "Eine Taste: weiter", 27);
    while (!(pad_pressed() & (BUTTON_A | BUTTON_B | BUTTON_C | BUTTON_START)))
        SYS_doVBlankProcess();
    ui_overlay_close();
}

/* The 96x96 end picture right of the text (plane B is cleared; the main menu repaints everything anyway). */
static void show_end_picture(bool win)
{
    VDP_setEnable(FALSE);
    VDP_clearPlane(BG_B, TRUE);
    VDP_setHorizontalScroll(BG_B, 0);
    VDP_setVerticalScroll(BG_B, 0);
    ui_cells_transparent(27, 3, 12, 12);
    if (win) {
        PAL_setColors(0, (const u16 *)pic_win_pal, 32, CPU);
        VDP_loadTileData((const u32 *)pic_win_tiles, TILE_USER_INDEX, PIC_WIN_TILES, CPU);
        VDP_setTileMapDataRectEx(BG_B, (const u16 *)pic_win_map, TILE_USER_INDEX, 27, 3, 12, 12, 12, CPU);
    } else {
        PAL_setColors(0, (const u16 *)pic_lose_pal, 32, CPU);
        VDP_loadTileData((const u32 *)pic_lose_tiles, TILE_USER_INDEX, PIC_LOSE_TILES, CPU);
        VDP_setTileMapDataRectEx(BG_B, (const u16 *)pic_lose_map, TILE_USER_INDEX, 27, 3, 12, 12, 12, CPU);
    }
    VDP_setEnable(TRUE);
}

/* The outcome is decided: a summary page, then back to the main menu. */
static void end_screen(void)
{
    static char l[12][28];
    const char *lines[12];
    GameOutcome o = game_outcome(&game, &world, OWN_P1);
    u8 i, n = 8, sc = cur_sc && !cur_sc->tutorial ? (u8)(cur_sc - SCENARIOS) + 1 : 0;
    if (o == OUT_WIN) {
        sprintf(l[0], "Du bist durch das Portal");
        sprintf(l[1], "entkommen!");
    } else if (turn_humans_present(&turns, &world)) {
        sprintf(l[0], "Das Portal ist zu.");
        sprintf(l[1], "Du bist nicht entkommen.");
    } else {
        sprintf(l[0], "Dein Zauberer ist gefallen.");
        l[1][0] = 0;
    }
    sprintf(l[2], "%s", cur_sc ? cur_sc->title : "");
    sprintf(l[3], "Runden:        %d", turns.round);
    sprintf(l[4], "Siegpunkte:    %d", game.vp[OWN_P1]);
    sprintf(l[5], "davon Beute:   %d", game.loot_vp[OWN_P1]);
    sprintf(l[6], "Besiegte:      %d", game.kills[OWN_P1]);
    sprintf(l[7], "Gegner-VP:     %d", game.vp[OWN_P2]);
    if (o == OUT_WIN && sc) {           /* the campaign: points become experience, a first clear a level */
        Wizard *w = &wizard_slots[0];
        u8 level = w->level;
        wizard_campaign_result(w, game.vp[OWN_P1], sc);
        wizards_save();
        l[8][0] = 0;
        sprintf(l[9], "Erfahrung +%d (%d)", game.vp[OWN_P1], w->xp);
        sprintf(l[10], "Stufe %d%s", w->level, w->level > level ? "  - aufgestiegen!" : "");
        n = 11;
    }
    for (i = 0; i < n; i++)
        lines[i] = l[i];
    g_dbg.screen = SCREEN_END;
    music_start(o == OUT_WIN ? SONG_WIN : SONG_LOSE);
    show_end_picture(o == OUT_WIN);
    text_page(o == OUT_WIN ? "GEWONNEN" : "VERLOREN", lines, n);
    music_stop();
    end_pending = FALSE;
    game_ended = TRUE;
}

/* ---------- help, lexicon, log (texts: tools/md_help.py) ----------
 * A text resource is: u16 page count (big-endian), then per page u8 title_len, title, u8 line_count and
 * per line u8 len + bytes. */

static u16 book_count(const u8 *bin)
{
    return ((u16)bin[0] << 8) | bin[1];
}

/* The page's title (copied into title, 0-terminated), its line count; *lines = first line record. */
static u8 book_page(const u8 *bin, u16 idx, char *title, const u8 **lines)
{
    const u8 *p = bin + 2;
    u8 n;
    while (idx--) {
        u8 k;
        p += 1 + p[0];
        n = *p++;
        for (k = 0; k < n; k++)
            p += 1 + p[0];
    }
    memcpy(title, p + 1, p[0]);
    title[p[0]] = 0;
    p += 1 + p[0];
    n = *p++;
    *lines = p;
    return n;
}

/* The nth line record of a page as a string; the pointer moves on. */
static const u8 *page_line(const u8 *p, char *out)
{
    memcpy(out, p + 1, p[0]);
    out[p[0]] = 0;
    return p + 1 + p[0];
}

/* Pages with < > and B. */
static void book_viewer(const u8 *bin, u16 start)
{
    u16 n = book_count(bin), cur = start < n ? start : 0;
    bool redraw = TRUE;
    pad_pressed();
    render_cursor(0, 0, 0, FALSE);
    for (;;) {
        u16 pressed = pad_pressed();
        if (pressed & (BUTTON_B | BUTTON_START | BUTTON_A)) {
            sound_play(SND_BACK);
            break;
        }
        if (pressed & (BUTTON_RIGHT | BUTTON_C | BUTTON_DOWN)) {
            cur = cur + 1 < n ? cur + 1 : 0;
            redraw = TRUE;
            sound_play(SND_MENU);
        }
        if (pressed & (BUTTON_LEFT | BUTTON_UP)) {
            cur = cur ? cur - 1 : n - 1;
            redraw = TRUE;
            sound_play(SND_MENU);
        }
        if (redraw) {
            char title[28], line[28];
            const u8 *p;
            u8 i, k = book_page(bin, cur, title, &p);
            ui_overlay_clear();
            snprintf(line, sizeof line, "%.20s %d/%d", title, cur + 1, n);
            ui_text(0, 0, C_BRIGHT_YELLOW, line, 27);
            for (i = 0; i < k; i++) {
                p = page_line(p, line);
                ui_text(0, 2 + i, C_BRIGHT_WHITE, line, 27);
            }
            ui_text(0, 26, C_GREY, "<,>: Seite   A/B: zurueck", 27);
            redraw = FALSE;
        }
        SYS_doVBlankProcess();
    }
    ui_overlay_close();
}

/* ---- the lexicon: creatures, objects, spells; unseen entries show ??? ---- */

static void lex_creature_label(u8 i, char *out)
{
    snprintf(out, 28, "%s", lexicon_seen_creature(&lex, i) ? CREATURES[i].name : "???");
}

static void lex_object_label(u8 i, char *out)
{
    snprintf(out, 28, "%s", lexicon_seen_object(&lex, i) ? OBJECTS[i].name : "???");
}

static void lex_spell_label(u8 i, char *out)
{
    snprintf(out, 28, "%s", SPELLS[i].name);
}

/* name, values and description of one entry as a page */
static void lex_detail(u8 section, u8 idx)
{
    static char l[24][28];
    const char *lines[24];
    const u8 *p;
    char title[28];
    u8 n = 0, k, i;
    snprintf(l[n++], 28, "%s", "");
    if (section == 0) {
        const CreatureDef *c = &CREATURES[idx];
        snprintf(l[n++], 28, "Kampf %d  Vert. %d  MR %d", c->combat, c->defence, c->magic_res);
        snprintf(l[n++], 28, "Konst. %d  Ausd. %d  AP %d", c->con, c->stamina, c->ap);
        snprintf(l[n++], 28, "Siegpunkte %d", c->vp);
        k = book_page(lexicon_md, idx, title, &p);
    } else if (section == 1) {
        const ObjectDef *o = &OBJECTS[idx];
        snprintf(l[n++], 28, "Gewicht %d  Siegpunkte %d", o->weight, o->vp);
        k = book_page(lexicon_md, CR_COUNT + idx, title, &p);
    } else {
        static const char *const CAT[4] = {"Beschwoerung", "Trank", "Flaeche", "Zauber"};
        snprintf(l[n++], 28, "%s", CAT[SPELLS[idx].category]);
        snprintf(l[n++], 28, "Mana Stufe 1: %d, x(Stufe+1)", SPELLS[idx].mana);
        k = idx < book_count(spells_md) ? book_page(spells_md, idx, title, &p) : 0;
    }
    snprintf(l[n++], 28, "%s", "");
    for (i = 0; i < k && n < 24; i++)
        p = page_line(p, l[n++]);
    for (i = 0; i < n; i++)
        lines[i] = l[i];
    text_page(section == 0 ? CREATURES[idx].name : section == 1 ? OBJECTS[idx].name : SPELLS[idx].name,
              lines, n);
}

static void lexicon_screen(void)
{
    static const char *const SEC[3] = {"Kreaturen", "Objekte", "Zauber"};
    s8 sec = 0, pick;
    for (;;) {
        sec = menu_run_text3("Lexikon", "A/C waehlt, B zurueck", sec < 0 ? 0 : sec, SEC);
        if (sec < 0)
            return;
        {
            u8 n = sec == 0 ? CR_COUNT : sec == 1 ? OBJ_COUNT : SPELL_COUNT;
            MenuLabel label = sec == 0 ? lex_creature_label : sec == 1 ? lex_object_label : lex_spell_label;
            pick = 0;
            for (;;) {
                char title[28];
                snprintf(title, sizeof title, "%s (%d)", SEC[(u8)sec], sec == 0 ? lexicon_seen_count(&lex) : n);
                pick = menu_run(title, "A/C: lesen, B zurueck", n, (u8)(pick < 0 ? 0 : pick), label);
                if (pick < 0)
                    break;
                if (sec == 0 && !lexicon_seen_creature(&lex, (u8)pick))
                    continue;
                if (sec == 1 && !lexicon_seen_object(&lex, (u8)pick))
                    continue;
                lex_detail((u8)sec, (u8)pick);
            }
            pick = 0;
        }
    }
}

/* ---- the whole map (m) ---- */

static u8 bigmap[MAP_MAX_W * MAP_MAX_H];

static void bigmap_screen(void)
{
    char head[28];
    u8 x, y;
    for (y = 0; y < world.h; y++)
        for (x = 0; x < world.w; x++) {
            u8 col = C_BLACK, u;
            if (sight_explored(&p1_sight, &world, x, y)) {     /* unexplored stays black (GDD 3.4) */
                u = world_unit_at(&world, x, y, UL_GROUND);
                if (u == NO_UNIT)
                    u = world_unit_at(&world, x, y, UL_AIR);
                if (u != NO_UNIT && world.units[u].owner != OWN_P1 && !sight_visible(&p1_sight, &world, x, y))
                    u = NO_UNIT;                                /* enemies only where they are seen now */
                if (u != NO_UNIT)
                    col = world.units[u].owner == OWN_P1 ? C_BRIGHT_WHITE : C_BRIGHT_RED;
                else if (game.portal_open && x == game.portal_x && y == game.portal_y)
                    col = C_BRIGHT_MAGENTA;
                else
                    col = world_floor(&world, x, y) == FL_WATER ? C_BLUE
                          : world_floor(&world, x, y) == FL_FOREST ? C_GREEN : C_GREY;
            }
            bigmap[y * world.w + x] = col;
        }
    for (y = 0; y < heard_n; y++)                               /* roughly where noises came from (D69) */
        for (x = 0; x < 9; x++)
            if (heard_x[y] + x % 3 < world.w && heard_y[y] + x / 3 < world.h &&
                bigmap[(heard_y[y] + x / 3) * world.w + heard_x[y] + x % 3] != C_BRIGHT_WHITE)
                bigmap[(heard_y[y] + x / 3) * world.w + heard_x[y] + x % 3] = C_BRIGHT_CYAN;
    if (foe_x >= 0 && foe_x < world.w && foe_y < world.h)
        bigmap[foe_y * world.w + foe_x] = C_BRIGHT_YELLOW;     /* where he was last seen */
    pad_pressed();
    render_cursor(0, 0, 0, FALSE);
    ui_overlay_close();
    render_overview(bigmap, world.w, world.h);
    snprintf(head, sizeof head, "Gesamtkarte  Runde %d", turns.round);
    ui_text(0, 0, C_BRIGHT_YELLOW, head, 27);
    if (foe_x >= 0) {
        snprintf(head, sizeof head, "Gegner zuletzt Runde %d", foe_round);
        ui_text(0, 26, C_BRIGHT_YELLOW, head, 27);
    } else {
        ui_text(0, 26, C_GREY, "Gegner noch ungesehen.", 27);
    }
    while (!(pad_pressed() & (BUTTON_A | BUTTON_B | BUTTON_C | BUTTON_START)))
        SYS_doVBlankProcess();
    render_overview_end();
    ui_text(0, 0, C_BLACK, "", 27);
    ui_text(0, 26, C_BLACK, "", 27);
    ui_overlay_close();
}

/* ---- the message log ---- */

static void log_screen(void)
{
    static char l[24][28];
    const char *lines[24];
    u8 n = 0, k, shown = 0;
    if (log_count == 0) {
        snprintf(l[n++], 28, "Noch keine Meldungen.");
    } else {
        /* newest last; each message may take two lines: fill from the newest backwards */
        char tmp[24][28];
        u8 t = 0;
        for (k = 0; k < log_count && t < 22; k++) {
            const char *text = log_ring[(log_head + LOG_N - 1 - k) % LOG_N];
            char part[28];
            u8 len = strlen(text), off = 0, rows = 0, r;
            char rowtxt[2][28];
            while (off < len && rows < 2) {
                u8 cut = len - off > 26 ? 26 : len - off;
                if (cut == 26) {
                    u8 b = cut;
                    while (b > 8 && text[off + b] != ' ' && text[off + b - 1] != ' ')
                        b--;
                    if (b > 8)
                        cut = b;
                }
                memcpy(part, text + off, cut);
                part[cut] = 0;
                snprintf(rowtxt[rows++], 28, "%s", part);
                off += cut;
                while (text[off] == ' ')
                    off++;
            }
            if (t + rows > 22)
                break;
            for (r = 0; r < rows; r++)
                snprintf(tmp[t++], 28, "%s", rowtxt[r]);
            shown++;
        }
        for (k = 0; k < t; k++)
            snprintf(l[n++], 28, "%s", tmp[t - 1 - k]);          /* oldest of the shown first */
        (void)shown;
    }
    for (k = 0; k < n; k++)
        lines[k] = l[k];
    text_page("Meldungen", lines, n);
}

static const char *tutorial_hint(u8 step)
{
    static char hint[64];
    char title[28], line[28];
    const u8 *p;
    u8 n, i;
    if (step + 1 >= book_count(tutorial_md))
        return "";
    n = book_page(tutorial_md, step + 1, title, &p);
    hint[0] = 0;
    for (i = 0; i < n; i++) {
        p = page_line(p, line);
        if (!line[0])
            break;                      /* the hint is the first paragraph */
        if (hint[0])
            strcat(hint, " ");
        strcat(hint, line);
    }
    return hint;
}

/* ---------- spells ---------- */

static u8 menu_spells[SPELL_COUNT];     /* spell ids of the open list */

static void spell_label(u8 i, char *out)
{
    u8 id = menu_spells[i];
    char name[18];
    const char *full = SPELLS[id].name;
    u16 n = strlen(full);
    if (n > 16 && n > 7 && strcmp(full + n - 7, " Potion") == 0) {
        memcpy(name, full, n - 7);
        strcpy(name + n - 7, " Pot.");
    } else {
        snprintf(name, sizeof name, "%s", full);
    }
    sprintf(out, "%-15.15s %d %3d", name, books[OWN_P1].level[id],
            spell_cast_mana(id, books[OWN_P1].level[id]));
}

static void cast_targeted(void)
{
    SpellShot shot;
    char m[48];
    u8 wiz = active();
    bool ok;
    u8 count_before = world.unit_count;

    if (target_spell == SP_MAGIC_BOLT || target_spell == SP_MAGIC_LIGHTNING) {
        if (target_spell == SP_MAGIC_LIGHTNING)
            ok = spell_lightning(&world, &books[OWN_P1], wiz, look_x, look_y, target_air, &turns.rng, &shot);
        else
            ok = spell_bolt(&world, &books[OWN_P1], wiz, target_spell, look_x, look_y, target_air, &turns.rng, &shot);
        if (!ok) {
            msg(1, C_BRIGHT_RED, "Ausser Reichweite oder Sicht.");
            return;
        }
        sound_play(target_spell == SP_MAGIC_LIGHTNING ? SND_LIGHTNING : SND_BOLT);
        if (tutorial_on)
            tutorial_notify(&tut, TUT_SPELL);
    } else {
        CastResult cr = spell_apply(&world, &books[OWN_P1], wiz, target_spell, look_x, look_y,
                                    target_air, &turns.rng, &shot);
        if (cr == CAST_REJECTED) {
            msg(1, C_BRIGHT_RED, "Ausser Reichweite oder Sicht.");
            return;
        }
        if (cr == CAST_BAD_TERRAIN) {
            msg(1, C_BRIGHT_RED, "Das Ziel nimmt das nicht an.");
            return;
        }
        if (tutorial_on)
            tutorial_notify(&tut, TUT_SPELL);
        sound_play(target_spell == SP_TELEPORT ? SND_TELEPORT : (target_spell == SP_CURSE || target_spell == SP_SUBVERSION) ? SND_CURSE : SND_SPELL);
        if (cr == CAST_NO_RES) {
            msg(1, C_GREY, "Das Ziel widersteht.");
        } else if (target_spell == SP_MAGIC_FIRE || target_spell == SP_GOOEY_BLOB ||
                   target_spell == SP_TANGLE_VINE || target_spell == SP_FLOOD) {
            if (!shot.hit)
                msg(1, C_GREY, "Nichts faengt an.");
            else
                msg(1, C_BRIGHT_MAGENTA,
                    target_spell == SP_MAGIC_FIRE ? "Es brennt!"
                    : target_spell == SP_GOOEY_BLOB ? "Klebriger Brei!"
                    : target_spell == SP_TANGLE_VINE ? "Ranken wachsen!" : "Die Flut steigt!");
        } else if (target_spell == SP_MAGIC_EYE) {
            game.eye_x = look_x;
            game.eye_y = look_y;
            game.eye_rounds = 1;
            game.eye_range = shot.eye_range;
            msg(1, C_BRIGHT_CYAN, "Auge eroeffnet.");
        } else if (target_spell == SP_MAGIC_SHIELD) {
            msg(1, C_BRIGHT_CYAN, "Schild aktiv.");
        } else if (target_spell == SP_TELEPORT) {
            msg(1, C_BRIGHT_CYAN, "Teleportiert!");
        } else if (target_spell == SP_SUBVERSION) {
            msg(1, C_BRIGHT_YELLOW, "Die Kreatur wechselt die Seite!");
        } else if (target_spell == SP_CURSE) {
            msg(1, C_BRIGHT_YELLOW, "Toedliche Wunde!");
        } else if (target_spell == SP_MAGIC_ATTACK) {
            sprintf(m, "Magie trifft %d Kreaturen.", shot.splash_hits);
            msg(1, C_BRIGHT_YELLOW, m);
        } else if (target_spell == SP_ENCHANT) {
            msg(1, C_BRIGHT_CYAN, "Waffen verzaubert.");
        }
        settle();
        update_sight();
        frame();
        return;
    }
    if (shot.hit)
        sprintf(m, "Zauber trifft: %d Schaden.", shot.damage);
    else
        sprintf(m, "Zauber verpufft.");
    msg(1, shot.hit ? C_BRIGHT_YELLOW : C_GREY, m);
    if (shot.terrain_smashed) {
        sound_play(SND_SMASH);
        msg(2, C_BRIGHT_YELLOW, "Blitz schlaegt das Terrain ein!");
    }
    if (world.unit_count < count_before)
        msg(2, C_BRIGHT_RED, "Mindestens eine Kreatur stirbt.");
    settle();
    update_sight();
    frame();
}

static void throw_or_fire(void)
{
    const Unit *u = &world.units[active()];
    int16_t dx = look_x - u->x, dy = look_y - u->y;
    s8 sx = dx > 0 ? 1 : dx < 0 ? -1 : 0, sy = dy > 0 ? 1 : dy < 0 ? -1 : 0;
    if (target_kind == TA_THROW) {
        if (brew_throw_vial(&world, &turns.rng, active(), sx, sy) ||
            items_throw(&world, &turns.rng, active(), sx, sy))
        {
            sound_play(SND_THROW);
            msg(1, C_BRIGHT_YELLOW, "Geworfen!");
        }
        else
            msg(1, C_BRIGHT_RED, "Nichts zu werfen.");
        settle();
    } else {
        u8 dmg = 0;
        if (items_fire(&world, &turns.rng, active(), look_x, look_y, &dmg)) {
            sound_play(SND_BOW);
            msg(1, dmg ? C_BRIGHT_YELLOW : C_GREY, dmg ? "Schuss trifft!" : "Schuss daneben.");
            settle();
        } else {
            msg(1, C_BRIGHT_RED, "Kein Ziel in Reichweite.");
        }
    }
    update_sight();
    frame();
}

static void start_aim(TargetKind kind, u8 spell)
{
    mode = MODE_TARGET;
    target_kind = kind;
    target_spell = spell;
    target_air = kind == TA_SPELL && (world.units[active()].flags & UF_FLYING) != 0;   /* the caster's own height first */
    look_x = world.units[active()].x;
    look_y = world.units[active()].y;
    frame();
}

/* Confirm the aim (C): casting at the caster himself cancels without cost
 * (GDD 7.1), except for spells that target the caster. */
static void aim_confirm(void)
{
    const Unit *u = &world.units[active()];
    mode = MODE_PLAY;
    if (target_kind == TA_SPELL && target_spell != SP_MAGIC_SHIELD && target_spell != SP_ENCHANT &&
        look_x == u->x && look_y == u->y) {
        msg(1, C_GREY, "Abgebrochen.");
        msg(2, C_GREY, "");
        frame();
        return;
    }
    if (target_kind == TA_SPELL)
        cast_targeted();
    else
        throw_or_fire();
}

static void kind_label(u8 i, char *out)
{
    static const char *const K[2] = {"Zauber", "Beschwoerungen"};
    sprintf(out, "%s", K[i]);
}

static void do_cast(void)
{
    u8 wiz = active(), n = 0, ns = 0, nb = 0, i;
    bool summons = FALSE;
    s8 pick;
    if (ride_actor_kind(&world.units[wiz]) != CR_WIZARD) {
        msg(1, C_BRIGHT_RED, "Nur Zauberer zaubern.");
        return;
    }
    for (i = 0; i < SPELL_COUNT; i++) {
        if (books[OWN_P1].level[i] == 0)
            continue;
        if (SPELLS[i].category == SPC_SUMMON)
            nb++;
        else
            ns++;
    }
    if (!ns && !nb) {
        msg(1, C_BRIGHT_RED, "Keine Zauber im Buch (Designer).");
        return;
    }
    if (ns && nb) {
        pick = menu_run("Was wirken?", "A/C waehlen, B zurueck", 2, 0, kind_label);
        if (pick < 0) {
            frame();
            return;
        }
        summons = pick == 1;
    } else {
        summons = nb != 0;
    }
    for (i = 0; i < SPELL_COUNT; i++)
        if (books[OWN_P1].level[i] && (SPELLS[i].category == SPC_SUMMON) == summons)
            menu_spells[n++] = i;
    for (i = 0; i < n; i++)
        g_spells[i] = menu_spells[i];
    pick = menu_run(summons ? "Kreatur           Stf Mana" : "Zauber            Anz Mana",
                    "A/C wirkt, B zurueck", n, 0, spell_label);
    if (pick < 0) {
        frame();
        return;
    }
    i = menu_spells[(u8)pick];
    if (SPELLS[i].category == SPC_POTION) {
        if (brew_cast(&world, &books[OWN_P1], wiz, i))
            msg(1, C_BRIGHT_GREEN, "Der Kessel brodelt.");
        else
            msg(1, C_BRIGHT_RED, "Brauen braucht Kessel und Zutat.");
        frame();
    } else if (SPELLS[i].category == SPC_SUMMON) {
        if (spell_summon(&world, &books[OWN_P1], wiz, i, &turns.rng)) {
            if (tutorial_on)
                tutorial_notify(&tut, TUT_SPELL);
            sound_play(SND_SUMMON);
            msg(1, C_BRIGHT_GREEN, "Beschworen!");
        }
        else
            msg(1, C_BRIGHT_RED, "Kein Platz - Mana verloren.");
        update_sight();
        frame();
    } else {
        start_aim(TA_SPELL, i);
    }
}

/* ---------- items ---------- */

#define PICK_MAX 18
static struct { u8 x, y; u16 tile; } picks[PICK_MAX];
static u8 pick_n;

static const char *pick_where(int16_t dx, int16_t dy)
{
    static const char *const NAMES[9] = {"NW", "N", "NO", "W", "hier", "O", "SW", "S", "SO"};
    return NAMES[(dy + 1) * 3 + (dx + 1)];
}

static void pick_gather(void)
{
    const Unit *u = &world.units[active()];
    u8 i, pass;
    pick_n = 0;
    for (pass = 0; pass < 2; pass++)
        for (i = 0; i < world.object_count && pick_n < PICK_MAX; i++) {
            const LocObject *o = &world.objects[i];
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

static void pick_label(u8 i, char *out)
{
    const Unit *u = &world.units[active()];
    int16_t dx, dy;
    if (i == pick_n) {
        sprintf(out, "Alles aufheben");
        return;
    }
    world_delta(&world, u->x, u->y, picks[i].x, picks[i].y, &dx, &dy);
    sprintf(out, "%-14.14s %s", OBJECTS[items_kind_of_tile(picks[i].tile)].name, pick_where(dx, dy));
}

static bool pick_entry(u8 k)
{
    u8 i;
    for (i = 0; i < world.object_count; i++)
        if (world.objects[i].x == picks[k].x && world.objects[i].y == picks[k].y &&
            world.objects[i].tile == picks[k].tile) {
            u8 kind = items_kind_of_tile(picks[k].tile);
            if (!items_pick_up_object(&world, active(), i))
                return FALSE;
            if (kind != NO_ITEM)
                lexicon_see_object(&lex, kind);     /* discovery */
            return TRUE;
        }
    return FALSE;
}

static void do_pickup(void)
{
    u8 got = 0, k;
    s8 pick;
    pick_gather();
    if (pick_n == 0) {
        msg(1, C_BRIGHT_RED, "Nichts aufzuheben.");
        frame();
        return;
    }
    if (pick_n == 1 && picks[0].x == world.units[active()].x && picks[0].y == world.units[active()].y) {
        if (pick_entry(0)) {
            sound_play(SND_PICKUP);
            msg(1, C_BRIGHT_GREEN, "Aufgehoben.");
        } else {
            msg(1, C_BRIGHT_RED, "Zu schwer, kein Platz oder AP.");
        }
        frame();
        return;
    }
    pick = menu_run("Aufheben", "A/C nimmt, B zurueck", pick_n + 1, 0, pick_label);
    if (pick < 0) {
        frame();
        return;
    }
    if ((u8)pick == pick_n) {
        for (k = 0; k < pick_n; k++)
            if (pick_entry(k))
                got++;
    } else if (pick_entry((u8)pick)) {
        got = 1;
    }
    if (got) {
        sound_play(SND_PICKUP);
        msg(1, C_BRIGHT_GREEN, got > 1 ? "Alles aufgehoben, was ging." : "Aufgehoben.");
    }
    else
        msg(1, C_BRIGHT_RED, "Zu schwer, kein Platz oder AP.");
    frame();
}

/* ---------- action menu ---------- */

typedef enum {
    MA_CAST, MA_PICKUP, MA_DROP, MA_WIELD, MA_THROW, MA_FIRE, MA_EAT, MA_READ,
    MA_QUAFF, MA_FILL, MA_MOUNT, MA_TAKEOFF, MA_LAND, MA_USE, MA_LOOK, MA_UNITDONE,
    MA_ENDTURN, MA_SAVE, MA_QUIT, MA_HELP, MA_LEXICON, MA_LOG, MA_MAP, MA_COUNT
} Act;

static const char *const ACT_NAME[MA_COUNT] = {
    "Zaubern", "Aufheben", "Fallen lassen", "Naechstes Objekt", "Werfen", "Schiessen",
    "Essen", "Schriftrolle lesen", "Trinken", "Phiole fuellen", "Auf-/Absteigen",
    "Auffliegen", "Landen", "Tuer/Truhe nutzen", "Schauen", "Einheit fertig", "Zug beenden",
    "Speichern", "Zum Hauptmenue", "Hilfe", "Lexikon", "Meldungen", "Gesamtkarte"};

static u8 menu_acts[MA_COUNT];          /* the actions possible right now, most useful first */
static u8 menu_n;
static u8 last_act = MA_COUNT;          /* preselected next time: A A repeats it */

static void act_label(u8 i, char *out)
{
    u8 a = menu_acts[i];
    if (a == MA_MOUNT && (world.units[active()].flags & UF_RIDDEN))
        sprintf(out, "Absteigen");
    else if (a == MA_MOUNT)
        sprintf(out, "Aufsteigen");
    else
        sprintf(out, "%s", ACT_NAME[a]);
}

static bool near_usable(void)
{
    const Unit *u = &world.units[active()];
    s8 dx, dy;
    for (dy = -1; dy <= 1; dy++)
        for (dx = -1; dx <= 1; dx++) {
            int16_t x = u->x + dx, y = u->y + dy;
            u8 fe;
            if ((dx || dy) && world_wrap(&world, &x, &y)) {
                fe = world_feature(&world, x, y);
                if (fe == FE_DOOR_OPEN || fe == FE_DOOR_CLOSED || fe == FE_DOOR_LOCKED ||
                    fe == FE_CHEST || fe == FE_CHEST_FREE)
                    return TRUE;
            }
        }
    return FALSE;
}

static void build_actions(void)
{
    const Unit *u = &world.units[active()];
    bool hand = u->in_use != NO_ITEM && u->in_use < u->item_count;
    bool caster = ride_actor_kind(u) == CR_WIZARD, book = FALSE;
    u16 i;
    menu_n = 0;
    for (i = 0; i < SPELL_COUNT; i++)
        if (books[OWN_P1].level[i])
            book = TRUE;
    if (caster && book)
        menu_acts[menu_n++] = MA_CAST;
    pick_gather();
    if (pick_n)
        menu_acts[menu_n++] = MA_PICKUP;
    if (u->item_count)
        menu_acts[menu_n++] = MA_WIELD;
    if (near_usable())
        menu_acts[menu_n++] = MA_USE;
    menu_acts[menu_n++] = MA_LOOK;
    if (hand)
        menu_acts[menu_n++] = MA_THROW;
    if (items_can_fire(&world, active()))
        menu_acts[menu_n++] = MA_FIRE;
    if (hand)
        menu_acts[menu_n++] = MA_DROP;
    if (hand) {
        menu_acts[menu_n++] = MA_EAT;
        menu_acts[menu_n++] = MA_READ;
        menu_acts[menu_n++] = MA_FILL;
    }
    menu_acts[menu_n++] = MA_QUAFF;
    menu_acts[menu_n++] = MA_MOUNT;
    if (u->flags & UF_FLYING)
        menu_acts[menu_n++] = MA_LAND;
    else if (u->ap_fly || effect_active(u, EFF_FLYING))
        menu_acts[menu_n++] = MA_TAKEOFF;
    menu_acts[menu_n++] = MA_ENDTURN;
    menu_acts[menu_n++] = MA_MAP;
    menu_acts[menu_n++] = MA_LOG;
    menu_acts[menu_n++] = MA_LEXICON;
    menu_acts[menu_n++] = MA_HELP;
    menu_acts[menu_n++] = MA_SAVE;
    menu_acts[menu_n++] = MA_QUIT;
    g_dbg.acts_n = menu_n;
    for (i = 0; i < menu_n && i < sizeof g_acts; i++)
        g_acts[i] = menu_acts[i];
}

static const char *text_a, *text_b;
static void text_label(u8 i, char *out)
{
    sprintf(out, "%s", i ? text_b : text_a);
}

/* A menu of two prepared lines (the options show their state). */
static s8 menu_run_text(const char *title, const char *hint, u8 n, u8 start, const char *a, const char *b)
{
    text_a = a;
    text_b = b;
    return menu_run(title, hint, n, start, text_label);
}

static const char *const *text_set;
static void text_set_label(u8 i, char *out)
{
    snprintf(out, 28, "%s", text_set[i]);
}

/* A menu of three fixed lines (returns the choice, -1 on cancel). */
static s8 menu_run_text3(const char *title, const char *hint, u8 start, const char *const *set)
{
    text_set = set;
    return menu_run(title, hint, 3, start, text_set_label);
}

static void quit_label(u8 i, char *out)
{
    static const char *const T[3] = {"Speichern, Hauptmenue", "Ohne Speichern beenden", "Weiterspielen"};
    sprintf(out, "%s", T[i]);
}

static void do_action(Act a)
{
    u8 me = active();
    const Unit *u = &world.units[me];
    confirm_end = FALSE;
    switch (a) {
    case MA_CAST:
        do_cast();
        return;
    case MA_PICKUP:
        do_pickup();
        return;
    case MA_DROP:
        if (items_drop(&world, me))
            msg(1, C_BRIGHT_GREEN, "Fallen gelassen.");
        else
            msg(1, C_BRIGHT_RED, "Kein Objekt in der Hand.");
        break;
    case MA_WIELD:
        if (items_cycle(&world, me)) {
            if (u->in_use != NO_ITEM)
                msg(1, C_BRIGHT_GREEN, OBJECTS[u->items[u->in_use]].name);
            else
                msg(1, C_GREY, "Leere Haende.");
        } else if (!world_can_pay(&world, me, ACT_CHANGE)) {
            msg(1, C_BRIGHT_RED, "Zu wenig AP.");
        } else {
            msg(1, C_BRIGHT_RED, "Keine Waffe zum Fuehren.");
        }
        break;
    case MA_THROW:
        if (u->in_use == NO_ITEM)
            msg(1, C_BRIGHT_RED, "Kein Objekt in der Hand.");
        else {
            start_aim(TA_THROW, 0);
            return;
        }
        break;
    case MA_FIRE:
        if (!items_can_fire(&world, me))
            msg(1, C_BRIGHT_RED, "Kein Bogen in der Hand.");
        else {
            start_aim(TA_FIRE, 0);
            return;
        }
        break;
    case MA_EAT:
        if (items_eat(&world, me)) {
            sound_play(SND_EAT);
            msg(1, C_BRIGHT_GREEN, "Gegessen.");
        } else {
            msg(1, C_BRIGHT_RED, "Kein Essen in der Hand.");
        }
        break;
    case MA_READ: {
        const char *txt = items_read(&world, me);
        if (txt)
            msg(1, C_BRIGHT_CYAN, txt);
        else
            msg(1, C_BRIGHT_RED, "Keine Schriftrolle in der Hand.");
        break;
    }
    case MA_QUAFF:
        if (brew_drink_vial(&world, me)) {
            sound_play(SND_DRINK);
            msg(1, C_BRIGHT_GREEN, "Phiole getrunken.");
        } else if (brew_drink(&world, me)) {
            sound_play(SND_DRINK);
            msg(1, C_BRIGHT_GREEN, "Aus dem Kessel getrunken.");
        }
        else
            msg(1, C_BRIGHT_RED, "Nichts zu trinken hier.");
        break;
    case MA_FILL:
        if (brew_fill(&world, me)) {
            sound_play(SND_DRINK);
            msg(1, C_BRIGHT_GREEN, "Phiole gefuellt.");
        }
        else
            msg(1, C_BRIGHT_RED, "Kein Kessel oder keine leere Phiole.");
        break;
    case MA_MOUNT:
        if (u->flags & UF_RIDDEN) {
            if (ride_dismount(&world, me))
            {
                sound_play(SND_FLY);
                msg(1, C_BRIGHT_GREEN, "Abgestiegen.");
            }
            else
                msg(1, C_BRIGHT_RED, "Kein Platz zum Absteigen.");
        } else if (ride_mount_adjacent(&world, me)) {
            turn_revalidate(&turns, &world);
            sound_play(SND_FLY);
            msg(1, C_BRIGHT_GREEN, "Aufgesessen!");
        } else {
            msg(1, C_BRIGHT_RED, "Kein Reittier in Reichweite.");
        }
        break;
    case MA_TAKEOFF:
        if (world_take_off(&world, me)) {
            sound_play(SND_FLY);
            msg(1, C_BRIGHT_GREEN, "Steigt auf.");
        } else if (u->flags & UF_FLYING)
            msg(1, C_BRIGHT_RED, "Du fliegst schon.");
        else if (u->ap_fly == 0 && !effect_active(u, EFF_FLYING))
            msg(1, C_BRIGHT_RED, "Diese Kreatur fliegt nicht.");
        else if (u->ap < ACTIONS[ACT_TAKE_OFF].ap)
            msg(1, C_BRIGHT_RED, "Zu wenig AP zum Aufsteigen.");
        else
            msg(1, C_BRIGHT_RED, "Da fliegt schon einer.");
        break;
    case MA_LAND:
        if (world_land(&world, me)) {
            sound_play(SND_FLY);
            msg(1, C_BRIGHT_GREEN, "Landet.");
        } else if (!(u->flags & UF_FLYING))
            msg(1, C_BRIGHT_RED, "Die Kreatur fliegt nicht.");
        else if (u->ap < ACTIONS[ACT_LAND].ap)
            msg(1, C_BRIGHT_RED, "Zu wenig AP zum Landen.");
        else
            msg(1, C_BRIGHT_RED, "Kein Platz zum Landen.");
        break;
    case MA_USE:
        apply_pending = TRUE;
        msg(1, C_BRIGHT_CYAN, "Benutzen: Richtung?");
        return;
    case MA_LOOK:
        mode = MODE_LOOK;
        look_x = u->x;
        look_y = u->y;
        frame();
        return;
    case MA_UNITDONE:
        if (!turn_units_left(&turns, &world)) {
            end_turn();
            return;
        }
        turn_finish_unit(&turns, &world);
        msg(1, turn_units_left(&turns, &world) ? C_GREY : C_BRIGHT_YELLOW,
            turn_units_left(&turns, &world) ? "" : "Alle fertig - START lang: Zugende.");
        break;
    case MA_ENDTURN:
        end_turn();
        return;
    case MA_HELP:
        book_viewer(help_md, 0);
        frame();
        return;
    case MA_LEXICON:
        lexicon_screen();
        frame();
        return;
    case MA_LOG:
        log_screen();
        frame();
        return;
    case MA_MAP:
        bigmap_screen();
        frame();
        return;
    case MA_SAVE:
        save_game();
        msg(1, C_BRIGHT_GREEN, "Gespeichert.");
        break;
    case MA_QUIT: {
        s8 q = menu_run("Spiel verlassen?", "A/C waehlt, B zurueck", 3, 2, quit_label);
        if (q == 0 || q == 1) {
            if (q == 0)
                save_game();
            quit_game = TRUE;
        } else {
            frame();
        }
        return;
    }
    default:
        return;
    }
    update_sight();
    frame();
}

static void action_menu(void)
{
    u8 i, start = 0;
    s8 pick;
    build_actions();
    for (i = 0; i < menu_n; i++)
        if (menu_acts[i] == last_act)
            start = i;
    pick = menu_run("Aktion", "A/C waehlt, B zurueck", menu_n, start, act_label);
    if (pick < 0) {
        frame();
        return;
    }
    last_act = menu_acts[(u8)pick];
    do_action((Act)last_act);
}

/* The frontend's hooks into the core's turn loop (also needed after a load: the saved Turns hold
 * stale pointers). */
static void bind_game(void)
{
    ai_ctx.books = books;
    ai_ctx.game = &game;
    ai_ctx.profiles = ai_profiles;
    turns.ai = ai_wizard_phase;
    turns.ai_ctx = &ai_ctx;
    turns.on_round = on_round;
    turns.on_ai = NULL;
    turns.on_phase = NULL;
    turns.round_ctx = &game;
}

/* The game state is complete: start showing it. */
static void begin_play(const char *welcome)
{
    end_pending = game_ended = confirm_end = apply_pending = quit_game = FALSE;
    foe_x = -1;
    alerts_reset();
    mode = MODE_PLAY;
    view_set_cursor(-100, -100, NO_CURSOR);   /* the cursor is a sprite (render_cursor); NO_CURSOR spares a per-field check */
    render_set_world(&world);
    view_invalidate();
    ui_clear_panel_rows(0, 27);
    frame();
    msg(1, C_BRIGHT_YELLOW, welcome);
}

/* ---------- saving (cartridge SRAM) ----------
 * One slot. The live state is streamed to the SRAM and back, no copy in RAM: a SaveGame plus its
 * blob would not fit next to the game. Layout: magic, version, scenario, body length, body, sum.
 * The magic is written last, so an interrupted save leaves no valid slot. Same build only. */

#define SAVE_MAGIC "LOCM"
#define SAVE_VERSION 2             /* 2: World grew (remains, noises, AP factor) */
#define SAVE_HEADER 8
#define SAVE_AREAS_MD 4

static u32 sram_pos, sram_sum;

static void sput(const void *p, u16 n)
{
    const u8 *b = p;
    while (n--) {
        u8 v = *b++;
        SRAM_writeByte(sram_pos++, v);
        sram_sum = ((sram_sum << 5) | (sram_sum >> 27)) ^ v;
    }
}

static void sget(void *p, u16 n)
{
    u8 *b = p;
    while (n--) {
        u8 v = SRAM_readByte(sram_pos++);
        *b++ = v;
        sram_sum = ((sram_sum << 5) | (sram_sum >> 27)) ^ v;
    }
}

static u16 save_body_len(void)
{
    return sizeof world + sizeof turns + sizeof game + sizeof books + 1 + sizeof p1_sight.explored +
           1 + SAVE_AREAS_MD * sizeof(Area) + sizeof wizard_slots[0] + sizeof ai_profiles;
}

static u8 scenario_index(void)
{
    return cur_sc ? (u8)(cur_sc - SCENARIOS) : 0;
}

static bool save_game(void)
{
    Area areas[SAVE_AREAS_MD];
    u8 area_count, idx = scenario_index(), loads = 0xFF;
    u16 len = save_body_len();
    memset(areas, 0, sizeof areas);
    area_count = area_export(areas, SAVE_AREAS_MD);
    SRAM_enable();
    SRAM_writeByte(0, 0);                       /* no valid slot while writing */
    sram_pos = SAVE_HEADER;
    sram_sum = 0;
    sput(&world, sizeof world);
    sput(&turns, sizeof turns);
    sput(&game, sizeof game);
    sput(books, sizeof books);
    sput(&loads, 1);
    sput(p1_sight.explored, sizeof p1_sight.explored);
    sput(&area_count, 1);
    sput(areas, sizeof areas);
    sput(&wizard_slots[0], sizeof wizard_slots[0]);
    sput(ai_profiles, sizeof ai_profiles);
    SRAM_writeLong(sram_pos, sram_sum);          /* the sum, then the header */
    SRAM_writeByte(1, SAVE_VERSION);
    SRAM_writeByte(2, idx);
    SRAM_writeByte(3, len >> 8);
    SRAM_writeByte(4, len & 255);
    SRAM_writeByte(5, 0x4D);
    SRAM_writeByte(6, 0x4D);
    SRAM_writeByte(0, 'L');                      /* first byte of the magic, last */
    SRAM_disable();
    save_known_valid = TRUE;
    return TRUE;
}

/* Header ok and body sum matches? Returns the scenario index or -1. */
static s8 save_check(void)
{
    u16 len;
    u32 stored;
    s8 idx;
    u16 i;
    SRAM_enableRO();
    if (SRAM_readByte(0) != 'L' || SRAM_readByte(1) != SAVE_VERSION || SRAM_readByte(5) != 0x4D ||
        SRAM_readByte(6) != 0x4D) {
        SRAM_disable();
        return -1;
    }
    idx = SRAM_readByte(2);
    len = ((u16)SRAM_readByte(3) << 8) | SRAM_readByte(4);
    if (len != save_body_len() || idx < 0 || idx >= SCENARIO_COUNT) {
        SRAM_disable();
        return -1;
    }
    sram_sum = 0;
    for (i = 0; i < len; i++)
        sram_sum = ((sram_sum << 5) | (sram_sum >> 27)) ^ SRAM_readByte(SAVE_HEADER + i);
    stored = SRAM_readLong(SAVE_HEADER + len);
    SRAM_disable();
    return stored == sram_sum ? idx : -1;
}

/* Restore the saved game into the live state; false when there is no valid slot. */
static bool load_game(void)
{
    Area areas[SAVE_AREAS_MD];
    u8 area_count, loads, idx;
    s8 chk = save_check();
    if (chk < 0)
        return FALSE;
    idx = chk;
    SRAM_enableRO();
    sram_pos = SAVE_HEADER;
    sram_sum = 0;
    sget(&world, sizeof world);
    sget(&turns, sizeof turns);
    sget(&game, sizeof game);
    sget(books, sizeof books);
    sget(&loads, 1);
    sight_init(&p1_sight, OWN_P1);
    sget(p1_sight.explored, sizeof p1_sight.explored);
    sget(&area_count, 1);
    sget(areas, sizeof areas);
    sget(&wizard_slots[0], sizeof wizard_slots[0]);
    sget(ai_profiles, sizeof ai_profiles);
    SRAM_disable();
    cur_sc = &SCENARIOS[idx];
    area_reset();
    area_import(areas, area_count > SAVE_AREAS_MD ? SAVE_AREAS_MD : area_count);
    bind_game();
    view_set_portal(game.portal_open ? game.portal_x : -1, game.portal_y);
    view_set_sight(&p1_sight);
    brew_register_map_cauldrons(&world);
    update_sight();
    return TRUE;
}

static bool save_exists(void)
{
    save_known_valid = save_check() >= 0;
    return save_known_valid;
}

/* ---------- the wizard and the settings (cartridge SRAM, second area) ----------
 * The designed wizard and the sound switches outlive a game. They live at 16 KB in the SRAM, behind the
 * game slot: magic, version, flags, the wizard, a sum; the magic is written last. */

#define SET_BASE 16384
#define SET_VERSION 2

static void wizards_reset(void)
{
    u8 i;
    for (i = 0; i < WIZARD_SLOTS; i++)
        wizard_slot_reset(i);
}

static u16 settings_len(void)
{
    return 1 + sizeof wizard_slots[0] + sizeof lex;
}

static void wizards_save(void)
{
    u8 flags = (sound_on ? 1 : 0) | (music_on ? 2 : 0) | (auto_end ? 4 : 0);
    u16 len = settings_len();
    SRAM_enable();
    SRAM_writeByte(SET_BASE, 0);
    sram_pos = SET_BASE + 8;
    sram_sum = 0;
    sput(&flags, 1);
    sput(&wizard_slots[0], sizeof wizard_slots[0]);
    sput(&lex, sizeof lex);
    SRAM_writeLong(sram_pos, sram_sum);
    SRAM_writeByte(SET_BASE + 1, SET_VERSION);
    SRAM_writeByte(SET_BASE + 2, len >> 8);
    SRAM_writeByte(SET_BASE + 3, len & 255);
    SRAM_writeByte(SET_BASE + 4, 0x57);
    SRAM_writeByte(SET_BASE + 5, 0x57);
    SRAM_writeByte(SET_BASE, 'L');
    SRAM_disable();
}

/* Load the designed wizard and the switches; a stock wizard when there is none (or it is damaged). */
static void wizards_load(void)
{
    static Wizard w;
    u16 len = settings_len(), i;
    u8 flags;
    wizards_reset();
    lexicon_init(&lex);
    SRAM_enableRO();
    if (SRAM_readByte(SET_BASE) != 'L' || SRAM_readByte(SET_BASE + 1) != SET_VERSION ||
        SRAM_readByte(SET_BASE + 4) != 0x57 || SRAM_readByte(SET_BASE + 5) != 0x57 ||
        (((u16)SRAM_readByte(SET_BASE + 2) << 8) | SRAM_readByte(SET_BASE + 3)) != len) {
        SRAM_disable();
        return;
    }
    sram_sum = 0;
    for (i = 0; i < len; i++)
        sram_sum = ((sram_sum << 5) | (sram_sum >> 27)) ^ SRAM_readByte(SET_BASE + 8 + i);
    if (SRAM_readLong(SET_BASE + 8 + len) != sram_sum) {
        SRAM_disable();
        return;
    }
    sram_pos = SET_BASE + 8;
    sget(&flags, 1);
    sget(&w, sizeof w);
    sget(&lex, sizeof lex);
    SRAM_disable();
    if (!wizard_valid(&w))
        return;
    wizard_slots[0] = w;
    sound_on = (flags & 1) != 0;
    music_on = (flags & 2) != 0;
    auto_end = (flags & 4) != 0;
}

/* A wizard without any spell can do nothing: offer the standard set (GDD, M5). */
static void offer_standard_set(void)
{
    u16 k, sum = 0;
    static const char *const T[2] = {"Ja, das Standardset", "Nein, ohne Zauber"};
    (void)T;
    for (k = 0; k < SPELL_COUNT; k++)
        sum += wizard_slots[0].book.level[k];
    if (sum == 0)
        wizard_apply_standard_set(&wizard_slots[0]);
}

/* ---------- the designer (GDD 7.3) ---------- */

static void wiz_line(char *out)
{
    const Wizard *w = &wizard_slots[0];
    snprintf(out, 28, "%.10s St.%d  XP %d", w->name, w->level, w->xp);
}

#define PAGE_ATTRS 0
#define PAGE_SPELLS 1
#define PAGE_CREATURES 2

static void designer_attrs(void)
{
    static const char *const NAMES[WA_COUNT + 2] = {"Kampf", "Verteidigung", "Magieresist.",
                                                    "Konstitution", "Ausdauer", "Mana", "Aktionspunkte"};
    Wizard *w = &wizard_slots[0];
    u8 cur = 0, i, redraw = TRUE;
    char line[28];
    pad_pressed();
    ui_overlay_clear();
    ui_text(0, 0, C_BRIGHT_YELLOW, "Attribute", 27);
    ui_text(0, 26, C_GREY, "</>: -/+  A: +  B zurueck", 27);
    for (;;) {
        u16 pressed = pad_pressed();
        if (pressed & (BUTTON_B | BUTTON_START)) {
            sound_play(SND_BACK);
            break;
        }
        if (pressed & BUTTON_UP) {
            cur = cur ? cur - 1 : WA_COUNT + 1;
            sound_play(SND_MENU);
            redraw = TRUE;
        }
        if (pressed & BUTTON_DOWN) {
            cur = (cur + 1) % (WA_COUNT + 2);
            sound_play(SND_MENU);
            redraw = TRUE;
        }
        if (pressed & (BUTTON_RIGHT | BUTTON_A | BUTTON_C)) {
            sound_play(SND_CONFIRM);
            if (cur == WA_COUNT)
                wizard_mana_raise(w);
            else if (cur == WA_COUNT + 1)
                wizard_ap_raise(w);
            else
                wizard_raise(w, (WizardAttr)cur);
            redraw = TRUE;
        }
        if (pressed & BUTTON_LEFT) {
            sound_play(SND_BACK);
            if (cur == WA_COUNT)
                wizard_mana_lower(w);
            else if (cur == WA_COUNT + 1)
                wizard_ap_lower(w);
            else
                wizard_lower(w, (WizardAttr)cur);
            redraw = TRUE;
        }
        if (redraw) {
            wiz_line(line);
            ui_text(0, 1, C_BRIGHT_WHITE, line, 27);
            for (i = 0; i < WA_COUNT + 2; i++) {
                u8 val = i < WA_COUNT ? wizard_attr(w, (WizardAttr)i) : i == WA_COUNT ? w->mana_max : w->ap;
                u8 cost = i < WA_COUNT ? wizard_attr_cost((WizardAttr)i, val)
                                       : i == WA_COUNT ? wizard_mana_cost(w) : wizard_ap_cost(w);
                snprintf(line, sizeof line, "%c %-13.13s %3d %3dXP", i == cur ? '>' : ' ', NAMES[i], val, cost);
                ui_text(0, 3 + i, i == cur ? C_BRIGHT_YELLOW : C_BRIGHT_WHITE, line, 27);
            }
            redraw = FALSE;
        }
        SYS_doVBlankProcess();
    }
    ui_overlay_close();
}

/* Spells or summons to buy: > moves, right/A buys a level, left sells one back, B leaves. */
static void designer_shop(u8 page)
{
    Wizard *w = &wizard_slots[0];
    u8 shop[SPELL_COUNT], n = 0, cur = 0, top = 0, rows = 18, i, redraw = TRUE;
    char line[28];
    for (i = 0; i < SPELL_COUNT; i++)
        if ((page == PAGE_CREATURES) == (SPELLS[i].category == SPC_SUMMON))
            shop[n++] = i;
    pad_pressed();
    ui_overlay_clear();
    ui_text(0, 0, C_BRIGHT_YELLOW, page == PAGE_CREATURES ? "Kreaturen" : "Zauber", 27);
    ui_text(0, 26, C_GREY, "A/>: kaufen  <: zurueck  B", 27);
    for (;;) {
        u16 pressed = pad_pressed();
        if (pressed & (BUTTON_B | BUTTON_START)) {
            sound_play(SND_BACK);
            break;
        }
        if (pressed & BUTTON_UP) {
            cur = cur ? cur - 1 : n - 1;
            sound_play(SND_MENU);
            redraw = TRUE;
        }
        if (pressed & BUTTON_DOWN) {
            cur = cur + 1 < n ? cur + 1 : 0;
            sound_play(SND_MENU);
            redraw = TRUE;
        }
        if (pressed & (BUTTON_RIGHT | BUTTON_A | BUTTON_C)) {
            wizard_spell_raise(w, shop[cur]);
            sound_play(SND_CONFIRM);
            redraw = TRUE;
        }
        if (pressed & BUTTON_LEFT) {
            wizard_spell_lower(w, shop[cur]);
            sound_play(SND_BACK);
            redraw = TRUE;
        }
        if (redraw) {
            if (cur < top)
                top = cur;
            if (cur >= top + rows)
                top = cur - rows + 1;
            wiz_line(line);
            ui_text(0, 1, C_BRIGHT_WHITE, line, 27);
            ui_text(0, 2, C_BRIGHT_YELLOW, "  Name            Anz Preis", 27);
            for (i = 0; i < rows; i++) {
                u8 k = top + i;
                if (k < n) {
                    u16 cost = wizard_spell_next_cost(w, shop[k]);
                    snprintf(line, sizeof line, "%c %-16.16s %2d %4d", k == cur ? '>' : ' ', SPELLS[shop[k]].name,
                             w->book.level[shop[k]], cost);
                    ui_text(0, 3 + i, k == cur ? C_BRIGHT_YELLOW : (cost && w->xp >= cost ? C_BRIGHT_WHITE : C_GREY),
                            line, 27);
                } else {
                    ui_text(0, 3 + i, C_BLACK, "", 27);
                }
            }
            {   /* what the selection costs to cast */
                u8 lv = w->book.level[shop[cur]];
                snprintf(line, sizeof line, "Mana: %d (Stufe %d)", spell_cast_mana(shop[cur], lv ? lv : 1), lv ? lv : 1);
                ui_text(0, 22, C_BRIGHT_CYAN, line, 27);
            }
            redraw = FALSE;
        }
        SYS_doVBlankProcess();
    }
    ui_overlay_close();
}

static void page_label(u8 i, char *out)
{
    static const char *const P[3] = {"Attribute verteilen", "Zauber erlernen", "Kreaturen beschwoeren"};
    sprintf(out, "%s", P[i]);
}

static void designer(void)
{
    char title[28];
    s8 pick = 0;
    for (;;) {
        wiz_line(title);
        pick = menu_run(title, "A/C waehlt, B zurueck", 3, (u8)(pick < 0 ? 0 : pick), page_label);
        if (pick < 0)
            break;
        if (pick == PAGE_ATTRS)
            designer_attrs();
        else
            designer_shop(pick);
        wizards_save();
    }
}

static void options(void)
{
    s8 pick = 0;
    for (;;) {
        char l0[28], l1[28], l2[28];
        sprintf(l0, "Ton: %s", sound_on ? "an" : "aus");
        sprintf(l1, "Musik: %s", music_on ? "an" : "aus");
        sprintf(l2, "Rundenwechsel: %s", auto_end ? "auto" : "von Hand");
        const char *const set[3] = {l0, l1, l2};
        pick = menu_run_text3("Optionen", "A/C schaltet um, B zurueck", (u8)(pick < 0 ? 0 : pick), set);
        if (pick < 0)
            break;
        if (pick == 0)
            sound_on = !sound_on;
        else if (pick == 2)
            auto_end = !auto_end;
        else {
            music_on = !music_on;
            if (music_on)
                music_start(SONG_TITLE);
            else
                music_stop();
        }
        wizards_save();
        sound_play(SND_CONFIRM);
    }
}

/* ---------- start ---------- */

static void new_game(const Scenario *sc)
{
    const u8 *map = sc->map;
    u16 map_len = *sc->map_len;
    cur_sc = sc;
    if (sc->variants) {                 /* a terrain variant, never twice in a row */
        static u8 last = 0xFF;
#ifdef LOC_MD_FIXED_SEED
        u8 v = 0;                       /* reproducible runs for tests */
        (void)last;
#else
        u8 v = (u8)(vtimer & (MCL_VARIANTS - 1));
        if (v == last)
            v = (v + 1) & (MCL_VARIANTS - 1);
        last = v;
#endif
        map = mcl_variant[v];
        map_len = mcl_variant_len[v];
    }
    area_reset();
    offer_standard_set();
    world_load_bin(&world, map, map_len);
    spellbook_load(books, sc->scn, *sc->scn_len);
    brew_register_map_cauldrons(&world);
#ifdef LOC_MD_FIXED_SEED
    turn_init(&turns, &world, 0x4C4F43UL, 1u << OWN_P1);      /* reproducible runs for tests */
#else
    turn_init(&turns, &world, 0x4C4F43UL ^ ((u32)vtimer << 7), 1u << OWN_P1);
#endif
    if (!sc->tutorial)
        populate_scenario(&world, &turns.rng);
    turns.wildlife = !sc->tutorial;
    game_init(&game, world.portal_x, world.portal_y, world.portal_rmin, world.portal_rmax, &turns.rng);
    game_set_portal_span(&game, world.portal_span);
    game_set_wizard_level(&game, OWN_P1, wizard_slots[0].level);
    if (ai_scenario_load(&world, ai_profiles, sc->scn, *sc->scn_len))
        ai_profile_apply(ai_profiles, &world, &game, OWN_P2);
    game_new_round(&game, turns.round);
    view_set_portal(game.portal_open ? game.portal_x : -1, game.portal_y);
    bind_game();
    sight_init(&p1_sight, OWN_P1);
    update_sight();
    view_set_sight(&p1_sight);
    wizard_apply_to_world(&wizard_slots[0], &world, active());
    memcpy(&books[OWN_P1], wizard_book(&wizard_slots[0]), sizeof(Spellbook));
    update_sight();
    tutorial_on = sc->tutorial;
    if (tutorial_on) {
        turns.round1_lock = FALSE;      /* the tutorial teaches movement at once */
        tutorial_init(&tut, &world);
    }
    begin_play(tutorial_on ? "Tutorial: folge der Hinweiszeile."
                           : "Willkommen! A Menue, C Einheit, START fertig, START lang Zugende.");
    if (tutorial_on)
        book_viewer(tutorial_md, 0);
    frame();
}

/* ---------- input ---------- */

static u16 hash16(u16 h, u16 v)
{
    return (u16)((h * 31u + v) ^ (h >> 7));
}

static void debug_update(void)
{
    const Unit *u = &world.units[active()];
    u16 h = 0;
    u8 i;
    g_dbg.magic = 0x4C44;
    g_dbg.frames++;
    g_dbg.round = turns.round;
    g_dbg.mode = mode;
    g_dbg.units = world.unit_count;
    g_dbg.scenario = scenario_index();
    g_dbg.x = u->x;
    g_dbg.y = u->y;
    g_dbg.ap = u->ap;
    g_dbg.hp = u->con;
    g_dbg.mana = u->mana;
    g_dbg.items = u->item_count;
    g_dbg.spell = target_spell;
    g_dbg.flags = (game_ended ? 1 : 0) | (end_pending ? 2 : 0) | (quit_game ? 4 : 0) |
                  (apply_pending ? 8 : 0) | (save_known_valid ? 16 : 0);
    for (i = 0; i < world.unit_count; i++) {
        const Unit *w = &world.units[i];
        h = hash16(h, (u16)((w->x << 8) | w->y));
        h = hash16(h, (u16)((w->con << 8) | w->ap));
    }
    for (i = 0; i < world.object_count; i++)
        h = hash16(h, (u16)((world.objects[i].x << 8) | world.objects[i].y));
    g_dbg.hash = h;
}

#ifdef LOC_MD_TEST_CMD
/* Test builds only (tools: md/test/e2e.py): the emulator test writes a command into g_cmd through the
 * debugger and the game executes it in its next frame, to set up situations the world does not offer. */
volatile u16 g_cmd[5];                  /* [0] op (cleared when done), [1..4] arguments */

static void test_commands(void)
{
    Unit *u = &world.units[active()];
    u16 op = g_cmd[0];
    s16 dx = (s16)g_cmd[2], dy = (s16)g_cmd[3];
    if (!op)
        return;
    switch (op) {
    case 1:                             /* enemy of kind a1 at (+a2, +a3) */
        world_spawn_unit(&world, OWN_P2, g_cmd[1], u->x + dx, u->y + dy);
        break;
    case 2:                             /* give item a1 */
        if (u->item_count < 6)
            u->items[u->item_count++] = g_cmd[1];
        break;
    case 3:                             /* teleport to (a1, a2) */
        u->x = g_cmd[1];
        u->y = g_cmd[2];
        break;
    case 4:                             /* full AP, stamina, constitution, mana */
        u->ap = u->ap_max;
        u->sta = u->sta_max;
        u->con = u->con_max;
        u->mana = u->mana_max;
        break;
    case 5:                             /* feature a1 at (+a2, +a3) */
        world.feature[u->y + dy][u->x + dx] = g_cmd[1];
        world_map_changed(&world);
        break;
    case 6:                             /* an open portal at (+a1, +a2) */
        world.portal_x = game.portal_x = u->x + (s16)g_cmd[1];
        world.portal_y = game.portal_y = u->y + (s16)g_cmd[2];
        game.portal_open = TRUE;
        game.portal_closed = FALSE;
        view_set_portal(game.portal_x, game.portal_y);
        break;
    case 7:                             /* the wizard dies */
        world_kill_unit(&world, active(), CR_GOBLIN, OWN_P2, FALSE);
        settle();
        break;
    case 8:                             /* object of kind a1 under the unit */
        if (world.object_count < MAX_OBJECTS) {
            LocObject *o = &world.objects[world.object_count++];
            o->x = u->x;
            o->y = u->y;
            o->tile = OBJECTS[g_cmd[1]].tile;
        }
        break;
    case 9:                             /* an own unit of kind a1 at (+a2, +a3) */
        world_spawn_unit(&world, OWN_P1, g_cmd[1], u->x + dx, u->y + dy);
        break;
    case 10:                            /* empty the map around: remove every unit but the active one */
        {
            u8 i, keep = active();
            for (i = world.unit_count; i-- > 0;)
                if (i != keep)
                    world_remove_unit(&world, i);
            turn_revalidate(&turns, &world);
        }
        break;
    }
    update_sight();
    frame();
    g_cmd[0] = 0;
}
#endif

/* State of the running game loop (one frame at a time, so tests can drive it). */
static struct {
    Chord chord;
    u16 blink, anim, start_t;
    u8 phase;
    bool start_down, start_fired;
} pl;

static void play_init(void)
{
    memset(&pl, 0, sizeof pl);
    chord_init(&pl.chord, WINDOW_CS, DELAY_CS, REPEAT_CS);
}

/* Everything the loop does in one frame except waiting for the V-Blank. */
static void play_frame(void)
{
    static const struct { u16 button; u8 arrow; } DIRS[4] = {
        {BUTTON_UP, ARROW_UP}, {BUTTON_DOWN, ARROW_DOWN},
        {BUTTON_LEFT, ARROW_LEFT}, {BUTTON_RIGHT, ARROW_RIGHT}};
    PadEv ev;
    u16 pressed = 0;
    u8 m = 0, k;
    bool start_tap = FALSE;

    g_loop++;
#ifdef LOC_MD_TEST_CMD
    test_commands();
#endif
    if (auto_end && !game_ended && !end_pending && mode == MODE_PLAY && turns.phase == OWN_P1 &&
        world.unit_count && !turn_units_left(&turns, &world)) {      /* D72 */
        msg(1, C_BRIGHT_GREEN, "Alle fertig - die Runde endet.");
        end_turn();
    }
    while (ev_pop(&ev)) {
        pressed |= ev.changed & ev.state;
        if (ev.changed & BUTTON_START) {
            if (ev.state & BUTTON_START) {
                pl.start_down = TRUE;
                pl.start_fired = FALSE;
                pl.start_t = ev.cs;
            } else {
                start_tap = pl.start_down && !pl.start_fired;
                pl.start_down = FALSE;
            }
        }
        if (!game_ended)
            for (k = 0; k < 4; k++)
                if (ev.changed & DIRS[k].button)
                    m |= chord_key(&pl.chord, DIRS[k].arrow, (ev.state & DIRS[k].button) != 0, ev.cs);
    }
    if (!game_ended) {
        m |= chord_poll(&pl.chord, clock_cs());
        if (m) {
            confirm_end = FALSE;
            do_step(m);
        }
        if (pl.start_down && !pl.start_fired && mode == MODE_PLAY &&
            (u16)(clock_cs() - pl.start_t) >= HOLD_CS) {
            pl.start_fired = TRUE;                  /* START held: end the turn */
            end_turn();
        }
        if (mode != MODE_PLAY) {
            if (pressed & BUTTON_B) {
                mode = MODE_PLAY;
                msg(1, C_GREY, "");
                msg(2, C_GREY, "");
                frame();
            } else if ((pressed & BUTTON_A) && mode == MODE_TARGET && target_kind == TA_SPELL) {
                target_air = !target_air;          /* CAST-A / CAST-G (F8) */
                frame();
            } else if ((pressed & (BUTTON_C | BUTTON_A)) && mode == MODE_TARGET) {
                aim_confirm();
            } else if ((pressed & BUTTON_A) && mode == MODE_LOOK) {
                mode = MODE_PLAY;
                msg(1, C_GREY, "");
                frame();
            }
        } else {
            if (pressed & BUTTON_A) {
                action_menu();
            } else if (pressed & BUTTON_X) {
                do_action(MA_CAST);
            } else if (pressed & BUTTON_Y) {
                do_action(MA_PICKUP);
            } else if (pressed & BUTTON_Z) {
                do_action(MA_LOOK);
            } else if (pressed & BUTTON_C) {
                confirm_end = FALSE;
                apply_pending = FALSE;
                turn_next_unit(&turns, &world, FALSE);
                if (tutorial_on)
                    tutorial_notify(&tut, TUT_SWITCH);
                msg(1, C_GREY, "");
                if (!turn_units_left(&turns, &world))
                    msg(1, C_BRIGHT_YELLOW, "Alle fertig - START lang: Zugende.");
                frame();
            } else if (pressed & BUTTON_B) {
                if (apply_pending) {
                    apply_pending = FALSE;
                    msg(1, C_GREY, "");
                }
            } else if (start_tap) {
                do_action(MA_UNITDONE);             /* all units done: this ends the turn */
            }
        }
        if (end_pending)
            end_screen();
    }
    if (++pl.blink >= BLINK_FRAMES) {
        pl.blink = 0;
        cursor_on = !cursor_on;
        if (!game_ended && mode == MODE_PLAY)
            draw_cursor();
    }
    if (++pl.anim >= ANIM_FRAMES) {
        pl.anim = 0;
        if (!game_ended) {
            view_animate(++pl.phase);
            render_fields();
        }
    }
    debug_update();
}

static void play(const Scenario *sc, bool load)
{
    music_stop();                       /* the game itself is quiet */
    render_init();
    ui_reset();
    play_init();
    view_set_idle(TRUE);
    if (load) {
        if (!load_game())
            return;
        begin_play("Spielstand geladen.");
    } else {
        new_game(sc);
    }
    g_dbg.screen = SCREEN_PLAY;
    while (!quit_game && !game_ended) {
        play_frame();
        SYS_doVBlankProcess();
    }
}

/* ---------- title and main menu ---------- */

/* Load a 320x224 picture (tiles, tilemap, two palette lines) onto plane B. */
static void show_title(void)
{
    VDP_setEnable(FALSE);
    render_init();                      /* planes, sprites, palettes */
    ui_reset();
    PAL_setColors(0, (const u16 *)title_pal, 32, CPU);
    VDP_loadTileData((const u32 *)title_tiles, TILE_USER_INDEX, TITLE_TILES, CPU);
    VDP_setTileMapDataRectEx(BG_B, (const u16 *)title_map, TILE_USER_INDEX, 0, 0, 40, 28, 40, CPU);
    VDP_setHorizontalScroll(BG_B, 0);
    VDP_setVerticalScroll(BG_B, 0);
    VDP_setEnable(TRUE);
}

/* The title picture; START (or A/C) goes on. */
static void title_screen(void)
{
    u16 frames = 0;
    g_dbg.screen = SCREEN_TITLE;
    show_title();
    music_start(SONG_TITLE);
    pad_pressed();
    for (;;) {
        if (pad_pressed() & (BUTTON_START | BUTTON_A | BUTTON_C))
            break;
        if ((frames++ & 31) == 0)
            ui_text(8, 27, (frames & 32) ? C_BRIGHT_YELLOW : C_BRIGHT_WHITE, "START DRUECKEN", 24);
        if (frames == 1)
            ui_text(0, 27, C_GREY, "MD-Port", 8);
        SYS_doVBlankProcess();
    }
}

/* D75 (upstream): only world 1 is playable for now; the other two are shown as locked. Test builds keep
 * them open (LOC_MD_ALL_WORLDS makes a normal build do the same). */
#if defined(LOC_MD_TEST_CMD) || defined(LOC_MD_ALL_WORLDS)
#define WORLD_LOCKED(i) 0
#else
#define WORLD_LOCKED(i) ((i) == 1 || (i) == 2)
#endif

static void scenario_label(u8 i, char *out)
{
    static const char *const EXTRA[7] = {"Tutorial", "Spielstand laden", "Zauberer gestalten",
                                         "Zauberer zuruecksetzen", "Optionen", "Hilfe", "Lexikon"};
    if (i < 3)
        sprintf(out, WORLD_LOCKED(i) ? "%d %.20s (bald)" : "%d %s", i + 1, SCENARIOS[i].title);
    else
        sprintf(out, "%s", EXTRA[i - 3]);
}

void game_run(void)
{
    JOY_init();
    SYS_setVBlankCallback(on_vblank);
    sound_init();
    render_init();
    ui_init();
    wizards_load();
    for (;;) {
        s8 pick = 0;
        title_screen();
        for (;;) {
            VDP_clearPlane(BG_B, TRUE);         /* the picture is done */
            g_dbg.screen = SCREEN_MENU;
            ui_reset();
            render_cursor(0, 0, 0, FALSE);
            pick = menu_run("LORDS OF CHAOS", "A/C waehlt, B zurueck", 10, (u8)(pick < 0 ? 0 : pick),
                            scenario_label);
            if (pick < 0)
                break;                          /* back to the title */
            if (pick == 5) {
                designer();
                continue;
            }
            if (pick == 6) {
                static const char *const Q[2] = {"Ja, zuruecksetzen", "Nein"};
                if (menu_run_text("Zauberer zuruecksetzen?", "A/C waehlt, B zurueck", 2, 1, Q[0], Q[1]) == 0) {
                    wizards_reset();
                    wizards_save();
                }
                continue;
            }
            if (pick == 7) {
                options();
                continue;
            }
            if (pick == 8) {
                book_viewer(help_md, 0);
                continue;
            }
            if (pick == 9) {
                lexicon_screen();
                continue;
            }
            if (pick < 3 && WORLD_LOCKED(pick)) {
                static const char *const L[] = {"Diese Welt ist noch nicht", "spielbar. Zuerst kommt",
                                                "Welt 1."};
                text_page(SCENARIOS[pick].title, L, 3);
                continue;
            }
            if (pick == 4) {                    /* continue the saved game */
                if (save_exists()) {
                    play(NULL, TRUE);
                } else {
                    static const char *const L[] = {"Es gibt noch keinen Spielstand.", "",
                                                    "Das Spiel speichert am Ende",
                                                    "jeder Runde selbst."};
                    text_page("Spielstand laden", L, 4);
                    continue;
                }
            } else {
                play(&SCENARIOS[pick == 3 ? TUTORIAL_INDEX : (u8)pick], FALSE);
                wizards_save();                 /* what the game learned (the lexicon) */
            }
            ui_reset();
            render_init();                      /* a clean screen for the menu */
            music_start(SONG_TITLE);
        }
    }
}
