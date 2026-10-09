#include "world.h"

#include <string.h>

#include "area.h"
#include "effect.h"
#include "events.h"
#include "gen/data.h"
#include "items.h"
#include "ride.h"
#include "gen/tiles.h"

/* Movement blocking per feature (GDD 3.3): chairs, candle stands and the
 * cauldron can be walked onto; furniture with a body blocks. */
static const bool FEATURE_BLOCKS[FE_COUNT] = {
    [FE_NONE] = false, [FE_WALL] = true, [FE_DOOR_CLOSED] = true,
    [FE_DOOR_OPEN] = false, [FE_BED] = true, [FE_BOOKSHELF] = true,
    [FE_CANDLE] = false, [FE_CAULDRON] = false, [FE_TABLE] = true,
    [FE_CHAIR] = false, [FE_DRAWERS] = true, [FE_CHEST] = true,
    [FE_TREE] = true, [FE_ROCK] = true, [FE_DOOR_LOCKED] = true,
    [FE_CHEST_FREE] = true, [FE_WINDOW] = true, [FE_FENCE] = true,
    /* door leaves (D61) are a drawing marker only: they never block (D76) */
    [FE_LEAF_N] = false, [FE_LEAF_E] = false, [FE_LEAF_S] = false, [FE_LEAF_W] = false,
};

/* Tall features that block ground sight (GDD 3.4). A table rather than an
 * ||-chain: ez80 clang turns such chains into an i14 bit test it cannot
 * legalize (AGON-QUIRKS T7). */
static const bool FEATURE_SIGHT[FE_COUNT] = {
    [FE_WALL] = true, [FE_DOOR_CLOSED] = true, [FE_BOOKSHELF] = true,
    [FE_TREE] = true, [FE_ROCK] = true, [FE_DOOR_LOCKED] = true,
};



static void init_unit(Unit *u, uint8_t x, uint8_t y, uint8_t kind, uint8_t owner)
{
    const CreatureDef *k = &CREATURES[kind];
    u->x = x;
    u->y = y;
    u->kind = kind;
    u->owner = owner;
    u->flags = (uint8_t)(((k->flags & CF_UNDEAD) ? UF_UNDEAD : 0) |
                         ((k->flags & CF_MOUNT) ? UF_MOUNT : 0));
    u->native = k->native;
    if (kind == CR_PIXIE)
        u->flags |= UF_INVISIBLE;       /* pixies are always unseen (K2) */
    u->ap = u->ap_max = k->ap;
    u->ap_fly = k->ap_fly;
    u->sta = u->sta_max = k->stamina;
    u->con = u->con_max = k->con;
    u->wounds = 0;
    u->com = k->combat;
    u->def = k->defence;
    u->mr = k->magic_res;
    u->mana = u->mana_max = k->mana;
    u->item_count = 0;
    u->in_use = NO_ITEM;
    u->rider_kind = 0xFF;
    u->rider_con = u->rider_con_max = u->rider_sta = u->rider_sta_max = 0;
    u->rider_com = u->rider_def = u->rider_mr = 0;
    u->rider_wounds = 0;
    u->rider_ap = u->rider_ap_max = 0;
    u->post_x = u->post_y = 0xFF;
    u->grudge = 0;
    u->herd_dir = 0;
    u->travel = 0;
    u->group = 0;
    u->reacted = false;
    u->plan_route = 0xFF;
    u->plan_step = u->plan_flags = u->visit_head = 0;
    memset(u->visited, 0xFF, sizeof u->visited);
    u->alarm = 0;
    u->alarm_charge = 0;
    u->alarm_x = u->alarm_y = 0;
    u->alarm_owner = OWN_NEUTRAL;
}

bool world_load_bin(World *w, const uint8_t *b, uint16_t len)
{
    uint8_t mw, mh, x, y, i, n;
    uint16_t cells, pos, k;

    if (len < MAPBIN_HEADER || memcmp(b, "LOCM", 4) != 0 ||
        (b[4] != 2 && b[4] != MAPBIN_VERSION))
        return false;
    if ((uint16_t)(b[5] | (b[6] << 8)) != TILE_COUNT)   /* stale map vs tile bank */
        return false;
    mw = b[7];
    mh = b[8];
    if (mw == 0 || mh == 0 || mw > MAP_MAX_W || mh > MAP_MAX_H)
        return false;
    cells = (uint16_t)((uint16_t)mw * mh);
    pos = (uint16_t)(MAPBIN_HEADER + 3u * cells);
    if (len < pos + 1u)
        return false;
    for (k = 0; k < cells; k++) {
        if (b[MAPBIN_HEADER + k] >= FL_COUNT ||
            b[MAPBIN_HEADER + cells + k] >= FE_COUNT ||
            b[MAPBIN_HEADER + 2u * cells + k] > DE_MUSHROOMS)
            return false;
    }
    n = b[pos++];
    if (n > MAX_UNITS || len < pos + 4u * n + 1u)
        return false;
    w->disturb_n = 0;                    /* a new map: no old trouble */
    for (i = 0; i < n; i++) {
        const uint8_t *u = &b[pos + 4u * i];
        if (u[0] >= mw || u[1] >= mh || u[2] >= CR_COUNT || u[3] >= OWN_COUNT)
            return false;
    }
    {
        uint16_t opos = (uint16_t)(pos + 4u * n);
        uint8_t no = b[opos];
        if (no > MAX_OBJECTS || len < opos + 1u + 4u * no)
            return false;
        for (i = 0; i < no; i++) {
            const uint8_t *o = &b[opos + 1u + 4u * i];
            if (o[0] >= mw || o[1] >= mh || (uint16_t)(o[2] | (o[3] << 8)) >= TILE_COUNT)
                return false;
        }
    }

    /* Validated: build the world. */
    {
        uint8_t gen = (uint8_t)(w->generation + 1);
        memset(w, 0, sizeof *w);
        w->generation = gen;
    }
    w->w = mw;
    w->h = mh;
    w->wrap = b[9] ? 1 : 0;
    for (y = 0; y < mh; y++)
        for (x = 0; x < mw; x++) {
            k = (uint16_t)((uint16_t)y * mw + x);
            w->floor[y][x] = b[MAPBIN_HEADER + k];
            w->feature[y][x] = b[MAPBIN_HEADER + cells + k];
            w->decor[y][x] = b[MAPBIN_HEADER + 2u * cells + k];
        }
    for (i = 0; i < n; i++) {
        const uint8_t *u = &b[pos + 4u * i];
        init_unit(&w->units[i], u[0], u[1], u[2], u[3]);
        w->units[i].id = i;
    }
    w->unit_count = n;
    w->next_id = n;
    pos = (uint16_t)(pos + 4u * n);
    w->object_count = b[pos++];
    for (i = 0; i < w->object_count; i++) {
        w->objects[i].x = b[pos++];
        w->objects[i].y = b[pos++];
        w->objects[i].tile = (uint16_t)(b[pos] | (b[pos + 1] << 8));
        pos += 2;
    }
    /* v4 maps: undead guards stand their ground (M4h Wächter) */
    for (i = 0; i < w->unit_count; i++)
        if (w->units[i].owner == OWN_NEUTRAL &&
            (w->units[i].flags & UF_UNDEAD))
            w->units[i].post_x = w->units[i].x,
            w->units[i].post_y = w->units[i].y;
    w->portal_x = w->portal_y = -1;      /* v2 maps carry no portal */
    w->portal_rmin = w->portal_rmax = w->portal_span = 0;
    w->route_n = w->route_summon_n = w->trig_n = 0;
    memset(w->trig_fired, 0, sizeof w->trig_fired);
    if (b[4] >= 5 && pos + 5 <= len) {   /* v5: portal x y rmin rmax span */
        if (b[pos] != 0xFF) {            /* 0xFF = no portal (filler) */
            w->portal_x = b[pos];
            w->portal_y = b[pos + 1];
            w->portal_rmin = b[pos + 2];
            w->portal_rmax = b[pos + 3];
            w->portal_span = b[pos + 4];
        }
        pos += 5;
    }
    memset(w->roof, 0, sizeof w->roof);
    if (b[4] >= 5) {                     /* one roof byte per field */
        uint16_t k2;
        if (len < pos + cells)
            return false;
        for (k2 = 0; k2 < cells; k2++)
            if (b[pos + k2])
                w->roof[k2 >> 3] |= (uint8_t)(0x80u >> (k2 & 7));
    }
    {   /* doors that start open get their leaf (D61, needs the roof) */
        int16_t x, y, lx, ly;
        uint8_t leaf;
        for (y = 0; y < w->h; y++)
            for (x = 0; x < w->w; x++)
                if (w->feature[y][x] == FE_DOOR_OPEN && !world_is_gate(w, x, y) &&
                    world_leaf_spot(w, x, y, -1, -1, &lx, &ly, &leaf))
                    w->feature[ly][lx] = leaf;
    }
    return true;
}

