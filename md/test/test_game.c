/*
 * Host tests of the Mega Drive game logic (md/src/game_md.c) against the real core.
 *
 * game_md.c is included, so its static functions can be driven directly, with the SGDK calls
 * replaced by md/test/stubs.c. Pad input goes through the same V-Blank event queue as on the
 * console (tap(), hold(), script). Run with md/test/run.sh.
 */
#define LOC_MD_FIXED_SEED 1
#include "../src/game_md.c"

#include <setjmp.h>
#include <stdlib.h>

#include "test.h"

int test_checks, test_failures;
static jmp_buf abort_test;
static const char *current_test = "";

void test_fail_now(const char *why)
{
    test_failures++;
    printf("  FAIL (%s) %s\n", current_test, why);
    longjmp(abort_test, 1);
}

void test_set_variants(const unsigned char *map, unsigned short len);
#include <setjmp.h>
extern jmp_buf *test_power_loss;
extern long test_power_after;
extern unsigned char test_overview[46 * 46];
extern int test_overview_w, test_overview_h, test_overview_ended;
extern u16 test_pad;
extern const u16 *test_script;
extern u32 test_vblanks;
extern u8 test_sram[32768];
extern bool test_sram_ro;
extern char test_msg[3][64], test_msg_log[64][64], test_text[28][40], test_text_ever[65536];
extern u8 test_msg_colour[3];
extern unsigned test_text_ever_n;
extern u16 test_msg_log_n;
extern u8 test_sounds[256], test_last_sound;
extern s16 test_cursor_x, test_cursor_y;
extern bool test_cursor_visible;

/* ---------- driving the game ---------- */

/* Feed a pad script: the pad starts released, so the first press of the script is an edge. */
#define SCRIPT(s) do { test_pad = 0; pad_last = 0; test_script = (s); } while (0)

static void tick(u16 n)
{
    while (n--) {
        play_frame();
        SYS_doVBlankProcess();
    }
}

static void tap(u16 buttons)
{
    test_script = NULL;
    test_pad = buttons;
    tick(2);
    test_pad = 0;
    tick(2);
}

static void hold(u16 buttons, u16 frames)
{
    test_script = NULL;
    test_pad = buttons;
    tick(frames);
    test_pad = 0;
    tick(2);
}

/* Press a button that opens a menu or page, then feed the rest of the input as a script:
 * the loop that now runs inside the game reads it frame by frame. */
static void tap_with(u16 button, const u16 *script)
{
    test_pad = button;
    tick(1);                            /* the V-Blank queues the press */
    test_script = script;
    tick(1);                            /* the menu runs in here and reads the script */
    test_script = NULL;
    test_pad = 0;
    tick(2);
}

static void clear_log(void)
{
    test_msg_log_n = 0;
    memset(test_msg_log, 0, sizeof test_msg_log);
    memset(test_sounds, 0, sizeof test_sounds);
    test_last_sound = 0xFF;
}

static bool said(const char *part)
{
    u16 i, n = test_msg_log_n < 64 ? test_msg_log_n : 64;
    for (i = 0; i < n; i++)
        if (strstr(test_msg_log[i], part))
            return TRUE;
    return strstr(test_msg[1], part) || strstr(test_msg[2], part) || strstr(test_msg[0], part);
}

/* was this text drawn at any time since the start of the test? */
static bool ever(const char *part)
{
    return strstr(test_text_ever, part) != NULL;
}

static bool page_has(const char *part)
{
    u8 r;
    for (r = 0; r < 28; r++)
        if (strstr(test_text[r], part))
            return TRUE;
    return FALSE;
}

/* A new game of scenario sc, ready for input. */
static void boot(u8 sc)
{
    memset(test_sram, 0, sizeof test_sram);
    test_sram_ro = FALSE;
    test_pad = 0;
    test_script = NULL;
    test_vblanks = 0;
    ev_head = ev_tail = 0;
    pad_last = 0;
    memset(test_text, 0, sizeof test_text);
    test_text_ever_n = 0;
    test_text_ever[0] = 0;
    SYS_setVBlankCallback(on_vblank);
    save_known_valid = FALSE;
    wizards_reset();
    wizard_apply_standard_set(&wizard_slots[0]);       /* the games below want a wizard with spells */
    sound_on = music_on = TRUE;
    play_init();
    new_game(&SCENARIOS[sc]);
    g_dbg.menus = 0;
    clear_log();
}

/* The wizard alone on flat grass at (10, 10), round 2 (movement allowed), full AP. */
static Unit *arena(void)
{
    u8 i, w;
    Unit *u;
    boot(0);
    w = active();
    for (i = world.unit_count; i-- > 0;)
        if (i != w)
            world_remove_unit(&world, i);
    turn_revalidate(&turns, &world);
    world.object_count = 0;
    world.cauldron_count = 0;
    memset(world.feature, FE_NONE, sizeof world.feature);
    memset(world.decor, DE_NONE, sizeof world.decor);
    memset(world.floor, FL_GRASS, sizeof world.floor);
    memset(world.roof, 0, sizeof world.roof);
    world.portal_x = world.portal_y = -1;
    world_map_changed(&world);
    u = &world.units[active()];
    u->x = u->y = 10;
    u->ap = u->ap_max;
    u->sta = u->sta_max;
    u->con = u->con_max;
    u->in_use = NO_ITEM;
    u->item_count = 0;
    turns.round1_lock = FALSE;
    update_sight();
    frame();
    clear_log();
    return &world.units[active()];
}

static Unit *me(void)
{
    return &world.units[active()];
}

/* An enemy next to the wizard (dx, dy), P2's, with plenty of constitution. */
static Unit *enemy_at(int8_t dx, int8_t dy, u8 kind)
{
    u8 i = world_spawn_unit(&world, OWN_P2, kind, me()->x + dx, me()->y + dy);
    CHECK(i != NO_UNIT, "enemy spawned");
    return &world.units[i];
}

static void put_object(u8 x, u8 y, u8 kind)
{
    LocObject *o = &world.objects[world.object_count++];
    o->x = x;
    o->y = y;
    o->tile = OBJECTS[kind].tile;
}

static u8 count_objects_at(u8 x, u8 y)
{
    u8 i, n = 0;
    for (i = 0; i < world.object_count; i++)
        if (world.objects[i].x == x && world.objects[i].y == y)
            n++;
    return n;
}

static void give(u8 kind)
{
    Unit *u = me();
    u->items[u->item_count++] = kind;
}

/* ---------- tests ---------- */

static void t_boot_scenarios(void)
{
    u8 sc;
    for (sc = 0; sc < 3; sc++) {
        boot(sc);
        if (getenv("TEST_VERBOSE"))
            printf("sc %d: units %d w %d h %d wrap %d round %d phase %d\n", sc, world.unit_count, world.w, world.h, world.wrap, turns.round, turns.phase);
        CHECK(world.unit_count > 1, "units were placed");
        CHECK_EQ(turns.round, 1, "starts in round 1");
        CHECK_EQ(turns.phase, OWN_P1, "the player's phase");
        CHECK_EQ(me()->owner, OWN_P1, "active unit is the player's");
        CHECK_EQ(ride_actor_kind(me()), CR_WIZARD, "the player is a wizard");
        CHECK(me()->ap > 0 && me()->mana > 0, "wizard has AP and mana");
        CHECK(books[OWN_P1].level[SP_MAGIC_BOLT] > 0, "stock wizard knows magic bolt");
        CHECK(world.w >= 9 && world.h >= 9, "map is at least a window");
        CHECK(strstr(test_msg[1], "Willkommen") != NULL, "welcome message");
        CHECK_EQ(g_dbg.screen, SCREEN_PLAY == g_dbg.screen ? SCREEN_PLAY : g_dbg.screen, "screen id sane");
    }
    boot(0);
    CHECK(cur_sc == &SCENARIOS[0], "scenario remembered");
    CHECK(world.w == 46 && world.wrap, "scenario 1 is the wrapping 46x46 land");
}

static void t_round1_lock(void)
{
    Unit *u;
    boot(0);
    u = me();
    {
        u8 x = u->x, y = u->y, ap = u->ap;
        tap(BUTTON_RIGHT);
        tap(BUTTON_DOWN);
        CHECK(u->x == x && u->y == y && u->ap == ap, "no movement in round 1 (PM 7)");
        CHECK(said("Runde 1"), "refusal message");
        CHECK(test_sounds[SND_ERROR] > 0, "error buzz");
    }
}

static void t_move_orthogonal(void)
{
    static const struct { u16 b; s8 dx, dy; } D[4] = {
        {BUTTON_UP, 0, -1}, {BUTTON_DOWN, 0, 1}, {BUTTON_LEFT, -1, 0}, {BUTTON_RIGHT, 1, 0}};
    u8 i;
    for (i = 0; i < 4; i++) {
        Unit *u = arena();
        u8 ap = u->ap;
        tap(D[i].b);
        CHECK_EQ(u->x, 10 + D[i].dx, "x after step");
        CHECK_EQ(u->y, 10 + D[i].dy, "y after step");
        CHECK(u->ap < ap, "a step costs AP");
        CHECK(test_sounds[SND_STEP] == 1, "step sound");
    }
}

static void t_move_diagonal_chord(void)
{
    Unit *u = arena();
    u8 ap = u->ap;
    /* two arrows within the chord window make a diagonal */
    test_pad = BUTTON_UP;
    tick(2);
    test_pad = BUTTON_UP | BUTTON_RIGHT;
    tick(2);
    test_pad = 0;
    tick(4);
    CHECK_EQ(u->x, 11, "diagonal: x");
    CHECK_EQ(u->y, 9, "diagonal: y");
    CHECK(ap - u->ap >= 1, "diagonal costs AP");
    /* a single arrow, released: one orthogonal step */
    u = arena();
    tap(BUTTON_LEFT);
    CHECK(u->x == 9 && u->y == 10, "single arrow: orthogonal");
}

static void t_move_held_repeats(void)
{
    Unit *u = arena();
    hold(BUTTON_RIGHT, 120);            /* 2 s: first step, then repeats */
    CHECK(u->x >= 12, "a held arrow repeats");
    CHECK(u->x <= 10 + 12, "but not wildly");
}

