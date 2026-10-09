#!/bin/bash
# Downloads the newest Lords of Chaos Mega Drive ROM from the GitHub release
# of joergsf4/lords-of-chaos-md into the ROM folder of the handheld (EmuELEC).
#
# Run it on the device (SSH or PORTS menu). Needs WiFi and curl or wget.
#   scp tools/update-rom-on-device.sh root@<ip>:/storage/
#   ssh root@<ip> bash /storage/update-rom-on-device.sh

REPO="joergsf4/lords-of-chaos-md"
DEST="${ROM_DIR:-/storage/roms/megadrive}"
NAME="lords-of-chaos.md"
TMP="$(mktemp -d /tmp/loc-update.XXXXXX)" || exit 1
trap 'rm -rf "$TMP"' EXIT

fetch() {   # fetch URL FILE
    if command -v curl >/dev/null 2>&1; then
        curl -fsSL "$1" -o "$2"
    else
        wget -q -O "$2" "$1"
    fi
}

if [ ! -d "$DEST" ]; then
    echo "ROM folder $DEST not found. Set ROM_DIR=/path and run again." >&2
    exit 1
fi

echo "Looking for the latest release of $REPO ..."
fetch "https://api.github.com/repos/$REPO/releases/latest" "$TMP/release.json" || {
    echo "Could not reach GitHub (WiFi on?)." >&2
    exit 1
}

TAG="$(grep -o '"tag_name": *"[^"]*"' "$TMP/release.json" | head -1 | sed 's/.*: *"\(.*\)"/\1/')"
ROM_URL="$(grep -o '"browser_download_url": *"[^"]*\.bin"' "$TMP/release.json" | head -1 | sed 's/.*: *"\(.*\)"/\1/')"
SUM_URL="$(grep -o '"browser_download_url": *"[^"]*SHA256SUMS[^"]*"' "$TMP/release.json" | head -1 | sed 's/.*: *"\(.*\)"/\1/')"

if [ -z "$ROM_URL" ]; then
    echo "No .bin file in the latest release." >&2
    exit 1
fi
echo "Release $TAG: ${ROM_URL##*/}"

fetch "$ROM_URL" "$TMP/rom.bin" || { echo "Download failed." >&2; exit 1; }

# Check the SHA-256 against the release's SHA256SUMS.txt when there is one.
if [ -n "$SUM_URL" ] && fetch "$SUM_URL" "$TMP/sums.txt"; then
    WANT="$(grep "${ROM_URL##*/}" "$TMP/sums.txt" | awk '{print $1}' | head -1)"
    HAVE="$(sha256sum "$TMP/rom.bin" | awk '{print $1}')"
    if [ -n "$WANT" ] && [ "$WANT" != "$HAVE" ]; then
        echo "SHA-256 mismatch, ROM not installed." >&2
        echo "  expected $WANT" >&2
        echo "  got      $HAVE" >&2
        exit 1
    fi
    echo "SHA-256 ok."
else
    echo "No checksum file found, skipping the check."
fi

mv -f "$TMP/rom.bin" "$DEST/$NAME" && sync
echo "Installed $DEST/$NAME ($TAG). Reload the game list."
exit 0
