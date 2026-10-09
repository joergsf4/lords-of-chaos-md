#include "selftest.h"

#include <stdio.h>
#include <string.h>

#include "ai.h"
#include "ai_priv.h"
#include "area.h"
#include "brew.h"
#include "ride.h"
#include "save.h"
#include "wizard.h"
#include "effect.h"
#include "chord.h"
#include "combat.h"
#include "events.h"
#include "game.h"
#include "items.h"
#include "lexicon.h"
#include "gen/maps.h"
#include "gen/scenarios.h"
#include "names.h"
#include "populate.h"
#include "rng.h"
#include "sight.h"
#include "spells.h"
#include "turn.h"
#include "tutorial.h"
#include "view.h"
#include "world.h"

/* view_hash() of the wizard house with the cursor on the wizard.
 * Must be identical on host and Agon; update deliberately when the map,
 * tiles or composition rules change. Changed for M2d: the new unexplored
 * tile shifted every tile ID after "tree"; M2e added air_shadow and
 * cursor_blue, M3d/M3e object and portal tiles, M3g four treasures;
 * M5c added the seven fx tiles after "floor_*" (IDs shifted again);
 * D70 added "remains" (IDs after it shift). */
#define HOUSE_VIEW_HASH 0x3B18F952UL

static selftest_log_fn out;
static uint16_t fails;
static World world;

/* the two save games are big: one static pair for every test that needs them
 * (the eZ80 selftest has only ~20 KB of stack and heap) */
#ifndef LOC_SELFTEST_NO_SAVE
static SaveGame sg_a, sg_b;
#endif

static void load_house(void)
{
    world_load_bin(&world, MAPBIN_WIZARD_HOUSE, MAPBIN_WIZARD_HOUSE_LEN);
}

/* The Agon console loses the output tail at emulator exit; the eZ80 run
 * therefore only prints failures (plus the final verdict from main). */
static bool verbose_checks = true;

static void check(int ok, const char *what)
{
    char buf[80];
    if (!ok)
        fails++;
    if (!verbose_checks && ok)
        return;
    snprintf(buf, sizeof buf, "%s %s", ok ? "ok  " : "FAIL", what);
    out(buf);
}

static int has_layer(const FieldLayers *f, uint16_t id)
{
    uint8_t i;
    for (i = 0; i < f->n; i++)
        if (f->id[i] == id)
            return 1;
    return 0;
}

/* Visible fields of the whole map - for the shadowcasting shape checks. */
static uint16_t count_visible(const Sight *s, const World *w)
{
    uint16_t n = 0;
    uint8_t x, y;
    for (y = 0; y < w->h; y++)
        for (x = 0; x < w->w; x++)
            if (sight_visible(s, w, x, y))
                n++;
    return n;
}

/* Every window field of the fast (cached) path must equal view_compose(). */
static int fast_equals_reference(void)
{
    FieldLayers ref;
    uint8_t vx, vy;
    for (vy = 0; vy < VIEW_H; vy++)
        for (vx = 0; vx < VIEW_W; vx++) {
            const FieldLayers *f = view_field(vx, vy);
            view_compose(&world, (int16_t)(view_origin_x() + vx),
                         (int16_t)(view_origin_y() + vy), &ref);
            if (f->n != ref.n || f->air != ref.air || f->ride != ref.ride ||
                memcmp(f->id, ref.id, ref.n * sizeof ref.id[0]) != 0)
                return 0;
        }
    return 1;
}

static void test_rng(void)
{
    Rng r;
    uint16_t i;
    int in_range = 1;

    rng_seed(&r, 1);
    check(rng_next(&r) == 270369UL, "rng: xorshift32 #1");
    check(rng_next(&r) == 67634689UL, "rng: xorshift32 #2");
    check(rng_next(&r) == 2647435461UL, "rng: xorshift32 #3");
    rng_seed(&r, 0);
    check(r.state != 0, "rng: zero seed remapped");
    for (i = 0; i < 500; i++)
        if (rng_range(&r, 7) >= 7)
            in_range = 0;
    check(in_range, "rng: range bound");
}

static void test_world(void)
{
    static uint8_t bad[300];
    uint16_t i;

    check(world_load_bin(&world, MAPBIN_WIZARD_HOUSE, MAPBIN_WIZARD_HOUSE_LEN),
          "map: binary house loads");
    for (i = 0; i < MAPBIN_WIZARD_HOUSE_LEN && i < sizeof bad; i++)
        bad[i] = MAPBIN_WIZARD_HOUSE[i];
    bad[0] = 'X';
    check(!world_load_bin(&world, bad, MAPBIN_WIZARD_HOUSE_LEN), "map: bad magic rejected");
    bad[0] = 'L';
    bad[5] ^= 1;
    check(!world_load_bin(&world, bad, MAPBIN_WIZARD_HOUSE_LEN), "map: stale tile count rejected");
    bad[5] ^= 1;
    check(!world_load_bin(&world, bad, 100), "map: truncated file rejected");
    bad[MAPBIN_HEADER] = FL_COUNT;
    check(!world_load_bin(&world, bad, MAPBIN_WIZARD_HOUSE_LEN), "map: out-of-range floor rejected");
    check(world.w == 9 && world.units[0].x == 3, "map: failed load leaves world unchanged");

    load_house();
    check(world.w == 9 && world.h == 9 && !world.wrap, "world: house is 9x9, no wrap");
    check(world.unit_count == 2 && world.units[0].x == 3 && world.units[0].y == 4,
          "world: wizard at 3,4");
    check(world.feature[3][5] == FE_DOOR_OPEN, "world: open door at 5,3");
    check(world_blocks(&world, 0, 0) && world_blocks(&world, 1, 1), "world: wall and bed block");
    check(!world_blocks(&world, 3, 3), "world: cauldron is walkable");
    check(world_blocks(&world, -1, 0), "world: outside a small map blocks");
}

static void test_view(void)
{
    check(view_anim_table_ok(), "view: anim_pair matches the ANIM_A/ANIM_B lists");
    FieldLayers f;

    view_compose(&world, 0, 0, &f);   /* top-left corner: walls E and S */
    check(f.n >= 2 && f.id[0] == T_FLOOR_STONE && has_layer(&f, T_WALL_00 + (2 | 4)),
          "view: corner wall mask E+S");

    view_compose(&world, 5, 3, &f);   /* door between stone and path */
    check(has_layer(&f, T_DOOR_V_OPEN) || has_layer(&f, T_DOOR_V_OPEN_EN) ||
          has_layer(&f, T_DOOR_V_OPEN_ES) || has_layer(&f, T_DOOR_V_OPEN_WN) ||
          has_layer(&f, T_DOOR_V_OPEN_WS), "view: door orientation vertical");
    {   /* D84: the leaf of a door in a north-south wall is drawn in its own tile */
        uint8_t keep = world.feature[2][6];
        world.feature[2][6] = FE_LEAF_S;             /* marker north-east of the door */
        view_invalidate();
        view_compose(&world, 5, 3, &f);
        check(has_layer(&f, T_DOOR_V_OPEN_EN), "d84: marker north-east: leaf east, hinge north");
        world.feature[2][6] = keep;
        view_invalidate();
        world.feature[2][6] = FE_LEAF_S;
        view_invalidate();
        view_compose(&world, 6, 2, &f);
        check(!has_layer(&f, T_DOOR_V_OPEN) && !has_layer(&f, T_DOOR_V_OPEN_EN) &&
              !has_layer(&f, T_DOOR_V_CLOSED), "d84: the marker field draws no leaf of its own");
        world.feature[2][6] = keep;
        view_invalidate();
        view_compose(&world, 5, 3, &f);          /* the checks below read this field */
    }
    check(has_layer(&f, T_FLOOR_PATH_HALF_E) && !has_layer(&f, T_FLOOR_STONE_HALF_W),
          "view: half floor only where neighbour floor differs");

    view_compose(&world, 3, 4, &f);   /* wizard on a rug */
    check(f.n == 3 && f.id[0] == T_FLOOR_STONE && f.id[1] == T_DECOR_RUG &&
          f.id[2] == T_WIZARD_P1, "view: layer order floor, rug, wizard");
    view_compose(&world, 8, 3, &f);   /* neutral goblin outside the house */
    check(has_layer(&f, T_GOBLIN_NEUTRAL), "view: creature in owner colour (neutral)");

    view_set_phase(1);
    view_compose(&world, 1, 2, &f);
    check(has_layer(&f, T_CANDLE_1), "view: candle animation phase");
    view_set_phase(0);
}

static void test_dirty_and_move(void)
{
    char buf[48];
    uint32_t h;

    view_set_origin(0, 0);
    view_set_cursor(3, 4, T_CURSOR_GREEN);
    view_invalidate();
    check(view_update(&world) == VIEW_W * VIEW_H, "view: first frame all dirty");
    h = view_hash();
    if (verbose_checks) {
        snprintf(buf, sizeof buf, "view: hash=0x%08lX", (unsigned long)h);
        out(buf);
    }
    check(h == HOUSE_VIEW_HASH, "view: deterministic house hash");
    check(fast_equals_reference(), "view: cached fast path equals reference");
    view_clean();
    check(view_update(&world) == 0, "view: unchanged frame is clean");

    check(world_move_unit(&world, 0, 0, 1), "move: wizard south");
    view_set_cursor(3, 5, T_CURSOR_GREEN);
    check(view_update(&world) == 2, "view: a step dirties exactly 2 fields");
    view_set_phase(1);
    view_update(&world);
    check(fast_equals_reference(), "view: fast path equals reference (moved, phase 1)");
    view_set_phase(0);
    view_update(&world);
    view_clean();
    check(view_animate(1) == 4, "view: animate dirties only the 4 candles");
    check(fast_equals_reference(), "view: animated frame equals reference");
    view_clean();
    check(view_animate(1) == 0, "view: same phase again changes nothing");
    view_animate(0);
    view_clean();
    view_clean();

    check(!world_move_unit(&world, 0, 0, 5), "move: no jumping into walls");
    world.units[0].x = 1;
    world.units[0].y = 5;
    check(!world_move_unit(&world, 0, -1, 0), "move: wall blocks");
    check(!world_move_unit(&world, 0, 0, 1), "move: table blocks");

    {   /* D43: ghost and spectre drift through the same wall */
        uint8_t g;
        world.unit_count = 1;
        g = world_spawn_unit(&world, OWN_P1, CR_GHOST, 1, 5);
        world.units[g].flags = (uint8_t)(world.units[g].flags & ~UF_FLYING);
        world.units[g].ap = 40;
        check(world_move_unit(&world, g, -1, 0),
              "d43: the ghost walks through the wall");
        world.units[g].x = 1;
        world.units[g].y = 5;
        world.units[0].x = 0;            /* a foreign body still stops it */
        world.units[0].y = 5;
        world.units[0].owner = OWN_P2;
        check(!world_move_unit(&world, g, -1, 0),
              "d43: another unit still blocks the ghost");
    }
}

static void test_ap(void)
{
    load_house();
    check(world.units[0].ap == 40, "ap: wizard starts with 40");
    check(world_step_cost(&world, 7, 3, false) == 3 && world_step_cost(&world, 2, 2, false) == 4,
          "ap: path 3, floor 4");
    check(world_step_cost(&world, 2, 2, true) == 6 && world_step_cost(&world, 7, 3, true) == 5,
          "ap: diagonal = 3/2 rounded up");
    check(world_move_unit(&world, 0, 1, 1) && world.units[0].ap == 34, "ap: diagonal step costs 6");
    world.units[0].ap = 3;
    check(!world_move_unit(&world, 0, 0, 1), "ap: not enough AP blocks");
    world_new_turn(&world);
    check(world.units[0].ap == 40, "ap: new turn refills");
}

static void test_stats_and_names(void)
{
    const char *g[GROUND_MAX];
    uint8_t n;

    load_house();
    check(world.units[0].sta == 60 && world.units[0].mana == 80, "stats: wizard stamina 60, mana 80");
    world_move_unit(&world, 0, 1, 1);                         /* diagonal: 6 AP */
    check(world.units[0].sta == 57, "stats: step costs half the AP as stamina");
    world_new_turn(&world);
    check(world.units[0].sta == 60, "stats: new turn recovers stamina (capped)");
    world.units[0].sta = 9;
    world_new_turn(&world);
    check(world.units[0].sta == 19, "stats: recovery is a sixth of max (K4)");
    check(world.units[0].ap == 20, "stats: exhausted creatures get half AP (PM 12)");
    world_new_turn(&world);                        /* 19 of 60: cured */
    check(world.units[0].ap == 40, "stats: one quiet round cures exhaustion");
    world.units[0].mana = 0;
    world_new_turn(&world);
    check(world.units[0].mana == 3, "stats: mana regenerates 4 % per round");

    n = ground_names(&world, 3, 7, g);                        /* scroll on wood */
    check(n == 1 && g[0][0] == 'S', "names: object on the floor");
    n = ground_names(&world, 3, 4, g);
    check(n == 1 && g[0][0] == 'T', "names: rug (Teppich)");
    n = ground_names(&world, 3, 3, g);
    check(n == 1 && g[0][0] == 'K', "names: cauldron (Kessel)");
    n = ground_names(&world, 2, 2, g);
    check(n == 1 && g[0][0] == 'S' && g[0][1] == 't', "names: bare floor (Steinboden)");
    check(name_unit(&world.units[1])[0] == 'G', "names: goblin");
}

static void test_data(void)
{
    check(spell_mana(SP_GIANT_BAT, 1) == 4 && spell_mana(SP_GIANT_BAT, 3) == 8,
          "data: giant bat mana 2 * (L+1)");
    check(spell_mana(SP_GOLD_DRAGON, 8) == 207, "data: gold dragon level 8 = 207");
    check(spell_mana(SP_MAGIC_BOLT, 0) == 1 && spell_mana(SP_MAGIC_SHIELD, 2) == 6,
          "data: bolt and shield mana from K5.2");
    check(SPELLS[SP_SUPER_POTION].amiga == 0 && SPELLS[SP_BOMB_POTION].known == 0,
          "data: super potion not on Amiga, bomb potion cost unknown");
    check(FLOOR_AP[FL_PATH] == 3 && FLOOR_AP[FL_STONE] == 4, "data: floor costs from costs.csv");
    check(ACTIONS[ACT_CAST].ap == 8 && ACTIONS[ACT_MELEE].stamina == 8,
          "data: action costs from actions.csv");
}

static void test_creatures(void)
{
    const CreatureDef *g = &CREATURES[CR_GOBLIN];
    check(CR_COUNT == 26 && CR_WIZARD == 0, "creatures: wizard + 25 from the table");
    check(g->ap == 30 && g->stamina == 45 && g->con == 32 && g->combat == 9 && g->defence == 9,
          "creatures: goblin values from [PM 34]");
    check(CREATURES[CR_GOLD_DRAGON].ap_fly == 40 && CREATURES[CR_ZOMBIE].flags & CF_UNDEAD,
          "creatures: dragon flies (40), zombie undead");
    check(CREATURES[CR_UNICORN].flags & CF_MOUNT && CREATURES[CR_DWARF].flags & CF_RIDE,
          "creatures: unicorn is a mount, dwarf can ride");
    world_load_bin(&world, MAPBIN_TESTLAND, MAPBIN_TESTLAND_LEN);
    world.units[1].kind = CR_CROCODILE;   /* re-init a unit as crocodile */
    world.units[1].native = CREATURES[CR_CROCODILE].native;
    check(world_unit_step_cost(&world, 1, 16, 0, false) == 4 &&
          world_unit_step_cost(&world, 0, 16, 0, false) == 12,
          "creatures: crocodile pays floor cost in water, wizard 12");
    check(world_unit_step_cost(&world, 1, 16, 0, true) == 6, "creatures: diagonal affinity 6");
    load_house();
}

static void test_terrain(void)
{
    FieldLayers a, b;
    check(world_load_bin(&world, MAPBIN_TESTLAND, MAPBIN_TESTLAND_LEN), "terrain: testland loads");
    check(world.w == 36 && world.h == 36 && world.wrap, "terrain: 36x36, wraps");
    check(FLOOR_AP[FL_WATER] == 12 && FLOOR_AP[FL_FOREST] == 8 && FLOOR_AP[FL_SWAMP] == 10,
          "terrain: costs water 12, forest 8, swamp 10");
    check(world_blocks_sight(&world, 25, 4) && !world_blocks_sight(&world, 20, 4),
          "terrain: forest blocks sight, grass does not");
    check(FLOOR_NATIVE[FL_WATER] == NATIVE_WATER && FLOOR_DROWN[FL_WATER],
          "terrain: water is native to water types, drowns others");
    check(world_blocks(&world, 32, 31), "terrain: rock blocks");
    {
        int16_t x, y;
        int printable = 1;
        for (y = 0; y < world.h; y++)
            for (x = 0; x < world.w; x++) {
                char ch = world_char(&world, x, y);
                if (ch < 32 || ch > 126)
                    printable = 0;
            }
        check(printable && world_char(&world, 32, 31) == 'R' && world_char(&world, 16, 0) == '~',
              "terrain: every field has a printable dump char");
    }
    view_compose(&world, -1, 12, &a);
    view_compose(&world, 35, 12, &b);
    check(a.n == b.n && memcmp(a.id, b.id, a.n * sizeof a.id[0]) == 0, "terrain: wrap-around x=-1 == x=35");
    view_set_origin(0, 0);
    view_follow(&world, 0, 0);
    check(view_origin_x() == 32 && view_origin_y() == 32, "terrain: camera wraps (origin 32,32, D78 centre 4)");
    {   /* D85: one field off the middle keeps the window, two recentre */
        view_set_origin(10, 10);
        view_follow(&world, 15, 14);              /* offset +1 from the middle (14) */
        check(view_origin_x() == 10, "d85: one field off the middle: no scroll");
        view_follow(&world, 16, 14);              /* offset +2 */
        check(view_origin_x() == 12 && view_origin_y() == 10,
              "d85: two fields off: recentre on the unit");
    }
    view_invalidate();
    view_update(&world);
    view_clean();
    check(view_animate(1) > 0 && fast_equals_reference(), "terrain: water animates like reference");
    {   /* D51: the water flows through four frames and every frame is the
         * same as the reference composition */
        uint8_t ph, moved = 1, distinct = 0;
        FieldLayers prev, cur;
        int16_t x, y, wx = -1, wy = -1;
        for (y = 0; y < world.h && wx < 0; y++)
            for (x = 0; x < world.w; x++)
                if (world.floor[y][x] == FL_WATER && world.feature[y][x] == FE_NONE) {
                    wx = x;
                    wy = y;
                    break;
                }
        check(wx >= 0, "d51: the testland has open water");
        view_set_phase(0);
        view_compose(&world, wx, wy, &prev);
        for (ph = 1; ph < 4; ph++) {
            view_set_phase(ph);
            view_compose(&world, wx, wy, &cur);
            if (cur.id[0] != prev.id[0])
                distinct++;
            else
                moved = 0;
            prev = cur;
            view_animate(ph);
            if (!fast_equals_reference())
                moved = 0;
        }
        check(moved && distinct == 3, "d51: water shows a new frame in each of the 4 phases");
        view_set_phase(0);
        view_animate(0);
        view_compose(&world, wx, wy, &a);
        view_compose(&world, wx, wy, &b);
        check(a.n == b.n && memcmp(a.id, b.id, a.n * sizeof a.id[0]) == 0,
              "d51: composing a field twice gives the same layers");
    }
    {   /* D52: transitions - shore on water, tufts on a path, corners */
        uint8_t save[3][3], save_fe = world.feature[31][11];
        int16_t x, y, bx = 10, by = 30;
        FieldLayers f;
        for (y = 0; y < 3; y++)
            for (x = 0; x < 3; x++) {
                save[y][x] = world.floor[by + y][bx + x];
                world.floor[by + y][bx + x] = FL_GRASS;
            }
        world.feature[by + 1][bx + 1] = FE_NONE;
        world.floor[by + 1][bx + 1] = FL_WATER;
        view_compose(&world, bx + 1, by + 1, &f);
        check(f.id[1] == T_EDGE_SHORE_M01 + 14, "d52: a pond is walled in by the shore (mask 15)");
        world.floor[by + 1][bx] = FL_WATER;          /* water to the west */
        view_compose(&world, bx + 1, by + 1, &f);
        check(f.id[1] == T_EDGE_SHORE_M01 + 6 && f.n == 2, "d52: open side west: shore mask 7, no redundant corners");
        world.floor[by + 1][bx] = FL_GRASS;
        world.floor[by][bx] = FL_WATER;
        world.floor[by + 1][bx + 1] = FL_WATER;
        view_compose(&world, bx + 1, by + 1, &f);
        check(f.id[1] == T_EDGE_SHORE_M01 + 14 || f.n > 1, "d52: a diagonal water neighbour still gets edges");
        world.floor[by + 1][bx + 1] = FL_PATH;
        view_compose(&world, bx + 1, by + 1, &f);
        check(f.id[1] == T_EDGE_PATH_M01 + 14, "d52: grass around a path field: tufts on all sides");
        world.floor[by + 1][bx + 1] = FL_GRASS;
        world.floor[by + 1][bx + 2] = FL_TALL_GRASS;
        view_compose(&world, bx + 1, by + 1, &f);
        check(f.id[1] == T_EDGE_TALL_M01 + 1, "d52: tall grass to the east frays the meadow");
        world.floor[by + 1][bx + 2] = FL_STONE;
        view_compose(&world, bx + 1, by + 1, &f);
        check(f.n == 1, "d52: stone floor next to grass gives no transition");
        for (y = 0; y < 3; y++)
            for (x = 0; x < 3; x++)
                world.floor[by + y][bx + x] = save[y][x];
        world.feature[31][11] = save_fe;
    }
    {   /* D54: windows, fence and gate, bridge, flowers, mushrooms, bubbles */
        enum { RX = 8, RY = 26, RW = 9, RH = 6 };
        uint8_t sfl[RH][RW], sfe[RH][RW], sde[RH][RW];
        int16_t x, y;
        FieldLayers f;
        uint8_t bubbles = 0;
        for (y = 0; y < RH; y++)
            for (x = 0; x < RW; x++) {
                sfl[y][x] = world.floor[RY + y][RX + x];
                sfe[y][x] = world.feature[RY + y][RX + x];
                sde[y][x] = world.decor[RY + y][RX + x];
                world.floor[RY + y][RX + x] = FL_GRASS;
                world.feature[RY + y][RX + x] = FE_NONE;
                world.decor[RY + y][RX + x] = DE_NONE;
            }
        /* wall row y=RY+1 with a window in the middle, a plain wall field next to it */
        for (x = 1; x <= 7; x++)
            world.feature[RY + 1][RX + x] = FE_WALL;
        world.feature[RY + 1][RX + 4] = FE_WINDOW;
        world_map_changed(&world);
        view_compose(&world, RX + 4, RY + 1, &f);
        check(f.id[f.n - 1] == T_WINDOW_H, "d54: a window in an east-west wall draws the horizontal window");
        check(world_is_wall_line(&world, RX + 4, RY + 1) && world_blocks(&world, RX + 4, RY + 1),
              "d54: a window blocks movement and joins the wall line");
        check(!world_blocks_sight(&world, RX + 4, RY + 1), "d54: a window does not block sight");
        check(sight_has_los(&world, RX + 4, RY, RX + 4, RY + 2), "d54: you see through a window");
        check(!sight_has_los(&world, RX + 3, RY, RX + 3, RY + 2), "d54: but not through the wall beside it");
        world.feature[RY + 1][RX + 4] = FE_NONE;
        world.feature[RY][RX + 4] = FE_WALL;
        world.feature[RY + 2][RX + 4] = FE_WALL;
        world.feature[RY + 1][RX + 4] = FE_WINDOW;
        view_compose(&world, RX + 4, RY + 1, &f);
        check(f.id[f.n - 1] == T_WINDOW_V, "d54: a window in a north-south wall draws the vertical window");
        world.feature[RY][RX + 4] = FE_NONE;
        world.feature[RY + 2][RX + 4] = FE_NONE;
        for (x = 0; x < RW; x++)
            world.feature[RY + 1][RX + x] = FE_NONE;
        /* fence with a gate */
        for (x = 1; x <= 7; x++)
            world.feature[RY + 3][RX + x] = FE_FENCE;
        view_compose(&world, RX + 3, RY + 3, &f);
        check(f.id[f.n - 1] == T_FENCE_00 + 10, "d54: a fence joins east and west (mask 10)");
        check(!world_is_wall_line(&world, RX + 3, RY + 3) && world_blocks(&world, RX + 3, RY + 3) &&
              !world_blocks_sight(&world, RX + 3, RY + 3), "d54: a fence blocks movement, not sight");
        world.feature[RY + 3][RX + 4] = FE_DOOR_CLOSED;
        view_compose(&world, RX + 4, RY + 3, &f);
        check(f.n == 2 && f.id[f.n - 1] == T_GATE_H_CLOSED,
              "d54: a door between fence posts is a gate (no half floors)");
        view_compose(&world, RX + 3, RY + 3, &f);
        check(f.id[f.n - 1] == T_FENCE_00 + 10, "d54: the fence still joins the gate");
        world.feature[RY + 3][RX + 4] = FE_DOOR_OPEN;
        view_compose(&world, RX + 4, RY + 3, &f);
        check(f.id[f.n - 1] == T_GATE_H_OPEN, "d54: an open gate shows open");
        world.feature[RY + 3][RX + 4] = FE_DOOR_CLOSED;
        world.feature[RY + 3][RX + 3] = FE_WALL;
        view_compose(&world, RX + 4, RY + 3, &f);
        check(f.id[f.n - 1] == T_DOOR_H_CLOSED, "d54: a door beside a wall stays a door");
        for (x = 0; x < RW; x++)
            world.feature[RY + 3][RX + x] = FE_NONE;
        /* bridge over a river: east-west deck when water lies north/south */
        for (x = 0; x < RW; x++) {
            world.floor[RY + 4][RX + x] = FL_WATER;
            world.floor[RY + 5][RX + x] = FL_WATER;
        }
        world.floor[RY + 4][RX + 4] = FL_BRIDGE;
        view_compose(&world, RX + 4, RY + 4, &f);
        check(f.id[0] == T_FLOOR_BRIDGE_H, "d54: a bridge with water to the south is an east-west deck");
        check(FLOOR_AP[FL_BRIDGE] == FLOOR_AP[FL_GRASS] && !FLOOR_DROWN[FL_BRIDGE] &&
              !FLOOR_SIGHT[FL_BRIDGE], "d54: bridge costs like grass, no drowning, no sight block");
        view_compose(&world, RX + 5, RY + 4, &f);
        {
            uint8_t i, shore_west = 0;
            for (i = 0; i < f.n; i++)   /* mask bit W (8) = lip on the bridge side */
                if (f.id[i] >= T_EDGE_SHORE_M01 && f.id[i] < T_EDGE_SHORE_M01 + 15 &&
                    ((f.id[i] - T_EDGE_SHORE_M01 + 1) & 8))
                    shore_west = 1;
            check(!shore_west, "d54: no shore lip against the bridge");
        }
        world.floor[RY + 4][RX + 4] = FL_WATER;
        world.floor[RY + 3][RX + 4] = FL_BRIDGE;
        world.floor[RY + 4][RX + 3] = FL_GRASS;
        world.floor[RY + 4][RX + 5] = FL_GRASS;
        world.floor[RY + 5][RX + 3] = FL_GRASS;
        world.floor[RY + 5][RX + 5] = FL_GRASS;
        world.floor[RY + 5][RX + 4] = FL_BRIDGE;
        world.floor[RY + 4][RX + 4] = FL_BRIDGE;
        view_compose(&world, RX + 4, RY + 4, &f);
        check(f.id[0] == T_FLOOR_BRIDGE_V, "d54: a bridge between banks (water gone N/S) runs north-south");
        /* decor */
        world.decor[RY][RX] = DE_FLOWERS;
        world.decor[RY][RX + 1] = DE_MUSHROOMS;
        view_compose(&world, RX, RY, &f);
        check(f.id[f.n - 1] >= T_DECOR_FLOWERS_0 && f.id[f.n - 1] <= T_DECOR_FLOWERS_0 + 2,
              "d54: flower bed draws a flower tile");
        view_compose(&world, RX + 1, RY, &f);
        check(f.id[f.n - 1] == T_DECOR_MUSH_0, "d54: mushrooms draw the glowing mushroom tile");
        /* bubbles: some swamp fields, not all */
        for (y = 0; y < RH; y++)
            for (x = 0; x < RW; x++) {
                world.floor[RY + y][RX + x] = FL_SWAMP;
                world.feature[RY + y][RX + x] = FE_NONE;
                world.decor[RY + y][RX + x] = DE_NONE;
            }
        for (y = 0; y < RH; y++)
            for (x = 0; x < RW; x++) {
                view_compose(&world, RX + x, RY + y, &f);
                if (f.id[f.n - 1] == T_DECOR_BUBBLE_0)
                    bubbles++;
            }
        check(bubbles >= 1 && bubbles < RW * RH / 2, "d54: swamp bubbles on some fields only");
        for (y = 0; y < RH; y++)
            for (x = 0; x < RW; x++) {
                world.floor[RY + y][RX + x] = sfl[y][x];
                world.feature[RY + y][RX + x] = sfe[y][x];
                world.decor[RY + y][RX + x] = sde[y][x];
            }
        world_map_changed(&world);
    }
    {   /* D56: the roof opens on what the active figure sees - a window or an
         * open door gives a real view into the room, not a patch */
        enum { SX = 8, SY = 26, Y0 = 21, ROWS = 14 };   /* cleared: y 21..34 */
        static uint8_t sfl[ROWS][9], sfe[ROWS][9], sde[ROWS][9], sroof[sizeof world.roof];
        int16_t x, y;
        FieldLayers f;
        memcpy(sroof, world.roof, sizeof sroof);
        for (y = 0; y < ROWS; y++)
            for (x = 0; x < 9; x++) {
                sfl[y][x] = world.floor[Y0 + y][SX + x];
                sfe[y][x] = world.feature[Y0 + y][SX + x];
                sde[y][x] = world.decor[Y0 + y][SX + x];
                world.floor[Y0 + y][SX + x] = FL_GRASS;
                world.feature[Y0 + y][SX + x] = FE_NONE;
                world.decor[Y0 + y][SX + x] = DE_NONE;
            }
        /* a room 5 wide, 4 deep: walls y=SY+1 (top, window at x=4) .. y=SY+5,
         * side walls x=SX+1 and SX+7; everything under one roof */
        for (y = 1; y <= 5; y++)
            for (x = 1; x <= 7; x++) {
                uint16_t cell = (uint16_t)((uint16_t)(SY + y) * world.w + (SX + x));
                world.roof[cell >> 3] |= (uint8_t)(0x80u >> (cell & 7));
                if (y == 1 || y == 5 || x == 1 || x == 7)
                    world.feature[SY + y][SX + x] = FE_WALL;
            }
        world.feature[SY + 1][SX + 4] = FE_WINDOW;
        world_map_changed(&world);
        view_set_sight(NULL);
        view_set_roof_viewer(SX + 4, SY - 3);          /* outside, north of the window */
        check(sight_look(&world, SX + 4, SY - 3, SX + 4, SY + 4) &&
              sight_look(&world, SX + 4, SY - 3, SX + 4, SY + 2),
              "d56: through the window the view reaches deep into the room");
        check(!sight_look(&world, SX + 4, SY - 3, SX + 2, SY + 3),
              "d56: the walls beside the window cut the view off at an angle");
        view_compose(&world, SX + 4, SY + 3, &f);
        check(!has_layer(&f, T_ROOF), "d56: the roof opens on a field seen through the window");
        {   /* somewhere along the edge of the seen cone a lifted field is fringed */
            uint8_t fringed = 0, bad = 0;
            for (y = 2; y <= 4; y++)
                for (x = 2; x <= 6; x++) {
                    view_compose(&world, SX + x, SY + y, &f);
                    if (has_layer(&f, T_ROOF_HALF) || has_layer(&f, T_ROOF_FAINT)) {
                        fringed++;
                        if (has_layer(&f, T_ROOF))
                            bad++;          /* a fringe never sits on a closed roof */
                    }
                }
            check(fringed > 0 && bad == 0,
                  "d80: a lifted field beside closed roof keeps a see-through fringe");
        }
        view_compose(&world, SX + 4, SY - 1, &f);
        check(!has_layer(&f, T_ROOF_HALF) && !has_layer(&f, T_ROOF_FAINT),
              "d80: outside the building there is no fringe");
        view_compose(&world, SX + 2, SY + 3, &f);
        check(has_layer(&f, T_ROOF), "d56: a roofed field out of sight stays covered");
        view_compose(&world, SX + 4, SY + 1, &f);
        check(f.id[f.n - 1] == T_WINDOW_H, "d56: the window itself is never roofed over");
        view_set_roof_viewer(SX, SY - 3);               /* walks off to one side */
        view_compose(&world, SX + 4, SY + 3, &f);
        check(has_layer(&f, T_ROOF), "d56: out of sight the roof closes again");
        /* an open door works the same way */
        world.feature[SY + 5][SX + 4] = FE_DOOR_OPEN;
        world_map_changed(&world);
        view_set_roof_viewer(SX + 4, SY + 8);           /* south of the door */
        check(sight_look(&world, SX + 4, SY + 8, SX + 4, SY + 2),
              "d56: an open door shows the room beyond");
        world.feature[SY + 5][SX + 4] = FE_DOOR_CLOSED;
        world_map_changed(&world);
        check(!sight_look(&world, SX + 4, SY + 8, SX + 4, SY + 3),
              "d56: a closed door hides it");
        view_set_roof_viewer(-1, 0);
        memcpy(world.roof, sroof, sizeof sroof);
        for (y = 0; y < ROWS; y++)
            for (x = 0; x < 9; x++) {
                world.floor[Y0 + y][SX + x] = sfl[y][x];
                world.feature[Y0 + y][SX + x] = sfe[y][x];
                world.decor[Y0 + y][SX + x] = sde[y][x];
            }
        world_map_changed(&world);
    }
    {   /* variants are a pure function of the position (wrap-stable) */
        uint8_t seen_base = 0, seen_var = 0;
        int16_t x, y;
        for (y = 0; y < world.h; y++)
            for (x = 0; x < world.w; x++)
                if (world.floor[y][x] == FL_GRASS && world.feature[y][x] == FE_NONE) {
                    view_compose(&world, x, y, &a);
                    if (a.id[0] == T_FLOOR_GRASS)
                        seen_base = 1;
                    else if (a.id[0] >= T_FLOOR_GRASS_1 && a.id[0] <= T_FLOOR_GRASS_3)
                        seen_var = 1;
                }
        check(seen_base && seen_var, "d51: meadows mix the base tile and its variants");
    }
    view_animate(0);
    view_set_origin(0, 0);
    load_house();
}

static void test_chord(void)
{
    Chord c;
    int8_t dx, dy;
    const uint8_t W = 8, D = 35, R = 20;   /* 80 ms window, 350/200 ms repeat */

    chord_init(&c, W, D, R);
    check(chord_key(&c, ARROW_UP, true, 100) == 0, "chord: first arrow waits");
    check(chord_key(&c, ARROW_UP, false, 103) == ARROW_UP, "chord: quick tap fires on release");

    chord_init(&c, W, D, R);
    chord_key(&c, ARROW_RIGHT, true, 0);
    check(chord_poll(&c, 8) == 0 && chord_poll(&c, 9) == ARROW_RIGHT,
          "chord: held arrow fires after the window");
    check(chord_key(&c, ARROW_RIGHT, false, 30) == 0, "chord: release after firing is silent");
    {   /* D88: a numpad diagonal is two arrows at the same instant */
        Chord n;
        chord_init(&n, 4, 35, 20);
        check(chord_keys(&n, ARROW_DOWN | ARROW_LEFT, true, 50) == (ARROW_DOWN | ARROW_LEFT),
              "chord d88: a numpad diagonal moves at once");
        check(chord_keys(&n, ARROW_DOWN | ARROW_LEFT, false, 60) == 0 && n.held == 0,
              "chord d88: and releases cleanly");
        check(chord_keys(&n, ARROW_UP, true, 100) == 0 &&
              chord_keys(&n, ARROW_UP, false, 102) == ARROW_UP,
              "chord d88: a single numpad direction is a plain tap");
    }
    {   /* D79: a slow step must not eat the delay before the first repeat */
        Chord d;
        uint8_t diag = ARROW_DOWN | ARROW_RIGHT;
        chord_init(&d, 8, 35, 20);
        chord_key(&d, ARROW_DOWN, true, 100);
        check(chord_key(&d, ARROW_RIGHT, true, 103) == diag, "chord d79: the chord fires");
        chord_done(&d, 140);                     /* the step took 370 ms */
        check(chord_poll(&d, 150) == 0 && chord_poll(&d, 174) == 0,
              "chord d79: no repeat while the delay runs from the end of the step");
        check(chord_poll(&d, 175) == diag, "chord d79: the first repeat after 350 ms");
        chord_done(&d, 200);                     /* later repeats keep their clock */
        check(chord_poll(&d, 194) == 0 && chord_poll(&d, 195) == diag,
              "chord d79: later repeats keep the 200 ms clock");
    }

    chord_init(&c, W, D, R);
    chord_key(&c, ARROW_UP, true, 200);
    check(chord_key(&c, ARROW_RIGHT, true, 204) == (ARROW_UP | ARROW_RIGHT),
          "chord: up+right within window = NE");
    check(chord_key(&c, ARROW_UP, false, 210) == 0 && chord_key(&c, ARROW_RIGHT, false, 211) == 0,
          "chord: releasing a chord is silent");
    check(chord_to_step(ARROW_UP | ARROW_RIGHT, &dx, &dy) && dx == 1 && dy == -1,
          "chord: NE maps to dx=1 dy=-1");

    chord_init(&c, W, D, R);
    chord_key(&c, ARROW_DOWN, true, 0);
    check(chord_poll(&c, 9) == ARROW_DOWN, "chord: late partner -> first fires alone");
    check(chord_key(&c, ARROW_LEFT, true, 12) == 0 &&
          chord_key(&c, ARROW_LEFT, false, 14) == ARROW_LEFT,
          "chord: late partner fires on its own");

    chord_init(&c, W, D, R);
    chord_key(&c, ARROW_LEFT, true, 0);
    check(chord_key(&c, ARROW_RIGHT, true, 2) == ARROW_LEFT, "chord: opposite arrows no diagonal");
    check(!chord_to_step(ARROW_LEFT | ARROW_RIGHT, &dx, &dy), "chord: contradictory mask rejected");

    chord_init(&c, W, D, R);
    chord_key(&c, ARROW_UP, true, 0);
    chord_key(&c, ARROW_LEFT, true, 3);                       /* NW emitted */
    check(chord_poll(&c, 30) == 0, "chord: no repeat before the delay");
    check(chord_poll(&c, 38) == (ARROW_UP | ARROW_LEFT), "chord: held chord repeats as diagonal");
    check(chord_poll(&c, 50) == 0 && chord_poll(&c, 58) == (ARROW_UP | ARROW_LEFT),
          "chord: then repeats every interval");
    chord_key(&c, ARROW_LEFT, false, 60);
    check(chord_poll(&c, 94) == 0 && chord_poll(&c, 95) == ARROW_UP,
          "chord: releasing one key continues with the other after the delay");
    chord_key(&c, ARROW_UP, false, 96);
    check(chord_poll(&c, 200) == 0, "chord: nothing held, no repeat");
}

