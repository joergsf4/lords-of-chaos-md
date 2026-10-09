#!/bin/bash
# Builds the Mega Drive ROM (SGDK in docker) -> out/md/rom.bin.
# The SGDK makefile compiles everything below src/, so the project is
# staged in out/md/ from md/ (frontend) + src/core (platform-free core)
# + tests/ (selftest); src/agon is left out.
# Usage: md/build.sh [clean]      SELFTEST=1 md/build.sh (core self-test ROM)
#        EXTRA_FLAGS="-DFOO" md/build.sh
set -euo pipefail
cd "$(dirname "$0")/.."
STAGE=${MD_OUT:-out/md}              # MD_OUT=out/md-test md/build.sh: a second build next to the normal one
if [ "${1:-}" = "clean" ]; then rm -rf "$STAGE"; exit 0; fi
[ -f md/res/tile_pixels.bin ] || uv run --quiet tools/png2md.py
[ -f md/res/title_tiles.bin ] || uv run --quiet tools/md_assets.py
[ -f md/res/help_md.bin ] || uv run --quiet tools/md_help.py
[ -f md/res/music_title.bin ] || uv run --quiet tools/md_sound.py
for g in tiles data maps scenarios; do
    [ -f src/core/gen/$g.h ] || { echo "src/core/gen missing - run the generators (uv run tools/build.py --all)" >&2; exit 1; }
done
mkdir -p "$STAGE/src" "$STAGE/res" "$STAGE/inc"
rsync -a --delete md/src/ "$STAGE/src/"
rsync -a --delete src/core/ "$STAGE/src/core/"
rsync -a --exclude resources.h md/res/ "$STAGE/res/"
rsync -a --delete md/inc/ "$STAGE/inc/"
rm -f "$STAGE/src/selftest.c" "$STAGE/src/selftest.h"
SELF=""
if [ "${SELFTEST:-}" = "1" ]; then
    cp tests/selftest.c tests/selftest.h "$STAGE/src/"
    SELF="-DLOC_MD_SELFTEST -DLOC_SELFTEST_NO_SAVE"
fi
# make does not see changed flags: build from scratch when they differ from the last build here
FLAGS="$SELF ${EXTRA_FLAGS:-}"
if [ -d "$STAGE/out" ] && [ "$(cat "$STAGE/out/.flags" 2>/dev/null)" != "$FLAGS" ]; then rm -rf "$STAGE/out"; fi
mkdir -p "$STAGE/out"
echo "$FLAGS" > "$STAGE/out/.flags"
rm -f "$STAGE"/out/src/main.o "$STAGE"/out/src/main.d "$STAGE"/out/src/selftest.*
docker run --rm --platform linux/amd64 -e EXTRA_FLAGS="-DVIEW_STATIC_CACHE=2 $SELF -Isrc/core -Isrc/core/gen ${EXTRA_FLAGS:-}" \
    -v "$PWD/$STAGE":/m68k -t registry.gitlab.com/doragasu/docker-sgdk:v2.11
echo "ROM: $STAGE/out/rom.bin"
