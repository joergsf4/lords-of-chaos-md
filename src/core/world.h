/*
 * World state: map layers (floor, decor, feature), units and objects.
 * Platform-free; the view (view.h) turns it into tile layers for drawing.
 */
#ifndef LOC_WORLD_H
#define LOC_WORLD_H

#include <stdbool.h>
#include <stdint.h>

#include "map_def.h"

#define MAP_MAX_W 46   /* D64: Level 1 is 46x46, smaller maps fit inside */
#define MAP_MAX_H 46
#define MAX_UNITS 32
#define MAX_OBJECTS 64
#define WORLD_DISTURB 6
#define NO_UNIT 0xFF
#define MAX_KILLS 16

typedef enum {
    FL_STONE, FL_WOOD, FL_GRASS, FL_PATH, FL_TALL_GRASS, FL_FOREST, FL_MAGIC_WOOD,
    FL_SHADOW_WOOD, FL_SWAMP, FL_WATER, FL_RUBBLE, FL_BRIDGE /* D54 */, FL_COUNT
} Floor;
typedef enum { DE_NONE, DE_RUG, DE_PENTACLE, DE_FLOWERS, DE_MUSHROOMS /* D54 */ } Decor;
typedef enum {
    FE_NONE, FE_WALL, FE_DOOR_CLOSED, FE_DOOR_OPEN, FE_BED, FE_BOOKSHELF,
    FE_CANDLE, FE_CAULDRON, FE_TABLE, FE_CHAIR, FE_DRAWERS, FE_CHEST, FE_TREE,
    FE_ROCK, FE_DOOR_LOCKED /* C2 */, FE_CHEST_FREE /* C1: opens without a key */,
    FE_WINDOW /* D54: wall you can see through */, FE_FENCE /* D54: low, see-through */,
    /* D61: the leaf of an open door, named after the field edge it
     * stands on; blocks neither movement nor sight (D76) */
    FE_LEAF_N, FE_LEAF_E, FE_LEAF_S, FE_LEAF_W,
    FE_COUNT
} Feature;

/* Status flags shown as panel icons (PM 11). */
enum { UF_UNDEAD = 1, UF_FLYING = 2, UF_MOUNT = 4, UF_WOUNDED = 8,
       UF_INVISIBLE = 16, UF_MAGIC_WEAPON = 32 /* enchanted (M4b Enchant) */,
       /* 64 is UF_RIDDEN (ride.h). The reaction of D29 lives in
        * Unit.reacted: it shared bit 64 with UF_RIDDEN, and the round
        * reset dropped riders out of the world (wizard "died"). */
       UF_ENGAGED = 128 /* bound in melee until its owner's phase ends (GDD 6) */ };

/* Brewing cauldron on a field (M4c, GDD 7.2). */
#define CAULDRONS_MAX 4
typedef struct Cauldron {
    uint8_t x, y;
    uint8_t potion;   /* SP_* potion spell it holds, 0xFF = empty */
    uint8_t doses;    /* draughts left */
    uint8_t level;    /* brewed spell level: strength and duration (F1) */
} Cauldron;

/* Timed effect on a unit (M4b, D22/F1): kind, strength, rounds left. */
typedef enum {
    EFF_SHIELD, EFF_PROTECT, EFF_STRENGTH, EFF_INVISIBLE, EFF_SPEED,
    EFF_FLYING, EFF_MAGIC_WEAPON
} EffectKind;
#define UNIT_EFFECTS 4
typedef struct {
    uint8_t kind;    /* EffectKind */
    uint8_t power;
    uint8_t rounds;
} Effect;

/* Remains of the dead (D70): a skeleton on the field where a creature
 * died; the look mode names what it was. A ring, the oldest goes first. */
#define REMAINS_MAX 16
typedef struct {
    uint8_t x, y;
    uint8_t kind;   /* CreatureKind of the dead */
    uint8_t owner;
} Remains;

/* Noises the player may hear (D69): fights, spells and deaths since the
 * player's own phase began. A ring, the oldest goes first. */
typedef enum { NOISE_FIGHT, NOISE_SPELL, NOISE_DEATH } NoiseKind;
#define NOISE_MAX 16
typedef struct {
    uint8_t x, y;
    uint8_t kind;   /* NoiseKind */
    uint8_t owner;  /* who made it (the victim for a death) */
} Noise;

/* A patrol route of the AI (K10.7, K10.8): waypoints and capability flags
 * (bit 0 flier, 1 use, 2 carry, 3 wood, 4 water, 5 rock, 6 wizards may use). */
