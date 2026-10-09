#include "fx.h"

#include <agon/keyboard.h>
#include <agon/vdp.h>
#include <stdio.h>
#include <string.h>

#include "../core/colors.h"
#include "../core/events.h"
#include "../core/gen/data.h"
#include "../core/gen/tiles.h"
#include "../core/view.h"
#include "music.h"
#include "render.h"
#include "sound.h"

#define FX_FRAME_CS 8    /* one effect frame: 80 ms */
#define FX_TOUCHED 32

/* Sprite pool (ADR 0012): sprite 0 is the cursor, 1..FX_SPRITES belong
 * to the effects. Sprites float above the map, so nothing has to be
 * repainted after them - unlike the tile overlays. */
#define FX_SPRITES 8
#define SPR_MAIN 1           /* projectile / spell effect */
#define SPR_TRAIL 2          /* lightning trail: 2, 3 */
#define SPR_DIGIT 4          /* damage digits: 4..8 */
#define MAP_EDGE (VIEW_W * TILE_PX - TILE_PX)   /* last sprite x/y inside */

static bool fx_enabled = true;
bool fx_glide_on = true;

void fx_set_enabled(bool on)
{
    fx_enabled = on;
}

void fx_init(void)
{
    uint8_t s;
    for (s = 1; s <= FX_SPRITES; s++) {
        vdp_select_sprite(s);
        vdp_clear_sprite();
        vdp_adv_add_sprite_bitmap(render_tile_buffer(T_FX_SPARK_0));
        vdp_hide_sprite();
    }
    vdp_activate_sprites(FX_SPRITES + 1);
    vdp_refresh_sprites();
}

/* Key releases that arrived during a show. Presses are dropped (K5: no
 * ghost input), but a dropped release would leave the arrow "held" in the
 * chord logic and the unit would run on by itself (a tapped arrow is
 * released within the 80 ms glide). The main loop collects them with
 * fx_take_release(). */
#define RELEASES 8
static uint8_t release_vkey[RELEASES];
static uint8_t release_n;

uint8_t fx_take_release(void)
{
    uint8_t v;
    if (!release_n)
        return 0;
    v = release_vkey[0];
    release_n--;
    memmove(release_vkey, release_vkey + 1, release_n);
    return v;
}

/* Wait n centiseconds (the clock ticks in steps of 2); queued presses are
 * dropped, releases kept for the chord logic. */
static void wait_cs(uint8_t n)
{
    struct keyboard_event_t e;
    uint32_t until = getsysvar_time() + n;
    while ((int32_t)(getsysvar_time() - until) < 0) {
        audio_poll();                     /* effects are step lists */
        while (kbuf_poll_event(&e))
            if (!e.isdown && release_n < RELEASES)
                release_vkey[release_n++] = (uint8_t)e.vkey;
    }
}

static void wait_frames(uint8_t n)
{
    wait_cs((uint8_t)(n * FX_FRAME_CS));
}

/* ---------- sprites ---------- */

static void spr_set(uint8_t s, const uint16_t *tiles, uint8_t n)
{
    uint8_t i;
    vdp_select_sprite(s);
    vdp_clear_sprite();
    for (i = 0; i < n; i++)
        vdp_adv_add_sprite_bitmap(render_tile_buffer(tiles[i]));
}

/* Place sprite s at a pixel position; outside the map window it hides
 * (it must never float over the panel). */
static void spr_at(uint8_t s, int16_t x, int16_t y)
{
    vdp_select_sprite(s);
    if (x < 0 || y < 0 || x > MAP_EDGE || y > MAP_EDGE) {
        vdp_hide_sprite();
        return;
    }
    vdp_move_sprite_to(x, y);
    vdp_show_sprite();
}

static void spr_hide_all(void)
{
    uint8_t s;
    for (s = 1; s <= FX_SPRITES; s++) {
        vdp_select_sprite(s);
        vdp_hide_sprite();
    }
    vdp_refresh_sprites();
}

/* ---------- what a spell looks and sounds like ---------- */

