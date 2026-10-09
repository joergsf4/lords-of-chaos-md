#include "view.h"

#include "gen/data.h"
#include "area.h"
#include "ride.h"
#include "sight.h"

#include <string.h>

/* Tile IDs per floor; half floors follow each floor tile in the order
 * n, s, w, e (see tools/build_tiles.py). */
static const uint16_t FLOOR_TILE[FL_COUNT] = {
    [FL_STONE] = T_FLOOR_STONE, [FL_WOOD] = T_FLOOR_WOOD, [FL_GRASS] = T_FLOOR_GRASS,
    [FL_PATH] = T_FLOOR_PATH, [FL_TALL_GRASS] = T_FLOOR_TALLGRASS,
    [FL_FOREST] = T_FLOOR_FOREST, [FL_MAGIC_WOOD] = T_FLOOR_MAGICWOOD,
    [FL_SHADOW_WOOD] = T_FLOOR_SHADOWWOOD, [FL_SWAMP] = T_FLOOR_SWAMP,
    [FL_WATER] = T_FLOOR_WATER_0, [FL_RUBBLE] = T_FLOOR_RUBBLE,
    [FL_BRIDGE] = T_FLOOR_BRIDGE_H};   /* v: see compose_static */
static const uint16_t FLOOR_HALF[FL_COUNT] = {
    [FL_STONE] = T_FLOOR_STONE_HALF_N, [FL_WOOD] = T_FLOOR_WOOD_HALF_N,
    [FL_GRASS] = T_FLOOR_GRASS_HALF_N, [FL_PATH] = T_FLOOR_PATH_HALF_N,
    [FL_TALL_GRASS] = T_FLOOR_TALLGRASS_HALF_N, [FL_FOREST] = T_FLOOR_FOREST_HALF_N,
    [FL_MAGIC_WOOD] = T_FLOOR_MAGICWOOD_HALF_N,
    [FL_SHADOW_WOOD] = T_FLOOR_SHADOWWOOD_HALF_N, [FL_SWAMP] = T_FLOOR_SWAMP_HALF_N,
    [FL_WATER] = T_FLOOR_WATER_0_HALF_N, [FL_RUBBLE] = T_FLOOR_RUBBLE_HALF_N,
    [FL_BRIDGE] = T_FLOOR_GRASS_HALF_N};
enum { HALF_N, HALF_S, HALF_W, HALF_E };

/* Per-field floor variants (graphics polish, D51): a hash of the world
 * position picks the texture, so a meadow is not one repeated tile. Entry 0
 * of a row is unused; a missing variant (0) falls back to the base tile. */
static const uint16_t FLOOR_VAR[FL_COUNT][4] = {
    [FL_GRASS] = {0, T_FLOOR_GRASS_1, T_FLOOR_GRASS_2, T_FLOOR_GRASS_3},
    [FL_PATH] = {0, T_FLOOR_PATH_1, T_FLOOR_PATH_2, T_FLOOR_PATH_1},
    [FL_TALL_GRASS] = {0, T_FLOOR_TALLGRASS_1, 0, T_FLOOR_TALLGRASS_1},
    [FL_SWAMP] = {0, T_FLOOR_SWAMP_1, 0, T_FLOOR_SWAMP_1},
    [FL_MAGIC_WOOD] = {0, T_FLOOR_MAGICWOOD_1, T_FLOOR_MAGICWOOD_2, T_FLOOR_MAGICWOOD_1},
    [FL_SHADOW_WOOD] = {0, T_FLOOR_SHADOWWOOD_1, T_FLOOR_SHADOWWOOD_2, T_FLOOR_SHADOWWOOD_1},
    [FL_WATER] = {0, T_FLOOR_WATERB_0, 0, T_FLOOR_WATERB_0},
};
/* Half the fields keep the base texture. */
static const uint8_t VAR_PICK[8] = {0, 0, 1, 0, 2, 0, 3, 1};

static void push(FieldLayers *f, uint16_t id);

/* Terrain transitions (D52): a field that gives way to a neighbouring terrain
 * gets a transparent edge overlay (tools/art/make_terrain.py): the shore lip
 * on water, grass tufts on a path, tall-grass blades on a meadow. Edge tiles
 * are indexed by a 4-bit neighbour mask (N=1 E=2 S=4 W=8, like wall_mask),
 * corner tiles cover a lone diagonal neighbour (NW, NE, SE, SW). */
enum { TR_NONE, TR_SHORE, TR_PATH, TR_TALL };
static const uint8_t TR_FAMILY[FL_COUNT] = {
    [FL_WATER] = TR_SHORE, [FL_PATH] = TR_PATH, [FL_GRASS] = TR_TALL};
#define FLBIT(f) ((uint16_t)(1u << (f)))
/* which neighbouring floors a family reacts to */
static const uint16_t TR_NEIGH[4] = {
    [TR_SHORE] = (uint16_t)~(FLBIT(FL_WATER) | FLBIT(FL_BRIDGE)),
    [TR_PATH] = FLBIT(FL_GRASS) | FLBIT(FL_TALL_GRASS) | FLBIT(FL_SWAMP) |
                FLBIT(FL_FOREST),
    [TR_TALL] = FLBIT(FL_TALL_GRASS)};
static const uint16_t TR_EDGE[4] = {0, T_EDGE_SHORE_M01, T_EDGE_PATH_M01,
                                    T_EDGE_TALL_M01};
static const uint16_t TR_CORNER[4] = {0, T_EDGE_SHORE_C0, T_EDGE_PATH_C0,
                                      T_EDGE_TALL_C0};

/* Floor next to a field; beyond the edge of a small map it is the field's own. */
static uint8_t floor_near(const World *w, int16_t x, int16_t y, uint8_t own)
{
    if (!world_wrap(w, &x, &y))
        return own;
    return w->floor[y][x];
}

static bool tr_hit(const World *w, int16_t x, int16_t y, uint8_t own, uint16_t neigh)
{
    return (neigh >> floor_near(w, x, y, own)) & 1;
}

