#!/usr/bin/env python3
"""Runs out/rom.bin in BlastEm's debugger and takes screenshots at given frames.

    python3 tools/emushot.py 60 900 1800      # screenshots after 60, 900, 1800 frames
    python3 tools/emushot.py 120 start 300    # screenshot, press Start, run to frame 300, screenshot
    python3 tools/emushot.py rec 600 rec      # record video + audio (out/emushot/rec_*.apng/.wav)
    python3 tools/emushot.py --fast 9000      # unthrottled (~16x), for screenshots only
    python3 tools/emushot.py --rom out/dev_3.bin 300    # another ROM (tools/devrom.sh)

Screenshots go to out/emushot/frame_<n>.png. A BlastEm window opens while it runs.
Adapted from the Wanderburg project: the debugger reads commands from a terminal
(hence the pty), and the screenshot binding writes blastem_*.png into $HOME.

BlastEm runs with its own HOME (out/emuhome) whose config sets
machine_freeze_action to debug: an access that would lock up real hardware then
stops in the debugger (printed here) instead of opening a dialog, and the
user's own BlastEm config stays untouched.

Frames are counted with the game's own clock: SGDK's `vtimer` (+1 per V-Int).
A conditional breakpoint on the V-Int handler (`_VINT`) stops when vtimer
reaches the target. BlastEm's own `frames n` counts rendered frames, and
unthrottled it skips some: `frames 1248` once ran ~9750 frames. The addresses
come from the ROM's symbol file (out/symbol.txt for out/rom.bin, out/dev_N.sym
for a dev ROM); without one, `frames` is used. While interrupts are off (large
loads), vtimer stands still, so "n frames" are n V-Ints.
"""
import glob
import os
import pty
import re
import select
import sys
import struct
import time
import zlib

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
MD_OUT = os.path.join(ROOT, os.environ.get('MD_OUT', 'out/md'))     # MD_OUT=out/md-test: another build
ROM = os.path.join(MD_OUT, 'out', 'rom.bin')
OUT = os.path.join(MD_OUT, 'emushot')
ANSI = re.compile(r'\x1b\[[0-9;]*[A-Za-z]')
EMU_HOME = os.path.join(MD_OUT, 'emuhome')
DEFAULT_CFG = '/opt/homebrew/Cellar/blastem/1.0.0/libexec/default.cfg'


FAST_PERCENT = 10000            # speed table entry 4 in the private config (the emulator then runs unthrottled)


def emu_home(home=EMU_HOME):
    """Private HOME for BlastEm: machine_freeze_action debug (no dialog on a
    lock-up) and speed 4 = FAST_PERCENT. Parallel runs use separate homes,
    because screenshots land in HOME."""
    cfg_dir = os.path.join(home, '.config', 'blastem')
    os.makedirs(cfg_dir, exist_ok=True)
    with open(DEFAULT_CFG) as f:
        cfg = f.read()
    for section in ('ui {', 'system {'):
        cfg = cfg.replace(section, section + '\n\tmachine_freeze_action debug', 1)
    cfg = re.sub(r'(speeds \{[^}]*?\n\s*4 )\d+', r'\g<1>%d' % FAST_PERCENT, cfg, flags=re.S)
    # entry 1 = 100 %: "ui.set_speed.0" does nothing (checked), so set_fast(False) uses 1
    cfg = re.sub(r'(speeds \{[^}]*?\n\s*1 )\d+', r'\g<1>100', cfg, flags=re.S)
    with open(os.path.join(cfg_dir, 'blastem.cfg'), 'w') as f:
        f.write(cfg)
    return home


def symbols_for(rom):
    rom = os.path.abspath(rom)
    if rom == ROM:
        return os.path.join(MD_OUT, 'out', 'symbol.txt')
    sym = os.path.splitext(rom)[0] + '.sym'
    return sym if os.path.exists(sym) else None


def symbol(path, name):
    with open(path) as f:
        text = f.read()
    m = re.search(r'^([0-9a-f]{8}) [a-zA-Z] ' + re.escape(name) + r'$', text, re.M)
    return int(m.group(1), 16) & 0xFFFFFF if m else None