static void t_move_out_of_ap(void)
{
    Unit *u = arena();
    u8 x;
    u->ap = 0;
    x = u->x;
    tap(BUTTON_RIGHT);
    CHECK_EQ(u->x, x, "no AP, no step");
    CHECK(said("AP"), "message about AP");
}

static void t_wall_bump(void)
{
    Unit *u = arena();
    world.feature[10][11] = FE_WALL;
    world_map_changed(&world);
    tap(BUTTON_RIGHT);
    CHECK(u->x == 10 && u->y == 10, "walls stop the wizard");
    CHECK(world_feature(&world, 11, 10) == FE_WALL || world_feature(&world, 11, 10) == FE_NONE,
          "a wall is a wall or rubble");
}

static void t_door_open_close_lock(void)
{
    Unit *u = arena();
    u8 ap;
    world.feature[10][11] = FE_DOOR_CLOSED;
    world_map_changed(&world);
    ap = u->ap;
    tap(BUTTON_RIGHT);
    CHECK_EQ(world_feature(&world, 11, 10), FE_DOOR_OPEN, "bumping a closed door opens it");
    CHECK(u->ap < ap, "opening costs AP");
    CHECK(test_sounds[SND_DOOR] == 1, "door sound");
    CHECK(u->x == 10, "the wizard did not step through yet");
    /* A, then "Tuer/Truhe nutzen", then a direction closes it again */
    {
        u8 i;
        build_actions();
        for (i = 0; i < menu_n; i++)
            if (menu_acts[i] == MA_USE)
                break;
        CHECK(i < menu_n, "'use' is offered next to a door");
    }
    do_action(MA_USE);
    CHECK(apply_pending, "waiting for a direction");
    tap(BUTTON_RIGHT);
    CHECK_EQ(world_feature(&world, 11, 10), FE_DOOR_CLOSED, "use + direction closes the door");
    CHECK(!apply_pending, "the pending use is spent");
    /* locking needs a key */
    u->ap = u->ap_max;
    world.feature[10][11] = FE_DOOR_CLOSED;
    do_action(MA_USE);
    tap(BUTTON_RIGHT);
    CHECK_EQ(world_feature(&world, 11, 10), FE_DOOR_CLOSED, "no key: not locked");
    CHECK(said("Schluessel"), "tells about the key");
    give(OBJ_CHEST_KEY);
    u->ap = u->ap_max;
    do_action(MA_USE);
    tap(BUTTON_RIGHT);
    CHECK_EQ(world_feature(&world, 11, 10), FE_DOOR_LOCKED, "with a key: locked");
    /* and unlocked by bumping with the key */
    u->ap = u->ap_max;
    tap(BUTTON_RIGHT);
    CHECK_EQ(world_feature(&world, 11, 10), FE_DOOR_CLOSED, "bumping a locked door with the key unlocks it");
}

static void t_chest(void)
{
    Unit *u = arena();
    u8 ap = u->ap, before;
    world.feature[10][11] = FE_CHEST_FREE;
    world_map_changed(&world);
    before = world.object_count;
    tap(BUTTON_RIGHT);
    CHECK(world_feature(&world, 11, 10) != FE_CHEST_FREE, "the chest is open");
    CHECK(u->ap < ap, "opening costs AP");
    CHECK(test_sounds[SND_CHEST] == 1, "chest sound");
    CHECK(world.object_count >= before, "contents do not vanish");
}

static void t_melee(void)
{
    Unit *u = arena(), *e;
    u8 hits = 0, ap, rounds;
    e = enemy_at(1, 0, CR_GOBLIN);
    e->con = e->con_max = 200;           /* sturdy: we want to see damage, not death */
    for (rounds = 0; rounds < 12 && e->con == 200; rounds++) {
        u->ap = u->ap_max;
        u->sta = u->sta_max;
        ap = u->ap;
        clear_log();
        tap(BUTTON_RIGHT);
        CHECK(u->ap < ap, "an attack costs AP");
        CHECK(u->x == 10, "attacking does not move the wizard");
        CHECK(test_sounds[SND_HIT] + test_sounds[SND_MISS] + test_sounds[SND_CRIT] + test_sounds[SND_DEATH] >= 1,
              "an attack has a sound");
        if (e->con < 200)
            hits++;
        if (world.unit_count < 2)
            break;
        world.units[world_find_unit(&world, e->id)].done = FALSE;
        e = &world.units[world_find_unit(&world, e->id)];
        if (u->con < 3)
            u->con = u->con_max;
    }
    CHECK(hits >= 1, "twelve swings hit at least once");
}

static void t_melee_kill_credits_vp(void)
{
    Unit *u = arena(), *e;
    u8 id, n0, tries;
    e = enemy_at(1, 0, CR_GOBLIN);
    id = e->id;
    n0 = world.unit_count;
    for (tries = 0; tries < 60 && world_find_unit(&world, id) != NO_UNIT; tries++) {
        u->ap = u->ap_max;
        u->sta = u->sta_max;
        u->con = u->con_max;
        tap(BUTTON_RIGHT);
    }
    CHECK(world_find_unit(&world, id) == NO_UNIT, "the goblin dies eventually");
    CHECK_EQ(world.unit_count, n0 - 1, "one unit fewer");
    CHECK(said("stirbt") || test_sounds[SND_DEATH] > 0, "death is announced");
    CHECK(game.vp[OWN_P1] > 0 || game.kills[OWN_P1] > 0, "the kill is credited");
}

static void t_pickup_drop_wield_eat(void)
{
    Unit *u = arena();
    put_object(10, 10, OBJ_APPLE);
    do_action(MA_PICKUP);
    CHECK_EQ(u->item_count, 1, "picked the apple up");
    CHECK_EQ(count_objects_at(10, 10), 0, "it left the ground");
    CHECK(test_sounds[SND_PICKUP] == 1, "pickup sound");
    CHECK(said("Aufgehoben"), "pickup message");
    do_action(MA_DROP);
    CHECK(u->item_count == 0 || u->in_use == NO_ITEM, "dropped or nothing to drop");
    if (u->item_count == 0) {
        CHECK_EQ(count_objects_at(10, 10), 1, "it lies on the ground again");
        CHECK(said("Fallen gelassen"), "drop message");
    } else {
        /* the apple was carried but not in hand: wield it, then drop it */
        do_action(MA_WIELD);
        do_action(MA_DROP);
        CHECK_EQ(u->item_count, 0, "dropped after wielding");
    }
    /* eat: pick up, wield, eat */
    do_action(MA_PICKUP);
    CHECK_EQ(u->item_count, 1, "picked up again");
    if (u->in_use == NO_ITEM)
        do_action(MA_WIELD);
    clear_log();
    u->con = u->con_max > 2 ? u->con_max - 2 : 1;
    do_action(MA_EAT);
    CHECK_EQ(u->item_count, 0, "the apple is eaten");
    CHECK(said("Gegessen"), "eat message");
    CHECK(test_sounds[SND_EAT] == 1, "eat sound");
}

static void t_drop_one_at_a_time(void)
{
    Unit *u = arena();
    give(OBJ_KNIFE);
    give(OBJ_SWORD);
    give(OBJ_APPLE);
    u->in_use = 1;
    do_action(MA_DROP);
    CHECK_EQ(u->item_count, 2, "dropping takes exactly one item");
    CHECK_EQ(count_objects_at(10, 10), 1, "and puts exactly one on the ground");
}

static void t_pickup_fails_without_ap(void)
{
    Unit *u = arena();
    put_object(10, 10, OBJ_SWORD);
    u->ap = 0;
    clear_log();
    do_action(MA_PICKUP);
    CHECK_EQ(u->item_count, 0, "nothing taken without AP");
    CHECK_EQ(count_objects_at(10, 10), 1, "it stays on the ground");
    CHECK(said("Zu schwer") || said("AP"), "the refusal is explained");
    CHECK(test_sounds[SND_PICKUP] == 0, "no pickup sound");
}

static void t_pickup_menu(void)
{
    static const u16 script[] = {BUTTON_C, 0, 0, 0xFFFF};
    Unit *u = arena();
    put_object(10, 10, OBJ_APPLE);
    put_object(10, 10, OBJ_KNIFE);
    pick_gather();
    CHECK_EQ(pick_n, 2, "two objects to choose from");
    SCRIPT(script);
    do_pickup();                        /* the menu: C takes the first entry */
    CHECK_EQ(u->item_count, 1, "exactly one taken");
    CHECK_EQ(count_objects_at(10, 10), 1, "the other stays");
    /* "Alles aufheben": last entry of the menu */
    put_object(10, 10, OBJ_SWORD);
    {
        static const u16 all[] = {BUTTON_UP, 0, BUTTON_C, 0, 0, 0, 0xFFFF};
        SCRIPT(all);
        do_pickup();
    }
    CHECK(u->item_count >= 2, "'all' takes more than one");
}

static void t_pickup_nothing(void)
{
    arena();
    do_action(MA_PICKUP);
    CHECK(said("Nichts aufzuheben"), "nothing here");
    CHECK_EQ(me()->item_count, 0, "inventory unchanged");
}

static void t_throw_item(void)
{
    Unit *u = arena();
    u8 objs;
    give(OBJ_KNIFE);
    u->in_use = 0;
    objs = world.object_count;
    do_action(MA_THROW);
    CHECK_EQ(mode, MODE_TARGET, "aiming");
    tap(BUTTON_RIGHT);
    tap(BUTTON_RIGHT);
    CHECK_EQ(look_x, 12, "cursor moved");
    tap(BUTTON_C);
    CHECK_EQ(mode, MODE_PLAY, "back to play");
    CHECK_EQ(u->item_count, 0, "the knife left the hand");
    CHECK(world.object_count > objs, "and lies somewhere");
    CHECK(test_sounds[SND_THROW] == 1, "throw sound");
}

static void t_aim_cancel(void)
{
    Unit *u = arena();
    give(OBJ_KNIFE);
    u->in_use = 0;
    do_action(MA_THROW);
    CHECK_EQ(mode, MODE_TARGET, "aiming");
    tap(BUTTON_B);
    CHECK_EQ(mode, MODE_PLAY, "B cancels aiming");
    CHECK_EQ(u->item_count, 1, "nothing thrown");
    /* confirming on the own field cancels without cost (GDD 7.1) */
    u->ap = u->ap_max;
    do_action(MA_THROW);
    tap(BUTTON_C);
    CHECK_EQ(mode, MODE_PLAY, "confirm on self ends aiming");
    CHECK_EQ(u->item_count, 1, "still holding the knife");
}