static void push_transition(const World *w, int16_t wx, int16_t wy, uint8_t fl,
                            FieldLayers *out)
{
    static const int8_t DX[8] = {0, 1, 0, -1, -1, 1, 1, -1};
    static const int8_t DY[8] = {-1, 0, 1, 0, -1, -1, 1, 1};
    /* orthogonal mask bits that make a corner redundant: NW, NE, SE, SW */
    static const uint8_t SHADOWED[4] = {1 | 8, 1 | 2, 4 | 2, 4 | 8};
    uint8_t fam = TR_FAMILY[fl], mask = 0, i;
    uint16_t neigh;
    if (fam == TR_NONE || world_is_wall_line(w, wx, wy))
        return;
    neigh = TR_NEIGH[fam];
    for (i = 0; i < 4; i++)
        if (tr_hit(w, (int16_t)(wx + DX[i]), (int16_t)(wy + DY[i]), fl, neigh))
            mask |= (uint8_t)(1u << i);
    if (mask)
        push(out, (uint16_t)(TR_EDGE[fam] + mask - 1));
    for (i = 0; i < 4; i++)
        if (!(mask & SHADOWED[i]) &&
            tr_hit(w, (int16_t)(wx + DX[4 + i]), (int16_t)(wy + DY[4 + i]), fl, neigh))
            push(out, (uint16_t)(TR_CORNER[fam] + i));
}

/* Deterministic 16-bit hash of a world position (no division; the casts keep
 * it identical with the 24-bit int of the eZ80). */
static uint16_t field_hash(int16_t x, int16_t y)
{
    uint16_t h = (uint16_t)((uint16_t)x * 0x9E37u) ^ (uint16_t)((uint16_t)y * 0x85EBu);
    h ^= (uint16_t)(h >> 7);
    h = (uint16_t)(h * 0x2C1Bu);
    h ^= (uint16_t)(h >> 9);
    return h;
}

static const uint16_t FEATURE_TILE[FE_COUNT] = {
    [FE_BED] = T_BED, [FE_BOOKSHELF] = T_BOOKSHELF, [FE_CANDLE] = T_CANDLE_0,
    [FE_CAULDRON] = T_CAULDRON, [FE_TABLE] = T_TABLE, [FE_CHAIR] = T_CHAIR,
    [FE_DRAWERS] = T_DRAWERS, [FE_CHEST] = T_CHEST, [FE_TREE] = T_TREE,
    [FE_ROCK] = T_ROCK, [FE_CHEST_FREE] = T_CHEST,
};

/* D65: the leaf of an open door in an east-west wall stands in the frame,
 * hinged at one jamb. Its field (D61) lies diagonally next to the door:
 * leaf field west of the door = hinge west; field on the viewer's side
 * (south) = swung towards us, else seen through the opening. No leaf field
 * (a door that started open under no roof): the bare frame. */
static uint16_t door_h_open_tile(const World *w, int16_t x, int16_t y)
{
    static const int8_t DX[4] = {-1, 1, -1, 1};
    static const int8_t DY[4] = {-1, -1, 1, 1};
    uint8_t i;
    for (i = 0; i < 4; i++) {
        bool east = DX[i] > 0;
        if (world_feature(w, (int16_t)(x + DX[i]), (int16_t)(y + DY[i])) !=
            (east ? FE_LEAF_W : FE_LEAF_E))
            continue;
        if (DY[i] > 0)
            return east ? T_DOOR_H_OPEN_E : T_DOOR_H_OPEN_W;
        return east ? T_DOOR_H_FAR_E : T_DOOR_H_FAR_W;
    }
    return T_DOOR_H_OPEN;
}

/* D84: the leaf of an open door in a north-south wall stands beside the
 * frame, in the door's own tile. The marker field (D61) diagonally next to
 * the door tells side and hinge: east marker = east side, north marker =
 * hinged at the north jamb. No marker: the bare frame. */
static uint16_t door_v_open_tile(const World *w, int16_t x, int16_t y)
{
    static const int8_t DX[4] = {-1, 1, -1, 1};
    static const int8_t DY[4] = {-1, -1, 1, 1};
    uint8_t i;
    for (i = 0; i < 4; i++) {
        bool east = DX[i] > 0, north = DY[i] < 0;
        if (world_feature(w, (int16_t)(x + DX[i]), (int16_t)(y + DY[i])) !=
            (north ? FE_LEAF_S : FE_LEAF_N))
            continue;
        if (east)
            return north ? T_DOOR_V_OPEN_EN : T_DOOR_V_OPEN_ES;
        return north ? T_DOOR_V_OPEN_WN : T_DOOR_V_OPEN_WS;
    }
    return T_DOOR_V_OPEN;
}

/* Fence and gate (D54): a low fence joins its neighbours like a wall line
 * does, but it is its own family - it does not hide the floor and does not
 * block sight. A door between fence posts is a gate. */
static bool fence_at(const World *w, int16_t x, int16_t y)
{
    return world_feature(w, x, y) == FE_FENCE;
}

#define is_gate world_is_gate

static uint8_t fence_mask(const World *w, int16_t x, int16_t y)
{
    return (uint8_t)(((fence_at(w, x, (int16_t)(y - 1)) || is_gate(w, x, (int16_t)(y - 1))) ? 1 : 0) |
                     ((fence_at(w, (int16_t)(x + 1), y) || is_gate(w, (int16_t)(x + 1), y)) ? 2 : 0) |
                     ((fence_at(w, x, (int16_t)(y + 1)) || is_gate(w, x, (int16_t)(y + 1))) ? 4 : 0) |
                     ((fence_at(w, (int16_t)(x - 1), y) || is_gate(w, (int16_t)(x - 1), y)) ? 8 : 0));
}

/* Animated tiles: up to 4 frames per group, chosen by the 2-bit phase.
 * Two-frame groups (candles, portal, area effects) repeat A, B, A, B. The
 * water flows over 4 frames; all fields share one phase, so the ripples move
 * on across field borders (see tools/art/make_terrain.py). */
#define ANIM_2(a, b) {a, b, a, b}
static const uint16_t ANIM_F[][4] = {
    ANIM_2(T_CANDLE_0, T_CANDLE_1),
    {T_FLOOR_WATER_0, T_FLOOR_WATER_1, T_FLOOR_WATER_2, T_FLOOR_WATER_3},
    ANIM_2(T_PORTAL_0, T_PORTAL_1),
    ANIM_2(T_AREA_FIRE_0, T_AREA_FIRE_1),
    ANIM_2(T_AREA_BLOB_0, T_AREA_BLOB_1),
    ANIM_2(T_AREA_VINE_0, T_AREA_VINE_1),
    ANIM_2(T_AREA_FLOOD_0, T_AREA_FLOOD_1),
    {T_FLOOR_WATERB_0, T_FLOOR_WATERB_1, T_FLOOR_WATERB_2, T_FLOOR_WATERB_3},
    ANIM_2(T_DECOR_LILY_0, T_DECOR_LILY_1),
    ANIM_2(T_DECOR_MUSH_0, T_DECOR_MUSH_1),
    ANIM_2(T_DECOR_BUBBLE_0, T_DECOR_BUBBLE_1),
};
#define ANIM_N (sizeof ANIM_F / sizeof ANIM_F[0])

