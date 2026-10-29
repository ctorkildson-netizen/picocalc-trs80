# trs80

A TRS-80 Model I for the ClockworkPi **PicoCalc** with an RP2350 (Pico 2 W),
UF2 Loader and Blair Leduc's
[PicoCalc Shell](https://github.com/BlairLeduc/picocalc-shell). Level II
BASIC, up to 48 KB, and the Expansion Interface's two disk drives, with disk
images on the SD card, at the Model I's own speed.

The PicoCalc's 64-column screen is the Model I's 64 columns, so its 64 x 16
screen fits across without squeezing: each character is a 5 x 18 pixel cell,
in a clearer version of the Model I's letters, or (F8) in the Model I's own.

## Trying it

You need your own copy of the Model I's Level II ROM: it's Tandy's, so it
isn't included here.

1. Copy `trs80.uf2` (from the latest release) into `pico2-apps` on the SD card.
2. Make a folder `/trs80` on the card and put the ROM in it, either as one
   12 KB file called `level2.rom`, or as the three 4 KB parts of MAME's
   `trs80l2` set: `rom-a.z1`, `rom-b.z2` and `rom-c.z3`.
3. Run trs80 from the shell. At `MEMORY SIZE?`, press Enter, and you're in
   BASIC.

For disks, make a folder `/trs80/disks`, put disk images in it, press F3 and
choose one for drive 0, then F1 to start the machine again from it.

## Keys

| Key | |
|---|---|
| F1 | Power on: start again, memory cleared |
| F2 | Back to the shell |
| F3, F4 | Choose the disk in drive 0, drive 1 (Del in the list empties the drive) |
| F5 | Save the Model I's screen as text, in `/screenshots/trs80-NNN.txt` |
| F6 (Shift-F1) | Choose a game's keys (see below) |
| F7 | Sound: off, quiet, medium, loud |
| F8 | Settings, and what trs80 found (ROM, drives, clock) |

The typing keys type what they say: the PicoCalc sends the Model I the keys,
Shift and all, that make each character. The Model I's own keys: Enter is
ENTER, the arrows are its arrows, Home is CLEAR, Esc and Break are BREAK,
Backspace is its backspace (LEFT), Tab its tab (RIGHT), and Del is
SHIFT-LEFT, which deletes the line. Shift on its own counts too, as some
games use it as a button.

The F8 page has three settings, kept in `/trs80/settings.txt` with the disks
and the volume:

- **M** memory: 16, 32 or 48 KB (from the next F1)
- **F** letters: the PicoCalc's clearer ones, or the Model I's own, if
  `/trs80/mcm6670p.z29` (its character generator, from MAME's `trs80` set)
  is on the card
- **S** sound: as F7

F5 on the F8 page saves it as `/screenshots/trs80-settings.txt`, which is
handy for a bug report.

## Disks

- Images in **JV1** format (the usual `.dsk`, 35 to 80 tracks, single
  sided) and **DMK** format, which keeps each track as the disk had it.
  They're read and written in place, so keep a copy of any you care about.
  (Writing to DMK images has been tried less than writing to JV1.)
- The Expansion Interface is there when drive 0 has a disk at power-on (F1),
  as Level II waits for a disk when there's an Expansion Interface. With no
  disk in drive 0, it's a Model I without one.
- NEWDOS/80 boots, and games run from it. When the shell's clock is set,
  trs80 sets NEWDOS's clock and date from it as the DOS starts (shell v1.1
  or later).
- Files written to the card are dated from the shell's clock too.

## Sound

Games made sound through the Model I's cassette output, which was wired to
a speaker or the tape recorder, and so does trs80: through the PicoCalc's
speakers or headphones, in glorious 1-bit. F7 is the volume.

## Game keys

Some games want keys that are awkward on the PicoCalc. A layout is a small
text file in `/trs80/layouts` saying which PicoCalc key gives which Model I
key; F6 chooses one, and Del in the list goes back to the usual keys.
[layouts/README](layouts/README) has the format, and
[layouts/meteor.txt](layouts/meteor.txt) is an example.

## What it isn't

- No cassette tape: programs come on disk.
- Level II BASIC only: no Level I, and no 4 KB machine.
- No printer, no RS-232, and no lower-case modification: like a stock
  Model I, it shows upper case only.
- One Model I, not a Model III or 4.

## Building

```
git clone --recursive https://github.com/ctorkildson-netizen/picocalc-trs80
cd picocalc-trs80
cmake -B build -DPICO_SDK_PATH=/path/to/pico-sdk
cmake --build build
```

makes `build/trs80.uf2`. The emulator is in [core/](core/), portable C
with no Pico SDK in it; [port/](port/) is the PicoCalc's side: screen,
keyboard, SD card and sound.

`host/run.sh level2.rom` builds the core on a PC and runs its tests: typing
through the PicoCalc's keys, the sound against a model of the cassette
output, a timed boot, and the layout parser (which needs no ROM).

The submodules: Blair Leduc's
[picocalc-text-starter](https://github.com/BlairLeduc/picocalc-text-starter)
drivers (version 0.16, with FAT32 fixes, from the `diamond-dev` branch of
[a fork](https://github.com/ctorkildson-netizen/picocalc-text-starter)), his
[shell](https://github.com/BlairLeduc/picocalc-shell) for the clock, and
[picocalc-extras](https://github.com/ctorkildson-netizen/picocalc-extras)
for the file list.

## Licence

MIT, in [LICENSE](LICENSE). The Z80 is
[superzazu/z80](https://github.com/superzazu/z80) (MIT, Nicolas Allemand),
with one fix: [core/sz_z80.LICENSE](core/sz_z80.LICENSE) says what.