bool world_has_roof(const World *w, int16_t x, int16_t y)
{
    uint16_t cell;
    if (!world_wrap(w, &x, &y))
        return false;
    cell = (uint16_t)(y * w->w + x);
    return (w->roof[cell >> 3] & (uint8_t)(0x80u >> (cell & 7))) != 0;
}

void world_poke(World *w, int16_t x, int16_t y)
{
    uint8_t i;
    for (i = 0; i < w->trig_n; i++)
        if (w->trig_x[i] == x && w->trig_y[i] == y && w->trig_id[i] < 64)
            w->trig_fired[w->trig_id[i] >> 3] |= (uint8_t)(1u << (w->trig_id[i] & 7));
}

void world_map_changed(World *w)
{
    w->generation++;
}

bool world_wrap(const World *w, int16_t *x, int16_t *y)
{
    if (w->wrap) {
        /* Hot path: callers are at most one map size off. The eZ80 and
         * the 68000 (Mega Drive port) have no fast 32-bit modulo, so only
         * far coordinates pay for the division. */
        if (*x < 0)
            *x = (int16_t)(*x + w->w);
        else if (*x >= w->w)
            *x = (int16_t)(*x - w->w);
        if (*y < 0)
            *y = (int16_t)(*y + w->h);
        else if (*y >= w->h)
            *y = (int16_t)(*y - w->h);
        if (*x < 0 || *x >= w->w)
            *x = (int16_t)(((*x % w->w) + w->w) % w->w);
        if (*y < 0 || *y >= w->h)
            *y = (int16_t)(((*y % w->h) + w->h) % w->h);
        return true;
    }
    return *x >= 0 && *y >= 0 && *x < w->w && *y < w->h;
}

uint8_t world_feature(const World *w, int16_t x, int16_t y)
{
    return world_wrap(w, &x, &y) ? w->feature[y][x] : FE_NONE;
}

uint8_t world_floor(const World *w, int16_t x, int16_t y)
{
    return world_wrap(w, &x, &y) ? w->floor[y][x] : FL_GRASS;
}

bool world_is_wall_line(const World *w, int16_t x, int16_t y)
{
    uint8_t f = world_feature(w, x, y);
    static const bool WALL_LINE[FE_COUNT] = {   /* table, not ||: AGON-QUIRKS T7 */
        [FE_WALL] = true, [FE_DOOR_CLOSED] = true, [FE_DOOR_OPEN] = true,
        [FE_DOOR_LOCKED] = true, [FE_WINDOW] = true,
    };
    return f < FE_COUNT && WALL_LINE[f];
}


/* A roof does not block sight (D56): walls, doors and windows decide what
 * is seen, the roof is only drawn over what nobody sees. */
bool world_blocks_sight(const World *w, int16_t x, int16_t y)
{
    if (!world_wrap(w, &x, &y))
        return false;
    return FLOOR_SIGHT[w->floor[y][x]] || FEATURE_SIGHT[w->feature[y][x]];
}

bool world_feature_blocks_sight(const World *w, uint8_t x, uint8_t y)
{
    return FEATURE_SIGHT[w->feature[y][x]];
}

/* Eight sight-blocking bits of one row, MSB first (floor and feature; the
 * roof is display only, D56). */
uint8_t world_sight_byte(const World *w, uint8_t y, uint8_t x)
{
    const uint8_t *fl = w->floor[y], *fe = w->feature[y];
    uint8_t bits = 0, i;
    for (i = 0; i < 8; i++) {
        uint8_t xi = (uint8_t)(x + i);
        bits <<= 1;
        if (xi < w->w && (FLOOR_SIGHT[fl[xi]] || FEATURE_SIGHT[fe[xi]]))
            bits |= 1;
    }
    return bits;
}

bool world_blocks(const World *w, int16_t x, int16_t y)
{
    if (!world_wrap(w, &x, &y))
        return true;
    return FEATURE_BLOCKS[w->feature[y][x]];
}