/* Which group a tile belongs to: 1 + index into ANIM_F, 0 = not animated.
 * anim_swap() is called for every layer of every field (roughly 13 600 times
 * per compose on a 9x9 window), so it must not search.
 *
 * Kept in step with ANIM_F by hand; selftest_view() checks every tile id
 * against the table, so drift cannot pass unnoticed. */
static const uint8_t anim_pair[TILE_COUNT] = {
    [T_CANDLE_0] = 1,      [T_CANDLE_1] = 1,
    [T_FLOOR_WATER_0] = 2, [T_FLOOR_WATER_1] = 2,
    [T_FLOOR_WATER_2] = 2, [T_FLOOR_WATER_3] = 2,
    [T_PORTAL_0] = 3,      [T_PORTAL_1] = 3,
    [T_AREA_FIRE_0] = 4,   [T_AREA_FIRE_1] = 4,
    [T_AREA_BLOB_0] = 5,   [T_AREA_BLOB_1] = 5,
    [T_AREA_VINE_0] = 6,   [T_AREA_VINE_1] = 6,
    [T_AREA_FLOOD_0] = 7,  [T_AREA_FLOOD_1] = 7,
    [T_FLOOR_WATERB_0] = 8, [T_FLOOR_WATERB_1] = 8,
    [T_FLOOR_WATERB_2] = 8, [T_FLOOR_WATERB_3] = 8,
    [T_DECOR_LILY_0] = 9,  [T_DECOR_LILY_1] = 9,
    [T_DECOR_MUSH_0] = 10, [T_DECOR_MUSH_1] = 10,
    [T_DECOR_BUBBLE_0] = 11, [T_DECOR_BUBBLE_1] = 11,
};

static uint16_t anim_swap(uint16_t id, uint8_t ph)
{
    uint8_t k;
    if (id >= TILE_COUNT)
        return id;
    k = anim_pair[id];
    if (k == 0)
        return id;
    return ANIM_F[k - 1][ph & 3];
}

static bool is_animated(uint16_t id)
{
    return id < TILE_COUNT && anim_pair[id] != 0;
}

bool view_anim_table_ok(void)
{
    uint16_t id;
    for (id = 0; id < TILE_COUNT; id++) {
        uint8_t k, want = 0;
        for (k = 0; k < ANIM_N; k++) {
            uint8_t f;
            for (f = 0; f < 4; f++)
                if (id == ANIM_F[k][f])
                    want = (uint8_t)(k + 1);
        }
        if (anim_pair[id] != want)
            return false;
    }
    return true;
}

static FieldLayers fields[VIEW_H][VIEW_W];
static uint8_t dirty[VIEW_H][VIEW_W];
static uint8_t animated[VIEW_H][VIEW_W];   /* field holds an animated tile */
static bool valid;
static int16_t origin_x, origin_y;
static int16_t cursor_x, cursor_y;
static uint16_t cursor_tile = NO_CURSOR;
static uint8_t phase;
static uint8_t tick;   /* full animation counter; phase = tick & 3 */
static const Sight *sight_map;   /* NULL: omniscient (tests, mockups) */
static int16_t portal_x = -1, portal_y = -1;   /* open portal, if any */

void view_invalidate(void) { valid = false; }

void view_set_origin(int16_t x, int16_t y)
{
    origin_x = x;
    origin_y = y;
}

int16_t view_origin_x(void) { return origin_x; }
int16_t view_origin_y(void) { return origin_y; }

/* D78/D85: the window recentres on the unit, but only when it has moved
 * more than one field off the middle (of 9): so every second step in one
 * direction, not every step. At the map edge the window stops (clamp
 * below). */
#define VIEW_CENTRE (VIEW_W / 2)
#define VIEW_SLACK 1

void view_follow(const World *w, int16_t x, int16_t y)
{
    int16_t rx = (int16_t)(x - origin_x), ry = (int16_t)(y - origin_y);
    if (rx < VIEW_CENTRE - VIEW_SLACK || rx > VIEW_CENTRE + VIEW_SLACK)
        origin_x = (int16_t)(x - VIEW_CENTRE);
    if (ry < VIEW_CENTRE - VIEW_SLACK || ry > VIEW_CENTRE + VIEW_SLACK)
        origin_y = (int16_t)(y - VIEW_CENTRE);
    if (w->wrap) {    /* keep the origin inside the world */
        origin_x = (int16_t)(((origin_x % w->w) + w->w) % w->w);
        origin_y = (int16_t)(((origin_y % w->h) + w->h) % w->h);
    } else {          /* keep small maps in view */
        if (origin_x > w->w - VIEW_W) origin_x = (int16_t)(w->w - VIEW_W);
        if (origin_y > w->h - VIEW_H) origin_y = (int16_t)(w->h - VIEW_H);
        if (origin_x < 0) origin_x = 0;
        if (origin_y < 0) origin_y = 0;
    }
}

void view_set_cursor(int16_t x, int16_t y, uint16_t tile)
{
    cursor_x = x;
    cursor_y = y;
    cursor_tile = tile;
}

void view_set_sight(const Sight *s)
{
    sight_map = s;
}

void view_set_portal(int16_t x, int16_t y)
{
    portal_x = x;
    portal_y = y;
}

void view_set_phase(uint8_t p)
{
    phase = p & 3;
    tick = p;
}

static void push(FieldLayers *f, uint16_t id)
{
    if (f->n < VIEW_MAX_LAYERS)
        f->id[f->n++] = id;
}

/* push() for an airborne unit: also flags the layer, the renderer draws
 * it a few pixels higher (GDD 11.3). */
static void push_air(FieldLayers *f, uint16_t id)
{
    if (f->n < VIEW_MAX_LAYERS) {
        f->air |= (uint16_t)(1u << f->n);
        f->id[f->n++] = id;
    }
}

static uint8_t wall_mask(const World *w, int16_t x, int16_t y)
{
    return (uint8_t)((world_is_wall_line(w, x, (int16_t)(y - 1)) ? 1 : 0) |
                     (world_is_wall_line(w, (int16_t)(x + 1), y) ? 2 : 0) |
                     (world_is_wall_line(w, x, (int16_t)(y + 1)) ? 4 : 0) |
                     (world_is_wall_line(w, (int16_t)(x - 1), y) ? 8 : 0));
}

/* Layers that only change when the map changes: floor, half floors, decor,
 * feature (candles in phase 0). (wx, wy) must be inside the map. */
