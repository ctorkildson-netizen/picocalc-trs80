/* m1.c -- see m1.h */
#include <string.h>
#include "m1.h"

#if defined(M1_CPU_CHIPS)
#define CHIPS_IMPL
#include "chips_z80.h"
#elif defined(M1_CPU_SZ)
#include "sz_z80.h"
#else
#error "choose a Z80: M1_CPU_SZ or M1_CPU_CHIPS"
#endif

struct m1 {
#if defined(M1_CPU_CHIPS)
	z80_t cpu;
	uint64_t pins;
#else
	z80 cpu;
#endif
	uint64_t instructions;
	uint8_t kbd[8];			/* row bits: a set bit is a key down */
	uint8_t mem[0x10000];		/* the whole address space: ROM, I/O, video, RAM */
};

size_t m1_size(void) { return sizeof(m1_t); }

/* The keyboard: address bits 0-7 each select a row; the byte read is the OR of
   the selected rows. (Model I keyboard, 3800-3BFF; A8-A9 ignored.) */
static uint8_t kbd_read(const m1_t *m, uint16_t a)
{
	uint8_t v = 0, sel = (uint8_t)a;
	for (int r = 0; r < 8; r++) if (sel & (1u << r)) v |= m->kbd[r];
	return v;
}

static inline uint8_t mem_read(m1_t *m, uint16_t a)
{
	if (a >= M1_KBD_BASE && a < M1_VRAM_BASE) return kbd_read(m, a);
	return m->mem[a];
}

static inline void mem_write(m1_t *m, uint16_t a, uint8_t v)
{
	if (a < M1_KBD_BASE) return;			/* ROM, and 3000-37FF (unused here) */
	if (a < M1_VRAM_BASE) return;			/* keyboard */
	if (a < M1_RAM_BASE) {
		/* a stock Model I keeps 7 bits: bit 6 isn't there, and reads back
		   as NOT (bit 5 OR bit 7), so text comes back as upper case */
		v &= 0xBF;
		if (!(v & 0xA0)) v |= 0x40;
	}
	m->mem[a] = v;
}

#if defined(M1_CPU_SZ)
static uint8_t sz_rd(void *u, uint16_t a) { return mem_read((m1_t *)u, a); }
static void sz_wr(void *u, uint16_t a, uint8_t v) { mem_write((m1_t *)u, a, v); }
static uint8_t sz_in(z80 *z, uint8_t p) { (void)z; (void)p; return 0xFF; }
static void sz_out(z80 *z, uint8_t p, uint8_t v) { (void)z; (void)p; (void)v; }
#endif

void m1_init(m1_t *m, const uint8_t *rom)
{
	memset(m, 0, sizeof *m);
	memcpy(m->mem, rom, M1_ROM_SIZE);
	memset(m->mem + 0x3000, 0xFF, 0x0800);		/* nothing there: reads high */
#if defined(M1_CPU_CHIPS)
	m->pins = z80_init(&m->cpu);
#else
	z80_init(&m->cpu);
	m->cpu.read_byte = sz_rd;
	m->cpu.write_byte = sz_wr;
	m->cpu.port_in = sz_in;
	m->cpu.port_out = sz_out;
	m->cpu.userdata = m;
#endif
}

uint32_t m1_run(m1_t *m, uint32_t cycles)
{
#if defined(M1_CPU_CHIPS)
	uint64_t pins = m->pins;
	uint32_t n = 0;
	while (n < cycles) {
		pins = z80_tick(&m->cpu, pins);
		n++;
		if (pins & Z80_MREQ) {
			uint16_t a = Z80_GET_ADDR(pins);
			if (pins & Z80_RD) { Z80_SET_DATA(pins, mem_read(m, a)); }
			else if (pins & Z80_WR) { mem_write(m, a, Z80_GET_DATA(pins)); }
		} else if (pins & Z80_IORQ) {
			if (pins & Z80_RD) { Z80_SET_DATA(pins, 0xFF); }
		}
		if (z80_opdone(&m->cpu)) m->instructions++;
	}
	m->pins = pins;
	return n;
#else
	unsigned long start = m->cpu.cyc;
	while (m->cpu.cyc - start < cycles) { z80_step(&m->cpu); m->instructions++; }
	return (uint32_t)(m->cpu.cyc - start);
#endif
}

const uint8_t *m1_vram(const m1_t *m) { return m->mem + M1_VRAM_BASE; }
uint64_t m1_instructions(const m1_t *m) { return m->instructions; }

void m1_set_matrix(m1_t *m, const uint8_t rows[8]) { memcpy(m->kbd, rows, 8); }

void m1_key(m1_t *m, int row, int col, bool down)
{
	if (row < 0 || row > 7 || col < 0 || col > 7) return;
	if (down) m->kbd[row] |= (uint8_t)(1u << col);
	else m->kbd[row] &= (uint8_t)~(1u << col);
}