/* FNV-1a over all units (position, AP, stamina) - wandering replay check. */
static uint32_t unit_hash(const World *w)
{
    uint32_t h = 2166136261UL;
    uint8_t i;
    for (i = 0; i < w->unit_count; i++) {
        const Unit *u = &w->units[i];
        uint8_t v[4] = { u->x, u->y, u->ap, u->sta }, k;
        for (k = 0; k < 4; k++)
            h = (h ^ v[k]) * 16777619UL;
    }
    return h;
}

static void test_turn(void)
{
    Turns t;
    uint32_t seen;
    uint8_t i, moved;

    world_load_bin(&world, MAPBIN_TESTLAND, MAPBIN_TESTLAND_LEN);
    turn_init(&t, &world, 42, 1u << OWN_P1);
    check(t.round == 1 && t.phase == OWN_P1, "turn: round 1 begins with player 1");
    check(t.active == 0 && world.units[0].kind == CR_WIZARD, "turn: wizard is active");
    check(!turn_may_move(&t), "turn: round 1 allows casting only (PM 7)");
    check(world.units[2].x == 27 && world.units[2].y == 5,
          "turn: independents stay put in round 1");

    turn_next_unit(&t, &world, false);
    check(t.active == 10, "turn: Tab selects the dwarf next");
    turn_next_unit(&t, &world, false);
    check(t.active == 11, "turn: then the flying bat");
    turn_next_unit(&t, &world, false);
    check(t.active == 0, "turn: Tab wraps around to the wizard");
    turn_next_unit(&t, &world, true);
    check(t.active == 11, "turn: Shift+Tab goes back to the bat");

    world.units[0].ap = 0;
    turn_next_unit(&t, &world, false);
    check(t.active == 10, "turn: Tab skips units without AP");
    world.units[0].ap = world.units[0].ap_max;
    turn_next_unit(&t, &world, false);
    turn_next_unit(&t, &world, false);
    check(t.active == 0, "turn: back on the wizard");

    turn_finish_unit(&t, &world);
    check(world.units[0].done && t.active == 10,
          "turn: space finishes the wizard, dwarf is next");
    check(turn_units_left(&t, &world), "turn: unfinished units are left");
    turn_finish_unit(&t, &world);
    turn_finish_unit(&t, &world);
    check(!turn_units_left(&t, &world) && t.active == 11,
          "turn: all finished, active stays for rendering");

    t.round1_lock = false;
    check(turn_may_move(&t), "turn: round 1 lock is switchable (emulator)");
    turn_end_phase(&t, &world);
    check(t.round == 2 && t.phase == OWN_P1 && t.active == 0,
          "turn: AI wizard passes, round 2 returns to p1");
    check(world.units[0].ap == 40 && world.units[0].sta == 60 &&
          world.units[0].mana == 80,
          "turn: round end refills AP, stamina and mana");
    check(!world.units[0].done && !world.units[10].done && !world.units[11].done,
          "turn: new phase clears the finished flags");

    moved = 0;
    for (i = 0; i < world.unit_count; i++)
        if (world.units[i].owner == OWN_NEUTRAL && world.units[i].ap < world.units[i].ap_max)
            moved++;
    check(moved > 0, "turn: independent creatures spent AP");
    seen = unit_hash(&world);

    world_load_bin(&world, MAPBIN_TESTLAND, MAPBIN_TESTLAND_LEN);
    turn_init(&t, &world, 42, 1u << OWN_P1);
    t.round1_lock = false;
    turn_end_phase(&t, &world);
    check(t.round == 2 && unit_hash(&world) == seen,
          "turn: same seed wanders the same way");

    turn_end_phase(&t, &world);
    turn_end_phase(&t, &world);
    check(t.round == 4 && t.phase == OWN_P1, "turn: rounds keep advancing");
}

static void test_sight(void)
{
    Sight s;
    FieldLayers f;

    world_load_bin(&world, MAPBIN_TESTLAND, MAPBIN_TESTLAND_LEN);
    world.unit_count = 2;                    /* wizards only for isolation */
    world.units[1].x = 0;
    world.units[1].y = 35;                   /* enemy far away */
    view_set_sight(NULL);
    sight_init(&s, OWN_P1);
    sight_compute(&world, &s);
    check(sight_visible(&s, &world, 6, 6), "sight: own field is visible");
    check(sight_visible(&s, &world, 3, 6) && !sight_visible(&s, &world, 2, 6),
          "sight: wall visible, the field behind it not");
    world.units[0].x = 20;                   /* leave the house (test only) */
    world.units[0].y = 13;
    sight_compute(&world, &s);
    check(sight_visible(&s, &world, 29, 13) && !sight_visible(&s, &world, 30, 13),
          "sight: ground range 9 (Chebyshev)");
    check(sight_explored(&s, &world, 6, 6) && !sight_visible(&s, &world, 6, 6),
          "sight: explored stays when sight moves on");
    view_set_sight(&s);
    view_compose(&world, 6, 6, &f);
    check(has_layer(&f, T_OVERLAY_REMEMBERED), "sight: remembered raster overlay");
    view_compose(&world, 31, 26, &f);        /* original p2 spot: never seen */
    check(f.n == 1 && f.id[0] == T_UNEXPLORED, "sight: unexplored is black");

    world.units[1].x = 21;                   /* enemy steps into sight */
    world.units[1].y = 13;
    sight_compute(&world, &s);
    view_compose(&world, 21, 13, &f);
    check(has_layer(&f, T_WIZARD_P2), "sight: enemy in sight is drawn");
    world.units[1].x = 6;                    /* enemy into the dark house */
    world.units[1].y = 6;
    sight_compute(&world, &s);
    view_compose(&world, 6, 6, &f);
    check(has_layer(&f, T_OVERLAY_REMEMBERED) && !has_layer(&f, T_WIZARD_P2),
          "sight: hidden movement keeps enemies invisible");

    world.units[0].x = 0;                    /* north-west corner */
    world.units[0].y = 0;
    world.units[1].x = 34;
    world.units[1].y = 34;
    sight_init(&s, OWN_P1);
    sight_compute(&world, &s);
    check(sight_visible(&s, &world, 35, 0) && sight_visible(&s, &world, 0, 35) &&
          sight_visible(&s, &world, 35, 35), "sight: rays wrap around the world");
    check(sight_visible(&s, &world, 27, 0) && !sight_visible(&s, &world, 26, 0),
          "sight: wrapped range 9");

    /* the fast path must compose the same hidden map as the reference */
    {
        uint8_t dirty_n;
        view_set_sight(&s);
        view_set_origin(8, 1);            /* window over house and grass */
        view_invalidate();
        dirty_n = view_update(&world);
        view_clean();
        view_update(&world);
        check(dirty_n == VIEW_W * VIEW_H && fast_equals_reference(),
              "sight: fast path equals reference with sight");
        view_animate(1);
        check(fast_equals_reference(), "sight: animated frame equals reference");
        view_animate(0);
        view_clean();
        view_set_sight(NULL);
    }

    /* Shadowcasting (D39, ADR 0009): on clear ground the field of view has
     * to be exactly the Chebyshev square. A gap between two of the eight
     * octants - the classic defect of this algorithm - shows up here and
     * nowhere else, because every other check only looks at single fields. */
    {
        uint8_t x, y;
        world_load_bin(&world, MAPBIN_TESTLAND, MAPBIN_TESTLAND_LEN);
        for (y = 0; y < world.h; y++)
            for (x = 0; x < world.w; x++) {
                world.floor[y][x] = FL_GRASS;
                world.feature[y][x] = FE_NONE;
            }
        world_map_changed(&world);
        world.unit_count = 1;
        world.units[0].x = 18;
        world.units[0].y = 18;
        world.units[0].flags = (uint8_t)(world.units[0].flags & ~UF_FLYING);
        sight_init(&s, OWN_P1);
        sight_compute(&world, &s);
        {   /* K11.1: the octagon, counted by its own definition */
            int16_t ox, oy;
            uint16_t octagon = 0;
            for (oy = -9; oy <= 9; oy++)
                for (ox = -9; ox <= 9; ox++)
                    if (2 * (ox < 0 ? -ox : ox) < 19 && 2 * (oy < 0 ? -oy : oy) < 19 &&
                        2 * ((ox < 0 ? -ox : ox) > (oy < 0 ? -oy : oy) ? (ox < 0 ? -ox : ox)
                                                                          : (oy < 0 ? -oy : oy)) +
                        ((ox < 0 ? -ox : ox) > (oy < 0 ? -oy : oy) ? (oy < 0 ? -oy : oy)
                                                                  : (ox < 0 ? -ox : ox)) < 19)
                        octagon++;
            check(octagon > 81 && octagon < 361,
                  "sight: the octagon is bigger than the 9x9 square, smaller than 19x19");
            check(count_visible(&s, &world) == octagon,
                  "sight: clear ground gives exactly the octagon (D < 19)");
        }
        check(sight_visible(&s, &world, 18 + 9, 18) && !sight_visible(&s, &world, 18 + 10, 18) &&
              sight_visible(&s, &world, 18 + 6, 18 + 6) && !sight_visible(&s, &world, 18 + 7, 18 + 7) &&
              !sight_visible(&s, &world, 18 + 9, 18 + 9),
              "sight: 9 fields straight, 6 diagonal; the square corners are out");
        check(sight_visible(&s, &world, 18 + 6, 18 + 6) && sight_visible(&s, &world, 18 - 6, 18 + 6) &&
              sight_visible(&s, &world, 18 + 6, 18 - 6) && sight_visible(&s, &world, 18 - 6, 18 - 6),
              "sight: every diagonal tip of the octagon is reached");

        for (y = 17; y <= 19; y++)       /* walled in: own field plus the ring */
            for (x = 17; x <= 19; x++)
                if (x != 18 || y != 18)
                    world.feature[y][x] = FE_WALL;
        world_map_changed(&world);
        sight_init(&s, OWN_P1);
        sight_compute(&world, &s);
        check(count_visible(&s, &world) == 9,
              "sight: walled in sees its own field and the walls, nothing else");

        /* D56 (reverses D44): a roof is display only - no walls anywhere in
         * this setup, so a roof alone hides nothing. */
        {
            uint16_t cell;
            for (y = 0; y < world.h; y++)
                for (x = 0; x < world.w; x++)
                    world.feature[y][x] = FE_NONE;
            memset(world.roof, 0, sizeof world.roof);
            for (y = 17; y <= 19; y++)
                for (x = 17; x <= 19; x++) {
                    cell = (uint16_t)((uint16_t)y * world.w + x);
                    world.roof[cell >> 3] |= (uint8_t)(0x80u >> (cell & 7));
                }
            world_map_changed(&world);
            world.units[0].x = 12;                /* out in the open */
            world.units[0].y = 18;
            sight_init(&s, OWN_P1);
            sight_compute(&world, &s);
            check(sight_visible(&s, &world, 18, 18),
                  "d56: a roof alone does not hide its inside from a viewer in the open");
            world.units[0].x = 18;                /* now underneath it */
            world.units[0].y = 18;
            sight_init(&s, OWN_P1);
            sight_compute(&world, &s);
            check(sight_visible(&s, &world, 17, 19) &&
                  sight_visible(&s, &world, 12, 18),
                  "d56: from under the roof it sees its room and out again");
        }
    }
}

/* D67 0b: wounds, constitution factor, stamina, hovering, take-off/landing. */
static void test_0b(void)
{
    Unit *u;
    world_load_bin(&world, MAPBIN_TESTLAND, MAPBIN_TESTLAND_LEN);
    u = &world.units[0];

    /* constitution factor: AP divided by floor(ConMax/Con) (R10) */
    u->con_max = 60;
    u->con = 40;                         /* 60/40 = 1: no malus */
    check(world_con_factor(u) == 1, "0b: above 50 % no constitution factor");
    u->con = 30;
    check(world_con_factor(u) == 2, "0b: at 50 % the factor is 2");
    u->con = 20;
    check(world_con_factor(u) == 3, "0b: at 33 % the factor is 3");
    u->sta = u->sta_max;
    world_new_turn(&world);
    check(u->ap == u->ap_max / 3, "0b: AP refill is divided by the factor");
    u->con = u->con_max;

    /* wounds are a counter, curse sets 7 (R9) */
    world_set_wounds(u, 9);
    check(u->wounds == 7 && (u->flags & UF_WOUNDED), "0b: wounds cap at 7");
    world_set_wounds(u, 0);
    check(u->wounds == 0 && !(u->flags & UF_WOUNDED), "0b: no wounds, no flag");

    /* exhaustion at a sixth of the maximum, recovery a sixth (R11) */
    u->sta_max = 60;
    u->sta = 10;
    world_new_turn(&world);
    check(u->ap == u->ap_max && u->sta == 20, "0b: stamina 10 of 60 is not exhausted");
    u->sta = 9;
    world_new_turn(&world);
    check(u->ap == u->ap_max / 2, "0b: stamina below a sixth halves the AP");

    /* melee needs 8 stamina, not only AP (K6.2); spells cost none */
    u->sta = 7;
    u->ap = 40;
    check(!world_can_pay(&world, 0, ACT_MELEE) && world_can_pay(&world, 0, ACT_CAST),
          "0b: melee wants 8 stamina, casting none");
    world_pay(&world, 0, ACT_CAST);
    check(u->sta == 7 && u->ap == 32, "0b: casting costs AP only");

    /* a flyer pays half its unspent AP as stamina each round (R12) */
    world_load_bin(&world, MAPBIN_TESTLAND, MAPBIN_TESTLAND_LEN);
    u = &world.units[11];
    check(world_take_off(&world, 11), "0b: bat takes off");
    u->sta = 50;
    u->ap = 40;
    world_new_turn(&world);
    check(u->sta == 50 - 20 + 12, "0b: hovering costs half the AP left (+ a sixth back)");

    /* take-off needs a free field: a second being blocks it */
    world_load_bin(&world, MAPBIN_TESTLAND, MAPBIN_TESTLAND_LEN);
    u = &world.units[11];
    world.units[1].x = u->x;
    world.units[1].y = u->y;
    check(!world_take_off(&world, 11), "0b: no take-off with another being on the field");
    world.units[1].x = 30;
    world.units[1].y = 30;
    u->ap = 5;
    check(!world_take_off(&world, 11), "0b: take-off needs 6 AP");
    u->ap = 24;
    check(world_take_off(&world, 11), "0b: take-off with 6 AP and a free field");
    /* landing: another being on the field blocks it too */
    world.units[1].x = u->x;
    world.units[1].y = u->y;
    check(!world_land(&world, 11), "0b: no landing on an occupied field");
}

/* D67 0d: ranges, attack values, resistance spells, teleport, potions. */
static void test_0d(void)
{
    Spellbook book;
    SpellShot shot;
    Rng rng;
    uint8_t k, ok;
    Unit *u;

    check(spell_range(SP_MAGIC_BOLT, 1) == 9 && spell_range(SP_MAGIC_BOLT, 8) == 23 &&
          spell_range(SP_MAGIC_EYE, 2) == 16 && spell_range(SP_TELEPORT, 3) == 36,
          "0d: ranges 2L+7, eye 3L+10, teleport 2L+30");
    check(spell_attack_value(SP_MAGIC_BOLT, 3) == 37 &&
          spell_attack_value(SP_MAGIC_LIGHTNING, 3) == 42,
          "0d: bolt 4L+25, lightning 4L+30");
    check(world_range(&world, 0, 0, 3, 1) == 7 && world_range(&world, 0, 0, 0, 4) == 8 &&
          world_range(&world, 2, 2, 5, 5) == 9,
          "0d: distance is 2 max + min (K1)");

    world_load_bin(&world, MAPBIN_TESTLAND, MAPBIN_TESTLAND_LEN);
    world.unit_count = 2;
    world.feature[5][8] = FE_DOOR_OPEN;
    world_map_changed(&world);
    u = &world.units[0];
    world.units[1].kind = CR_GOBLIN;
    world.units[1].owner = OWN_P2;
    world.units[1].x = 9;
    world.units[1].y = 5;
    world.units[1].con = world.units[1].con_max = 32;
    memset(&book, 0, sizeof book);

    /* a bolt reaches only 2L+7 distance units: wizard (6,6), goblin (9,5): D=7 */
    book.level[SP_MAGIC_BOLT] = 0;
    u->ap = 40;
    check(spell_in_range(&world, u, SP_MAGIC_BOLT, 0, 9, 5),
          "0d: level 0 reaches 7 units, the goblin is at 7");
    check(!spell_in_range(&world, u, SP_MAGIC_BOLT, 0, 11, 6) &&
          !spell_in_range(&world, u, SP_MAGIC_BOLT, 1, 11, 6) &&
          spell_in_range(&world, u, SP_MAGIC_BOLT, 2, 11, 6),
          "0d: a field 10 units away needs level 2 (reach 11)");

    /* Curse: RND(8L+55)+10 >= MR sets seven wounds */
    ok = 0;
    for (k = 0; k < 100; k++) {
        world_load_bin(&world, MAPBIN_TESTLAND, MAPBIN_TESTLAND_LEN);
        world.unit_count = 2;
        world.feature[5][8] = FE_DOOR_OPEN;
        world_map_changed(&world);
        world.units[1].kind = CR_GOBLIN;
        world.units[1].owner = OWN_P2;
        world.units[1].x = 9;
        world.units[1].y = 5;
        world.units[1].mr = 46;
        memset(&book, 0, sizeof book);
        book.level[SP_CURSE] = 1;
        world.units[0].ap = 40;
        world.units[0].mana = 80;
        rng_seed(&rng, 300 + k);
        if (spell_apply(&world, &book, 0, SP_CURSE, 9, 5, false, &rng, &shot) == CAST_OK &&
            world.units[1].wounds == 7)
            ok++;
    }
    /* n = 63, needs roll >= 36 -> 27/63 = 43 % */
    check(ok > 30 && ok < 58, "0d: curse at level 1 against MR 46 succeeds about 43 %");

    /* Subversion: wizards and wizard-riders are immune; MR 46: RND(63) >= 46 */
    ok = 0;
    for (k = 0; k < 100; k++) {
        world_load_bin(&world, MAPBIN_TESTLAND, MAPBIN_TESTLAND_LEN);
        world.unit_count = 2;
        world.feature[5][8] = FE_DOOR_OPEN;
        world_map_changed(&world);
        world.units[1].kind = CR_GOBLIN;
        world.units[1].owner = OWN_P2;
        world.units[1].x = 9;
        world.units[1].y = 5;
        world.units[1].mr = 46;
        memset(&book, 0, sizeof book);
        book.level[SP_SUBVERSION] = 1;
        world.units[0].ap = 40;
        world.units[0].mana = 80;
        rng_seed(&rng, 700 + k);
        if (spell_apply(&world, &book, 0, SP_SUBVERSION, 9, 5, false, &rng, &shot) == CAST_OK &&
            world.units[1].owner == OWN_P1)
            ok++;
    }
    check(ok > 15 && ok < 40, "0d: subversion at level 1 against MR 46 succeeds about 27 %");
    world.units[1].kind = CR_UNICORN;                  /* a mount carrying a wizard */
    world.units[1].rider_kind = CR_WIZARD;
    world.units[1].owner = OWN_P2;
    book.level[SP_SUBVERSION] = 1;
    world.units[0].ap = 40;
    world.units[0].mana = 80;
    check(spell_apply(&world, &book, 0, SP_SUBVERSION, 9, 5, false, &rng, &shot) == CAST_REJECTED,
          "0d: a mount with a wizard on its back cannot be subverted");
    world.units[1].rider_kind = 0xFF;
    world.units[1].flags |= UF_MOUNT;
    world.units[1].mr = 0;
    book.level[SP_SUBVERSION] = 1;
    world.units[0].ap = 40;
    check(spell_apply(&world, &book, 0, SP_SUBVERSION, 9, 5, false, &rng, &shot) == CAST_OK &&
          world.units[1].owner == OWN_P1,
          "0d: mounts can be subverted now");

    /* Magic Shield: Defence + 4 (L+1) + 12 for L+1 rounds, fixed */
    world_load_bin(&world, MAPBIN_TESTLAND, MAPBIN_TESTLAND_LEN);
    u = &world.units[0];
    memset(&book, 0, sizeof book);
    book.level[SP_MAGIC_SHIELD] = 1;
    u->ap = 40;
    u->mana = 80;
    spell_apply(&world, &book, 0, SP_MAGIC_SHIELD, u->x, u->y, false, &rng, &shot);
    check(effect_power(u, EFF_SHIELD) == 20 && items_defence(&world, 0) == 12 + 20,
          "0d: shield level 1 adds 20 Defence");
    check(items_magic_res(&world, 0) == u->mr,
          "0d: shields no longer add to magic resistance");

    /* Enchant lasts L+3 rounds */
    book.level[SP_ENCHANT] = 2;
    u->ap = 40;
    spell_apply(&world, &book, 0, SP_ENCHANT, u->x, u->y, false, &rng, &shot);
    check(u->effects[1].kind == EFF_MAGIC_WEAPON && u->effects[1].rounds == 5,
          "0d: enchant at level 2 lasts 5 rounds");

    /* Teleport: within D < 2L it lands exactly; the target must be free */
    world_load_bin(&world, MAPBIN_TESTLAND, MAPBIN_TESTLAND_LEN);
    world.unit_count = 1;
    u = &world.units[0];
    memset(&book, 0, sizeof book);
    book.level[SP_TELEPORT] = 5;                       /* exact up to D 9 */
    u->ap = 40;
    u->mana = 80;
    check(spell_apply(&world, &book, 0, SP_TELEPORT, u->x, (int16_t)(u->y + 2), false, &rng,
                      &shot) == CAST_OK && u->x == 6 && u->y == 8 && u->ap == 0,
          "0d: a short teleport lands exactly, AP are gone");
    u->ap = 40;
    book.level[SP_TELEPORT] = 5;
    world.feature[10][6] = FE_WALL;
    world_map_changed(&world);
    check(spell_apply(&world, &book, 0, SP_TELEPORT, 6, 10, false, &rng, &shot) == CAST_REJECTED &&
          u->y == 8,
          "0d: teleporting into a wall fails");
    check(book.level[SP_TELEPORT] == 4, "0d: and the cast is spent");

    /* potions: fixed +20/+25, duration floor((3 (L-1) + 10) / consumption) */
    world_load_bin(&world, MAPBIN_TESTLAND, MAPBIN_TESTLAND_LEN);
    world.unit_count = 1;
    u = &world.units[0];
    memset(&book, 0, sizeof book);
    book.level[SP_PROTECTION_POTION] = 1;
    u->ap = 40;
    u->mana = 80;
    brew_set_cauldron(&world, u->x, u->y, false, 0xFF);
    world.objects[world.object_count].x = u->x;
    world.objects[world.object_count].y = u->y;
    world.objects[world.object_count].tile = T_OBJ_CLOVER;
    world.object_count++;
    brew_cast(&world, &book, 0, SP_PROTECTION_POTION);
    u->ap = 40;
    check(brew_drink(&world, 0) && effect_power(u, EFF_PROTECT) == 25 &&
          items_defence(&world, 0) == 12 + 25,
          "0d: a protection potion adds 25 Defence");
    check(u->effects[0].rounds == 10 / CREATURES[CR_WIZARD].potion,
          "0d: a level-1 draught lasts 10 / consumption rounds");
}

/* D67 0e: throw and shot ranges, dragon fire. */
static void test_0e(void)
{
    Rng rng;
    uint8_t s, tg, k, hits = 0, lit = 0;

    world_load_bin(&world, MAPBIN_MANY_COLOURED_LAND, MAPBIN_MANY_COLOURED_LAND_LEN);
    world.unit_count = 0;
    area_reset();
    s = world_spawn_unit(&world, OWN_P1, CR_WIZARD, 2, 1);
    world.units[s].com = 10;
    check(items_throw_range(&world, s, 1) == 25 && items_throw_range(&world, s, 10) == 7 &&
          items_throw_range(&world, s, 3) == 11,
          "0e: throw range 2 C / weight + 5 (K6.4)");
    world.units[s].com = 100;
    check(items_throw_range(&world, s, 1) == 36, "0e: throw range is capped at 36");

    /* bow: 16 units, an enchanted bow 22 */
    world.units[s].com = 10;
    world.units[s].items[0] = OBJ_BOW;
    world.units[s].item_count = 1;
    world.units[s].in_use = 0;
    check(items_can_fire(&world, s) && items_fire_range(&world, s) == 16,
          "0e: a bow reaches 16 units");
    world.units[s].flags |= UF_MAGIC_WEAPON;
    check(items_fire_range(&world, s) == 22, "0e: an enchanted bow reaches 22");
    world.units[s].flags &= (uint8_t)~UF_MAGIC_WEAPON;
    tg = world_spawn_unit(&world, OWN_P2, CR_GOBLIN, 10, 1);   /* 8 fields = 16 units */
    world.units[tg].con = world.units[tg].con_max = 250;
    world.units[s].ap = 40;
    check(items_fire(&world, &rng, s, 10, 1, NULL), "0e: a target 16 units away is in reach");
    world.units[tg].x = 11;                                    /* 18 units */
    world.units[s].ap = 40;
    check(!items_fire(&world, &rng, s, 11, 1, NULL), "0e: 18 units are too far for the bow");

    /* dragon fire: reach 12, attack 35, ignites the target field */
    world.unit_count = 0;
    s = world_spawn_unit(&world, OWN_P1, CR_RED_DRAGON, 2, 20);
    tg = world_spawn_unit(&world, OWN_P2, CR_GOBLIN, 8, 20);
    check(items_can_fire(&world, s) && items_fire_range(&world, s) == 12,
          "0e: a dragon breathes fire 12 units far");
    for (k = 0; k < 100; k++) {
        uint8_t dmg = 0;
        area_reset();
        world.units[s].ap = 40;
        world.units[s].sta = 60;
        world.units[tg].con = world.units[tg].con_max = 250;
        rng_seed(&rng, 6000 + k);
        if (items_fire(&world, &rng, s, 8, 20, &dmg) && dmg)
            hits++;
        if (area_kind_at(&world, 8, 20) == AREA_FIRE)
            lit++;
    }
    /* A 35 against Defence 9: 71 rolls, 61 hurt = 86 %; grass flammability 12 of 20 = 60 % */
    check(hits > 70 && hits < 100, "0e: dragon fire hurts a goblin in most shots");
    check(lit > 40 && lit < 80, "0e: and sets the grass alight in about 60 %");
    world.units[tg].x = 9;                                     /* 14 units */
    world.units[s].ap = 40;
    check(!items_fire(&world, &rng, s, 9, 20, NULL), "0e: 14 units are too far for the dragon");
    area_reset();
}

/* D67 0g: the rider's own AP (K6.6), pixies, kill credit of riders. */
static void test_0g(void)
{
    uint8_t wizard, mount;
    Spellbook book;
    Rng rng;
    SpellShot shot;

    world_load_bin(&world, MAPBIN_TESTLAND, MAPBIN_TESTLAND_LEN);
    world.unit_count = 0;
    area_reset();
    rng_seed(&rng, 5);
    wizard = world_spawn_unit(&world, OWN_P1, CR_WIZARD, 6, 7);
    mount = world_spawn_unit(&world, OWN_P1, CR_UNICORN, 6, 6);
    world.units[wizard].ap = 40;
    world.units[wizard].ap_max = 40;
    check(ride_mount(&world, wizard, 6, 6), "0g: the wizard mounts");
    mount = 0;
    {
        Unit *m = &world.units[mount];
        check(m->rider_ap == 30 && m->rider_ap_max == 40,
              "0g: the rider keeps the AP he had left (40 - 10 for mounting)");
        m->ap = 56;
        memset(&book, 0, sizeof book);
        book.level[SP_MAGIC_SHIELD] = 2;
        m->mana = 80;
        check(spell_apply(&world, &book, mount, SP_MAGIC_SHIELD, m->x, m->y, false, &rng,
                          &shot) == CAST_OK,
              "0g: the rider casts from the saddle");
        check(m->rider_ap == 22 && m->ap == 56,
              "0g: the spell cost the rider 8 AP, the mount none");
        check(world_move_unit(&world, mount, 0, -1) && m->rider_ap == 22 && m->ap < 56,
              "0g: riding on costs the mount AP, not the rider");
        m->rider_ap = 0;
        check(!world_can_pay(&world, mount, ACT_CAST) && world_has_ap(&world, mount),
              "0g: a rider without AP cannot cast, the mount can still move");
        m->ap = 0;
        check(!world_has_ap(&world, mount), "0g: both spent: the unit is done");
        m->rider_con = 5;
        m->rider_con_max = 30;                       /* factor 6 */
        world_new_turn(&world);
        check(m->ap == m->ap_max && m->rider_ap == 40 / 6,
              "0g: the round refill gives the rider his own AP, divided by his own con");
    }

    /* a creature riding a mount that gets killed lands with its AP */
    world_load_bin(&world, MAPBIN_TESTLAND, MAPBIN_TESTLAND_LEN);
    world.unit_count = 0;
    wizard = world_spawn_unit(&world, OWN_P1, CR_WIZARD, 6, 7);
    mount = world_spawn_unit(&world, OWN_P1, CR_UNICORN, 6, 6);
    world.units[wizard].ap = 40;
    world.units[wizard].ap_max = 40;
    ride_mount(&world, wizard, 6, 6);
    world.units[0].ap = 40;
    check(ride_dismount(&world, 0) && world.unit_count == 2 &&
          world.units[1].ap == 30 && world.units[1].ap_max == 40,
          "0g: dismounting returns the rider with his own AP");

    /* F3: a wounded rider keeps his wounds through the ride */
    world.unit_count = 0;
    wizard = world_spawn_unit(&world, OWN_P1, CR_WIZARD, 6, 7);
    mount = world_spawn_unit(&world, OWN_P1, CR_UNICORN, 6, 6);
    world.units[wizard].ap = 40;
    world_set_wounds(&world.units[wizard], 3);
    ride_mount(&world, wizard, 6, 6);
    world.units[0].ap = 40;
    check(ride_dismount(&world, 0) && world.unit_count == 2 &&
          world.units[1].wounds == 3 && (world.units[1].flags & UF_WOUNDED),
          "F3: the rider gets off with his wounds");

    /* F31: the wounds bleed in the saddle (on a flying mount too); bled
     * out, the rider dies there, his pack falls, the mount stays */
    world.unit_count = 0;
    world.object_count = 0;
    wizard = world_spawn_unit(&world, OWN_P1, CR_WIZARD, 6, 7);
    mount = world_spawn_unit(&world, OWN_P1, CR_PEGASUS, 6, 6);
    world.units[wizard].ap = 40;
    world.units[wizard].con = 10;
    world.units[wizard].items[0] = OBJ_GOLD;
    world.units[wizard].item_count = 1;
    world_set_wounds(&world.units[wizard], 3);
    ride_mount(&world, wizard, 6, 6);
    world.units[0].flags |= UF_FLYING;
    world_new_turn(&world);
    check(world.unit_count == 1 && (world.units[0].flags & UF_RIDDEN) &&
          world.units[0].rider_con == 4,
          "F31: a wounded rider bleeds 2 con per wound in the saddle");
    world_new_turn(&world);
    check(world.unit_count == 1 && !(world.units[0].flags & UF_RIDDEN) &&
          world.units[0].kind == CR_PEGASUS && world.units[0].item_count == 0 &&
          world.object_count == 1,
          "F31: bled out, the rider dies in the saddle and drops his pack");

    /* pixies are always invisible (K2) */
    world.unit_count = 0;
    wizard = world_spawn_unit(&world, OWN_P1, CR_PIXIE, 6, 6);
    check((world.units[wizard].flags & UF_INVISIBLE) != 0,
          "0g: a pixie is invisible from the start");
    world_new_turn(&world);
    world_new_turn(&world);
    check((world.units[wizard].flags & UF_INVISIBLE) != 0,
          "0g: and stays so when no potion runs");
}