uint8_t world_unit_at(const World *w, int16_t x, int16_t y, UnitLayer layer)
{
    uint8_t i;
    bool air = layer == UL_AIR;
    if (!world_wrap(w, &x, &y))
        return NO_UNIT;
    for (i = 0; i < w->unit_count; i++)
        if (w->units[i].x == x && w->units[i].y == y &&
            ((w->units[i].flags & UF_FLYING) != 0) == air)
            return i;
    return NO_UNIT;
}

uint8_t world_blocking_unit_at(const World *w, int16_t x, int16_t y,
                               UnitLayer layer, uint8_t owner)
{
    uint8_t i;
    bool air = layer == UL_AIR;
    if (!world_wrap(w, &x, &y))
        return NO_UNIT;
    for (i = 0; i < w->unit_count; i++)
        if (w->units[i].x == x && w->units[i].y == y &&
            ((w->units[i].flags & UF_FLYING) != 0) == air &&
            (owner == OWN_NEUTRAL || w->units[i].owner != owner))
            return i;
    return NO_UNIT;
}

/* Ground that drowns: water floors and flooded fields (K5.3). */
static bool drowning_at(const World *w, uint8_t x, uint8_t y)
{
    return FLOOR_DROWN[w->floor[y][x]] || area_kind_at(w, x, y) == AREA_FLOOD;
}

uint8_t world_step_cost(const World *w, int16_t x, int16_t y, bool diagonal)
{
    uint8_t c = FLOOR_AP[world_floor(w, x, y)];
    return diagonal ? (uint8_t)((c * 3 + 1) / 2) : c;
}

uint8_t world_unit_step_cost(const World *w, uint8_t unit, int16_t x, int16_t y,
                             bool diagonal)
{
    uint8_t c;
    AreaKind ak = area_kind_at(w, x, y);
    if (unit < w->unit_count && (FLOOR_NATIVE[world_floor(w, x, y)] & w->units[unit].native))
        c = FLOOR_AP[FL_STONE];   /* at home in this terrain: plain floor cost */
    else
        c = FLOOR_AP[world_floor(w, x, y)];
    if (ak == AREA_FIRE)
        c = 16;                   /* wading through flames (K8.4) */
    else if (ak == AREA_FLOOD &&
             !(unit < w->unit_count && (w->units[unit].native & NATIVE_WATER)))
        c = 12;                   /* a flooded field is water */
    return diagonal ? (uint8_t)((c * 3 + 1) / 2) : c;
}

uint8_t world_air_step_cost(bool diagonal)
{
    return diagonal ? AIR_AP_DIAG : AIR_AP_ORTH;
}

void world_spend(World *w, uint8_t unit, uint8_t ap)
{
    uint8_t st = (uint8_t)((ap + 1) / 2);   /* half the AP, rounded up */
    Unit *u = &w->units[unit];
    u->ap = (uint8_t)(u->ap - ap);
    u->sta = u->sta > st ? (uint8_t)(u->sta - st) : 0;
}

void world_spend_ap(World *w, uint8_t unit, uint8_t ap)
{
    w->units[unit].ap = (uint8_t)(w->units[unit].ap - ap);
}

/* Actions a rider performs himself from the saddle (K6.6). */
static bool rider_action(uint8_t act)
{
    static const bool R[ACT_COUNT] = {
        [ACT_CAST] = true, [ACT_FIRE] = true, [ACT_THROW] = true,
        [ACT_PICK_UP] = true, [ACT_DROP] = true, [ACT_CHANGE] = true,
        [ACT_EAT] = true, [ACT_DRINK] = true, [ACT_FILL] = true,
        [ACT_READ] = true, [ACT_OPEN_DOOR] = true, [ACT_UNLOCK] = true,
        [ACT_OPEN_CHEST] = true,
    };
    return R[act];
}

bool world_can_pay(const World *w, uint8_t unit, uint8_t action)
{
    const Unit *u = &w->units[unit];
    if ((u->flags & UF_RIDDEN) && rider_action(action))
        return u->rider_ap >= ACTIONS[action].ap && u->rider_sta >= ACTIONS[action].stamina;
    return u->ap >= ACTIONS[action].ap && u->sta >= ACTIONS[action].stamina;
}

uint8_t world_pool_ap(const World *w, uint8_t unit, uint8_t action)
{
    const Unit *u = &w->units[unit];
    return ((u->flags & UF_RIDDEN) && rider_action(action)) ? u->rider_ap : u->ap;
}

void world_spend_ap_for(World *w, uint8_t unit, uint8_t action, uint8_t ap)
{
    Unit *u = &w->units[unit];
    if ((u->flags & UF_RIDDEN) && rider_action(action))
        u->rider_ap = (uint8_t)(u->rider_ap - ap);
    else
        u->ap = (uint8_t)(u->ap - ap);
}

bool world_has_ap(const World *w, uint8_t unit)
{
    const Unit *u = &w->units[unit];
    return u->ap > 0 || ((u->flags & UF_RIDDEN) && u->rider_ap > 0);
}

void world_pay(World *w, uint8_t unit, uint8_t action)
{
    Unit *u = &w->units[unit];
    if ((u->flags & UF_RIDDEN) && rider_action(action)) {
        u->rider_ap = (uint8_t)(u->rider_ap - ACTIONS[action].ap);
        u->rider_sta = u->rider_sta > ACTIONS[action].stamina
                           ? (uint8_t)(u->rider_sta - ACTIONS[action].stamina) : 0;
        return;
    }
    u->ap = (uint8_t)(u->ap - ACTIONS[action].ap);
    u->sta = u->sta > ACTIONS[action].stamina
                 ? (uint8_t)(u->sta - ACTIONS[action].stamina) : 0;
}

void world_set_wounds(Unit *u, uint8_t n)
{
    u->wounds = n > 7 ? 7 : n;
    if (u->wounds)
        u->flags |= UF_WOUNDED;
    else
        u->flags &= (uint8_t)~UF_WOUNDED;
}

uint8_t world_con_factor(const Unit *u)
{
    uint8_t f;
    if (u->con == 0 || u->con >= u->con_max)
        return 1;
    f = (uint8_t)(u->con_max / u->con);
    return f ? f : 1;
}

