#!/bin/sh
# The emulator's core on a PC: ./host/run.sh [level2.rom]
# Without a ROM only the key-layout test runs; with one (your own Level II
# ROM, 12 KB: see the README), typing, sound and the boot run too.
set -e
cd "$(dirname "$0")/.."
B=host/build
mkdir -p $B
CORE="core/m1.c core/sz_z80.c core/keys.c core/sha1.c core/fdc.c core/dosclock.c"
CC="${CC:-cc} -O2 -Wall -Wextra -Icore -DM1_CPU_SZ"
$CC host/layouttest.c core/keys.c -o $B/layouttest
$CC host/typetest.c $CORE -o $B/typetest
$CC -DM1_EDGES host/soundtest.c $CORE -o $B/soundtest
$CC host/boot.c $CORE -o $B/boot
$CC host/disks.c $CORE -o $B/disks
$B/layouttest
if [ -n "$1" ]; then
	$B/typetest "$1"
	$B/soundtest "$1" $B/sound.wav
	$B/boot "$1" 5 | tail -3
else
	echo "(no ROM given: typing, sound and boot tests skipped)"
fi