static void compose_static(const World *w, int16_t wx, int16_t wy, FieldLayers *out)
{
    static const int8_t DX[4] = {0, 0, -1, 1}, DY[4] = {-1, 1, 0, 0};
    uint8_t fl, fe, i;

    out->n = 0;
    out->air = 0;
    out->ride = 0;
    out->foe = 0;
    out->wade = 0;
    fl = w->floor[wy][wx];
    fe = w->feature[wy][wx];
    {
        uint16_t h = field_hash(wx, wy), t = FLOOR_VAR[fl][VAR_PICK[(h >> 3) & 7]];
        if (fl == FL_BRIDGE)   /* spans the river: water north or south = east-west deck */
            t = (world_floor(w, wx, (int16_t)(wy - 1)) == FL_WATER ||
                 world_floor(w, wx, (int16_t)(wy + 1)) == FL_WATER)
                    ? T_FLOOR_BRIDGE_H : T_FLOOR_BRIDGE_V;
        push(out, t ? t : FLOOR_TILE[fl]);
        push_transition(w, wx, wy, fl, out);
        /* bubbles rise in the swamp (D54) */
        if (fl == FL_SWAMP && fe == FE_NONE && w->decor[wy][wx] == DE_NONE &&
            ((h >> 6) & 15) < 3)
            push(out, T_DECOR_BUBBLE_0);
        /* A water lily on open water: water on both sides along one axis
         * (a two-field river qualifies, a bank corner does not). */
        if (fl == FL_WATER && out->n == 1 && ((h >> 6) & 15) < 2 && fe == FE_NONE &&
            ((world_floor(w, (int16_t)(wx - 1), wy) == FL_WATER &&
              world_floor(w, (int16_t)(wx + 1), wy) == FL_WATER) ||
             (world_floor(w, wx, (int16_t)(wy - 1)) == FL_WATER &&
              world_floor(w, wx, (int16_t)(wy + 1)) == FL_WATER)))
            push(out, T_DECOR_LILY_0);
    }

    /* Half floors: each side of a wall line shows the neighbour's floor. */
    if (world_is_wall_line(w, wx, wy) && !is_gate(w, wx, wy)) {
        for (i = 0; i < 4; i++) {
            int16_t nx = (int16_t)(wx + DX[i]), ny = (int16_t)(wy + DY[i]);
            uint8_t nfl = world_floor(w, nx, ny);
            if (!world_is_wall_line(w, nx, ny) && nfl != fl)
                push(out, (uint16_t)(FLOOR_HALF[nfl] + i));
        }
    }

    if (w->decor[wy][wx] == DE_RUG)
        push(out, T_DECOR_RUG);
    else if (w->decor[wy][wx] == DE_PENTACLE)
        push(out, T_DECOR_PENTACLE);
    else if (w->decor[wy][wx] == DE_FLOWERS) {
        uint8_t v = VAR_PICK[(field_hash(wx, wy) >> 3) & 7];
        push(out, (uint16_t)(T_DECOR_FLOWERS_0 + (v > 2 ? 2 : v)));
    } else if (w->decor[wy][wx] == DE_MUSHROOMS)
        push(out, T_DECOR_MUSH_0);

    if (fe == FE_WALL) {
        push(out, (uint16_t)(T_WALL_00 + wall_mask(w, wx, wy)));
    } else if (fe == FE_WINDOW) {
        bool vertical = world_is_wall_line(w, wx, (int16_t)(wy - 1)) ||
                        world_is_wall_line(w, wx, (int16_t)(wy + 1));
        push(out, vertical ? T_WINDOW_V : T_WINDOW_H);
    } else if (fe == FE_FENCE) {
        push(out, (uint16_t)(T_FENCE_00 + fence_mask(w, wx, wy)));
    } else if (is_gate(w, wx, wy)) {
        bool vertical = fence_at(w, wx, (int16_t)(wy - 1)) || fence_at(w, wx, (int16_t)(wy + 1));
        bool open = fe == FE_DOOR_OPEN;
        push(out, vertical ? (open ? T_GATE_V_OPEN : T_GATE_V_CLOSED)
                           : (open ? T_GATE_H_OPEN : T_GATE_H_CLOSED));
    } else if (fe == FE_DOOR_CLOSED || fe == FE_DOOR_OPEN || fe == FE_DOOR_LOCKED) {
        bool vertical = world_is_wall_line(w, wx, (int16_t)(wy - 1)) ||
                        world_is_wall_line(w, wx, (int16_t)(wy + 1));
        push(out, vertical ? (fe == FE_DOOR_OPEN ? door_v_open_tile(w, wx, wy) : T_DOOR_V_CLOSED)
                           : (fe == FE_DOOR_OPEN ? door_h_open_tile(w, wx, wy) : T_DOOR_H_CLOSED));
    } else if (fe >= FE_LEAF_N && fe <= FE_LEAF_W) {
        /* D65/D84: drawn in the door's own tile, the field only keeps room for it */
    } else if (fe == FE_CANDLE) {
        push(out, T_CANDLE_0);
    } else if (fe != FE_NONE) {
        push(out, FEATURE_TILE[fe]);
    }
}

/* Whose eyes decide whether a roof is lifted: only the currently active
 * figure. Under a roof (D46) nothing is drawn at all; outside, the roof
 * opens on every field that figure sees (D41, D56): through an open door
 * or a window you look into the room as far as the walls let you, along
 * the same shadowcast the sight rules use (sight_look). Whatever it does
 * not see stays roofed again - the interior is then only a remembered
 * field. Negative x switches it off.
 *
 * Replaces the old flood fill, which lifted the whole connected roof as
 * soon as one own unit stood anywhere under it (F7, M4e) - and with it
 * two buffers worth 3.9 KB of eZ80 RAM. */
static int16_t roof_vx = -1, roof_vy;

static uint8_t active_unit_id = NO_UNIT;

void view_set_active_unit(uint8_t id)
{
    active_unit_id = id;
}

/* The unit drawn on a field: with several (C3) the active one, else the
 * first. build_overlay() applies the same rule. */
static uint8_t unit_for_view(const World *w, int16_t x, int16_t y, UnitLayer layer)
{
    uint8_t first = world_unit_at(w, x, y, layer), i;
    if (first == NO_UNIT || w->units[first].id == active_unit_id)
        return first;
    if (!world_wrap(w, &x, &y))
        return first;
    for (i = (uint8_t)(first + 1); i < w->unit_count; i++)
        if (w->units[i].id == active_unit_id && w->units[i].x == x &&
            w->units[i].y == y &&
            ((w->units[i].flags & UF_FLYING) != 0) == (layer == UL_AIR))
            return i;
    return first;
}

void view_set_roof_viewer(int16_t x, int16_t y)
{
    roof_vx = x;
    roof_vy = y;
}