/* D67 0h: sight by K11 - heights, covers, shot lines, fire. */
static void test_0h(void)
{
    Sight s;
    uint8_t x, y, a;

    world_load_bin(&world, MAPBIN_TESTLAND, MAPBIN_TESTLAND_LEN);
    for (y = 0; y < world.h; y++)
        for (x = 0; x < world.w; x++) {
            world.floor[y][x] = FL_GRASS;
            world.feature[y][x] = FE_NONE;
        }
    memset(world.roof, 0, sizeof world.roof);
    area_reset();
    world_map_changed(&world);
    world.unit_count = 0;

    check(sight_in_reach(9, 0, 19) && !sight_in_reach(10, 0, 19) &&
          sight_in_reach(6, 6, 19) && !sight_in_reach(7, 7, 19) &&
          sight_in_reach(11, 0, 23) && sight_in_reach(7, 7, 23) &&
          !sight_in_reach(8, 8, 23),
          "0h: reach octagon: 9/6 on the ground, 11/7 in the air");

    /* a ground observer sees a flier behind a wall, but not a ground target */
    world_spawn_unit(&world, OWN_P1, CR_WIZARD, 10, 10);
    world.feature[10][12] = FE_WALL;
    world_map_changed(&world);
    a = world_spawn_unit(&world, OWN_P2, CR_GIANT_BAT, 14, 10);
    world.units[a].flags |= UF_FLYING;
    {
        uint8_t b = world_spawn_unit(&world, OWN_P2, CR_GOBLIN, 14, 10);
        sight_init(&s, OWN_P1);
        sight_compute(&world, &s);
        check(sight_unit_visible(&s, &world, &world.units[a]),
              "0h: a flier behind a wall is seen from the ground");
        check(!sight_unit_visible(&s, &world, &world.units[b]),
              "0h: the goblin behind it on the ground is not");
        world.units[b].x = 11;                       /* next to the wall, 1 field away */
        check(sight_unit_visible(&s, &world, &world.units[a]), "0h: still the flier");
        world_remove_unit(&world, b);
    }

    /* the eight neighbours are always seen, even diagonally behind a corner */
    world.unit_count = 1;
    world.feature[9][10] = FE_WALL;
    world.feature[10][9] = FE_WALL;
    world_map_changed(&world);
    sight_init(&s, OWN_P1);
    sight_compute(&world, &s);
    check(sight_visible(&s, &world, 9, 9), "0h: a neighbour behind a corner is seen (D < 4)");
    world.feature[9][10] = world.feature[10][9] = FE_NONE;
    world.feature[10][12] = FE_NONE;
    world_map_changed(&world);

    /* under a roof the other height is not seen at all */
    {
        uint16_t cell = (uint16_t)(10 * world.w + 10);
        world.roof[cell >> 3] |= (uint8_t)(0x80u >> (cell & 7));
        a = world_spawn_unit(&world, OWN_P2, CR_GIANT_BAT, 13, 10);
        world.units[a].flags |= UF_FLYING;
        sight_init(&s, OWN_P1);
        sight_compute(&world, &s);
        check(!sight_unit_visible(&s, &world, &world.units[a]),
              "0h: a ground observer under a roof does not see fliers");
        memset(world.roof, 0, sizeof world.roof);
        world_remove_unit(&world, a);
    }

    /* a flier sees the ground target in the open but not under a canopy or roof */
    {
        uint8_t f = world_spawn_unit(&world, OWN_P1, CR_GIANT_BAT, 10, 10);
        uint8_t v1, v2, v3;
        world.units[f].flags |= UF_FLYING;
        world.unit_count = 0;
        f = world_spawn_unit(&world, OWN_P1, CR_GIANT_BAT, 10, 10);
        world.units[f].flags |= UF_FLYING;
        v1 = world_spawn_unit(&world, OWN_P2, CR_GOBLIN, 16, 10);   /* open grass */
        v2 = world_spawn_unit(&world, OWN_P2, CR_GOBLIN, 10, 17);
        v3 = world_spawn_unit(&world, OWN_P2, CR_GOBLIN, 15, 5);
        world.floor[17][10] = FL_FOREST;                           /* canopy */
        {
            uint16_t cell = (uint16_t)(5 * world.w + 15);
            world.roof[cell >> 3] |= (uint8_t)(0x80u >> (cell & 7));  /* roofed */
        }
        world_map_changed(&world);
        sight_init(&s, OWN_P1);
        sight_compute(&world, &s);
        check(sight_unit_visible(&s, &world, &world.units[v1]),
              "0h: the flier sees a goblin in the open");
        check(!sight_unit_visible(&s, &world, &world.units[v2]),
              "0h: but not one under the forest canopy");
        check(!sight_unit_visible(&s, &world, &world.units[v3]),
              "0h: nor one under a roof");
        check(sight_visible(&s, &world, 10, 17) && sight_visible(&s, &world, 15, 5),
              "0h: the terrain itself is still in view");
        world.units[v2].x = 10;
        world.units[v2].y = 11;                                    /* canopy next to it */
        world.floor[11][10] = FL_FOREST;
        world_map_changed(&world);
        sight_compute(&world, &s);
        check(sight_unit_visible(&s, &world, &world.units[v2]),
              "0h: next to the flier the canopy hides nobody (D < 4)");
        memset(world.roof, 0, sizeof world.roof);
        world.floor[17][10] = world.floor[11][10] = FL_GRASS;
    }

    /* the Magic Eye: D <= 3L + 10 around the point, walls ignored, shows the hidden */
    world.unit_count = 1;
    world.units[0].x = 3;
    world.units[0].y = 3;
    world.feature[20][20] = FE_WALL;
    world_map_changed(&world);
    {
        uint8_t h = world_spawn_unit(&world, OWN_P2, CR_GOBLIN, 22, 20);
        world.units[h].flags |= UF_INVISIBLE;
        sight_init(&s, OWN_P1);
        sight_compute(&world, &s);
        sight_add_eye(&s, &world, 20, 20, 13);                      /* level 1: 13 units */
        check(sight_visible(&s, &world, 26, 20) && !sight_visible(&s, &world, 27, 20),
              "0h: the eye sees 13 units straight (6 fields)");
        check(sight_unit_visible(&s, &world, &world.units[h]),
              "0h: the eye shows ground targets even behind cover");
        world_remove_unit(&world, h);
    }
    world.feature[20][20] = FE_NONE;

    /* fire and blob block the sight (K11.4) */
    world.unit_count = 0;
    world_spawn_unit(&world, OWN_P1, CR_WIZARD, 10, 10);
    world_map_changed(&world);
    sight_init(&s, OWN_P1);
    sight_compute(&world, &s);
    check(sight_visible(&s, &world, 16, 10), "0h: open grass: the field is seen");
    area_set(&world, AREA_FIRE, 3, OWN_P2, 13, 10);
    sight_init(&s, OWN_P1);
    sight_compute(&world, &s);
    check(!sight_visible(&s, &world, 16, 10) && sight_visible(&s, &world, 13, 10),
          "0h: a burning field blocks what lies behind it");
    area_reset();
    world_map_changed(&world);

    /* shot lines by heights (K11.6) */
    {
        uint16_t cell = (uint16_t)(10 * world.w + 10);
        check(sight_shot_clear(&world, 10, 10, false, 16, 10, false),
              "0h: ground to ground over open grass is clear");
        world.feature[10][13] = FE_WALL;
        world_map_changed(&world);
        check(!sight_shot_clear(&world, 10, 10, false, 16, 10, false) &&
              sight_shot_clear(&world, 10, 10, false, 16, 10, true) &&
              sight_shot_clear(&world, 10, 10, true, 16, 10, false) &&
              sight_shot_clear(&world, 10, 10, true, 16, 10, true),
              "0h: a wall stops ground fire only, never a shot with the air in it");
        world.roof[cell >> 3] |= (uint8_t)(0x80u >> (cell & 7));
        check(!sight_shot_clear(&world, 10, 10, false, 16, 10, true),
              "0h: a ground shooter under a roof cannot shoot up");
        check(!sight_shot_clear(&world, 16, 10, true, 10, 10, false),
              "0h: nor can a flier hit a roofed ground target");
        memset(world.roof, 0, sizeof world.roof);
        world.feature[10][13] = FE_NONE;
        world_map_changed(&world);
    }
}

/* D67 KI 1: the creature loop of K10.3 on an open field. */
static void open_field(void)
{
    uint8_t x, y;
    world_load_bin(&world, MAPBIN_TESTLAND, MAPBIN_TESTLAND_LEN);
    for (y = 0; y < world.h; y++)
        for (x = 0; x < world.w; x++) {
            world.floor[y][x] = FL_GRASS;
            world.feature[y][x] = FE_NONE;
        }
    memset(world.roof, 0, sizeof world.roof);
    world.object_count = 0;
    world.unit_count = 0;
    area_reset();
    world_map_changed(&world);
}

static void test_ki1(void)
{
    Rng rng;
    AiView v;
    uint8_t a, b, c;
    uint8_t k;

    rng_seed(&rng, 21);

    /* the view: nearest first, own side and the invisible left out */
    open_field();
    a = world_spawn_unit(&world, OWN_P2, CR_GOBLIN, 10, 10);
    b = world_spawn_unit(&world, OWN_P1, CR_WIZARD, 14, 10);
    c = world_spawn_unit(&world, OWN_P1, CR_DWARF, 12, 10);
    world_spawn_unit(&world, OWN_P2, CR_TROLL, 11, 10);
    k = world_spawn_unit(&world, OWN_P1, CR_PIXIE, 11, 11);   /* always invisible */
    ai_build_view(&world, a, &v);
    check(v.en_n == 2 && v.en[0].id == world.units[c].id && v.en[1].id == world.units[b].id &&
          v.en[0].dist == 4 && v.en[1].dist == 8,
          "ki1: the view lists the visible foes nearest first, not the pixie or friends");
    (void)k;
    world.units[b].x = 30;                     /* beyond 9 fields */
    ai_build_view(&world, a, &v);
    check(v.en_n == 1, "ki1: what lies beyond the octagon is not seen");

    /* a strong creature attacks a weak one next to it, a weak one runs */
    open_field();
    a = world_spawn_unit(&world, OWN_NEUTRAL, CR_BEAR, 10, 10);
    b = world_spawn_unit(&world, OWN_P1, CR_PIXIE, 11, 10);
    world.units[b].flags &= (uint8_t)~UF_INVISIBLE;
    world.units[b].con = world.units[b].con_max = 200;
    ai_creature_turn(&world, &rng, NULL, world.units[a].id);
    check(world.units[b].con < 200 || world.units[a].ap < world.units[a].ap_max,
          "ki1: the bear attacks what it can hurt");

    open_field();
    a = world_spawn_unit(&world, OWN_NEUTRAL, CR_GOBLIN, 10, 10);
    b = world_spawn_unit(&world, OWN_P1, CR_GIANT, 13, 10);   /* Combat 21 against Defence 9 */
    world.units[b].flags |= 0;
    ai_creature_turn(&world, &rng, NULL, world.units[a].id);
    check(world.units[a].x < 10 || world.units[a].x != 10,
          "ki1: a goblin flees from a giant");
    check(world_range(&world, world.units[a].x, world.units[a].y, 13, 10) > 6,
          "ki1: it ends farther from the giant");

    /* bound: no flight, a bodyguard never runs */
    open_field();
    a = world_spawn_unit(&world, OWN_P2, CR_GOBLIN, 10, 10);
    b = world_spawn_unit(&world, OWN_P1, CR_GIANT, 11, 10);
    world_engage(&world, a);
    check(world_engaged(&world, a), "ki1: next to the giant the goblin is bound");
    {
        uint8_t ox = world.units[a].x;
        ai_creature_turn(&world, &rng, NULL, world.units[a].id);
        check(world.units[a].x == ox, "ki1: a bound creature does not run");
    }
    (void)b;

    /* sleepers do nothing until they see a foe */
    open_field();
    a = world_spawn_unit(&world, OWN_P2, CR_BEAR, 10, 10);
    world.units[a].plan_flags = 0x40;
    ai_creature_turn(&world, &rng, NULL, world.units[a].id);
    check((world.units[a].plan_flags & 0x40) != 0 && world.units[a].ap == world.units[a].ap_max,
          "ki1: a sleeper without a foe in sight stays asleep");
    world_spawn_unit(&world, OWN_P1, CR_DWARF, 14, 10);
    ai_creature_turn(&world, &rng, NULL, world.units[a].id);
    check((world.units[a].plan_flags & 0x40) == 0, "ki1: it wakes at the sight of a foe");

    /* objects: it walks to a sword, picks it up and wields it */
    open_field();
    a = world_spawn_unit(&world, OWN_P2, CR_DWARF, 10, 10);
    world.objects[0].x = 13;
    world.objects[0].y = 10;
    world.objects[0].tile = OBJECTS[OBJ_SWORD].tile;
    world.object_count = 1;
    for (k = 0; k < 4 && world.units[a].item_count == 0; k++) {
        world.units[a].ap = world.units[a].ap_max;
        ai_creature_turn(&world, &rng, NULL, world.units[a].id);
    }
    check(world.units[a].item_count == 1 && world.units[a].items[0] == OBJ_SWORD,
          "ki1: it fetches the sword");
    world.units[a].ap = world.units[a].ap_max;
    world.units[a].in_use = NO_ITEM;
    ai_creature_turn(&world, &rng, NULL, world.units[a].id);
    check(world.units[a].in_use == 0, "ki1: and takes it in hand");

    /* loot goes to the wizard: thrown when in range, carried before */
    open_field();
    a = world_spawn_unit(&world, OWN_P2, CR_GOBLIN, 10, 10);
    b = world_spawn_unit(&world, OWN_P2, CR_WIZARD, 14, 10);
    world.units[a].items[0] = OBJ_GOLD;
    world.units[a].item_count = 1;
    ai_creature_turn(&world, &rng, NULL, world.units[a].id);
    check(world.units[a].item_count == 0 && items_kind_at(&world, 14, 10) == OBJ_GOLD,
          "ki1: the goblin throws its loot to the wizard (it lands on his field)");

    /* an archer fires at a target it can hurt, ignores one it cannot */
    open_field();
    a = world_spawn_unit(&world, OWN_P2, CR_DWARF, 10, 10);
    world.units[a].items[0] = OBJ_BOW;
    world.units[a].item_count = 1;
    world.units[a].in_use = 0;
    b = world_spawn_unit(&world, OWN_P1, CR_GOBLIN, 16, 10);
    world.units[b].con = world.units[b].con_max = 250;
    ai_creature_turn(&world, &rng, NULL, world.units[a].id);
    check(world.units[a].ap < world.units[a].ap_max,
          "ki1: the bowman shoots (or walks) at the goblin within 16 units");

    /* a flier takes off at once and lands to fight a ground foe */
    open_field();
    a = world_spawn_unit(&world, OWN_P2, CR_GIANT_BAT, 10, 10);
    b = world_spawn_unit(&world, OWN_P1, CR_DWARF, 12, 10);
    world.units[b].con = world.units[b].con_max = 250;
    ai_creature_turn(&world, &rng, NULL, world.units[a].id);
    check(world.units[b].con < 250 || (world.units[a].flags & UF_FLYING),
          "ki1: the bat takes off or lands and bites");

    /* nothing in view: a bodyguard follows its wizard and stands within 5 units */
    open_field();
    a = world_spawn_unit(&world, OWN_P2, CR_GOBLIN, 10, 10);
    b = world_spawn_unit(&world, OWN_P2, CR_WIZARD, 16, 10);
    for (k = 0; k < 4; k++) {
        world.units[a].ap = world.units[a].ap_max;
        ai_creature_turn(&world, &rng, NULL, world.units[a].id);
    }
    check(world_range(&world, world.units[a].x, world.units[a].y, 16, 10) < 5,
          "ki1: creatures without a plan guard their wizard within 5 units");
    {
        uint8_t ox = world.units[a].x, oy = world.units[a].y;
        world.units[a].ap = world.units[a].ap_max;
        ai_creature_turn(&world, &rng, NULL, world.units[a].id);
        check(world.units[a].x == ox && world.units[a].y == oy,
              "ki1: and then stay put");
    }
    (void)c;
}

/* D67 KI 2: the AI wizard - profile, priorities, spells, routes, triggers. */
static void test_ki2(void)
{
    static AiProfile prof[OWN_NEUTRAL];
    static Spellbook bk[OWN_NEUTRAL];
    AiCtx ctx = {0};
    AiEnv env = {0};
    AiView v;
    Game g;
    Rng rng;
    Turns t;
    uint8_t wz, foe, k;

    rng_seed(&rng, 31);
    world_load_bin(&world, MAPBIN_MANY_COLOURED_LAND, MAPBIN_MANY_COLOURED_LAND_LEN);
    area_reset();
    check(ai_scenario_load(&world, prof, SCN_MANY_COLOURED_LAND, SCN_MANY_COLOURED_LAND_LEN) &&
          prof[OWN_P2].present && prof[OWN_P2].mana == 120 && prof[OWN_P2].con == 63 &&
          prof[OWN_P2].com == 9 && prof[OWN_P2].def == 10 && prof[OWN_P2].mr == 90,
          "ki2: the scenario carries Torquemada's values (K10.2)");
    check(prof[OWN_P2].prio[SP_MAGIC_LIGHTNING] == 220 && prof[OWN_P2].prio[SP_MAGIC_BOLT] == 200 &&
          prof[OWN_P2].prio[SP_SUPER_POTION] == 190 && prof[OWN_P2].prio[SP_MAGIC_SHIELD] == 60,
          "ki2: and his spell priorities");
    check(spellbook_load(bk, SCN_MANY_COLOURED_LAND, SCN_MANY_COLOURED_LAND_LEN) &&
          bk[OWN_P2].level[SP_MAGIC_FIRE] == 5 && bk[OWN_P2].level[SP_VAMPIRE] == 2 &&
          bk[OWN_P2].level[SP_MAGIC_BOLT] == 3,
          "ki2: the book has the original's levels");
    check(world.route_n == 8 && world.route_summon_n == 8 && world.routes[0].n == 4 &&
          (world.routes[0].flags & 0x40),
          "ki2: eight routes with wizard flags");

    /* the profile reaches the wizard */
    game_init(&g, -1, -1, 1, 1, &rng);
    wz = ai_wizard_of(&world, OWN_P2);
    ai_profile_apply(prof, &world, &g, OWN_P2);
    check(wz != NO_UNIT && world.units[wz].mana_max == 120 && world.units[wz].con == 63 &&
          world.units[wz].com == 9 && g.wizard_vp[OWN_P2] == 24,
          "ki2: his unit takes the values");

    /* plans: a wizard gets a route flagged for wizards and is never a bodyguard */
    for (k = 0; k < 20; k++) {
        ai_plan_new(&world, &rng, wz);
        if (world.units[wz].plan_route == 0xFF || !(world.routes[world.units[wz].plan_route].flags & 0x40) ||
            (world.units[wz].plan_flags & 0x80))
            break;
    }
    check(k == 20, "ki2: the wizard walks only wizard routes and is never aggressive");
    {   /* summoned creatures: a bat may take the flier route, a dwarf never */
        uint8_t bat, dw, fl = 0, bad = 0;
        bat = world_spawn_unit(&world, OWN_P2, CR_GIANT_BAT, 10, 10);
        dw = world_spawn_unit(&world, OWN_P2, CR_DWARF, 11, 10);
        for (k = 0; k < 60; k++) {
            ai_plan_new(&world, &rng, bat);
            if (world.units[bat].plan_route == 5)
                fl = 1;
            ai_plan_new(&world, &rng, dw);
            if (world.units[dw].plan_route == 5)
                bad = 1;
        }
        check(fl && !bad, "ki2: the flier route fits a bat, not a dwarf (K10.7)");
        world_remove_unit(&world, dw);
        world_remove_unit(&world, bat);
    }
    {   /* aggression by the table: spiders (99 %) become bodyguards almost always */
        uint8_t sp, n = 0;
        sp = world_spawn_unit(&world, OWN_P2, CR_GIANT_SPIDER, 10, 10);
        for (k = 0; k < 50; k++) {
            ai_plan_new(&world, &rng, sp);
            if (world.units[sp].plan_flags & 0x80)
                n++;
        }
        check(n >= 45, "ki2: a spider is a bodyguard in 99 of 100 cases");
        world_remove_unit(&world, sp);
    }

    /* triggers wake sleepers once the terrain changed on their field */
    world.trig_n = 1;
    world.trig_x[0] = 20;
    world.trig_y[0] = 20;
    world.trig_id[0] = 3;
    check(!(world.trig_fired[0] & 8), "ki2: no trigger fired yet");
    world.feature[20][20] = FE_DOOR_CLOSED;
    world.units[wz].x = 19;
    world.units[wz].y = 20;
    world.units[wz].ap = 40;
    world_open_door(&world, wz, 20, 20);
    check((world.trig_fired[0] & 8) != 0, "ki2: opening the door fires its trigger");
    {
        uint8_t sl = world_spawn_unit(&world, OWN_NEUTRAL, CR_BEAR, 30, 30);
        world.units[sl].plan_flags = (uint8_t)(0x40 | 3);
        world.units[sl].ap = world.units[sl].ap_max;
        ai_creature_turn(&world, &rng, NULL, world.units[sl].id);
        check(!(world.units[sl].plan_flags & 0x40), "ki2: the fired trigger wakes the sleeper");
        world_remove_unit(&world, sl);
    }

    /* spell choice: priorities are halved for good after a cast (K10.5) */
    open_field();
    world.route_n = 0;
    memset(prof, 0, sizeof prof);
    memset(bk, 0, sizeof bk);
    prof[OWN_P2].present = true;
    prof[OWN_P2].prio[SP_MAGIC_BOLT] = 200;
    prof[OWN_P2].prio[SP_MAGIC_SHIELD] = 60;
    prof[OWN_P2].prio[SP_GOBLIN] = 95;               /* odd: held back at first */
    bk[OWN_P2].level[SP_MAGIC_BOLT] = 3;
    bk[OWN_P2].level[SP_MAGIC_SHIELD] = 2;
    bk[OWN_P2].level[SP_GOBLIN] = 2;
    wz = world_spawn_unit(&world, OWN_P2, CR_WIZARD, 10, 10);
    world.units[wz].mana = world.units[wz].mana_max = 120;
    world.units[wz].ap = world.units[wz].ap_max = 40;
    foe = world_spawn_unit(&world, OWN_P1, CR_TROLL, 14, 10);
    world.units[foe].con = world.units[foe].con_max = 250;
    env.book = &bk[OWN_P2];
    env.profile = &prof[OWN_P2];
    ai_build_view(&world, wz, &v);
    check(ai_wizard_cast(&world, &rng, &env, world.units[wz].id, &v) == A_DONE &&
          bk[OWN_P2].level[SP_MAGIC_BOLT] == 2 && prof[OWN_P2].prio[SP_MAGIC_BOLT] == 100,
          "ki2: the bolt (priority 200) is cast first and its priority halves to 100");
    world.units[wz].ap = 40;
    ai_build_view(&world, wz, &v);
    ai_wizard_cast(&world, &rng, &env, world.units[wz].id, &v);
    check(prof[OWN_P2].prio[SP_MAGIC_BOLT] == 50, "ki2: and halves again (persistent)");
    world.units[wz].ap = 40;
    ai_build_view(&world, wz, &v);
    check(ai_wizard_cast(&world, &rng, &env, world.units[wz].id, &v) == A_DONE &&
          bk[OWN_P2].level[SP_MAGIC_SHIELD] == 1,
          "ki2: then the shield (60) follows, but only when none is active");
    world.units[wz].ap = 40;
    ai_build_view(&world, wz, &v);
    {
        uint8_t level_before = bk[OWN_P2].level[SP_MAGIC_SHIELD];
        ai_wizard_cast(&world, &rng, &env, world.units[wz].id, &v);
        check(bk[OWN_P2].level[SP_MAGIC_SHIELD] == level_before,
              "ki2: no second shield while one is active");
    }
    for (k = 0; k < 3; k++) {            /* until a pass finds nothing to cast */
        world.units[wz].ap = 40;
        ai_build_view(&world, wz, &v);
        ai_wizard_cast(&world, &rng, &env, world.units[wz].id, &v);
    }
    check(prof[OWN_P2].prio[SP_GOBLIN] == 94 || bk[OWN_P2].level[SP_GOBLIN] < 2,
          "ki2: a pass without a cast rounds an odd priority down (95 -> 94), then it can be cast");

    /* summoning keeps a reserve of 40 mana in peace */
    world.unit_count = 0;
    wz = world_spawn_unit(&world, OWN_P2, CR_WIZARD, 10, 10);
    memset(prof, 0, sizeof prof);
    memset(bk, 0, sizeof bk);
    prof[OWN_P2].present = true;
    prof[OWN_P2].prio[SP_GOBLIN] = 100;
    bk[OWN_P2].level[SP_GOBLIN] = 2;
    world.units[wz].mana = world.units[wz].mana_max = 30;   /* 2 x 3 = 6 -> 24 left: too little */
    world.units[wz].ap = world.units[wz].ap_max = 40;
    ai_build_view(&world, wz, &v);
    check(ai_wizard_cast(&world, &rng, &env, world.units[wz].id, &v) == A_NONE &&
          world.unit_count == 1,
          "ki2: below 40 mana left he does not summon in peace");
    world.units[wz].mana = world.units[wz].mana_max = 120;
    ai_build_view(&world, wz, &v);
    check(ai_wizard_cast(&world, &rng, &env, world.units[wz].id, &v) == A_DONE &&
          world.unit_count == 3,
          "ki2: with the reserve he summons two goblins (level 2)");

    /* the phase: wizard first, creatures after, a fallback profile without a scenario */
    open_field();
    memset(bk, 0, sizeof bk);
    bk[OWN_P2].level[SP_MAGIC_BOLT] = 2;
    game_init(&g, -1, -1, 1, 1, &rng);
    ctx.books = bk;
    ctx.game = &g;
    ctx.profiles = NULL;
    memset(&t, 0, sizeof t);
    t.phase = OWN_P2;
    rng_seed(&t.rng, 8);
    wz = world_spawn_unit(&world, OWN_P2, CR_WIZARD, 10, 10);
    foe = world_spawn_unit(&world, OWN_P1, CR_TROLL, 15, 10);
    world.units[foe].con = world.units[foe].con_max = 250;
    ai_wizard_phase(&t, &world, &ctx);
    check(bk[OWN_P2].level[SP_MAGIC_BOLT] < 2,
          "ki2: without a scenario table the wizard still casts from a fallback priority list");
}

static void test_flight(void)
{
    FieldLayers f;

    world_load_bin(&world, MAPBIN_TESTLAND, MAPBIN_TESTLAND_LEN);
    check(world.unit_count == 13 && world.units[11].kind == CR_GIANT_BAT &&
          world.units[11].ap_fly == 62 && world.units[11].owner == OWN_P1,
          "fly: testland has a p1 giant bat (index 11)");
    check(world.units[0].ap_fly == 0, "fly: the wizard cannot fly");

    check(world_unit_at(&world, 14, 2, UL_GROUND) == 11 &&
          world_unit_at(&world, 14, 2, UL_AIR) == NO_UNIT,
          "fly: bat starts on the ground layer");
    check(!world_take_off(&world, 0), "fly: wizard cannot take off");
    check(world_take_off(&world, 11) && world.units[11].ap == 46,
          "fly: take-off costs 6 AP, 18 of 24 become 46 of 62 (K2)");
    check((world.units[11].flags & UF_FLYING) != 0 &&
          world_unit_at(&world, 14, 2, UL_AIR) == 11 &&
          world_unit_at(&world, 14, 2, UL_GROUND) == NO_UNIT,
          "fly: bat occupies the air layer");

    {   /* flight ignores terrain: two steps east, onto the river */
        check(world_move_unit(&world, 11, 1, 0) && world_move_unit(&world, 11, 1, 0),
              "fly: straight over anything");
    }
    check(world.units[11].x == 16 && world.units[11].y == 2 &&
          world.units[11].ap == 38 && world_floor(&world, 16, 2) == FL_WATER,
          "fly: 2 air steps onto the river cost 8 AP");
    check(!world_land(&world, 11), "fly: no landing on water");
    check(world_move_unit(&world, 11, 1, 0) && world_land(&world, 11) &&
          world.units[11].ap == 13,
          "fly: landing is free, 34 of 62 become 13 of 24");
    check(world_unit_at(&world, 17, 2, UL_GROUND) == 11 &&
          !(world.units[11].flags & UF_FLYING),
          "fly: back on the ground layer");

    {   /* ground and air unit share a field */
        world.units[11].x = 6;
        world.units[11].y = 6;
        world.units[11].flags |= UF_FLYING;
        check(world_unit_at(&world, 6, 6, UL_GROUND) == 0 &&
              world_unit_at(&world, 6, 6, UL_AIR) == 11,
              "fly: wizard below, bat above");
        view_set_sight(NULL);
        view_compose(&world, 6, 6, &f);
        check(has_layer(&f, T_WIZARD_P1) && has_layer(&f, T_AIR_SHADOW) &&
              has_layer(&f, T_GIANT_BAT_P1),
              "fly: view stacks ground unit, shadow and flyer");
        check(f.air != 0, "fly: the flyer layer is flagged");
        view_set_origin(2, 2);
        view_set_cursor(6, 6, T_CURSOR_GREEN);
        view_invalidate();
        view_update(&world);
        check(view_dirty(4, 4) && fast_equals_reference(),
              "fly: fast path composes the stack as well");
        view_set_sight(NULL);
        view_clean();
    }

    {   /* round end refills the layer budget (PM 34: bat 24 ground, 62 air) */
        world.units[11].flags |= UF_FLYING;
        world_new_turn(&world);
        check(world.units[11].ap == 62, "fly: airborne refill uses ap_fly");
        world.units[11].flags &= (uint8_t)~UF_FLYING;
        world_new_turn(&world);
        check(world.units[11].ap == 24, "fly: grounded refill uses ap_max");
    }

    {   /* sight from the air: range 11, walls do not block */
        Sight s;
        world.unit_count = 12;
        world.units[11].x = 6;
        world.units[11].y = 6;
        world.units[11].flags |= UF_FLYING;
        sight_init(&s, OWN_P1);
        sight_compute(&world, &s);
        check(sight_visible(&s, &world, 6, 2) && sight_visible(&s, &world, 6, 1),
              "fly: the bat looks over the house walls");
        check(sight_visible(&s, &world, 6, 17) && !sight_visible(&s, &world, 6, 18),
              "fly: air range 11");
    }
}

static void test_bump_and_look(void)
{
    char buf[24];
    Sight s;

    world_load_bin(&world, MAPBIN_TESTLAND, MAPBIN_TESTLAND_LEN);
    world.feature[5][6] = FE_DOOR_CLOSED;         /* north of the wizard */
    world_map_changed(&world);
    check(world_bump_kind(&world, 0, 0, -1) == BUMP_DOOR, "bump: closed door ahead");
    check(world_open_door(&world, 0, 6, 5) &&
          world.feature[5][6] == FE_DOOR_OPEN && world.units[0].ap == 34,
          "bump: opening costs 6 AP and opens the door");
    check(world_bump_kind(&world, 0, 0, -1) == BUMP_OK, "bump: open door is walkable");
    world.feature[5][6] = FE_DOOR_CLOSED;
    world_map_changed(&world);
    check(!world_open_door(&world, 11, 6, 5),
          "bump: the bat has no hands (CF_USE)");
    world.units[0].ap = 5;
    check(!world_open_door(&world, 0, 6, 5) && world.feature[5][6] == FE_DOOR_CLOSED,
          "bump: not enough AP leaves the door shut");
    world.units[0].ap = 40;
    world.units[0].x = 6;
    world.units[0].y = 3;
    check(world_bump_kind(&world, 0, 0, -1) == BUMP_TERRAIN, "bump: wall is terrain");
    world.units[1].x = 5;
    world.units[1].y = 3;
    check(world_bump_kind(&world, 0, -1, 0) == BUMP_UNIT, "bump: unit ahead");
    check(world_bump_kind(&world, 0, 0, 1) == BUMP_OK, "bump: free step is fine");

    view_set_sight(NULL);
    check(strcmp(describe_field(&world, NULL, 5, 3, buf, sizeof buf),
                 "Zauberer-2") == 0, "look: names the unit on the field");
    check(strcmp(describe_field(&world, NULL, 6, 2, buf, sizeof buf), "Wand") == 0,
          "look: names the wall");
    world.units[11].flags |= UF_FLYING;
    world.units[11].x = 8;
    world.units[11].y = 8;
    check(strcmp(describe_field(&world, NULL, 8, 8, buf, sizeof buf),
                 "Riesenfledermaus (Luft)") == 0, "look: flying units get a suffix");

    world.unit_count = 2;                 /* wizards only, enemies far */
    world.units[0].x = 20;
    world.units[0].y = 13;
    world.units[1].x = 0;
    world.units[1].y = 35;
    sight_init(&s, OWN_P1);
    sight_compute(&world, &s);
    check(strcmp(describe_field(&world, &s, 6, 3, buf, sizeof buf), "Unerforscht.") == 0,
          "look: unexplored stays dark");
    check(strcmp(describe_field(&world, &s, 20, 13, buf, sizeof buf), "Zauberer-1") == 0,
          "look: own units are always named");
    check(strcmp(describe_field(&world, &s, 0, 35, buf, sizeof buf), "Unerforscht.") == 0,
          "look: enemies stay dark without sight");
    world.units[1].x = 21;                /* steps into sight */
    world.units[1].y = 13;
    sight_compute(&world, &s);
    check(strcmp(describe_field(&world, &s, 21, 13, buf, sizeof buf), "Zauberer-2") == 0,
          "look: enemy is named when in sight");
    view_set_sight(NULL);
}