class Blastem:
    def __init__(self, rom=ROM, fast=False, home=EMU_HOME):
        """fast: unthrottled (~16x) for tests and screenshots; recordings with
        sound need real time."""
        self.home = emu_home(home)
        self.frame = 0                  # emulated frames so far (frames() counts them)
        self.stalls = 0                 # "frames n" that did not stop in time (see frames())
        self.pid, self.fd = pty.fork()
        if self.pid == 0:
            os.environ['HOME'] = self.home
            os.execvp('blastem', ['blastem', rom, '-d'])
        self.read(10.0, prompt=True)
        self.vtimer = None
        sym = symbols_for(rom)
        if sym and symbol(sym, '_VINT') is not None and symbol(sym, 'vtimer') is not None:
            self.vtimer = symbol(sym, 'vtimer')
            for _ in range(5):                  # BlastEm sometimes needs a moment after the start
                out = self.cmd(f'b 0x{symbol(sym, "_VINT"):X}', 3.0)
                m = re.search(r'Breakpoint (\d+) set', out)
                if m:
                    break
                out += self.read(2.0, prompt=True)
                m = re.search(r'Breakpoint (\d+) set', out)
                if m:
                    break
            if not m:
                raise SystemExit(f'BlastEm did not set the V-Int breakpoint: {out[-300:]!r}')
            self.bp = m.group(1)
        self.fast = False
        if fast:
            self.set_fast(True)

    def set_fast(self, fast):
        """speed table entry 4 (unthrottled) or 1 (set to 100 %); takes one frame"""
        key = 'ui.set_speed.%d' % (4 if fast else 1)
        self.cmd(f'binddown "{key}"')
        self.frames(1)
        self.cmd(f'bindup "{key}"')
        self.fast = fast

    def read(self, wait, prompt=False):
        out, end = '', time.time() + wait
        while time.time() < end:
            ready, _, _ = select.select([self.fd], [], [], 0.05)
            if not ready:
                continue
            try:
                data = os.read(self.fd, 65536).decode('utf-8', 'replace')
            except OSError:
                break
            if not data:
                break
            out += data
            if prompt and out.rstrip().endswith('>'):
                break
        return ANSI.sub('', out)

    def cmd(self, text, timeout=5.0):
        os.write(self.fd, (text + '\n').encode())
        return self.read(timeout, prompt=True)

    def frames(self, n):
        if n <= 0:
            return                      # "frames 0" would run the emulator without limit
        self.frame += n
        if self.vtimer is None:
            out = self.cmd(f'frames {n}', 3.0 + n / (300 if self.fast else 50))
            if not out.rstrip().endswith('>'):
                self.stalls += 1        # unthrottled, BlastEm's frame count skips frames
        else:
            target = self.peek(self.vtimer) + n
            self.cmd(f'condition {self.bp} [0x{self.vtimer:X}].l >= {target}', 2.0)
            out = self.cmd('c', 3.0 + n / (300 if self.fast else 50))
        end = time.time() + 120
        while not out.rstrip().endswith('>'):
            if time.time() > end:
                print('EMULATOR DOES NOT STOP (vtimer stands still?)')
                raise SystemExit(1)
            out += self.read(5.0, prompt=True)
        if re.search(r'nmapped|lock up|xception', out):
            print('EMULATOR STOPPED:', out[-1500:])
            raise SystemExit(1)
        if self.vtimer is not None and self.peek(self.vtimer) != target:
            print(f'EMULATOR STOPPED at vtimer {self.peek(self.vtimer)} instead of {target}:', out[-800:])
            raise SystemExit(1)

    def run_until_value(self, addr, value, size='w', timeout=300):
        """Runs until the game variable at addr reaches value (checked at each V-Int),
        e.g. the scene counter gsScene; afterwards self.frame is the game's vtimer."""
        self.cmd(f'condition {self.bp} [0x{addr:X}].{size} >= {value}', 2.0)
        out = self.cmd('c', 5.0)
        end = time.time() + timeout
        while not out.rstrip().endswith('>'):
            if time.time() > end:
                raise SystemExit(f'EMULATOR: [0x{addr:X}] did not reach {value}')
            out += self.read(5.0, prompt=True)
        if re.search(r'nmapped|lock up|xception', out):
            print('EMULATOR STOPPED:', out[-1500:])
            raise SystemExit(1)
        self.frame = self.peek(self.vtimer)

    def peek(self, addr, size='l'):
        """read memory through the debugger (addr e.g. 0xFF0086)"""
        out = self.cmd(f'p/d [0x{addr:X}].{size}', 2.0)
        return int(re.search(r'\]\.[wbl]: (-?\d+)', out).group(1))

    def shot(self, tag, out_dir=None):
        """BlastEm names screenshots by the second; unthrottled, several fall into one
        second and overwrite each other. So the folder is cleared first and the file
        that appears is taken."""
        home = self.home
        for old in glob.glob(home + '/blastem_*.png'):
            os.remove(old)
        dst = os.path.join(out_dir or OUT, tag + '.png')
        if os.path.exists(dst):
            os.remove(dst)
        self.cmd('binddown "ui.screenshot"')
        self.frames(1)
        self.cmd('bindup "ui.screenshot"')
        self.frames(1)
        new, end = [], time.time() + 3.0
        while time.time() < end:                    # the file appears a moment later
            new = glob.glob(home + '/blastem_*.png')
            if new and os.path.getsize(new[0]) > 0:
                time.sleep(0.05)
                break
            time.sleep(0.02)
        if not new:
            print(f'{tag}: no screenshot')
            return
        os.makedirs(out_dir or OUT, exist_ok=True)
        os.replace(new[0], dst)
        print(f'{tag}: {dst}')

    def press(self, button, n=8):
        key = f'gamepads.1.{button}'
        self.cmd(f'binddown "{key}"')
        self.frames(n)
        self.cmd(f'bindup "{key}"')

    def record_toggle(self):
        self.cmd('binddown "ui.record_video"')
        self.frames(2)
        self.cmd('bindup "ui.record_video"')
        self.frames(1)

    def close(self):
        try:
            os.write(self.fd, b'quit\n')
            time.sleep(0.4)
            os.kill(self.pid, 9)
        except OSError:
            pass


