#include "items.h"

#include <stddef.h>

#include "area.h"
#include "combat.h"
#include "effect.h"
#include "events.h"
#include "gen/data.h"
#include "ride.h"
#include "sight.h"

/* Weapon of the object in use, WEAPON_NONE without one. */
uint8_t items_in_use_weapon(const Unit *u)
{
    if (u->in_use == NO_ITEM || u->in_use >= u->item_count)
        return WEAPON_NONE;
    return OBJECTS[u->items[u->in_use]].weapon;
}

uint8_t items_weight(const World *w, uint8_t unit)
{
    uint8_t i, sum = 0;
    const Unit *u;
    if (unit >= w->unit_count)
        return 0;
    u = &w->units[unit];
    for (i = 0; i < u->item_count; i++)
        sum = (uint8_t)(sum + OBJECTS[u->items[i]].weight);
    return sum;
}

uint8_t items_kind_at(const World *w, int16_t x, int16_t y)
{
    uint8_t i;
    for (i = 0; i < w->object_count; i++)
        if (w->objects[i].x == x && w->objects[i].y == y) {
            uint8_t k;
            for (k = 0; k < OBJ_COUNT; k++)
                if (OBJECTS[k].tile == w->objects[i].tile)
                    return k;
        }
    return NO_ITEM;
}

static void remove_ground_object(World *w, uint8_t i)
{
    w->objects[i] = w->objects[w->object_count - 1];
    w->object_count--;
}

bool items_pick_up(World *w, uint8_t unit)
{
    uint8_t i;
    if (unit >= w->unit_count)
        return false;
    for (i = 0; i < w->object_count; i++)
        if (w->objects[i].x == w->units[unit].x &&
            w->objects[i].y == w->units[unit].y &&
            items_kind_of_tile(w->objects[i].tile) != NO_ITEM)
            return items_pick_up_object(w, unit, i);
    return false;
}

uint8_t items_kind_of_tile(uint16_t tile)
{
    uint8_t k;
    for (k = 0; k < OBJ_COUNT; k++)
        if (OBJECTS[k].tile == tile)
            return k;
    return NO_ITEM;
}

bool items_pick_up_object(World *w, uint8_t unit, uint8_t obj)
{
    Unit *u;
    uint8_t kind;
    if (unit >= w->unit_count || obj >= w->object_count)
        return false;
    u = &w->units[unit];
    if (world_distance(w, u->x, u->y, w->objects[obj].x, w->objects[obj].y) > 1)
        return false;                    /* own field or a neighbour */
    kind = items_kind_of_tile(w->objects[obj].tile);
    if (kind == NO_ITEM || u->item_count >= UNIT_ITEMS)
        return false;
    if (kind == OBJ_CAULDRON_FULL)
        return false;                    /* it would spill (GDD 7.2) */
    if ((uint16_t)items_weight(w, unit) + OBJECTS[kind].weight >
        CREATURES[ride_actor_kind(u)].carry)
        return false;                    /* too heavy (GDD 8) */
    if (!world_can_pay(w, unit, ACT_PICK_UP))
        return false;
    world_pay(w, unit, ACT_PICK_UP);
    u->items[u->item_count++] = kind;
    remove_ground_object(w, obj);
    return true;
}

bool items_drop(World *w, uint8_t unit)
{
    Unit *u;
    uint8_t kind;
    if (unit >= w->unit_count)
        return false;
    u = &w->units[unit];
    if (u->in_use == NO_ITEM || u->in_use >= u->item_count ||
        w->object_count >= MAX_OBJECTS)
        return false;
    if (!world_can_pay(w, unit, ACT_DROP))
        return false;
    world_pay(w, unit, ACT_DROP);
    kind = u->items[u->in_use];
    u->items[u->in_use] = u->items[u->item_count - 1];
    u->item_count--;
    u->in_use = NO_ITEM;
    w->objects[w->object_count].x = u->x;
    w->objects[w->object_count].y = u->y;
    w->objects[w->object_count].tile = OBJECTS[kind].tile;
    w->object_count++;
    return true;
}

