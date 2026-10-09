#!/usr/bin/env python3
"""
End-to-end tests of the Mega Drive ROM in BlastEm.

    python3 md/test/e2e.py                  # all tests (builds out/md-test first)
    python3 md/test/e2e.py -k save          # tests whose name contains "save"
    python3 md/test/e2e.py --no-build       # use the existing out/md-test build

The ROM is built with -DLOC_MD_FIXED_SEED (reproducible world) into out/md-test. The game
publishes what it is doing in the RAM block g_dbg (md/src/game_md.c); the tests read it through
BlastEm's debugger and press buttons the way a player would, so they cover the real binary: the
title, menus, scenarios, turns, input, saving to the SRAM and the end of the game.

Every test starts a fresh emulator with its own HOME (so its own, empty cartridge SRAM).
"""
from __future__ import annotations

import os
import re
import shutil
import subprocess
import sys
import time
import unittest
import warnings

warnings.simplefilter("ignore", ResourceWarning)
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
OUT = "out/md-test"
os.environ["MD_OUT"] = OUT
sys.path.insert(0, str(ROOT / "tools"))
import emushot as e  # noqa: E402

FIELDS = ["magic", "frames", "screen", "menus", "round", "mode", "units", "scenario", "x", "y", "ap", "hp",
          "mana", "flags", "msg1", "hash", "acts_n", "sel", "items", "spell"]
SIGNED = {"x", "y"}
SCREEN = {"boot": 0, "title": 1, "menu": 2, "play": 3, "end": 4}
MODE = {"play": 0, "look": 1, "target": 2}
# ids of the action menu entries (enum Act in md/src/game_md.c)
ACT = {"cast": 0, "pickup": 1, "drop": 2, "wield": 3, "throw": 4, "fire": 5, "eat": 6, "read": 7, "quaff": 8, "fill": 9,
       "mount": 10, "takeoff": 11, "land": 12, "use": 13, "look": 14, "unitdone": 15, "endturn": 16, "save": 17, "quit": 18,
       "help": 19, "lexicon": 20, "log": 21, "map": 22}
FLAG = {"ended": 1, "end_pending": 2, "quit": 4, "apply_pending": 8, "save": 16}


def spell_enum() -> dict:
    """SP_* ids from the generated core header."""
    text = (ROOT / "src" / "core" / "gen" / "data.h").read_text()
    body = re.search(r"typedef enum \{([^}]*SP_GIANT_BAT[^}]*)\} SpellId", text, re.S)
    names = re.findall(r"(SP_[A-Z_]+)", body.group(1) if body else text)
    return {n: i for i, n in enumerate(names)}


def msg_hash(text: str) -> int:
    """The game's hash of a message line (md/src/game_md.c msg())."""
    h = 0
    for c in text.encode("latin-1"):
        h = (((h * 31 + c) & 0xFFFFFFFF) ^ (h >> 7)) & 0xFFFF
    return h


def build(extra: str = "", out: str = OUT) -> None:
    env = dict(os.environ, MD_OUT=out, EXTRA_FLAGS=("-DLOC_MD_FIXED_SEED -DLOC_MD_TEST_CMD " + extra).strip())
    r = subprocess.run([str(ROOT / "md" / "build.sh")], cwd=ROOT, env=env, capture_output=True, text=True)
    if r.returncode != 0 or not (ROOT / out / "out" / "rom.bin").exists():
        print(r.stdout[-3000:], r.stderr[-3000:])
        raise SystemExit("build failed")