#define ROUTES_MAX 12
#define ROUTE_WP_MAX 12
#define TRIGGERS_MAX 12
typedef struct {
    uint8_t n;                       /* waypoints */
    uint8_t flags;
    uint8_t x[ROUTE_WP_MAX], y[ROUTE_WP_MAX];
} Route;

typedef struct {
    uint8_t x, y;
    uint8_t kind;   /* CreatureKind */
    uint8_t owner;  /* Owner */
    uint8_t flags;  /* UF_* */
    uint8_t native; /* NATIVE_* terrain type (pays floor cost there) */
    uint8_t ap, ap_max, ap_fly;
    uint8_t sta, sta_max;     /* stamina */
    uint8_t con, con_max;     /* constitution */
    uint8_t wounds;           /* 0..7, each costs 2 con per round (R9) */
    uint8_t com, def;         /* combat, defence */
    uint8_t mr;               /* magic resistance */
    uint8_t mana, mana_max;   /* wizards only */
    uint8_t items[6];         /* carried object kinds (OBJ_*) */
    uint8_t item_count;
    uint8_t in_use;           /* index into items, 0xFF = bare hands */
    uint8_t id;               /* stable while the unit lives (indices shift) */
    uint8_t rider_kind;       /* kind carried on this mount, 0xFF = none */
    /* the rider's own values while mounted (D60); his mana sits in
     * mana/mana_max, his items in items[] */
    uint8_t rider_con, rider_con_max, rider_sta, rider_sta_max;
    uint8_t rider_com, rider_def, rider_mr;
    uint8_t rider_wounds;     /* kept through the ride and bleeding in the saddle (F3, F31) */
    uint8_t rider_ap, rider_ap_max;   /* the rider acts with his own AP (K6.6) */
    uint8_t post_x, post_y;   /* guard post (M4h) / territory, 0xFF = none */
    uint8_t grudge;           /* wild animals (D35): owners that attacked it */
    uint8_t herd_dir;         /* crossing herd: direction 1..8, 0 = none */
    uint8_t travel;           /* crossing herd: fields walked so far */
    uint8_t group;            /* herd: the leader's id (0 = alone) */
    bool reacted;             /* the round's defensive reaction is spent (D29) */
    uint8_t alarm;            /* alarmed for this many rounds (D37) */
    uint8_t alarm_charge;     /* 1 = attack the disturber, 0 = flee */
    uint8_t alarm_x, alarm_y; /* where the trouble was */
    uint8_t alarm_owner;      /* who caused it */
    /* AI plan (K10.7): route, step, flags (bit 7 aggressive = bodyguard,
     * bit 6 asleep, low 6 bits trigger id); route 0xFF = no plan, which counts
     * as aggressive. `visited` is the ring of the last 8 fields it stood on,
     * packed x + y * w (0xFFFF = empty), against walking back and forth. */
    uint8_t plan_route, plan_step, plan_flags;
    uint8_t visit_head;
    uint16_t visited[8];
    bool done;                /* finished for this phase (space, turn.h) */
    Effect effects[UNIT_EFFECTS];   /* timed, tick at the round end (M4b) */
} Unit;

/* One death with its killer, for the VP account (game_credit_kills). */
typedef struct {
    uint8_t victim_kind, victim_owner;
    uint8_t killer_kind, killer_owner;
    bool melee;
} Kill;

typedef struct {
    uint8_t x, y;
    uint16_t tile;   /* TileId */
} Object;