static bool roof_lifted(const World *w, int16_t wx, int16_t wy)
{
    if (roof_vx < 0)
        return false;
    return sight_look(w, roof_vx, roof_vy, wx, wy);
}

/* D46 (playtest 2026-10-05): indoors no roof is drawn at all. The
 * per-field lift (D41) missed wall-corner fields - shadowcasting and the
 * Bresenham ray disagree there - so moving around a house kept popping
 * roof tiles up wherever the ray was cut. */
static bool viewer_under_roof(const World *w)
{
    if (roof_vx < 0)
        return false;
    return world_has_roof(w, roof_vx, roof_vy);
}

/* The roof covers the outer wall (playtest 2026-10-05): the bitmap marks
 * only the interior, and without this the house looked open-topped, the
 * roof ending in front of the wall. A wall or door touching roofed
 * ground carries the roof; on a wall the per-field lift (D41) never
 * applies - its roof goes when the viewer stands under the roof (D46),
 * not when the wall itself is in line of sight. */
static bool roof_covered(const World *w, int16_t wx, int16_t wy, bool *wall)
{
    static const int8_t N[4][2] = {{0, -1}, {0, 1}, {-1, 0}, {1, 0}};
    uint8_t fe = w->feature[wy][wx], i;
    *wall = fe == FE_WALL;
    if (fe == FE_WINDOW)
        return false;                    /* the pane stays visible (D56) */
    if (world_has_roof(w, wx, wy))
        return true;
    if (fe != FE_WALL && fe != FE_DOOR_CLOSED && fe != FE_DOOR_OPEN &&
        fe != FE_DOOR_LOCKED)
        return false;
    for (i = 0; i < 4; i++)
        if (world_has_roof(w, (int16_t)(wx + N[i][0]), (int16_t)(wy + N[i][1])))
            return true;
    return false;
}

/* A closed (not lifted) roof field next to a lifted one, walls excluded. */
static bool roof_closed_at(const World *w, int16_t x, int16_t y)
{
    bool wall;
    if (!world_wrap(w, &x, &y) || !roof_covered(w, x, y, &wall) || wall)
        return false;
    return !roof_lifted(w, x, y);
}

/* D80: the opening in the roof gets soft edges. A lifted field beside a
 * closed roof field keeps half of its roof as a checker (the pixels in
 * between show the room), one beside only a diagonal neighbour a quarter:
 * roof -> half -> quarter -> clear instead of a hard cut. */
static void push_roof_fringe(const World *w, int16_t wx, int16_t wy,
                             FieldLayers *out)
{
    static const int8_t N4[4][2] = {{0, -1}, {0, 1}, {-1, 0}, {1, 0}};
    static const int8_t ND[4][2] = {{-1, -1}, {1, -1}, {-1, 1}, {1, 1}};
    uint8_t i;
    for (i = 0; i < 4; i++)
        if (roof_closed_at(w, (int16_t)(wx + N4[i][0]), (int16_t)(wy + N4[i][1]))) {
            push(out, T_ROOF_HALF);
            return;
        }
    for (i = 0; i < 4; i++)
        if (roof_closed_at(w, (int16_t)(wx + ND[i][0]), (int16_t)(wy + ND[i][1]))) {
            push(out, T_ROOF_FAINT);
            return;
        }
}

/* The roof goes on LAST, not with the static layers (D44): drawn early it
 * sat below the units, and the renderer paints bottom-up - a figure under
 * a closed roof appeared to stand on it. Pushed here it covers whatever is
 * underneath, and the sight overlay and cursor still come after. */
static void apply_roof_rule(const World *w, int16_t wx, int16_t wy,
                            FieldLayers *out)
{
    bool wall;
    if (!world_wrap(w, &wx, &wy) || !roof_covered(w, wx, wy, &wall))
        return;
    if (viewer_under_roof(w))            /* D46: indoors, no roofs at all */
        return;
    if (!wall && roof_lifted(w, wx, wy)) {   /* D41: the glimpse through a door */
        push_roof_fringe(w, wx, wy, out);
        return;
    }
    push(out, T_ROOF);
}


/* A unit the frontend animates itself (gliding sprite): left out of the
 * composition meanwhile. NO_UNIT = none. Presentation only. */
static uint8_t hidden_unit_id = NO_UNIT;

void view_hide_unit(uint8_t id)
{
    hidden_unit_id = id;
}

/* Creature idle animation (D53): now and then a creature plays a four-step
 * idle (about a quarter of the time, staggered by unit id so the troop does
 * not move in unison). Creatures with drawn frames (CREATURE_FRAME) flap or
 * sway: base, f1, base, f2. All others rise a pixel or two - the renderer
 * lifts the layer (view_bob). A unit with a rider keeps its base tile: the
 * rider offsets are tied to it (ride.c, render.c). */
#define IDLE_REST 4
static const uint8_t FLAP_SEQ[4] = {0, 1, 0, 2};
static const uint8_t LIFT_SEQ[5] = {0, 1, 2, 1, 0};
static uint8_t unit_bob;   /* set by push_unit: lift << 4 | layer + 1, 0 = none */

static bool idle_on;   /* off by default: tests and tools see base tiles */

void view_set_idle(bool on) { idle_on = on; }

static uint8_t idle_step(const Unit *un)
{
    uint8_t slot = (uint8_t)((tick + un->id * 3) & 15);
    if (!idle_on || slot >= 4 || ride_rider_kind(un) < CR_COUNT)
        return IDLE_REST;
    return slot;
}

static uint16_t unit_tile(const Unit *un, uint8_t step)
{
    uint16_t t = CREATURE_TILE[un->kind];
    if (step < 4 && FLAP_SEQ[step] && CREATURE_FRAME[un->kind][FLAP_SEQ[step] - 1])
        t = CREATURE_FRAME[un->kind][FLAP_SEQ[step] - 1];
    return (uint16_t)(t + un->owner);
}

/* Hidden movement (GDD 3.4, AMI 4): enemy units are only drawn when the
 * viewer currently sees their field; invisible enemies never. */