/* The sound of a spell (event kind = spell id). A switch, not an ||
 * chain over enums (ez80-clang, T7). */
static uint8_t spell_sound(uint8_t spell)
{
    if (spell >= SPELL_COUNT)
        return SND_SPELL;
    switch (spell) {
    case SP_MAGIC_BOLT:
        return SND_BOLT;
    case SP_MAGIC_LIGHTNING:
        return SND_LIGHTNING;
    case SP_TELEPORT:
        return SND_TELEPORT;
    case SP_CURSE:
    case SP_SUBVERSION:
        return SND_CURSE;
    default:
        break;
    }
    if (SPELLS[spell].category == SPC_SUMMON)
        return SND_SUMMON;
    if (SPELLS[spell].category == SPC_POTION)
        return SND_DRINK;                 /* the cauldron bubbles */
    return SND_SPELL;
}

typedef enum { LOOK_NONE, LOOK_SUMMON, LOOK_TELEPORT, LOOK_SHIELD,
               LOOK_CURSE, LOOK_BUBBLE, LOOK_SPARK } SpellLook;

static uint8_t spell_look(uint8_t spell)
{
    if (spell >= SPELL_COUNT)
        return LOOK_SPARK;
    switch (spell) {
    case SP_MAGIC_BOLT:
    case SP_MAGIC_LIGHTNING:
        return LOOK_NONE;                 /* the projectile shows it */
    case SP_TELEPORT:
        return LOOK_TELEPORT;
    case SP_MAGIC_SHIELD:
    case SP_ENCHANT:
        return LOOK_SHIELD;
    case SP_CURSE:
    case SP_SUBVERSION:
        return LOOK_CURSE;
    default:
        break;
    }
    if (SPELLS[spell].category == SPC_SUMMON)
        return LOOK_SUMMON;
    if (SPELLS[spell].category == SPC_POTION)
        return LOOK_BUBBLE;
    return LOOK_SPARK;
}

static void spell_show(uint8_t look, int16_t px, int16_t py)
{
    static const uint16_t SUMMON[3] = {T_FX_SUMMON_0, T_FX_SUMMON_1, T_FX_SUMMON_2};
    static const uint16_t TELE[2] = {T_FX_TELE_0, T_FX_TELE_1};
    static const uint16_t BUBBLE[2] = {T_FX_BUBBLE_0, T_FX_BUBBLE_1};
    static const uint16_t SPARK[2] = {T_FX_SPARK_0, T_FX_SPARK_1};
    static const uint16_t SHIELD[1] = {T_FX_SHIELD};
    static const uint16_t CURSE[1] = {T_FX_CURSE};
    const uint16_t *frames;
    uint8_t n, steps, i;
    int8_t rise = 0;
    switch (look) {
    case LOOK_SUMMON: frames = SUMMON; n = 3; steps = 6; break;
    case LOOK_TELEPORT: frames = TELE; n = 2; steps = 8; break;
    case LOOK_SHIELD: frames = SHIELD; n = 1; steps = 6; break;
    case LOOK_CURSE: frames = CURSE; n = 1; steps = 8; rise = 1; break;
    case LOOK_BUBBLE: frames = BUBBLE; n = 2; steps = 6; break;
    case LOOK_SPARK: frames = SPARK; n = 2; steps = 6; break;
    default: return;
    }
    spr_set(SPR_MAIN, frames, n);
    for (i = 0; i < steps; i++) {
        vdp_select_sprite(SPR_MAIN);
        vdp_nth_sprite_frame((uint8_t)((n == 3 ? i / 2 : i) % n));
        spr_at(SPR_MAIN, px, (int16_t)(py - (rise ? i * 2 : 0)));
        vdp_refresh_sprites();
        wait_cs(6);
    }
    spr_hide_all();
}

/* ---------- projectiles ---------- */

