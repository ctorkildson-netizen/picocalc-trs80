/* m1.h -- a TRS-80 Model I (Level II BASIC, 48 KB), first cut: CPU, memory,
 * keyboard matrix and video RAM. Portable C: no SDK headers, no allocation.
 * Built with one of two Z80 cores, to compare them: M1_CPU_SZ (superzazu/z80,
 * instruction-stepped) or M1_CPU_CHIPS (floooh/chips z80.h, cycle-stepped). */
#ifndef M1_H
#define M1_H
#include <stdint.h>
#include <stdbool.h>

#define M1_ROM_SIZE   0x3000		/* Level II: 12 KB at 0000 */
#define M1_KBD_BASE   0x3800		/* keyboard matrix, 3800-3BFF */
#define M1_VRAM_BASE  0x3C00		/* video, 3C00-3FFF: 64 x 16 */
#define M1_VRAM_SIZE  0x0400
#define M1_RAM_BASE   0x4000		/* RAM, 4000-FFFF */
#define M1_COLS 64
#define M1_ROWS 16
#define M1_CLOCK_HZ 1774080u		/* 10.6445 MHz / 6 */

typedef struct m1 m1_t;

void     m1_init(m1_t *m, const uint8_t *rom);	/* rom: M1_ROM_SIZE bytes */
uint32_t m1_run(m1_t *m, uint32_t cycles);	/* whole instructions until at least cycles have run; returns how many */
const uint8_t *m1_vram(const m1_t *m);
uint64_t m1_instructions(const m1_t *m);
void     m1_key(m1_t *m, int row, int col, bool down);
size_t   m1_size(void);			/* sizeof the machine, for a static buffer */

#endif