static void t_cast_at_self_cancels(void)
{
    Unit *u = arena();
    u8 mana = u->mana;
    mode = MODE_TARGET;
    target_kind = TA_SPELL;
    target_spell = SP_MAGIC_BOLT;
    look_x = u->x;
    look_y = u->y;
    clear_log();
    aim_confirm();
    CHECK_EQ(mode, MODE_PLAY, "aiming is over");
    CHECK_EQ(u->mana, mana, "casting at oneself costs nothing (GDD 7.1)");
    CHECK(said("Abgebrochen"), "it says so");
    CHECK(test_sounds[SND_BOLT] == 0, "no bolt sound");
}

static void t_look_mode(void)
{
    Unit *u = arena();
    u8 ap = u->ap;
    do_action(MA_LOOK);
    CHECK_EQ(mode, MODE_LOOK, "looking");
    tap(BUTTON_RIGHT);
    tap(BUTTON_DOWN);
    CHECK(look_x == 11 && look_y == 11, "the look cursor moves");
    CHECK(u->x == 10 && u->y == 10 && u->ap == ap, "the wizard does not");
    tap(BUTTON_B);
    CHECK_EQ(mode, MODE_PLAY, "B leaves look mode");
}

static void t_cast_bolt(void)
{
    Unit *u = arena(), *e;
    u8 mana, i, found = 0;
    static u16 seq[96];
    e = enemy_at(3, 0, CR_GOBLIN);
    e->con = e->con_max = 200;
    /* the spell list: find the position of magic bolt among the non-summon spells */
    for (i = 0; i < SPELL_COUNT; i++) {
        if (books[OWN_P1].level[i] && SPELLS[i].category != SPC_SUMMON) {
            if (i == SP_MAGIC_BOLT)
                break;
            found++;
        }
    }
    {
        u8 k = 0, have_summons = 0;
        for (i = 0; i < SPELL_COUNT; i++)
            if (books[OWN_P1].level[i] && SPELLS[i].category == SPC_SUMMON)
                have_summons = 1;
        if (have_summons)
            seq[k++] = BUTTON_C;       /* "Zauber" in the kind menu */
        seq[k++] = 0;
        while (found--) {
            seq[k++] = BUTTON_DOWN;
            seq[k++] = 0;
        }
        seq[k++] = BUTTON_C;
        seq[k++] = 0;
        seq[k++] = 0xFFFF;
        SCRIPT(seq);
    }
    mana = u->mana;
    do_cast();
    CHECK_EQ(mode, MODE_TARGET, "aiming the bolt");
    CHECK_EQ(target_spell, SP_MAGIC_BOLT, "the bolt is the spell");
    tap(BUTTON_RIGHT);
    tap(BUTTON_RIGHT);
    tap(BUTTON_RIGHT);
    CHECK_EQ(look_x, 13, "cursor x on the goblin");
    CHECK_EQ(look_y, 10, "cursor y on the goblin");
    clear_log();
    tap(BUTTON_C);
    CHECK_EQ(mode, MODE_PLAY, "cast done");
    CHECK(u->mana < mana, "the bolt costs mana");
    CHECK(test_sounds[SND_BOLT] == 1, "bolt sound");
    CHECK(said("Zauber") || said("Schaden") || said("verpufft"), "spell message");
}

static void t_cast_out_of_range_costs_nothing(void)
{
    u8 spell;
    for (spell = 0; spell < SPELL_COUNT; spell++) {
        Unit *u;
        if (SPELLS[spell].category == SPC_SUMMON || SPELLS[spell].category == SPC_POTION)
            continue;
        if (spell == SP_MAGIC_SHIELD || spell == SP_ENCHANT)
            continue;                      /* these work on the caster */
        u = arena();
        books[OWN_P1].level[spell] = 3;
        u->mana = u->mana_max;
        {
            u8 mana = u->mana;
            mode = MODE_TARGET;
            target_kind = TA_SPELL;
            target_spell = spell;
            look_x = 45;
            look_y = 40;                      /* far away */
            clear_log();
            aim_confirm();
            if (u->mana != mana)
                printf("  (spell %d %s took mana out of range)\n", spell, SPELLS[spell].name);
            CHECK_EQ(u->mana, mana, "a refused cast keeps the mana");
            CHECK(said("Reichweite") || said("Sicht") || said("nimmt"), "a refusal message");
        }
    }
}

static void t_summon(void)
{
    Unit *u = arena();
    u8 i, n0 = world.unit_count, spell = SPELL_COUNT;
    for (i = 0; i < SPELL_COUNT; i++)
        if (books[OWN_P1].level[i] && SPELLS[i].category == SPC_SUMMON) {
            spell = i;
            break;
        }
    CHECK(spell < SPELL_COUNT, "the stock wizard knows a summon spell");
    if (spell == SPELL_COUNT)
        return;
    u->mana = u->mana_max;
    {
        u8 mana = u->mana;
        u8 got = spell_summon(&world, &books[OWN_P1], active(), spell, &turns.rng);
        CHECK(got != 0, "summoned");
        CHECK_EQ(world.unit_count, n0 + got, "as many units as reported");
        CHECK(u->mana < mana, "summoning costs mana");
    }
}

static void t_not_a_caster(void)
{
    Unit *u = arena();
    /* make the active unit a goblin */
    u->kind = CR_GOBLIN;
    do_cast();
    CHECK(said("Nur Zauberer"), "only wizards cast");
    CHECK_EQ(mode, MODE_PLAY, "no aiming");
}

static void t_end_turn_round_and_save(void)
{
    Unit *u = arena();
    u8 round = turns.round;
    u->ap = 1;
    clear_log();
    end_turn();
    CHECK_EQ(turns.round, round + 1, "a new round");
    CHECK_EQ(me()->ap, me()->ap_max, "AP is back");
    CHECK(test_sounds[SND_ROUND] >= 1, "round chime");
    CHECK(said("Neue Runde"), "message");
    CHECK(save_exists(), "the round end saved the game");
    CHECK(said("Gespeichert"), "save message");
}

static void t_start_hold_ends_turn(void)
{
    Unit *u = arena();
    u8 round = turns.round, other;
    other = world_spawn_unit(&world, OWN_P1, CR_GOBLIN, 12, 12);   /* a second unit keeps a tap from ending the turn */
    world.units[other].ap = world.units[other].ap_max;
    (void)u;
    hold(BUTTON_START, 20);              /* a tap finishes the unit, nothing more */
    CHECK_EQ(turns.round, round, "a short press does not end the turn");
    CHECK(turn_units_left(&turns, &world), "the other unit still has its turn");
    hold(BUTTON_START, 50);              /* 0.6 s at 60 Hz = 36 frames */
    CHECK_EQ(turns.round, round + 1, "holding START ends the turn");
}

static void t_start_tap_finishes_unit(void)
{
    Unit *u = arena();
    u8 round = turns.round;
    tap(BUTTON_START);
    CHECK(u->done || !turn_units_left(&turns, &world), "the unit is done");
    CHECK(said("Alle fertig"), "all units done");
    CHECK_EQ(turns.round, round, "the round did not end");
    tap(BUTTON_START);
    CHECK_EQ(turns.round, round + 1, "START with everyone done ends the turn");
}

static void t_next_unit(void)
{
    Unit *u = arena();
    u8 id = u->id, other;
    other = world_spawn_unit(&world, OWN_P1, CR_GOBLIN, 12, 12);
    CHECK(other != NO_UNIT, "a second own unit");
    world.units[other].ap = world.units[other].ap_max;
    tap(BUTTON_C);
    CHECK(me()->id != id, "C switches to the next unit");
    tap(BUTTON_C);
    CHECK_EQ(me()->id, id, "and around again");
}

static void t_action_menu_contents(void)
{
    Unit *u = arena();
    u8 i;
    bool has[MA_COUNT];
    build_actions();
    memset(has, 0, sizeof has);
    for (i = 0; i < menu_n; i++)
        has[menu_acts[i]] = TRUE;
    CHECK(has[MA_CAST] && has[MA_LOOK] && has[MA_ENDTURN] && has[MA_SAVE] && has[MA_QUIT],
          "the basics are always there");
    CHECK(!has[MA_LAND] && !has[MA_THROW] && !has[MA_DROP] && !has[MA_PICKUP] && !has[MA_USE],
          "nothing for a bare wizard on grass");
    put_object(10, 10, OBJ_APPLE);
    give(OBJ_KNIFE);
    u->in_use = 0;
    world.feature[9][10] = FE_CHEST_FREE;
    build_actions();
    memset(has, 0, sizeof has);
    for (i = 0; i < menu_n; i++)
        has[menu_acts[i]] = TRUE;
    CHECK(has[MA_PICKUP] && has[MA_THROW] && has[MA_DROP] && has[MA_USE] && has[MA_WIELD],
          "object, hand and chest unlock their actions");
    u->flags |= UF_FLYING;
    build_actions();
    memset(has, 0, sizeof has);
    for (i = 0; i < menu_n; i++)
        has[menu_acts[i]] = TRUE;
    CHECK(has[MA_LAND] && !has[MA_TAKEOFF], "a flyer can land, not take off");
}

static void t_action_menu_open_cancel(void)
{
    static const u16 seq[] = {BUTTON_B, 0, 0xFFFF};
    Unit *u = arena();
    u8 ap = u->ap;
    tap_with(BUTTON_A, seq);             /* open, then B cancels */
    CHECK_EQ(g_dbg.menus, 0, "menu closed");
    CHECK_EQ(u->ap, ap, "cancelling costs nothing");
    CHECK_EQ(mode, MODE_PLAY, "still in play");
}