typedef struct {
    uint8_t w, h, wrap;
    uint8_t generation;   /* bumped whenever the map layers change (view cache) */
    uint8_t floor[MAP_MAX_H][MAP_MAX_W];
    uint8_t decor[MAP_MAX_H][MAP_MAX_W];
    uint8_t feature[MAP_MAX_H][MAP_MAX_W];
    Unit units[MAX_UNITS];
    /* aggressive acts this round (D37): x, y, owner - the independents'
     * phase scares the animals nearby */
    uint8_t disturb_n;
    uint8_t disturb[WORLD_DISTURB][3];
    uint8_t unit_count;
    Object objects[MAX_OBJECTS];
    uint8_t object_count;
    int16_t portal_x, portal_y;   /* v3 maps: -1 = none */
    uint8_t portal_rmin, portal_rmax;
    uint8_t portal_span;          /* rounds it stays open, 0 = for good (v5) */
    uint8_t next_id;              /* unit ids, see world_spawn_unit */
    Kill kills[MAX_KILLS];        /* deaths not yet credited */
    uint8_t kill_count;
    Cauldron cauldrons[CAULDRONS_MAX];
    uint8_t cauldron_count;
    char save_map[32];            /* map of the running scenario (save) */
    uint8_t roof[MAP_MAX_W * MAP_MAX_H / 8 + 1];   /* v4: bit per field */
    /* AI routes and triggers (scenario file v2, K10.8) */
    Route routes[ROUTES_MAX];
    uint8_t route_n;
    uint8_t route_summon_n;       /* summoned creatures draw among the first n routes */
    uint8_t trig_n;
    uint8_t trig_x[TRIGGERS_MAX], trig_y[TRIGGERS_MAX], trig_id[TRIGGERS_MAX];
    uint8_t trig_fired[8];        /* bit per trigger id 0..63, set when terrain changed there */
    Remains remains[REMAINS_MAX]; /* D70 */
    uint8_t remains_n, remains_next;
    Noise noises[NOISE_MAX];      /* D69 */
    uint8_t noise_n, noise_next;
    uint8_t ap_scale;             /* D71: percent on every AP budget, 0 = 100 */
} World;

/* Leave a skeleton on (x, y) (D70). */
void world_add_remains(World *w, int16_t x, int16_t y, uint8_t kind, uint8_t owner);
/* The newest remains on (x, y), NULL when there are none. */
const Remains *world_remains_at(const World *w, int16_t x, int16_t y);
/* Record a noise at (x, y) (D69); world_noise_clear starts a new listening
 * period (the player's phase). */
void world_noise(World *w, int16_t x, int16_t y, uint8_t kind, uint8_t owner);
void world_noise_clear(World *w);
/* AP of a budget under the scenario's AP factor (D71): ap * scale / 100,
 * at most 255. */
uint8_t world_scale_ap(const World *w, uint8_t ap);
/* Set the AP factor in percent and rescale every unit already placed
 * (the map's units come before the scenario). */
void world_set_ap_scale(World *w, uint8_t pct);

/* Load a binary map (.map, ADR 0008). Validates everything first; on
 * false the world is left unchanged. */
bool world_load_bin(World *w, const uint8_t *data, uint16_t len);
/* Terrain was changed at (x, y) by an action (door, wall, chest, spell): fire the
 * AI triggers standing there (K10.7). */
void world_poke(World *w, int16_t x, int16_t y);
/* Call after changing floor/decor/feature/roof (door opened ...).
 * Mandatory, not cosmetic: view.c caches the static tile layers against
 * `generation`, and sight.c caches the sight-blocking bitmaps against it.
 * A terrain write without this call leaves both looking at stale terrain.
 * That includes writes made directly by tests. */
void world_map_changed(World *w);

/* Normalise (x, y) for wrapping maps. Returns false if outside a
 * non-wrapping map. */
bool world_wrap(const World *w, int16_t *x, int16_t *y);

/* Feature at (x, y); FE_NONE outside a non-wrapping map. */
uint8_t world_feature(const World *w, int16_t x, int16_t y);
/* Floor at (x, y); FL_GRASS outside a non-wrapping map. */
uint8_t world_floor(const World *w, int16_t x, int16_t y);
/* Wall or door: forms the connected wall line (GDD 11.2). */
bool world_is_wall_line(const World *w, int16_t x, int16_t y);
/* Blocks sight between ground positions: floor (data/costs.csv) or a
 * tall feature (GDD 3.4). */
bool world_blocks_sight(const World *w, int16_t x, int16_t y);
/* Same test for coordinates already normalised inside the map (ray fast
 * path; see world.c). */
/* Only the feature on (x, y) (wall, tree, closed door ...) blocks sight. */
bool world_feature_blocks_sight(const World *w, uint8_t x, uint8_t y);
/* Roof of the field (v4 maps): blocks landing (GDD 3.2) and is drawn over
 * the building until a figure sees the field (D56); it does not block sight. */
bool world_has_roof(const World *w, int16_t x, int16_t y);
/* Eight blocking flags of row y starting at column x, packed MSB-first;
 * see world.c. */
uint8_t world_sight_byte(const World *w, uint8_t y, uint8_t x);
/* Feature blocks ground movement (GDD 3.3 furniture table). */
bool world_blocks(const World *w, int16_t x, int16_t y);

