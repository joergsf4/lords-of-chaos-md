/*
 * Carried objects (GDD 8): every unit has a small inventory with one
 * object "in use"; weapons give combat bonuses (the shield always, when
 * carried), treasures carry victory points through the portal (GDD 9).
 * Action costs come from data/actions.csv.
 */
#ifndef LOC_ITEMS_H
#define LOC_ITEMS_H

#include <stdbool.h>
#include <stdint.h>

#include "rng.h"
#include "world.h"

#define UNIT_ITEMS 6
#define NO_ITEM 0xFF
/* Attack value of a bow shot (K6.4); an enchanted bow doubles it. */
#define BOW_ATTACK 15

/* Total weight carried (against the creature's carry limit). */
uint8_t items_weight(const World *w, uint8_t unit);
/* Kind of the topmost ground object at (x, y), NO_ITEM when none. */
uint8_t items_kind_at(const World *w, int16_t x, int16_t y);
/* Pick up the object under the unit (ACT_PICK_UP): weight limit applies. */
bool items_pick_up(World *w, uint8_t unit);
/* Pick up ground object obj from the own field or a neighbour (same
 * checks: room, weight, AP, no full cauldron). */
bool items_pick_up_object(World *w, uint8_t unit, uint8_t obj);
/* Object kind of a ground tile, NO_ITEM when none. */
uint8_t items_kind_of_tile(uint16_t tile);
/* Drop the object in use onto the unit's field (ACT_DROP). */
bool items_drop(World *w, uint8_t unit);
/* Wield pack slot `slot` (NO_ITEM = bare hands), ACT_CHANGE. False: no such
 * slot, the shield (it never takes the hand, D28), already in hand, no AP. */
bool items_wield(World *w, uint8_t unit, uint8_t slot);
/* What the carried objects add to the bars, before the constitution factor
 * (the same additions items_combat / items_defence use): the weapon in
 * hand's Combat, the best carried object's Defence. 0 for creatures that
 * cannot use weapons. */
uint8_t items_combat_bonus(const Unit *u);
uint8_t items_defence_bonus(const Unit *u);
/* Wield the next carried object (ACT_CHANGE); empty hands are allowed. */
bool items_cycle(World *w, uint8_t unit);
/* Throw the object in use along a direction: it flies up to 6 fields,
 * stops at terrain or a unit (thrown damage, GDD 6.1) and lands on the
 * last free field. ACT_THROW. */
/* D59: a friend hit by a thrown object catches it into his pack - when
 * there is room and he can carry the weight. False: it falls down. */
bool items_catch(World *w, uint8_t unit, uint8_t kind);
bool items_throw(World *w, Rng *rng, uint8_t unit, int8_t dx, int8_t dy);
/* Throw range in distance units (K6.4): min(36, 2 Combat_eff / weight + 5). */
uint8_t items_throw_range(const World *w, uint8_t unit, uint8_t weight);
/* Can the unit fire at all: a bow in hand, or a dragon's breath (K6.4)? */
bool items_can_fire(const World *w, uint8_t unit);
/* Its reach in distance units: bow 16, enchanted bow 22, dragon 12. */
uint8_t items_fire_range(const World *w, uint8_t unit);
/* Fire the bow in use or the dragon's breath at a field (ACT_FIRE): bow
 * 16 / 22 units with attack 15 / 30, dragon 12 units with attack 35 and an
 * ignition of the target field; line of sight, ground and air targets,
 * Defence counts. */
bool items_fire(World *w, Rng *rng, uint8_t unit, int16_t tx, int16_t ty,
                uint8_t *damage);
/* Effective values (K6.1): Combat plus the weapon in use, Defence plus the
 * best defence of any carried object (not added up), both divided by the
 * constitution factor, at least 1. Creatures that cannot use weapons get no
 * item bonus. */
uint8_t items_combat(const World *w, uint8_t unit);
uint8_t items_defence(const World *w, uint8_t unit);
/* Defence WITHOUT the carried shield: what spell attacks roll against
 * (D32) - a magic bolt cuts through armour; melee, bow and thrown
 * weapons still meet the full defence. */
/* Magic resistance incl. the protective spells (D40). */
uint8_t items_magic_res(const World *w, uint8_t unit);
/* Weapon of the object in use, WEAPON_NONE without one. */
uint8_t items_in_use_weapon(const Unit *u);
/* Can the attacker wound the (possibly undead) defender (GDD 4.2)?
 * Undead attackers, the Magic Slayer and enchanted weapons do; spells
 * bypass the check entirely. */
bool items_can_harm_undead(const World *w, uint8_t attacker, uint8_t defender);
/* EAT the object in use (GDD 8): food heals Con or Mana, ACT_EAT. */
bool items_eat(World *w, uint8_t unit);
/* READ the scroll in use (GDD 8): a hint line, ACT_READ, scroll gone. */
const char *items_read(World *w, uint8_t unit);
/* Open the chest at (x, y): a free chest (C1) just opens; a locked one
 * is unlocked by a carried chest key (key vanishes, GDD 8), otherwise
 * it is pried open at triple the AP.
 * The chest drops a random treasure and disappears. */
bool items_open_chest(World *w, Rng *rng, uint8_t unit, int16_t x, int16_t y);

#endif