void world_delta(const World *w, int16_t x0, int16_t y0, int16_t x1, int16_t y1,
                 int16_t *dx, int16_t *dy)
{
    *dx = (int16_t)(x1 - x0);
    *dy = (int16_t)(y1 - y0);
    if (w->wrap) {
        if (*dx > w->w / 2) *dx = (int16_t)(*dx - w->w);
        if (*dx < -w->w / 2) *dx = (int16_t)(*dx + w->w);
        if (*dy > w->h / 2) *dy = (int16_t)(*dy - w->h);
        if (*dy < -w->h / 2) *dy = (int16_t)(*dy + w->h);
    }
}

uint8_t world_distance(const World *w, int16_t x0, int16_t y0, int16_t x1, int16_t y1)
{
    int16_t dx, dy;
    world_delta(w, x0, y0, x1, y1, &dx, &dy);
    if (dx < 0) dx = (int16_t)-dx;
    if (dy < 0) dy = (int16_t)-dy;
    return (uint8_t)(dx > dy ? dx : dy);
}

uint16_t world_range(const World *w, int16_t x0, int16_t y0, int16_t x1, int16_t y1)
{
    int16_t dx, dy;
    world_delta(w, x0, y0, x1, y1, &dx, &dy);
    if (dx < 0) dx = (int16_t)-dx;
    if (dy < 0) dy = (int16_t)-dy;
    return dx > dy ? (uint16_t)(2 * dx + dy) : (uint16_t)(2 * dy + dx);
}

uint8_t world_find_unit(const World *w, uint8_t id)
{
    uint8_t i;
    if (id == NO_UNIT)
        return NO_UNIT;
    for (i = 0; i < w->unit_count; i++)
        if (w->units[i].id == id)
            return i;
    return NO_UNIT;
}

/* Add a freshly initialised unit (summons); returns its index. The id
 * counter wraps after 255 spawns, so ids still in use are skipped. */
uint8_t world_scale_ap(const World *w, uint8_t ap)
{
    uint16_t v;
    if (w->ap_scale == 0 || w->ap_scale == 100)
        return ap;
    v = (uint16_t)((uint16_t)ap * w->ap_scale / 100u);
    return v > 255 ? 255 : (uint8_t)v;
}

void world_set_ap_scale(World *w, uint8_t pct)
{
    uint8_t old = w->ap_scale ? w->ap_scale : 100, i;
    if (pct == 0)
        pct = 100;
    w->ap_scale = pct;
    for (i = 0; i < w->unit_count; i++) {   /* back to the base, then scaled */
        Unit *u = &w->units[i];
        u->ap_max = world_scale_ap(w, (uint8_t)((uint16_t)u->ap_max * 100u / old));
        u->ap_fly = world_scale_ap(w, (uint8_t)((uint16_t)u->ap_fly * 100u / old));
        u->ap = u->ap_max;
        if (u->rider_ap_max) {
            u->rider_ap_max = world_scale_ap(w, (uint8_t)((uint16_t)u->rider_ap_max * 100u / old));
            u->rider_ap = u->rider_ap_max;
        }
    }
}

void world_add_remains(World *w, int16_t x, int16_t y, uint8_t kind, uint8_t owner)
{
    Remains *r;
    if (!world_wrap(w, &x, &y))
        return;
    r = &w->remains[w->remains_next];
    w->remains_next = (uint8_t)((w->remains_next + 1) % REMAINS_MAX);
    if (w->remains_n < REMAINS_MAX)
        w->remains_n++;
    r->x = (uint8_t)x;
    r->y = (uint8_t)y;
    r->kind = kind;
    r->owner = owner;
}

const Remains *world_remains_at(const World *w, int16_t x, int16_t y)
{
    uint8_t k;
    if (!world_wrap(w, &x, &y))
        return NULL;
    for (k = 1; k <= w->remains_n; k++) {   /* newest first */
        const Remains *r = &w->remains[(w->remains_next + REMAINS_MAX - k) % REMAINS_MAX];
        if (r->x == x && r->y == y)
            return r;
    }
    return NULL;
}

void world_noise(World *w, int16_t x, int16_t y, uint8_t kind, uint8_t owner)
{
    Noise *n;
    if (!world_wrap(w, &x, &y))
        return;
    n = &w->noises[w->noise_next];
    w->noise_next = (uint8_t)((w->noise_next + 1) % NOISE_MAX);
    if (w->noise_n < NOISE_MAX)
        w->noise_n++;
    n->x = (uint8_t)x;
    n->y = (uint8_t)y;
    n->kind = kind;
    n->owner = owner;
}

void world_noise_clear(World *w)
{
    w->noise_n = w->noise_next = 0;
}

uint8_t world_spawn_unit(World *w, uint8_t owner, uint8_t kind, uint8_t x, uint8_t y)
{
    Unit *u;
    if (w->unit_count >= MAX_UNITS || kind >= CR_COUNT)
        return NO_UNIT;
    while (w->next_id == NO_UNIT || world_find_unit(w, w->next_id) != NO_UNIT)
        w->next_id++;
    u = &w->units[w->unit_count];
    init_unit(u, x, y, kind, owner);
    u->ap = u->ap_max = world_scale_ap(w, u->ap_max);   /* D71 */
    u->ap_fly = world_scale_ap(w, u->ap_fly);
    u->id = w->next_id++;
    u->done = false;
    return w->unit_count++;
}

void world_provoke(World *w, uint8_t unit, uint8_t attacker_owner)
{
    if (unit < w->unit_count && attacker_owner < OWN_NEUTRAL &&
        w->units[unit].owner == OWN_NEUTRAL)
        w->units[unit].grudge |= (uint8_t)(1u << attacker_owner);
}

void world_disturb(World *w, int16_t x, int16_t y, uint8_t owner)
{
    uint8_t i;
    if (!world_wrap(w, &x, &y))
        return;
    for (i = 0; i < w->disturb_n; i++)    /* one entry per field is enough */
        if (w->disturb[i][0] == x && w->disturb[i][1] == y)
            return;
    if (w->disturb_n >= WORLD_DISTURB)
        return;
    w->disturb[w->disturb_n][0] = (uint8_t)x;
    w->disturb[w->disturb_n][1] = (uint8_t)y;
    w->disturb[w->disturb_n][2] = owner;
    w->disturb_n++;
}

void world_remove_unit(World *w, uint8_t unit)
{
    if (unit >= w->unit_count)
        return;
    w->units[unit] = w->units[w->unit_count - 1];   /* swap with the last */
    w->unit_count--;
}

/* The dead drop everything they carried on their field (D21) - flyers
 * onto the ground below. A full object list swallows the rest. */
