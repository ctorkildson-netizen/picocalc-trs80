/* sound.h -- the speakers: samples at clk_sys / 4096 (see sound.c) */
#ifndef SOUND_H
#define SOUND_H
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
void sound_init(void);
void sound_put(const int16_t *s, size_t n);	/* append samples */
uint32_t sound_queued(void);			/* samples waiting to play */
void sound_silence(void);			/* everything waiting becomes silence (before a menu) */
void sound_mute(bool on);
bool sound_muted(void);
uint32_t sound_late(void);			/* times the samples fell behind */
#endif
