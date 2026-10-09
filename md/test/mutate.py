#!/usr/bin/env python3
"""
Mutation check of the host tests: change one thing in the frontend at a time and make sure the
tests notice. A mutant that survives means a critical behaviour is not covered.

    python3 md/test/mutate.py            # all mutants
    python3 md/test/mutate.py save       # only those whose name contains "save"
"""
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
SRC = ROOT / "md" / "src"

# (name, file, old, new, which tests should catch it)
MUTANTS = [
    ("eat twice", "game_md.c",
     '        if (items_eat(&world, me)) {\n            sound_play(SND_EAT);',
     '        items_eat(&world, me);\n        if (items_eat(&world, me)) {\n            sound_play(SND_EAT);', "game"),
    ("step without sound", "game_md.c",
     '        sound_play(SND_STEP);\n        if (game_try_enter_portal', '        if (game_try_enter_portal', "game"),
    ("round 1 lock ignored", "game_md.c",
     '    if (!turn_may_move(&turns)) {\n        msg(1, C_BRIGHT_RED, "Runde 1: nur Zaubern (PM 7).");\n        return;\n    }\n    if (world_move_unit',
     '    if (world_move_unit', "game"),
    ("start hold too long", "game_md.c", '#define HOLD_CS 60', '#define HOLD_CS 6000', "game"),
    ("start hold instant", "game_md.c", '#define HOLD_CS 60', '#define HOLD_CS 0', "game"),
    ("autosave off", "game_md.c", '    if (!end_pending && save_game() && !heard)', '    if (FALSE && save_game() && !heard)', "game"),
    ("tone effect leaves PSG loud", "sound_md.c", '        PSG_setEnvelope(TONE_CH, PSG_ENVELOPE_MIN);', '        PSG_setEnvelope(TONE_CH, PSG_ENVELOPE_MAX);', "units"),
    ("song rest leaves PSG loud", "sound_md.c", '        PSG_setEnvelope(c->psg, PSG_ENVELOPE_MIN);', '        PSG_setEnvelope(c->psg, PSG_ENVELOPE_MAX);', "units"),
    ("auto end ignores unfinished units", "game_md.c", 'world.unit_count && !turn_units_left(&turns, &world)) {      /* D72 */',
     'world.unit_count) {      /* D72 */', "game"),
    ("noise heard from anywhere", "game_md.c", 'world_distance(&world, world.units[i].x, world.units[i].y, n->x, n->y) <= HEAR_FIELDS;',
     'world_distance(&world, world.units[i].x, world.units[i].y, n->x, n->y) <= 250;', "game"),
    ("noise even when seen", "game_md.c", '        if (sight_visible(&p1_sight, &world, n->x, n->y))\n            continue;                                           /* seen, not just heard */',
     '', "game"),
    ("aim height never toggles", "game_md.c", '                target_air = !target_air;          /* CAST-A / CAST-G (F8) */',
     '', "game"),
    ("newcomer warned every time", "game_md.c", '        memcpy(seen_ids, now_ids, sizeof seen_ids);', '', "game"),
    ("save checksum unchecked", "game_md.c", '    return stored == sram_sum ? idx : -1;', '    return idx;', "game"),
    ("save checksum too short", "game_md.c",
     '    for (i = 0; i < len; i++)\n        sram_sum = ((sram_sum << 5) | (sram_sum >> 27)) ^ SRAM_readByte(SAVE_HEADER + i);',
     '    for (i = 0; i < len - 40; i++)\n        sram_sum = ((sram_sum << 5) | (sram_sum >> 27)) ^ SRAM_readByte(SAVE_HEADER + i);', "game"),
    ("save magic first", "game_md.c",
     "    SRAM_writeByte(0, 'L');                      /* first byte of the magic, last */\n    SRAM_disable();",
     "    SRAM_disable();", "game"),
    ("load forgets books", "game_md.c", '    sget(books, sizeof books);\n    sget(&loads, 1);', '    sget(&loads, 1);', "game"),
    ("load keeps stale hooks", "game_md.c", '    cur_sc = &SCENARIOS[idx];\n    area_reset();', '    cur_sc = &SCENARIOS[idx];\n    area_reset();\n    goto skip_bind;', "game"),
    ("menu does not wrap", "game_md.c", '            sel = sel ? sel - 1 : n - 1;', '            sel = sel ? sel - 1 : 0;', "game"),
    ("menu cancel returns pick", "game_md.c",
     '            sound_play(SND_BACK);\n            ui_overlay_close();\n            g_dbg.menus--;\n            return -1;',
     '            sound_play(SND_BACK);\n            ui_overlay_close();\n            g_dbg.menus--;\n            return (s8)sel;', "game"),
    ("quit without save saves", "game_md.c", '            if (q == 0)\n                save_game();', '            save_game();', "game"),
    ("aim at self casts", "game_md.c", 'look_x == u->x && look_y == u->y) {\n        msg(1, C_GREY, "Abgebrochen.");', 'look_x == 9999 && look_y == u->y) {\n        msg(1, C_GREY, "Abgebrochen.");', "game"),
    ("out of range cast pays", "game_md.c", '            msg(1, C_BRIGHT_RED, "Ausser Reichweite oder Sicht.");\n            return;\n        }\n        if (cr == CAST_BAD_TERRAIN)', '            msg(1, C_BRIGHT_RED, "Ausser Reichweite oder Sicht.");\n            world.units[wiz].mana = 0;\n            return;\n        }\n        if (cr == CAST_BAD_TERRAIN)', "game"),
    ("bolt out of range pays", "game_md.c",
     '        if (!ok) {\n            msg(1, C_BRIGHT_RED, "Ausser Reichweite oder Sicht.");\n            return;',
     '        if (!ok) {\n            msg(1, C_BRIGHT_RED, "Ausser Reichweite oder Sicht.");\n            world.units[wiz].mana = 0;\n            return;', "game"),
    ("pickup message always ok", "game_md.c", '            msg(1, C_BRIGHT_RED, "Zu schwer, kein Platz oder AP.");\n        }\n        frame();', '            msg(1, C_BRIGHT_GREEN, "Aufgehoben.");\n        }\n        frame();', "game"),
    ("torus step wrong cell", "torus.h", 't->rtx = (t->rtx + (dx > 0 ? 3 : PLANE_W - 3)) % PLANE_W;', 't->rtx = (t->rtx + (dx > 0 ? 2 : PLANE_W - 3)) % PLANE_W;', "units"),
    ("torus slot wraps late", "torus.h", 't->rox = dx > 0 ? (t->rox + 1 == RING ? 0 : t->rox + 1)', 't->rox = dx > 0 ? (t->rox + 1 == RING + 1 ? 0 : t->rox + 1)', "units"),
    ("ui messages overlap", "ui_md.c", '#define MSG1_ROWS 4', '#define MSG1_ROWS 6', "units"),
    ("ui overlay too small", "ui_md.c", '    for (r = 0; r < MAP_TILES; r++)\n        ui_text(0, r, C_BLACK, "", MAP_TILES);', '    for (r = 0; r < MAP_TILES - 1; r++)\n        ui_text(0, r, C_BLACK, "", MAP_TILES);', "units"),
    ("ui clears nothing on close", "ui_md.c", '            set_cell(c, r, 0);                  /* transparent: the map shows */', '            (void)c;', "units"),
    ("snprintf number loop macro side effect", "libc_md.c",
     '            while (len) {                  /* (PUT does not evaluate its argument once the buffer is full) */\n                char dch = tmp[len - 1];\n                len--;\n                PUT(dch);\n            }',
     '            while (len)\n                PUT(tmp[--len]);', "units"),
    ("snprintf off by one", "libc_md.c", '    if (n)\n        buf[pos < n ? pos : n - 1] = 0;', '    if (n)\n        buf[pos < n ? pos : n] = 0;', "units"),
]


def run(which):
    r = subprocess.run([str(ROOT / "md" / "test" / "run.sh"), which], capture_output=True, text=True)
    return r.returncode, r.stdout + r.stderr


def main():
    only = sys.argv[1] if len(sys.argv) > 1 else ""
    rc, out = run("all")
    if rc != 0:
        print("the tests fail without any mutation:\n", out[-1500:])
        return 1
    survived = []
    for name, fname, old, new, which in MUTANTS:
        if only and only not in name:
            continue
        path = SRC / fname
        text = path.read_text()
        if old not in text:
            print(f"?? {name}: the pattern is gone, update the mutant")
            survived.append(name + " (stale)")
            continue
        path.write_text(text.replace(old, new, 1))
        try:
            rc, out = run(which)
        finally:
            path.write_text(text)
        killed = rc != 0
        print(("killed   " if killed else "SURVIVED ") + name)
        if not killed:
            survived.append(name)
    print(f"\n{len(survived)} survivors" + (": " + ", ".join(survived) if survived else ""))
    return 1 if survived else 0


if __name__ == "__main__":
    sys.exit(main())
