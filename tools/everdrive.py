#!/usr/bin/env python3
"""
Real hardware: load a ROM onto a Mega EverDrive PRO over USB and photograph the TV with a camera.

    tools/everdrive.py run out/md/out/rom.bin            # reset, load, start
    tools/everdrive.py shots out/hw 60 5                 # 12 camera pictures in 60 s, one every 5 s
    tools/everdrive.py demo 90 5                         # run out/md-demo ROM (MD_OUT=out/md-demo, -DLOC_MD_DEMO) + pictures

Needs Krikzz's edlink (https://github.com/krikzz/edlink, dist/edlink.exe) and Mono; EDLINK points to the
exe (default: ~/.cache/edlink/edlink.exe). The camera is an AVFoundation device (ffmpeg), ED_CAM its index
(default 0, see `ffmpeg -f avfoundation -list_devices true -i ""`). Pictures: <dir>/hwNN.jpg.
"""
import os
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
EDLINK = os.environ.get("EDLINK", str(Path.home() / ".cache" / "edlink" / "edlink.exe"))
CAM = os.environ.get("ED_CAM", "0")


def edlink(*args, check=True):
    r = subprocess.run(["mono", EDLINK, *args], capture_output=True, text=True, timeout=120)
    out = r.stdout + r.stderr
    if check and "ERROR" in out:
        raise SystemExit(out)
    return out


def run(rom):
    edlink("reset", check=False)
    time.sleep(3)                    # the menu is up; a running Mega CD BIOS makes `run` time out
    out = edlink("run", "--file", str(rom))
    time.sleep(2)
    return out


def shot(path):
    subprocess.run(["ffmpeg", "-hide_banner", "-loglevel", "error", "-y", "-f", "avfoundation", "-framerate", "30",
                    "-i", CAM, "-frames:v", "12", "-update", "1", str(path)], check=True, timeout=60)


def shots(out_dir, seconds, every):
    Path(out_dir).mkdir(parents=True, exist_ok=True)
    t0 = time.time()
    n = 0
    while time.time() - t0 < seconds:
        shot(Path(out_dir) / f"hw{n:02d}.jpg")
        print(f"{time.time() - t0:5.1f}s hw{n:02d}.jpg")
        n += 1
        time.sleep(max(0, t0 + n * every - time.time()))


def main():
    cmd = sys.argv[1] if len(sys.argv) > 1 else ""
    if cmd == "run":
        print(run(sys.argv[2]))
    elif cmd == "shots":
        shots(sys.argv[2], float(sys.argv[3]), float(sys.argv[4]))
    elif cmd == "demo":
        run(ROOT / "out" / "md-demo" / "out" / "rom.bin")
        shots(ROOT / "out" / "hw", float(sys.argv[2]), float(sys.argv[3]))
    else:
        print(__doc__)
        return 1


if __name__ == "__main__":
    sys.exit(main())