static void t_action_menu_remembers_last(void)
{
    static const u16 pick_look[] = {BUTTON_DOWN, 0, BUTTON_DOWN, 0, BUTTON_C, 0, 0xFFFF};
    static const u16 again[] = {BUTTON_C, 0, 0xFFFF};
    arena();
    /* the bare wizard's list: Zaubern, Schauen, Trinken, Aufsteigen, Zug beenden, ... */
    tap_with(BUTTON_A, pick_look);
    CHECK_EQ(last_act, menu_acts[2], "the choice is remembered");
    if (menu_acts[2] == MA_LOOK) {
        CHECK_EQ(mode, MODE_LOOK, "look mode was entered");
        tap(BUTTON_B);
    }
    /* the next A starts on the remembered entry: C picks it again */
    tap_with(BUTTON_A, again);
    if (last_act == MA_LOOK)
        CHECK_EQ(mode, MODE_LOOK, "A, C repeats the last action");
}

static void num_label(u8 i, char *out)
{
    sprintf(out, "entry %d", i);
}

static void t_menu_run_navigation(void)
{
    static const u16 down2[] = {BUTTON_DOWN, 0, BUTTON_DOWN, 0, BUTTON_C, 0, 0xFFFF};
    static const u16 wrap[] = {BUTTON_UP, 0, BUTTON_C, 0, 0xFFFF};
    static const u16 back[] = {BUTTON_B, 0, 0xFFFF};
    static const u16 start[] = {BUTTON_START, 0, 0xFFFF};
    static const u16 far[] = {BUTTON_UP, 0, BUTTON_UP, 0, BUTTON_UP, 0, BUTTON_C, 0, 0xFFFF};
    arena();
    SCRIPT(down2);
    CHECK_EQ(menu_run("t", "h", 5, 0, num_label), 2, "down, down, C");
    SCRIPT(wrap);
    CHECK_EQ(menu_run("t", "h", 5, 0, num_label), 4, "up from the top wraps to the bottom");
    SCRIPT(back);
    CHECK_EQ(menu_run("t", "h", 5, 0, num_label), -1, "B cancels");
    SCRIPT(start);
    CHECK_EQ(menu_run("t", "h", 5, 0, num_label), -1, "START cancels");
    SCRIPT(far);
    CHECK_EQ(menu_run("t", "h", 30, 0, num_label), 27, "scrolling lists address the right entry");
    CHECK_EQ(g_dbg.menus, 0, "menus balanced");
}

static void t_quit_menu(void)
{
    static const u16 q_save[] = {BUTTON_UP, 0, BUTTON_UP, 0, BUTTON_C, 0, 0xFFFF};   /* index 0 */
    static const u16 q_nosave[] = {BUTTON_UP, 0, BUTTON_C, 0, 0xFFFF};               /* index 1 */
    static const u16 q_cancel[] = {BUTTON_C, 0, 0xFFFF};                             /* index 2 */
    arena();
    do_action(MA_SAVE);
    CHECK(save_exists(), "manual save");
    memset(test_sram, 0, sizeof test_sram);
    SCRIPT(q_cancel);
    do_action(MA_QUIT);
    CHECK(!quit_game, "'continue' keeps playing");
    SCRIPT(q_nosave);
    do_action(MA_QUIT);
    CHECK(quit_game, "quit without saving");
    CHECK(!save_exists(), "and nothing was written");
    quit_game = FALSE;
    SCRIPT(q_save);
    do_action(MA_QUIT);
    CHECK(quit_game, "save and quit");
    CHECK(save_exists(), "the save is there");
}

static void snapshot_state(World *w, Turns *t, Game *g, u8 explored[MAP_MAX_H][SIGHT_COLS])
{
    *w = world;
    *t = turns;
    *g = game;
    memcpy(explored, p1_sight.explored, sizeof p1_sight.explored);
}

static void t_save_load_roundtrip(void)
{
    static World w0;
    static Turns t0;
    static Game g0;
    static u8 ex0[MAP_MAX_H][SIGHT_COLS];
    static Spellbook b0[OWN_NEUTRAL];
    u8 sc;
    for (sc = 0; sc < 3; sc++) {
        Unit *u;
        boot(sc);
        end_turn();                      /* round 2 */
        u = me();
        if (world_move_unit(&world, active(), 1, 0))
            update_sight();
        game.vp[OWN_P1] = 17;
        CHECK(save_game(), "saved");
        snapshot_state(&w0, &t0, &g0, ex0);
        memcpy(b0, books, sizeof b0);
        /* ruin the live state, then load */
        memset(&world, 0x55, sizeof world);
        memset(&turns, 0x55, sizeof turns);
        memset(&game, 0x55, sizeof game);
        memset(books, 0x55, sizeof books);
        cur_sc = NULL;
        CHECK(load_game(), "loaded");
        CHECK(memcmp(&world, &w0, sizeof world) == 0, "world restored bit for bit");
        CHECK_EQ(turns.round, t0.round, "round restored");
        CHECK_EQ(turns.active_id, t0.active_id, "active unit restored");
        CHECK(memcmp(&game, &g0, sizeof game) == 0, "game restored");
        CHECK(memcmp(books, b0, sizeof b0) == 0, "spell books restored");
        CHECK(memcmp(p1_sight.explored, ex0, sizeof ex0) == 0, "explored map restored");
        CHECK(cur_sc == &SCENARIOS[sc], "the scenario is remembered");
        CHECK(turns.ai == ai_wizard_phase && turns.on_round == on_round, "turn hooks re-bound");
        CHECK_EQ(game.vp[OWN_P1], 17, "points kept");
        (void)u;
        /* the loaded game plays on */
        begin_play("Spielstand geladen.");
        tap(BUTTON_RIGHT);
        end_turn();
        CHECK_EQ(turns.round, t0.round + 1, "a loaded game continues");
    }
}

static void t_save_rejects_damage(void)
{
    static World w0;
    boot(0);
    end_turn();
    w0 = world;
    CHECK(save_exists(), "a valid save");
    /* a flipped bit in the body */
    test_sram[SAVE_HEADER + 100] ^= 0x10;
    CHECK(!save_exists(), "a damaged save is refused");
    memset(&world, 0, sizeof world);
    CHECK(!load_game(), "load refuses it");
    CHECK_EQ(world.w, 0, "and leaves the live state alone");
    test_sram[SAVE_HEADER + 100] ^= 0x10;
    CHECK(save_exists(), "restored: valid again");
    /* wrong version */
    test_sram[1] = 99;
    CHECK(!save_exists(), "another version is refused");
    test_sram[1] = SAVE_VERSION;
    /* interrupted write: the first byte is the last to be written */
    test_sram[0] = 0;
    CHECK(!save_exists(), "no magic, no save");
    /* an empty cartridge */
    memset(test_sram, 0, sizeof test_sram);
    CHECK(!save_exists(), "empty SRAM");
    memset(test_sram, 0xFF, sizeof test_sram);
    CHECK(!save_exists(), "erased SRAM");
}

static void t_save_interrupted_is_never_a_wrong_slot(void)
{
    static World a, b;
    static jmp_buf loss;
    long total, k, step;
    boot(0);
    end_turn();                          /* state A, saved by the autosave */
    CHECK(save_exists(), "state A is saved");
    a = world;
    end_turn();                          /* state B (a newer autosave is now in the SRAM) */
    b = world;
    total = (long)save_body_len() + SAVE_HEADER + 4;
    /* the power fails at every kind of point: early, anywhere in the body, near the end */
    for (k = 1; k <= total; k += (k < 64 || k > total - 80) ? 1 : 211) {
        u8 saved[sizeof test_sram];
        boot(0);
        end_turn();
        a = world;
        CHECK(save_game(), "baseline save");
        end_turn();
        b = world;
        memcpy(saved, test_sram, sizeof saved);
        test_power_loss = &loss;
        test_power_after = k;
        if (setjmp(loss) == 0)
            save_game();                 /* interrupted at byte k (or complete when k = total) */
        test_power_after = 0;
        test_power_loss = NULL;
        if (save_exists()) {
            World loaded;
            CHECK(load_game(), "a slot that looks valid loads");
            loaded = world;
            if (memcmp(&loaded, &a, sizeof loaded) != 0 && memcmp(&loaded, &b, sizeof loaded) != 0) {
                printf("  (power loss after %ld of %ld bytes left a valid but wrong slot)\n", k, total);
                CHECK(FALSE, "an interrupted save must be invalid, or the old or the new state");
                return;
            }
        }
        (void)saved;
    }
    CHECK(TRUE, "no interruption point produced a wrong but valid slot");
}

static void t_save_fits_sram(void)
{
    boot(0);
    CHECK(save_body_len() + SAVE_HEADER + 4 <= 32768, "the save fits the 32 KB SRAM");
    CHECK(save_body_len() < 16384, "even a 16 KB part would do");
    save_game();
    CHECK_EQ(sram_pos, SAVE_HEADER + save_body_len(), "the writer ended where the body ends");
}

static void t_portal_escape_win(void)
{
    static const u16 page[] = {BUTTON_A, 0, 0, 0, 0xFFFF};
    Unit *u = arena();
    world.portal_x = 11;
    world.portal_y = 10;
    game.portal_x = 11;
    game.portal_y = 10;
    game.portal_open = TRUE;
    game.portal_closed = FALSE;
    CHECK_EQ(game_outcome(&game, &world, OWN_P1), OUT_RUNNING, "still running");
    do_step(ARROW_RIGHT);
    CHECK(test_sounds[SND_PORTAL] >= 1, "portal sound");
    CHECK(game.vp[OWN_P1] >= VP_ESCAPE, "escaping scores points");
    CHECK(end_pending, "the end is pending");
    SCRIPT(page);
    tick(1);
    CHECK(game_ended, "the game is over");
    CHECK_EQ(g_dbg.screen, SCREEN_END, "the end page was shown");
    CHECK(page_has("Portal"), "page says what happened");
    CHECK(page_has("Siegpunkte"), "page lists the score");
    CHECK_EQ(game_outcome(&game, &world, OWN_P1), OUT_WIN, "a win");
    (void)u;
}

static void t_wizard_dies_lose(void)
{
    static const u16 page[] = {BUTTON_A, 0, 0, 0, 0xFFFF};
    Unit *u = arena();
    CHECK_EQ(game_outcome(&game, &world, OWN_P1), OUT_RUNNING, "running");
    world_kill_unit(&world, active(), CR_GOBLIN, OWN_P2, FALSE);
    (void)u;
    settle();
    CHECK(end_pending, "the end is pending");
    SCRIPT(page);
    end_pending = TRUE;
    tick(1);
    CHECK(game_ended, "game over");
    CHECK(page_has("gefallen") || page_has("Portal"), "lose text");
}