static void test_combat(void)
{
    Rng rng;
    CombatResult r;
    uint8_t seed_hits = 0, k;

    /* damage = RND(min(255, 2 (A+1))) - Def: chance (n-1-Def)/n (K6.2) */
    check(combat_hit_chance(10, 10) == 50 && combat_hit_chance(20, 10) == 73 &&
          combat_hit_chance(10, 30) == 0 && combat_hit_chance(200, 20) == 91,
          "combat: hit chance of the original roll");
    {
        uint16_t sum = 0;
        uint8_t zero = 0;
        for (k = 0; k < 200; k++) {
            uint8_t d;
            rng_seed(&rng, 5 + k);
            d = combat_roll(&rng, 10, 10);
            sum = (uint16_t)(sum + d);
            if (d == 0)
                zero++;
            check(d <= 11, "combat: the roll never exceeds 2 (A+1) - 1 - Def");
        }
        check(zero > 70 && zero < 130, "combat: about half the rolls miss at A = Def");
        check(sum > 200 && sum < 700, "combat: the roll averages about 3 over defence");
    }

    world_load_bin(&world, MAPBIN_TESTLAND, MAPBIN_TESTLAND_LEN);
    world.unit_count = 2;
    world.units[1].x = 7;                       /* goblin east of the wizard */
    world.units[1].y = 6;
    world.units[1].kind = CR_GOBLIN;
    world.units[1].com = 9;
    world.units[1].def = 9;
    world.units[1].con = world.units[1].con_max = 32;
    world.units[1].ap = 30;
    world.units[1].sta = 45;

    check(!combat_melee(&world, &rng, 0, 0, &r), "combat: no self attacks");
    world.units[1].x = 20;
    check(!combat_melee(&world, &rng, 0, 1, &r), "combat: no attacks across the map");
    world.units[1].x = 7;
    world.units[1].owner = OWN_P1;
    check(!combat_melee(&world, &rng, 0, 1, &r), "combat: no friendly fire");
    world.units[1].owner = OWN_NEUTRAL;
    world.units[1].flags |= UF_FLYING;
    check(!combat_melee(&world, &rng, 0, 1, &r), "combat: no melee against flyers");
    world.units[1].flags &= (uint8_t)~UF_FLYING;

    /* many seeded exchanges: statistics instead of pinned rolls */
    for (k = 0; k < 200; k++) {
        rng_seed(&rng, 1000 + k);
        world.units[0].ap = 40;
        world.units[0].sta = 60;
        world.units[0].con = 30;
        world.units[1].ap = 30;
        world.units[1].sta = 45;
        world.units[1].con = 32;
        world_set_wounds(&world.units[0], 0);
        world_set_wounds(&world.units[1], 0);
        world.units[0].flags &= (uint8_t)~UF_ENGAGED;
        if (combat_melee(&world, &rng, 0, 1, &r) && r.hit)
            seed_hits++;
    }
    /* wizard Combat 10 against Defence 9: 12/22 */
    check(seed_hits > 80 && seed_hits < 140, "combat: ~55 % hits over 200 seeds");
    check(world.units[1].x == 7, "combat: survivor is still in place");

    {   /* costs and the return blow (K6.2) */
        CombatResult a, b;
        world.units[0].ap = 40;
        world.units[0].sta = 60;
        world.units[0].con = 30;
        world.units[1].ap = 30;
        world.units[1].sta = 45;
        world.units[1].con = 32;
        rng_seed(&rng, 7);
        combat_melee(&world, &rng, 0, 1, &a);
        check(world.units[0].ap == 32 && world.units[0].sta == 52,
              "combat: melee costs 8 AP and 8 stamina");
        check(a.returned || a.died, "combat: the defender answers");
        if (a.returned)
            check(world.units[1].ap == 26 && world.units[1].sta == 41,
                  "combat: the return blow costs 4 AP and 4 stamina");
        world.units[0].ap = 40;
        world.units[0].sta = 60;
        world.units[0].con = 30;
        world.units[1].ap = 30;
        world.units[1].sta = 45;
        world.units[1].con = 32;
        rng_seed(&rng, 7);
        combat_melee(&world, &rng, 0, 1, &b);
        check(a.hit == b.hit && a.damage == b.damage && a.returned == b.returned,
              "combat: same seed, same outcome");

        world.units[1].ap = 3;                  /* too tired to answer */
        world.units[1].con = 32;
        world.units[0].ap = 40;
        world.units[0].sta = 60;
        combat_melee(&world, &rng, 0, 1, &b);
        check(!b.returned, "combat: no return blow below 4 AP");
        world.units[1].ap = 30;
        world.units[1].sta = 3;
        world.units[1].con = 32;
        world.units[0].ap = 40;
        world.units[0].sta = 60;
        combat_melee(&world, &rng, 0, 1, &b);
        check(!b.returned, "combat: no return blow below 4 stamina");
        world.units[0].ap = 40;
        world.units[0].sta = 7;
        check(!combat_melee(&world, &rng, 0, 1, &b),
              "combat: an attack needs 8 stamina");
        /* every attack is answered, not only the first one of the round */
        world.units[1].con = 200;
        world.units[1].con_max = 200;
        {
            uint8_t answered = 0;
            for (k = 0; k < 3; k++) {
                world.units[0].con = 30;
                world.units[0].ap = 40;
                world.units[0].sta = 60;
                world.units[1].ap = 30;
                world.units[1].sta = 45;
                rng_seed(&rng, 31 + k);
                combat_melee(&world, &rng, 0, 1, &b);
                answered = (uint8_t)(answered + (b.returned ? 1 : 0));
            }
            check(answered == 3, "combat: every blow of a round is answered (R7)");
        }
        world.units[1].con = world.units[1].con_max = 32;
    }

    world.units[1].con = 1;                     /* mortal blow */
    world.units[1].ap = 0;
    {
        uint8_t count_before = world.unit_count, tries = 0;
        do {
            tries++;
            rng_seed(&rng, 90 + tries);
            world.units[0].ap = 40;
            world.units[0].sta = 60;
            world.units[1].ap = 0;
            world.units[1].con = 1;
        } while (!combat_melee(&world, &rng, 0, 1, &r) || !r.hit);
        check(r.died && world.unit_count == count_before - 1,
              "combat: the dead leave the world");
    }

    {   /* wounds bleed 2 con each per round (R9) */
        world_load_bin(&world, MAPBIN_TESTLAND, MAPBIN_TESTLAND_LEN);
        world.unit_count = 1;
        world.units[0].con = world.units[0].con_max = 30;
        world_set_wounds(&world.units[0], 1);
        world_new_turn(&world);
        check(world.units[0].con == 28, "combat: a wound bleeds 2 con a round (R9)");
        world_set_wounds(&world.units[0], 3);
        world_new_turn(&world);
        check(world.units[0].con == 22, "combat: three wounds bleed 6");
        world_set_wounds(&world.units[0], 1);
        world.units[0].con = 1;
        world_new_turn(&world);
        check(world.unit_count == 0, "combat: bleeding to death removes the unit");
    }

    {   /* a hit above a quarter of ConMax opens a wound (K6.3) */
        world_load_bin(&world, MAPBIN_TESTLAND, MAPBIN_TESTLAND_LEN);
        world.unit_count = 1;
        world.units[0].con = world.units[0].con_max = 40;
        combat_damage(&world, 0, 10, CR_GOBLIN, OWN_NEUTRAL, true, NULL);
        check(world.units[0].wounds == 0 && world.units[0].con == 30,
              "combat: 10 of 40 is exactly a quarter, no wound");
        combat_damage(&world, 0, 11, CR_GOBLIN, OWN_NEUTRAL, true, NULL);
        check(world.units[0].wounds == 1, "combat: more than a quarter opens a wound");
        combat_damage(&world, 0, 5, CR_GOBLIN, OWN_NEUTRAL, true, NULL);
        check(world.units[0].wounds == 1, "combat: a small hit opens no further wound");
    }

    {   /* bound units do not move, the arrival binds only the mover (K11.7) */
        world_load_bin(&world, MAPBIN_TESTLAND, MAPBIN_TESTLAND_LEN);
        world.unit_count = 2;
        world.units[0].ap = 40;
        world.units[1].x = 8;
        world.units[1].y = 6;
        world.units[1].kind = CR_GOBLIN;
        world.units[1].owner = OWN_P2;
        check(world_move_unit(&world, 0, 1, 0) && world.units[0].x == 7,
              "bound: stepping up to an enemy is allowed");
        check(world_engaged(&world, 0) && !world_engaged(&world, 1),
              "bound: only the mover is bound, not the one he walked up to");
        check(world_bump_kind(&world, 0, -1, 0) == BUMP_BOUND &&
              !world_move_unit(&world, 0, -1, 0) && world.units[0].x == 7,
              "bound: the bound unit cannot step back");
        world_release(&world, world.units[0].owner);   /* his phase ends */
        check(!world_engaged(&world, 0) && world_move_unit(&world, 0, -1, 0),
              "bound: free to leave in the next phase");
        world.units[0].ap = 40;
        world_engage(&world, 0);                         /* no enemy next to him */
        check(!world_engaged(&world, 0), "bound: nobody next to him, no binding");
        world.units[0].x = 7;
        world_engage(&world, 0);
        check(world_engaged(&world, 0), "bound: next to an enemy again");
        world.units[1].x = 20;                           /* enemy gone */
        check(!world_engaged(&world, 0), "bound: the binding loosens when the enemy is gone");
    }

    {   /* attacking binds the attacker, the answer binds the defender */
        world_load_bin(&world, MAPBIN_TESTLAND, MAPBIN_TESTLAND_LEN);
        world.unit_count = 2;
        world.units[0].ap = 40;
        world.units[0].sta = 60;
        world.units[1].x = 7;
        world.units[1].y = 6;
        world.units[1].kind = CR_GOBLIN;
        world.units[1].owner = OWN_P2;
        world.units[1].con = world.units[1].con_max = 200;
        world.units[1].ap = 30;
        world.units[1].sta = 45;
        rng_seed(&rng, 3);
        combat_melee(&world, &rng, 0, 1, &r);
        check(world_engaged(&world, 0) && world_engaged(&world, 1),
              "bound: attacker and answering defender are bound");
        world.units[1].flags |= UF_INVISIBLE;
        check(!world_engaged(&world, 0), "bound: an invisible enemy does not bind");
    }

    {   /* through the turn flow: bound in contact, free one phase later */
        Turns tt;
        world_load_bin(&world, MAPBIN_TESTLAND, MAPBIN_TESTLAND_LEN);
        world.unit_count = 2;
        world.units[0].ap = 40;
        world.units[1].x = 8;
        world.units[1].y = 6;
        world.units[1].kind = CR_GOBLIN;
        turn_init(&tt, &world, 3, 1u << OWN_P1);
        tt.round1_lock = false;
        check(world_move_unit(&world, 0, 1, 0), "combat: step up to the enemy");
        turn_end_phase(&tt, &world);           /* P1 done, P2 passes, round 2 */
        check(tt.round == 2 && !world_engaged(&world, 0),
              "combat: free again in the next round");
    }

    {   /* D59: a grazing wild animal lets a passer-by go; only a grudge,
         * a charge at the disturber or its territory make it hostile */
        world_load_bin(&world, MAPBIN_TESTLAND, MAPBIN_TESTLAND_LEN);
        world.unit_count = 2;
        world.units[1].x = (uint8_t)(world.units[0].x + 1);
        world.units[1].y = world.units[0].y;
        world.units[1].owner = OWN_NEUTRAL;
        world.units[1].kind = CR_GORILLA;              /* peaceful */
        world.units[1].grudge = 0;
        world.units[1].alarm = 0;
        world.units[1].post_x = 0xFF;
        world.units[1].herd_dir = 0;
        check(!combat_hostile_to(&world, &world.units[1], OWN_P1,
                                 world.units[0].x, world.units[0].y),
              "combat: a peaceful animal is not hostile to a passer-by (D59)");
        world.units[1].grudge = (uint8_t)(1u << OWN_P1);
        check(combat_hostile_to(&world, &world.units[1], OWN_P1,
                                world.units[0].x, world.units[0].y),
              "combat: an animal with a grudge is hostile (D59)");
        world.units[1].grudge = 0;
        world.units[1].kind = CR_BEAR;                 /* territorial */
        world.units[1].post_x = 0;
        world.units[1].post_y = 0;
        check(!combat_hostile_to(&world, &world.units[1], OWN_P1,
                                 world.units[0].x, world.units[0].y),
              "combat: outside its territory the bear lets you pass (D59)");
        world.units[1].post_x = world.units[1].x;
        world.units[1].post_y = world.units[1].y;
        check(combat_hostile_to(&world, &world.units[1], OWN_P1,
                                world.units[0].x, world.units[0].y),
              "combat: inside its territory the bear is hostile (D59)");
        world.units[1].kind = CR_GOBLIN;
        world.units[1].post_x = 0xFF;
        check(combat_hostile_to(&world, &world.units[1], OWN_P1, 0, 0),
              "combat: a neutral monster is always hostile (D59)");
    }

    {   /* weapons and defence items (K6.1): the weapon in use adds Combat, the
         * best defence of any carried item adds to Defence - not summed */
        world_load_bin(&world, MAPBIN_TESTLAND, MAPBIN_TESTLAND_LEN);
        world.unit_count = 1;
        world.units[0].com = 10;
        world.units[0].def = 12;
        world.units[0].con = world.units[0].con_max = 30;
        world.units[0].item_count = 0;
        world.units[0].in_use = NO_ITEM;
        check(items_combat(&world, 0) == 10 && items_defence(&world, 0) == 12,
              "items: bare values");
        world.units[0].items[0] = OBJ_SWORD;
        world.units[0].items[1] = OBJ_SHIELD;
        world.units[0].item_count = 2;
        check(items_defence(&world, 0) == 12 + 13,
              "items: the best carried defence counts (shield 13, sword 4 not added)");
        check(items_combat(&world, 0) == 10, "items: a sword in the pack adds no Combat");
        world.units[0].in_use = 0;
        check(items_combat(&world, 0) == 20, "items: the sword in use adds 10 Combat");
        world.units[0].flags |= UF_MAGIC_WEAPON;
        check(items_combat(&world, 0) == 30 && items_defence(&world, 0) == 12 + 26,
              "items: enchanted values count double");
        world.units[0].flags &= (uint8_t)~UF_MAGIC_WEAPON;
        world.units[0].con = 14;                       /* 30/14 = 2 */
        check(items_combat(&world, 0) == 10 && items_defence(&world, 0) == 12,
              "items: the constitution factor halves Combat and Defence");
        world.units[0].con = 1;
        check(items_combat(&world, 0) >= 1, "items: effective values stay at least 1");
        world.units[0].con = 30;
        world.units[0].kind = CR_GOLD_DRAGON;          /* no weapon use */
        check(items_combat(&world, 0) == 10, "items: creatures without weapon use get no bonus");
    }

    {   /* terrain attacks (K6.4): 1.5 C >= toughness to try, RND(2 C) >= to smash */
        world_load_bin(&world, MAPBIN_TESTLAND, MAPBIN_TESTLAND_LEN);
        world.unit_count = 1;
        world.units[0].x = 7;                   /* beside the house door */
        world.units[0].y = 6;
        world.units[0].ap = 40;
        world.units[0].sta = 60;
        world.feature[5][8] = FE_DOOR_CLOSED;    /* closed for the attack */
        world_map_changed(&world);
        {
            bool destroyed = false;
            check(combat_terrain(&world, &rng, 0, 3, 2, &destroyed) == 0,
                  "combat: walls are indestructible");
            check(combat_terrain(&world, &rng, 0, 9, 5, &destroyed) == 0,
                  "combat: open ground has nothing to hit");
            check(combat_terrain(&world, &rng, 0, 8, 5, &destroyed) == 0 &&
                  world.units[0].ap == 40,
                  "combat: a weak fighter (Combat 10) does not even try a door");
            world.units[0].com = 50;
            {
                uint8_t hits = 0;
                uint8_t gen = world.generation;
                while (!destroyed && hits < 100) {
                    world.units[0].ap = 40;
                    world.units[0].sta = 60;
                    rng_seed(&rng, 500 + hits);
                    combat_terrain(&world, &rng, 0, 8, 5, &destroyed);
                    hits++;
                }
                check(destroyed && world.feature[5][8] == FE_NONE &&
                      world.generation != gen,
                      "combat: a strong fighter smashes the door");
                check(world.units[0].ap == 34 && world.units[0].sta == 54,
                      "combat: a blow at terrain costs 6 AP and 6 stamina");
            }
        }
    }
}

static void test_spells(void)
{
    Spellbook book;
    Rng rng;
    static Spellbook scnbooks[OWN_NEUTRAL];

    check(spellbook_load(scnbooks, SCN_MANY_COLOURED_LAND, SCN_MANY_COLOURED_LAND_LEN) &&
          scnbooks[OWN_P1].level[SP_GIANT_BAT] == 2 &&
          scnbooks[OWN_P1].level[SP_MAGIC_BOLT] == 1 &&
          scnbooks[OWN_P1].level[SP_DWARF] == 1 &&
          scnbooks[OWN_P2].level[SP_GOBLIN] == 2,
          "spells: books from the scenario file");
    rng_seed(&rng, 11);
    memset(&book, 0, sizeof book);
    book.level[SP_GIANT_BAT] = 2;       /* p1 test book for the casts below */
    check(SUMMON_KIND[SP_DWARF] == CR_DWARF && SUMMON_KIND[SP_GIANT_BAT] == CR_GIANT_BAT &&
          SUMMON_KIND[SP_MAGIC_BOLT] == 0xFF, "spells: summon kinds from the table");

    world_load_bin(&world, MAPBIN_TESTLAND, MAPBIN_TESTLAND_LEN);
    world.unit_count = 1;                       /* the wizard at 6,6 */
    check(spell_can_cast(&world, &book, 0, SP_GIANT_BAT), "spells: castable");
    world.units[0].flags |= UF_FLYING;
    check(!spell_can_cast(&world, &book, 0, SP_GIANT_BAT), "spells: not from the air");
    world.units[0].flags &= (uint8_t)~UF_FLYING;
    world.units[0].mana = 3;
    check(!spell_can_cast(&world, &book, 0, SP_GIANT_BAT), "spells: too little mana");
    world.units[0].mana = 80;
    world.units[0].ap = 5;
    check(!spell_can_cast(&world, &book, 0, SP_GIANT_BAT), "spells: too little AP");

    world.units[0].ap = 40;
    {   /* K5.3: a cast at level 2 calls two creatures, costs 2 (2+1) mana, burns a level */
        const CreatureDef *bat = &CREATURES[CR_GIANT_BAT];
        uint8_t got = spell_summon(&world, &book, 0, SP_GIANT_BAT, &rng);
        check(got == 2 && world.unit_count == 3,
              "0f: a level-2 cast summons two creatures");
        check(world.units[1].kind == CR_GIANT_BAT && world.units[1].owner == OWN_P1 &&
              world.units[1].ap == 24 && world.units[1].sta == 75,
              "spells: summoned with its own AP and stamina");
        check(world.units[1].com == bat->combat && world.units[1].def == bat->defence &&
              world.units[1].con == bat->con,
              "0f: summoned creatures have the plain table values");
        check(world.units[0].ap == 32 && world.units[0].mana == 80 - 6 &&
              book.level[SP_GIANT_BAT] == 1,
              "0f: 8 AP, mana 2 x (2+1), the level is used up");
        check(spell_summon(&world, &book, 0, SP_GIANT_BAT, &rng) == 1 &&
              world.units[0].mana == 80 - 6 - 4 && book.level[SP_GIANT_BAT] == 0,
              "0f: the last level calls one more, then the spell is spent");
        check(!spell_can_cast(&world, &book, 0, SP_GIANT_BAT),
              "0f: level 0 cannot be cast");
        world_remove_unit(&world, 3);
        world_remove_unit(&world, 2);
        world_remove_unit(&world, 1);
        world.units[0].mana = 72;
        book.level[SP_GIANT_BAT] = 2;
    }

    {   /* no room: mana lost, nothing appears (GDD 7.2) */
        uint8_t k;
        static const int8_t DX[8] = {0, 1, 1, 1, 0, -1, -1, -1};
        static const int8_t DY[8] = {-1, -1, 0, 1, 1, 1, 0, -1};
        for (k = 0; k < 8; k++) {
            int16_t x = (int16_t)(world.units[0].x + DX[k]);
            int16_t y = (int16_t)(world.units[0].y + DY[k]);
            if (world_wrap(&world, &x, &y) && !world_blocks(&world, x, y))
                world_spawn_unit(&world, OWN_NEUTRAL, CR_GOBLIN, (uint8_t)x, (uint8_t)y);
        }
        world.units[0].ap = 40;
        check(spell_summon(&world, &book, 0, SP_GIANT_BAT, &rng) == 0 &&
              world.units[0].mana == 72 - 6 && book.level[SP_GIANT_BAT] == 1,
              "spells: without room the mana is lost and the level too");
    }
}

/* ---------- wild animals and random scenarios (D35) ---------- */

static void strip_neutrals(void)
{
    uint8_t i = world.unit_count;
    while (i-- > 0)
        if (world.units[i].owner == OWN_NEUTRAL)
            world_remove_unit(&world, i);
}

static uint8_t count_chests(void)
{
    uint8_t x, y, n = 0;
    for (y = 0; y < world.h; y++)
        for (x = 0; x < world.w; x++)
            if (world.feature[y][x] == FE_CHEST || world.feature[y][x] == FE_CHEST_FREE)
                n++;
    return n;
}

static uint32_t world_digest(void)
{
    uint32_t h = 2166136261u;
    uint8_t i;
    for (i = 0; i < world.unit_count; i++)
        h = (h ^ (uint32_t)(world.units[i].kind * 7 + world.units[i].x * 131 +
                            world.units[i].y * 4099)) * 16777619u;
    for (i = 0; i < world.object_count; i++)
        h = (h ^ (uint32_t)(world.objects[i].tile + world.objects[i].x * 977 +
                            world.objects[i].y * 31)) * 16777619u;
    return h ^ count_chests();
}

/* Did the ring see a swing by this creature kind? */
static bool swung(uint8_t kind)
{
    GameEvent ev[EVENT_RING];
    uint8_t n = events_drain(ev, EVENT_RING), i;
    for (i = 0; i < n; i++)
        if (ev[i].type == EV_SWING && ev[i].kind == kind)
            return true;
    return false;
}

static void rounds_of_neutrals(Rng *r, uint8_t n)
{
    uint8_t k;
    for (k = 0; k < n; k++) {
        world_new_turn(&world);
        ai_run_hunters(&world, r, OWN_NEUTRAL, NO_UNIT);
    }
}

static void test_wild(void)
{
    Rng r;
    uint32_t d1, d2;
    uint8_t i, animals = 0, chests_before, wiz = NO_UNIT;
    bool placed_ok = true;

    /* the same seed populates the same world, another seed another one */
    world_load_bin(&world, MAPBIN_MANY_COLOURED_LAND, MAPBIN_MANY_COLOURED_LAND_LEN);
    strip_neutrals();
    chests_before = count_chests();
    rng_seed(&r, 5);
    populate_scenario(&world, &r);
    d1 = world_digest();
    for (i = 0; i < world.unit_count; i++) {
        const Unit *u = &world.units[i];
        if (u->kind == CR_WIZARD) {
            if (u->owner == OWN_P1)
                wiz = i;
            continue;
        }
        animals++;
        if (u->owner != OWN_NEUTRAL || CREATURES[u->kind].wild == WILD_NONE)
            placed_ok = false;
    }
    for (i = 0; i < world.unit_count; i++) {
        uint8_t k;
        if (world.units[i].kind == CR_WIZARD)
            continue;
        for (k = 0; k < world.unit_count; k++)
            if (world.units[k].kind == CR_WIZARD &&
                world_distance(&world, world.units[i].x, world.units[i].y,
                               world.units[k].x, world.units[k].y) < POP_WIZARD_GAP)
                placed_ok = false;
        if (world.floor[world.units[i].y][world.units[i].x] == FL_WATER)
            placed_ok = false;
    }
    check(animals >= POP_ANIMALS_MIN && animals <= POP_ANIMALS_MIN + 3 && placed_ok,
          "d35: 5-8 wild animals, far from the wizards, not in water");
    check(count_chests() >= chests_before + POP_CHESTS_MIN,
          "d35: at least five new chests");
    check(world.object_count >= POP_FINDS_MIN + POP_KEYS,
          "d35: loose finds and chest keys lie around");
    world_load_bin(&world, MAPBIN_MANY_COLOURED_LAND, MAPBIN_MANY_COLOURED_LAND_LEN);
    strip_neutrals();
    rng_seed(&r, 5);
    populate_scenario(&world, &r);
    d2 = world_digest();
    check(d1 == d2, "d35: same seed, same world");
    world_load_bin(&world, MAPBIN_MANY_COLOURED_LAND, MAPBIN_MANY_COLOURED_LAND_LEN);
    strip_neutrals();
    rng_seed(&r, 6);
    populate_scenario(&world, &r);
    check(world_digest() != d1, "d35: another seed, another world");

    {   /* D55: the ground picks the animal (data/habitats.csv) */
        uint8_t seed, k, valid = 1, finds_ok = 1;
        unsigned croc = 0, bear = 0, spider = 0, lion = 0, total = 0;
        for (seed = 1; seed <= 60; seed++) {
            world_load_bin(&world, MAPBIN_MANY_COLOURED_LAND, MAPBIN_MANY_COLOURED_LAND_LEN);
            strip_neutrals();
            rng_seed(&r, seed);
            populate_scenario(&world, &r);
            for (k = 0; k < world.unit_count; k++) {
                const Unit *u = &world.units[k];
                uint8_t wt;
                bool water_near = false;
                int8_t dx, dy;
                if (u->kind == CR_WIZARD)
                    continue;
                for (dy = -1; dy <= 1; dy++)
                    for (dx = -1; dx <= 1; dx++)
                        if ((dx || dy) && world_floor(&world, (int16_t)(u->x + dx),
                                                      (int16_t)(u->y + dy)) == FL_WATER)
                            water_near = true;
                wt = (uint8_t)(HABITAT[u->kind][world.floor[u->y][u->x]] +
                               (water_near ? HABITAT_SHORE[u->kind] : 0));
                if (!wt || world.floor[u->y][u->x] == FL_BRIDGE)
                    valid = 0;               /* no habitat, or on a bridge */
                total++;
                if (u->kind == CR_CROCODILE) croc++;
                if (u->kind == CR_BEAR) bear++;
                if (u->kind == CR_GIANT_SPIDER) spider++;
                if (u->kind == CR_LION) lion++;
            }
            for (k = 0; k < world.object_count; k++) {
                uint16_t t = world.objects[k].tile;
                uint8_t f = world.floor[world.objects[k].y][world.objects[k].x];
                if (t == OBJECTS[OBJ_CHEST_KEY].tile)
                    continue;
                if (f == FL_MAGIC_WOOD && t != OBJECTS[OBJ_FAIRYWING].tile &&
                    t != OBJECTS[OBJ_MAGIC_MUSHROOM].tile && t != OBJECTS[OBJ_MAGIC_APPLE].tile &&
                    t != OBJECTS[OBJ_MISTLETOE].tile && world.objects[k].x > 12)
                    finds_ok = 0;   /* x > 12: the houses' own equipment is fixed */
                if (f == FL_SHADOW_WOOD && t != OBJECTS[OBJ_SULPH].tile &&
                    t != OBJECTS[OBJ_NITRO].tile && t != OBJECTS[OBJ_RUNE_STONE].tile)
                    finds_ok = 0;
            }
        }
        check(valid, "d55: every wild animal starts on ground that suits it, never on a bridge");
        check(croc > 0 && bear > 0 && spider > 0 && lion > 0 && total > 200,
              "d55: crocodiles, bears, spiders and lions all turn up over 60 seeds");
        check(finds_ok, "d55: loose finds fit the wood they lie in");
    }

    /* behaviour on the open road (row 12 is path) */
    world_load_bin(&world, MAPBIN_MANY_COLOURED_LAND, MAPBIN_MANY_COLOURED_LAND_LEN);
    strip_neutrals();
    for (i = 0; i < world.unit_count; i++)
        if (world.units[i].owner == OWN_P1)
            wiz = i;
    world.units[wiz].x = 20;
    world.units[wiz].y = 12;
    world.units[wiz].con = world.units[wiz].con_max;
    {   /* peaceful: leaves a neighbour alone until attacked */
        uint8_t g = world_spawn_unit(&world, OWN_NEUTRAL, CR_GORILLA, 21, 12);
        uint8_t gid = world.units[g].id;
        rng_seed(&r, 9);
        events_reset();
        rounds_of_neutrals(&r, 4);
        check(!swung(CR_GORILLA), "d35: a peaceful gorilla leaves the wizard alone");
        g = world_find_unit(&world, gid);
        world_provoke(&world, g, OWN_P1);
        check(world.units[g].grudge == 1, "d35: an attack is remembered");
        g = world_find_unit(&world, gid);
        world.units[g].x = 21;               /* next to the wizard again */
        world.units[g].y = 12;
        events_reset();
        rounds_of_neutrals(&r, 2);
        check(swung(CR_GORILLA), "d35: provoked, it fights back");
        g = world_find_unit(&world, gid);
        if (g != NO_UNIT)
            world_remove_unit(&world, g);
    }
    world_load_bin(&world, MAPBIN_MANY_COLOURED_LAND, MAPBIN_MANY_COLOURED_LAND_LEN);
    strip_neutrals();                    /* the gorilla may have won */
    for (i = 0; i < world.unit_count; i++)
        if (world.units[i].owner == OWN_P1)
            wiz = i;
    world.units[wiz].x = 17;
    world.units[wiz].y = 26;
    {   /* territorial: defends its home, ignores the far wizard */
        uint8_t l = world_spawn_unit(&world, OWN_NEUTRAL, CR_LION, 26, 26);
        ai_set_post(&world, l);
        events_reset();
        rounds_of_neutrals(&r, 3);
        check(!swung(CR_LION), "d35: the lion ignores a wizard outside its territory");
        for (i = 0; i < world.unit_count; i++)
            if (world.units[i].owner == OWN_P1)
                wiz = i;
        world.units[wiz].x = 24;             /* two fields from its home */
        world.units[wiz].y = 26;
        events_reset();
        rounds_of_neutrals(&r, 2);
        check(swung(CR_LION), "d35: an intruder in the territory is attacked");
    }

    /* picking up from the own field or a neighbour, not further */
    world_load_bin(&world, MAPBIN_MANY_COLOURED_LAND, MAPBIN_MANY_COLOURED_LAND_LEN);
    {
        uint8_t k, apple = 0xFF, sword = 0xFF, w0 = 0, before, da = 0xFF, ds = 0xFF;
        for (k = 0; k < world.unit_count; k++)
            if (world.units[k].owner == OWN_P1)
                w0 = k;
        for (k = 0; k < world.object_count; k++) {   /* the own house's */
            uint8_t d = world_distance(&world, world.units[w0].x, world.units[w0].y,
                                       world.objects[k].x, world.objects[k].y);
            if (world.objects[k].tile == T_OBJ_APPLE && d < da) {
                apple = k;
                da = d;
            }
            if (world.objects[k].tile == T_OBJ_SWORD && d < ds) {
                sword = k;
                ds = d;
            }
        }
        world.units[w0].ap = 40;
        check(sword != 0xFF && !items_pick_up_object(&world, w0, sword),
              "pickup: two fields away is out of reach");
        before = world.units[w0].item_count;
        check(apple != 0xFF && items_pick_up_object(&world, w0, apple) &&
              world.units[w0].item_count == before + 1,
              "pickup: the apple next door is taken");
    }

    /* scared animals (D37): one roll per herd, all alike */
    world_load_bin(&world, MAPBIN_MANY_COLOURED_LAND, MAPBIN_MANY_COLOURED_LAND_LEN);
    strip_neutrals();
    {
        uint8_t e1 = world_spawn_unit(&world, OWN_NEUTRAL, CR_ELEPHANT, 24, 12);
        uint8_t lead = world.units[e1].id, k, alarmed = 0, same = 1;
        uint8_t e2 = world_spawn_unit(&world, OWN_NEUTRAL, CR_ELEPHANT, 25, 12);
        uint8_t e3 = world_spawn_unit(&world, OWN_NEUTRAL, CR_ELEPHANT, 26, 12);
        uint8_t mode, d0;
        world.units[e1].group = world.units[e2].group = world.units[e3].group = lead;
        world_disturb(&world, 22, 12, OWN_P1);
        rng_seed(&r, 21);
        d0 = world_distance(&world, 25, 12, 22, 12);
        ai_run_hunters(&world, &r, OWN_NEUTRAL, NO_UNIT);
        mode = world.units[world_find_unit(&world, lead)].alarm_charge;
        for (k = 0; k < world.unit_count; k++)
            if (world.units[k].kind == CR_ELEPHANT) {
                if (world.units[k].alarm)
                    alarmed++;
                if (world.units[k].alarm_charge != mode)
                    same = 0;
            }
        check(alarmed == 3 && same && world.disturb_n == 0,
              "d37: the whole herd is scared and acts alike");
        if (!mode)
            check(world_distance(&world, world.units[world_find_unit(&world, lead)].x,
                                 world.units[world_find_unit(&world, lead)].y,
                                 22, 12) > d0 - 1,
                  "d37: fleeing, the leader runs away from the trouble");
    }
    {   /* a charging elephant tramples whoever is in the way */
        uint8_t e, g, gid, k;
        world_load_bin(&world, MAPBIN_MANY_COLOURED_LAND, MAPBIN_MANY_COLOURED_LAND_LEN);
        strip_neutrals();
        for (k = 0; k < world.unit_count; k++)
            if (world.units[k].owner == OWN_P2)
                world.units[k].x = 30, world.units[k].y = 20;
        e = world_spawn_unit(&world, OWN_NEUTRAL, CR_ELEPHANT, 20, 12);
        g = world_spawn_unit(&world, OWN_P2, CR_GOBLIN, 21, 12);
        gid = world.units[g].id;
        world_spawn_unit(&world, OWN_P2, CR_GOBLIN, 23, 12);
        world.units[e].alarm = 3;
        world.units[e].alarm_charge = 0;     /* fleeing east, from x = 17 */
        world.units[e].alarm_x = 17;
        world.units[e].alarm_y = 12;
        world.units[e].alarm_owner = OWN_P1;
        rng_seed(&r, 4);
        events_reset();
        world_new_turn(&world);
        ai_run_hunters(&world, &r, OWN_NEUTRAL, NO_UNIT);
        g = world_find_unit(&world, gid);
        check(g == NO_UNIT || world.units[g].con < world.units[g].con_max ||
              world.units[g].x != 21,
              "d37: the stampeding elephant tramples the goblin");
        events_reset();
    }

    /* a wizard riding a gryphon survives the round change (bug: the
     * reaction flag shared bit 64 with UF_RIDDEN) */
    world_load_bin(&world, MAPBIN_MANY_COLOURED_LAND, MAPBIN_MANY_COLOURED_LAND_LEN);
    strip_neutrals();
    {
        uint8_t k, wz = 0, gr;
        Game g;
        for (k = 0; k < world.unit_count; k++)
            if (world.units[k].owner == OWN_P1)
                wz = k;
        world.units[wz].x = 20;
        world.units[wz].y = 12;
        gr = world_spawn_unit(&world, OWN_P1, CR_GRYPHON, 21, 12);
        world.units[wz].ap = 40;
        (void)gr;
        rng_seed(&r, 1);
        game_init(&g, world.portal_x, world.portal_y, world.portal_rmin,
                  world.portal_rmax, &r);
        for (k = 0; k < world.unit_count; k++)
            if (world.units[k].owner == OWN_P1 && world.units[k].kind == CR_WIZARD)
                wz = k;
        check(ride_mount_adjacent(&world, wz), "ride: the wizard mounts the gryphon");
        world_new_turn(&world);
        world_new_turn(&world);
        check(game_outcome(&g, &world, OWN_P1) == OUT_RUNNING &&
              !game_over(&g, &world),
              "ride: a riding wizard lives through the round change");
        {
            unsigned others = (unsigned)UF_UNDEAD | (unsigned)UF_FLYING |
                              (unsigned)UF_MOUNT | (unsigned)UF_WOUNDED |
                              (unsigned)UF_INVISIBLE | (unsigned)UF_MAGIC_WEAPON |
                              (unsigned)UF_ENGAGED;
            check(((unsigned)UF_RIDDEN & others) == 0,
                  "ride: UF_RIDDEN has a bit of its own");
        }
    }

    /* spells reach through tall grass, eyes do not (D36) */
    world_load_bin(&world, MAPBIN_MANY_COLOURED_LAND, MAPBIN_MANY_COLOURED_LAND_LEN);
    {   /* its own strip of ground: grass, three tall grass, grass */
        int16_t x;
        for (x = 7; x <= 11; x++) {
            world.floor[19][x] = (x >= 8 && x <= 10) ? FL_TALL_GRASS : FL_GRASS;
            world.feature[19][x] = FE_NONE;
        }
        world_map_changed(&world);
    }
    check(world.floor[19][9] == FL_TALL_GRASS &&
          !sight_has_los(&world, 7, 19, 11, 19) &&
          sight_has_spell_los(&world, 7, 19, 11, 19),
          "d36: a spell flies through tall grass that blocks the view");

    /* herds come only later, then cross and leave */
    world_load_bin(&world, MAPBIN_MANY_COLOURED_LAND, MAPBIN_MANY_COLOURED_LAND_LEN);
    strip_neutrals();
    rng_seed(&r, 3);
    check(!populate_herd(&world, &r, 1), "d35: no herd in the first rounds");
    {
        uint8_t tries = 0, herd = 0, left = 0;
        while (!populate_herd(&world, &r, 10) && tries++ < 200)
            ;
        for (i = 0; i < world.unit_count; i++)
            if (world.units[i].herd_dir)
                herd++;
        check(herd >= 1 && herd <= 4 &&
              CREATURES[world.units[world.unit_count - 1].kind].wild == WILD_HERD,
              "d35: a herd of herd animals enters at an edge");
        check(!populate_herd(&world, &r, 11), "d35: one herd at a time");
        rounds_of_neutrals(&r, 40);
        for (i = 0; i < world.unit_count; i++)
            if (world.units[i].herd_dir)
                left++;
        check(left == 0, "d35: the herd crosses the map and leaves");
    }
    events_reset();
}

static void test_bolt(void)
{
    Spellbook book;
    SpellShot shot;
    Rng rng;
    uint8_t hits = 0, k;

    world_load_bin(&world, MAPBIN_TESTLAND, MAPBIN_TESTLAND_LEN);
    world.unit_count = 1;
    world.feature[5][8] = FE_DOOR_OPEN; /* open the house door for lines */
    world_map_changed(&world);
    memset(&book, 0, sizeof book);
    book.level[SP_MAGIC_BOLT] = 8;      /* enough casts for the tests */
    world.units[1].kind = CR_GOBLIN;    /* target on the path */
    world.units[1].owner = OWN_NEUTRAL;
    world.units[1].x = 9;
    world.units[1].y = 5;
    world.units[1].con = world.units[1].con_max = 32;
    world.unit_count = 2;

    check(!spell_bolt(&world, &book, 0, SP_MAGIC_BOLT, 20, 20, false, &rng, &shot),
          "bolt: out of range is rejected");
    check(!spell_bolt(&world, &book, 0, SP_MAGIC_BOLT, 6, 1, false, &rng, &shot),
          "bolt: no line of sight through the wall");

    {   /* seeded volleys at the goblin; failures counted, printed once */
        bool all_cast = true;
        for (k = 0; k < 50; k++) {
            world.units[0].ap = 40;
            world.units[0].mana = 80;
            world.units[1].con = 32;
            book.level[SP_MAGIC_BOLT] = 8;
            rng_seed(&rng, 7000 + k);
            if (!spell_bolt(&world, &book, 0, SP_MAGIC_BOLT, 9, 5, false, &rng, &shot))
                all_cast = false;
            if (shot.hit)
                hits++;
            if (world.unit_count == 1)  /* goblin died: respawn */
                world_spawn_unit(&world, OWN_NEUTRAL, CR_GOBLIN, 9, 5);
        }
        check(all_cast, "bolt: 50 casts all go through");
    }
    check(hits > 38, "bolt: a level-8 bolt (A 57) hits a goblin in about 92 %");
    check(book.level[SP_MAGIC_BOLT] == 7, "bolt: every cast burns one level");

    {   /* lightning: splash + terrain + wall rejection */
        memset(&book, 0, sizeof book);
        book.level[SP_MAGIC_LIGHTNING] = 1;
        world.unit_count = 1;
        world.units[0].ap = 40;
        world.units[0].mana = 80;
        world_spawn_unit(&world, OWN_NEUTRAL, CR_GOBLIN, 9, 5);
        check(!spell_lightning(&world, &book, 0, 3, 2, false, &rng, &shot),
              "bolt: lightning rejects massive targets");
        {   /* RND(2A) >= toughness: a rock (200) never breaks, a table (60) sometimes */
            uint8_t n;
            bool rock_broke = false, table_broke = false;
            for (n = 0; n < 30; n++) {
                book.level[SP_MAGIC_LIGHTNING] = 8;      /* A = 62 */
                world.units[0].ap = 40;
                world.units[0].mana = 80;
                world.feature[5][9] = FE_ROCK;
                world_map_changed(&world);
                rng_seed(&rng, 77 + n);
                spell_lightning(&world, &book, 0, 9, 5, false, &rng, &shot);
                if (world.feature[5][9] == FE_NONE)
                    rock_broke = true;
                book.level[SP_MAGIC_LIGHTNING] = 8;
                world.units[0].ap = 40;
                world.units[0].mana = 80;
                world.feature[5][9] = FE_TABLE;
                world_map_changed(&world);
                rng_seed(&rng, 177 + n);
                spell_lightning(&world, &book, 0, 9, 5, false, &rng, &shot);
                if (shot.terrain_smashed && world.feature[5][9] == FE_NONE)
                    table_broke = true;
            }
            check(!rock_broke, "bolt: lightning cannot break a rock (toughness 200)");
            check(table_broke, "bolt: lightning smashes a table now and then");
        }
    }

    {   /* F8: casting from the air, and the target height (CAST-A/G) */
        uint8_t wz, gob, bat, n;
        bool bat_hit = false, gob_touched = false;
        world_load_bin(&world, MAPBIN_TESTLAND, MAPBIN_TESTLAND_LEN);
        world.unit_count = 0;
        wz = world_spawn_unit(&world, OWN_P1, CR_WIZARD, 5, 13);
        gob = world_spawn_unit(&world, OWN_NEUTRAL, CR_GOBLIN, 8, 13);
        bat = world_spawn_unit(&world, OWN_NEUTRAL, CR_GIANT_BAT, 8, 13);
        world.units[bat].flags |= UF_FLYING;
        memset(&book, 0, sizeof book);
        book.level[SP_GOBLIN] = 1;
        book.level[SP_TELEPORT] = 5;
        book.level[SP_CURSE] = 5;
        book.level[SP_MAGIC_FIRE] = 5;
        world.units[wz].mana = 250;
        world.units[wz].ap = 40;
        world.units[wz].flags |= UF_FLYING;

        check(!spell_can_cast(&world, &book, wz, SP_GOBLIN),
              "F8: no summoning from the air");
        world.units[wz].flags &= (uint8_t)~UF_FLYING;
        check(spell_can_cast(&world, &book, wz, SP_GOBLIN),
              "F8: summoning on the ground");
        world.units[wz].flags |= UF_FLYING;

        /* a bolt from the air at the air hits only the bat, never the goblin */
        for (n = 0; n < 20 && !bat_hit; n++) {
            book.level[SP_MAGIC_BOLT] = 8;
            world.units[wz].ap = 40;
            world.units[wz].mana = 250;
            world.units[gob].con = world.units[gob].con_max;
            world.units[bat].con = world.units[bat].con_max;
            rng_seed(&rng, 300 + n);
            check(spell_bolt(&world, &book, wz, SP_MAGIC_BOLT, 8, 13, true, &rng, &shot),
                  "F8: a flying wizard casts a bolt");
            if (world.unit_count < 3)
                break;                      /* the bat fell: it was hit */
            if (shot.hit)
                bat_hit = true;
            if (world.units[gob].con != world.units[gob].con_max)
                gob_touched = true;
        }
        check((bat_hit || world.unit_count < 3) && !gob_touched,
              "F8: CAST-A hits the flyer, not the unit below");

        /* aimed at the ground: the goblin is the target, the bat is spared */
        world.unit_count = 0;
        wz = world_spawn_unit(&world, OWN_P1, CR_WIZARD, 5, 13);
        gob = world_spawn_unit(&world, OWN_NEUTRAL, CR_GOBLIN, 8, 13);
        world.units[wz].mana = 250;
        world.units[wz].ap = 40;
        world.units[wz].flags |= UF_FLYING;
        check(spell_apply(&world, &book, wz, SP_CURSE, 8, 13, true, &rng, &shot)
                  == CAST_REJECTED,
              "F8: Curse into the empty air finds no target");
        check(spell_apply(&world, &book, wz, SP_MAGIC_FIRE, 8, 13, true, &rng, &shot)
                  == CAST_REJECTED,
              "F8: area spells take the ground only");
        world.units[wz].ap = 40;
        check(spell_apply(&world, &book, wz, SP_CURSE, 8, 13, false, &rng, &shot)
                  != CAST_REJECTED,
              "F8: Curse from the air at the ground goes through");

        /* teleport into the air needs the flying potion (K5.3) */
        world.unit_count = 0;
        wz = world_spawn_unit(&world, OWN_P1, CR_WIZARD, 5, 13);
        world.units[wz].mana = 250;
        world.units[wz].ap = 40;
        check(spell_apply(&world, &book, wz, SP_TELEPORT, 7, 13, true, &rng, &shot)
                  == CAST_REJECTED && world.units[wz].x == 5,
              "F8: no teleport into the air without the flying potion");
        effect_grant(&world.units[wz], EFF_FLYING, 1, 3);
        check(spell_apply(&world, &book, wz, SP_TELEPORT, 7, 13, true, &rng, &shot)
                  == CAST_OK && world.units[wz].x == 7 &&
              (world.units[wz].flags & UF_FLYING),
              "F8: on the potion the wizard teleports into the air");
        world.units[wz].ap = 40;
        check(spell_apply(&world, &book, wz, SP_TELEPORT, 5, 13, false, &rng, &shot)
                  == CAST_OK && world.units[wz].x == 5 &&
              !(world.units[wz].flags & UF_FLYING),
              "F8: a ground teleport lands him");
    }
}

