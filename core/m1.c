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
	/* sound */
	uint64_t a_num, a_den;		/* guest cycles per sample, num/den; 0: off */
	uint64_t a_at, a_rem, a_prev;	/* the next sample's boundary (and the fraction), the last one */
	uint64_t a_last;		/* cycle of the last level change counted in a_acc */
	int64_t a_acc;			/* level x cycles since a_prev */
	int8_t a_level;			/* -1, 0, +1 */
	int32_t a_x1, a_y1;		/* the DC blocker */
	int16_t a_ring[M1_AUDIO_RING];
	uint32_t a_head, a_tail, a_dropped;
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
#define A_GAIN 16000			/* a level's height, out of 32767 (an edge swings twice that) */

/* every sample whose boundary has passed, up to the cycle now */
static void audio_advance(m1_t *m, uint64_t now)
{
	if (!m->a_num) return;
	while (m->a_at <= now) {
		m->a_acc += (int64_t)m->a_level * (int64_t)(m->a_at - m->a_last);
		m->a_last = m->a_at;
		uint64_t span = m->a_at - m->a_prev;
		int32_t x = span ? (int32_t)(m->a_acc * A_GAIN / (int64_t)span) : 0;
		/* DC blocker: y = x - x1 + 0.995 y1 (Q15), so a level left high or
		   low settles to 0, the silence value */
		int32_t y = x - m->a_x1 + (int32_t)(((int64_t)m->a_y1 * 32604) >> 15);
		m->a_x1 = x; m->a_y1 = y;
		if (y > 32767) y = 32767;
		if (y < -32768) y = -32768;
		uint32_t next = (m->a_head + 1) % M1_AUDIO_RING;
		if (next != m->a_tail) { m->a_ring[m->a_head] = (int16_t)y; m->a_head = next; }
		else m->a_dropped++;
		m->a_acc = 0;
		m->a_prev = m->a_at;
		m->a_at += m->a_num / m->a_den;
		m->a_rem += m->a_num % m->a_den;
		if (m->a_rem >= m->a_den) { m->a_at++; m->a_rem -= m->a_den; }
	}
}

static uint8_t sz_in(z80 *z, uint8_t p) { (void)z; (void)p; return 0xFF; }
static void sz_out(z80 *z, uint8_t p, uint8_t v)
{
	m1_t *m = (m1_t *)z->userdata;
	if (p != 0xFF) return;
	/* the cassette output: 1 one way, 2 the other, 0 (and 3) nothing. The
	   edge is stamped at the start of the writing instruction (m->cycles has
	   not counted it yet): the same offset for every edge, so pitch is exact */
	int8_t level = (v & 3) == 1 ? 1 : (v & 3) == 2 ? -1 : 0;
#ifdef M1_EDGES
	{ extern void m1_edge(uint64_t at, int level); m1_edge(m->cycles, level); }
#endif
	if (level == m->a_level || !m->a_num) { m->a_level = level; return; }
	audio_advance(m, m->cycles);
	m->a_acc += (int64_t)m->a_level * (int64_t)(m->cycles - m->a_last);
	m->a_last = m->cycles;
	m->a_level = level;
}

void m1_audio(m1_t *m, uint64_t num, uint64_t den)
{
	m->a_num = den ? num : 0; m->a_den = den ? den : 1;
	m->a_prev = m->a_last = m->cycles;
	m->a_at = m->cycles + (m->a_num / m->a_den); m->a_rem = m->a_num % m->a_den;
	m->a_acc = 0; m->a_x1 = m->a_y1 = 0;
	m->a_head = m->a_tail = 0;
}

size_t m1_audio_drain(m1_t *m, int16_t *dst, size_t max)
{
	size_t n = 0;
	while (n < max && m->a_tail != m->a_head) { dst[n++] = m->a_ring[m->a_tail]; m->a_tail = (m->a_tail + 1) % M1_AUDIO_RING; }
	return n;
}

uint32_t m1_audio_dropped(const m1_t *m) { return m->a_dropped; }

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
	m->a_level = 0;
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
#ifdef M1_PCHOOK
		{ extern void m1_pchook(uint16_t pc, uint8_t a, uint8_t b, uint8_t c); m1_pchook(m->cpu.pc, m->cpu.a, m->cpu.b, m->cpu.c); }
#endif
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
	audio_advance(m, m->cycles);
	return done;
}

const uint8_t *m1_vram(const m1_t *m) { return m->mem + M1_VRAM_BASE; }
uint64_t m1_instructions(const m1_t *m) { return m->instructions; }
uint64_t m1_cycles(const m1_t *m) { return m->cycles; }
void m1_set_matrix(m1_t *m, const uint8_t rows[8]) { memcpy(m->kbd, rows, 8); }
fdc_t *m1_fdc(m1_t *m) { return &m->fdc; }
bool m1_has_ei(const m1_t *m) { return m->ei; }
uint8_t m1_peek(const m1_t *m, uint16_t a) { return a >= M1_VRAM_BASE && a < m->ram_top ? m->mem[a] : 0xFF; }
void m1_poke(m1_t *m, uint16_t a, uint8_t v) { if (a >= M1_RAM_BASE && a < m->ram_top) m->mem[a] = v; }