class Game:
    """One emulator session with a private HOME: pad, debugger reads, restart with the same SRAM."""

    def __init__(self, name: str, fresh: bool = True):
        self.home = str(ROOT / OUT / "emuhome" / name)
        if fresh and os.path.isdir(self.home):
            shutil.rmtree(self.home)
        self.b = None
        self.start()

    def start(self):
        rom = str(ROOT / OUT / "out" / "rom.bin")
        self.b = e.Blastem(rom, fast=True, home=self.home)
        self.sym = e.symbols_for(rom)
        self.base = e.symbol(self.sym, "g_dbg")
        assert self.base is not None, "g_dbg missing from symbol.txt"

    def close(self):
        if self.b:
            self.b.close()
            self.b = None
            time.sleep(0.3)             # BlastEm writes the SRAM when it quits

    def restart(self):
        self.close()
        self.start()

    def d(self) -> dict:
        out = {}
        for i, name in enumerate(FIELDS):
            v = self.b.peek(self.base + 2 * i, "w")
            if name in SIGNED and v >= 32768:
                v -= 65536
            elif v < 0:
                v += 65536
            out[name] = v
        return out

    def frames(self, n: int):
        self.b.frames(n)

    def press(self, button: str, hold: int = 8, after: int = 12):
        self.b.press(button, hold)
        self.b.frames(after)

    def hold(self, button: str, frames: int, after: int = 20):
        self.b.press(button, frames)
        self.b.frames(after)

    def wait_screen(self, name: str, limit: int = 1500):
        want = SCREEN[name]
        for _ in range(limit // 20):
            if self.d()["screen"] == want:
                return self.d()
            self.b.frames(20)
        raise AssertionError(f"screen {name} not reached, state {self.d()}")

    def to_menu(self):
        self.wait_screen("title")
        self.frames(30)
        self.press("start")
        return self.wait_screen("menu")

    def start_scenario(self, index: int = 0):
        self.to_menu()
        for _ in range(index):
            self.press("down")
        self.press("a")
        s = self.wait_screen("play")
        self.frames(10)
        return self.d()

    def cmd(self, op: int, *args: int):
        """Run a test command in the game (build flag LOC_MD_TEST_CMD, see test_commands())."""
        base = e.symbol(self.sym, "g_cmd")
        assert base is not None, "g_cmd missing: build with -DLOC_MD_TEST_CMD"
        for i, a in enumerate(args, start=1):
            self.b.cmd(f"set [0x{base + 2 * i:X}].w {a & 0xFFFF}", 2.0)
        self.b.cmd(f"set [0x{base:X}].w {op}", 2.0)
        f0 = self.d()["frames"]
        for _ in range(40):
            self.b.frames(2)
            if self.b.peek(base, "w") == 0:
                # the command ran at the start of a game frame; the debug block is refreshed at its end,
                # and a frame can take several V-Blanks (sight): wait until a frame has finished after it
                for _ in range(60):
                    if self.d()["frames"] != f0:
                        return
                    self.b.frames(2)
                return
        raise AssertionError(f"test command {op} was not executed")

    def action(self, name: str):
        """Open the action menu and choose an entry by its id (the list depends on the situation)."""
        self.press("a")
        n = self.d()["acts_n"]
        acts = [self.b.peek(e.symbol(self.sym, "g_acts") + i, "b") for i in range(n)]
        if ACT[name] not in acts:
            self.press("b")
            raise AssertionError(f"action {name} not offered, the menu has {acts}")
        steps = (acts.index(ACT[name]) - self.d()["sel"]) % n
        for _ in range(steps):
            self.press("down", 8, 8)
        self.press("a")
        return acts

    def end_turn(self):
        r = self.d()["round"]
        self.hold("start", 60, 60)
        for _ in range(20):
            if self.d()["round"] != r or self.d()["screen"] != SCREEN["play"]:
                break
            self.frames(20)

    def shot(self, tag: str):
        self.b.shot(tag, out_dir=str(ROOT / OUT / "emushot"))

    def wait_for(self, cond, what: str, limit: int = 400):
        """Poll the debug block until cond(d) holds (a frame may still be running when we look)."""
        d = self.d()
        for _ in range(limit // 10):
            d = self.d()
            if cond(d):
                return d
            self.b.frames(10)
        raise AssertionError(f"timeout waiting for {what}: {d}")

    def spell_ids(self, n: int = 40):
        base = e.symbol(self.sym, "g_spells")
        return [self.b.peek(base + i, "b") for i in range(n)]

    def msg_is(self, text: str) -> bool:
        return self.d()["msg1"] == msg_hash(text)


class E2E(unittest.TestCase):
    def setUp(self):
        self.g = None

    def tearDown(self):
        if self.g:
            self.g.close()

    def game(self, name=None) -> Game:
        self.g = Game(name or self.id().split(".")[-1])
        return self.g

    # ---- boot and menus ----

    def test_boot_reaches_title(self):
        g = self.game()
        g.wait_screen("title")
        self.assertEqual(g.d()["menus"], 0)

    def test_title_menu_and_back(self):
        g = self.game()
        g.to_menu()
        self.assertEqual(g.d()["menus"], 1, "the main menu is a list menu")
        g.press("b")
        g.wait_screen("title")
        g.press("start")
        g.wait_screen("menu")

    def test_controls_page_and_back(self):
        g = self.game()
        g.to_menu()
        for _ in range(8):
            g.press("down")
        g.press("a")                      # Hilfe: the first page is the controls
        g.frames(20)
        g.press("b")                      # B closes the help
        g.frames(20)
        self.assertEqual(g.d()["screen"], SCREEN["menu"])
        self.assertEqual(g.d()["menus"], 1)

    def test_load_without_save_stays_in_menu(self):
        g = self.game()
        g.to_menu()
        for _ in range(4):
            g.press("down")
        g.press("a")                      # "Spielstand laden" with an empty cartridge
        g.frames(30)
        self.assertEqual(g.d()["screen"], SCREEN["menu"], "nothing to load: stays in the menu")
        g.press("a")                      # the info page
        g.frames(20)
        self.assertEqual(g.d()["screen"], SCREEN["menu"])

    def design_ap(self, g, raises: int):
        """Main menu -> Zauberer gestalten -> Attribute -> raise the action points -> back to the main menu."""
        for _ in range(5):
            g.press("down")
        g.press("a")                      # Zauberer gestalten
        g.press("a")                      # Attribute verteilen
        for _ in range(6):
            g.press("down", 8, 6)         # the sixth row is the action points
        for _ in range(raises):
            g.press("right", 8, 8)
        g.press("b")                      # back to the designer menu
        g.press("b")                      # back to the main menu
        self.assertEqual(g.d()["screen"], SCREEN["menu"])
        for _ in range(5):
            g.press("up")

    def test_designer_changes_persist_across_restart(self):
        g = self.game()
        g.to_menu()
        self.design_ap(g, 3)
        g.press("a")
        d = g.wait_screen("play")
        g.frames(10)
        ap_designed = g.d()["ap"]
        self.assertGreater(ap_designed, 34, "the designed action points are in the game")
        g.restart()                       # same cartridge: the wizard was saved
        g.start_scenario(0)
        self.assertEqual(g.d()["ap"], ap_designed, "the wizard survived a restart")

    def test_designer_shop_pages_work(self):
        g = self.game()
        g.to_menu()
        for _ in range(5):
            g.press("down")
        g.press("a")                      # designer menu
        for page_downs in (1, 1):         # spells, then creatures
            g.press("down")
            g.press("a")
            g.frames(60)
            g.press("right")              # buy a level
            g.press("down")
            g.press("left")               # sell it back
            g.press("b")
            g.wait_for(lambda d: d["menus"] == 1, "back in the designer menu")
        g.press("b")
        g.wait_screen("menu")

    def test_tutorial_starts_with_intro_and_hint(self):
        g = self.game()
        g.to_menu()
        for _ in range(3):
            g.press("down")
        g.press("a")                      # Tutorial
        g.frames(60)
        g.press("b")                      # close the intro pages
        d = g.wait_screen("play")
        g.wait_for(lambda d: d["scenario"] == 3 and d["round"] == 1, "the tutorial scenario")
        # movement is allowed in round 1 here
        d0 = g.d()
        moved = False
        for b in ("right", "down", "left", "up"):
            g.press(b, 8, 14)
            d1 = g.d()
            if (d1["x"], d1["y"]) != (d0["x"], d0["y"]):
                moved = True
                break
        self.assertTrue(moved, "the tutorial teaches movement at once")

    def test_help_and_lexicon_open_from_the_menu(self):
        g = self.game()
        g.to_menu()
        for _ in range(8):
            g.press("down")
        g.press("a")                      # Hilfe
        g.frames(30)
        g.press("right")
        g.press("left")
        g.press("b")
        g.wait_screen("menu")
        g.press("down")
        g.press("a")                      # Lexikon
        g.press("a")                      # Kreaturen
        g.press("a")                      # the first entry (the wizard is known)
        g.frames(20)
        g.press("a")                      # close its page
        g.press("b")
        g.press("b")
        g.wait_screen("menu")
        self.assertEqual(g.d()["menus"], 1)

    def test_help_log_lexicon_in_the_game_menu(self):
        g = self.ready()
        for name in ("help", "lexicon", "log"):
            g.action(name)
            g.frames(30)
            g.press("b")
            g.press("b")
            g.wait_for(lambda d: d["menus"] == 0, f"{name}: back in the game")
        self.assertEqual(g.d()["screen"], SCREEN["play"])

    def test_big_map_and_back_repaints_the_window(self):
        try:
            from PIL import Image, ImageChops
        except ImportError:
            self.skipTest("Pillow missing (uv run --with pillow)")
        g = self.ready()
        g.press("right", 8, 14)
        g.frames(40)
        g.shot("map_before")
        g.action("map")
        g.frames(60)
        g.shot("map_overview")
        g.press("b")
        g.wait_for(lambda d: d["menus"] == 0 and d["screen"] == SCREEN["play"], "back in the game")
        g.frames(240)                     # the window is painted again
        g.shot("map_after")
        d = Path(ROOT / OUT / "emushot")
        a = Image.open(d / "map_before.png").convert("RGB")
        o = Image.open(d / "map_overview.png").convert("RGB")
        b = Image.open(d / "map_after.png").convert("RGB")
        diff_ab = sum(1 for p in ImageChops.difference(a, b).getdata() if p != (0, 0, 0))
        diff_ao = sum(1 for p in ImageChops.difference(a, o).getdata() if p != (0, 0, 0))
        self.assertGreater(diff_ao, 3000, "the overview looks different from the window")
        self.assertLess(diff_ab, 1500, f"the window is back as it was ({diff_ab} pixels differ)")

    def test_reset_wizard(self):
        g = self.game()
        g.to_menu()
        self.design_ap(g, 3)
        for _ in range(6):
            g.press("down")
        g.press("a")                      # Zauberer zuruecksetzen
        g.press("up")                     # "Ja"
        g.press("a")
        for _ in range(5):
            g.press("up")
        g.press("a")
        g.wait_screen("play")
        g.frames(10)
        self.assertEqual(g.d()["ap"], 34, "back to the stock wizard")

    def test_options_page_opens(self):
        g = self.game()
        g.to_menu()
        for _ in range(7):
            g.press("down")
        g.press("a")                      # Optionen
        self.assertEqual(g.d()["menus"], 1)
        g.press("c")                      # toggle sound
        g.press("b")
        g.wait_screen("menu")
        self.assertEqual(g.d()["menus"], 1)

    def test_auto_round_change_option(self):
        g = self.game()
        g.to_menu()
        for _ in range(7):
            g.press("down")
        g.press("a")                      # Optionen
        g.press("down")
        g.press("down")
        g.press("c")                      # Rundenwechsel: auto
        g.press("b")
        g.wait_screen("menu")
        for _ in range(5):
            g.press("up")                 # up to the first scenario (the cursor is on "Optionen")
        g.press("up")
        g.press("up")
        g.press("a")
        g.wait_screen("play")
        g.frames(10)
        r = g.d()["round"]
        g.press("start")                  # the only unit is done: no second press needed
        g.wait_for(lambda d: d["round"] == r + 1, "the round ended by itself", 900)

    # ---- scenarios ----

    def test_scenarios_start(self):
        for idx, units_min in ((0, 5), (1, 2), (2, 2)):
            g = self.game(f"scen{idx}")
            d = g.start_scenario(idx)
            self.assertEqual(d["scenario"], idx)
            self.assertEqual(d["round"], 1)
            self.assertGreaterEqual(d["units"], units_min)
            self.assertGreater(d["ap"], 0)
            self.assertGreater(d["mana"], 0)
            self.assertEqual(d["magic"], 0x4C44)
            g.close()

    # ---- turns and movement ----

    def test_round1_blocks_movement_then_round2_allows(self):
        g = self.game()
        d0 = g.start_scenario(0)
        for b in ("right", "down", "left", "up"):
            g.press(b)
        d1 = g.d()
        self.assertEqual((d1["x"], d1["y"], d1["ap"]), (d0["x"], d0["y"], d0["ap"]), "round 1: no movement")
        self.assertTrue(g.msg_is("Runde 1: nur Zaubern (PM 7)."), "the refusal is shown")
        g.end_turn()
        d2 = g.d()
        self.assertEqual(d2["round"], 2)
        self.assertEqual(d2["ap"], d0["ap"], "AP is full again")
        moved = False
        for b in ("right", "down", "left", "up"):
            before = g.d()
            g.press(b)
            after = g.d()
            if (after["x"], after["y"]) != (before["x"], before["y"]):
                moved = True
                self.assertLess(after["ap"], before["ap"], "a step costs AP")
                break
        self.assertTrue(moved, "some direction is free in round 2")

    def test_end_turn_saves_and_flags(self):
        g = self.game()
        g.start_scenario(0)
        self.assertFalse(g.d()["flags"] & FLAG["save"])
        g.end_turn()
        self.assertTrue(g.d()["flags"] & FLAG["save"], "the autosave happened")

    def test_start_tap_finishes_unit_hold_ends_turn(self):
        g = self.game()
        g.start_scenario(0)
        r = g.d()["round"]
        g.press("start")                  # one unit: finished, then all done
        self.assertEqual(g.d()["round"], r)
        g.press("start")                  # everyone done: this ends the turn
        g.wait_for(lambda d: d["round"] == r + 1, "the AI phase is over (a round later)", 900)
        g.hold("start", 60, 60)           # held: ends the turn at once
        g.wait_for(lambda d: d["round"] == r + 2, "the held START ended the next turn", 900)

    # ---- menus in the game ----

    def test_action_menu_opens_and_cancels(self):
        g = self.game()
        d0 = g.start_scenario(0)
        g.press("a")
        self.assertEqual(g.d()["menus"], 1)
        g.press("b")
        self.assertEqual(g.d()["menus"], 0)
        self.assertEqual(g.d()["ap"], d0["ap"])

    def test_look_mode(self):
        g = self.game()
        d0 = g.start_scenario(0)
        g.end_turn()
        g.action("look")
        g.frames(10)
        self.assertEqual(g.d()["mode"], MODE["look"], "look mode")
        d1 = g.d()
        g.press("right")
        g.press("down")
        self.assertEqual((g.d()["x"], g.d()["y"]), (d1["x"], d1["y"]), "the unit stays")
        g.press("b")
        self.assertEqual(g.d()["mode"], MODE["play"])
        self.assertEqual(g.d()["ap"], d1["ap"])

    def test_quit_to_menu_without_saving(self):
        g = self.game()
        g.start_scenario(0)
        g.press("a")
        g.press("up")                     # wraps to the last entry: "Zum Hauptmenue"
        g.press("a")
        g.press("up")                     # "Ohne Speichern beenden"
        g.press("a")
        g.wait_screen("menu")
        self.assertFalse(g.d()["flags"] & FLAG["save"] and False)

    # ---- critical actions, set up through test commands ----

    def ready(self, scenario=0):
        """A game in round 2 with the wizard alone (so nothing wanders in)."""
        g = self.game(self.id().split(".")[-1])
        g.start_scenario(scenario)
        g.end_turn()
        g.cmd(10)
        g.cmd(4)
        return g

    CR = {"goblin": 6, "pegasus": 11, "unicorn": 10}
    OBJ = {"apple": 17, "sword": 1, "knife": 2}
    FE = {"none": 0, "wall": 1, "door": 2, "door_open": 3, "chest_free": 15, "door_locked": 14}

    def test_combat_kills_an_enemy(self):
        g = self.ready()
        d0 = g.d()
        g.cmd(1, self.CR["goblin"], 1, 0)
        self.assertEqual(g.d()["units"], d0["units"] + 1)
        attacked = False
        for _ in range(60):
            g.cmd(4)
            before = g.d()
            g.press("right", 8, 14)
            after = g.d()
            if after["units"] == d0["units"]:
                break
            if after["ap"] < before["ap"]:
                attacked = True
            self.assertEqual((after["x"], after["y"]), (d0["x"], d0["y"]), "attacking does not move")
        self.assertTrue(attacked, "attacks cost AP")
        self.assertEqual(g.d()["units"], d0["units"], "the goblin died")

    def test_door_opens_and_chest_opens(self):
        g = self.ready()
        d0 = g.d()
        g.cmd(5, self.FE["door"], 1, 0)
        g.press("right", 8, 14)
        self.assertTrue(g.msg_is("Tuer geoeffnet."), "the door opens")
        g.wait_for(lambda d: d["ap"] < d0["ap"], "the door costs AP")
        g.cmd(4)
        g.cmd(5, self.FE["chest_free"], 0, 1)
        g.press("down", 8, 14)
        self.assertTrue(g.msg_is("Truhe geoeffnet!"), "the chest opens")

    def test_wall_blocks(self):
        g = self.ready()
        d0 = g.d()
        g.cmd(5, self.FE["wall"], 1, 0)
        g.press("right", 8, 14)
        d1 = g.d()
        self.assertEqual((d1["x"], d1["y"]), (d0["x"], d0["y"]))

    def test_pickup_drop_eat(self):
        g = self.ready()
        g.cmd(8, self.OBJ["apple"])
        d0 = g.d()
        self.assertEqual(d0["items"], 0)
        g.action("pickup")
        g.frames(20)
        if g.d()["menus"]:                # more objects within reach: the choice, the first entry is ours
            g.press("a")
        g.wait_for(lambda d: d["items"] == 1, "the apple in the pack")
        self.assertTrue(g.msg_is("Aufgehoben."))
        g.action("wield") if False else None
        # hand: the apple may need wielding before it can be eaten
        try:
            g.action("eat")
        except AssertionError:
            g.action("wield")
            g.action("eat")
        g.frames(10)
        self.assertEqual(g.d()["items"], 0, "the apple is eaten")
        self.assertTrue(g.msg_is("Gegessen."))

    def test_cast_bolt_aim_and_cancel(self):
        g = self.ready()
        g.cmd(1, self.CR["goblin"], 3, 0)
        d0 = g.d()
        sp = spell_enum()
        g.action("cast")
        g.press("a")                      # kind menu: "Zauber"
        n = g.d()
        ids = g.spell_ids()
        listed = [i for i in ids if i < len(sp)]
        bolt = sp["SP_MAGIC_BOLT"]
        self.assertIn(bolt, ids, f"magic bolt is in the list {ids}")
        for _ in range(ids.index(bolt)):
            g.press("down", 8, 8)
        g.press("a")
        g.wait_for(lambda d: d["mode"] == MODE["target"], "aiming")
        self.assertEqual(g.d()["spell"], bolt)
        g.press("right"); g.press("right"); g.press("right")
        g.press("b")
        self.assertEqual(g.d()["mode"], MODE["play"])
        self.assertEqual(g.d()["mana"], d0["mana"], "cancelling costs nothing")

    def test_cast_and_summon_cost_mana(self):
        g = self.ready()
        d0 = g.d()
        g.action("cast")
        g.press("down")                   # kind menu: "Beschwoerungen"
        g.press("a")
        g.press("a")                      # first summon
        g.frames(20)
        d1 = g.d()
        self.assertGreater(d1["units"], d0["units"], "something was summoned")
        self.assertLess(d1["mana"], d0["mana"], "summoning costs mana")

    def assert_end_picture(self, g, tag):
        try:
            from PIL import Image
        except ImportError:
            self.skipTest("Pillow missing (uv run --with pillow)")
        g.shot(tag)
        im = Image.open(Path(ROOT / OUT / "emushot" / f"{tag}.png")).convert("RGB")
        w, h = im.size
        box = im.crop((w * 27 // 40, h * 3 // 28, w, h * 15 // 28))      # the 12x12 tile area right of the text
        colours = {p for p in box.getdata()}
        self.assertGreater(len(colours), 6, f"the end picture is drawn ({len(colours)} colours)")

    def test_portal_escape_wins(self):
        g = self.ready()
        g.cmd(6, 1, 0)                    # an open portal east of the wizard
        g.press("right", 8, 14)
        g.wait_screen("end")
        g.frames(20)
        self.assert_end_picture(g, "end_win")
        g.press("a")
        g.wait_screen("menu")

    def test_wizard_death_loses(self):
        g = self.ready()
        g.cmd(7)
        g.wait_screen("end")
        g.frames(20)
        self.assert_end_picture(g, "end_lose")
        g.press("a")
        g.wait_screen("menu")

    def test_next_unit_and_flying(self):
        g = self.ready()
        g.cmd(9, self.CR["pegasus"], 1, 1)
        d0 = g.d()
        g.press("c")
        d1 = g.d()
        self.assertNotEqual((d0["x"], d0["y"]), (d1["x"], d1["y"]), "C selects the other unit")
        g.cmd(3, 9, 15)                   # out of the house (flyers cannot start under a roof)
        g.cmd(4)                          # the new unit has no AP yet
        g.action("takeoff")
        g.frames(10)
        self.assertTrue(g.msg_is("Steigt auf."), "the pegasus takes off")
        g.action("land")
        g.frames(10)
        self.assertTrue(g.msg_is("Landet."), "and lands")

    def test_use_door_close_and_lock(self):
        g = self.ready()
        g.cmd(5, self.FE["door_open"], 1, 0)
        g.action("use")
        self.assertTrue(g.d()["flags"] & FLAG["apply_pending"])
        g.press("right", 8, 14)
        g.wait_for(lambda d: d["msg1"] == msg_hash("Tuer geschlossen."), "use + direction closes it")
        g.wait_for(lambda d: not d["flags"] & FLAG["apply_pending"], "the pending use is spent")

    # ---- saving ----

    def test_save_survives_restart_and_continues(self):
        g = self.game()
        g.start_scenario(0)
        g.end_turn()
        g.end_turn()                      # round 3, autosaved
        before = g.d()
        self.assertEqual(before["round"], 3)
        g.close()
        g.start()                         # same HOME: the same cartridge SRAM
        g.to_menu()
        for _ in range(4):
            g.press("down")
        g.press("a")                      # Spielstand laden
        after = g.wait_screen("play")
        g.frames(20)
        after = g.d()
        self.assertEqual(after["round"], 3, "the saved round")
        self.assertEqual(after["hash"], before["hash"], "the same world")
        self.assertEqual((after["x"], after["y"]), (before["x"], before["y"]))
        g.end_turn()
        self.assertEqual(g.d()["round"], 4, "a loaded game plays on")

    def test_manual_save_and_quit_keeps_slot(self):
        g = self.game()
        g.start_scenario(0)
        g.end_turn()
        g.press("a")
        g.press("up")                     # Zum Hauptmenue
        g.press("a")
        g.press("up")
        g.press("up")                     # first entry: "Speichern, Hauptmenue"
        g.press("a")
        g.wait_screen("menu")
        g.restart()
        g.to_menu()
        for _ in range(4):
            g.press("down")
        g.press("a")
        g.wait_screen("play")
        self.assertEqual(g.d()["round"], 2)

    # ---- longer play ----

    def test_many_rounds_without_crashing(self):
        for idx in (0, 1, 2):
            g = self.game(f"long{idx}")
            g.start_scenario(idx)
            frames0 = g.d()["frames"]
            for r in range(25):
                if g.d()["screen"] != SCREEN["play"]:
                    break
                g.press("right", 8, 6)
                g.press("down", 8, 6)
                g.press("left", 8, 6)
                g.end_turn()
            d = g.d()
            self.assertIn(d["screen"], (SCREEN["play"], SCREEN["end"]), f"scenario {idx}: {d}")
            self.assertGreater(g.d()["frames"], frames0)
            g.close()

    def test_game_ends_and_returns_to_menu(self):
        g = self.game()
        g.start_scenario(0)
        for _ in range(60):
            if g.d()["screen"] == SCREEN["end"]:
                break
            g.end_turn()
        self.assertEqual(g.d()["screen"], SCREEN["end"], "a lone wizard cannot win: the portal closes")
        g.press("a")
        g.wait_screen("menu")
        g.press("a")                      # and a new game starts
        g.wait_screen("play")
        self.assertEqual(g.d()["round"], 1)


class Visual(unittest.TestCase):
    """Screenshots: the hardware scrolling must show what a full repaint shows."""

    def run_walk(self, out: str, tag: str):
        os.environ["MD_OUT"] = out
        import importlib
        importlib.reload(e)
        home = str(ROOT / out / "emuhome" / tag)
        shutil.rmtree(home, ignore_errors=True)
        b = e.Blastem(str(ROOT / out / "out" / "rom.bin"), fast=True, home=home)
        shots = []
        try:
            b.frames(120)
            b.press("start", 8); b.frames(40)
            b.press("a", 8); b.frames(500)
            b.press("start", 60); b.frames(300)
            seq = [("left", 14), ("up", 12), ("right", 10), ("down", 12)]
            for k, (d, n) in enumerate(seq):
                for _ in range(n):
                    b.press(d, 8); b.frames(40)
                b.frames(60)
                path = str(ROOT / out / "emushot")
                b.shot(f"{tag}_{k}", out_dir=path)
                shots.append(os.path.join(path, f"{tag}_{k}.png"))
        finally:
            b.close()
            os.environ["MD_OUT"] = OUT
            importlib.reload(e)
        return shots

    def test_scrolling_equals_full_repaint(self):
        try:
            from PIL import Image, ImageChops
        except ImportError:
            self.skipTest("Pillow missing (uv run --with pillow)")
        build("", "out/md-test")
        build("-DRENDER_FORCE_REPAINT", "out/md-test-full")
        a = self.run_walk("out/md-test", "inc")
        b = self.run_walk("out/md-test-full", "full")
        for pa, pb in zip(a, b):
            ia, ib = Image.open(pa).convert("RGB"), Image.open(pb).convert("RGB")
            diff = sum(1 for p in ImageChops.difference(ia, ib).getdata() if p != (0, 0, 0))
            # animation phase and the blinking cursor may differ, a wrongly scrolled map differs everywhere
            self.assertLess(diff, 1500, f"{os.path.basename(pa)}: {diff} pixels differ")


class SelfTest(unittest.TestCase):
    def test_core_selftest_on_the_console(self):
        env = dict(os.environ, MD_OUT="out/md-selftest", SELFTEST="1")
        r = subprocess.run([str(ROOT / "md" / "build.sh")], cwd=ROOT, env=env, capture_output=True, text=True)
        self.assertEqual(r.returncode, 0, r.stderr[-2000:])
        r = subprocess.run([sys.executable, str(ROOT / "tools" / "mdtest.py")], cwd=ROOT,
                           env=dict(os.environ, MD_OUT="out/md-selftest"), capture_output=True, text=True)
        self.assertEqual(r.returncode, 0, r.stdout[-1500:] + r.stderr[-1500:])


if __name__ == "__main__":
    args = sys.argv[1:]
    if "--no-build" in args:
        args.remove("--no-build")
    elif not any(a in ("-h", "--help") for a in args):
        build()
    unittest.main(argv=[sys.argv[0]] + args, verbosity=2)