bool items_cycle(World *w, uint8_t unit)
{
    Unit *u;
    uint8_t n, pos, start;
    if (unit >= w->unit_count)
        return false;
    u = &w->units[unit];
    if (u->item_count == 0)
        return false;                    /* nothing to cycle through */
    n = u->item_count;
    /* Wield cycle (D28): every carried object in turn, then bare hands.
     * A shield defends from wherever it is carried (D21) and never
     * takes the hand. */
    start = (u->in_use == NO_ITEM || u->in_use >= n) ? n : u->in_use;
    pos = start;
    for (;;) {
        pos = (uint8_t)((pos + 1) % (uint8_t)(n + 1));   /* slot n = bare */
        if (pos == n)
            break;                       /* bare hands always allowed */
        if (OBJECTS[u->items[pos]].weapon != WEAPON_SHIELD)
            break;
    }
    if (pos == start)
        return false;                    /* nothing else to wield */
    if (!world_can_pay(w, unit, ACT_CHANGE))
        return false;
    world_pay(w, unit, ACT_CHANGE);
    u->in_use = pos == n ? NO_ITEM : pos;
    return true;
}

/* Enchanted weapons count double (K7); the Magic Slayer is already the
 * doubled table entry. */
static uint16_t magic_scale(const Unit *u, uint8_t weapon, uint16_t value)
{
    if ((u->flags & UF_MAGIC_WEAPON) && weapon != WEAPON_MAGIC_SLAYER)
        return (uint16_t)(value * 2);
    return value;
}

bool items_wield(World *w, uint8_t unit, uint8_t slot)
{
    Unit *u;
    if (unit >= w->unit_count)
        return false;
    u = &w->units[unit];
    if (slot != NO_ITEM && (slot >= u->item_count ||
                            OBJECTS[u->items[slot]].weapon == WEAPON_SHIELD))
        return false;                    /* nothing there / the shield never takes the hand */
    if (slot == u->in_use || (slot == NO_ITEM && u->in_use >= u->item_count))
        return false;                    /* already in hand */
    if (!world_can_pay(w, unit, ACT_CHANGE))
        return false;
    world_pay(w, unit, ACT_CHANGE);
    u->in_use = slot;
    return true;
}

/* Attack value of a thrown object (K6.4): the weapon table's throw value,
 * other objects weigh nothing as a missile. */
static uint8_t throw_value(const Unit *u, uint8_t weapon)
{
    uint16_t v = weapon == WEAPON_NONE ? 0 : WEAPONS[weapon].thrown;
    v = magic_scale(u, weapon, v);
    return v > 255 ? 255 : (uint8_t)v;
}

bool items_catch(World *w, uint8_t unit, uint8_t kind)
{
    Unit *c;
    if (unit >= w->unit_count)
        return false;
    c = &w->units[unit];
    if (c->item_count >= UNIT_ITEMS ||
        (uint16_t)items_weight(w, unit) + OBJECTS[kind].weight >
            CREATURES[ride_actor_kind(c)].carry)
        return false;
    c->items[c->item_count++] = kind;
    return true;
}

bool items_throw(World *w, Rng *rng, uint8_t unit, int8_t dx, int8_t dy)
{
    Unit *u;
    uint8_t kind, weapon, dist;
    int16_t x, y;
    if (unit >= w->unit_count || (dx == 0 && dy == 0) ||
        dx < -1 || dx > 1 || dy < -1 || dy > 1)
        return false;
    u = &w->units[unit];
    if (u->in_use == NO_ITEM || u->in_use >= u->item_count)
        return false;
    if (!world_can_pay(w, unit, ACT_THROW))
        return false;
    kind = u->items[u->in_use];
    weapon = OBJECTS[kind].weapon;
    world_pay(w, unit, ACT_THROW);
    u->items[u->in_use] = u->items[u->item_count - 1];
    u->item_count--;
    u->in_use = NO_ITEM;

    /* fly first (no dice yet), then resolve the hit: the projectile
     * event must come before its hit or miss */
    x = u->x;
    y = u->y;
    {
        uint8_t target = NO_UNIT, steps = 0;
        uint16_t flown = 0, reach = items_throw_range(w, unit, OBJECTS[kind].weight);
        for (dist = 0; dist < 36; dist++) {
            int16_t nx = (int16_t)(x + dx), ny = (int16_t)(y + dy);
            flown = (uint16_t)(flown + (dx != 0 && dy != 0 ? 3 : 2));
            if (flown > reach)
                break;
            if (!world_wrap(w, &nx, &ny) || world_blocks(w, nx, ny))
                break;
            target = world_unit_at(w, nx, ny, UL_GROUND);
            if (target == NO_UNIT)
                target = world_unit_at(w, nx, ny, UL_AIR);
            steps++;
            if (target != NO_UNIT)
                break;
            x = nx;
            y = ny;
        }
        events_push(EV_PROJECTILE, u->x, u->y, PJ_THROWN, u->owner,
                    (uint8_t)(int8_t)(dx * steps), (uint8_t)(int8_t)(dy * steps));
        if (target != NO_UNIT && w->units[target].owner == u->owner) {
            if (items_catch(w, target, kind))
                return true;
            /* no room: it drops at the friend's feet, unhurt */
        } else if (target != NO_UNIT) { /* thrown weapons hit flyers too */
            uint8_t dmg = 0;
            if (items_can_harm_undead(w, unit, target))
                dmg = combat_roll(rng, throw_value(u, weapon),
                                  items_defence(w, target));
            if (dmg)
                combat_damage(w, target, dmg, ride_actor_kind(u), u->owner, false, NULL);
            else
                events_push(EV_MISS, u->x, u->y, u->kind, u->owner, 0, 0);
            /* it lands in front of the target: x/y stopped there */
        }
    }
    if (w->object_count < MAX_OBJECTS) {
        w->objects[w->object_count].x = (uint8_t)x;
        w->objects[w->object_count].y = (uint8_t)y;
        w->objects[w->object_count].tile = OBJECTS[kind].tile;
        w->object_count++;
    }
    return true;
}

