# Mega Drive port

Frontend for the platform-free core (`src/core`), built with SGDK in Docker.

    md/build.sh              # -> out/md/out/rom.bin   (needs docker, rsync, uv)
    md/run.sh                # BlastEm
    SELFTEST=1 md/build.sh   # core self-test ROM; python3 tools/mdtest.py runs it in BlastEm
    EXTRA_FLAGS=-DLOC_MD_VIEWER md/build.sh    # map viewer instead of the game

## State

Title picture, main menu with the three scenarios (scenario 1 picks one of 16 terrain variants), end page. Playable with the stock wizard: turns and AI
phases, movement, melee, doors, chests, spells and summons, objects, mounts, flying, look mode,
portal and end screen. Saves to the cartridge SRAM at the end of each round (menu: load). Sound effects (XGM2 PCM) and PSG songs on the title/menu. Missing yet: wizard designer, lexicon, tutorial, help pages, visual effects (fx), options.

Pad: d-pad moves/attacks (two directions = diagonal). A opens the action menu: only what is
possible right now, the last action preselected, so A A repeats it. C next unit. START tap = unit
done (all done: ends the turn), START held = end the turn. B = back/cancel. X/Y/Z (six-button pad)
cast/pick up/look. While aiming or looking the d-pad moves the cursor, C confirms, B cancels.

## How it is built

- `src/render_md.c`: the 9x9 window of 24x24 fields on plane B, used as a torus: scrolling moves the
  plane's scroll registers and paints only the fields that came into view (check against what a slot
  already shows). Fields are composed per 8x8 block
  from the 4 bpp block data of their layers (fast path) or as chunky pixels (flyers, riders, ...).
  `tools/png2md.py` makes the tile data and chooses three map palette lines (14 colours each);
  line 3 is the UI line (the 16 logical colours of `colors.h`). The cursor is a sprite.
- `src/ui_md.c`: text and bars on plane A (shared glyph tiles per character/colour, opaque black
  backgrounds), menus over the map without touching the map.
- `src/game_md.c`: the game loop (port of the parts of `src/agon/main.c` the core does not own).
- `inc/`: shims so core files (real `<stdint.h>`, `bool`) and SGDK code (`u8`, `bool` = `u8`) coexist.
- `src/libc_md.c`: the libc subset the core uses (`memcmp`, `snprintf`, ...).
- The core is built with `-DVIEW_STATIC_CACHE=2` (window-sized static cache, ~2 KB instead of 42 KB).

## Real hardware (Mega EverDrive PRO)

`tools/everdrive.py` loads a ROM over USB (Krikzz's `edlink.exe`, needs Mono; `EDLINK` points to the exe) and
photographs the TV with a camera (ffmpeg/AVFoundation, `ED_CAM` = device index):

    tools/everdrive.py run out/md/out/rom.bin
    MD_OUT=out/md-demo EXTRA_FLAGS=-DLOC_MD_DEMO md/build.sh && tools/everdrive.py demo 60 4

`-DLOC_MD_DEMO` replaces the pad with a script (`DEMO[]` in `src/game_md.c`: title, menu, scenario 1, end turn,
scroll, action menu); `-DLOC_MD_DEMO=2` continues the saved game from the menu. First run (2026-10-09):
title, menu, play, turn end, scrolling and the SRAM save/load all work on the real console.