void selftest_set_verbose(bool verbose)
{
    verbose_checks = verbose;
}

static void test_items(void)
{
    Rng rng;

    check(OBJECTS[OBJ_SWORD].weapon == WEAPON_SWORD &&
          OBJECTS[OBJ_GOLD].vp == 40 && OBJECTS[OBJ_SCROLL].category == OC_SCROLL,
          "items: table values from objects.csv");
    check(WEAPONS[WEAPON_SWORD].combat == 10 && WEAPONS[WEAPON_SHIELD].defence == 13 &&
          WEAPONS[WEAPON_BOW].ranged == 1 && WEAPONS[WEAPON_SWORD].defence == 4 &&
          WEAPONS[WEAPON_SWORD].thrown == 16 && WEAPONS[WEAPON_MAGIC_SLAYER].combat == 30,
          "items: weapon values");

    world_load_bin(&world, MAPBIN_TESTLAND, MAPBIN_TESTLAND_LEN);
    world.unit_count = 1;                    /* wizard at 6,6 */
    world.units[0].x = 6;
    world.units[0].y = 8;                    /* the sword lies at 6,8 */
    check(items_kind_at(&world, 6, 8) == OBJ_SWORD, "items: sword on the ground");

    check(items_pick_up(&world, 0) && world.units[0].item_count == 1 &&
          world.units[0].items[0] == OBJ_SWORD && world.units[0].ap == 32,
          "items: picking up costs 8 AP");
    check(items_kind_at(&world, 6, 8) == NO_ITEM, "items: gone from the ground");
    check(items_combat(&world, 0) == 10 && items_defence(&world, 0) == 16,
          "items: not wielded yet; the carried sword adds its 4 defence");

    check(items_cycle(&world, 0) && world.units[0].in_use == 0 &&
          world.units[0].ap == 28, "items: wielding costs 4 AP");
    check(items_combat(&world, 0) == 20,
          "k7: a wielded sword adds its 10 Combat");

    {   /* shield carried: defence always (GDD 6.1) */
        world.units[0].items[1] = OBJ_SHIELD;
        world.units[0].item_count = 2;
        check(items_defence(&world, 0) == 25, "items: carried shield +13 defence (D31)");
        world.units[0].items[2] = OBJ_SHIELD;
        world.units[0].item_count = 3;
        check(items_defence(&world, 0) == 25, "items: shields do not stack (D21)");
        world.units[0].item_count = 2;
    }

    check(items_drop(&world, 0) && world.units[0].item_count == 1 &&
          items_kind_at(&world, 6, 8) == OBJ_SWORD && world.units[0].ap == 28,
          "items: dropping costs 2 AP");

    {   /* throw the scroll eastwards across the open grass */
        world.units[0].x = 10;
        world.units[0].y = 8;
        world.units[0].in_use = 0;
        world.units[0].items[0] = OBJ_SCROLL;
        world.units[0].item_count = 1;
        world.units[0].ap = 40;
        rng_seed(&rng, 5);
        check(items_throw(&world, &rng, 0, 1, 0) && world.units[0].item_count == 0,
              "items: throw leaves the hand");
        check(items_kind_at(&world, 22, 8) == OBJ_SCROLL,
              "items: a light scroll flies 12 fields (range 25 units, K6.4)");
    }

    {   /* D59: an apple thrown at a friend lands in his pack, unhurt */
        uint8_t f, con, before = world.object_count;
        world.units[0].x = 10;
        world.units[0].y = 8;
        world.units[0].in_use = 0;
        world.units[0].items[0] = OBJ_APPLE;
        world.units[0].item_count = 1;
        world.units[0].ap = 40;
        f = world_spawn_unit(&world, world.units[0].owner, CR_GOBLIN, 12, 8);
        check(f != NO_UNIT, "items: a friendly goblin to catch");
        con = world.units[f].con;
        rng_seed(&rng, 5);
        check(items_throw(&world, &rng, 0, 1, 0) &&
              world.units[f].item_count == 1 &&
              world.units[f].items[0] == OBJ_APPLE &&
              world.units[f].con == con && world.object_count == before,
              "items: a friend catches the thrown apple (D59)");
        world.units[0].items[0] = OBJ_VIAL_BOMB;
        world.units[0].item_count = 1;
        world.units[0].in_use = 0;
        f = world_find_unit(&world, world.units[f].id);
        check(brew_throw_vial(&world, &rng, 0, 1, 0) &&
              world.units[f].item_count == 2 &&
              world.units[f].items[1] == OBJ_VIAL_BOMB &&
              world.units[f].con == con,
              "items: a friend catches a bomb vial whole (D59)");
        world_remove_unit(&world, f);
    }

    {   /* bow: pick up, wield, fire at the goblin (9,6) from outside */
        uint8_t dmg = 1;
        world.units[0].x = 8;
        world.units[0].y = 5;
        world.units[0].ap = 40;
        world.units[0].items[0] = OBJ_BOW;
        world.units[0].item_count = 1;
        world.units[0].in_use = 0;
        world_spawn_unit(&world, OWN_NEUTRAL, CR_GOBLIN, 11, 5);
        rng_seed(&rng, 11);
        check(items_fire(&world, &rng, 0, 11, 5, &dmg),
              "items: bow fires in range");
        check(world.units[0].ap == 32, "items: firing costs 8 AP");
        check(!items_fire(&world, &rng, 0, 20, 5, &dmg),
              "items: out of range rejected");
    }
}

static void test_game(void)
{
    Game g;
    Rng rng;

    rng_seed(&rng, 42);
    game_init(&g, 26, 3, 12, 15, &rng);
    check(g.portal_round >= 12 && g.portal_round <= 15 && !g.portal_open,
          "game: portal round within the span");
    game_new_round(&g, 11);
    check(!g.portal_open, "game: still closed before its round");
    game_new_round(&g, g.portal_round);
    check(g.portal_open, "game: opens at its round");

    world_load_bin(&world, MAPBIN_TESTLAND, MAPBIN_TESTLAND_LEN);
    world.unit_count = 1;
    world.units[0].x = 26;
    world.units[0].y = 3;
    world.units[0].items[0] = OBJ_GOLD;         /* 40 VP */
    world.units[0].items[1] = OBJ_SWORD;        /* worth nothing */
    world.units[0].item_count = 2;
    check(game_try_enter_portal(&g, &world, 0) && world.unit_count == 0,
          "game: the wizard escapes and leaves the world");
    check(g.vp[OWN_P1] == VP_ESCAPE + 40 && (g.escaped & 1) != 0,
          "game: escape bonus plus carried treasures");
    check(items_kind_at(&world, 26, 3) == NO_ITEM,
          "game: the escaped take their objects along");

    {   /* creatures cannot pass */
        world_load_bin(&world, MAPBIN_TESTLAND, MAPBIN_TESTLAND_LEN);
        world.unit_count = 1;
        world.units[0].kind = CR_GOBLIN;
        world.units[0].x = 26;
        world.units[0].y = 3;
        check(!game_try_enter_portal(&g, &world, 0) && world.unit_count == 1,
              "game: creatures cannot pass");
        check(game_over(&g, &world), "game: over without wizards");
    }

    {   /* the open portal is drawn as an animated layer */
        FieldLayers f;
        view_set_sight(NULL);
        view_set_portal(26, 3);
        view_compose(&world, 26, 3, &f);
        check(has_layer(&f, T_PORTAL_0), "game: portal drawn on its field");
        view_set_portal(-1, -1);
        view_set_sight(NULL);
    }

    {   /* kill credit (K6.5): a wizard gets twice the table value, a summoned
         * creature the value itself; a dead wizard is worth 4 Level + 15 */
        Kill k = {CR_GOBLIN, OWN_NEUTRAL, CR_WIZARD, OWN_P1, true};
        game_init(&g, -1, -1, 1, 1, &rng);
        game_kill_credit(&g, &k);                  /* wizard melee: 2 */
        k.melee = false;
        game_kill_credit(&g, &k);                  /* wizard ranged: 2 as well */
        k.killer_kind = CR_GIANT_BAT;
        k.killer_owner = OWN_P2;
        k.melee = true;
        game_kill_credit(&g, &k);                  /* summoned creature: 1 */
        check(g.vp[OWN_P1] == 4 && g.vp[OWN_P2] == 1,
              "game: kills score 2 x table value for wizards, the value for creatures");
        k.victim_kind = CR_WIZARD;
        k.victim_owner = OWN_P1;
        game_kill_credit(&g, &k);                  /* a creature kills a wizard: 19 / 2 */
        check(g.vp[OWN_P2] == 1 + 9,
              "game: a creature killing a wizard gets half of 4 Level + 15");
        k.killer_kind = CR_WIZARD;
        game_set_wizard_level(&g, OWN_P1, 3);
        game_kill_credit(&g, &k);                  /* a wizard kills a level-3 wizard */
        check(g.vp[OWN_P2] == 10 + 27,
              "game: a wizard killing a level-3 wizard gets 4 x 3 + 15");
        k.victim_owner = OWN_P2;
        game_kill_credit(&g, &k);
        check(g.vp[OWN_P2] == 10 + 27,
              "game: friendly fire scores nothing");
        g.vp[OWN_P2] = 250;
        k.victim_owner = OWN_P1;
        game_kill_credit(&g, &k);
        check(g.vp[OWN_P2] == 255, "game: the score stops at 255");
    }

    {   /* the portal closes after its span and that ends the game (K4) */
        Game c;
        game_init(&c, 26, 3, 5, 5, &rng);
        game_set_portal_span(&c, 3);
        game_new_round(&c, 5);
        check(c.portal_open && !c.portal_closed, "game: open in its round");
        game_new_round(&c, 7);
        check(!c.portal_closed, "game: still open in the last round");
        game_new_round(&c, 8);
        check(c.portal_closed, "game: closed after the span");
        world_load_bin(&world, MAPBIN_TESTLAND, MAPBIN_TESTLAND_LEN);
        world.units[0].x = 26;
        world.units[0].y = 3;
        check(!game_try_enter_portal(&c, &world, 0), "game: nobody enters a closed portal");
        check(game_over(&c, &world) && game_outcome(&c, &world, OWN_P1) == OUT_LOSE,
              "game: a wizard still on the map loses when it shuts");
    }
}

static void test_ai(void)
{
    Turns t;
    Game g;
    Spellbook books[OWN_NEUTRAL];
    AiCtx ctx = {0};
    Rng rng;

    world_load_bin(&world, MAPBIN_TESTLAND, MAPBIN_TESTLAND_LEN);
    world.unit_count = 2;                       /* wizard + one goblin */
    world.units[1].kind = CR_GOBLIN;
    world.units[1].owner = OWN_NEUTRAL;
    world.units[1].x = 9;                       /* in sight through the door */
    world.units[1].y = 5;
    world.units[1].com = 9;
    world.units[1].def = 9;
    world.units[1].con = world.units[1].con_max = 32;
    world.units[1].ap = 30;
    world.units[1].sta = 45;
    world.feature[5][8] = FE_DOOR_OPEN;         /* clear line of sight */
    world_map_changed(&world);

    check(ai_nearest_enemy(&world, 1, 9) == 0, "ai: goblin scents the wizard");
    world.feature[5][8] = FE_DOOR_CLOSED;
    world_map_changed(&world);
    world.units[1].x = 12;                      /* behind the east wall */
    world.units[1].y = 6;
    check(ai_nearest_enemy(&world, 1, 9) == NO_UNIT,
          "ai: no prey through walls (hidden movement)");
    world.units[1].x = 9;
    world.units[1].y = 5;
    world.feature[5][8] = FE_DOOR_OPEN;
    world_map_changed(&world);

    {   /* hunter with distance closes in */
        uint8_t before = 255, after;
        world.units[1].ap = 30;
        rng_seed(&rng, 3);
        before = (uint8_t)((world.units[1].x > world.units[0].x)
                               ? world.units[1].x - world.units[0].x : 1);
        ai_hunter(&world, &rng, 1);
        after = (uint8_t)((world.units[1].x > world.units[0].x)
                              ? world.units[1].x - world.units[0].x : 0);
        check(after < before || world.units[1].ap < 30,
              "ai: hunter closes in or fights");
    }

    {   /* wizard AI: summons, then walks to the portal over rounds */
        uint8_t seen_summons = 0, k;
        uint8_t p2 = world_spawn_unit(&world, OWN_P2, CR_WIZARD, 2, 13);
        memset(books, 0, sizeof books);
        books[OWN_P2].level[SP_GOBLIN] = 2;
        (void)p2;
        game_init(&g, 26, 3, 1, 1, &rng);       /* portal open from round 1 */
        game_new_round(&g, 1);
        ctx.books = books;
        ctx.game = &g;
        t = (Turns){0};
        t.phase = OWN_P2;
        rng_seed(&t.rng, 99);
        for (k = 0; k < 40 && !(g.escaped & (1u << OWN_P2)); k++) {
            uint8_t count = world.unit_count;
            ai_wizard_phase(&t, &world, &ctx);
            if (world.unit_count > count)
                seen_summons = 1;
            world_new_turn(&world);             /* next round, AP refills */
            t.round++;
        }
        check(seen_summons, "ai: the wizard summons company");
        check(g.escaped & (1u << OWN_P2),
              "ai: the wizard escapes through the portal");
        check(g.vp[OWN_P2] >= VP_ESCAPE, "ai: escape scores");
    }
}

static uint8_t rounds_seen;

static void count_round(Turns *t, World *w, void *ctx)
{
    (void)w;
    rounds_seen++;
    game_new_round((Game *)ctx, t->round);
}

/* Regressions from the M3 review: unit removal reorders the list, kills
 * score the victim, the turn loop ends without humans, > 24 units. */
static void test_review_fixes(void)
{
    Turns t;
    Rng rng;
    Game g;
    Spellbook books[OWN_NEUTRAL];
    AiCtx ctx = {0};
    uint8_t a, b, i, a_id, b_id;

    rng_seed(&rng, 7);
    world_load_bin(&world, MAPBIN_TESTLAND, MAPBIN_TESTLAND_LEN);
    world.unit_count = 0;
    world_spawn_unit(&world, OWN_P1, CR_WIZARD, 5, 5);
    world_spawn_unit(&world, OWN_NEUTRAL, CR_GOBLIN, 10, 10);
    a = world_spawn_unit(&world, OWN_P1, CR_DWARF, 12, 12);
    b = world_spawn_unit(&world, OWN_P1, CR_GIANT_BAT, 14, 14);
    a_id = world.units[a].id;
    b_id = world.units[b].id;
    check(a_id != b_id && world_find_unit(&world, a_id) == a,
          "fix: units get distinct ids");
    turn_init(&t, &world, 1, 1u << OWN_P1);
    turn_next_unit(&t, &world, false);
    check(t.active == a, "fix: the dwarf is active");
    world.units[b].done = true;
    world_kill_unit(&world, 1, CR_DWARF, OWN_P1, true);   /* goblin dies */
    turn_revalidate(&t, &world);
    check(world.units[t.active].id == a_id && !world.units[t.active].done,
          "fix: active unit survives a death elsewhere");
    check(world.units[world_find_unit(&world, b_id)].done,
          "fix: done flags move with their units");
    check(world.kill_count == 1 && world.kills[0].victim_kind == CR_GOBLIN,
          "fix: the kill is logged");
    world_kill_unit(&world, t.active, CR_GOBLIN, OWN_NEUTRAL, true);
    check(world.kill_count == 1, "fix: independents' kills are not logged");
    turn_revalidate(&t, &world);
    check(t.active < world.unit_count && world.units[t.active].owner == OWN_P1,
          "fix: a dead active unit hands over to another");
    game_init(&g, -1, -1, 1, 1, &rng);
    game_credit_kills(&g, &world);
    check(g.vp[OWN_P1] == CREATURES[CR_GOBLIN].vp && world.kill_count == 0,
          "fix: logged kills are credited once");

    {   /* a refused melee leaves a clean result (the AI reads it) */
        CombatResult r;
        memset(&r, 0xAA, sizeof r);
        check(!combat_melee(&world, &rng, 0, 0, &r) && !r.died && !r.attacker_died,
              "fix: refused melee zeroes the result");
    }

    {   /* D21: the dead drop what they carried, also when bleeding out */
        uint8_t v, objs = world.object_count;
        v = world_spawn_unit(&world, OWN_P2, CR_GOBLIN, 20, 21);
        world.units[v].items[0] = OBJ_GOLD;
        world.units[v].items[1] = OBJ_SWORD;
        world.units[v].item_count = 2;
        world_kill_unit(&world, v, CR_WIZARD, OWN_P1, true);
        check(world.object_count == objs + 2 && items_kind_at(&world, 20, 21) != NO_ITEM,
              "fix: the dead drop their objects");
        game_credit_kills(&g, &world);
        v = world_spawn_unit(&world, OWN_P2, CR_GOBLIN, 21, 21);
        world.units[v].items[0] = OBJ_GOLD;
        world.units[v].item_count = 1;
        world.units[v].con = 1;
        world_set_wounds(&world.units[v], 1);
        v = world.units[v].id;
        world_new_turn(&world);
        check(world_find_unit(&world, v) == NO_UNIT && world.object_count == objs + 3,
              "fix: bled-out units drop too");
        check(items_kind_at(&world, 21, 21) == OBJ_GOLD,
              "fix: the treasure lies where he fell");
    }

    {   /* D21: ranged attacks share the 10..90 % clamp of D16 */
        uint8_t s, tg, hits = 0, n;
        world_load_bin(&world, MAPBIN_MANY_COLOURED_LAND, MAPBIN_MANY_COLOURED_LAND_LEN);
        world.unit_count = 0;
        s = world_spawn_unit(&world, OWN_P1, CR_DWARF, 2, 1);
        tg = world_spawn_unit(&world, OWN_P2, CR_GOBLIN, 5, 1);
        world.units[s].items[0] = OBJ_BOW;
        world.units[s].item_count = 1;
        world.units[s].in_use = 0;
        world.units[tg].def = 10;                /* A 15: 21 of 32 rolls hurt */
        world.units[tg].con = world.units[tg].con_max = 255;
        for (n = 0; n < 100; n++) {
            uint8_t dmg = 0;
            world.units[s].ap = 40;
            world.units[tg].con = 255;
            if (items_fire(&world, &rng, s, 5, 1, &dmg) && dmg)
                hits++;
        }
        check(hits > 45 && hits < 85, "fix: the bow rolls 15 against Defence (K6.4)");
    }

    /* more than 24 units: the eZ80 int is 24 bit (no bit masks) */
    world.unit_count = 0;
    for (i = 0; i < 30; i++)
        world_spawn_unit(&world, OWN_P1, CR_GOBLIN, i, 20);
    turn_init(&t, &world, 1, 1u << OWN_P1);
    for (i = 0; i < 29; i++)
        turn_finish_unit(&t, &world);
    check(t.active == 29 && turn_units_left(&t, &world),
          "fix: finish flags beyond unit 24");
    turn_finish_unit(&t, &world);
    check(!turn_units_left(&t, &world), "fix: all 30 units finished");

    /* no human left: the AI plays on, the round hook opens the portal,
     * the escaped wizard ends the loop */
    load_house();
    {
        uint8_t x = world.units[0].x, y = world.units[0].y;
        world.unit_count = 0;
        world_spawn_unit(&world, OWN_P2, CR_WIZARD, x, y);
        for (i = 0; i < OWN_NEUTRAL; i++)
            memset(&books[i], 0, sizeof books[i]);
        ctx.books = books;
        ctx.game = &g;
        game_init(&g, x, y, 3, 3, &rng);          /* opens under him */
        turn_init(&t, &world, 1, 1u << OWN_P1);
        t.ai = ai_wizard_phase;
        t.ai_ctx = &ctx;
        t.on_round = count_round;
        t.round_ctx = &g;
        rounds_seen = 0;
        turn_end_phase(&t, &world);
        check(g.portal_open && rounds_seen == 2 && t.round == 3,
              "fix: round hook opens the portal in AI rounds");
        check((g.escaped & (1u << OWN_P2)) != 0,
              "fix: the AI escapes, the turn loop returns");

        world.unit_count = 0;                     /* no portal: bounded */
        world_spawn_unit(&world, OWN_P2, CR_WIZARD, x, y);
        game_init(&g, -1, -1, 1, 1, &rng);
        turn_init(&t, &world, 1, 1u << OWN_P1);
        t.ai = ai_wizard_phase;
        t.ai_ctx = &ctx;
        t.on_round = count_round;
        t.round_ctx = &g;
        rounds_seen = 0;
        turn_end_phase(&t, &world);
        check(rounds_seen == TURN_AUTOPLAY_ROUNDS &&
              t.round == 1 + TURN_AUTOPLAY_ROUNDS,
              "fix: AI autoplay stops after its bound");
    }
}

static void test_scenario(void)
{
    world_load_bin(&world, MAPBIN_MANY_COLOURED_LAND, MAPBIN_MANY_COLOURED_LAND_LEN);
    check(world.w == 46 && world.h == 46 && world.wrap,
          "scn: 46x46, wraps (D64)");
    check(world.portal_x == 33 && world.portal_y == 3 &&
          world.portal_rmin == 12 && world.portal_rmax == 15,
          "scn: portal from the map (v3)");
    {
        uint8_t i, wizards = 0, treasures = 0;
        int16_t vp_fields = 0;
        for (i = 0; i < world.unit_count; i++)
            if (world.units[i].kind == CR_WIZARD)
                wizards++;
        for (i = 0; i < world.object_count; i++) {
            uint8_t k, kind = NO_ITEM;
            for (k = 0; k < OBJ_COUNT; k++)
                if (OBJECTS[k].tile == world.objects[i].tile)
                    kind = k;
            if (kind != NO_ITEM && OBJECTS[kind].category == OC_TREASURE) {
                treasures++;
                vp_fields = (int16_t)(vp_fields + OBJECTS[kind].vp);
            }
        }
        check(wizards == 2 && world.unit_count == 2,
              "scn: two wizards (human + AI), the AI starts alone (D35)");
        check(treasures == 0 && vp_fields == 0,
              "d35: no fixed treasure - chests come at random");
    }
    check(world_load_bin(&world, MAPBIN_TESTLAND, MAPBIN_TESTLAND_LEN) &&
          world.portal_x == -1,
          "scn: v2 testland stays portal-free");
}

static void test_m4a(void)
{
    Rng rng;

    check(OBJECTS[OBJ_APPLE].eat_con == 10 && OBJECTS[OBJ_MAGIC_MUSHROOM].eat_mana == 6 &&
          OBJECTS[OBJ_CHEST_KEY].category == OC_KEY,
          "m4a: food and key values from objects.csv");

    {   /* undead: only undead, magic weapons and spells wound (GDD 4.2) */
        CombatResult r;
        world_load_bin(&world, MAPBIN_TESTLAND, MAPBIN_TESTLAND_LEN);
        world.unit_count = 2;
        world.units[1].kind = CR_ZOMBIE;
        world.units[1].owner = OWN_NEUTRAL;
        world.units[1].flags |= UF_UNDEAD;
        world.units[1].x = 7;
        world.units[1].y = 6;
        world.units[1].con = world.units[1].con_max = 250;
        world.units[1].ap = 0;              /* no return blows in this test */
        rng_seed(&rng, 1);
        combat_melee(&world, &rng, 0, 1, &r);
        check(!r.hit && world.units[1].con == 250 && world.units[0].ap == 32,
              "m4a: bare hands clank off the zombie");
        world.units[0].ap = 40;
        world.units[0].items[0] = OBJ_SWORD;
        world.units[0].item_count = 1;
        world.units[0].in_use = 0;
        rng_seed(&rng, 1);
        combat_melee(&world, &rng, 0, 1, &r);
        check(!r.hit && world.units[1].con == 250,
              "m4a: normal weapons cannot wound undead");
        world.units[0].items[0] = OBJ_MAGIC_SLAYER;
        world.units[0].item_count = 1;
        world.units[0].in_use = 0;
        {
            uint8_t k, hit = 0;
            for (k = 0; k < 30; k++) {
                world.units[0].ap = 40;
                world.units[0].sta = 60;
                world.units[0].con = 30;   /* free counters wear him down */
                world.units[1].con = 250;
                rng_seed(&rng, 200 + k);
                combat_melee(&world, &rng, 0, 1, &r);
                hit = hit || r.hit;
            }
            check(hit, "m4a: the magic slayer wounds the zombie");
        }
        world.units[0].items[0] = OBJ_SWORD;
        world.units[0].flags |= UF_MAGIC_WEAPON;   /* enchanted (M4b) */
        {
            uint8_t k, hit = 0;
            for (k = 0; k < 30; k++) {
                world.units[0].ap = 40;
                world.units[0].sta = 60;
                world.units[0].con = 30;
                world.units[1].con = 250;
                rng_seed(&rng, 300 + k);
                combat_melee(&world, &rng, 0, 1, &r);
                hit = hit || r.hit;
            }
            check(hit, "m4a: enchanted weapons wound undead");
        }
    }

    {   /* below half Constitution: -2 combat and defence (GDD 4.1) */
        world_load_bin(&world, MAPBIN_TESTLAND, MAPBIN_TESTLAND_LEN);
        world.unit_count = 1;
        world.units[0].con = world.units[0].con_max = 30;
        check(items_combat(&world, 0) == 10 && items_defence(&world, 0) == 12,
              "m4a: full strength values");
        world.units[0].con = 14;              /* under 50 % */
        check(items_combat(&world, 0) == 5 && items_defence(&world, 0) == 6,
              "m4a: below half Constitution both halve (K6.1)");
        world.units[0].sta = 60;
        world_new_turn(&world);
        check(world.units[0].ap == 20,
              "m4a: badly hurt units refill half AP");
    }

    {   /* eat and read */
        world_load_bin(&world, MAPBIN_TESTLAND, MAPBIN_TESTLAND_LEN);
        world.unit_count = 1;
        world.units[0].con = 10;
        world.units[0].ap = 40;
        world.units[0].items[0] = OBJ_APPLE;
        world.units[0].item_count = 1;
        world.units[0].in_use = 0;
        world.units[0].sta = 5;
        check(items_eat(&world, 0) && world.units[0].con == 20 &&
              world.units[0].sta == 45 &&   /* 5 + 40, eating costs no stamina */
              world.units[0].item_count == 0 && world.units[0].ap == 36,
              "m4a: eating an apple heals 10 Con and gives 40 stamina");
        world.units[0].items[0] = OBJ_MAGIC_MUSHROOM;
        world.units[0].item_count = 1;
        world.units[0].in_use = 0;
        world.units[0].mana = 60;
        check(items_eat(&world, 0) && world.units[0].mana == 66,
              "m4a: the magic mushroom gives 6 mana");
        world.units[0].items[0] = OBJ_SCROLL;
        world.units[0].item_count = 1;
        world.units[0].in_use = 0;
        check(items_read(&world, 0) != NULL && world.units[0].item_count == 0,
              "m4a: reading consumes the scroll");
        check(!items_eat(&world, 0), "m4a: nothing edible left");
    }

    {   /* chests: key unlocks cheap, prying costs triple, loot drops */
        bool destroyed = false;
        uint8_t objects_before;
        world_load_bin(&world, MAPBIN_TESTLAND, MAPBIN_TESTLAND_LEN);
        world.unit_count = 1;
        world.units[0].x = 4;                /* next to the chest at 3,8 */
        world.units[0].y = 8;
        world.units[0].ap = 40;
        world.feature[8][3] = FE_CHEST;
        world_map_changed(&world);
        objects_before = world.object_count;
        rng_seed(&rng, 9);
        check(items_open_chest(&world, &rng, 0, 3, 8) &&
              world.feature[8][3] == FE_NONE &&
              world.object_count == objects_before + 1 &&
              world.units[0].ap == 40 - ACTIONS[ACT_OPEN_CHEST].ap * 3,
              "m4a: prying open costs triple AP and drops loot");
        world.feature[8][3] = FE_CHEST;
        world_map_changed(&world);
        world.units[0].items[0] = OBJ_CHEST_KEY;
        world.units[0].item_count = 1;
        world.units[0].in_use = 0;
        rng_seed(&rng, 9);
        check(items_open_chest(&world, &rng, 0, 3, 8) &&
              world.units[0].item_count == 0 &&
              world.units[0].ap == 40 - ACTIONS[ACT_OPEN_CHEST].ap * 3 -
                                        ACTIONS[ACT_UNLOCK].ap,
              "m4a: the key unlocks for 8 AP and vanishes");
        (void)destroyed;
    }
}

static void test_m4b(void)
{
    Spellbook book;
    SpellShot shot;
    Rng rng;
    uint8_t k;

    world_load_bin(&world, MAPBIN_TESTLAND, MAPBIN_TESTLAND_LEN);
    world.unit_count = 2;
    world.units[1].kind = CR_GOBLIN;
    world.units[1].owner = OWN_P2;
    world.units[1].x = 9;
    world.units[1].y = 5;
    world.units[1].mr = 46;
    world.units[1].con = world.units[1].con_max = 32;
    world.feature[5][8] = FE_DOOR_OPEN;         /* targets need sight (D17) */
    world_map_changed(&world);
    memset(&book, 0, sizeof book);

    {   /* effects: grant, tick, expire */
        Unit *u = &world.units[0];
        check(effect_grant(u, EFF_SHIELD, 4, 2) && effect_active(u, EFF_SHIELD) &&
              effect_power(u, EFF_SHIELD) == 4, "m4b: effect granted");
        check(items_defence(&world, 0) == 12 + 4, "m4b: shield spell adds defence");
        world_new_turn(&world);
        check(effect_active(u, EFF_SHIELD), "m4b: one round off, still there");
        world_new_turn(&world);
        check(!effect_active(u, EFF_SHIELD) && items_defence(&world, 0) == 12,
              "m4b: effect expires and the flag goes");
    }

    {   /* magic shield spell */
        Unit *u = &world.units[0];
        memset(u->effects, 0, sizeof u->effects);
        book.level[SP_MAGIC_SHIELD] = 3;
        world.units[0].ap = 40;
        world.units[0].mana = 80;
        rng_seed(&rng, 1);
        check(spell_apply(&world, &book, 0, SP_MAGIC_SHIELD, u->x, u->y, false, &rng,
                          &shot) == CAST_OK &&
              effect_active(u, EFF_SHIELD) && effect_power(u, EFF_SHIELD) == 28 &&
              u->effects[0].rounds == 4,
              "m4b: magic shield at level 3: +4 (L+1) + 12 = 28 for 4 rounds");
        check(book.level[SP_MAGIC_SHIELD] == 2, "m4b: one level down");
    }

    {   /* teleport: jump with scatter, 0 AP, fails on busy */
        Unit *u = &world.units[0];
        book.level[SP_TELEPORT] = 1;
        u->ap = 40;
        u->mana = 80;
        rng_seed(&rng, 7);
        check(spell_apply(&world, &book, 0, SP_TELEPORT, 12, 6, false, &rng, &shot) ==
              CAST_OK, "m4b: teleport goes through");
        check(u->ap == 0 && u->x >= 8 && u->x <= 16, "m4b: scattered, 0 AP");
        u->x = 6;
        u->y = 6;
    }

    {   /* curse: wound on failed resistance */
        memset(&world.units[1].flags, 0, 1);
        book.level[SP_CURSE] = 4;
        world.units[0].ap = 40;
        world.units[0].mana = 80;
        {
            bool wounded = false, resisted = false;
            for (k = 0; k < 30; k++) {
                CastResult cr;
                world.units[0].ap = 40;
                world.units[0].mana = 80;
                book.level[SP_CURSE] = 4;
                world_set_wounds(&world.units[1], 0);
                rng_seed(&rng, 400 + k);
                cr = spell_apply(&world, &book, 0, SP_CURSE, 9, 5, false, &rng, &shot);
                if (cr == CAST_OK && (world.units[1].flags & UF_WOUNDED))
                    wounded = true;
                if (cr == CAST_NO_RES)
                    resisted = true;
            }
            check(wounded && resisted, "m4b: curse wounds or meets resistance");
        }
    }

    {   /* subversion: the goblin changes sides */
        book.level[SP_SUBVERSION] = 8;
        {
            bool switched = false;
            for (k = 0; k < 40; k++) {
                CastResult cr;
                world.units[0].ap = 40;
                world.units[0].mana = 200;
                book.level[SP_SUBVERSION] = 8;
                world.units[1].owner = OWN_P2;
                rng_seed(&rng, 600 + k);
                cr = spell_apply(&world, &book, 0, SP_SUBVERSION, 9, 5, false, &rng, &shot);
                if (cr == CAST_OK && world.units[1].owner == OWN_P1)
                    switched = true;
            }
            check(switched, "m4b: subversion wins the goblin over");
            world.units[1].owner = OWN_P2;
        }
    }

    {   /* magic attack: hits the whole kind around the target */
        uint8_t hits = 0, k2;
        world_spawn_unit(&world, OWN_NEUTRAL, CR_GOBLIN, 10, 5);   /* same kind */
        for (k2 = 0; k2 < 30; k2++) {
            hits = hits;                 /* keep count below */
            world.units[0].ap = 40;
            world.units[0].mana = 200;
            book.level[SP_MAGIC_ATTACK] = 8;
            while (world.unit_count < 4) /* two goblins: 1 and 3 */
                world_spawn_unit(&world, OWN_P2, CR_GOBLIN,
                                 9, 5);
            rng_seed(&rng, 700 + k2);
            if (spell_apply(&world, &book, 0, SP_MAGIC_ATTACK, 9, 5, false, &rng,
                            &shot) == CAST_OK)
                hits = (uint8_t)(hits + shot.splash_hits);
        }
        check(hits > 0, "m4b: magic attack strikes the kind in the area");
        check(world.units[2].kind == CR_DWARF || world.unit_count <= 4,
              "m4b: other kinds stay untouched");
    }

    {   /* enchant: weapons of every unit on the field become magic */
        Unit *g = &world.units[1];
        g->items[0] = OBJ_SWORD;
        g->item_count = 1;
        g->in_use = 0;
        book.level[SP_ENCHANT] = 2;
        world.units[0].ap = 40;
        world.units[0].mana = 80;
        rng_seed(&rng, 9);
        check(spell_apply(&world, &book, 0, SP_ENCHANT, g->x, g->y, false, &rng, &shot) ==
              CAST_OK && (g->flags & UF_MAGIC_WEAPON) != 0,
              "m4b: enchant flags the carried weapons");
        check(effect_active(g, EFF_MAGIC_WEAPON), "m4b: enchant as effect");
        {
            uint8_t z = world_spawn_unit(&world, OWN_NEUTRAL, CR_ZOMBIE, 8, 5);
            check(items_can_harm_undead(&world, 1, z),
                  "m4b: the enchanted sword wounds undead");
        }
    }

    {   /* magic eye reveals through walls */
        Sight s;
        world.unit_count = 1;
        world.units[0].x = 6;
        world.units[0].y = 6;
        sight_init(&s, OWN_P1);
        sight_compute(&world, &s);
        check(!sight_visible(&s, &world, 6, 1), "m4b: wall blocks the view");
        sight_add_eye(&s, &world, 6, 2, 13);
        check(sight_visible(&s, &world, 6, 1) && sight_visible(&s, &world, 6, 6),
              "m4b: the eye sees through walls");
    }

    {   /* speed: double AP, triple recovery */
        Unit *u = &world.units[0];
        memset(u->effects, 0, sizeof u->effects);
        effect_grant(u, EFF_SPEED, 1, 2);
        u->sta = 15;                     /* not exhausted, recovery visible */
        world_new_turn(&world);
        check(u->ap == 80, "m4b: speed doubles AP");
        check(u->sta == 45, "m4b: speed recovers half the maximum (15+30)");
    }
}