static bool is_dragon(uint8_t kind)
{
    return kind == CR_GOLD_DRAGON || kind == CR_GREEN_DRAGON || kind == CR_RED_DRAGON;
}

uint8_t items_throw_range(const World *w, uint8_t unit, uint8_t weight)
{
    uint16_t r = (uint16_t)(2u * items_combat(w, unit) / (weight ? weight : 1) + 5u);
    return r > 36 ? 36 : (uint8_t)r;
}

bool items_can_fire(const World *w, uint8_t unit)
{
    const Unit *u;
    uint8_t weapon;
    if (unit >= w->unit_count)
        return false;
    u = &w->units[unit];
    if (is_dragon(ride_actor_kind(u)))
        return true;
    weapon = items_in_use_weapon(u);
    return weapon != WEAPON_NONE && WEAPONS[weapon].ranged != 0;
}

uint8_t items_fire_range(const World *w, uint8_t unit)
{
    const Unit *u = &w->units[unit];
    if (is_dragon(ride_actor_kind(u)))
        return 12;
    return (u->flags & UF_MAGIC_WEAPON) ? 22 : 16;
}

bool items_fire(World *w, Rng *rng, uint8_t unit, int16_t tx, int16_t ty,
                uint8_t *damage)
{
    Unit *u;
    uint8_t target, attack;
    bool dragon;
    int16_t dx, dy;
    if (damage)
        *damage = 0;
    if (unit >= w->unit_count || !world_wrap(w, &tx, &ty))
        return false;
    u = &w->units[unit];
    if (!items_can_fire(w, unit))
        return false;                    /* no bow in hand, no dragon */
    dragon = is_dragon(ride_actor_kind(u));
    if (!world_can_pay(w, unit, ACT_FIRE))
        return false;
    if (world_range(w, u->x, u->y, tx, ty) > items_fire_range(w, unit))
        return false;
    {   /* the shot line depends on the heights (K11.6) */
        uint8_t g = world_unit_at(w, tx, ty, UL_GROUND), a = world_unit_at(w, tx, ty, UL_AIR);
        bool tgt_air = g == NO_UNIT && a != NO_UNIT;
        if (!sight_shot_clear(w, u->x, u->y, (u->flags & UF_FLYING) != 0, tx, ty, tgt_air))
            return false;
    }
    target = world_unit_at(w, tx, ty, UL_GROUND);
    if (target == NO_UNIT)
        target = world_unit_at(w, tx, ty, UL_AIR);
    if (target == NO_UNIT && !dragon)
        return false;                    /* a bow needs something to hit */
    world_delta(w, u->x, u->y, tx, ty, &dx, &dy);
    world_pay(w, unit, ACT_FIRE);
    events_push(EV_PROJECTILE, u->x, u->y, dragon ? PJ_BOLT : PJ_ARROW, u->owner,
                (uint8_t)(int8_t)dx, (uint8_t)(int8_t)dy);
    attack = dragon ? 35 : (uint8_t)magic_scale(u, items_in_use_weapon(u), BOW_ATTACK);
    if (target != NO_UNIT) {
        uint8_t dmg = 0;
        if (dragon || items_can_harm_undead(w, unit, target))
            dmg = combat_roll(rng, attack, items_defence(w, target));
        if (dmg) {
            if (damage)
                *damage = dmg;
            combat_damage(w, target, dmg, ride_actor_kind(u), u->owner, false, NULL);
        } else
            events_push(EV_MISS, tx, ty, u->kind, u->owner, 0, 0);
    }
    if (dragon && rng_range(rng, 20) < area_susceptibility(w, AREA_FIRE, tx, ty))
        area_set(w, AREA_FIRE, area_level(AREA_FIRE, u->owner), u->owner, tx, ty);
    return true;
}