void world_drop_carried(World *w, const Unit *u)
{
    uint8_t i;
    for (i = 0; i < u->item_count && w->object_count < MAX_OBJECTS; i++) {
        Object *o = &w->objects[w->object_count++];
        o->x = u->x;
        o->y = u->y;
        o->tile = OBJECTS[u->items[i]].tile;
    }
}

void world_kill_unit(World *w, uint8_t victim, uint8_t killer_kind,
                     uint8_t killer_owner, bool melee)
{
    if (victim >= w->unit_count)
        return;
    Unit mount;
    events_push(EV_DEATH, w->units[victim].x, w->units[victim].y,
                w->units[victim].kind, w->units[victim].owner, 0, 0);
    world_add_remains(w, w->units[victim].x, w->units[victim].y,
                      w->units[victim].kind, w->units[victim].owner);   /* D70 */
    world_noise(w, w->units[victim].x, w->units[victim].y, NOISE_DEATH,
                w->units[victim].owner);                           /* D69 */
    mount = w->units[victim];
    if (!(mount.flags & UF_RIDDEN))
        world_drop_carried(w, &w->units[victim]);   /* a rider keeps his pack */
    if (killer_owner < OWN_NEUTRAL && w->kill_count < MAX_KILLS) {
        Kill *k = &w->kills[w->kill_count++];
        k->victim_kind = w->units[victim].kind;
        k->victim_owner = w->units[victim].owner;
        k->killer_kind = killer_kind;
        k->killer_owner = killer_owner;
        k->melee = melee;
    }
    world_remove_unit(w, victim);
    if (mount.flags & UF_RIDDEN)
        ride_throw_off(w, &mount);       /* the rider survives the fall (D60) */
}

bool world_move_unit(World *w, uint8_t unit, int8_t dx, int8_t dy)
{
    int16_t nx, ny;
    uint8_t cost;
    Unit *u;
    if (unit >= w->unit_count || (dx == 0 && dy == 0) ||
        dx < -1 || dx > 1 || dy < -1 || dy > 1)
        return false;
    u = &w->units[unit];
    nx = (int16_t)(u->x + dx);
    ny = (int16_t)(u->y + dy);
    if (!world_wrap(w, &nx, &ny))
        return false;
    if (u->flags & UF_FLYING) {
        /* Flyers cross anything, they only respect the air layer. */
        if (world_blocking_unit_at(w, nx, ny, UL_AIR, u->owner) != NO_UNIT)
            return false;
        cost = world_air_step_cost(dx != 0 && dy != 0);
    } else {
        /* D43: ghost and spectre drift through walls and furniture. Other
         * units on the field still stop them - a body is a body. */
        if ((world_blocks(w, nx, ny) &&
             !(CREATURES[u->kind].flags & CF_PHASE)) ||
            world_blocking_unit_at(w, nx, ny, UL_GROUND, u->owner) != NO_UNIT)
            return false;
        if (area_blocks_kind(w, nx, ny))
            return false;                  /* stuck in blob or vine (M4d) */
        cost = world_unit_step_cost(w, unit, nx, ny, dx != 0 && dy != 0);
    }
    if (u->ap < cost)
        return false;
    if (world_engaged(w, unit))
        return false;                      /* bound units do not move (K11.7) */
    world_spend(w, unit, cost);
    u->x = (uint8_t)nx;
    u->y = (uint8_t)ny;
    world_engage(w, unit);                 /* arriving next to an enemy binds the mover */
    return true;
}

/* Another being (any layer) on the unit's field stops take-off and
 * landing (K8.1). */
static bool field_shared(const World *w, uint8_t unit)
{
    uint8_t i;
    for (i = 0; i < w->unit_count; i++)
        if (i != unit && w->units[i].x == w->units[unit].x &&
            w->units[i].y == w->units[unit].y)
            return true;
    return false;
}

/* The AP budget of a creature in the air: its flying AP, or twice the
 * ground AP under a flying potion (K8.1, K2). */
static uint16_t air_budget(const Unit *u)
{
    return u->ap_fly ? u->ap_fly : (uint16_t)(u->ap_max * 2);
}

bool world_take_off(World *w, uint8_t unit)
{
    Unit *u;
    AreaKind ak;
    if (unit >= w->unit_count)
        return false;
    u = &w->units[unit];
    if (u->flags & UF_FLYING)
        return false;
    if (u->ap_fly == 0 && !effect_active(u, EFF_FLYING))
        return false;                         /* no wings, no flying potion */
    if (u->ap < ACTIONS[ACT_TAKE_OFF].ap)
        return false;                         /* at least 6 AP (K8.1) */
    if (world_engaged(w, unit))
        return false;                         /* bound in melee */
    if (field_shared(w, unit))
        return false;                         /* nobody else on the field */
    if (world_has_roof(w, u->x, u->y))
        return false;                         /* indoors: no take-off bit */
    ak = area_kind_at(w, u->x, u->y);
    if (ak == AREA_VINE || ak == AREA_BLOB)
        return false;
    world_pay(w, unit, ACT_TAKE_OFF);
    /* the AP are converted in proportion between the two budgets (K2) */
    u->ap = (uint8_t)((uint16_t)u->ap * air_budget(u) / u->ap_max);
    u->flags |= UF_FLYING;
    return true;
}

bool world_land(World *w, uint8_t unit)
{
    Unit *u;
    AreaKind ak;
    if (unit >= w->unit_count)
        return false;
    u = &w->units[unit];
    if (!(u->flags & UF_FLYING))
        return false;
    if (field_shared(w, unit))
        return false;                         /* no free ground slot */
    if (world_has_roof(w, u->x, u->y))
        return false;                         /* landing under a roof (GDD 3.2) */
    if (drowning_at(w, u->x, u->y))
        return false;                         /* drowning floor (own rule) */
    ak = area_kind_at(w, u->x, u->y);
    if (ak == AREA_FIRE || ak == AREA_BLOB)
        return false;                         /* no-landing bit */
    if (u->ap < ACTIONS[ACT_LAND].ap)
        return false;
    world_pay(w, unit, ACT_LAND);             /* free */
    u->ap = (uint8_t)((uint16_t)u->ap * u->ap_max / air_budget(u));
    u->flags &= (uint8_t)~UF_FLYING;
    return true;
}

/* Round start of a unit's side (K4, R9-R12): wounds bleed 2 con each; a
 * flyer pays half of the AP it left unspent in stamina; the AP refill
 * is halved when exhausted (stamina under max/6) and divided by the
 * constitution factor; stamina recovers max/6 (max/2 with speed). */