static void push_unit(const World *w, const Unit *un, FieldLayers *out, bool air)
{
    uint8_t first = out->n;
    if (hidden_unit_id != NO_UNIT && un->id == hidden_unit_id)
        return;
    if (sight_map && un->owner != sight_map->owner &&
        (!sight_unit_visible(sight_map, w, un) || (un->flags & UF_INVISIBLE)))
        return;
    {   /* a rider sits behind its mount, lifted by the renderer (M4k): the
         * mount's body hides the rider's legs, no extra artwork needed */
        uint8_t rk = ride_rider_kind(un);
        if (rk < CR_COUNT && out->n + 2 <= VIEW_MAX_LAYERS) {
            if (air)
                out->air |= (uint16_t)(1u << out->n);
            out->ride |= (uint16_t)(1u << out->n);
            out->id[out->n++] = (uint16_t)(CREATURE_TILE[rk] + un->owner);
        }
    }
    {
        uint8_t step = idle_step(un);
        if (air)
            push_air(out, unit_tile(un, step));
        else {
            if (out->n < VIEW_MAX_LAYERS && FLOOR_DROWN[w->floor[un->y][un->x]] &&
                ride_rider_kind(un) >= CR_COUNT)
                out->wade = (uint8_t)(out->n + 1);   /* waist-deep (C4) */
            /* creatures without drawn frames rise a little (not wading) */
            if (step < 4 && LIFT_SEQ[step] && !out->wade && out->n < VIEW_MAX_LAYERS &&
                !CREATURE_FRAME[un->kind][0] && ride_rider_kind(un) >= CR_COUNT)
                unit_bob = (uint8_t)((LIFT_SEQ[step] << 4) | (out->n + 1));
            push(out, unit_tile(un, step));
        }
    }
    /* Mark what belongs to an enemy wizard so the renderer can frame it
     * (B4). Wild animals stay unmarked - they are nobody's troops. */
    if (sight_map && un->owner != sight_map->owner && un->owner != OWN_NEUTRAL)
        while (first < out->n)
            out->foe |= (uint16_t)(1u << first++);
}

/* Hidden map (GDD 11.2): unexplored fields are a black tile, explored but
 * out of sight get the raster overlay on top. A roof stays in its normal
 * texture (playtest 2026-10-05): the raster made the roof read as
 * "obscured" from outside, and the opaque tile covers whatever is below
 * it anyway. */
static bool has_roof_layer(const FieldLayers *out)
{
    uint8_t i;
    for (i = 0; i < out->n; i++)
        if (out->id[i] == T_ROOF)
            return true;
    return false;
}

static void apply_sight(const World *w, int16_t wx, int16_t wy, FieldLayers *out)
{
    if (!sight_map)
        return;
    if (!sight_explored(sight_map, w, wx, wy)) {
        out->n = 0;
        out->air = 0;
        out->ride = 0;
        out->foe = 0;
        out->wade = 0;
        push(out, T_UNEXPLORED);
    } else if (!sight_visible(sight_map, w, wx, wy) && !has_roof_layer(out)) {
        push(out, T_OVERLAY_REMEMBERED);
    }
}

static void compose_cursor(const World *w, int16_t wx, int16_t wy, FieldLayers *out)
{
    if (cursor_tile != NO_CURSOR) {
        int16_t cx = cursor_x, cy = cursor_y;
        if (world_wrap(w, &cx, &cy) && cx == wx && cy == wy)
            push(out, cursor_tile);
    }
}

/* Per-frame layers on top: animation phase, object, unit. */
static void compose_dynamic(const World *w, int16_t wx, int16_t wy, FieldLayers *out)
{
    uint8_t i, u;

    if (phase)
        for (i = 0; i < out->n; i++)
            out->id[i] = anim_swap(out->id[i], phase);

    for (i = 0; i < w->object_count; i++) {
        if (w->objects[i].x == wx && w->objects[i].y == wy) {
            push(out, w->objects[i].tile);
            break;
        }
    }
    if (i == w->object_count && world_remains_at(w, wx, wy))
        push(out, T_REMAINS);         /* a skeleton under no object (D70) */

    if (portal_x == wx && portal_y == wy)
        push(out, (phase & 1) ? T_PORTAL_1 : T_PORTAL_0);

    u = unit_for_view(w, wx, wy, UL_GROUND);
    if (u != NO_UNIT)
        push_unit(w, &w->units[u], out, false);
    u = unit_for_view(w, wx, wy, UL_AIR);
    if (u != NO_UNIT) {
        push(out, T_AIR_SHADOW);      /* ground shadow below the flyer */
        push_unit(w, &w->units[u], out, true);
    }
    apply_roof_rule(w, wx, wy, out);
    {   /* area effect overlay (M4d, layer 7) */
        AreaKind k = area_kind_at(w, wx, wy);
        if (k != AREA_NONE) {
            uint8_t p2 = area_power_at(w, wx, wy);
            uint16_t base = k == AREA_FIRE ? T_AREA_FIRE_0
                          : k == AREA_BLOB ? T_AREA_BLOB_0
                          : k == AREA_VINE ? T_AREA_VINE_0 : T_AREA_FLOOD_0;
            (void)p2;
            push(out, (phase & 1) ? (uint16_t)(base + 1) : base);
        }
    }
}

/* Reference implementation (slow, used by tests and the panel). */
void view_compose(const World *w, int16_t x, int16_t y, FieldLayers *out)
{
    int16_t wx = x, wy = y;
    if (!world_wrap(w, &wx, &wy)) {
        out->n = 0;
        push(out, T_FLOOR_GRASS);   /* outside a small map */
        return;
    }
    compose_static(w, wx, wy, out);
    compose_dynamic(w, wx, wy, out);
    apply_sight(w, wx, wy, out);
    compose_cursor(w, wx, wy, out);
}

/* Static layers of every map field, computed once per map
 * (MAP_MAX_W * MAP_MAX_H * sizeof(StaticField) = 26 KB). Platforms with
 * little RAM (the Mega Drive port, repo lords-of-chaos-md: 64 KB) build with
 * VIEW_STATIC_CACHE=0 and compose the static layers on demand instead;
 * the result is the same, only slower. VIEW_STATIC_CACHE=2 caches only the
 * window (VIEW_W x VIEW_H fields, ~2 KB), indexed by world position modulo
 * the window size: a scroll step recomposes just the new row or column. */
#ifndef VIEW_STATIC_CACHE
#define VIEW_STATIC_CACHE 1
#endif
#define VIEW_CACHE_FULL (VIEW_STATIC_CACHE == 1)
#define VIEW_CACHE_WIN (VIEW_STATIC_CACHE == 2)

#if VIEW_CACHE_FULL || VIEW_CACHE_WIN
/* Static layers only: at most floor, 4 transition pieces, decor and feature
 * (a wall: floor, 4 half floors, decor, wall). A full FieldLayers per field
 * took 44 KB of the eZ80's scarce RAM (AGON-QUIRKS S6). */
