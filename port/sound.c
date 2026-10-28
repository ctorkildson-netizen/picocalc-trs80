/* sound.c -- the PicoCalc's speakers, for the Model I's 1-bit sound.
 *
 * GP26 and GP27 are the two channels of one PWM slice, filtered into the
 * amplifier (Blair Leduc's hardware notes, 5.1-5.3). The slice runs at
 * clk_sys / 2048, about 73 kHz at 150 MHz; each sample is written for two
 * periods, so samples go at clk_sys / 4096. One DMA channel, set to run for
 * ever and paced by the slice's wrap, reads a ring of compare words with the
 * hardware's ring wrap: no interrupts at all, so there is no refill deadline
 * to miss. The program writes ahead of where the DMA is reading; if it falls
 * behind, the DMA plays the silence written ahead of the samples. */
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/pwm.h"
#include "hardware/dma.h"
#include "hardware/clocks.h"
#include "sound.h"

#define TOP 2047
#define MID ((TOP + 1) / 2)
#define RING_BITS 15				/* bytes: 32 KB, 8192 words, 4096 samples */
#define RING_WORDS ((1u << RING_BITS) / 4)
#define SILENCE_AHEAD 2048			/* words of silence kept past the samples */

static uint32_t ring[RING_WORDS] __attribute__((aligned(1u << RING_BITS)));
static int dma_ch = -1;
static uint32_t w;				/* the next word to write, as an index */
static bool muted;
static int vol = 2;			/* 0 off, 1 quiet, 2 medium, 3 loud */
static const int32_t vol_scale[4] = { 0, 96, 176, 256 };	/* /256 */
static uint32_t late;

static inline uint32_t word(int16_t s)
{
	int32_t d = MID + (((int32_t)s * (MID - 1)) >> 15);
	return (uint32_t)d | ((uint32_t)d << 16);	/* (left in the low half, right in the high) */
}

static uint32_t reading(void)		/* the word the DMA reads next */
{
	return ((uint32_t)dma_hw->ch[dma_ch].read_addr - (uint32_t)ring) / 4 % RING_WORDS;
}

void sound_init(void)
{
	uint slice = pwm_gpio_to_slice_num(26);
	gpio_set_function(26, GPIO_FUNC_PWM);
	gpio_set_function(27, GPIO_FUNC_PWM);
	pwm_config c = pwm_get_default_config();
	pwm_config_set_clkdiv_int(&c, (uint32_t)(clock_get_hz(clk_sys) / 150000000u ? clock_get_hz(clk_sys) / 150000000u : 1));
	pwm_config_set_wrap(&c, TOP);
	pwm_init(slice, &c, false);
	for (unsigned i = 0; i < RING_WORDS; i++) ring[i] = word(0);

	dma_ch = dma_claim_unused_channel(true);
	dma_channel_config d = dma_channel_get_default_config((uint)dma_ch);
	channel_config_set_transfer_data_size(&d, DMA_SIZE_32);
	channel_config_set_read_increment(&d, true);
	channel_config_set_write_increment(&d, false);
	channel_config_set_ring(&d, false, RING_BITS);
	channel_config_set_dreq(&d, pwm_get_dreq(slice));
	dma_channel_configure((uint)dma_ch, &d, &pwm_hw->slice[slice].cc, ring, dma_encode_endless_transfer_count(), true);
	pwm_set_enabled(slice, true);
	w = (reading() + 2 * 1220) % RING_WORDS;	/* (start two fields ahead) */
}


/* words written and not yet played */
static uint32_t ahead(void) { return (w - reading() + RING_WORDS) % RING_WORDS; }

void sound_put(const int16_t *s, size_t n)
{
	if (dma_ch < 0) return;
	/* fallen behind (the DMA passed us): start again a little ahead of it */
	if (ahead() > RING_WORDS - SILENCE_AHEAD) { late++; w = (reading() + 64) % RING_WORDS; }
	for (size_t i = 0; i < n; i++) {
		uint32_t v = word(muted ? 0 : (int16_t)((int32_t)s[i] * vol_scale[vol] / 256));
		ring[w] = v; ring[(w + 1) % RING_WORDS] = v;
		w = (w + 2) % RING_WORDS;
	}
	for (uint32_t i = 0; i < SILENCE_AHEAD; i++) ring[(w + i) % RING_WORDS] = word(0);
}

uint32_t sound_queued(void) { return dma_ch < 0 ? 0 : ahead() / 2; }

void sound_silence(void)
{
	if (dma_ch < 0) return;
	for (unsigned i = 0; i < RING_WORDS; i++) ring[i] = word(0);
	w = (reading() + 2 * 1220) % RING_WORDS;
}

void sound_mute(bool on) { muted = on; }
bool sound_muted(void) { return muted; }
uint32_t sound_late(void) { return late; }

void sound_volume(int v) { vol = v < 0 ? 0 : v > 3 ? 3 : v; }
int sound_get_volume(void) { return vol; }
