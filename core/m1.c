/* m1.c -- see m1.h */
#include <string.h>
#include "m1.h"
#include "sz_z80.h"

struct m1 {
	z80 cpu;
	const uint8_t *rom;
	uint64_t instructions, cycles;
	uint32_t ram_top;		/* first address with no RAM */
	bool ei;			/* the Expansion Interface is fitted */
	bool int_line;			/* the Z80's INT, as the EI drives it */
	bool fdc_touched;		/* the EI was read or written: look at INT again */
	uint64_t next_event;
	uint8_t kbd[8];			/* row bits: a set bit is a key down */
	fdc_t fdc;
	uint8_t mem[0x10000];		/* the whole address space: ROM, video, RAM */
};

size_t m1_size(void) { return sizeof(m1_t); }

/* The keyboard: address bits 0-7 each select a row; the byte read is the OR of
   the selected rows. (3800-3BFF; A8-A9 ignored.) */
static uint8_t kbd_read(const m1_t *m, uint16_t a)
{
	uint8_t v = 0, sel = (uint8_t)a;
	for (int r = 0; r < 8; r++) if (sel & (1u << r)) v |= m->kbd[r];
	return v;
}

/* 3000-3BFF: nothing, the Expansion Interface, the keyboard */
static uint8_t io_read(m1_t *m, uint16_t a)
{
	if (a >= M1_KBD_BASE) return kbd_read(m, a);
	if (m->ei && a >= 0x37E0 && a <= 0x37EF) {
		m->fdc_touched = true;
		return fdc_read(&m->fdc, a, m->cycles);
	}
	return 0xFF;
}

static inline uint8_t mem_read(m1_t *m, uint16_t a)
{
	if ((a & 0xF000) == 0x3000 && a < M1_VRAM_BASE) return io_read(m, a);
	if (a >= m->ram_top) return 0xFF;
	return m->mem[a];
}

static inline void mem_write(m1_t *m, uint16_t a, uint8_t v)
{
	if (a < 0x3000 || a >= m->ram_top) return;	/* ROM; no RAM there */
	if (a < M1_VRAM_BASE) {
		if (m->ei && a >= 0x37E0 && a <= 0x37EF) {
			m->fdc_touched = true;
			fdc_write(&m->fdc, a, v, m->cycles);
		}
		return;
	}
	if (a < M1_RAM_BASE) {
		/* a stock Model I keeps 7 bits: bit 6 isn't there, and reads back
		   as NOT (bit 5 OR bit 7), so text comes back as upper case */
		v &= 0xBF;
		if (!(v & 0xA0)) v |= 0x40;
	}
	m->mem[a] = v;
}

static uint8_t sz_rd(void *u, uint16_t a) { return mem_read((m1_t *)u, a); }
static void sz_wr(void *u, uint16_t a, uint8_t v) { mem_write((m1_t *)u, a, v); }
static uint8_t sz_in(z80 *z, uint8_t p) { (void)z; (void)p; return 0xFF; }
static void sz_out(z80 *z, uint8_t p, uint8_t v) { (void)z; (void)p; (void)v; }

void m1_init(m1_t *m, const uint8_t *rom, const disk_io_t *io)
{
	memset(m, 0, sizeof *m);
	m->rom = rom;
	fdc_init(&m->fdc, io);
	m1_power_on(m, 48);
}

void m1_power_on(m1_t *m, int ram_kb)
{
	memset(m->mem, 0, sizeof m->mem);
	memcpy(m->mem, m->rom, M1_ROM_SIZE);
	m->ram_top = ram_kb >= 48 ? 0x10000 : ram_kb >= 32 ? 0xC000 : 0x8000;
	m->ei = fdc_present(&m->fdc);
	fdc_reset(&m->fdc, m->cycles);
	m->int_line = false;
	m->next_event = fdc_next_event(&m->fdc);
	memset(m->kbd, 0, sizeof m->kbd);
	z80_init(&m->cpu);
	m->cpu.read_byte = sz_rd;
	m->cpu.write_byte = sz_wr;
	m->cpu.port_in = sz_in;
	m->cpu.port_out = sz_out;
	m->cpu.userdata = m;
}

uint32_t m1_run(m1_t *m, uint32_t cycles)
{
	uint32_t done = 0;
	while (done < cycles) {
		m->cpu.int_pending = m->int_line;	/* (level-sensitive: held until the latch is read) */
		unsigned long before = m->cpu.cyc;
		z80_step(&m->cpu);
		uint32_t n = (uint32_t)(m->cpu.cyc - before);
		done += n;
		m->cycles += n;
		m->instructions++;
		if (m->ei && (m->fdc_touched || m->cycles >= m->next_event)) {
			m->int_line = fdc_int(&m->fdc, m->cycles);
			m->next_event = fdc_next_event(&m->fdc);
			m->fdc_touched = false;
		}
	}
	return done;
}

const uint8_t *m1_vram(const m1_t *m) { return m->mem + M1_VRAM_BASE; }
uint64_t m1_instructions(const m1_t *m) { return m->instructions; }
uint64_t m1_cycles(const m1_t *m) { return m->cycles; }
void m1_set_matrix(m1_t *m, const uint8_t rows[8]) { memcpy(m->kbd, rows, 8); }
fdc_t *m1_fdc(m1_t *m) { return &m->fdc; }
bool m1_has_ei(const m1_t *m) { return m->ei; }