/* Can this ATTACKER wound an UNDEAD defender (GDD 4.2)? Undead
 * attackers, the Magic Slayer and enchanted weapons (M4b) do; normal
 * weapons and bare hands do not. Callers pass the defender. */
bool items_can_harm_undead(const World *w, uint8_t attacker, uint8_t defender)
{
    const Unit *a = &w->units[attacker];
    uint8_t weapon;
    if (!(w->units[defender].flags & UF_UNDEAD))
        return true;                     /* the living are always woundable */
    if (a->flags & UF_UNDEAD)
        return true;
    weapon = items_in_use_weapon(a);
    return weapon != WEAPON_NONE &&
           (weapon == WEAPON_MAGIC_SLAYER || a->flags & UF_MAGIC_WEAPON);
}

/* Effective value (K6.1): the basis divided by floor(ConMax/Con), at
 * least 1. */
static uint8_t effective(const Unit *u, uint16_t basis)
{
    uint16_t v = (uint16_t)(basis / world_con_factor(u));
    return v < 1 ? 1 : (v > 255 ? 255 : (uint8_t)v);
}

uint8_t items_combat_bonus(const Unit *u)
{
    uint8_t weapon = items_in_use_weapon(u);
    uint16_t v;
    if (weapon == WEAPON_NONE || !(CREATURES[ride_actor_kind(u)].flags & CF_WEAPONS))
        return 0;
    v = magic_scale(u, weapon, WEAPONS[weapon].combat);
    return v > 255 ? 255 : (uint8_t)v;
}

uint8_t items_defence_bonus(const Unit *u)
{
    uint8_t i, best = 0;
    if (!(CREATURES[ride_actor_kind(u)].flags & CF_WEAPONS))
        return 0;
    /* the best defence of any carried object counts, not the sum (K6.1) */
    for (i = 0; i < u->item_count; i++) {
        uint8_t wp = OBJECTS[u->items[i]].weapon;
        if (wp != WEAPON_NONE) {
            uint16_t d = magic_scale(u, wp, WEAPONS[wp].defence);
            if (d > best)
                best = d > 255 ? 255 : (uint8_t)d;
        }
    }
    return best;
}

uint8_t items_combat(const World *w, uint8_t unit)
{
    const Unit *u;
    uint16_t com;
    if (unit >= w->unit_count)
        return 0;
    u = &w->units[unit];
    com = u->com;
    if (effect_active(u, EFF_STRENGTH))
        com = (uint16_t)(com + effect_power(u, EFF_STRENGTH));
    com = (uint16_t)(com + items_combat_bonus(u));
    return effective(u, com);
}

uint8_t items_defence(const World *w, uint8_t unit)
{
    const Unit *u;
    uint16_t def;
    if (unit >= w->unit_count)
        return 0;
    u = &w->units[unit];
    def = (uint16_t)(u->def + items_defence_bonus(u));
    if (effect_active(u, EFF_SHIELD))
        def = (uint16_t)(def + effect_power(u, EFF_SHIELD));
    if (effect_active(u, EFF_PROTECT))
        def = (uint16_t)(def + effect_power(u, EFF_PROTECT));
    return effective(u, def);
}

/* Magic resistance (K2): the creature's own value. Protective spells and
 * potions work on Defence since D67; resistance only matters for Curse,
 * Subversion and Magic Attack. */
uint8_t items_magic_res(const World *w, uint8_t unit)
{
    if (unit >= w->unit_count)
        return 0;
    return w->units[unit].mr;
}

