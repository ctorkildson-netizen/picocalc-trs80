/* dosclock.h -- give a TRSDOS-style DOS (TRSDOS 2.3, NEWDOS/80) the date and
 * time, as its owner would have typed them in. Such a DOS keeps its clock in
 * RAM: 4041 seconds, 4042 minutes, 4043 hours, 4044 year (two digits), 4045
 * day, 4046 month, counted by its 40 Hz interrupt. Once the clock is seen to
 * tick with no date set -- the seconds going up by one, about a second apart,
 * so a program that only happens to use that memory isn't taken for a DOS --
 * the time is put there, once per power-on. Portable. */
#ifndef DOSCLOCK_H
#define DOSCLOCK_H
#include <stdbool.h>
#include "m1.h"

typedef struct { int fields, changed; uint8_t last; uint8_t ticks; bool done; } dosclock_t;

static inline void dosclock_reset(dosclock_t *c) { c->fields = 0; c->changed = 0; c->last = 0xFF; c->ticks = 0; c->done = false; }

/* once a field; returns true the field it sets the clock */
bool dosclock_field(dosclock_t *c, m1_t *m, int year, int month, int day, int hour, int min, int sec);

#endif