static void t_game_ends_without_portal_escape(void)
{
    /* the portal closing without the wizard escaping is a loss with the wizard still alive */
    arena();
    game.portal_open = FALSE;
    game.portal_closed = TRUE;
    CHECK(game_over(&game, &world), "the game is over when the portal is shut");
    CHECK_EQ(game_outcome(&game, &world, OWN_P1), OUT_LOSE, "and it counts as lost");
}

static void t_takeoff_land(void)
{
    Unit *u = arena();
    u8 p = world_spawn_unit(&world, OWN_P1, CR_PEGASUS, 12, 12);
    CHECK(p != NO_UNIT, "a pegasus");
    turns.active = p;
    turns.active_id = world.units[p].id;
    u = me();
    u->ap = u->ap_max;
    clear_log();
    do_action(MA_TAKEOFF);
    CHECK(u->flags & UF_FLYING, "it flies");
    CHECK(test_sounds[SND_FLY] >= 1, "sound");
    do_action(MA_LAND);
    CHECK(!(u->flags & UF_FLYING), "it landed");
}

static void t_unit_not_flying_cannot_land(void)
{
    arena();
    do_action(MA_LAND);
    CHECK(said("fliegt nicht"), "not flying");
    do_action(MA_TAKEOFF);
    CHECK(said("fliegt nicht"), "this wizard cannot fly");
}

static void t_mount(void)
{
    Unit *u = arena();
    u8 horse = world_spawn_unit(&world, OWN_P1, CR_UNICORN, 11, 10);
    u8 x;
    CHECK(horse != NO_UNIT, "a unicorn next door");
    world.units[horse].ap = world.units[horse].ap_max;
    u = me();
    x = u->x;
    clear_log();
    do_action(MA_MOUNT);
    CHECK(said("Aufgesessen") || said("Reittier"), "mount attempt answered");
    (void)x;
}

static void t_apply_wrong_target(void)
{
    arena();
    do_action(MA_USE);
    tap(BUTTON_RIGHT);
    CHECK(said("nichts"), "nothing to use there");
    CHECK(!apply_pending, "pending use cleared");
    /* cancel with B */
    do_action(MA_USE);
    tap(BUTTON_B);
    CHECK(!apply_pending, "B cancels a pending use");
}

static void t_ai_phase_runs(void)
{
    u8 i, before_hash;
    boot(1);
    before_hash = (u8)g_dbg.hash;
    for (i = 0; i < 6; i++)
        end_turn();
    CHECK(turns.round >= 7 || game_ended || end_pending, "rounds advance");
    CHECK(world.unit_count > 0, "the world survives");
    (void)before_hash;
}

static void t_long_play_all_scenarios(void)
{
    static const u16 cancel[] = {BUTTON_B, 0, BUTTON_B, 0, BUTTON_B, 0, BUTTON_B, 0, 0xFFFF};
    static const u16 page[] = {BUTTON_A, 0, 0, 0, 0xFFFF};
    u8 sc, r;
    for (sc = 0; sc < 3; sc++) {
        boot(sc);
        for (r = 0; r < 40 && !game_ended; r++) {
            /* a bit of everything each round, through the same entry points the pad uses */
            do_step(ARROW_RIGHT);
            do_step(ARROW_DOWN);
            do_step(ARROW_LEFT);
            SCRIPT(cancel);
            do_action(MA_PICKUP);
            SCRIPT(cancel);
            do_action(MA_LOOK);
            mode = MODE_PLAY;
            SCRIPT(cancel);
            do_cast();
            mode = MODE_PLAY;
            end_turn();
            if (end_pending || game_ended) {
                SCRIPT(page);
                tick(1);
            }
            if (!game_ended)
                CHECK(world.unit_count > 0 && world.unit_count <= MAX_UNITS, "unit count sane");
        }
        CHECK(r >= 5 || game_ended, "played several rounds");
        test_script = NULL;
    }
}

extern u32 test_fuzz_seed;
void test_fuzz_start(u32 seed);

static void check_invariants(const char *what)
{
    u8 i;
    if (world.unit_count == 0 || world.unit_count > MAX_UNITS)
        test_fail_now("unit count out of range");
    if (active() >= world.unit_count)
        test_fail_now("the active unit does not exist");
    for (i = 0; i < world.unit_count; i++) {
        const Unit *u = &world.units[i];
        if (u->x >= world.w || u->y >= world.h)
            test_fail_now("a unit left the map");
        {   /* a speed effect doubles it, the flying potion doubles the ground budget (world_new_turn) */
            u16 top = u->ap_max > u->ap_fly ? u->ap_max : u->ap_fly;
            if (u->ap > 4 * top + 4)
                test_fail_now("AP far above any maximum");
        }
        if (u->mana > u->mana_max)
            test_fail_now("mana above its maximum");
        if (u->con > u->con_max && u->con_max)
            test_fail_now("constitution above its maximum");
        if (u->item_count > 6)
            test_fail_now("too many items");
        if (u->in_use != NO_ITEM && u->in_use >= u->item_count)
            test_fail_now("the hand holds an item that is not carried");
    }
    if (world.object_count > MAX_OBJECTS)
        test_fail_now("object count out of range");
    if (mode > MODE_TARGET)
        test_fail_now("mode out of range");
    if (look_x < -1 || look_y < -1 || look_x > world.w || look_y > world.h)
        test_fail_now("the look cursor left the map");
    if (g_dbg.menus > 3)
        test_fail_now("menus pile up");
    if (turns.round == 0)
        test_fail_now("round counter broken");
    (void)what;
}

/* Random play: any button at any time, in every scenario. The game must neither crash (ASan/UBSan
 * catch memory errors when run with SANITIZE=1) nor leave its invariants. */
static void t_fuzz_random_buttons(void)
{
    u32 seed;
    u8 sc;
    for (sc = 0; sc < 3; sc++)
        for (seed = 1; seed <= 6; seed++) {
            u16 f;
            boot(sc);
            test_fuzz_start(seed * 7919 + sc);
            for (f = 0; f < 2500 && !game_ended && !quit_game; f++) {
                play_frame();
                SYS_doVBlankProcess();
                check_invariants("fuzz");
                test_vblanks = 0;           /* a long run is fine, only one stuck loop is not */
            }
            test_fuzz_seed = 0;
            test_script = NULL;
            mode = MODE_PLAY;
        }
    CHECK(TRUE, "18 random sessions survived");
}

/* The same, with the world disturbed now and then: enemies and friends appear next to the active unit,
 * objects and doors turn up, items are handed out. This reaches combat, spells, doors, chests, riding
 * and flying in situations a quiet world never produces. */
static void chaos(u32 *rs)
{
    Unit *u = &world.units[active()];
    u8 kind, x, y, k;
    *rs = *rs * 1664525u + 1013904223u;
    k = (*rs >> 9) % 8;
    *rs = *rs * 1664525u + 1013904223u;
    kind = (*rs >> 9) % CR_COUNT;
    x = (u8)(u->x + ((*rs >> 4) % 3) - 1);
    y = (u8)(u->y + ((*rs >> 6) % 3) - 1);
    if (x >= world.w || y >= world.h)
        return;
    switch (k) {
    case 0:
        world_spawn_unit(&world, OWN_P2, kind, x, y);
        break;
    case 1:
        world_spawn_unit(&world, OWN_P1, kind, x, y);
        break;
    case 2:
        if (world.object_count < MAX_OBJECTS) {
            LocObject *o = &world.objects[world.object_count++];
            o->x = x;
            o->y = y;
            o->tile = OBJECTS[(*rs >> 12) % OBJ_COUNT].tile;
        }
        break;
    case 3:
        world.feature[y][x] = (u8)((*rs >> 11) % FE_COUNT);
        world_map_changed(&world);
        break;
    case 4:
        if (u->item_count < 6)
            u->items[u->item_count++] = (u8)((*rs >> 10) % OBJ_COUNT);
        break;
    case 5:
        u->ap = u->ap_max;
        u->mana = u->mana_max;
        u->con = u->con_max;
        break;
    case 6:
        if ((*rs >> 8) & 1)
            world_take_off(&world, active());
        else
            world_land(&world, active());
        break;
    default:
        break;
    }
    update_sight();
}

static void t_fuzz_with_chaos(void)
{
    u32 seed;
    u8 sc;
    const char *n = getenv("FUZZ_SEEDS");
    u32 seeds = n ? (u32)atoi(n) : 8;
    for (sc = 0; sc < 3; sc++)
        for (seed = 1; seed <= seeds; seed++) {
            u16 f;
            u32 rs = seed * 2654435761u + sc;
            boot(sc);
            test_fuzz_start(seed * 104729 + sc);
            for (f = 0; f < 3000 && !game_ended && !quit_game; f++) {
                if ((f % 25) == 0)
                    chaos(&rs);
                play_frame();
                SYS_doVBlankProcess();
                check_invariants("chaos");
                test_vblanks = 0;
            }
            test_fuzz_seed = 0;
            test_script = NULL;
            mode = MODE_PLAY;
        }
    CHECK(TRUE, "chaotic sessions survived");
}

static void t_wizard_settings_roundtrip(void)
{
    Wizard before;
    boot(0);
    wizards_reset();
    wizard_slots[0].xp = 600;
    wizard_raise(&wizard_slots[0], WA_COMBAT);
    wizard_raise(&wizard_slots[0], WA_COMBAT);
    snprintf(wizard_slots[0].name, sizeof wizard_slots[0].name, "Merlin");
    sound_on = FALSE;
    music_on = TRUE;
    before = wizard_slots[0];
    wizards_save();
    memset(&wizard_slots[0], 0, sizeof wizard_slots[0]);
    sound_on = TRUE;
    music_on = FALSE;
    wizards_load();
    CHECK(memcmp(&wizard_slots[0], &before, sizeof before) == 0, "the designed wizard comes back");
    CHECK(!sound_on && music_on, "and the switches");
    /* the game slot and the settings do not touch each other */
    end_turn();
    save_game();
    wizards_load();
    CHECK(memcmp(&wizard_slots[0], &before, sizeof before) == 0, "a game save leaves the wizard alone");
    wizards_save();
    CHECK(save_exists(), "a settings save leaves the game slot alone");
    CHECK(save_body_len() + SAVE_HEADER + 4 < SET_BASE, "the game slot ends before the settings start");
    /* damage: a stock wizard instead */
    test_sram[SET_BASE + 20] ^= 1;
    wizards_load();
    CHECK_EQ(wizard_slots[0].level, 1, "damaged settings give the stock wizard");
    CHECK_EQ(wizard_slots[0].xp, 600, "with its creation budget");
    /* an empty cartridge */
    memset(test_sram, 0, sizeof test_sram);
    wizards_load();
    CHECK(wizard_valid(&wizard_slots[0]), "an empty cartridge: a valid stock wizard");
}