bool items_eat(World *w, uint8_t unit)
{
    Unit *u;
    uint8_t kind;
    if (unit >= w->unit_count)
        return false;
    u = &w->units[unit];
    if (u->in_use == NO_ITEM || u->in_use >= u->item_count)
        return false;
    kind = u->items[u->in_use];
    if (OBJECTS[kind].category != OC_FOOD)
        return false;
    if (!world_can_pay(w, unit, ACT_EAT))
        return false;
    world_pay(w, unit, ACT_EAT);
    {
        uint8_t heal = OBJECTS[kind].eat_con;
        uint8_t mana = OBJECTS[kind].eat_mana;
        if (heal && u->con < u->con_max)
            u->con = (uint8_t)(u->con + heal > u->con_max ? u->con_max
                                                          : u->con + heal);
        if (heal && u->sta < u->sta_max) {   /* PM: food gives 4x its Con as stamina */
            uint16_t sta = (uint16_t)(u->sta + 4 * heal);
            u->sta = sta > u->sta_max ? u->sta_max : (uint8_t)sta;
        }
        if (mana && u->mana < u->mana_max)
            u->mana = (uint8_t)(u->mana + mana > u->mana_max ? u->mana_max
                                                             : u->mana + mana);
    }
    u->items[u->in_use] = u->items[u->item_count - 1];   /* consumed */
    u->item_count--;
    u->in_use = NO_ITEM;
    return true;
}

const char *items_read(World *w, uint8_t unit)
{
    Unit *u;
    uint8_t kind;
    if (unit >= w->unit_count)
        return NULL;
    u = &w->units[unit];
    if (u->in_use == NO_ITEM || u->in_use >= u->item_count)
        return NULL;
    kind = u->items[u->in_use];
    if (OBJECTS[kind].category != OC_SCROLL)
        return NULL;
    if (!world_can_pay(w, unit, ACT_READ))
        return NULL;
    world_pay(w, unit, ACT_READ);
    u->items[u->in_use] = u->items[u->item_count - 1];   /* read away */
    u->item_count--;
    u->in_use = NO_ITEM;
    return "Gelesen: Das Portal kommt erst spaet.";   /* until scenarios carry texts */
}

/* Chest loot table (own values, D7): every chest holds one treasure. */
/* Chests hold most of the treasure and the better weapons (D35). */
static const uint8_t CHEST_LOOT[] = {
    OBJ_GOLD, OBJ_GOLD, OBJ_GOLD, OBJ_EMERALD, OBJ_EMERALD, OBJ_RUBY,
    OBJ_WAND, OBJ_RUNE_STONE, OBJ_DIAMOND,
    OBJ_SWORD, OBJ_AXE, OBJ_SPEAR, OBJ_BOW, OBJ_SHIELD, OBJ_KNIFE,
    OBJ_NINJA_STAR, OBJ_VIAL_HEALING, OBJ_VIAL_STRENGTH, OBJ_SCROLL,
    OBJ_SLAYER,                          /* rare: one entry in 20 */
};

bool items_open_chest(World *w, Rng *rng, uint8_t unit, int16_t x, int16_t y)
{
    Unit *u;
    uint8_t i, ap, kind;
    if (unit >= w->unit_count || !world_wrap(w, &x, &y))
        return false;
    if (w->feature[y][x] != FE_CHEST && w->feature[y][x] != FE_CHEST_FREE)
        return false;
    u = &w->units[unit];
    kind = NO_ITEM;
    if (w->feature[y][x] == FE_CHEST)       /* locked (C1): a key opens cheaply */
        for (i = 0; i < u->item_count; i++)
            if (u->items[i] == OBJ_CHEST_KEY)
                kind = i;
    if (w->feature[y][x] == FE_CHEST_FREE)
        ap = ACTIONS[ACT_OPEN_CHEST].ap;    /* no lock: just lift the lid */
    else
        ap = kind != NO_ITEM ? ACTIONS[ACT_UNLOCK].ap
                             : (uint8_t)(ACTIONS[ACT_OPEN_CHEST].ap * 3);
    if (world_pool_ap(w, unit, ACT_OPEN_CHEST) < ap)
        return false;
    if (!(CREATURES[ride_actor_kind(u)].flags & CF_USE))
        return false;                    /* hands needed */
    world_spend_ap_for(w, unit, ACT_OPEN_CHEST, ap);
    if (kind != NO_ITEM) {               /* keys vanish after use (GDD 8) */
        u->items[kind] = u->items[u->item_count - 1];
        u->item_count--;
    }
    w->feature[y][x] = FE_NONE;          /* empty box stays as rubble-less */
    world_map_changed(w);
    world_poke(w, x, y);
    if (w->object_count < MAX_OBJECTS) { /* the loot drops */
        uint8_t loot = CHEST_LOOT[rng_range(rng, (uint16_t)(sizeof CHEST_LOOT))];
        w->objects[w->object_count].x = (uint8_t)x;
        w->objects[w->object_count].y = (uint8_t)y;
        w->objects[w->object_count].tile = OBJECTS[loot].tile;
        w->object_count++;
    }
    return true;
}