def repair_apng(path):
    """BlastEm updates the frame count in the acTL chunk without fixing its CRC
    (taken from the Wanderburg project); returns a readable copy."""
    data = open(path, 'rb').read()
    out, pos = bytearray(data[:8]), 8
    while pos + 8 <= len(data):
        length, typ = struct.unpack('>I4s', data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + length]
        out += struct.pack('>I', length) + typ + body + struct.pack('>I', zlib.crc32(typ + body) & 0xFFFFFFFF)
        pos += 12 + length
        if typ == b'IEND':
            break
    fixed = path + '.fixed.png'
    open(fixed, 'wb').write(out)
    return fixed


def main():
    """Arguments in order: a number runs to that frame and takes a screenshot,
    a button name (start, a, b, c, up, ...) presses it for 4 frames."""
    args = sys.argv[1:]
    rom = ROM
    fast = '--fast' in args                            # unthrottled; not for recordings with sound
    args = [a for a in args if a != '--fast']
    if len(args) >= 2 and args[0] == '--rom':          # e.g. out/dev_battle_eb.bin (tools/devrom.sh)
        rom, args = os.path.abspath(args[1]), args[2:]
    b = Blastem(rom, fast)
    recording, started = False, time.time()
    try:
        now = 0
        for arg in args or ['120']:
            if arg.isdigit():
                t = int(arg)
                if t > now:
                    b.frames(t - now)
                now = max(now, t) + 2          # the screenshot itself runs two frames
                b.shot(f'frame_{t}')
            elif arg == 'rec':
                b.record_toggle()
                now += 3
                if recording:
                    time.sleep(2.0)
                    home = EMU_HOME
                    os.makedirs(OUT, exist_ok=True)
                    for f in glob.glob(home + '/blastem_*.apng') + glob.glob(home + '/blastem_*.wav'):
                        if os.path.getmtime(f) >= started:
                            dst = os.path.join(OUT, 'rec' + os.path.splitext(f)[1])
                            os.replace(f, dst)
                            print('recording:', dst)
                recording, started = not recording, time.time()
            else:
                b.press(arg)
                now += 8
    finally:
        b.close()


if __name__ == '__main__':
    main()