/* The two layers a unit can occupy per field (GDD 3.1): at most one
 * ground unit and one flying unit share a field. */
typedef enum { UL_GROUND, UL_AIR } UnitLayer;

/* Unit at (x, y) on that layer, NO_UNIT if none (ground layer unless
 * stated). */
uint8_t world_unit_at(const World *w, int16_t x, int16_t y, UnitLayer layer);
/* First unit on that layer of (x, y) that stops `owner` from entering
 * (C3): units of the same wizard may share a field, everything else
 * blocks - and for neutral movers every unit does. NO_UNIT if free. */
uint8_t world_blocking_unit_at(const World *w, int16_t x, int16_t y,
                               UnitLayer layer, uint8_t owner);
/* AP cost to enter (x, y); diagonal steps cost 3/2, rounded up (GDD 5.3). */
uint8_t world_step_cost(const World *w, int16_t x, int16_t y, bool diagonal);
/* Same for a unit: its terrain type (wood/water/rock) pays only the plain
 * floor cost in matching terrain (GDD 5.3). */
uint8_t world_unit_step_cost(const World *w, uint8_t unit, int16_t x, int16_t y,
                             bool diagonal);
/* Flying step (GDD 5.3): constant, whatever the ground below looks like. */
uint8_t world_air_step_cost(bool diagonal);
/* Move a unit one step (8 directions); false if blocked, occupied, outside
 * or not enough AP. Airborne units ignore terrain and ground units and
 * only respect the air layer. Spends the AP on success. */
bool world_move_unit(World *w, uint8_t unit, int8_t dx, int8_t dy);
/* Take off (<) / land (>): pay the action cost (actions.csv), switch
 * layers. Landing needs a free ground slot and no roof (v4 maps)
 * (GDD 3.1, deferred). */
bool world_take_off(World *w, uint8_t unit);
bool world_land(World *w, uint8_t unit);
/* Movement payment: AP and half of it (rounded up) as stamina (K8.1). */
void world_spend(World *w, uint8_t unit, uint8_t ap);
/* AP only (no stamina). */
void world_spend_ap(World *w, uint8_t unit, uint8_t ap);
/* Pay the AP and stamina of an action (data/actions.csv, K8.1). */
void world_pay(World *w, uint8_t unit, uint8_t action);
/* Can the unit afford the action? A rider in the saddle pays what he does
 * himself (spells, throws, objects, doors) from his own AP and stamina; moving,
 * fighting, taking off and landing cost the mount (K6.6). */
bool world_can_pay(const World *w, uint8_t unit, uint8_t action);
/* The AP the action would draw on (the rider's or the mount's). */
uint8_t world_pool_ap(const World *w, uint8_t unit, uint8_t action);
/* Spend `ap` (no stamina) from the pool of that action. */
void world_spend_ap_for(World *w, uint8_t unit, uint8_t action, uint8_t ap);
/* Is the unit able to act at all this phase: its own AP or the rider's? */
bool world_has_ap(const World *w, uint8_t unit);
/* Wounds (R9): set the counter (capped at 7) and keep UF_WOUNDED in step. */
void world_set_wounds(Unit *u, uint8_t n);
/* Constitution factor floor(ConMax / ConAct), at least 1 (K4, K6.1). */
uint8_t world_con_factor(const Unit *u);
/* Remove a unit (swap with the last): indices of other units may change,
 * so callers re-find units by id (world_find_unit, turn_revalidate). */
void world_remove_unit(World *w, uint8_t unit);
/* A wild animal (D35) remembers who attacked it and fights back. */
void world_provoke(World *w, uint8_t unit, uint8_t attacker_owner);
/* Note an aggressive act at (x, y) by owner (D37); a full list keeps the
 * first ones. */
void world_disturb(World *w, int16_t x, int16_t y, uint8_t owner);
/* The dead drop everything they carried on their field (D21). */
void world_drop_carried(World *w, const Unit *u);
/* A unit dies by someone's hand: its carried objects drop onto its
 * field (D21), the kill is logged for the VP account (game_credit_kills)
 * unless the killer is independent, then the unit is removed. Killer kind and owner are passed by value - the killer itself
 * may already be gone (lightning splash). A ridden mount throws its
 * rider off instead, who keeps his pack (D60). */
void world_kill_unit(World *w, uint8_t victim, uint8_t killer_kind,
                     uint8_t killer_owner, bool melee);
/* Add a freshly initialised unit (summons) with a fresh id; returns its
 * index. */