/* 8 directions, 0 = north, clockwise - from the sign of the offset. */
static uint8_t arrow_dir(int16_t dx, int16_t dy)
{
    int16_t ax = dx < 0 ? (int16_t)-dx : dx, ay = dy < 0 ? (int16_t)-dy : dy;
    bool diag = ax * 2 >= ay && ay * 2 >= ax;
    if (diag) {
        if (dx > 0)
            return dy < 0 ? 1 : 3;
        return dy < 0 ? 7 : 5;
    }
    if (ax > ay)
        return dx > 0 ? 2 : 6;
    return dy < 0 ? 0 : 4;
}

static void fly(uint8_t kind, int16_t fx_, int16_t fy, int16_t tx, int16_t ty)
{
    static const uint16_t BOLT[2] = {T_FX_BOLT_0, T_FX_BOLT_1};
    static const uint16_t LIGHT[2] = {T_FX_LIGHTNING_0, T_FX_LIGHTNING_1};
    static const uint16_t SPIN[2] = {T_FX_SPIN_0, T_FX_SPIN_1};
    uint16_t arrow;
    int16_t x0 = (int16_t)(fx_ * TILE_PX), y0 = (int16_t)(fy * TILE_PX);
    int16_t dx = (int16_t)((tx - fx_) * TILE_PX), dy = (int16_t)((ty - fy) * TILE_PX);
    int16_t ax = dx < 0 ? (int16_t)-dx : dx, ay = dy < 0 ? (int16_t)-dy : dy;
    int16_t steps = (int16_t)((ax > ay ? ax : ay) / 8), i;
    if (steps < 3)
        steps = 3;
    switch (kind) {
    case PJ_LIGHTNING:
        spr_set(SPR_MAIN, LIGHT, 2);
        spr_set(SPR_TRAIL, LIGHT, 2);
        spr_set(SPR_TRAIL + 1, LIGHT, 2);
        break;
    case PJ_ARROW:
        arrow = (uint16_t)(T_FX_ARROW_0 + arrow_dir(dx, dy));
        spr_set(SPR_MAIN, &arrow, 1);
        break;
    case PJ_THROWN:
        spr_set(SPR_MAIN, SPIN, 2);
        break;
    default:
        spr_set(SPR_MAIN, BOLT, 2);
        break;
    }
    for (i = 0; i <= steps; i++) {
        int16_t x = (int16_t)(x0 + (int32_t)dx * i / steps);
        int16_t y = (int16_t)(y0 + (int32_t)dy * i / steps);
        if (kind != PJ_ARROW && (i & 1) == 0) {
            vdp_select_sprite(SPR_MAIN);
            vdp_next_sprite_frame();
        }
        spr_at(SPR_MAIN, x, y);
        if (kind == PJ_LIGHTNING) {       /* two sparks trail the head */
            uint8_t t;
            for (t = 0; t < 2; t++) {
                int16_t j = (int16_t)(i - 2 * (t + 1));
                if (j < 0)
                    j = 0;
                spr_at((uint8_t)(SPR_TRAIL + t),
                       (int16_t)(x0 + (int32_t)dx * j / steps),
                       (int16_t)(y0 + (int32_t)dy * j / steps));
            }
        }
        vdp_refresh_sprites();
        wait_cs(2);
    }
    spr_hide_all();
}

/* ---------- gliding units ---------- */

void fx_glide(uint16_t tile, int16_t vx0, int16_t vy0, int16_t vx1, int16_t vy1)
{
    int16_t x0 = (int16_t)(vx0 * TILE_PX), y0 = (int16_t)(vy0 * TILE_PX);
    int16_t dx = (int16_t)((vx1 - vx0) * TILE_PX), dy = (int16_t)((vy1 - vy0) * TILE_PX);
    uint8_t i;
    if (!fx_enabled || !fx_glide_on)
        return;
    spr_set(SPR_MAIN, &tile, 1);
    for (i = 1; i <= 4; i++) {            /* 4 steps, 80 ms */
        spr_at(SPR_MAIN, (int16_t)(x0 + dx * i / 4), (int16_t)(y0 + dy * i / 4));
        vdp_refresh_sprites();
        wait_cs(2);
    }
    spr_hide_all();
}