static void t_designer_attributes(void)
{
    static const u16 raise3[] = {BUTTON_RIGHT, 0, BUTTON_RIGHT, 0, BUTTON_RIGHT, 0, BUTTON_B, 0, 0xFFFF};
    static const u16 lower_below_base[] = {BUTTON_LEFT, 0, BUTTON_LEFT, 0, BUTTON_B, 0, 0xFFFF};
    Wizard *w = &wizard_slots[0];
    u8 com;
    u16 xp;
    boot(0);
    wizards_reset();
    com = w->com;
    xp = w->xp;
    SCRIPT(raise3);
    designer_attrs();
    CHECK_EQ(w->com, com + 3, "three raises of combat");
    CHECK(w->xp < xp, "they cost experience");
    SCRIPT(lower_below_base);
    designer_attrs();
    CHECK_EQ(w->com, com + 1, "two lowers take two back");
    {
        static const u16 many[] = {BUTTON_LEFT, 0, BUTTON_LEFT, 0, BUTTON_LEFT, 0, BUTTON_LEFT, 0, BUTTON_B, 0, 0xFFFF};
        SCRIPT(many);
        designer_attrs();
        CHECK_EQ(w->com, com, "never below the start value");
        CHECK_EQ(w->xp, xp, "and every point was refunded in full");
    }
    /* without experience nothing can be bought */
    w->xp = 0;
    SCRIPT(raise3);
    designer_attrs();
    CHECK_EQ(w->com, com, "no experience, no attribute");
}

static void t_designer_shop(void)
{
    static const u16 buy[] = {BUTTON_RIGHT, 0, BUTTON_RIGHT, 0, BUTTON_B, 0, 0xFFFF};
    static const u16 sell[] = {BUTTON_LEFT, 0, BUTTON_LEFT, 0, BUTTON_LEFT, 0, BUTTON_B, 0, 0xFFFF};
    Wizard *w = &wizard_slots[0];
    u8 first = SPELL_COUNT, i;
    boot(0);
    wizards_reset();
    for (i = 0; i < SPELL_COUNT; i++)
        if (SPELLS[i].category != SPC_SUMMON) {
            first = i;
            break;
        }
    SCRIPT(buy);
    designer_shop(PAGE_SPELLS);
    CHECK_EQ(w->book.level[first], 2, "two levels of the first spell");
    CHECK(w->xp < 600, "the price was paid");
    SCRIPT(sell);
    designer_shop(PAGE_SPELLS);
    CHECK_EQ(w->book.level[first], 0, "selling back stops at zero");
    CHECK_EQ(w->xp, 600, "with a full refund");
    /* creatures page lists summons only: the first entry is a summon spell */
    {
        u8 s1 = SPELL_COUNT;
        for (i = 0; i < SPELL_COUNT; i++)
            if (SPELLS[i].category == SPC_SUMMON) {
                s1 = i;
                break;
            }
        SCRIPT(buy);
        designer_shop(PAGE_CREATURES);
        CHECK_EQ(w->book.level[s1], 2, "the creatures page buys summons");
    }
    w->xp = 0;
    SCRIPT(buy);
    designer_shop(PAGE_SPELLS);
    CHECK_EQ(w->book.level[first], 0, "without experience nothing is bought");
}

static void t_designer_menu_and_save(void)
{
    static const u16 seq[] = {BUTTON_C, 0,                          /* attributes */
                              BUTTON_RIGHT, 0, BUTTON_B, 0,         /* raise combat, back */
                              BUTTON_B, 0, 0xFFFF};                 /* leave the designer */
    Wizard *w = &wizard_slots[0];
    u8 com;
    boot(0);
    wizards_reset();
    com = w->com;
    SCRIPT(seq);
    designer();
    CHECK_EQ(w->com, com + 1, "the change was made");
    memset(w, 0, sizeof *w);
    wizards_load();
    CHECK_EQ(w->com, com + 1, "and saved when the designer closed");
}

static void t_options_persist(void)
{
    static const u16 toggle[] = {BUTTON_C, 0, BUTTON_DOWN, 0, BUTTON_C, 0, BUTTON_B, 0, 0xFFFF};
    boot(0);
    wizards_reset();
    sound_on = music_on = TRUE;
    SCRIPT(toggle);
    options();
    CHECK(!sound_on, "sound switched off");
    CHECK(!music_on, "music switched off");
    sound_on = music_on = TRUE;
    wizards_load();
    CHECK(!sound_on && !music_on, "both survive a reload");
}

static void t_campaign_xp_and_level(void)
{
    static const u16 page[] = {BUTTON_A, 0, 0, 0, 0xFFFF};
    Wizard *w = &wizard_slots[0];
    u16 xp;
    u8 level;
    boot(1);                            /* scenario 2 */
    wizards_reset();
    xp = w->xp;
    level = w->level;
    game.vp[OWN_P1] = 25;
    game.escaped |= 1u << OWN_P1;
    game.portal_open = TRUE;
    /* make it a real win: the wizard escaped */
    world_remove_unit(&world, active());
    turn_revalidate(&turns, &world);
    SCRIPT(page);
    end_screen();
    if (game_outcome(&game, &world, OWN_P1) == OUT_WIN) {
        CHECK_EQ(w->xp, xp + 25, "points become experience 1:1");
        CHECK_EQ(w->level, level + 1, "a first clear raises the level");
        CHECK(page_has("Stufe"), "the page shows the level");
        CHECK(page_has("aufgestiegen"), "and the level up");
    } else {
        CHECK(FALSE, "the win could not be set up");
    }
}

static void t_campaign_second_clear_gives_no_level(void)
{
    Wizard *w = &wizard_slots[0];
    boot(0);
    wizards_reset();
    wizard_campaign_result(w, 10, 1);
    CHECK_EQ(w->level, 2, "first clear");
    wizard_campaign_result(w, 10, 1);
    CHECK_EQ(w->level, 2, "the same scenario again: only the experience");
    CHECK_EQ(w->xp, 600 + 20, "experience counted both times");
}

static void t_standard_set_offered_when_empty(void)
{
    u16 sum = 0, k;
    boot(0);
    wizards_reset();                    /* a fresh wizard: an empty book */
    for (k = 0; k < SPELL_COUNT; k++)
        sum += wizard_slots[0].book.level[k];
    CHECK_EQ(sum, 0, "fresh wizards start without spells");
    new_game(&SCENARIOS[0]);
    sum = 0;
    for (k = 0; k < SPELL_COUNT; k++)
        sum += books[OWN_P1].level[k];
    CHECK(sum > 0, "a game never starts without spells");
}

static void t_designed_book_is_not_overwritten(void)
{
    Wizard *w = &wizard_slots[0];
    boot(0);
    wizards_reset();
    w->book.level[SP_HARPY] = 2;        /* a designed wizard with exactly one spell */
    new_game(&SCENARIOS[0]);
    CHECK_EQ(w->book.level[SP_HARPY], 2, "the designed spell is kept");
    CHECK_EQ(w->book.level[SP_MAGIC_BOLT], 0, "and nothing was added to a designed book");
    CHECK_EQ(books[OWN_P1].level[SP_HARPY], 2, "the game uses the designed book");
}

static void t_text_books_are_well_formed(void)
{
    static const struct { const u8 *bin; const char *name; } B[] = {
        {help_md, "help"}, {tutorial_md, "tutorial"}, {lexicon_md, "lexicon"}, {spells_md, "spells"}};
    u8 b;
    for (b = 0; b < 4; b++) {
        u16 n = book_count(B[b].bin), i;
        CHECK(n > 0, "a book has pages");
        for (i = 0; i < n; i++) {
            char title[28], line[28];
            const u8 *p;
            u8 k = book_page(B[b].bin, i, title, &p), j;
            CHECK(strlen(title) > 0 && strlen(title) <= 26, "title length");
            CHECK(k <= 22, "at most 22 lines");
            for (j = 0; j < k; j++) {
                u8 c;
                p = page_line(p, line);
                CHECK(strlen(line) <= 26, "line fits the 27 columns");
                for (c = 0; line[c]; c++)
                    if ((u8)line[c] < 32 || (u8)line[c] > 126) {
                        printf("  non-ASCII in %s page %d\n", B[b].name, i);
                        CHECK(FALSE, "ASCII only");
                        break;
                    }
            }
        }
    }
    CHECK_EQ(book_count(lexicon_md), CR_COUNT + OBJ_COUNT, "one lexicon page per creature and object");
    CHECK_EQ(book_count(spells_md), SPELL_COUNT, "one text per spell");
    CHECK_EQ(book_count(tutorial_md), TUT_COUNT, "an intro and one page per tutorial step");
}

static void t_help_viewer_navigation(void)
{
    static const u16 pages[] = {BUTTON_RIGHT, 0, BUTTON_RIGHT, 0, BUTTON_LEFT, 0, BUTTON_LEFT, 0, BUTTON_LEFT, 0,
                                BUTTON_B, 0, 0xFFFF};
    boot(0);
    SCRIPT(pages);
    book_viewer(help_md, 0);
    CHECK(page_has("/"), "the title carries the page number");
    CHECK_EQ(g_dbg.menus, 0, "no menu left open");
    /* every page of every book can be shown */
    {
        u16 n = book_count(help_md), i;
        for (i = 0; i < n; i++) {
            static const u16 leave[] = {BUTTON_B, 0, 0xFFFF};
            SCRIPT(leave);
            book_viewer(help_md, i);
        }
    }
    CHECK(TRUE, "all help pages drawn");
}