static void test_m4c(void)
{
    Spellbook book;
    Rng rng;

    world_load_bin(&world, MAPBIN_TESTLAND, MAPBIN_TESTLAND_LEN);
    world.unit_count = 1;                    /* the wizard at 6,6 */
    memset(&book, 0, sizeof book);
    book.level[SP_HEALING_POTION] = 2;
    book.level[SP_STRENGTH_POTION] = 1;
    book.level[SP_GOLD_DRAGON] = 1;

    {   /* brewing needs cauldron + ingredient, yields level+3 doses */
        Cauldron *c;
        world.units[0].ap = 40;
        world.units[0].mana = 80;
        check(!brew_cast(&world, &book, 0, SP_HEALING_POTION),
              "m4c: no brewing without a cauldron");
        brew_set_cauldron(&world, 6, 6, false, 0xFF);
        check(!brew_cast(&world, &book, 0, SP_HEALING_POTION),
              "m4c: no brewing without the ingredient");
        {   /* apple lies on the field: healing ingredient (GDD 7.2) */
            world.objects[world.object_count].x = 6;
            world.objects[world.object_count].y = 6;
            world.objects[world.object_count].tile = T_OBJ_APPLE;
            world.object_count++;
        }
        check(brew_cast(&world, &book, 0, SP_HEALING_POTION) &&
              (c = brew_cauldron_at(&world, 6, 6)) != NULL &&
              c->doses == 5 && c->potion == SP_HEALING_POTION &&
              book.level[SP_HEALING_POTION] == 1 &&
              world.units[0].mana == 80 - 3,
              "m4c: brewing fills level+3 doses and burns one level");
    }

    {   /* drinking heals wounds and consumes doses */
        Unit *u = &world.units[0];
        u->con = 10;
        u->sta = 0;
        world_set_wounds(u, 1);
        u->ap = 40;
        check(brew_drink(&world, 0) && u->con == 30 && u->sta == 60 &&
              !(u->flags & UF_WOUNDED),
              "m4c: the healing draught cures everything");
        check(brew_cauldron_at(&world, 6, 6)->doses == 4,
              "m4c: one dose down");
    }

    {   /* fill a vial, drink it */
        Unit *u = &world.units[0];
        u->items[0] = OBJ_VIAL_EMPTY;
        u->item_count = 1;
        u->in_use = 0;
        u->ap = 40;
        check(brew_fill(&world, 0) && u->items[0] == OBJ_VIAL_HEALING &&
              brew_cauldron_at(&world, 6, 6)->doses == 3,
              "m4c: filling takes a dose from the cauldron");
        u->con = 5;
        u->ap = 40;
        check(brew_drink_vial(&world, 0) && u->con == 30 &&
              u->item_count == 0,
              "m4c: drinking the vial heals");
    }

    {   /* strength potion effect via brewing (M4b machinery) */
        Unit *u = &world.units[0];
        u->ap = 40;
        u->mana = 80;
        {
            world.objects[world.object_count].x = 6;
            world.objects[world.object_count].y = 6;
            world.objects[world.object_count].tile = T_OBJ_MISTLETOE;
            world.object_count++;
        }
        check(brew_cast(&world, &book, 0, SP_STRENGTH_POTION) &&
              effect_active(u, EFF_STRENGTH) == false,
              "m4c: brewed strength waits in the cauldron");
        u->ap = 40;
        check(brew_drink(&world, 0) && effect_active(u, EFF_STRENGTH),
              "m4c: drinking grants the strength effect");
        check(items_combat(&world, 0) == 10 + 20,
              "m4c: a strength potion adds 20 Combat (K8.2)");
    }

    {   /* bomb vial explodes in the area */
        Unit *u = &world.units[0];
        uint8_t g1, g2;
        rng_seed(&rng, 3);
        u->x = 12;                        /* open grass, wall to the west */
        u->y = 6;
        world_spawn_unit(&world, OWN_NEUTRAL, CR_GOBLIN, 9, 6);
        world_spawn_unit(&world, OWN_NEUTRAL, CR_GOBLIN, 10, 7);
        g1 = world.unit_count - 2;
        g2 = world.unit_count - 1;
        u->items[0] = OBJ_VIAL_BOMB;
        u->item_count = 1;
        u->in_use = 0;
        u->ap = 40;
        check(brew_throw_vial(&world, &rng, 0, -1, 0),
              "m4c: the bomb flies");
        check(world.units[g1].con < 32 || world.units[g2].con < 32 ||
              world.unit_count < 3,
              "m4c: the explosion wounds the goblins");
    }

    {   /* dragons need the herb, and spend it */
        world_load_bin(&world, MAPBIN_TESTLAND, MAPBIN_TESTLAND_LEN);
        world.unit_count = 1;
        book.level[SP_GOLD_DRAGON] = 1;
        world.units[0].mana = 200;
        world.units[0].ap = 40;
        check(spell_summon(&world, &book, 0, SP_GOLD_DRAGON, &rng) == 0,
              "m4c: no dragon without a cauldron");
        brew_set_cauldron(&world, 6, 6, false, 0xFF);
        check(spell_summon(&world, &book, 0, SP_GOLD_DRAGON, &rng) == 0,
              "m4c: no dragon without dragon herb");
        {
            world.objects[world.object_count].x = 6;
            world.objects[world.object_count].y = 6;
            world.objects[world.object_count].tile = T_OBJ_DRAGON_HERB;
            world.object_count++;
        }
        check(spell_summon(&world, &book, 0, SP_GOLD_DRAGON, &rng) == 1 &&
              world.units[1].kind == CR_GOLD_DRAGON &&
              !items_kind_at(&world, 6, 6) == false,   /* herb spent */
              "m4c: the dragon rises and the herb is spent");
        check(items_kind_at(&world, 6, 6) != OBJ_DRAGON_HERB,
              "m4c: the herb is gone after the summon");
    }
}

/* Regressions from the M4a-c review. */
static void test_m4_review(void)
{
    Spellbook book;
    Rng rng;
    Unit *u;
    CombatResult r;
    uint8_t g;

    rng_seed(&rng, 11);
    world_load_bin(&world, MAPBIN_TESTLAND, MAPBIN_TESTLAND_LEN);
    world.unit_count = 1;                    /* the wizard at 6,6 */
    u = &world.units[0];

    /* flying potion: ground budget in the air, lands when it wears off */
    effect_grant(u, EFF_FLYING, 1, 2);
    check(world_take_off(&world, 0), "m4r: the potion lets the wizard fly");
    world_new_turn(&world);
    check((u->flags & UF_FLYING) && u->ap == 2 * u->ap_max,
          "m4r: airborne on a potion gets twice the ground AP");
    world_new_turn(&world);
    check(!(u->flags & UF_FLYING), "m4r: lands when the potion wears off");

    /* cauldrons: the object is the truth, full ones stay put */
    u->ap = 40;
    brew_set_cauldron(&world, 6, 6, false, 0xFF);
    check(brew_cauldron_at(&world, 6, 6) != NULL, "m4r: empty cauldron placed");
    check(items_pick_up(&world, 0) && brew_cauldron_at(&world, 6, 6) == NULL,
          "m4r: a carried cauldron leaves no ghost behind");
    u->in_use = (uint8_t)(u->item_count - 1);
    u->x = 9;
    u->y = 5;
    check(items_drop(&world, 0) && brew_cauldron_at(&world, 9, 5) != NULL &&
          brew_cauldron_at(&world, 9, 5)->doses == 0,
          "m4r: set down elsewhere it is an empty cauldron again");
    brew_set_cauldron(&world, 9, 5, true, SP_HEALING_POTION);
    u->ap = 40;
    check(!items_pick_up(&world, 0), "m4r: a full cauldron cannot be carried");
    check(brew_ingredient_potion(OBJ_DRAGON_HERB) == 0xFF,
          "m4r: dragon herb brews no healing potion");

    /* targeted spells need range and sight (D17) */
    world_load_bin(&world, MAPBIN_TESTLAND, MAPBIN_TESTLAND_LEN);
    world.unit_count = 1;
    u = &world.units[0];
    memset(&book, 0, sizeof book);
    book.level[SP_CURSE] = 3;
    g = world_spawn_unit(&world, OWN_P2, CR_GOBLIN, 20, 20);
    {
        SpellShot shot;
        u->ap = 40;
        u->mana = 80;
        check(spell_apply(&world, &book, 0, SP_CURSE, world.units[g].x,
                          world.units[g].y, false, &rng, &shot) == CAST_REJECTED &&
              book.level[SP_CURSE] == 3,
              "m4r: no curse beyond the spell range");
        world.units[g].x = 12;               /* behind the house wall */
        world.units[g].y = 6;
        check(spell_apply(&world, &book, 0, SP_CURSE, 12, 6, false, &rng, &shot) ==
              CAST_REJECTED, "m4r: no curse through walls");
    }

    /* the AI does not see invisible units */
    world.units[g].x = 7;
    world.units[g].y = 6;
    world.units[g].owner = OWN_NEUTRAL;
    check(ai_nearest_enemy(&world, g, 9) == 0, "m4r: the goblin sees the wizard");
    effect_grant(u, EFF_INVISIBLE, 1, 3);
    check(ai_nearest_enemy(&world, g, 9) == NO_UNIT,
          "m4r: but not the invisible wizard");
    effect_tick(u);
    effect_tick(u);
    effect_tick(u);

    /* the defender strikes back after a harmless blow (GDD 6, 4.2) */
    world.units[g].kind = CR_ZOMBIE;
    world.units[g].flags |= UF_UNDEAD;
    world.units[g].owner = OWN_P2;
    world.units[g].ap = 30;
    world.units[g].sta = 40;
    u->ap = 40;
    check(combat_melee(&world, &rng, 0, g, &r) && !r.hit && r.returned,
          "m4r: undead shrug off the blow and strike back");

    /* enchanted weapons double their values (GDD 6.1) */
    u->items[0] = OBJ_SWORD;
    u->item_count = 1;
    u->in_use = 0;
    u->con = u->con_max;
    {   /* K7: an enchanted weapon counts double */
        uint8_t plain = items_combat(&world, 0);
        effect_grant(u, EFF_MAGIC_WEAPON, 1, 2);
        check(items_combat(&world, 0) == (uint8_t)(plain + WEAPONS[WEAPON_SWORD].combat),
              "k7: an enchanted sword adds its Combat twice");
    }
}

static void test_m4d(void)
{
    Rng rng;
    uint8_t k, rounds;

    world_load_bin(&world, MAPBIN_TESTLAND, MAPBIN_TESTLAND_LEN);
    world.unit_count = 0;
    area_reset();

    check(area_damage(AREA_FIRE, 3) == 31 && area_damage(AREA_BLOB, 3) == 20 &&
          area_damage(AREA_VINE, 5) == 8 && area_damage(AREA_FLOOD, 5) == 0,
          "0e: damage per round 25+2F, 16+2(B-1), vine 8 (K8.3)");
    check(FLOOR_FIRE[FL_GRASS] > 0 && FLOOR_FIRE[FL_WATER] == 0 &&
          FEATURE_FIRE[FE_TREE] > 0 && FLOOR_VINE[FL_STONE] < FLOOR_VINE[FL_GRASS],
          "0e: grass and trees burn, water and stone do not");

    {   /* area_set ignores the dice but not the terrain (water at 15,0) */
        check(!area_set(&world, AREA_FIRE, 3, OWN_P1, 15, 0),
              "0e: no fire on water");
        check(area_set(&world, AREA_FIRE, 3, OWN_P1, 20, 19) &&
              area_kind_at(&world, 20, 19) == AREA_FIRE &&
              area_power_at(&world, 20, 19) == 3,
              "0e: fire takes grass");
        area_reset();
    }

    {   /* the cast ignites with RND(10-L) < flammability: grass 12 always */
        uint8_t n = 0;
        for (k = 0; k < 20; k++) {
            area_reset();
            rng_seed(&rng, 70 + k);
            n = (uint8_t)(n + area_cast(&world, &rng, AREA_FIRE, 4, OWN_P1, 20, 19));
        }
        check(n == 20, "0e: fire at level 4 always catches on grass (6 < 12)");
        n = 0;
        for (k = 0; k < 40; k++) {
            area_reset();
            rng_seed(&rng, 70 + k);
            n = (uint8_t)(n + area_cast(&world, &rng, AREA_FIRE, 4, OWN_P1, 6, 6));
        }
        check(n == 0, "0e: fire never catches on a stone floor");
        area_reset();
    }

    {   /* spread over open grass dies out on its own (survival rule) */
        rounds = 0;
        area_reset();
        rng_seed(&rng, 42);
        check(area_cast(&world, &rng, AREA_FIRE, 5, OWN_P1, 20, 19) == 1,
              "0e: fire starts on grass");
        while (area_round_end(&world, &rng) > 0 && rounds < 200)
            rounds++;
        check(rounds >= 2 && rounds < 200,
              "0e: the fire spreads and dies out on its own");
        check(area_kind_at(&world, 20, 19) == AREA_NONE,
              "0e: the field is clean again");
    }

    {   /* a blob persists longer at a higher level and spreads within the cap */
        uint8_t max_fields = 0;
        area_reset();
        rng_seed(&rng, 7);
        area_set(&world, AREA_BLOB, 8, OWN_P1, 20, 19);
        for (k = 0; k < 12; k++) {
            Area *ar;
            area_round_end(&world, &rng);
            ar = area_at(&world, 20, 19);
            if (ar && ar->count > max_fields)
                max_fields = ar->count;
        }
        check(max_fields > 1 && max_fields <= AREA_FIELDS_MAX,
              "0e: a strong blob spreads within the cap");
    }

    {   /* spread chance: RND(70-3F)+1 <= flammability per neighbour */
        int16_t sx = -1, sy = -1, x, y;
        uint16_t seed, spread = 0;
        for (y = 1; y < 35 && sx < 0; y++)
            for (x = 1; x < 35 && sx < 0; x++) {
                int8_t dx, dy;
                bool open = true;
                for (dy = -1; dy <= 1; dy++)
                    for (dx = -1; dx <= 1; dx++)
                        if (world.floor[y + dy][x + dx] != FL_GRASS ||
                            world.feature[y + dy][x + dx] != FE_NONE)
                            open = false;
                if (open) {
                    sx = x;
                    sy = y;
                }
            }
        check(sx >= 0, "0e: testland has an open grass patch");
        for (seed = 0; seed < 300 && sx >= 0; seed++) {
            Area *ar;
            area_reset();
            rng_seed(&rng, 500 + seed);
            area_set(&world, AREA_FIRE, 3, OWN_P1, sx, sy);
            area_round_end(&world, &rng);
            {
                Area exp[2];
                if (area_export(exp, 2) > 0 && exp[0].count > 1)
                    spread++;
            }
            (void)ar;
        }
        /* 8 neighbours, each 12/61: P(any) = 1 - (49/61)^8 = 83 % */
        check(spread > 200 && spread < 290,
              "0e: level 3 fire spreads from grass in most rounds");
        area_reset();
    }

    {   /* blob and flood do not creep into walls (feature wall) */
        area_reset();
        check(!area_set(&world, AREA_BLOB, 4, OWN_P1, 3, 4) &&
              !area_set(&world, AREA_FLOOD, 4, OWN_P1, 3, 4),
              "0e: no blob or flood on a wall");
        check(area_set(&world, AREA_FIRE, 4, OWN_P1, 21, 4),
              "0e: fire still takes a tree");
        area_reset();
    }

    {   /* a burnt tree is gone */
        bool gone = false;
        uint16_t s;
        for (s = 0; s < 200 && !gone; s++) {
            world_load_bin(&world, MAPBIN_TESTLAND, MAPBIN_TESTLAND_LEN);
            world.unit_count = 0;
            area_reset();
            area_set(&world, AREA_FIRE, 1, OWN_P1, 21, 4);
            rng_seed(&rng, 800 + s);
            area_round_end(&world, &rng);
            if (world.feature[4][21] == FE_NONE)
                gone = true;
        }
        check(gone, "0e: a fire that goes out leaves no tree behind");
        world_load_bin(&world, MAPBIN_TESTLAND, MAPBIN_TESTLAND_LEN);
        world.unit_count = 0;
        area_reset();
    }

    {   /* flood puts out fire on its field; fire burns scrolls, not gold */
        area_reset();
        area_set(&world, AREA_FIRE, 4, OWN_P1, 20, 19);
        check(area_set(&world, AREA_FLOOD, 4, OWN_P2, 20, 19) &&
              area_kind_at(&world, 20, 19) == AREA_FLOOD &&
              area_active_count() == 1,
              "0e: flood douses the fire on its field");
        area_reset();
        world.object_count = 2;
        world.objects[0].x = 20; world.objects[0].y = 19;
        world.objects[0].tile = OBJECTS[OBJ_SCROLL].tile;
        world.objects[1].x = 20; world.objects[1].y = 19;
        world.objects[1].tile = OBJECTS[OBJ_GOLD].tile;
        area_set(&world, AREA_FIRE, 4, OWN_P1, 20, 19);
        rng_seed(&rng, 11);
        area_round_end(&world, &rng);
        check(world.object_count == 1 &&
              world.objects[0].tile == OBJECTS[OBJ_GOLD].tile,
              "0e: fire burns the scroll and leaves the gold");
        area_reset();
        world.object_count = 1;
        area_set(&world, AREA_FLOOD, 4, OWN_P1, 20, 19);
        area_round_end(&world, &rng);
        check(world.object_count == 0, "0e: a flood washes everything away");
        world.object_count = 0;
        area_reset();
    }

    {   /* every wizard has his own fire; the level follows the last cast */
        area_reset();
        area_set(&world, AREA_FIRE, 4, OWN_P1, 20, 19);
        area_set(&world, AREA_FIRE, 2, OWN_P2, 22, 19);
        check(area_active_count() == 2 && area_power_at(&world, 20, 19) == 4 &&
              area_power_at(&world, 22, 19) == 2,
              "0e: two wizards, two fires with their own level");
        check(!area_set(&world, AREA_BLOB, 4, OWN_P1, 22, 19),
              "0e: another kind cannot take a held field");
        area_set(&world, AREA_FIRE, 1, OWN_P1, 20, 19);
        check(area_power_at(&world, 20, 19) == 1,
              "0e: a weaker later cast weakens the fire (F of the last cast)");
        area_reset();
    }

    {   /* vine and flood fill a square around the target */
        uint8_t got;
        area_reset();
        rng_seed(&rng, 9);
        got = area_cast(&world, &rng, AREA_VINE, 5, OWN_P1, 20, 19);
        check(got >= 8 && got <= 81, "0e: a vine cast fills many fields");
        check(area_active_count() == 1, "0e: and they are one area");
        area_reset();
        rng_seed(&rng, 9);
        got = area_cast(&world, &rng, AREA_FLOOD, 1, OWN_P1, 20, 19);
        check(got >= 1 && got <= 25, "0e: level 1 reaches D <= 4 only");
        area_reset();
    }

    {   /* the spell path: range, payment, area (spell_apply) */
        Spellbook sb;
        SpellShot shot;
        uint8_t g;
        area_reset();
        world.unit_count = 0;
        g = world_spawn_unit(&world, OWN_P1, CR_WIZARD, 20, 19);
        world.units[g].mana = 100;
        world.units[g].ap = 40;
        memset(&sb, 0, sizeof sb);
        sb.level[SP_MAGIC_FIRE] = 4;
        rng_seed(&rng, 3);
        check(spell_apply(&world, &sb, g, SP_MAGIC_FIRE, 21, 19, false, &rng,
                          &shot) == CAST_OK && shot.hit &&
              area_kind_at(&world, 21, 19) == AREA_FIRE &&
              sb.level[SP_MAGIC_FIRE] == 3 && world.units[g].mana < 100,
              "0e: spell_apply casts fire and pays");
        world.units[g].ap = 40;
        check(spell_apply(&world, &sb, g, SP_MAGIC_FIRE, 16, 19, false, &rng,
                          &shot) == CAST_OK && !shot.hit &&   /* water: fizzles */
              sb.level[SP_MAGIC_FIRE] == 2,
              "0e: fire on water fizzles but costs the cast");
        world.units[g].ap = 40;
        check(spell_apply(&world, &sb, g, SP_MAGIC_FIRE, 20, 0, false, &rng,
                          &shot) == CAST_REJECTED &&   /* out of reach */
              sb.level[SP_MAGIC_FIRE] == 2,
              "0e: out of reach costs nothing");
        area_reset();
        world.unit_count = 0;
    }

    {   /* fire and blob hurt only enemies, each exactly once, ignoring Defence */
        uint8_t doomed, enemy, own, before_e, before_o;
        area_reset();
        world.unit_count = 0;
        doomed = world_spawn_unit(&world, OWN_P2, CR_GOBLIN, 20, 19);
        enemy = world_spawn_unit(&world, OWN_P2, CR_GOBLIN, 20, 20);
        own = world_spawn_unit(&world, OWN_P1, CR_DWARF, 20, 21);
        world.units[enemy].x = 20; world.units[enemy].y = 19;
        world.units[own].x = 20; world.units[own].y = 19;
        world.units[doomed].con = 1;     /* dies; the last unit swaps in */
        world.units[enemy].con = world.units[enemy].con_max = 120;
        before_e = world.units[enemy].con;
        before_o = world.units[own].con;
        check(area_set(&world, AREA_FIRE, 5, OWN_P1, 20, 19),
              "0e: set under the enemies");
        rng_seed(&rng, 5);
        area_round_end(&world, &rng);
        check(world.unit_count == 2,
              "0e: the weak enemy burns to death");
        check(world.units[0].owner == OWN_P1 || world.units[1].owner == OWN_P1,
              "0e: own dwarf survives its own fire");
        {
            uint8_t i;
            for (i = 0; i < world.unit_count; i++) {
                if (world.units[i].owner == OWN_P2)
                    check(world.units[i].con == before_e - area_damage(AREA_FIRE, 5),
                          "0e: the enemy takes 25+2F exactly once");
                else
                    check(world.units[i].con == before_o,
                          "0e: own units stay unharmed by their fire");
            }
        }
        world.unit_count = 0;
        area_reset();
    }

    {   /* flood is water: slow to wade, drowning by stamina (D48), no instant death */
        uint8_t g;
        area_reset();
        world.unit_count = 0;
        g = world_spawn_unit(&world, OWN_P2, CR_GOBLIN, 20, 19);
        world.units[g].ap = 40;
        world.units[g].sta = 60;
        area_set(&world, AREA_FLOOD, 4, OWN_P1, 21, 19);
        check(world_unit_step_cost(&world, g, 21, 19, false) == 12,
              "0e: a flooded field costs 12 AP");
        area_set(&world, AREA_FIRE, 4, OWN_P1, 20, 20);
        check(world_unit_step_cost(&world, g, 20, 20, false) == 16,
              "0e: a burning field costs 16 AP");
        area_set(&world, AREA_FLOOD, 4, OWN_P1, 20, 19);
        world.units[g].con = world.units[g].con_max = 100;
        for (k = 0; k < 6; k++)
            world_new_turn(&world);
        check(world.units[g].con < 100 && world.unit_count == 1,
              "0e: standing in a flood drains stamina, then hurts");
        world.unit_count = 0;
        area_reset();
    }

    {   /* vine/blob block movement and can be torn through (toughness 40/50) */
        uint8_t g;
        bool torn = false;
        uint16_t tries;
        area_reset();
        world.unit_count = 0;
        g = world_spawn_unit(&world, OWN_P1, CR_WIZARD, 20, 19);
        world.units[g].ap = 40;
        check(area_set(&world, AREA_VINE, 5, OWN_P1, 21, 19),
              "0e: vine east of the wizard");
        check(!world_move_unit(&world, g, 1, 0) && area_toughness(&world, 21, 19) == 40,
              "0e: vine blocks the step");
        check(combat_terrain(&world, &rng, g, 21, 19, &torn) == 0,
              "0e: a weak wizard (Combat 10) cannot even try");
        world.units[g].com = 50;
        for (tries = 0; tries < 60 && !torn; tries++) {
            world.units[g].ap = 40;
            world.units[g].sta = 60;
            rng_seed(&rng, 4000 + tries);
            combat_terrain(&world, &rng, g, 21, 19, &torn);
        }
        check(torn && area_kind_at(&world, 21, 19) == AREA_NONE,
              "0e: a strong fighter tears the vine apart");
        area_reset();
    }

    {   /* performance: 4 areas stay cheap and one per kind and owner */
        uint8_t i;
        area_reset();
        rng_seed(&rng, 2);
        check(area_set(&world, AREA_FIRE, 4, OWN_P1, 5, 20) &&
              area_set(&world, AREA_BLOB, 4, OWN_P2, 13, 20) &&
              area_set(&world, AREA_VINE, 4, OWN_NEUTRAL, 25, 20) &&
              area_set(&world, AREA_FLOOD, 4, OWN_P2, 30, 20) &&
              area_active_count() == 4, "0e: four areas are active");
        for (i = 0; i < 20; i++) {
            area_round_end(&world, &rng);
            if (area_active_count() > 4) {
                check(false, "0e: never more than one area per kind and owner");
                break;
            }
        }
        area_reset();
    }
}

static void test_m4e(void)
{
    FieldLayers f;
    Rng rng;
    uint8_t wizard, mount, enemy;

    world_load_bin(&world, MAPBIN_TESTLAND, MAPBIN_TESTLAND_LEN);
    world.unit_count = 0;
    area_reset();

    check(WEAPONS[WEAPON_KNIFE].thrown == 20 && WEAPONS[WEAPON_SPEAR].ranged == 0 &&
          WEAPONS[WEAPON_CLUB].combat == 5 && WEAPONS[WEAPON_MAGIC_SLAYER].combat == 30 &&
          WEAPONS[WEAPON_AXE].thrown == 28,
          "m4e: weapon values from weapons.csv");
    check(OBJECTS[OBJ_SPEAR].weapon == WEAPON_SPEAR &&
          OBJECTS[OBJ_SLAYER].weight == 9,
          "m4e: the new weapons exist as objects");
    check(strcmp(name_object(T_OBJ_SWORD), "Schwert") == 0 &&
          strcmp(name_object(T_OBJ_RUBY), "Rubin") == 0 &&
          strcmp(name_object(T_OBJ_VIAL_HEALING), "Phiole Heilkraut") == 0 &&
          T_OBJ_VIAL_HEALING != T_OBJ_VIAL_SPEED &&   /* own tile per potion */
          strcmp(name_object(T_OBJ_SCROLL), "Schriftrolle") == 0,
          "objects show their own names on the ground and in look mode");

    {   /* riding: mount, ride along, dismount */
        wizard = world_spawn_unit(&world, OWN_P1, CR_WIZARD, 6, 7);
        mount = world_spawn_unit(&world, OWN_P1, CR_UNICORN, 6, 6);
        world.units[wizard].ap = 40;
        check(ride_mount(&world, wizard, 6, 6) && world.unit_count == 1 &&
              (world.units[0].flags & UF_RIDDEN) &&
              ride_rider_kind(&world.units[0]) == CR_WIZARD,
              "m4e: the wizard mounts the unicorn");
        mount = 0;                       /* the list re-ordered on removal */
        {   /* the pair is drawn as rider layer behind the mount layer (M4k) */
            FieldLayers rf;
            uint8_t li, rl = 0xFF;
            view_set_sight(NULL);
            view_compose(&world, 6, 6, &rf);
            for (li = 0; li < rf.n; li++)
                if (rf.ride & (1u << li))
                    rl = li;
            check(rl != 0xFF && rf.id[rl] == T_WIZARD_P1 &&
                  rf.id[rl + 1] == T_UNICORN_P1 && rf.air == 0 &&
                  rf.ride == (uint16_t)(1u << rl),
                  "m4k: the rider layer sits right behind the mount");
            world.units[mount].flags |= UF_FLYING;    /* a flying mount */
            view_compose(&world, 6, 6, &rf);
            rl = 0xFF;                                /* the shadow shifts it */
            for (li = 0; li < rf.n; li++)
                if (rf.ride & (1u << li))
                    rl = li;
            check(rl != 0xFF && rf.id[rl + 1] == T_UNICORN_P1 &&
                  (rf.air & (1u << rl)) && (rf.air & (1u << (rl + 1))),
                  "m4k: both layers of a flying pair are airborne");
            world.units[mount].flags &= (uint8_t)~UF_FLYING;
        }
        {   /* `b` finds a mount on any of the eight neighbour fields */
            static const int8_t NX[8] = {0, 1, 1, 1, 0, -1, -1, -1};
            static const int8_t NY[8] = {-1, -1, 0, 1, 1, 1, 0, -1};
            uint8_t d, found = 0, w2, m2;
            for (d = 0; d < 8; d++) {
                world.unit_count = 0;
                w2 = world_spawn_unit(&world, OWN_P1, CR_WIZARD, 20, 19);
                m2 = world_spawn_unit(&world, OWN_P1, CR_UNICORN,
                                      (uint8_t)(20 + NX[d]), (uint8_t)(19 + NY[d]));
                (void)m2;
                world.units[w2].ap = 40;
                if (ride_mount_adjacent(&world, w2) && world.unit_count == 1)
                    found++;
            }
            check(found == 8, "m4e: the wizard mounts a unicorn on every side");
            world.unit_count = 0;
            w2 = world_spawn_unit(&world, OWN_P1, CR_WIZARD, 20, 19);
            m2 = world_spawn_unit(&world, OWN_P1, CR_UNICORN, 22, 19);
            world.units[w2].ap = 40;
            check(!ride_mount_adjacent(&world, w2) && world.unit_count == 2,
                  "m4e: a unicorn two fields away is out of reach");
            world.unit_count = 0;
            wizard = world_spawn_unit(&world, OWN_P1, CR_WIZARD, 6, 7);
            mount = world_spawn_unit(&world, OWN_P1, CR_UNICORN, 6, 6);
            world.units[wizard].ap = 40;
            check(ride_mount(&world, wizard, 6, 6), "m4e: mount for the ride test");
            mount = 0;
        }
        check(world_move_unit(&world, mount, 0, -1),
              "m4e: the pair rides as one unit");
        check(!world_engaged(&world, mount),
              "m4e: the rider attacks from anywhere (D21)");
        world.units[mount].ap = 40;
        check(ride_dismount(&world, mount) && world.unit_count == 2 &&
              !(world.units[mount].flags & UF_RIDDEN),
              "m4e: dismounting brings the rider back");
    }

    {   /* D61/D76: an open door's leaf stands beside the doorway, drawn only, never blocking */
        static const uint8_t *const MAPS[6] = {
            MAPBIN_MANY_COLOURED_LAND, MAPBIN_RAGARILS_DOMAIN, MAPBIN_SLAYERS_DUNGEON,
            MAPBIN_TESTLAND, MAPBIN_TUTORIAL, MAPBIN_WIZARD_HOUSE};
        const uint16_t LENS[6] = {
            MAPBIN_MANY_COLOURED_LAND_LEN, MAPBIN_RAGARILS_DOMAIN_LEN,
            MAPBIN_SLAYERS_DUNGEON_LEN, MAPBIN_TESTLAND_LEN, MAPBIN_TUTORIAL_LEN,
            MAPBIN_WIZARD_HOUSE_LEN};
        uint8_t m, jammed = 0, open_bare = 0, u, leaf = FE_NONE;
        int16_t x, y, lx, ly;
        for (m = 0; m < 6; m++) {
            world_load_bin(&world, MAPS[m], LENS[m]);
            for (y = 0; y < world.h; y++)
                for (x = 0; x < world.w; x++) {
                    uint8_t fe = world.feature[y][x];
                    if (world_is_gate(&world, x, y))
                        continue;
                    if (fe == FE_DOOR_CLOSED &&
                        !world_leaf_spot(&world, x, y, -1, -1, &lx, &ly, &leaf))
                        jammed++;
                    if (fe == FE_DOOR_OPEN) {
                        int16_t dx, dy;
                        bool found = false;
                        for (dy = -1; dy <= 1; dy += 2)
                            for (dx = -1; dx <= 1; dx += 2) {
                                uint8_t f2 = world_feature(&world, (int16_t)(x + dx),
                                                           (int16_t)(y + dy));
                                if (f2 >= FE_LEAF_N && f2 <= FE_LEAF_W)
                                    found = true;
                            }
                        if (!found)
                            open_bare++;
                    }
                }
        }
        check(jammed == 0, "d61: every door on the maps has a leaf field");
        check(open_bare == 0, "d61: doors that start open have their leaf");

        /* D65: the leaf of a door in an east-west wall is drawn in the frame;
         * hinge side and swing follow the leaf field */
        {
            uint16_t n_h = 0, wrong = 0;
            for (m = 0; m < 6; m++) {
                world_load_bin(&world, MAPS[m], LENS[m]);
                for (y = 0; y < world.h; y++)
                    for (x = 0; x < world.w; x++) {
                        FieldLayers df;
                        uint16_t want;
                        if (world.feature[y][x] != FE_DOOR_CLOSED ||
                            world_is_gate(&world, x, y) ||
                            world_is_wall_line(&world, x, (int16_t)(y - 1)) ||
                            world_is_wall_line(&world, x, (int16_t)(y + 1)) ||
                            !world_leaf_spot(&world, x, y, -1, -1, &lx, &ly, &leaf))
                            continue;
                        world.feature[y][x] = FE_DOOR_OPEN;
                        world.feature[ly][lx] = leaf;
                        n_h++;
                        if (leaf == FE_LEAF_W)       /* field east of the door: hinge east */
                            want = ly > y ? T_DOOR_H_OPEN_E : T_DOOR_H_FAR_E;
                        else
                            want = ly > y ? T_DOOR_H_OPEN_W : T_DOOR_H_FAR_W;
                        view_compose(&world, x, y, &df);
                        if (!has_layer(&df, want) || has_layer(&df, T_DOOR_H_OPEN))
                            wrong++;
                        world.feature[y][x] = FE_DOOR_CLOSED;
                        world.feature[ly][lx] = FE_NONE;
                    }
            }
            check(n_h > 0, "d65: the maps have doors in east-west walls");
            check(wrong == 0, "d65: the leaf is drawn in the frame, on the hinge side");
        }

        /* testland: candles stand inside on both sides of the east door,
         * so the leaf swings out; opened from outside it stands NE/SE */
        world_load_bin(&world, MAPBIN_TESTLAND, MAPBIN_TESTLAND_LEN);
        world.unit_count = 0;
        u = world_spawn_unit(&world, OWN_P1, CR_GOBLIN, 9, 5);
        world.units[u].ap = 40;
        check(world_open_door(&world, u, 8, 5) &&
              world.feature[5][8] == FE_DOOR_OPEN &&
              world.feature[4][9] == FE_LEAF_S && !world_blocks(&world, 9, 4),
              "d76: the leaf swings out past the candles and does not block its field");
        check(!world_blocks(&world, 8, 5) && !world_blocks(&world, 9, 5),
              "d61: the doorway itself stays free");
        check(world_close_door(&world, u, 8, 5) &&
              world.feature[4][9] == FE_NONE && world.feature[5][8] == FE_DOOR_CLOSED,
              "d61: closing folds the leaf back");
        world.feature[4][9] = FE_ROCK;
        world.feature[6][9] = FE_ROCK;
        world.units[u].ap = 40;
        check(world_open_door(&world, u, 8, 5) &&
              world.feature[5][8] == FE_DOOR_OPEN && !world_blocks(&world, 8, 5),
              "d76: no room for the leaf anywhere - the door still opens, bare frame");
        check(world_close_door(&world, u, 8, 5), "d76: and closes again");
        world.feature[4][9] = FE_NONE;
        world.feature[6][9] = FE_NONE;
        world.feature[4][7] = FE_NONE;            /* clear the inner candle */
        check(world_open_door(&world, u, 8, 5) && world.feature[4][7] == FE_LEAF_S,
              "d61: with room inside, opened from outside it swings in");
    }

    {   /* D60: the rider acts from the saddle and keeps his own values */
        Spellbook rb;
        uint8_t wz, mt, i, before;
        world_load_bin(&world, MAPBIN_TESTLAND, MAPBIN_TESTLAND_LEN);
        world.unit_count = 0;
        wz = world_spawn_unit(&world, OWN_P1, CR_WIZARD, 20, 19);
        mt = world_spawn_unit(&world, OWN_P1, CR_UNICORN, 21, 19);
        world.units[wz].ap = 40;
        world.units[wz].con = 17;
        world.units[wz].mana = 33;
        world.units[wz].items[0] = OBJ_SWORD;
        world.units[wz].items[1] = OBJ_APPLE;
        world.units[wz].item_count = 2;
        check(ride_mount(&world, wz, 21, 19) && world.unit_count == 1,
              "d60: the wizard mounts");
        mt = 0;
        check(world.units[mt].mana == 33 && world.units[mt].rider_con == 17 &&
              ride_actor_kind(&world.units[mt]) == CR_WIZARD,
              "d60: the mount carries the rider's mana and values");
        memset(&rb, 0, sizeof rb);
        rb.level[SP_MAGIC_BOLT] = 1;
        world.units[mt].ap = 40;
        check(spell_can_cast(&world, &rb, mt, SP_MAGIC_BOLT),
              "d60: the wizard casts from the saddle");
        world.feature[19][22] = FE_DOOR_CLOSED;
        check(world_open_door(&world, mt, 22, 19) &&
              world.feature[19][22] == FE_DOOR_OPEN,
              "d60: the rider opens a door from the saddle");
        world.objects[world.object_count].x = 21;
        world.objects[world.object_count].y = 19;
        world.objects[world.object_count].tile = OBJECTS[OBJ_KNIFE].tile;
        world.object_count++;
        check(items_pick_up(&world, mt) && world.units[mt].item_count == 3,
              "d60: the rider picks up from the saddle");
        world.units[mt].ap = 40;
        check(ride_dismount(&world, mt) && world.unit_count == 2,
              "d60: dismount");
        wz = 1;
        check(world.units[wz].kind == CR_WIZARD && world.units[wz].con == 17 &&
              world.units[wz].mana == 33 && world.units[wz].item_count == 3 &&
              world.units[wz].items[1] == OBJ_APPLE &&
              world.units[mt].mana_max == 0 && world.units[mt].item_count == 0,
              "d60: the rider gets off with his own values and whole pack");
        world.units[wz].ap = 40;
        check(ride_mount(&world, wz, 21, 19) && world.unit_count == 1,
              "d60: mount again");
        before = world.object_count;
        world_kill_unit(&world, 0, CR_GOBLIN, OWN_P2, true);
        for (i = 0; i < world.unit_count; i++)
            if (world.units[i].kind == CR_WIZARD)
                break;
        check(i < world.unit_count && world.units[i].con == 17 &&
              world.units[i].item_count == 3 && world.object_count == before,
              "d60: a dying mount throws its rider off, he keeps his pack");
    }

    {   /* roof: loaded from the v4 map, blocks sight and landing */
        world_load_bin(&world, MAPBIN_MANY_COLOURED_LAND,
                       MAPBIN_MANY_COLOURED_LAND_LEN);
        {   /* scenario 1 starts with the wizard alone (creatures come from
             * the spellbook) */
            uint8_t k, own = 0;
            for (k = 0; k < world.unit_count; k++)
                if (world.units[k].owner == OWN_P1)
                    own++;
            check(own == 1 && world.units[0].kind == CR_WIZARD,
                  "scenario 1: player 1 starts with the wizard only");
        }
        world.unit_count = 0;
        check(world_has_roof(&world, 5, 5) && !world_has_roof(&world, 20, 19),
              "m4e: the house carries a roof");
        view_set_sight(NULL);
        view_set_roof_viewer(-1, 0);
        view_compose(&world, 5, 5, &f);
        check(has_layer(&f, T_ROOF), "m4e: the roof is visible outside");

        /* D41 (playtest 2026-10-05): only the active figure's line of sight
         * lifts a roof. This replaces F7, where one own unit anywhere under
         * the roof uncovered the whole building - a second figure outside
         * then looked straight into the house. The checks below hold
         * whatever the house looks like. */
        {
            view_set_roof_viewer(5, 5);              /* the active figure is there */
            view_compose(&world, 5, 5, &f);
            check(!has_layer(&f, T_ROOF),
                  "d41: the active figure uncovers its own roofed field");

            view_set_roof_viewer(20, 19);            /* outside, far away */
            view_compose(&world, 5, 5, &f);
            check(has_layer(&f, T_ROOF),
                  "d41: from outside the roof stays closed");

            /* The reversal of F7: another own unit under the roof must not
             * uncover anything while the active figure stands outside. */
            world.unit_count = 0;
            world_spawn_unit(&world, OWN_P1, CR_WIZARD, 5, 5);
            view_set_roof_viewer(20, 19);
            view_compose(&world, 5, 5, &f);
            check(has_layer(&f, T_ROOF),
                  "d41: a second own unit inside does not open the house");
            {   /* D44: the roof is laid last, so it covers the figure instead
                 * of the figure standing on it. This check used to assert the
                 * opposite as a characterisation of the old layer order. */
                uint8_t li, roof_i = 0xFF, unit_i = 0xFF;
                for (li = 0; li < f.n; li++) {
                    if (f.id[li] == T_ROOF)
                        roof_i = li;
                    if (f.id[li] == T_WIZARD_P1)
                        unit_i = li;
                }
                check(roof_i != 0xFF && unit_i != 0xFF && roof_i > unit_i,
                      "d44: a closed roof covers the figure under it");
            }
            world.unit_count = 0;
        }
        {   /* a ridden pair inside the lifted roof keeps its masks in step */
            FieldLayers rf;
            uint8_t li, rl = 0xFF, mt;
            world.unit_count = 0;
            view_set_sight(NULL);
            view_set_roof_viewer(5, 5);          /* the pair is the active figure */
            mt = world_spawn_unit(&world, OWN_P1, CR_UNICORN, 5, 5);
            world.units[mt].flags |= UF_RIDDEN;
            world.units[mt].rider_kind = CR_WIZARD;
            view_compose(&world, 5, 5, &rf);
            for (li = 0; li < rf.n; li++)
                if (rf.ride & (1u << li))
                    rl = li;
            check(!has_layer(&rf, T_ROOF) && rl != 0xFF &&
                  rf.id[rl] == T_WIZARD_P1 && rf.id[rl + 1] == T_UNICORN_P1,
                  "m4k: the rider mask survives the lifted roof");
            world.unit_count = 0;
        }
        {   /* flying units cannot land on a roof */
            uint8_t bat = world_spawn_unit(&world, OWN_P1, CR_GIANT_BAT, 5, 5);
            world.units[bat].flags |= UF_FLYING;
            check(!world_land(&world, bat),
                  "m4e: no landing under the roof");
        }
        {   /* D46 (2026-10-05): indoors no roof is drawn at all. The
             * per-field lift (D41) missed the north wall row - shadowcasting
             * saw those fields, the ray did not - so roof tiles kept
             * popping up between the rooms while walking around. */
            view_set_roof_viewer(6, 6);          /* the wizard starts indoors */
            check(world_has_roof(&world, 7, 2) &&
                  !sight_has_los(&world, 6, 6, 7, 2),
                  "d46: premise - a roofed wall the room has no ray to");
            view_compose(&world, 7, 2, &f);
            check(!has_layer(&f, T_ROOF),
                  "d46: indoors no roof, not even without line of sight");
            view_compose(&world, 5, 5, &f);
            check(!has_layer(&f, T_ROOF),
                  "d46: indoors no roof over the room itself either");
        }
        {   /* the roof keeps the outer wall covered (2026-10-05): a wall
             * field carries its roof even in line of sight - the facade
             * must not go open right in front of the viewer. */
            view_set_roof_viewer(19, 6);         /* outside, east of the house */
            check(world_has_roof(&world, 17, 6) &&
                  sight_has_los(&world, 19, 6, 17, 6),
                  "d46: premise - the east wall is roofed and in sight");
            view_compose(&world, 17, 6, &f);
            check(has_layer(&f, T_ROOF),
                  "d46: the wall keeps its roof even in line of sight");
            view_set_roof_viewer(-1, 0);
            world.feature[20][20] = FE_WALL;     /* a lone wall far from any roof */
            world_map_changed(&world);
            view_compose(&world, 20, 20, &f);
            check(!has_layer(&f, T_ROOF),
                  "d46: a wall away from any roof stays bare");
            world.feature[20][20] = FE_NONE;
            world_map_changed(&world);
        }
        {   /* a remembered roof keeps its normal texture (2026-10-05): no
             * dither overlay on roofed fields - from outside the roof read
             * as "obscured" patchwork. The opaque tile hides what is below
             * it anyway. */
            Sight s;
            world.unit_count = 0;
            view_set_roof_viewer(-1, 0);
            world_spawn_unit(&world, OWN_P1, CR_WIZARD, 6, 6);
            sight_init(&s, OWN_P1);
            sight_compute(&world, &s);           /* explores the rooms */
            world.unit_count = 0;                /* the explorer leaves */
            world_spawn_unit(&world, OWN_P1, CR_WIZARD, 20, 19);
            sight_compute(&world, &s);           /* tower out of sight now */
            view_set_sight(&s);
            view_compose(&world, 5, 5, &f);
            check(sight_explored(&s, &world, 5, 5) &&
                  !sight_visible(&s, &world, 5, 5),
                  "d46: premise - the room is remembered, not seen");
            check(has_layer(&f, T_ROOF) &&
                  !has_layer(&f, T_OVERLAY_REMEMBERED),
                  "d46: a remembered roof shows in its normal texture");
            view_set_sight(NULL);
            world.unit_count = 0;
        }
    }

    {   /* the new weapons in the melee path (values land via items_*) */
        uint8_t a, b;
        a = world_spawn_unit(&world, OWN_P1, CR_WIZARD, 6, 6);
        b = world_spawn_unit(&world, OWN_P2, CR_GOBLIN, 7, 6);
        world.units[a].items[0] = OBJ_AXE;
        world.units[a].item_count = 1;
        world.units[a].in_use = 0;
        check(items_combat(&world, a) == 19,
              "k7: the axe adds its 9 Combat");
        rng_seed(&rng, 5);
        /* wizard 10 against goblin defence 9: 55 %. Under D31 the axe's
         * +9 pushed this to the 90 % ceiling. */
        check(combat_hit_chance(items_combat(&world, a),
                                items_defence(&world, b)) == 75,
              "k7: axe vs goblin: 30 of 40 rolls hurt");
    }

    {   /* enemy in the walled house is hidden from outside rays */
        world_load_bin(&world, MAPBIN_MANY_COLOURED_LAND,
                       MAPBIN_MANY_COLOURED_LAND_LEN);
        world.unit_count = 0;
        world_spawn_unit(&world, OWN_P1, CR_WIZARD, 20, 19);
        world_spawn_unit(&world, OWN_P2, CR_GOBLIN, 5, 5);
        {
            Sight s;
            sight_init(&s, OWN_P1);
            sight_compute(&world, &s);
            check(!sight_visible(&s, &world, 5, 5),
                  "m4e: the walls hide the enemy inside");
        }
        view_set_sight(NULL);
    }
    (void)enemy;

    {   /* v4 map with roof but no portal: filler block keeps the layout */
        uint8_t m[25] = {'L', 'O', 'C', 'M', MAPBIN_VERSION, 0, 0, 2, 1, 0};
        m[5] = (uint8_t)(TILE_COUNT & 0xFF);
        m[6] = (uint8_t)(TILE_COUNT >> 8);
        m[16] = 0;                       /* no units */
        m[17] = 0;                       /* no objects */
        m[18] = 0xFF; m[19] = 0xFF;      /* portal filler (5 bytes) */
        m[24] = 1;                       /* roof on the second field */
        check(world_load_bin(&world, m, sizeof m) &&
              world.portal_x == -1 && !world_has_roof(&world, 0, 0) &&
              world_has_roof(&world, 1, 0),
              "m4e: v4 map without portal keeps roof layout");
    }
}