/* ---------- damage numbers ---------- */

/* "-12" rising from the field, digits as sprites: nothing to repaint. */
static void damage_number(int16_t px, int16_t py, uint8_t value)
{
    uint16_t glyph[5];
    uint8_t n = 0, k, i;
    char buf[4];
    snprintf(buf, sizeof buf, "%u", value);
    glyph[n++] = T_ICON_DMG_MINUS;
    for (k = 0; buf[k] && n < 4; k++)
        glyph[n++] = (uint16_t)(T_ICON_DMG_0 + (buf[k] - '0'));
    for (k = 0; k < n; k++)
        spr_set((uint8_t)(SPR_DIGIT + k), &glyph[k], 1);
    px = (int16_t)(px + TILE_PX / 2 - n * 3);     /* centred, 6 px apart */
    if (px + n * 6 > MAP_EDGE + TILE_PX - 8)
        px = (int16_t)(MAP_EDGE + TILE_PX - 8 - n * 6);
    if (px < 0)
        px = 0;
    for (i = 0; i < 8; i++) {
        int16_t y = (int16_t)(py + 4 - i * 2);
        if (y < 0)
            y = 0;
        for (k = 0; k < n; k++) {
            vdp_select_sprite((uint8_t)(SPR_DIGIT + k));
            vdp_move_sprite_to(px + k * 6, y);
            vdp_show_sprite();
        }
        vdp_refresh_sprites();
        wait_cs(4);
    }
    spr_hide_all();
}

/* ---------- unseen phases: only sounds ---------- */

void fx_pause(uint8_t cs)
{
    wait_cs(cs);
}

static void (*event_hook)(const GameEvent *);

void fx_set_event_hook(void (*fn)(const GameEvent *e))
{
    event_hook = fn;
}

static void tell_hook(const GameEvent *ev, uint8_t n)
{
    uint8_t i;
    if (event_hook)
        for (i = 0; i < n; i++)
            event_hook(&ev[i]);
}

void fx_drain_sounds(void)
{
    static GameEvent ev[EVENT_RING];
    uint8_t n = events_drain(ev, EVENT_RING), i;
    tell_hook(ev, n);
    if (!fx_enabled)
        return;
    for (i = 0; i < n; i++) {
        uint8_t snd;
        switch (ev[i].type) {
        case EV_SWING: snd = SND_SWING; break;
        case EV_HIT: snd = SND_HIT; break;
        case EV_MISS: snd = SND_MISS; break;
        case EV_DEATH: snd = SND_DEATH; break;
        case EV_SPELL: snd = spell_sound(ev[i].kind); break;
        case EV_SMASH: snd = SND_SMASH; break;
        case EV_PROJECTILE:
            snd = ev[i].kind == PJ_ARROW ? SND_BOW
                : ev[i].kind == PJ_THROWN ? SND_THROW : 0xFF;
            break;
        default: snd = 0xFF; break;
        }
        if (snd == 0xFF)
            continue;
        sound_play(snd);
        wait_cs(snd == SND_DEATH || snd == SND_SUMMON ? 40 : 22);
    }
}

/* ---------- the show ---------- */

/* World -> view field of an event (may lie outside the window). */
static bool event_view(const World *w, int16_t wx, int16_t wy,
                       int16_t *vx, int16_t *vy)
{
    world_delta(w, view_origin_x(), view_origin_y(), wx, wy, vx, vy);
    return *vx >= 0 && *vy >= 0 && *vx < VIEW_W && *vy < VIEW_H;
}

static void draw_overlay(int16_t vx, int16_t vy, uint16_t tile)
{
    render_draw_tile(tile, vx * TILE_PX, vy * TILE_PX);
}

/* Fields painted over by the tile overlays; repainted at the end. */
static int16_t touched[FX_TOUCHED][2];
static uint8_t touched_n;

