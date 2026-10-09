#!/usr/bin/env python3
"""Runs the Mega Drive selftest ROM (md/build.sh) in BlastEm and reports the verdict.

    python3 tools/mdtest.py            # exit code 0 = all core checks passed on the m68k

Reads g_selftest_done / g_selftest_fails from the debugger and takes a screenshot
(out/md/emushot/selftest.png). See tools/emushot.py for the emulator driver.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import emushot as e  # noqa: E402


def main():
    b = e.Blastem(fast=True)
    try:
        sym = e.symbols_for(e.ROM)
        done, fails = e.symbol(sym, 'g_selftest_done'), e.symbol(sym, 'g_selftest_fails')
        b.run_until_value(done, 1, 'w', timeout=int(os.environ.get('MDTEST_TIMEOUT', '600')))
        n = b.peek(fails, 'w')
        b.frames(2)
        b.shot('selftest')
        print(f'selftest fails: {n}  (vtimer {b.frame})')
        sys.exit(0 if n == 0 else 1)
    finally:
        b.close()


if __name__ == '__main__':
    main()