static void t_lexicon_discoveries(void)
{
    static const u16 enter[] = {BUTTON_C, 0,            /* Kreaturen */
                                BUTTON_C, 0,            /* the wizard: always known */
                                BUTTON_A, 0,            /* close the detail page */
                                BUTTON_B, 0, BUTTON_B, 0, 0xFFFF};
    boot(0);
    lexicon_init(&lex);
    CHECK_EQ(lexicon_seen_count(&lex), 0, "an empty lexicon");
    update_sight();
    CHECK(lexicon_seen_creature(&lex, CR_WIZARD), "the own wizard is known after the first look");
    SCRIPT(enter);
    lexicon_screen();
    CHECK(ever("Kampf"), "the detail page shows the values");
    /* an unseen entry cannot be opened */
    {
        static const u16 unseen[] = {BUTTON_C, 0, BUTTON_UP, 0, BUTTON_C, 0, BUTTON_B, 0, BUTTON_B, 0, 0xFFFF};
        lexicon_init(&lex);
        memset(test_text, 0, sizeof test_text);
        test_text_ever_n = 0;
        test_text_ever[0] = 0;
        SCRIPT(unseen);
        lexicon_screen();
        CHECK(!ever("Kampf"), "an unseen creature has no page");
    }
    /* a picked-up object is discovered */
    arena();
    lexicon_init(&lex);
    put_object(10, 10, OBJ_APPLE);
    do_action(MA_PICKUP);
    CHECK(lexicon_seen_object(&lex, OBJ_APPLE), "picking an object up discovers it");
}

static void t_lexicon_spells_page(void)
{
    static const u16 spells[] = {BUTTON_DOWN, 0, BUTTON_DOWN, 0, BUTTON_C, 0,   /* the "Zauber" section */
                                 BUTTON_C, 0,                                    /* the first spell */
                                 BUTTON_A, 0, BUTTON_B, 0, BUTTON_B, 0, 0xFFFF};
    boot(0);
    SCRIPT(spells);
    lexicon_screen();
    CHECK(ever("Mana Stufe"), "a spell page shows its mana");
}

static void t_lexicon_survives_settings(void)
{
    boot(0);
    lexicon_init(&lex);
    lexicon_see_creature(&lex, CR_DEMON);
    lexicon_see_object(&lex, OBJ_SWORD);
    wizards_save();
    lexicon_init(&lex);
    wizards_load();
    CHECK(lexicon_seen_creature(&lex, CR_DEMON) && lexicon_seen_object(&lex, OBJ_SWORD), "discoveries are saved");
}

static void t_big_map(void)
{
    static const u16 close[] = {BUTTON_A, 0, 0, 0, 0xFFFF};
    Unit *u = arena();
    u8 x, y, white = 0, magenta = 0;
    u16 lit = 0;
    int ended = test_overview_ended;
    game.portal_open = TRUE;
    game.portal_x = 14;
    game.portal_y = 10;
    update_sight();
    SCRIPT(close);
    bigmap_screen();
    CHECK_EQ(test_overview_w, 46, "the whole map");
    CHECK_EQ(test_overview_ended, ended + 1, "the window is repainted afterwards");
    CHECK_EQ(test_overview[10 * 46 + 10], C_BRIGHT_WHITE, "the own wizard is white");
    for (y = 0; y < 46; y++)
        for (x = 0; x < 46; x++) {
            u8 c = test_overview[y * 46 + x];
            white += c == C_BRIGHT_WHITE;
            magenta += c == C_BRIGHT_MAGENTA;
            lit += c != C_BLACK;
        }
    CHECK_EQ(white, 1, "one own unit");
    CHECK(lit < 46 * 46 && lit > 20, "only what was explored is shown");
    CHECK_EQ(test_overview[0], C_BLACK, "the far corner is unexplored");
    /* the portal shows once its field is explored */
    {
        u8 seen = sight_explored(&p1_sight, &world, 14, 10);
        SCRIPT(close);
        bigmap_screen();
        CHECK_EQ(test_overview[10 * 46 + 14], seen ? C_BRIGHT_MAGENTA : C_BLACK, "the open portal is magenta");
        (void)magenta;
    }
    /* an enemy is shown only where it is seen right now */
    {
        Unit *e = enemy_at(2, 0, CR_GOBLIN);
        u8 vis;
        update_sight();
        vis = sight_visible(&p1_sight, &world, e->x, e->y);
        SCRIPT(close);
        bigmap_screen();
        CHECK_EQ(test_overview[e->y * 46 + e->x], vis ? C_BRIGHT_RED : (u8)test_overview[e->y * 46 + e->x],
                 "a visible enemy is red");
        /* hide it by moving far away: then it must not be shown */
        e->x = 40;
        e->y = 40;
        update_sight();
        SCRIPT(close);
        bigmap_screen();
        CHECK(test_overview[40 * 46 + 40] != C_BRIGHT_RED, "an unseen enemy stays secret");
    }
    /* the enemy wizard's last known position */
    foe_x = 30;
    foe_y = 5;
    foe_round = 3;
    SCRIPT(close);
    bigmap_screen();
    CHECK_EQ(test_overview[5 * 46 + 30], C_BRIGHT_YELLOW, "last seen position marked");
    CHECK(ever("Runde 3"), "and the round it was seen in");
    (void)u;
}

static void t_message_log(void)
{
    static const u16 close[] = {BUTTON_A, 0, 0, 0, 0xFFFF};
    boot(0);
    log_head = log_count = 0;
    SCRIPT(close);
    log_screen();
    CHECK(page_has("Noch keine"), "an empty log says so");
    msg(1, C_BRIGHT_GREEN, "Erste Meldung.");
    msg(1, C_GREY, "");                         /* clearing a line is no message */
    msg(2, C_BRIGHT_RED, "Eine ziemlich lange zweite Meldung, die umbrochen werden muss.");
    CHECK_EQ(log_count, 2, "two messages logged");
    SCRIPT(close);
    log_screen();
    CHECK(page_has("Erste Meldung"), "the first message");
    CHECK(page_has("zweite Meldung") || page_has("ziemlich"), "the long one, wrapped");
    {   /* the ring keeps the newest */
        u8 i;
        for (i = 0; i < 40; i++) {
            char t[24];
            snprintf(t, sizeof t, "Nummer %d", i);
            msg(1, C_BRIGHT_WHITE, t);
        }
        CHECK_EQ(log_count, LOG_N, "the ring is full");
        SCRIPT(close);
        log_screen();
        CHECK(page_has("Nummer 39"), "the newest is shown");
        CHECK(!page_has("Erste Meldung"), "the oldest fell out");
    }
}

static void t_tutorial_flow(void)
{
    static const u16 intro[] = {BUTTON_B, 0, 0xFFFF};
    u8 step;
    boot(0);
    SCRIPT(intro);                    /* the intro pages shown at the start */
    new_game(&SCENARIOS[TUTORIAL_INDEX]);
    CHECK(tutorial_on, "the tutorial runs");
    CHECK(cur_sc->tutorial, "scenario flag");
    CHECK(!turns.round1_lock, "movement allowed at once");
    CHECK_EQ(tut.step, TUT_MOVE, "starts with moving");
    CHECK(strlen(tutorial_hint(TUT_MOVE)) > 5, "a hint for the first step");
    for (step = 0; step < TUT_DONE; step++)
        CHECK(strlen(tutorial_hint(step)) > 5, "every step has a hint");
    CHECK_EQ(strlen(tutorial_hint(TUT_DONE)), 0, "no hint after the last step");
    CHECK(strstr(test_msg[2], "Bewege") != NULL || strstr(test_msg[2], "D-Pad") != NULL, "the hint is shown");
    /* walk: the first step is done when the wizard left its field */
    {
        u8 dir, moved = 0;
        for (dir = 0; dir < 4 && !moved; dir++) {
            static const u8 M[4] = {ARROW_RIGHT, ARROW_DOWN, ARROW_LEFT, ARROW_UP};
            do_step(M[dir]);
            moved = me()->x != tut.wiz_x || me()->y != tut.wiz_y;
        }
        CHECK(moved, "the wizard can walk");
        frame();
        CHECK(tut.step > TUT_MOVE, "the step advanced");
    }
    /* C switches the unit and satisfies its step */
    tap(BUTTON_C);
    CHECK(tut.switch_done, "the switch was noted");
    /* no wildlife and no random world in the tutorial */
    CHECK(!turns.wildlife, "no wild animals");
}

static void t_main_menu_labels(void)
{
    char l[28];
    u8 i;
    static const char *const want[10] = {"1 The Many Coloured Land", "2 Slayer's Dungeon (bald)", "3 Ragaril's Domain (bald)",
                                         "Tutorial", "Spielstand laden", "Zauberer gestalten",
                                         "Zauberer zuruecksetzen", "Optionen", "Hilfe", "Lexikon"};
    for (i = 0; i < 10; i++) {
        scenario_label(i, l);
        CHECK(strcmp(l, want[i]) == 0, want[i]);
        CHECK(strlen(l) <= 27, "fits the menu");
    }
}

static void t_debug_block(void)
{
    arena();
    tick(3);
    CHECK_EQ(g_dbg.magic, 0x4C44, "debug magic");
    CHECK_EQ(g_dbg.x, me()->x, "debug x");
    CHECK_EQ(g_dbg.y, me()->y, "debug y");
    CHECK_EQ(g_dbg.round, turns.round, "debug round");
    {
        u16 h = g_dbg.hash;
        me()->x = 20;
        tick(1);
        if (g_dbg.hash == h)
            printf("  hash %u unit %d at %d,%d count %d frames %u ended %d\n", h, active(), me()->x, me()->y, world.unit_count, g_dbg.frames, game_ended);
        CHECK(g_dbg.hash != h, "the world hash follows moves");
    }
}

/* ---- what the upstream rules added (D69-D75, F8) ---- */

static void t_enemy_in_view_warns_with_direction(void)
{
    Unit *e;
    arena();
    alerts_reset();
    update_sight();
    clear_log();
    e = enemy_at(4, 0, CR_GOBLIN);
    (void)e;
    update_sight();
    CHECK(said("im Osten"), "the newcomer is announced with its direction");
    CHECK_EQ(test_msg_colour[2], C_BRIGHT_RED, "in red");
    clear_log();
    memset(test_msg[2], 0, sizeof test_msg[2]);
    update_sight();
    CHECK(!said("im Osten"), "an enemy already in view is not announced again");
}

