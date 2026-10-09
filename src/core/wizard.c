#include "wizard.h"

#include "items.h"

#include <stdio.h>
#include <string.h>

Wizard wizard_slots[WIZARD_SLOTS];

/* Designer (K3.2, K3.3, D67): a fresh wizard starts at the values below and
 * spends 600 XP. A point of an attribute costs floor(current value /
 * divisor) XP; a spell level from L to L+1 costs xp_base + xp_step * L. */
#define START_COM 5
#define START_DEF 5
#define START_MR 70
#define START_CON 34
#define START_STA 34
#define START_MANA 80
#define START_AP 34
#define START_XP 600
static const uint8_t ATTR_DIV[WA_COUNT] = {2, 2, 16, 10, 8};
static const uint8_t ATTR_MAX[WA_COUNT] = {30, 30, 100, 90, 90};
#define MANA_DIV 10
#define MANA_MAX 200
#define AP_DIV 4
#define AP_MAX 40
#define SPELL_BUY_MAX 8

uint8_t wizard_attr(const Wizard *w, WizardAttr a)
{
    switch (a) {
    case WA_COMBAT:
        return w->com;
    case WA_DEFENCE:
        return w->def;
    case WA_MAGIC_RES:
        return w->mr;
    case WA_CONSTITUTION:
        return w->con;
    default:
        return w->sta;
    }
}

uint8_t wizard_attr_cost(WizardAttr a, uint8_t current)
{
    return a < WA_COUNT ? (uint8_t)(current / ATTR_DIV[a]) : 9;
}

uint8_t wizard_attr_max(WizardAttr a)
{
    return a < WA_COUNT ? ATTR_MAX[a] : 100;
}

bool wizard_raise(Wizard *w, WizardAttr a)
{
    uint8_t cur = wizard_attr(w, a);
    uint8_t cost = wizard_attr_cost(a, cur);
    uint8_t *target = a == WA_COMBAT ? &w->com
                    : a == WA_DEFENCE ? &w->def
                    : a == WA_MAGIC_RES ? &w->mr
                    : a == WA_CONSTITUTION ? &w->con : &w->sta;
    if (cur >= wizard_attr_max(a) || w->xp < cost)
        return false;
    w->xp = (uint16_t)(w->xp - cost);
    *target = (uint8_t)(cur + 1);
    return true;
}

bool wizard_lower(Wizard *w, WizardAttr a)
{
    uint8_t cur = wizard_attr(w, a);
    uint8_t base = a == WA_COMBAT ? w->base_com
                 : a == WA_DEFENCE ? w->base_def
                 : a == WA_MAGIC_RES ? w->base_mr
                 : a == WA_CONSTITUTION ? w->base_con : w->base_sta;
    uint8_t *target = a == WA_COMBAT ? &w->com
                    : a == WA_DEFENCE ? &w->def
                    : a == WA_MAGIC_RES ? &w->mr
                    : a == WA_CONSTITUTION ? &w->con : &w->sta;
    if (cur <= base)
        return false;                    /* never below the start value */
    *target = (uint8_t)(cur - 1);
    w->xp = (uint16_t)(w->xp + wizard_attr_cost(a, *target));   /* full refund */
    return true;
}

/* Buyable spell levels (K3.3): from level L to L+1 it costs xp_base +
 * xp_step * L, level 8 is the cap. Spells without a price (the bomb) are
 * not for sale. */
uint16_t wizard_spell_next_cost(const Wizard *w, uint8_t spell)
{
    if (spell >= SPELL_COUNT || !SPELLS[spell].xp_base)
        return 0;
    return (uint16_t)(SPELLS[spell].xp_base + SPELLS[spell].xp_step * w->book.level[spell]);
}

bool wizard_spell_raise(Wizard *w, uint8_t spell)
{
    uint16_t cost;
    if (spell >= SPELL_COUNT)
        return false;
    cost = wizard_spell_next_cost(w, spell);
    if (!cost || w->book.level[spell] >= SPELL_BUY_MAX || w->xp < cost)
        return false;
    w->xp = (uint16_t)(w->xp - cost);
    w->book.level[spell]++;
    return true;
}

