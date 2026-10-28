/* m1.h -- a TRS-80 Model I: Level II BASIC, up to 48 KB, and the Expansion
 * Interface with its floppy drives. Portable C: no SDK headers, no
 * allocation. The Z80 is superzazu/z80 (MIT), instruction-stepped. */
#ifndef M1_H
#define M1_H
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "fdc.h"

#define M1_ROM_SIZE   0x3000		/* Level II: 12 KB at 0000 */
#define M1_KBD_BASE   0x3800		/* keyboard matrix, 3800-3BFF */
#define M1_VRAM_BASE  0x3C00		/* video, 3C00-3FFF: 64 x 16 */
#define M1_VRAM_SIZE  0x0400
#define M1_RAM_BASE   0x4000		/* RAM, 4000-FFFF */
#define M1_COLS 64
#define M1_ROWS 16
#define M1_CLOCK_HZ 1774080u		/* 10.6445 MHz / 6 */

typedef struct m1 m1_t;

/* rom: M1_ROM_SIZE bytes, kept by the caller; io: the disk images */
void     m1_init(m1_t *m, const uint8_t *rom, const disk_io_t *io);
/* power on: RAM cleared, the CPU and every chip reset. The Expansion
   Interface is there if drive 0 has a disk (as without one Level II waits
   for it); ram_kb is 16, 32 or 48. */
void     m1_power_on(m1_t *m, int ram_kb);
uint32_t m1_run(m1_t *m, uint32_t cycles);	/* whole instructions until at least cycles have run; returns how many */
const uint8_t *m1_vram(const m1_t *m);
uint64_t m1_instructions(const m1_t *m);
uint64_t m1_cycles(const m1_t *m);
void     m1_set_matrix(m1_t *m, const uint8_t rows[8]);	/* the whole keyboard at once */
fdc_t   *m1_fdc(m1_t *m);			/* the drives: fdc_insert() and so on */
bool     m1_has_ei(const m1_t *m);
uint8_t  m1_peek(const m1_t *m, uint16_t a);	/* RAM and video, without side effects */
void     m1_poke(m1_t *m, uint16_t a, uint8_t v);
/* Sound: the cassette output (port FF, bits 0-1), which games drive as a
   speaker. Each sample is the level averaged over the guest cycles it covers,
   with a DC blocker after it. Samples come every num/den guest cycles (a
   rational, so nothing drifts); m1_audio(m, 0, 0) turns it off. */
#define M1_AUDIO_RING 4096
void     m1_audio(m1_t *m, uint64_t num, uint64_t den);
size_t   m1_audio_drain(m1_t *m, int16_t *dst, size_t max);	/* samples ready, oldest first */
uint32_t m1_audio_dropped(const m1_t *m);	/* samples lost to a full ring */
size_t   m1_size(void);			/* sizeof the machine, for a static buffer */
#define M1_MACHINE_MAX (96 * 1024)	/* what a program should set aside (a host test checks it) */

#endif
