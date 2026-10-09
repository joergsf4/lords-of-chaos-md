# Tests of the Mega Drive frontend

| Level | What | Command | Time |
|---|---|---|---|
| Unit | libc subset, scroll bookkeeping (`torus.h`), text/bar UI (`ui_md.c`) | `md/test/run.sh units` | seconds |
| Game logic | `game_md.c` driven frame by frame against the real core: moves, doors, chests, melee, items, spells, aiming, turns, START tap/hold, menus, saving/loading (incl. power loss at every kind of byte), portal/win, death/loss, long play, random input and chaos fuzzing | `md/test/run.sh game [filter]` | seconds |
| Sanitizers | the same under AddressSanitizer + UBSan | `SANITIZE=1 md/test/run.sh` (`FUZZ_SEEDS=40` for longer fuzzing) | a minute |
| Mutation | change one behaviour at a time, the tests must notice | `python3 md/test/mutate.py [filter]` | 3 minutes |
| End to end | the real ROM in BlastEm: title, menus, scenarios, turns, input, saving to the SRAM across restarts, critical actions through test commands, the end of the game, hardware scrolling vs. full repaint, the core self-test on the console | `python3 md/test/e2e.py [-k name] [--no-build]` | 3 minutes |

`md/test/all.sh` runs everything, `md/test/all.sh quick` only the host tests.

How it works

- Host tests include `md/src/game_md.c` and replace SGDK with `md/test/stubs/genesis.h` and
  `stubs_hw.c` (SRAM backed by an array, V-Blank simulated, pad scripted or random) and `stubs_mod.c`
  (records messages, sounds, cursor). Input goes through the same V-Blank event queue as on the console.
- The e2e ROM is built with `-DLOC_MD_FIXED_SEED -DLOC_MD_TEST_CMD` into `out/md-test`: a reproducible
  world, and a mailbox (`g_cmd`) through which the test sets up situations (enemy next to the wizard,
  door, chest, portal, ...). The game publishes its state in `g_dbg`; the tests read it through BlastEm's
  debugger. Both exist only in test builds / are tiny in release builds.
- Found so far by these tests: `loc_memmove` overlapped with SGDK's `memcpy`, a stack overflow in the
  panel's "Hand:" line, actions that ran twice in the menu, a stale pad state that swallowed a press.