bool wizard_spell_lower(Wizard *w, uint8_t spell)
{
    if (spell >= SPELL_COUNT || w->book.level[spell] == 0)
        return false;
    if (!SPELLS[spell].xp_base)
        return false;   /* not for sale: nothing was ever paid */
    if (w->book.level[spell] <= w->base_book.level[spell])
        return false;   /* pre-given starting level: no refund for it */
    w->book.level[spell]--;
    w->xp = (uint16_t)(w->xp + wizard_spell_next_cost(w, spell));   /* full refund */
    return true;
}

uint8_t wizard_mana_cost(const Wizard *w)
{
    return (uint8_t)(w->mana_max / MANA_DIV);
}

bool wizard_mana_raise(Wizard *w)
{
    uint8_t cost = wizard_mana_cost(w);
    if (w->mana_max >= MANA_MAX || w->xp < cost)
        return false;
    w->xp = (uint16_t)(w->xp - cost);
    w->mana_max++;
    return true;
}

bool wizard_mana_lower(Wizard *w)
{
    if (w->mana_max <= START_MANA)
        return false;                    /* never below the start value */
    w->mana_max--;
    w->xp = (uint16_t)(w->xp + wizard_mana_cost(w));   /* full refund */
    return true;
}

uint8_t wizard_ap_cost(const Wizard *w)
{
    return (uint8_t)(w->ap / AP_DIV);
}

bool wizard_ap_raise(Wizard *w)
{
    uint8_t cost = wizard_ap_cost(w);
    if (w->ap >= AP_MAX || w->xp < cost)
        return false;
    w->xp = (uint16_t)(w->xp - cost);
    w->ap++;
    return true;
}

bool wizard_ap_lower(Wizard *w)
{
    if (w->ap <= START_AP)
        return false;                    /* never below the minimum */
    w->ap--;
    w->xp = (uint16_t)(w->xp + wizard_ap_cost(w));     /* full refund */
    return true;
}

bool wizard_valid(const Wizard *w)
{
    uint8_t i;
    bool terminated = false;
    for (i = 0; i < WIZARD_NAME_MAX; i++)
        if (w->name[i] == '\0')
            terminated = true;
    if (!terminated || w->level < 1 || w->level > 8)
        return false;
    for (i = 0; i < WA_COUNT; i++) {
        uint8_t v = wizard_attr(w, (WizardAttr)i);
        if (v == 0 || v > wizard_attr_max((WizardAttr)i))
            return false;
    }
    if (w->base_com > w->com || w->base_def > w->def || w->base_mr > w->mr ||
        w->base_con > w->con || w->base_sta > w->sta)
        return false;
    if (w->mana_max < START_MANA || w->mana_max > MANA_MAX)
        return false;
    if (w->ap < START_AP || w->ap > AP_MAX)
        return false;
    for (i = 0; i < SPELL_COUNT; i++)
        if (w->book.level[i] > SPELL_MAX_LEVEL)
            return false;
    return true;
}

void wizard_slot_reset(uint8_t slot)
{
    Wizard *w;
    if (slot >= WIZARD_SLOTS)
        return;
    w = &wizard_slots[slot];
    memset(w, 0, sizeof *w);
    strcpy(w->name, "Zauberer");
    w->level = 1;
    w->com = w->base_com = START_COM;
    w->def = w->base_def = START_DEF;
    w->mr = w->base_mr = START_MR;
    w->con = w->base_con = START_CON;
    w->sta = w->base_sta = START_STA;
    w->mana_max = START_MANA;
    w->ap = START_AP;
    w->xp = START_XP;      /* creation budget for the designer */
    /* fresh wizards start with EMPTY books (user rule, 2026-10-04):
     * spells and creatures are bought in the designer or picked up as
     * scrolls; the standard set is offered as a temporary template at
     * scenario start (wizard_apply_standard_set). */
}

