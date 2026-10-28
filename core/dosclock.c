/* dosclock.c -- see dosclock.h */
#include "dosclock.h"

bool dosclock_field(dosclock_t *c, m1_t *m, int year, int month, int day, int hour, int min, int sec)
{
	if (c->done || !m1_has_ei(m)) return false;
	if (++c->fields > 60 * 60) { c->done = true; return false; }	/* (not a DOS that does this) */
	uint8_t s = m1_peek(m, 0x4041);
	bool undated = !m1_peek(m, 0x4044) && !m1_peek(m, 0x4045) && !m1_peek(m, 0x4046);
	if (s != c->last) {
		int gap = c->fields - c->changed;
		bool tick = c->last != 0xFF && c->last < 60 && s == (c->last + 1) % 60 && gap >= 50 && gap <= 70;
		c->ticks = tick && undated ? (uint8_t)(c->ticks + 1) : 0;
		c->last = s;
		c->changed = c->fields;
	}
	if (c->ticks < 2 || !undated) return false;	/* (two ticks: it's running, past its start) */
	m1_poke(m, 0x4041, (uint8_t)sec);
	m1_poke(m, 0x4042, (uint8_t)min);
	m1_poke(m, 0x4043, (uint8_t)hour);
	m1_poke(m, 0x4044, (uint8_t)(year % 100));
	m1_poke(m, 0x4045, (uint8_t)day);
	m1_poke(m, 0x4046, (uint8_t)month);
	c->done = true;
	return true;
}