void world_new_turn(World *w)
{
    uint8_t i;
    for (i = 0; i < w->unit_count; i++) {
        Unit *u = &w->units[i];
        /* airborne on a flying potion (no wings): twice the ground budget */
        uint16_t full = u->ap_max;
        uint16_t sta;
        uint8_t bleed;
        bool speed = effect_active(u, EFF_SPEED);
        if (u->flags & UF_FLYING)
            full = air_budget(u);
        u->reacted = false;                 /* new round, new reaction (D29) */
        bleed = (uint8_t)(2 * u->wounds);
        u->con = u->con > bleed ? (uint8_t)(u->con - bleed) : 0;
        if (u->flags & UF_FLYING) {         /* hovering is not free (R12) */
            uint8_t hover = (uint8_t)(u->ap / 2);
            u->sta = u->sta > hover ? (uint8_t)(u->sta - hover) : 0;
        }
        if (speed)
            full = (uint16_t)(full * 2);
        if (u->sta < u->sta_max / 6)        /* exhausted: half the AP */
            full = (uint16_t)(full / 2);
        full = (uint16_t)(full / world_con_factor(u));
        u->ap = full > 255 ? 255 : (uint8_t)full;
        sta = (uint16_t)(u->sta + (speed ? u->sta_max / 2 : u->sta_max / 6));
        u->sta = (uint8_t)(sta > u->sta_max ? u->sta_max : sta);
        if (drowning_at(w, u->x, u->y) &&   /* treading water (C5) */
            !(u->native & NATIVE_WATER) && !(u->flags & UF_FLYING)) {
            /* net -25 % a round on top of the max/6 recovery */
            uint8_t cost = (uint8_t)((uint16_t)u->sta_max * 5 / 12);
            uint8_t hurt = (uint8_t)(u->con_max / 5 > 1 ? u->con_max / 5 : 1);
            u->sta = u->sta > cost ? (uint8_t)(u->sta - cost) : 0;
            if (u->sta == 0)               /* spent: drowning, like bleeding */
                u->con = u->con > hurt ? (uint8_t)(u->con - hurt) : 0;
        }
        if (u->flags & UF_RIDDEN) {         /* the rider's own refill (K6.6) */
            uint16_t ra = u->rider_ap_max;
            uint8_t rbleed = (uint8_t)(2 * u->rider_wounds);   /* in the saddle too (F31) */
            u->rider_con = u->rider_con > rbleed ? (uint8_t)(u->rider_con - rbleed) : 0;
            uint8_t f = (u->rider_con && u->rider_con < u->rider_con_max)
                            ? (uint8_t)(u->rider_con_max / u->rider_con) : 1;
            if (u->rider_sta < u->rider_sta_max / 6)
                ra = (uint16_t)(ra / 2);
            u->rider_ap = (uint8_t)(ra / (f ? f : 1));
            {
                uint16_t rs = (uint16_t)(u->rider_sta + u->rider_sta_max / 6);
                u->rider_sta = (uint8_t)(rs > u->rider_sta_max ? u->rider_sta_max : rs);
            }
        }
        effect_tick(u);                   /* durations run down (GDD 2.1) */
        if ((u->flags & UF_FLYING) && u->ap_fly == 0 &&
            !effect_active(u, EFF_FLYING)) {  /* the potion wore off */
            if (world_unit_at(w, u->x, u->y, UL_GROUND) == NO_UNIT &&
                !drowning_at(w, u->x, u->y))
                u->flags &= (uint8_t)~UF_FLYING;  /* sinks to the ground */
            else
                effect_grant(u, EFF_FLYING, 1, 1);  /* hovers on, no room */
        }
        if (u->mana_max) {
            uint8_t mana = (uint8_t)(u->mana + u->mana_max / 25);
            u->mana = mana > u->mana_max || mana < u->mana ? u->mana_max : mana;
        }
    }
    for (i = 0; i < w->unit_count; i++) {  /* riders bled out in the saddle (F31) */
        Unit *u = &w->units[i];
        if (!(u->flags & UF_RIDDEN) || u->rider_con != 0)
            continue;
        events_push(EV_DEATH, u->x, u->y, u->rider_kind, u->owner, 1, 0);
        world_add_remains(w, u->x, u->y, u->rider_kind, u->owner);
        world_noise(w, u->x, u->y, NOISE_DEATH, u->owner);
        world_drop_carried(w, u);          /* the pack in the mount is his */
        u->item_count = 0;
        u->in_use = NO_ITEM;
        u->mana = u->mana_max = 0;
        u->rider_kind = 0xFF;
        u->rider_wounds = 0;
        u->flags &= (uint8_t)~UF_RIDDEN;
    }
    for (i = w->unit_count; i-- > 0;)      /* bleeders that died */
        if (w->units[i].con == 0) {
            Unit mount = w->units[i];
            events_push(EV_DEATH, w->units[i].x, w->units[i].y,
                        w->units[i].kind, w->units[i].owner, 1, 0);
            world_add_remains(w, w->units[i].x, w->units[i].y,
                              w->units[i].kind, w->units[i].owner);
            world_noise(w, w->units[i].x, w->units[i].y, NOISE_DEATH,
                        w->units[i].owner);
            if (mount.flags & UF_RIDDEN) {
                world_remove_unit(w, i);
                ride_throw_off(w, &mount);   /* D60 */
            } else {
                world_drop_carried(w, &w->units[i]);
                world_remove_unit(w, i);
            }
        }
}

/* A visible enemy on the same height within the 8 neighbour fields (K11.7). */
static bool enemy_at_sleeve(const World *w, uint8_t unit)
{
    const Unit *u = &w->units[unit];
    UnitLayer layer = (u->flags & UF_FLYING) ? UL_AIR : UL_GROUND;
    int8_t dx, dy;
    if (u->flags & UF_INVISIBLE)
        return false;                      /* the unseen are never bound */
    for (dx = -1; dx <= 1; dx++)
        for (dy = -1; dy <= 1; dy++) {
            uint8_t o;
            if (dx == 0 && dy == 0)
                continue;
            o = world_unit_at(w, (int16_t)(u->x + dx), (int16_t)(u->y + dy), layer);
            if (o != NO_UNIT && w->units[o].owner != u->owner &&
                !(w->units[o].flags & UF_INVISIBLE))
                return true;
        }
    return false;
}