static void test_m4f(void)
{
    Wizard *w = &wizard_slots[0];
    Rng rng;

    wizard_slot_reset(0);
    {   /* review fixes: lowering, validation, scenario guard */
        Wizard t;
        wizard_slot_reset(3);
        t = wizard_slots[3];
        t.xp = 100;
        check(!wizard_lower(&t, WA_COMBAT) && t.com == 5,
              "m4f: no lowering below the minimum");
        check(wizard_raise(&t, WA_COMBAT) && wizard_lower(&t, WA_COMBAT) &&
              t.com == 5 && t.xp == 100, "m4f: lowering refunds the XP");
        check(wizard_valid(&t), "m4f: a stock wizard is valid");
        t.com = 0;
        check(!wizard_valid(&t), "m4f: zero attribute is rejected");
        t = wizard_slots[3];
        t.book.level[0] = SPELL_MAX_LEVEL + 1;
        check(!wizard_valid(&t), "m4f: book level above the cap is rejected");
        t = wizard_slots[3];
        memset(t.name, 'x', sizeof t.name);
        check(!wizard_valid(&t), "m4f: unterminated name is rejected");
        t = wizard_slots[3];
        wizard_campaign_result(&t, 5, 0);
        check(t.level == 1 && t.scenarios_done == 0 && t.xp == 600 + 5,
              "m4f: scenario 0 changes no level");
    }
    {   /* stock wizard: minimums, 600 XP, EMPTY books (user rule) */
        uint16_t s, sum = 0;
        check(w->level == 1 && w->xp == 600 && w->com == 5 && w->sta == 34 &&
              w->mana_max == 80 && w->ap == 34,
              "m4f: stock wizard: minimums and 600 XP (F6)");
        for (s = 0; s < SPELL_COUNT; s++)
            sum += w->book.level[s];
        check(sum == 0, "m4f: fresh wizards start with empty books");
        wizard_apply_standard_set(w);
        check(w->book.level[SP_MAGIC_BOLT] == 4 &&
              w->book.level[SP_MAGIC_SHIELD] == 3 &&
              w->book.level[SP_GIANT_BAT] == 2 && w->book.level[SP_GRYPHON] == 1,
              "m4f: the standard template fills spells + 8 creatures");
        check(w->book.level[SP_VAMPIRE] == 1 && w->book.level[SP_GIANT] == 1,
              "m4f: the standard template includes two strong creatures");
        check(w->com > 5 && w->def > 5 && w->xp <= 1 && wizard_valid(w),
              "m4f: the template spends the 600 XP up to a few points (no cheating)");
        wizard_apply_standard_set(w);   /* idempotent: bolt already there */
        check(w->book.level[SP_MAGIC_BOLT] == 4,
              "m4f: the standard set never overwrites designed books");
        wizard_slot_reset(0);           /* back to empty for the next tests */
    }
    check(wizard_attr_cost(WA_COMBAT, 5) == 2 &&
          wizard_attr_cost(WA_DEFENCE, 5) == 2 &&
          wizard_attr_cost(WA_MAGIC_RES, 70) == 4 &&
          wizard_attr_cost(WA_CONSTITUTION, 34) == 3 &&
          wizard_attr_cost(WA_STAMINA, 34) == 4 &&
          wizard_mana_cost(w) == 8 && wizard_ap_cost(w) == 8 &&
          wizard_attr_cost(WA_COMBAT, 29) == 14 && wizard_attr_cost(WA_STAMINA, 89) == 11,
          "m4f: point cost = floor(value / divisor), start costs 2/2/4/3/4, mana 8, AP 8");

    w->xp = 50;
    check(wizard_raise(w, WA_COMBAT) && w->com == 6 && w->xp == 48,
          "m4f: raising costs XP");
    w->xp = 1;
    check(!wizard_raise(w, WA_COMBAT), "m4f: no raising without XP");

    {   /* F6 end-to-end: spend the full 600 XP over every row, never
         * below the minimums, never above the 600 total */
        Wizard b;
        uint16_t rows;
        uint8_t raise_count = 0;
        wizard_slot_reset(3);
        b = wizard_slots[3];            /* minimums + 600 XP */
        check(b.xp == 600 && b.com == 5 && b.def == 5 && b.mr == 70 &&
              b.con == 34 && b.sta == 34 && b.mana_max == 80 && b.ap == 34,
              "m4f: fresh wizard = minimums + 600 XP");
        /* buy combat to the cap 30: the price rises with the value (K3.3) */
        {
            uint16_t expect = 0, v;
            for (v = 5; v < 30; v++)
                expect = (uint16_t)(expect + v / 2);
            while (wizard_raise(&b, WA_COMBAT))
                raise_count++;
            check(raise_count == 25 && b.com == 30 && b.xp == 600 - expect,
                  "m4f: combat caps at 30 after 25 points, 206 XP");
            /* mana at 8 and AP at 8 still work */
            check(wizard_mana_raise(&b) && b.mana_max == 81 && b.xp == 600 - expect - 8 &&
                  wizard_mana_lower(&b) && b.mana_max == 80 && b.xp == 600 - expect,
                  "m4f: mana raises/refunds alongside");
            /* lower it back: every lowering refunds the full price */
            {
                uint8_t i;
                for (i = 0; i < raise_count; i++)
                    wizard_lower(&b, WA_COMBAT);
            }
            check(b.com == 5 && b.xp == 600,
                  "m4f: lowering refunds everything, back to 600");
            /* defence to its cap costs the same 206; con and stamina rise to 90 */
            for (rows = 0; rows < 25; rows++)
                wizard_raise(&b, WA_DEFENCE);
            check(b.xp == 600 - expect && b.def == 30, "m4f: defence costs like combat");
            b.xp = 5000;
            for (rows = 0; rows < 56; rows++)
                wizard_raise(&b, WA_CONSTITUTION);
            for (rows = 0; rows < 56; rows++)
                wizard_raise(&b, WA_STAMINA);
            check(b.con == 90 && b.sta == 90, "m4f: constitution and stamina cap at 90");
        }
        /* and the whole thing stays a valid wizard */
        check(wizard_valid(&b), "m4f: maxed wizard still validates");
    }

    {   /* caps */
        w->xp = 60000;
        w->mr = wizard_attr_max(WA_MAGIC_RES);
        check(!wizard_raise(w, WA_MAGIC_RES), "m4f: caps stop raising");
        w->mr = 80;
    }

    {   /* campaign: VP -> XP 1:1, level up once per scenario (GDD 9) */
        wizard_slot_reset(1);
        wizard_campaign_result(&wizard_slots[1], 75, 1);
        check(wizard_slots[1].xp == 600 + 75 && wizard_slots[1].level == 2,
              "m4f: first clear gives XP and a level");
        wizard_campaign_result(&wizard_slots[1], 20, 1);
        check(wizard_slots[1].xp == 600 + 95 && wizard_slots[1].level == 2,
              "m4f: repeating scores XP without a level");
        wizard_campaign_result(&wizard_slots[1], 10, 2);
        check(wizard_slots[1].level == 3, "m4f: scenario 2 lifts again");
    }

    {   /* the original random wizard (K3.2): 6/6/90, spells 0..2, no XP */
        uint16_t s, sum = 0;
        uint8_t over = 0;
        rng_seed(&rng, 9);
        wizard_slot_random(2, &rng);
        for (s = 0; s < SPELL_COUNT; s++) {
            sum += wizard_slots[2].book.level[s];
            if (wizard_slots[2].book.level[s] > 2)
                over++;
        }
        check(wizard_slots[2].xp == 0 && wizard_slots[2].com == 6 &&
              wizard_slots[2].def == 6 && wizard_slots[2].mr == 90 &&
              wizard_valid(&wizard_slots[2]) && sum > 20 && over == 0,
              "m4f: random wizard: 6/6/90, every spell 0..2, nothing to spend");
    }

    {   /* apply to the world: F5 - values yes, items no */
        uint8_t u;
        wizard_slots[0].com = 14;
        wizard_slots[0].con = 33;
        wizard_slots[0].sta = 66;
        world_load_bin(&world, MAPBIN_TESTLAND, MAPBIN_TESTLAND_LEN);
        world.unit_count = 1;
        u = 0;
        wizard_apply_to_world(w, &world, u);
        check(world.units[u].com == 14 && world.units[u].con == 33 &&
              world.units[u].sta == 66 && world.units[u].item_count == 0,
              "m4f: the designer wizard enters without items (F5)");
    }
    wizard_slot_reset(0);
}

static void test_m4g(void)
{
    uint8_t wizards, undead, treasures;
    bool slayer = false;

    world_load_bin(&world, MAPBIN_SLAYERS_DUNGEON, MAPBIN_SLAYERS_DUNGEON_LEN);
    check(world.w == 36 && world.h == 36 && !world.wrap,
          "m4g: slayer's dungeon is 36x36, no wrap");
    check(world.portal_x == 32 && world.portal_y == 32 &&
          world.portal_rmin == 20 && world.portal_rmax == 24,
          "m4g: dungeon portal rounds 20-24");
    wizards = undead = treasures = 0;
    {
        uint8_t i;
        for (i = 0; i < world.unit_count; i++)
            if (world.units[i].kind == CR_WIZARD)
                wizards++;
            else if (world.units[i].flags & UF_UNDEAD)
                undead++;
        for (i = 0; i < world.object_count; i++) {
            uint8_t k;
            for (k = 0; k < OBJ_COUNT; k++)
                if (OBJECTS[k].tile == world.objects[i].tile) {
                    if (OBJECTS[k].category == OC_TREASURE)
                        treasures++;
                    if (OBJECTS[k].weapon == WEAPON_SLAYER)
                        slayer = true;
                }
        }
    }
    check(wizards == 2, "m4g: dungeon has two wizards");
    check(undead == 0 && treasures == 0 && !slayer && world.unit_count == 2,
          "d35: the dungeon holds only the wizards and their kit");
    check(!world_has_roof(&world, 32, 32), "m4g: the portal lies open");

    world_load_bin(&world, MAPBIN_RAGARILS_DOMAIN, MAPBIN_RAGARILS_DOMAIN_LEN);
    check(world.w == 36 && world.h == 36 && !world.wrap,
          "m4g: ragaril's domain is 36x36, no wrap");
    check(world.portal_x == 33 && world.portal_y == 3 &&
          world.portal_rmin == 44 && world.portal_rmax == 51,
          "m4g: domain portal rounds 44-51");
    wizards = 0;
    treasures = 0;                       /* count the domain on its own */
    {
        uint8_t i, swamps = 0, woods = 0;
        for (i = 0; i < world.unit_count; i++)
            if (world.units[i].kind == CR_WIZARD)
                wizards++;
        for (i = 0; i < world.object_count; i++) {
            uint8_t k;
            for (k = 0; k < OBJ_COUNT; k++)
                if (OBJECTS[k].tile == world.objects[i].tile &&
                    OBJECTS[k].category == OC_TREASURE)
                    treasures++;
        }
        {
            uint16_t cell;
            for (cell = 0; cell < 36u * 36; cell++)
                if (world.floor[cell / 36][cell % 36] == FL_SWAMP)
                    swamps++;
                else if (world.floor[cell / 36][cell % 36] == FL_MAGIC_WOOD)
                    woods++;
        }
        check(wizards == 2, "m4g: domain has two wizards (one human)");
        check(swamps > 100 && woods > 20, "m4g: the estate has its regions");
        check(treasures == 0, "d35: domain treasure comes from chests");
    }

    {   /* scenario books compile and load */
        static Spellbook scnbooks[OWN_NEUTRAL];
        check(spellbook_load(scnbooks, SCN_SLAYERS_DUNGEON,
                             SCN_SLAYERS_DUNGEON_LEN) &&
              scnbooks[OWN_P2].level[SP_DWARF] == 2 && scnbooks[OWN_P2].level[SP_ZOMBIE] == 0,
              "m4g: dungeon books load");
        check(spellbook_load(scnbooks, SCN_RAGARILS_DOMAIN,
                             SCN_RAGARILS_DOMAIN_LEN) &&
              scnbooks[OWN_P2].level[SP_VAMPIRE] == 1 &&
              scnbooks[OWN_P2].level[SP_DEMON] == 1,
              "m4g: ragaril commands undead");
    }
}

static void test_m5a(void)
{
    Game g;
    Rng rng;
    uint8_t wiz, mount;

    rng_seed(&rng, 3);
    world_load_bin(&world, MAPBIN_TESTLAND, MAPBIN_TESTLAND_LEN);
    world.unit_count = 0;
    area_reset();
    game_init(&g, 6, 6, 1, 1, &rng);
    game_new_round(&g, 1);
    wiz = world_spawn_unit(&world, OWN_P1, CR_WIZARD, 5, 5);
    world_spawn_unit(&world, OWN_P2, CR_WIZARD, 9, 9);
    check(game_outcome(&g, &world, OWN_P1) == OUT_RUNNING,
          "m5a: a living wizard keeps the game running");
    check(game_outcome(&g, &world, OWN_NEUTRAL) == OUT_RUNNING,
          "m5a: independents never win or lose");

    world.units[wiz].x = 6;
    world.units[wiz].y = 6;
    g.vp[OWN_P1] = 0;
    check(game_try_enter_portal(&g, &world, wiz) &&
          game_outcome(&g, &world, OWN_P1) == OUT_WIN &&
          game_outcome(&g, &world, OWN_P2) == OUT_RUNNING,
          "m5a: escaping through the portal wins");
    check(g.vp[OWN_P1] == VP_ESCAPE && g.loot_vp[OWN_P1] == 0,
          "m5a: the escape scores VP without loot");

    world_remove_unit(&world, 0);          /* p2 wizard is the only unit */
    world.unit_count = 0;
    wiz = world_spawn_unit(&world, OWN_P1, CR_WIZARD, 5, 5);
    game_init(&g, 6, 6, 1, 1, &rng);
    world_kill_unit(&world, wiz, CR_GOBLIN, OWN_P2, true);
    check(game_outcome(&g, &world, OWN_P1) == OUT_LOSE,
          "m5a: a dead wizard loses");
    game_credit_kills(&g, &world);
    check(g.kills[OWN_P2] == 1, "m5a: the kill is counted");

    world.unit_count = 0;
    world_spawn_unit(&world, OWN_P1, CR_WIZARD, 5, 5);
    mount = world_spawn_unit(&world, OWN_P1, CR_UNICORN, 5, 6);
    world.units[0].ap = 40;
    check(ride_mount(&world, 0, 5, 6), "m5a: wizard mounts the unicorn");
    (void)mount;
    check(game_outcome(&g, &world, OWN_P1) == OUT_RUNNING &&
          !game_over(&g, &world),
          "m5a: a riding wizard still counts as alive");

    game_init(&g, -1, -1, 1, 1, &rng);
    world.unit_count = 0;
    check(game_outcome(&g, &world, OWN_P1) == OUT_RUNNING,
          "m5a: maps without a portal never end");
}

static void test_m4h(void)
{
    Rng rng;
    uint8_t guard;

    world_load_bin(&world, MAPBIN_SLAYERS_DUNGEON, MAPBIN_SLAYERS_DUNGEON_LEN);
    world.unit_count = 0;
    {   /* map guards: undead carry their spawn post (testland's zombie;
         * the campaign maps hold no fixed monsters any more, D35) */
        uint8_t i, posted = 0;
        world_load_bin(&world, MAPBIN_TESTLAND, MAPBIN_TESTLAND_LEN);
        for (i = 0; i < world.unit_count; i++)
            if (world.units[i].owner == OWN_NEUTRAL &&
                (world.units[i].flags & UF_UNDEAD) &&
                world.units[i].post_x != 0xFF)
                posted++;
        check(posted >= 1, "m4h: undead guards carry a post");
    }

    {   /* a guard chases an intruder and returns home */
        uint8_t w2 = 255;
        guard = world_spawn_unit(&world, OWN_NEUTRAL, CR_ZOMBIE, 8, 9);
        world.units[guard].flags |= UF_UNDEAD;
        ai_set_post(&world, guard);
        w2 = world_spawn_unit(&world, OWN_P1, CR_WIZARD, 10, 9);
        rng_seed(&rng, 11);
        ai_guard(&world, &rng, guard, 3);
        check(world.units[guard].x > 8 || world.units[guard].ap < 30,
              "m4h: the guard moves toward the intruder");
        /* drive him home: intruder gone and out of sight */
        if (w2 != 255)
            world_remove_unit(&world, w2);
        guard = world_find_unit(&world, world.units[0].id);  /* post unit */
        guard = 0;                       /* only the guard is left */
        world.units[guard].ap = 30;
        ai_set_post(&world, guard);
        world.units[guard].x = 12;      /* dragged away */
        world.units[guard].y = 9;
        rng_seed(&rng, 12);
        ai_guard(&world, &rng, guard, 3);
        check(world.units[guard].x < 12 || world.units[guard].ap < 30,
              "m4h: the guard walks home");
    }

    {   /* the wizard AI grabs a treasure standing on its field */
        uint8_t wiz;
        Game g;
        Spellbook books2[OWN_NEUTRAL];
        AiCtx ctx = {0};
        Turns t;
        world_load_bin(&world, MAPBIN_SLAYERS_DUNGEON, MAPBIN_SLAYERS_DUNGEON_LEN);
        world.unit_count = 0;
        area_reset();
        memset(books2, 0, sizeof books2);
        game_init(&g, -1, -1, 1, 1, &rng);
        ctx.books = books2;
        ctx.game = &g;
        memset(&t, 0, sizeof t);
        t.phase = OWN_P2;
        t.rng = rng;
        wiz = world_spawn_unit(&world, OWN_P2, CR_WIZARD, 9, 9);
        {   /* a ruby under the wizard */
            world.objects[world.object_count].x = 9;
            world.objects[world.object_count].y = 9;
            world.objects[world.object_count].tile = T_OBJ_RUBY;
            world.object_count++;
        }
        ai_wizard_phase(&t, &world, &ctx);
        check(items_kind_at(&world, 9, 9) == NO_ITEM,
              "m4h: the AI picks up the treasure on its field");
        (void)wiz;
    }
    area_reset();
}

#ifndef LOC_SELFTEST_NO_SAVE
/* D64: a full 46x46 world - wrap at the far edge, sight, a save blob that
 * fits SAVE_BUF_SIZE and parses back. */
static void test_d64(void)
{
    static uint8_t buf[SAVE_BUF_SIZE];
    static Sight sg;
    uint16_t len, i;
    int16_t x = 46, y = -1;
    memset(&sg_a, 0, sizeof sg_a);
    sg_a.world.w = sg_a.world.h = MAP_MAX_W;
    sg_a.world.wrap = 1;
    memset(sg_a.world.floor, FL_GRASS, sizeof sg_a.world.floor);
    for (i = 0; i < MAX_UNITS; i++) {           /* the fullest world there is */
        world_spawn_unit(&sg_a.world, (uint8_t)(i & 3), CR_GOBLIN,
                         (uint8_t)(45 - i), (uint8_t)(45 - i));
    }
    sg_a.world.object_count = MAX_OBJECTS;
    check(MAP_MAX_W == 46 && MAP_MAX_H == 46, "d64: maps reach 46x46");
    check(world_wrap(&sg_a.world, &x, &y) && x == 0 && y == 45,
          "d64: the far edges wrap around");
    check(world_unit_at(&sg_a.world, 45, 45, UL_GROUND) != NO_UNIT,
          "d64: a unit stands on the last field");
    sight_init(&sg, OWN_P1);
    sight_compute(&sg_a.world, &sg);
    check(sight_visible(&sg, &sg_a.world, 0, 45) && sight_visible(&sg, &sg_a.world, 45, 0),
          "d64: sight reaches the far fields of a 46x46 map");
    len = save_serialize(&sg_a, buf, sizeof buf);
    check(len > 0 && len <= SAVE_BUF_SIZE, "d64: the fullest 46x46 save fits the buffer");
    check(save_deserialize(&sg_b, buf, len) && sg_b.world.w == 46 &&
          sg_b.world.unit_count == MAX_UNITS &&
          memcmp(sg_b.world.floor, sg_a.world.floor, sizeof sg_a.world.floor) == 0,
          "d64: the 46x46 save parses back");
}

#endif

/* D64: on the 46x46 Level 1 chests and finds grow with the area. */
static void test_d64_populate(void)
{
    Rng r;
    int16_t x, y;
    uint8_t chests = 0, fixed = 0;
    world_load_bin(&world, MAPBIN_MANY_COLOURED_LAND, MAPBIN_MANY_COLOURED_LAND_LEN);
    for (y = 0; y < world.h; y++)
        for (x = 0; x < world.w; x++)
            if (world.feature[y][x] == FE_CHEST || world.feature[y][x] == FE_CHEST_FREE)
                fixed++;
    rng_seed(&r, 7);
    populate_scenario(&world, &r);
    for (y = 0; y < world.h; y++)
        for (x = 0; x < world.w; x++)
            if (world.feature[y][x] == FE_CHEST || world.feature[y][x] == FE_CHEST_FREE)
                chests++;
    check((uint8_t)(chests - fixed) >= POP_CHESTS_MIN * 46u * 46u / (36u * 36u) - 1,
          "d64: the big map gets more chests (area scaled)");
    check(world.object_count < MAX_OBJECTS - 16,
          "d64: room left in the object list for chest loot and drops");
}

#ifndef LOC_SELFTEST_NO_SAVE
static void test_m4i(void)
{
    static uint8_t buf[SAVE_BUF_SIZE];
    uint16_t len;
    uint32_t ha, hb;

    world_load_bin(&world, MAPBIN_SLAYERS_DUNGEON, MAPBIN_SLAYERS_DUNGEON_LEN);
    memset(&sg_a, 0, sizeof sg_a);
    sg_a.world = world;
    sg_a.world.units[0].x = 7;              /* distinctive state */
    sg_a.loads_left = 3;
    sg_a.game.portal_round = 21;
    sg_a.explored[3][1] = 0x5A;
    area_reset();
    area_set(&world, AREA_FIRE, 4, OWN_P1, 20, 19);
    sg_a.area_count = area_export(sg_a.areas, SAVE_AREAS);
    area_reset();
    strcpy(sg_a.world.save_map, "maps/slayers_dungeon.map");

    len = save_serialize(&sg_a, buf, sizeof buf);
    check(len > 4000, "m4i: the blob holds the whole world");
    check(save_deserialize(&sg_b, buf, len), "m4i: the blob parses back");
    ha = save_hash(&sg_a);
    hb = save_hash(&sg_b);
    check(ha == hb && ha != 0, "m4i: save -> load -> same hash");
    check(sg_b.world.units[0].x == 7 && sg_b.loads_left == 3 &&
          sg_b.game.portal_round == 21,
          "m4i: the state survives the round trip");
    check(sg_b.explored[3][1] == 0x5A && sg_b.area_count == 1 &&
          sg_b.areas[0].kind == AREA_FIRE &&
          strcmp(sg_b.world.save_map, "maps/slayers_dungeon.map") == 0,
          "m4i: explored map, areas and map name survive");
    area_import(sg_b.areas, sg_b.area_count);
    check(area_kind_at(&world, 20, 19) == AREA_FIRE,
          "m4i: imported areas burn again");
    area_reset();
    sg_a.loads_left = 0;
    check(!save_may_load(&sg_a), "m4i: no charges, no load");
    sg_a.loads_left = 1;
    check(save_may_load(&sg_a), "m4i: one charge loads");
    sg_a.loads_left = 0xFF;
    check(save_may_load(&sg_a), "m4i: unlimited loads");
    sg_a.loads_left = 3;
    check(sg_b.world.unit_count == world.unit_count &&
          sg_b.world.object_count == world.object_count,
          "m4i: units and objects survive");

    {   /* magic and version gates */
        memcpy(buf, "XXXX", 4);
        check(!save_deserialize(&sg_b, buf, len), "m4i: wrong magic refused");
        len = save_serialize(&sg_a, buf, sizeof buf);
        buf[5] = (uint8_t)(buf[5] + 1);   /* any other version */
        check(!save_deserialize(&sg_b, buf, len), "m4i: wrong version refused");
        check(!save_deserialize(&sg_b, buf, (uint16_t)(len - 1)),
              "m4i: wrong length refused");
    }
}

#endif

static void test_m4k_ai(void)
{
    Rng rng;
    uint8_t guard, wiz;

    world_load_bin(&world, MAPBIN_SLAYERS_DUNGEON, MAPBIN_SLAYERS_DUNGEON_LEN);
    world.unit_count = 0;
    area_reset();

    {   /* a guard opens his crypt door to reach an intruder */
        uint8_t w1;
        guard = world_spawn_unit(&world, OWN_NEUTRAL, CR_SPECTRE, 6, 4);   /* strong enough to engage (K10.4) */
        world.units[guard].flags |= UF_UNDEAD;
        world.feature[3][7] = FE_DOOR_CLOSED;   /* door east of the guard */
        world_map_changed(&world);
        w1 = world_spawn_unit(&world, OWN_P1, CR_WIZARD, 9, 4);
        world.units[guard].ap = 30;
        rng_seed(&rng, 5);
        ai_guard(&world, &rng, guard, 8);       /* wide range: intruder first */
        check(world.feature[3][7] == FE_DOOR_OPEN ||
              world.units[guard].x > 6,
              "m4k: the guard opens the crypt door (or came through)");
        (void)w1;
    }

    {   /* the wizard AI pries open a chest on the treasure path */
        world_load_bin(&world, MAPBIN_TESTLAND, MAPBIN_TESTLAND_LEN);
        world.unit_count = 0;
        area_reset();
        wiz = world_spawn_unit(&world, OWN_P2, CR_WIZARD, 8, 6);
        world.units[wiz].ap = 40;
        world.feature[6][9] = FE_CHEST;         /* chest east of the wizard */
        world.units[wiz].items[0] = OBJ_CHEST_KEY;   /* K10.6: chests only with the key */
        world.units[wiz].item_count = 1;
        world_map_changed(&world);
        {   /* a diamond inside the chest */
            world.objects[world.object_count].x = 9;
            world.objects[world.object_count].y = 6;
            world.objects[world.object_count].tile = T_OBJ_DIAMOND;
            world.object_count++;
        }
        {   /* force the treasure walk: no enemies, chest on the way */
            Game g;
            Spellbook books2[OWN_NEUTRAL];
            AiCtx ctx = {0};
            Turns t;
            memset(books2, 0, sizeof books2);
            game_init(&g, -1, -1, 1, 1, &rng);
            ctx.books = books2;
            ctx.game = &g;
            memset(&t, 0, sizeof t);
            t.phase = OWN_P2;
            t.rng = rng;
            /* nearest_treasure needs line of sight: the wizard looks at
             * the chest field; the diamond lies under the chest */
            ai_wizard_phase(&t, &world, &ctx);
            check(world.feature[6][9] == FE_NONE &&
                  (world.unit_count >= 1),
                  "m4k: the AI opened the chest on its path");
        }
    }
    area_reset();
}

/* ---------- M5b: guided tutorial + lexicon ---------- */