uint8_t world_spawn_unit(World *w, uint8_t owner, uint8_t kind, uint8_t x, uint8_t y);
/* Chebyshev distance between two fields, honouring wrap-around. */
uint8_t world_distance(const World *w, int16_t x0, int16_t y0, int16_t x1, int16_t y1);
/* Offset from (x0, y0) to (x1, y1), the shortest way on wrapping maps. */
void world_delta(const World *w, int16_t x0, int16_t y0, int16_t x1, int16_t y1,
                 int16_t *dx, int16_t *dy);
/* Distance of the original (K1): 2 * max(dx, dy) + min(dx, dy), with wrap-around.
 * Ranges and radii of spells, throws and shots use it. */
uint16_t world_range(const World *w, int16_t x0, int16_t y0, int16_t x1, int16_t y1);
/* Index of the unit with this id, NO_UNIT when it is gone. */
uint8_t world_find_unit(const World *w, uint8_t id);
/* Bound (K11.7): the unit moved next to a visible enemy on its own height
 * or attacked (UF_ENGAGED) and that enemy still stands next to it. A bound
 * unit does not move, take off or land; attacks stay possible. The binding
 * ends with its owner's next phase. */
bool world_engaged(const World *w, uint8_t unit);
/* Visible enemy on the unit's own height next to it (flag-free). */
bool world_enemy_adjacent(const World *w, uint8_t unit);
/* After a move or an attack: the unit becomes bound if an enemy stands next
 * to it (the enemy itself does not, K11.7). */
void world_engage(World *w, uint8_t unit);
/* The owner's phase is over: his units are free to move again. */
void world_release(World *w, uint8_t owner);
/* Round end: fatal wounds bleed (PM 17), the bled-out drop their
 * objects; refill AP - the layer budget
 * while flying (ap_fly), halved when exhausted (PM 12) - recover 25 %
 * stamina (GDD 5.3), regenerate 4 % mana; bleeders that reach 0 are
 * removed. */
void world_new_turn(World *w);

/* Why a step fails, for bump messages (GDD 5.1). */
typedef enum {
    BUMP_OK,        /* the step would succeed */
    BUMP_NO_AP,     /* destination fine, not enough AP */
    BUMP_DOOR,      /* closed door: try world_open_door */
    BUMP_UNIT,      /* a unit blocks the layer */
    BUMP_TERRAIN,   /* impassable feature (attack on terrain, M3) */
    BUMP_HELD,      /* strong blob or vine on the field (M4d) */
    BUMP_BOUND,     /* bound by an enemy next to it (K11.7) */
    BUMP_OUTSIDE    /* outside a non-wrapping map */
} BumpKind;
BumpKind world_bump_kind(const World *w, uint8_t unit, int8_t dx, int8_t dy);
/* Bump-open a closed door (GDD 5.1): creatures with hands (CF_USE) pay
 * the action cost, the door opens and the view cache is invalidated. */
bool world_open_door(World *w, uint8_t unit, int16_t x, int16_t y);

/* D61: an open door's leaf swings into the room (the roofed side; else
 * away from the opener at fx/fy, fx < 0 = unknown) and stands on the
 * field beside the doorway, as a drawing marker only (D76: it does not
 * block). Without room on the room side it swings out; with no room
 * anywhere there is no leaf (false) and the door opens as a bare frame.
 * Gates fold flat and have no leaf. */
bool world_leaf_spot(const World *w, int16_t x, int16_t y, int16_t fx,
                     int16_t fy, int16_t *lx, int16_t *ly, uint8_t *leaf);
/* A door between fence posts (D54). */
bool world_is_gate(const World *w, int16_t x, int16_t y);
/* Close an open door (C2, ACT_OPEN_DOOR): hands needed, nobody standing in
 * the doorway; the leaf swings back (D61). */
bool world_close_door(World *w, uint8_t unit, int16_t x, int16_t y);
/* Lock a closed door / unlock a locked one (C2, ACT_UNLOCK): needs a chest
 * key among the carried objects; the key is not used up. A locked door
 * only gives way to blows (FEATURE_TOUGH) or the key. */
bool world_lock_door(World *w, uint8_t unit, int16_t x, int16_t y);
bool world_unlock_door(World *w, uint8_t unit, int16_t x, int16_t y);
/* Does the unit carry a chest key? */
bool world_has_key(const World *w, uint8_t unit);

/* Character for dumps (floor/feature/unit at a glance). */
char world_char(const World *w, int16_t x, int16_t y);

#endif