bool world_engaged(const World *w, uint8_t unit)
{
    if (unit >= w->unit_count)
        return false;
    if (ride_may_attack_from(w, unit))
        return false;                    /* riders attack from anywhere (D21) */
    if (!(w->units[unit].flags & UF_ENGAGED))
        return false;                      /* free again since its last phase */
    return enemy_at_sleeve(w, unit);       /* gone enemy: it loosens (K11.7) */
}

bool world_enemy_adjacent(const World *w, uint8_t unit)
{
    return unit < w->unit_count && enemy_at_sleeve(w, unit);
}

void world_engage(World *w, uint8_t unit)
{
    if (unit >= w->unit_count)
        return;
    if (enemy_at_sleeve(w, unit))
        w->units[unit].flags |= UF_ENGAGED;   /* only the mover or striker (K11.7) */
}

void world_release(World *w, uint8_t owner)
{
    uint8_t i;
    for (i = 0; i < w->unit_count; i++)
        if (w->units[i].owner == owner)
            w->units[i].flags &= (uint8_t)~UF_ENGAGED;
}

BumpKind world_bump_kind(const World *w, uint8_t unit, int8_t dx, int8_t dy)
{
    int16_t nx, ny;
    uint8_t cost;
    const Unit *u;
    if (unit >= w->unit_count || (dx == 0 && dy == 0) ||
        dx < -1 || dx > 1 || dy < -1 || dy > 1)
        return BUMP_OUTSIDE;
    u = &w->units[unit];
    nx = (int16_t)(u->x + dx);
    ny = (int16_t)(u->y + dy);
    if (!world_wrap(w, &nx, &ny))
        return BUMP_OUTSIDE;
    if (u->flags & UF_FLYING) {
        if (world_blocking_unit_at(w, nx, ny, UL_AIR, u->owner) != NO_UNIT)
            return BUMP_UNIT;
        cost = world_air_step_cost(dx != 0 && dy != 0);
    } else {
        if (w->feature[ny][nx] == FE_DOOR_CLOSED)
            return BUMP_DOOR;
        if (world_blocks(w, nx, ny))
            return BUMP_TERRAIN;
        if (world_blocking_unit_at(w, nx, ny, UL_GROUND, u->owner) != NO_UNIT)
            return BUMP_UNIT;
        if (area_blocks_kind(w, nx, ny))
            return BUMP_HELD;
        cost = world_unit_step_cost(w, unit, nx, ny, dx != 0 && dy != 0);
    }
    if (u->ap < cost)
        return BUMP_NO_AP;
    return world_engaged(w, unit) ? BUMP_BOUND : BUMP_OK;
}

static bool is_door_feature(uint8_t fe)
{
    return fe == FE_DOOR_CLOSED || fe == FE_DOOR_OPEN || fe == FE_DOOR_LOCKED;
}

static bool solid_wall_at(const World *w, int16_t x, int16_t y)
{
    uint8_t fe = world_feature(w, x, y);
    return fe == FE_WALL || fe == FE_WINDOW;
}

static bool fence_at(const World *w, int16_t x, int16_t y)
{
    return world_feature(w, x, y) == FE_FENCE;
}

bool world_is_gate(const World *w, int16_t x, int16_t y)
{
    if (!is_door_feature(world_feature(w, x, y)))
        return false;
    if (solid_wall_at(w, x, (int16_t)(y - 1)) || solid_wall_at(w, x, (int16_t)(y + 1)) ||
        solid_wall_at(w, (int16_t)(x - 1), y) || solid_wall_at(w, (int16_t)(x + 1), y))
        return false;
    return fence_at(w, x, (int16_t)(y - 1)) || fence_at(w, x, (int16_t)(y + 1)) ||
           fence_at(w, (int16_t)(x - 1), y) || fence_at(w, (int16_t)(x + 1), y);
}

/* D61: a field a door leaf can swing onto - bare, dry, nobody on it. */
static bool leaf_room(const World *w, int16_t *x, int16_t *y)
{
    if (!world_wrap(w, x, y))
        return false;
    return w->feature[*y][*x] == FE_NONE && !FLOOR_DROWN[w->floor[*y][*x]] &&
           world_unit_at(w, *x, *y, UL_GROUND) == NO_UNIT;
}

/* Which side of the door at (x, y) is the room (+1 = south or east): the
 * roofed one; with a roof on both sides or none, away from whoever opens
 * it (fx < 0: unknown, south or east). */
static int8_t leaf_side(const World *w, int16_t x, int16_t y, bool vertical,
                        int16_t fx, int16_t fy)
{
    bool plus = vertical ? world_has_roof(w, (int16_t)(x + 1), y)
                         : world_has_roof(w, x, (int16_t)(y + 1));
    bool minus = vertical ? world_has_roof(w, (int16_t)(x - 1), y)
                          : world_has_roof(w, x, (int16_t)(y - 1));
    if (plus != minus)
        return plus ? 1 : -1;
    if (fx >= 0) {
        int16_t dx, dy;
        world_delta(w, x, y, fx, fy, &dx, &dy);
        if ((vertical ? dx : dy) > 0)
            return -1;                   /* the opener stands south/east */
    }
    return 1;
}

bool world_leaf_spot(const World *w, int16_t x, int16_t y, int16_t fx,
                     int16_t fy, int16_t *lx, int16_t *ly, uint8_t *leaf)
{
    bool vertical = world_is_wall_line(w, x, (int16_t)(y - 1)) ||
                    world_is_wall_line(w, x, (int16_t)(y + 1));
    int8_t s = leaf_side(w, x, y, vertical, fx, fy), k, pass;
    for (pass = 0; pass < 2; pass++, s = (int8_t)-s)   /* room side first */
        for (k = -1; k <= 1; k += 2) {   /* north / west of the way first */
            int16_t cx = vertical ? (int16_t)(x + s) : (int16_t)(x + k);
            int16_t cy = vertical ? (int16_t)(y + k) : (int16_t)(y + s);
            if (leaf_room(w, &cx, &cy)) {
                *lx = cx;
                *ly = cy;
                *leaf = vertical ? (k < 0 ? FE_LEAF_S : FE_LEAF_N)
                                 : (k < 0 ? FE_LEAF_E : FE_LEAF_W);
                return true;
            }
        }
    return false;
}

/* Fold back the leaf of the door at (x, y): it stands diagonally next to
 * the door, on the edge that faces the doorway. */