static void touch(int16_t vx, int16_t vy)
{
    if (touched_n < FX_TOUCHED) {
        touched[touched_n][0] = vx;
        touched[touched_n][1] = vy;
        touched_n++;
    }
}

bool fx_drain_play(World *w, const Sight *s)
{
    static GameEvent ev[EVENT_RING];
    uint8_t n, i;
    bool fight = false;

    n = events_drain(ev, EVENT_RING);
    if (!n)
        return false;
    tell_hook(ev, n);
    if (!fx_enabled)
        return false;                    /* scripted run: drop the show */
    touched_n = 0;
    for (i = 0; i < n; i++) {
        const GameEvent *e = &ev[i];
        int16_t vx, vy;
        bool inside = event_view(w, e->x, e->y, &vx, &vy);
        if (e->type == EV_PROJECTILE) {  /* may start outside the window */
            int16_t tx = (int16_t)(vx + (int8_t)e->a);
            int16_t ty = (int16_t)(vy + (int8_t)e->b);
            int16_t wx = (int16_t)(e->x + (int8_t)e->a);
            int16_t wy = (int16_t)(e->y + (int8_t)e->b);
            world_wrap(w, &wx, &wy);
            if (s && !sight_visible(s, w, e->x, e->y) &&
                !sight_visible(s, w, wx, wy))
                continue;                  /* entirely in the fog */
            if (e->kind == PJ_ARROW)       /* bolts sounded with the spell */
                sound_play(SND_BOW);
            else if (e->kind == PJ_THROWN)
                sound_play(SND_THROW);
            fly(e->kind, vx, vy, tx, ty);
            continue;
        }
        if (!inside)
            continue;                      /* outside the 9x9 window */
        if (s && !sight_visible(s, w, e->x, e->y))
            continue;                      /* happens in the fog of war */
        if (e->type == EV_SWING || e->type == EV_HIT || e->type == EV_MISS ||
            e->type == EV_DEATH)
            fight = true;
        switch (e->type) {
        case EV_SWING:
            touch(vx, vy);
            sound_play(SND_SWING);
            draw_overlay(vx, vy, T_FX_SLASH);
            wait_frames(2);
            break;
        case EV_HIT:
            touch(vx, vy);
            sound_play(SND_HIT);
            draw_overlay(vx, vy, T_FX_HIT);
            damage_number((int16_t)(vx * TILE_PX), (int16_t)(vy * TILE_PX), e->a);
            break;
        case EV_MISS:
            touch(vx, vy);
            sound_play(SND_MISS);
            draw_overlay(vx, vy, T_FX_MISS);
            wait_frames(2);
            break;
        case EV_DEATH:
            touch(vx, vy);
            sound_play(SND_DEATH);
            draw_overlay(vx, vy, T_FX_DEATH_0);   /* white flash */
            wait_frames(1);
            draw_overlay(vx, vy, T_FX_DEATH_1);   /* the creature fades */
            wait_frames(2);
            draw_overlay(vx, vy, T_FX_DEATH_2);
            wait_frames(2);
            draw_overlay(vx, vy, T_FX_DEATH_3);   /* dust and a cross */
            wait_frames(2);
            draw_overlay(vx, vy, T_REMAINS);      /* what stays (D70) */
            wait_frames(6);
            break;
        case EV_SPELL:
            sound_play(spell_sound(e->kind));
            spell_show(spell_look(e->kind), (int16_t)(vx * TILE_PX),
                       (int16_t)(vy * TILE_PX));
            break;
        case EV_SMASH:
            touch(vx, vy);
            sound_play(SND_SMASH);
            draw_overlay(vx, vy, T_FX_DEATH_3);   /* debris cloud */
            wait_frames(2);
            break;
        default:
            break;                        /* EV_WOUND rides along with EV_HIT */
        }
    }
    /* repaint every field a tile overlay painted over */
    for (i = 0; i < touched_n; i++)
        view_mark_dirty((uint8_t)touched[i][0], (uint8_t)touched[i][1]);
    if (touched_n)
        render_fields();
    return fight;
}