#define STATIC_MAX 8
typedef struct {
    uint8_t n;
    uint16_t id[STATIC_MAX];
} StaticField;
#endif
#if VIEW_CACHE_FULL
static StaticField scache[MAP_MAX_H][MAP_MAX_W];
#endif
#if VIEW_CACHE_WIN
typedef struct {
    int16_t wx, wy;             /* world field held, -1 = none */
    StaticField f;
} WinCache;
static WinCache swin[VIEW_H][VIEW_W];
#endif
static const World *cache_world;
static uint8_t cache_gen;

void view_rebuild(const World *w)
{
#if VIEW_CACHE_WIN
    uint8_t x, y;
    for (y = 0; y < VIEW_H; y++)
        for (x = 0; x < VIEW_W; x++)
            swin[y][x].wx = -1;
#endif
#if VIEW_CACHE_FULL
    uint8_t x, y;
    for (y = 0; y < w->h; y++)
        for (x = 0; x < w->w; x++) {
            FieldLayers f;
            uint8_t i;
            compose_static(w, x, y, &f);
            scache[y][x].n = f.n < STATIC_MAX ? f.n : STATIC_MAX;
            for (i = 0; i < scache[y][x].n; i++)
                scache[y][x].id[i] = f.id[i];
        }
#endif
    cache_world = w;
    cache_gen = w->generation;
}

/* Fast path: cached static layers + a per-frame overlay of objects, units
 * and cursor mapped to window positions. Must equal view_compose(). */
static uint16_t over_obj[VIEW_H][VIEW_W];
static uint8_t over_unit[VIEW_H][VIEW_W];   /* ground unit index + 1 */
static uint8_t over_air[VIEW_H][VIEW_W];    /* air unit index + 1, 0 = none */
#define NO_TILE 0xFFFF

static bool to_view(const World *w, int16_t x, int16_t y, uint8_t *vx, uint8_t *vy)
{
    int16_t rx = (int16_t)(x - origin_x), ry = (int16_t)(y - origin_y);
    if (w->wrap) {   /* origin and (x, y) are inside the world: no division */
        if (rx < 0) rx = (int16_t)(rx + w->w);
        if (ry < 0) ry = (int16_t)(ry + w->h);
    }
    if (rx < 0 || ry < 0 || rx >= VIEW_W || ry >= VIEW_H)
        return false;
    *vx = (uint8_t)rx;
    *vy = (uint8_t)ry;
    return true;
}

static void build_overlay(const World *w)
{
    uint8_t i, vx, vy;
    memset(over_obj, 0xFF, sizeof over_obj);     /* NO_TILE */
    memset(over_unit, 0, sizeof over_unit);
    memset(over_air, 0, sizeof over_air);
    for (i = 0; i < w->remains_n; i++)    /* skeletons, objects lie on top (D70) */
        if (to_view(w, w->remains[i].x, w->remains[i].y, &vx, &vy))
            over_obj[vy][vx] = T_REMAINS;
    for (i = w->object_count; i-- > 0;)   /* first object in the list wins */
        if (to_view(w, w->objects[i].x, w->objects[i].y, &vx, &vy))
            over_obj[vy][vx] = w->objects[i].tile;
    for (i = 0; i < w->unit_count; i++) {
        const Unit *un = &w->units[i];
        if (to_view(w, un->x, un->y, &vx, &vy)) {
            /* the active unit is the one on top where own units share (C3) */
            if (un->flags & UF_FLYING) {
                if (over_air[vy][vx] == 0 || un->id == active_unit_id)
                    over_air[vy][vx] = (uint8_t)(i + 1);
            } else {
                if (over_unit[vy][vx] == 0 || un->id == active_unit_id)
                    over_unit[vy][vx] = (uint8_t)(i + 1);
            }
        }
    }
}

static void compose_fast(const World *w, uint8_t vx, uint8_t vy, FieldLayers *out)
{
    int16_t wx = (int16_t)(origin_x + vx), wy = (int16_t)(origin_y + vy);
    uint8_t i;
    unit_bob = 0;
    if (w->wrap) {   /* origin is normalised in view_update(): one subtraction */
        if (wx >= w->w) wx = (int16_t)(wx - w->w);
        if (wy >= w->h) wy = (int16_t)(wy - w->h);
    } else if (wx < 0 || wy < 0 || wx >= w->w || wy >= w->h) {
        out->n = 1;
        out->id[0] = T_FLOOR_GRASS;
        return;
    }
#if VIEW_CACHE_FULL
    out->n = scache[wy][wx].n;
    out->air = 0;
    out->ride = 0;
    out->foe = 0;
    out->wade = 0;
    for (i = 0; i < out->n; i++)
        out->id[i] = scache[wy][wx].id[i];
#elif VIEW_CACHE_WIN
    {
        WinCache *c = &swin[wy % VIEW_H][wx % VIEW_W];
        if (c->wx != wx || c->wy != wy) {
            FieldLayers f;
            compose_static(w, wx, wy, &f);
            c->wx = wx;
            c->wy = wy;
            c->f.n = f.n < STATIC_MAX ? f.n : STATIC_MAX;
            for (i = 0; i < c->f.n; i++)
                c->f.id[i] = f.id[i];
        }
        out->n = c->f.n;
        out->air = 0;
        out->ride = 0;
        out->foe = 0;
        out->wade = 0;
        for (i = 0; i < out->n; i++)
            out->id[i] = c->f.id[i];
    }
#else
    compose_static(w, wx, wy, out);
#endif
    if (phase)
        for (i = 0; i < out->n; i++)
            out->id[i] = anim_swap(out->id[i], phase);
    if (over_obj[vy][vx] != NO_TILE)
        push(out, over_obj[vy][vx]);
    if (portal_x == wx && portal_y == wy)
        push(out, (phase & 1) ? T_PORTAL_1 : T_PORTAL_0);
    if (over_unit[vy][vx])
        push_unit(w, &w->units[over_unit[vy][vx] - 1], out, false);
    if (over_air[vy][vx]) {
        push(out, T_AIR_SHADOW);
        push_unit(w, &w->units[over_air[vy][vx] - 1], out, true);
    }
    apply_roof_rule(w, wx, wy, out);
    {   /* area effect overlay (M4d, layer 7) */
        AreaKind k = area_kind_at(w, wx, wy);
        if (k != AREA_NONE) {
            uint16_t base = k == AREA_FIRE ? T_AREA_FIRE_0
                          : k == AREA_BLOB ? T_AREA_BLOB_0
                          : k == AREA_VINE ? T_AREA_VINE_0 : T_AREA_FLOOD_0;
            push(out, (phase & 1) ? (uint16_t)(base + 1) : base);
        }
    }
    apply_sight(w, wx, wy, out);
    compose_cursor(w, wx, wy, out);
    /* no lift for what is hidden or under a roof */
    if (unit_bob && ((unit_bob & 15) > out->n || has_roof_layer(out)))
        unit_bob = 0;
}