static void t_noise_is_heard_not_seen(void)
{
    Unit *u = arena();
    alerts_reset();
    world_noise(&world, u->x + 12, u->y, NOISE_DEATH, OWN_P2);
    memset(&p1_sight, 0, sizeof p1_sight);               /* nothing in view: heard only */
    clear_log();
    CHECK(report_noises(), "a noise within 16 fields is reported");
    CHECK(said("Todesschrei im Osten, 12 Felder"), "what, where, how far");
    CHECK_EQ(heard_n, 1, "and marked for the big map");
    CHECK_EQ(world.noise_n, 0, "the listening period starts over");
    world_noise(&world, u->x + 20, u->y, NOISE_FIGHT, OWN_P2);
    clear_log();
    CHECK(!report_noises(), "too far away to hear");
    CHECK_EQ(heard_n, 0, "nothing on the map");
}

static void t_noise_in_view_is_not_reported(void)
{
    Unit *u = arena();
    alerts_reset();
    update_sight();
    world_noise(&world, u->x + 5, u->y, NOISE_FIGHT, OWN_P2);
    CHECK(sight_visible(&p1_sight, &world, u->x + 5, u->y), "the spot is in view");
    clear_log();
    CHECK(!report_noises(), "a fight in plain view is seen, not heard");
    CHECK_EQ(heard_n, 0, "and not marked on the map");
}

static void t_noise_loudest_wins(void)
{
    Unit *u = arena();
    alerts_reset();
    world_noise(&world, u->x + 3, u->y, NOISE_SPELL, OWN_P2);
    world_noise(&world, u->x, u->y + 9, NOISE_FIGHT, OWN_P2);
    world_noise(&world, u->x - 8, u->y, NOISE_DEATH, OWN_P2);
    memset(&p1_sight, 0, sizeof p1_sight);
    clear_log();
    report_noises();
    CHECK(said("Todesschrei im Westen, 8 Felder +2"), "the death cry beats fight and spell");
}

static void t_auto_end_turn(void)
{
    u8 round;
    arena();
    round = turns.round;
    tick(3);
    CHECK_EQ(turns.round, round, "off by default: units done changes nothing");
    auto_end = TRUE;
    CHECK(turn_units_left(&turns, &world), "a unit still has something to do");
    tick(3);
    CHECK_EQ(turns.round, round, "no round change while a unit can still act");
    turn_finish_unit(&turns, &world);
    tick(3);
    CHECK_EQ(turns.round, round + 1, "every unit done: the round ends by itself");
    auto_end = FALSE;
}

static void t_auto_end_option_persists(void)
{
    arena();
    auto_end = TRUE;
    wizards_save();
    auto_end = FALSE;
    wizards_load();
    CHECK(auto_end, "the switch survives a restart");
    auto_end = FALSE;
}

static void t_aim_height_toggles_with_a(void)
{
    Unit *u = arena();
    start_aim(TA_SPELL, SP_MAGIC_BOLT);
    CHECK(!target_air, "a grounded caster aims at the ground first");
    tick(1);
    tap(BUTTON_A);
    CHECK(target_air, "A switches to the air");
    CHECK(said("LUFT"), "and says so");
    tap(BUTTON_A);
    CHECK(!target_air, "A switches back");
    CHECK_EQ(mode, MODE_TARGET, "aiming goes on");
    tap(BUTTON_B);
    u->flags |= UF_FLYING;
    start_aim(TA_SPELL, SP_MAGIC_BOLT);
    CHECK(target_air, "a flyer aims at its own height first");
    tap(BUTTON_B);
}

static void t_bolt_hits_only_the_aimed_height(void)
{
    Unit *u = arena(), *e;
    u8 con, mana;
    e = enemy_at(3, 0, CR_GOBLIN);
    e->con = e->con_max = 200;
    con = e->con;
    mana = u->mana;
    mode = MODE_TARGET;
    target_kind = TA_SPELL;
    target_spell = SP_MAGIC_BOLT;
    target_air = TRUE;                   /* the goblin walks: an air bolt passes by */
    look_x = u->x + 3;
    look_y = u->y;
    aim_confirm();
    CHECK(e->con == con, "a bolt into the air does not touch a ground unit");
    (void)mana;
}

static void t_remains_are_named_when_looking(void)
{
    Unit *u = arena();
    world_add_remains(&world, u->x + 1, u->y, CR_GOBLIN, OWN_P2);
    do_action(MA_LOOK);
    tap(BUTTON_RIGHT);
    CHECK(strncmp(test_msg[1], "Skelett:", 8) == 0, "the skeleton says what it was");
    tap(BUTTON_B);
}

static void t_save_keeps_remains_and_ap_factor(void)
{
    Unit *u = arena();
    world_add_remains(&world, u->x + 2, u->y, CR_GOBLIN, OWN_P2);
    world_set_ap_scale(&world, 128);
    CHECK(save_game(), "saved");
    world_add_remains(&world, u->x + 3, u->y, CR_GOBLIN, OWN_P2);
    world_set_ap_scale(&world, 100);
    CHECK(load_game(), "loaded");
    CHECK_EQ(world.remains_n, 1, "the remains came back");
    CHECK_EQ(world.ap_scale, 128, "and the AP factor");
}

static void t_level1_has_the_bigger_ap_budget(void)
{
    boot(0);
    CHECK_EQ(world.ap_scale, 128, "scenario 1 has the 46x46 AP factor (D71)");
    CHECK(me()->ap_max >= 40, "the wizard's budget grew with it");
}

static void t_ai_phase_is_announced(void)
{
    boot(0);
    clear_log();
    end_turn();
    CHECK(said("ist am Zug"), "the player is told that the opponent plays (E-8)");
}

static void t_locked_worlds_are_marked(void)
{
    char l[28];
    scenario_label(1, l);
    CHECK(strstr(l, "(bald)") != NULL, "world 2 is shown as not playable yet (D75)");
    scenario_label(0, l);
    CHECK(strstr(l, "(bald)") == NULL, "world 1 is open");
}

typedef struct {
    const char *name;
    void (*fn)(void);
} Test;

#define T(n) {#n, n}
static const Test TESTS[] = {
    T(t_boot_scenarios), T(t_round1_lock), T(t_move_orthogonal), T(t_move_diagonal_chord),
    T(t_move_held_repeats), T(t_move_out_of_ap), T(t_wall_bump), T(t_door_open_close_lock), T(t_chest),
    T(t_melee), T(t_melee_kill_credits_vp), T(t_pickup_drop_wield_eat), T(t_pickup_menu),
    T(t_pickup_nothing), T(t_drop_one_at_a_time), T(t_pickup_fails_without_ap), T(t_cast_at_self_cancels), T(t_throw_item), T(t_aim_cancel), T(t_look_mode), T(t_cast_bolt),
    T(t_cast_out_of_range_costs_nothing), T(t_summon), T(t_not_a_caster), T(t_end_turn_round_and_save),
    T(t_start_hold_ends_turn), T(t_start_tap_finishes_unit), T(t_next_unit), T(t_action_menu_contents),
    T(t_action_menu_open_cancel), T(t_action_menu_remembers_last), T(t_menu_run_navigation), T(t_quit_menu),
    T(t_save_load_roundtrip), T(t_save_rejects_damage), T(t_save_interrupted_is_never_a_wrong_slot), T(t_save_fits_sram), T(t_portal_escape_win),
    T(t_wizard_dies_lose), T(t_game_ends_without_portal_escape), T(t_takeoff_land),
    T(t_unit_not_flying_cannot_land), T(t_mount), T(t_apply_wrong_target), T(t_ai_phase_runs),
    T(t_wizard_settings_roundtrip), T(t_designer_attributes), T(t_designer_shop), T(t_designer_menu_and_save), T(t_options_persist), T(t_campaign_xp_and_level), T(t_campaign_second_clear_gives_no_level), T(t_standard_set_offered_when_empty), T(t_designed_book_is_not_overwritten), T(t_text_books_are_well_formed), T(t_help_viewer_navigation), T(t_lexicon_discoveries), T(t_lexicon_spells_page), T(t_lexicon_survives_settings), T(t_message_log), T(t_big_map), T(t_tutorial_flow), T(t_main_menu_labels), T(t_enemy_in_view_warns_with_direction), T(t_noise_is_heard_not_seen), T(t_noise_in_view_is_not_reported), T(t_noise_loudest_wins), T(t_auto_end_turn), T(t_auto_end_option_persists), T(t_aim_height_toggles_with_a), T(t_bolt_hits_only_the_aimed_height), T(t_remains_are_named_when_looking), T(t_save_keeps_remains_and_ap_factor), T(t_level1_has_the_bigger_ap_budget), T(t_locked_worlds_are_marked), T(t_ai_phase_is_announced), T(t_long_play_all_scenarios), T(t_fuzz_random_buttons), T(t_fuzz_with_chaos), T(t_debug_block),
};

int main(int argc, char **argv)
{
    u16 i, ran = 0, failed_tests = 0;
    const char *only = argc > 1 ? argv[1] : NULL;
    setvbuf(stdout, NULL, _IONBF, 0);
    test_set_variants(MAPBIN_MANY_COLOURED_LAND, MAPBIN_MANY_COLOURED_LAND_LEN);
    for (i = 0; i < sizeof TESTS / sizeof TESTS[0]; i++) {
        int before;
        if (only) {                     /* "a,b": any of the parts */
            char buf[128], *tok, *rest = buf;
            bool hit = FALSE;
            snprintf(buf, sizeof buf, "%s", only);
            while ((tok = strsep(&rest, ",")) != NULL)
                if (*tok && strstr(TESTS[i].name, tok))
                    hit = TRUE;
            if (!hit)
                continue;
        }
        current_test = TESTS[i].name;
        if (getenv("TEST_VERBOSE"))
            printf("... %s\n", TESTS[i].name);
        before = test_failures;
        if (setjmp(abort_test) == 0)
            TESTS[i].fn();
        ran++;
        printf("%s %s\n", test_failures == before ? "ok  " : "FAIL", TESTS[i].name);
        if (test_failures != before)
            failed_tests++;
    }
    printf("\n%d tests, %d checks, %d failed tests, %d failed checks\n", ran, test_checks, failed_tests,
           test_failures);
    return test_failures ? 1 : 0;
}
