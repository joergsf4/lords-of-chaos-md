#!/bin/bash
# Runs the ROM in BlastEm.
cd "$(dirname "$0")/.."
exec blastem out/md/out/rom.bin