/* A sensible starting template that FITS the 600 XP budget (nothing above
 * 600, no cheating): the core spells and ten creatures (incl. a vampire and a giant) at the levels below,
 * then the rest of the XP goes round-robin into the attributes. Applied on
 * request at scenario start; never overwrites a designed book. */
void wizard_apply_standard_set(Wizard *w)
{
    static const struct { uint8_t spell, level; } SET[] = {
        {SP_MAGIC_BOLT, 4}, {SP_MAGIC_SHIELD, 3}, {SP_HEALING_POTION, 3},
        {SP_MAGIC_EYE, 2}, {SP_MAGIC_LIGHTNING, 1}, {SP_CURSE, 1},
        {SP_GIANT_BAT, 2}, {SP_GOBLIN, 2}, {SP_DWARF, 3}, {SP_UNICORN, 1},
        {SP_HARPY, 1}, {SP_ZOMBIE, 1}, {SP_GORILLA, 1}, {SP_GRYPHON, 1},
        {SP_VAMPIRE, 1}, {SP_GIANT, 1},        /* the two heavy hitters */
    };
    uint8_t i;
    bool bought;
    if (!w || w->book.level[SP_MAGIC_BOLT] != 0)
        return;                        /* already has spells: leave it */
    for (i = 0; i < sizeof SET / sizeof SET[0]; i++) {
        uint8_t n;
        for (n = 0; n < SET[i].level; n++)
            wizard_spell_raise(w, SET[i].spell);
    }
    do {                               /* attributes, round-robin, until the XP runs out */
        bought = false;
        bought |= wizard_raise(w, WA_COMBAT);
        bought |= wizard_raise(w, WA_DEFENCE);
        bought |= wizard_raise(w, WA_CONSTITUTION);
        bought |= wizard_raise(w, WA_STAMINA);
        if (w->com >= 12 && w->mr < 80)
            bought |= wizard_raise(w, WA_MAGIC_RES);
        if (w->com >= 14 && w->mana_max < 90)
            bought |= wizard_mana_raise(w);
    } while (bought && w->com < 24);
}

/* The original random wizard (K3.2): Combat and Defence 6, Magic Resistance
 * 90, every spell level RND(3) (0..2), no XP to spend. */
void wizard_slot_random(uint8_t slot, Rng *rng)
{
    uint8_t spell;
    Wizard *w;
    wizard_slot_reset(slot);
    w = &wizard_slots[slot];
    snprintf(w->name, sizeof w->name, "Zufall-%u", slot + 1);
    w->com = w->base_com = 6;
    w->def = w->base_def = 6;
    w->mr = w->base_mr = 90;
    w->level = 1;
    for (spell = 0; spell < SPELL_COUNT; spell++)
        w->book.level[spell] = SPELLS[spell].xp_base
                                   ? (uint8_t)rng_range(rng, 3) : 0;
    w->xp = 0;
}

void wizard_apply_to_world(const Wizard *w, World *world, uint8_t unit)
{
    Unit *u;
    if (unit >= world->unit_count)
        return;
    u = &world->units[unit];
    u->com = w->com;
    u->def = w->def;
    u->mr = w->mr;
    u->con = u->con_max = w->con;
    u->sta = u->sta_max = w->sta;
    u->mana = u->mana_max = w->mana_max;  /* raised with XP (F6) */
    u->ap = u->ap_max = world_scale_ap(world, w->ap);   /* designer AP, min 34 (F6), D71 */
    u->item_count = 0;                   /* F5: the wizard arrives unarmed */
    u->in_use = NO_ITEM;
}

/* The spellbook of the slot (the frontend owns the books[] array). */
const Spellbook *wizard_book(const Wizard *w)
{
    return &w->book;
}

void wizard_campaign_result(Wizard *w, uint16_t vp, uint8_t scenario)
{
    w->xp = (uint16_t)(w->xp + vp);      /* 1:1 (GDD 9) */
    if (scenario >= 1 && scenario <= 16 && !(w->scenarios_done & (1u << (scenario - 1)))) {
        w->scenarios_done |= (uint16_t)(1u << (scenario - 1));
        if (w->level < 8)
            w->level++;                  /* first clear: one level up */
    }
}