/* Per window field: the lift of its idle creature (see unit_bob), whether a
 * ground unit stands there, and the idle step it was drawn in. */
static uint8_t bob[VIEW_H][VIEW_W];
static uint8_t unit_cell[VIEW_H][VIEW_W];   /* ground unit index + 1 */
static uint8_t cell_step[VIEW_H][VIEW_W];
static uint8_t had_air[VIEW_H][VIEW_W];     /* overhang before the last change */

uint8_t view_bob(uint8_t vx, uint8_t vy) { return bob[vy][vx]; }

/* A field overhangs the one above when it shows an airborne unit, a rider or
 * a lifted creature. */
static bool overhangs(uint8_t vx, uint8_t vy)
{
    return ((fields[vy][vx].air | fields[vy][vx].ride) != 0) || bob[vy][vx] != 0;
}

/* Airborne units are drawn a few pixels into the field above (GDD 11.3): a
 * field whose overhang changed - old or new - must repaint the field above
 * (stale overhang), and a repainted field must be followed by the overhang
 * field below it, since fields draw top-down. */
static void propagate_overhang(void)
{
    uint8_t vx, vy;
    for (vy = 0; vy < VIEW_H; vy++) {
        for (vx = 0; vx < VIEW_W; vx++) {
            if (!dirty[vy][vx])
                continue;
            if (vy > 0 && (overhangs(vx, vy) || had_air[vy][vx]))
                dirty[vy - 1][vx] = 1;
            if (vy + 1 < VIEW_H && overhangs(vx, (uint8_t)(vy + 1)))
                dirty[vy + 1][vx] = 1;
        }
    }
}

uint8_t view_update(const World *w)
{
    FieldLayers f;
    uint8_t vx, vy, n = 0;
    if (cache_world != w || cache_gen != w->generation)
        view_rebuild(w);
    if (w->wrap) {   /* once per frame, so the per-field code needs no division */
        origin_x = (int16_t)(((origin_x % w->w) + w->w) % w->w);
        origin_y = (int16_t)(((origin_y % w->h) + w->h) % w->h);
    }
    build_overlay(w);
    for (vy = 0; vy < VIEW_H; vy++) {
        for (vx = 0; vx < VIEW_W; vx++) {
            compose_fast(w, vx, vy, &f);
            animated[vy][vx] = 0;
            {
                uint8_t i;
                for (i = 0; i < f.n; i++)
                    if (is_animated(f.id[i]))
                        animated[vy][vx] = 1;
            }
            unit_cell[vy][vx] = over_unit[vy][vx];
            cell_step[vy][vx] = over_unit[vy][vx]
                ? idle_step(&w->units[over_unit[vy][vx] - 1]) : IDLE_REST;
            had_air[vy][vx] = overhangs(vx, vy);
            if (!valid || f.n != fields[vy][vx].n || f.air != fields[vy][vx].air ||
                f.ride != fields[vy][vx].ride || f.foe != fields[vy][vx].foe ||
                f.wade != fields[vy][vx].wade || unit_bob != bob[vy][vx] ||
                memcmp(f.id, fields[vy][vx].id, f.n * sizeof f.id[0]) != 0) {
                fields[vy][vx] = f;
                bob[vy][vx] = unit_bob;
                dirty[vy][vx] = 1;
            }
        }
    }
    valid = true;
    propagate_overhang();
    for (vy = 0; vy < VIEW_H; vy++)
        for (vx = 0; vx < VIEW_W; vx++)
            n = (uint8_t)(n + dirty[vy][vx]);
    return n;
}

uint8_t view_animate(uint8_t p)
{
    uint8_t vx, vy, i, n = 0;
    phase = p & 3;
    tick = p;
    for (vy = 0; vy < VIEW_H; vy++)
        for (vx = 0; vx < VIEW_W; vx++)
            had_air[vy][vx] = overhangs(vx, vy);
    for (vy = 0; vy < VIEW_H; vy++)
        for (vx = 0; vx < VIEW_W; vx++) {
            FieldLayers *f = &fields[vy][vx];
            if (unit_cell[vy][vx] && cache_world &&
                unit_cell[vy][vx] <= cache_world->unit_count) {
                /* a creature stands here: it may start, step or end its idle */
                uint8_t step = idle_step(&cache_world->units[unit_cell[vy][vx] - 1]);
                if (step != IDLE_REST || cell_step[vy][vx] != IDLE_REST) {
                    FieldLayers g;
                    compose_fast(cache_world, vx, vy, &g);
                    cell_step[vy][vx] = step;
                    if (g.n != f->n || memcmp(g.id, f->id, g.n * sizeof g.id[0]) != 0 ||
                        unit_bob != bob[vy][vx]) {
                        *f = g;
                        bob[vy][vx] = unit_bob;
                        dirty[vy][vx] = 1;
                    }
                    continue;
                }
            }
            if (!animated[vy][vx])
                continue;
            for (i = 0; i < f->n; i++) {
                uint16_t to = anim_swap(f->id[i], phase);
                if (to != f->id[i]) {
                    f->id[i] = to;
                    dirty[vy][vx] = 1;
                }
            }
        }
    propagate_overhang();
    for (vy = 0; vy < VIEW_H; vy++)
        for (vx = 0; vx < VIEW_W; vx++)
            n = (uint8_t)(n + dirty[vy][vx]);
    return n;
}

bool view_dirty(uint8_t vx, uint8_t vy) { return dirty[vy][vx] != 0; }

/* Force one field to repaint on the next render_fields() (fx overlays,
 * M5c). The cached layers stay valid - only the screen pixels are stale. */
void view_mark_dirty(uint8_t vx, uint8_t vy)
{
    if (vx < VIEW_W && vy < VIEW_H)
        dirty[vy][vx] = 1;
}

const FieldLayers *view_field(uint8_t vx, uint8_t vy) { return &fields[vy][vx]; }

void view_clean(void) { memset(dirty, 0, sizeof dirty); }

uint32_t view_hash(void)
{
    uint32_t h = 2166136261UL;
    uint8_t vx, vy, i;
    for (vy = 0; vy < VIEW_H; vy++)
        for (vx = 0; vx < VIEW_W; vx++) {
            const FieldLayers *f = &fields[vy][vx];
            h = (h ^ f->n) * 16777619UL;
            for (i = 0; i < f->n; i++) {
                h = (h ^ (f->id[i] & 0xFF)) * 16777619UL;
                h = (h ^ (f->id[i] >> 8)) * 16777619UL;
            }
        }
    return h;
}