static void test_m5b_tutorial(void)
{
    Tutorial t;
    Game g;
    Rng rng;
    uint8_t i, gob = NO_UNIT;

    check(world_load_bin(&world, MAPBIN_TUTORIAL, MAPBIN_TUTORIAL_LEN),
          "m5b: tutorial map loads");
    rng_seed(&rng, 7);
    game_init(&g, world.portal_x, world.portal_y, world.portal_rmin,
              world.portal_rmax, &rng);
    game_new_round(&g, 1);
    check(g.portal_round == 2, "m5b: portal opens on round 2");
    tutorial_init(&t, &world);
    check(t.step == TUT_MOVE, "m5b: tutorial starts at the move step");
    check(t.wiz_x == 2 && t.wiz_y == 2, "m5b: wizard start found");
    check(t.key_x == 3 && t.key_y == 2, "m5b: chest key found");
    check(t.chest_x == 5 && t.chest_y == 2, "m5b: chest found");
    check(t.enemies == 1, "m5b: one enemy unit (the goblin)");
    check(tutorial_update(&t, &world, &g) == TUT_MOVE,
          "m5b: nothing advances without action");
    check(!tutorial_finished(&t), "m5b: not finished at the start");
    check(world_move_unit(&world, 0, 1, 0), "m5b: wizard steps east");
    check(tutorial_update(&t, &world, &g) == TUT_SWITCH,
          "m5b: move step completes");
    tutorial_notify(&t, TUT_SWITCH);
    check(tutorial_update(&t, &world, &g) == TUT_PICKUP,
          "m5b: switch notification completes the switch step");
    world.objects[0].x = 200;             /* key off the field = picked up */
    check(tutorial_update(&t, &world, &g) == TUT_CHEST,
          "m5b: pickup completes");
    world.feature[2][5] = FE_NONE;        /* chest opened */
    world_map_changed(&world);
    check(tutorial_update(&t, &world, &g) == TUT_KILL,
          "m5b: chest step completes");
    for (i = 0; i < world.unit_count; i++)
        if (world.units[i].owner == OWN_P2)
            gob = i;
    check(gob != NO_UNIT, "m5b: the goblin exists");
    tutorial_notify(&t, TUT_SPELL);       /* cast during the fight: sticky */
    world_kill_unit(&world, gob, CR_WIZARD, OWN_P1, true);
    check(tutorial_update(&t, &world, &g) == TUT_PORTAL,
          "m5b: kill and spell steps complete (spell was sticky)");
    check(!tutorial_finished(&t), "m5b: portal step still open");
    game_new_round(&g, 2);                /* portal opens */
    check(game_try_enter_portal(&g, &world, 0) ||
          (world.units[0].x != 2 || world.units[0].y != 2),
          "m5b: wizard escapes or is on the way");
    g.escaped |= (uint8_t)(1u << OWN_P1); /* outcome says win either way */
    check(tutorial_update(&t, &world, &g) == TUT_DONE,
          "m5b: portal entry finishes the tutorial");
    check(tutorial_finished(&t), "m5b: tutorial finished");
}

static void test_m5b_lexicon(void)
{
    Lexicon l, l2;
    uint8_t buf[17];
    uint16_t len;

    lexicon_init(&l);
    check(lexicon_seen_count(&l) == 0, "m5b: lexicon starts empty");
    check(!lexicon_seen_creature(&l, CR_GOBLIN) &&
          !lexicon_seen_object(&l, OBJ_SWORD), "m5b: nothing seen at first");
    lexicon_see_creature(&l, CR_GOBLIN);
    lexicon_see_creature(&l, CR_DEMON);
    lexicon_see_object(&l, OBJ_SWORD);
    lexicon_see_object(&l, OBJ_DRAGON_HERB);
    check(lexicon_seen_creature(&l, CR_GOBLIN) &&
          !lexicon_seen_creature(&l, CR_WIZARD), "m5b: creature bits set");
    check(lexicon_seen_object(&l, OBJ_DRAGON_HERB) &&
          !lexicon_seen_object(&l, OBJ_GOLD), "m5b: object bits set");
    check(lexicon_seen_count(&l) == 4, "m5b: seen count");
    len = lexicon_export(&l, buf, sizeof buf);
    check(len == 17, "m5b: export is 17 bytes");
    lexicon_init(&l2);
    check(lexicon_import(&l2, buf, len) && lexicon_seen_count(&l2) == 4,
          "m5b: round trip keeps the entries");
    check(lexicon_seen_creature(&l2, CR_DEMON) &&
          lexicon_seen_object(&l2, OBJ_SWORD), "m5b: round trip bits");
    buf[0] = 'X';
    check(!lexicon_import(&l2, buf, len), "m5b: bad magic rejected");
    check(!lexicon_import(&l2, buf, 5), "m5b: wrong length rejected");
    {   /* bits beyond the tables must not survive an import */
        uint16_t i;
        for (i = 5; i < 17; i++)
            buf[i] = 0xFF;
        buf[0] = 'L'; buf[1] = 'O'; buf[2] = 'C'; buf[3] = 'L'; buf[4] = 1;
        check(lexicon_import(&l2, buf, sizeof buf) &&
              lexicon_seen_count(&l2) == CR_COUNT + OBJ_COUNT,
              "m5b: out-of-range bits dropped on import");
    }

    check(lexicon_object_kind_of_tile(OBJECTS[OBJ_RUBY].tile) == OBJ_RUBY,
          "m5b: field tile maps to the object kind");

    {   /* watch: own units always, others and objects only in sight */
        Sight s;
        Lexicon lw;
        load_house();
        sight_init(&s, OWN_P1);
        sight_compute(&world, &s);
        lexicon_init(&lw);
        lexicon_watch(&lw, &world, &s);
        check(lexicon_seen_creature(&lw, CR_WIZARD),
              "m5b: own units count as seen");
        check(lexicon_seen_object(&lw, OBJ_SCROLL),
              "m5b: the scroll on the visible field is seen");
        check(lexicon_seen_creature(&lw, CR_GOBLIN) ==
              sight_visible(&s, &world, 8, 3),
              "m5b: the goblin follows the sight rules");
    }
}

/* ---------- M5c: presentation events ---------- */

static void test_m5c_events(void)
{
    static GameEvent ev[EVENT_RING];
    uint8_t n, i;
    Rng rng;
    CombatResult r;

    events_reset();
    check(events_drain(ev, EVENT_RING) == 0, "m5c: ring starts empty");
    for (i = 0; i < 20; i++)
        events_push(EV_HIT, 1, 2, 3, 4, 5, 6);
    check(events_dropped() == 4, "m5c: full ring drops the new events");
    n = events_drain(ev, EVENT_RING);
    check(n == EVENT_RING, "m5c: drain returns the ring contents");
    check(ev[0].a == 5 && ev[0].x == 1 && ev[0].owner == 4,
          "m5c: order and payload kept");
    check(events_drain(ev, EVENT_RING) == 0, "m5c: drain clears the ring");

    /* a melee exchange: swing first, then hit or miss (and the reply) */
    events_reset();
    load_house();
    world.units[1].owner = OWN_P2;       /* the goblin turns hostile */
    world.units[1].x = 4;                /* next to the wizard (3,4) */
    world.units[1].y = 4;
    rng_seed(&rng, 3);
    check(combat_melee(&world, &rng, 0, 1, &r), "m5c: melee runs");
    n = events_drain(ev, EVENT_RING);
    check(n >= 2, "m5c: melee emits at least swing and result");
    check(ev[0].type == EV_SWING && ev[0].x == 4 && ev[0].y == 4 &&
          ev[0].kind == CR_WIZARD,
          "m5c: the swing names attacker and target field");
    check(ev[1].type == EV_HIT || ev[1].type == EV_MISS,
          "m5c: hit or miss follows the swing");
    if (ev[1].type == EV_HIT && !r.died)
        check(ev[1].a == r.damage, "m5c: the hit carries the damage");

    /* kills report where and what died */
    events_reset();
    world_kill_unit(&world, 1, CR_WIZARD, OWN_P1, true);
    n = events_drain(ev, EVENT_RING);
    check(n == 1 && ev[0].type == EV_DEATH && ev[0].kind == CR_GOBLIN,
          "m5c: a kill emits one death event");

    /* terrain attacks swing (b=1) and may smash */
    events_reset();
    load_house();
    rng_seed(&rng, 9);
    world.units[0].com = 60;
    {
        bool destroyed = false;
        (void)combat_terrain(&world, &rng, 0, 1, 6, &destroyed);  /* table */
        n = events_drain(ev, EVENT_RING);
        check(n >= 1 && ev[0].type == EV_SWING && ev[0].b == 1,
              "m5c: terrain attack swings with the terrain flag");
        if (n > 1)
            check(ev[1].type == EV_SMASH && ev[1].x == 1 && ev[1].y == 6,
                  "m5c: destruction emits a smash event");
        else
            check(!destroyed, "m5c: no smash event without destruction");
    }

    /* spells report id and target; the bolt itself hits or misses */
    events_reset();
    load_house();
    world.units[1].owner = OWN_P2;       /* goblin inside, clear line */
    world.units[1].x = 4;
    world.units[1].y = 3;
    {
        Spellbook b;
        SpellShot shot;
        Rng r2;
        memset(&b, 0, sizeof b);
        b.level[SP_MAGIC_BOLT] = 1;
        rng_seed(&r2, 11);
        check(spell_bolt(&world, &b, 0, SP_MAGIC_BOLT, 4, 3, false, &r2, &shot),
              "m5c: bolt cast at the goblin");
        n = events_drain(ev, EVENT_RING);
        check(n >= 1 && ev[0].type == EV_SPELL && ev[0].kind == SP_MAGIC_BOLT,
              "m5c: the cast emits a spell event with the id");
        check(ev[0].x == 4 && ev[0].y == 3, "m5c: the spell names its target");
        check(n >= 2 && ev[1].type == EV_PROJECTILE && ev[1].kind == PJ_BOLT &&
              ev[1].x == world.units[0].x && ev[1].y == world.units[0].y &&
              (int8_t)ev[1].a == 4 - world.units[0].x &&
              (int8_t)ev[1].b == 3 - world.units[0].y,
              "polish: the bolt flies from the caster to the target");
        if (n > 2)
            check(ev[2].type == EV_HIT || ev[2].type == EV_MISS,
                  "m5c: the bolt connects or whiffs");
    }

    /* bleeding out reports a death with the bleed flag */
    events_reset();
    load_house();
    world_set_wounds(&world.units[0], 1);
    world.units[0].con = 1;
    world_new_turn(&world);
    n = events_drain(ev, EVENT_RING);
    check(n == 1 && ev[0].type == EV_DEATH && ev[0].a == 1,
          "m5c: bleeding out emits a death event (a=1)");

    /* emitting must not touch the RNG: two identical runs agree */
    {
        CombatResult r1, r2;
        events_reset();
        load_house();
        world.units[1].owner = OWN_P2;
        world.units[1].x = 4;
        world.units[1].y = 4;
        rng_seed(&rng, 42);
        combat_melee(&world, &rng, 0, 1, &r1);
        events_drain(ev, EVENT_RING);     /* the drain must not matter */
        load_house();
        world.units[1].owner = OWN_P2;
        world.units[1].x = 4;
        world.units[1].y = 4;
        rng_seed(&rng, 42);
        combat_melee(&world, &rng, 0, 1, &r2);
        check(r1.hit == r2.hit && r1.damage == r2.damage &&
              r1.returned == r2.returned && r1.return_hit == r2.return_hit,
              "m5c: events do not change the dice");
    }
}

/* ---------- M5e: combat rebalance (D27 free counter, D28 dice) ---------- */

static void test_c1_c2(void)
{
    Rng rng;
    bool destroyed = false;
    uint8_t objects_before;

    /* C2: doors close, lock and unlock */
    world_load_bin(&world, MAPBIN_TESTLAND, MAPBIN_TESTLAND_LEN);
    world.unit_count = 2;
    world.units[0].ap = 40;
    world.feature[5][6] = FE_DOOR_OPEN;
    world_map_changed(&world);
    check(world_close_door(&world, 0, 6, 5) &&
          world.feature[5][6] == FE_DOOR_CLOSED &&
          world.units[0].ap == 40 - ACTIONS[ACT_OPEN_DOOR].ap,
          "c2: an open door closes for the door cost");
    check(!world_lock_door(&world, 0, 6, 5),
          "c2: locking needs a key");
    world.units[0].items[0] = OBJ_CHEST_KEY;
    world.units[0].item_count = 1;
    check(world_lock_door(&world, 0, 6, 5) &&
          world.feature[5][6] == FE_DOOR_LOCKED &&
          world.units[0].item_count == 1,
          "c2: the key locks the door and is kept");
    check(world_bump_kind(&world, 0, 0, -1) == BUMP_TERRAIN &&
          world_blocks_sight(&world, 6, 5),
          "c2: a locked door blocks walking and sight");
    check(!world_open_door(&world, 0, 6, 5),
          "c2: bumping does not open a locked door");
    check(world_unlock_door(&world, 0, 6, 5) &&
          world.feature[5][6] == FE_DOOR_CLOSED,
          "c2: the key unlocks it again");
    world.feature[5][6] = FE_DOOR_OPEN;
    world.units[1].x = 6;
    world.units[1].y = 5;
    check(!world_close_door(&world, 0, 6, 5),
          "c2: nobody can close a door with someone in the doorway");

    /* C2: an enemy without a key has to smash the locked door */
    world.units[1].x = 20;
    world.units[1].y = 20;
    world.feature[5][6] = FE_DOOR_LOCKED;
    world_map_changed(&world);
    world.units[0].item_count = 0;
    world.units[0].ap = 62;
    world.units[0].com = 70;
    rng_seed(&rng, 5);
    {
        uint8_t n;
        for (n = 0; n < 12 && world.feature[5][6] == FE_DOOR_LOCKED; n++) {
            world.units[0].ap = 62;
            world.units[0].sta = 100;
            combat_terrain(&world, &rng, 0, 6, 5, &destroyed);
        }
    }
    check(world.feature[5][6] == FE_NONE && destroyed,
          "c2: a locked door breaks under repeated blows");

    /* C1: free chest opens without a key at the plain cost */
    world.units[0].x = 4;
    world.units[0].y = 8;
    world.units[0].ap = 40;
    world.feature[8][3] = FE_CHEST_FREE;
    world_map_changed(&world);
    objects_before = world.object_count;
    rng_seed(&rng, 9);
    check(items_open_chest(&world, &rng, 0, 3, 8) &&
          world.feature[8][3] == FE_NONE &&
          world.object_count == objects_before + 1 &&
          world.units[0].ap == 40 - ACTIONS[ACT_OPEN_CHEST].ap,
          "c1: a free chest opens at single AP, no key");
}

static void test_c5_drowning(void)
{
    uint8_t n, sta_before;
    world_load_bin(&world, MAPBIN_TESTLAND, MAPBIN_TESTLAND_LEN);
    world.unit_count = 0;
    world_spawn_unit(&world, OWN_P1, CR_GOBLIN, 20, 19);
    world.floor[19][20] = FL_WATER;
    world_spawn_unit(&world, OWN_P1, CR_GOBLIN, 22, 19);   /* dry land */
    world.floor[19][22] = FL_STONE;
    sta_before = world.units[0].sta;
    world_new_turn(&world);
    check(world.units[0].sta < sta_before && world.units[1].sta == world.units[1].sta_max,
          "c5: a round in deep water costs stamina, dry land does not");
    for (n = 0; n < 20 && world.unit_count == 2; n++)
        world_new_turn(&world);
    check(world.unit_count == 1 && world.units[0].x == 22,
          "c5: the swimmer drowns once stamina is gone");
    {   /* C4: a figure in deep water is flagged to be drawn waist-deep */
        FieldLayers f;
        world.unit_count = 0;
        world_spawn_unit(&world, OWN_P1, CR_GOBLIN, 20, 19);
        view_compose(&world, 20, 19, &f);
        check(f.wade != 0, "c4: a wader is drawn lower");
        world.floor[19][20] = FL_STONE;
        world_map_changed(&world);
        view_compose(&world, 20, 19, &f);
        check(f.wade == 0, "c4: on dry land the figure stands normally");
        world.floor[19][20] = FL_WATER;
        world_map_changed(&world);
    }
    world.unit_count = 0;
    world_spawn_unit(&world, OWN_P1, CR_GOBLIN, 20, 19);
    world.units[0].flags |= UF_FLYING;
    for (n = 0; n < 10; n++)
        world_new_turn(&world);
    check(world.unit_count == 1 && world.units[0].con == world.units[0].con_max,
          "c5: a flier over water does not tire");
}

static uint8_t idle_tick(uint8_t id, uint8_t slot, uint8_t other_id)
{
    uint8_t t;
    for (t = 0; t < 16; t++)   /* unit `id` at `slot`, the other one resting */
        if (((t + id * 3) & 15) == slot && ((t + other_id * 3) & 15) >= 4)
            return t;
    return 0;
}

/* D53: creature idle animation - drawn frames, the lift of the others, and
 * the staggered schedule. */
/* D69-D71 (2026-10-08): noises, remains, the AP factor of a scenario. */
static void test_d69_d71(void)
{
    static AiProfile prof[OWN_NEUTRAL];
    CombatResult r;
    Rng rng;
    uint8_t wz, gob, k;
    bool fought = false;

    /* D71: the scenario's AP factor scales placed and later units */
    world_load_bin(&world, MAPBIN_TESTLAND, MAPBIN_TESTLAND_LEN);
    world.unit_count = 0;
    wz = world_spawn_unit(&world, OWN_P1, CR_WIZARD, 5, 13);
    check(world.units[wz].ap_max == 40, "d71: no factor, the table's AP");
    world_set_ap_scale(&world, 128);
    gob = world_spawn_unit(&world, OWN_P2, CR_GIANT_BAT, 6, 13);
    check(world.units[wz].ap_max == 51 && world.units[gob].ap_max == 30 &&
          world.units[gob].ap_fly == 79,
          "d71: x1.28 for the wizard there and the bat spawned after");
    world_load_bin(&world, MAPBIN_MANY_COLOURED_LAND, MAPBIN_MANY_COLOURED_LAND_LEN);
    area_reset();
    check(ai_scenario_load(&world, prof, SCN_MANY_COLOURED_LAND,
                           SCN_MANY_COLOURED_LAND_LEN) && world.ap_scale == 128,
          "d71: level 1 carries the factor 128 (46/36)");

    /* D69: a fight is heard; clearing starts the next listening period */
    world_load_bin(&world, MAPBIN_TESTLAND, MAPBIN_TESTLAND_LEN);
    world.unit_count = 0;
    wz = world_spawn_unit(&world, OWN_P2, CR_TROLL, 5, 13);
    gob = world_spawn_unit(&world, OWN_NEUTRAL, CR_GOBLIN, 6, 13);
    check(world.noise_n == 0, "d69: a fresh map is quiet");
    rng_seed(&rng, 5);
    world.units[wz].ap = 40;
    world.units[wz].sta = 40;
    if (combat_melee(&world, &rng, wz, gob, &r))
        fought = true;
    check(fought && world.noise_n >= 1 && world.noises[0].kind == NOISE_FIGHT &&
          world.noises[0].x == 6 && world.noises[0].owner == OWN_P2,
          "d69: melee leaves a fight noise where it happened");
    world_noise_clear(&world);
    check(world.noise_n == 0, "d69: cleared for the next period");

    /* D70: a death leaves remains that name the creature, and a noise */
    world.unit_count = 0;
    gob = world_spawn_unit(&world, OWN_NEUTRAL, CR_GOBLIN, 9, 14);
    combat_damage(&world, gob, 255, CR_TROLL, OWN_P2, true, NULL);
    {
        const Remains *rm = world_remains_at(&world, 9, 14);
        check(world.unit_count == 0 && rm && rm->kind == CR_GOBLIN &&
              rm->owner == OWN_NEUTRAL && !world_remains_at(&world, 9, 15),
              "d70: the dead goblin leaves a skeleton on its field");
    }
    check(world.noise_n == 1 && world.noises[0].kind == NOISE_DEATH,
          "d69: a death is heard too");
    for (k = 0; k < REMAINS_MAX + 2; k++)
        world_add_remains(&world, k, 0, CR_ZOMBIE, OWN_NEUTRAL);
    check(world.remains_n == REMAINS_MAX && !world_remains_at(&world, 9, 14) &&
          world_remains_at(&world, REMAINS_MAX + 1, 0),
          "d70: a ring - the oldest skeletons go first");
}

static void test_idle(void)
{
    FieldLayers f;
    uint8_t g, p, gid, pid, t, tr;
    world_load_bin(&world, MAPBIN_TESTLAND, MAPBIN_TESTLAND_LEN);
    world.unit_count = 0;
    g = world_spawn_unit(&world, OWN_P1, CR_GOBLIN, 20, 19);
    p = world_spawn_unit(&world, OWN_P1, CR_PIXIE, 21, 19);
    gid = world.units[g].id;
    pid = world.units[p].id;
    view_set_origin(18, 17);          /* goblin at field 2,2, pixie at 3,2 */
    view_set_phase(0);
    view_set_idle(false);
    view_invalidate();
    view_update(&world);
    view_clean();
    check(view_bob(2, 2) == 0, "d53: idle off: no lift");
    view_set_idle(true);
    tr = (uint8_t)(idle_tick(gid, 5, pid) + 8);
    view_animate(tr);
    view_clean();
    view_compose(&world, 20, 19, &f);
    check(f.id[f.n - 1] == CREATURE_TILE[CR_GOBLIN] + OWN_P1 && view_bob(2, 2) == 0,
          "d53: resting goblin: base tile, no lift");
    t = idle_tick(gid, 2, pid);
    view_animate(t);
    check(view_dirty(2, 2) && (view_bob(2, 2) >> 4) == 2 && (view_bob(2, 2) & 15) >= 1,
          "d53: idle goblin rises 2 pixels");
    check(view_dirty(2, 1), "d53: the field above repaints (the lift overhangs)");
    check(fast_equals_reference(), "d53: idle frame equals reference");
    view_clean();
    view_animate(tr);
    check(view_bob(2, 2) == 0 && view_dirty(2, 2) && view_dirty(2, 1),
          "d53: back at rest: lift gone, the stale overhang is repainted");
    view_clean();
    t = idle_tick(pid, 1, gid);
    view_animate(t);
    view_compose(&world, 21, 19, &f);
    check(f.id[f.n - 1] == CREATURE_FRAME[CR_PIXIE][0] + OWN_P1 && view_dirty(3, 2) &&
          view_bob(3, 2) == 0 && fast_equals_reference(),
          "d53: pixie shows drawn frame 1 (no lift)");
    view_clean();
    t = (uint8_t)((3 - pid * 3) & 15);   /* the goblin may idle too: only the pixie is checked */
    view_animate(t);
    view_compose(&world, 21, 19, &f);
    check(f.id[f.n - 1] == CREATURE_FRAME[CR_PIXIE][1] + OWN_P1 && fast_equals_reference(),
          "d53: pixie shows drawn frame 2");
    view_clean();
    t = idle_tick(pid, 2, gid);
    view_animate(t);
    view_compose(&world, 21, 19, &f);
    check(f.id[f.n - 1] == CREATURE_TILE[CR_PIXIE] + OWN_P1,
          "d53: pixie back on its base tile between flaps");
    view_set_idle(false);
    view_animate(0);
    view_set_origin(0, 0);
    load_house();
}

static void test_c3_overlap(void)
{
    FieldLayers f;
    uint8_t a, b;
    world_load_bin(&world, MAPBIN_TESTLAND, MAPBIN_TESTLAND_LEN);
    world.unit_count = 0;
    a = world_spawn_unit(&world, OWN_P1, CR_GOBLIN, 20, 19);
    b = world_spawn_unit(&world, OWN_P1, CR_GOBLIN, 21, 19);
    world.units[a].ap = world.units[b].ap = 40;
    check(world_bump_kind(&world, a, 1, 0) == BUMP_OK &&
          world_move_unit(&world, a, 1, 0) &&
          world.units[a].x == 21 && world.units[b].x == 21,
          "c3: own units may share a field");
    world_spawn_unit(&world, OWN_P2, CR_GOBLIN, 23, 19);
    world.units[2].ap = 40;
    world.units[b].x = 22;
    check(world_bump_kind(&world, b, 1, 0) == BUMP_UNIT &&
          !world_move_unit(&world, b, 1, 0),
          "c3: an enemy on the field still blocks");
    check(world_bump_kind(&world, 2, -1, 0) == BUMP_UNIT &&
          !world_move_unit(&world, 2, -1, 0),
          "c3: nobody else may step onto own units");
    world.unit_count = 0;
    world_spawn_unit(&world, OWN_NEUTRAL, CR_GOBLIN, 20, 19);
    world_spawn_unit(&world, OWN_NEUTRAL, CR_GOBLIN, 21, 19);
    check(!world_move_unit(&world, 0, 1, 0),
          "c3: wild creatures do not stack");
    /* the active unit is the one drawn */
    world.unit_count = 0;
    a = world_spawn_unit(&world, OWN_P1, CR_GOBLIN, 20, 19);
    b = world_spawn_unit(&world, OWN_P1, CR_TROLL, 20, 19);
    view_set_active_unit(world.units[b].id);
    view_compose(&world, 20, 19, &f);
    check(f.id[f.n - 1] == CREATURE_TILE[CR_TROLL] + OWN_P1,
          "c3: the active unit is drawn on top of the stack");
    view_set_active_unit(world.units[a].id);
    view_compose(&world, 20, 19, &f);
    check(f.id[f.n - 1] == CREATURE_TILE[CR_GOBLIN] + OWN_P1,
          "c3: and switches with the active unit");
    view_set_active_unit(NO_UNIT);
}

static void test_m5e_balance(void)
{
    Rng rng;
    CombatResult r;

    {   /* D27: the counter is free - even an exhausted unit strikes back
         * and enters its own turn unharmed */
        load_house();
        world.units[1].owner = OWN_P2;
        world.units[1].x = 4;
        world.units[1].y = 4;
        world.units[1].ap = 0;
        world.units[1].sta = 0;
        world.units[0].con = 30;
        world.units[1].con = 32;
        rng_seed(&rng, 5);
        world.units[0].ap = 40;
        world.units[0].sta = 60;
        check(combat_melee(&world, &rng, 0, 1, &r) && !r.returned,
              "m5e: exhausted defenders cannot answer (K6.2)");
        check(world.units[1].ap == 0 && world.units[1].sta == 0,
              "m5e: and pay nothing");
    }

    {   /* D28: the shield never takes the hand */
        load_house();
        world.units[0].ap = 40;
        world.units[0].items[0] = OBJ_SWORD;
        world.units[0].items[1] = OBJ_SHIELD;
        world.units[0].item_count = 2;
        world.units[0].in_use = NO_ITEM;
        check(items_cycle(&world, 0) && world.units[0].in_use == 0,
              "m5e: cycle from bare hands wields the sword");
        check(items_cycle(&world, 0) && world.units[0].in_use == NO_ITEM,
              "m5e: next stop is bare hands again");
        check(!items_cycle(&world, 0) || world.units[0].in_use != 1,
              "m5e: the shield is never wielded (D28/D21)");
        world.units[0].items[0] = OBJ_SHIELD;   /* shield only */
        world.units[0].item_count = 1;
        world.units[0].in_use = NO_ITEM;
        check(!items_cycle(&world, 0),
              "m5e: nothing to wield but a shield");
        check(items_defence(&world, 0) == 12 + WEAPONS[WEAPON_SHIELD].defence,
              "m5e: the carried shield still defends");
    }

    {   /* D77: item bonuses for the bars, and wielding a chosen slot */
        load_house();
        world.units[0].ap = 40;
        world.units[0].items[0] = OBJ_SWORD;
        world.units[0].items[1] = OBJ_SHIELD;
        world.units[0].item_count = 2;
        world.units[0].in_use = NO_ITEM;
        check(items_combat_bonus(&world.units[0]) == 0 &&
              items_defence_bonus(&world.units[0]) == WEAPONS[WEAPON_SHIELD].defence,
              "d77: nothing in hand: no combat bonus, the carried shield defends");
        check(items_wield(&world, 0, 0) && world.units[0].in_use == 0 &&
              world.units[0].ap == 36,
              "d77: wielding the chosen slot costs the change AP");
        check(items_combat_bonus(&world.units[0]) == WEAPONS[WEAPON_SWORD].combat,
              "d77: the sword in hand shows its Combat bonus");
        check(!items_wield(&world, 0, 0) && world.units[0].ap == 36,
              "d77: already in hand: no cost");
        check(!items_wield(&world, 0, 1) && !items_wield(&world, 0, 2),
              "d77: the shield and empty slots are refused");
        check(items_wield(&world, 0, NO_ITEM) && world.units[0].in_use == NO_ITEM &&
              items_combat_bonus(&world.units[0]) == 0,
              "d77: bare hands again");
        world.units[0].ap = 0;
        check(!items_wield(&world, 0, 0), "d77: no wielding without AP");
    }

    {   /* D29: bolt scales with the book level - level 1 wounds, level 8
         * usually kills a goblin (con 32) outright */
        uint8_t k, kills1 = 0, kills8 = 0, hits1 = 0, hits8 = 0;
        uint32_t dmg1 = 0;
        for (k = 0; k < 100; k++) {
            Spellbook b;
            SpellShot shot;
            load_house();                   /* fresh wizard and goblin */
            world.units[1].owner = OWN_P2;
            world.units[1].x = 4;
            world.units[1].y = 3;
            memset(&b, 0, sizeof b);
            b.level[SP_MAGIC_BOLT] = 1;     /* 4d6 */
            rng_seed(&rng, 900 + k);
            spell_bolt(&world, &b, 0, SP_MAGIC_BOLT, 4, 3, false, &rng, &shot);
            if (shot.hit) {
                hits1++;
                dmg1 += shot.damage;
                if (shot.died)
                    kills1++;
            }
            load_house();
            world.units[1].owner = OWN_P2;
            world.units[1].x = 4;
            world.units[1].y = 3;
            memset(&b, 0, sizeof b);
            b.level[SP_MAGIC_BOLT] = 8;     /* 11d6 */
            rng_seed(&rng, 900 + k);
            spell_bolt(&world, &b, 0, SP_MAGIC_BOLT, 4, 3, false, &rng, &shot);
            if (shot.hit) {
                hits8++;
                if (shot.died)
                    kills8++;
            }
        }
        check(hits1 >= 70, "m5e: the bolt connects over many seeds");
        check(kills1 < hits1 && kills1 * 10 >= hits1 * 2,
              "m5e: a level-1 bolt (A 29) kills a goblin on some hits, not all");
        {
            uint8_t avg = (uint8_t)(dmg1 / (hits1 ? hits1 : 1));
            check(avg >= 15 && avg <= 32,
                  "m5e: a level-1 bolt averages about 25 damage");
        }
        check(hits8 >= 85 && kills8 * 10 > hits8 * 5,
              "m5e: a level-8 bolt kills the goblin on most hits");
    }

    {   /* K3.3: spell shop and mana with XP */
        Wizard t;
        wizard_slot_reset(3);
        t = wizard_slots[3];
        t.xp = 100;
        check(wizard_spell_next_cost(&t, SP_HARPY) == 11 &&
              wizard_spell_next_cost(&t, SP_MAGIC_BOLT) == 6 &&
              wizard_spell_next_cost(&t, SP_GOLD_DRAGON) == 47 &&
              wizard_spell_next_cost(&t, SP_BOMB_POTION) == 0,
              "m5f: the first level costs xp_base, the bomb is not for sale");
        t.book.level[SP_MAGIC_BOLT] = 6;
        check(wizard_spell_next_cost(&t, SP_MAGIC_BOLT) == 6 + 3 * 6,
              "m5f: level L to L+1 costs base + step x L");
        check(wizard_spell_raise(&t, SP_HARPY) && t.book.level[SP_HARPY] == 1 &&
              t.xp == 89,
              "m5f: the first level costs the base price");
        check(wizard_spell_next_cost(&t, SP_HARPY) == 16 &&
              wizard_spell_raise(&t, SP_HARPY) && t.xp == 73,
              "m5f: every further level costs 5 more (11 + 5 L)");
        t.xp = 0;
        check(!wizard_spell_raise(&t, SP_HARPY), "m5f: no buying without XP");
        t.book.level[SP_HARPY] = 8;
        t.xp = 500;
        check(!wizard_spell_raise(&t, SP_HARPY), "m5f: level 8 is the cap");
        t.xp = 0;
        t.book.level[SP_HARPY] = 2;
        check(wizard_spell_lower(&t, SP_HARPY) && t.xp == 16 &&
              wizard_spell_lower(&t, SP_HARPY) && t.xp == 27,
              "m5f: lowering refunds exactly what the level cost");
        check(!wizard_spell_lower(&t, SP_TELEPORT),
              "m5f: nothing bought - lowering is refused");
        check(wizard_mana_cost(&t) == 8 && t.mana_max == 80 && t.ap == 34,
              "m5f: mana starts at 80, AP at 34 (F6)");
        t.xp = 8;
        check(wizard_mana_raise(&t) && t.mana_max == 81 && t.xp == 0 &&
              wizard_mana_lower(&t) && t.mana_max == 80 && t.xp == 8,
              "m5f: mana raises and refunds with 8 XP");
        t.xp = 7;
        check(!wizard_mana_raise(&t), "m5f: one mana point costs 80 / 10 = 8");
        t.xp = 7;
        check(!wizard_ap_raise(&t), "m5f: no AP without 8 XP");
        t.xp = 8;
        check(wizard_ap_raise(&t) && t.ap == 35 && t.xp == 0 &&
              wizard_ap_lower(&t) && t.ap == 34 && t.xp == 8,
              "m5f: AP raises and refunds with exactly 8");
    }

    {   /* K5.3: bolts roll against Defence like a blow - the shield counts */
        uint16_t k;
        uint8_t hits_shield = 0, hits_bare = 0, pass;
        load_house();
        world.units[1].items[0] = OBJ_SHIELD;
        world.units[1].item_count = 1;
        check(items_defence(&world, 1) ==
              CREATURES[CR_GOBLIN].defence + WEAPONS[WEAPON_SHIELD].defence &&
              items_magic_res(&world, 1) == CREATURES[CR_GOBLIN].magic_res,
              "m5e: shield counts in Defence; resistance is the creature's own");
        for (pass = 0; pass < 2; pass++)
            for (k = 0; k < 200; k++) {
                Spellbook b;
                SpellShot shot;
                load_house();
                world.units[1].owner = OWN_P2;
                world.units[1].x = 4;
                world.units[1].y = 3;
                world.units[1].con = world.units[1].con_max = 250;
                if (pass == 0) {
                    world.units[1].items[0] = OBJ_SHIELD;
                    world.units[1].item_count = 1;
                }
                memset(&b, 0, sizeof b);
                b.level[SP_MAGIC_BOLT] = 1;          /* A = 29 */
                rng_seed(&rng, 4000 + k);
                spell_bolt(&world, &b, 0, SP_MAGIC_BOLT, 4, 3, false, &rng, &shot);
                if (shot.hit) {
                    if (pass == 0) hits_shield++; else hits_bare++;
                }
            }
        /* A 29: 60 rolls; Defence 22 -> 62 %, Defence 9 -> 83 % */
        check(hits_shield >= 105 && hits_shield <= 145,
              "m5e: a shielded goblin is hit in about 62 % of the bolts");
        check(hits_bare >= 150 && hits_bare <= 180 && hits_bare > hits_shield,
              "m5e: without the shield about 83 %");
    }

    {   /* K6.2: the sword in hand adds 10 Combat: a goblin takes more, more often */
        uint16_t k, bare = 0, armed = 0;
        CombatResult cr;
        for (k = 0; k < 300; k++) {
            uint8_t pass;
            for (pass = 0; pass < 2; pass++) {
                load_house();
                world.units[0].items[0] = OBJ_SWORD;
                world.units[0].item_count = 1;
                world.units[0].in_use = pass ? 0 : NO_ITEM;
                world.units[1].owner = OWN_P2;
                world.units[1].x = 4;
                world.units[1].y = 4;
                world.units[1].con = world.units[1].con_max = 250;
                world.units[0].ap = 40;
                world.units[0].sta = 100;
                rng_seed(&rng, 5000 + k);
                if (combat_melee(&world, &rng, 0, 1, &cr) && cr.hit) {
                    if (pass)
                        armed = (uint16_t)(armed + cr.damage);
                    else
                        bare = (uint16_t)(bare + cr.damage);
                }
            }
        }
        check(armed > bare * 2, "m5e: a sword more than doubles the damage dealt");
    }
}

uint16_t core_selftest(selftest_log_fn log)
{
    out = log;
    fails = 0;
    test_rng();
    test_world();
    test_view();
    test_dirty_and_move();
    test_ap();
    test_chord();
    test_stats_and_names();
    test_data();
    test_terrain();
    test_creatures();
    test_turn();
    test_sight();
    test_flight();
    test_0b();
    test_0d();
    test_0e();
    test_0g();
    test_0h();
    test_ki1();
    test_ki2();
    test_bump_and_look();
    test_combat();
    test_spells();
    test_bolt();
    test_wild();
    test_items();
    test_game();
    test_ai();
    test_review_fixes();
    test_scenario();
    test_m4a();
    test_m4b();
    test_m4c();
    test_m4d();
    test_m4e();
    test_m4f();
    test_m4g();
    test_m4h();
    test_m5a();
#ifndef LOC_SELFTEST_NO_SAVE
    test_m4i();
#endif
#ifndef LOC_SELFTEST_NO_SAVE
    test_d64();
#endif
    test_d64_populate();
    test_m4k_ai();
    test_m4_review();
    test_m5b_tutorial();
    test_m5b_lexicon();
    test_m5c_events();
    test_m5e_balance();
    test_c1_c2();
    test_c5_drowning();
    test_c3_overlap();
    test_idle();
    test_d69_d71();
    load_house();   /* leave a clean state */
    return fails;
}
