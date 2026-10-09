#!/usr/bin/env python3
# /// script
# requires-python = ">=3.11"
# dependencies = []
# ///
"""
Sound for the Mega Drive frontend (md/): build/sfx/sfx.bin and build/music/*.bin.

    uv run tools/md_sound.py            # after tools/gen_sfx.py and tools/gen_music.py

  md/res/sfx/sfx_NN.wav   the 16 effect samples as 8-bit mono WAV (16 kHz); rescomp turns them
                          into XGM2 PCM (md/res/resources.res, `WAV ... XGM2 13300`)
  md/res/music_<name>.bin the songs as they are (title, win, lose); md/src/music_md.c plays them on the PSG
"""
import struct
import wave
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
RES = ROOT / "md" / "res"


def main():
    (RES / "sfx").mkdir(parents=True, exist_ok=True)
    d = (ROOT / "build" / "sfx" / "sfx.bin").read_bytes()
    assert d[:4] == b"LOCX" and d[4] == 1
    count, pos = d[5], 6
    total = 0
    for i in range(count):
        _flags, _base, length = struct.unpack_from("<BHH", d, pos)
        pos += 5
        pcm = bytes((b + 128) & 255 for b in d[pos:pos + length])      # int8 -> unsigned 8 bit
        pos += length
        with wave.open(str(RES / "sfx" / f"sfx_{i:02d}.wav"), "wb") as w:
            w.setnchannels(1)
            w.setsampwidth(1)
            w.setframerate(16000)
            w.writeframes(pcm)
        total += length
    for name in ("title", "win", "lose"):
        (RES / f"music_{name}.bin").write_bytes((ROOT / "build" / "music" / f"{name}.bin").read_bytes())
    print(f"[md_sound] {count} samples, {total} bytes; 3 songs")


if __name__ == "__main__":
    main()