static void leaf_remove(World *w, int16_t x, int16_t y)
{
    static const int8_t DX[4] = {-1, 1, -1, 1};
    static const int8_t DY[4] = {-1, -1, 1, 1};
    uint8_t i;
    bool vertical = world_is_wall_line(w, x, (int16_t)(y - 1)) ||
                    world_is_wall_line(w, x, (int16_t)(y + 1));
    for (i = 0; i < 4; i++) {
        int16_t cx = (int16_t)(x + DX[i]), cy = (int16_t)(y + DY[i]);
        uint8_t fe;
        bool mine;
        if (!world_wrap(w, &cx, &cy))
            continue;
        fe = w->feature[cy][cx];
        mine = vertical ? ((fe == FE_LEAF_S && DY[i] < 0) || (fe == FE_LEAF_N && DY[i] > 0))
                        : ((fe == FE_LEAF_E && DX[i] < 0) || (fe == FE_LEAF_W && DX[i] > 0));
        if (mine) {
            w->feature[cy][cx] = FE_NONE;
            return;
        }
    }
}

bool world_open_door(World *w, uint8_t unit, int16_t x, int16_t y)
{
    int16_t lx = -1, ly = -1;
    uint8_t leaf = FE_NONE;
    Unit *u;
    if (unit >= w->unit_count)
        return false;
    u = &w->units[unit];
    if (!world_wrap(w, &x, &y) || w->feature[y][x] != FE_DOOR_CLOSED)
        return false;
    if (!(CREATURES[ride_actor_kind(u)].flags & CF_USE))
        return false;                    /* creature without hands */
    if (!world_can_pay(w, unit, ACT_OPEN_DOOR))
        return false;
    world_pay(w, unit, ACT_OPEN_DOOR);
    w->feature[y][x] = FE_DOOR_OPEN;
    /* the leaf is only drawn, it never blocks (D76): no room, no marker.
     * Gates fold flat (D61). */
    if (!world_is_gate(w, x, y) &&
        world_leaf_spot(w, x, y, u->x, u->y, &lx, &ly, &leaf))
        w->feature[ly][lx] = leaf;
    world_map_changed(w);                /* static view layers change */
    world_poke(w, x, y);
    return true;
}

bool world_has_key(const World *w, uint8_t unit)
{
    const Unit *u;
    uint8_t i;
    if (unit >= w->unit_count)
        return false;
    u = &w->units[unit];
    for (i = 0; i < u->item_count; i++)
        if (u->items[i] == OBJ_CHEST_KEY)
            return true;
    return false;
}

/* Shared by close / lock / unlock: unit has hands and the AP, the field
 * holds `from`; it becomes `to`. */
static bool door_change(World *w, uint8_t unit, int16_t x, int16_t y,
                        uint8_t from, uint8_t to, uint8_t action)
{
    if (unit >= w->unit_count)
        return false;
    if (!world_wrap(w, &x, &y) || w->feature[y][x] != from)
        return false;
    if (!(CREATURES[ride_actor_kind(&w->units[unit])].flags & CF_USE))
        return false;
    if (!world_can_pay(w, unit, action))
        return false;
    world_pay(w, unit, action);
    w->feature[y][x] = to;
    world_map_changed(w);
    world_poke(w, x, y);
    return true;
}

bool world_close_door(World *w, uint8_t unit, int16_t x, int16_t y)
{
    int16_t cx = x, cy = y;
    if (!world_wrap(w, &cx, &cy) ||
        world_unit_at(w, cx, cy, UL_GROUND) != NO_UNIT ||
        world_unit_at(w, cx, cy, UL_AIR) != NO_UNIT)
        return false;                    /* somebody stands in the doorway */
    if (!door_change(w, unit, x, y, FE_DOOR_OPEN, FE_DOOR_CLOSED,
                     ACT_OPEN_DOOR))
        return false;
    leaf_remove(w, cx, cy);              /* the leaf swings back (D61) */
    return true;
}

bool world_lock_door(World *w, uint8_t unit, int16_t x, int16_t y)
{
    return world_has_key(w, unit) &&
           door_change(w, unit, x, y, FE_DOOR_CLOSED, FE_DOOR_LOCKED,
                       ACT_UNLOCK);
}

bool world_unlock_door(World *w, uint8_t unit, int16_t x, int16_t y)
{
    return world_has_key(w, unit) &&
           door_change(w, unit, x, y, FE_DOOR_LOCKED, FE_DOOR_CLOSED,
                       ACT_UNLOCK);
}

char world_char(const World *w, int16_t x, int16_t y)
{
    static const char FEATURE_CHARS[FE_COUNT] = {
        [FE_NONE] = ' ', [FE_WALL] = '#', [FE_DOOR_CLOSED] = 'D', [FE_DOOR_OPEN] = 'd',
        [FE_BED] = 'B', [FE_BOOKSHELF] = 'S', [FE_CANDLE] = 'K', [FE_CAULDRON] = 'C',
        [FE_TABLE] = 'T', [FE_CHAIR] = 'h', [FE_DRAWERS] = 'M', [FE_CHEST] = 'X',
        [FE_TREE] = 't', [FE_ROCK] = 'R', [FE_DOOR_LOCKED] = 'L',
        [FE_CHEST_FREE] = 'x', [FE_WINDOW] = 'W', [FE_FENCE] = 'F',
        [FE_LEAF_N] = '/', [FE_LEAF_E] = '/', [FE_LEAF_S] = '/', [FE_LEAF_W] = '/'};
    static const char FLOOR_CHARS[FL_COUNT] = {
        [FL_STONE] = '.', [FL_WOOD] = ',', [FL_GRASS] = '"', [FL_PATH] = ':',
        [FL_TALL_GRASS] = ';', [FL_FOREST] = 'f', [FL_MAGIC_WOOD] = 'm',
        [FL_SHADOW_WOOD] = 'n', [FL_SWAMP] = 'u', [FL_WATER] = '~', [FL_RUBBLE] = 'r',
        [FL_BRIDGE] = '='};
    uint8_t u, f;
    if (!world_wrap(w, &x, &y))
        return ' ';
    u = world_unit_at(w, x, y, UL_GROUND);
    if (u == NO_UNIT)
        u = world_unit_at(w, x, y, UL_AIR);
    if (u != NO_UNIT)
        return w->units[u].kind == CR_WIZARD ? '@' : 'g';
    f = w->feature[y][x];
    if (f != FE_NONE)
        return FEATURE_CHARS[f];
    if (w->decor[y][x] == DE_RUG)
        return '=';
    return FLOOR_CHARS[w->floor[y][x]];
}
