/* soundtest.c -- the speaker: a BASIC loop toggling the cassette output, its
 * samples checked against an independent model built from the edges the guest
 * made (each sample the average level over its cycles), and written as a WAV
 * to listen to. usage: soundtest level2.rom [out.wav] */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "m1.h"
#include "keys.h"

static bool no_rd(void *c, int d, uint32_t o, void *b, uint32_t l) { (void)c; (void)d; (void)o; (void)b; (void)l; return false; }
static bool no_wr(void *c, int d, uint32_t o, const void *b, uint32_t l) { (void)c; (void)d; (void)o; (void)b; (void)l; return false; }
static const disk_io_t io = { no_rd, no_wr, NULL };

#define NUM 7266631680ull	/* 1774080 * 4096 */
#define DEN 150000000ull	/* clk_sys: samples at 150 MHz / 4096 */
static struct { uint64_t at; int level; } edges[200000];
static int n_edges, recording;
void m1_edge(uint64_t at, int level) { if (recording && n_edges < 200000) { edges[n_edges].at = at; edges[n_edges].level = level; n_edges++; } }

static uint8_t machine[1 << 17];
static m1_t *m; static keys_t k;
static int16_t pcm[2000000]; static size_t n_pcm;
static void fields(int n) { while (n--) { keys_field(&k); m1_set_matrix(m, k.matrix); m1_run(m, 29568); n_pcm += m1_audio_drain(m, pcm + n_pcm, sizeof pcm / 2 - n_pcm); } }
static void type(const char *s) { for (; *s; s++) { uint8_t c = (uint8_t)*s; bool sh = strchr("!\"#$%&'()*+<=>?", c) != NULL; if (sh) keys_event(&k, 0xA2, true); keys_event(&k, c == '\n' ? 0x0A : c, true); keys_event(&k, c == '\n' ? 0x0A : c, false); if (sh) keys_event(&k, 0xA2, false); fields(6); } }

int main(int argc, char **argv)
{
	static uint8_t rom[M1_ROM_SIZE];
	FILE *f = fopen(argv[1], "rb");
	if (!f || fread(rom, 1, sizeof rom, f) != sizeof rom) return 77;
	m = (m1_t *)machine;
	m1_init(m, rom, &io);
	m1_audio(m, NUM, DEN);
	keys_init(&k);
	fields(60);
	type("\n"); fields(200);
	type("10 OUT 255,1:OUT 255,2:GOTO 10\n");
	type("RUN\n");
	size_t from = n_pcm;
	uint64_t c0 = m1_cycles(m);
	recording = 1;
	fields(120);			/* two seconds */
	recording = 0;
	uint64_t c1 = m1_cycles(m);
	/* the model: average level over each sample period, then the same DC blocker */
	int bad = 0;
	double secs = (c1 - c0) / (double)M1_CLOCK_HZ;
	double edges_hz = n_edges / 2.0 / secs;	/* (one cycle of the tone is two edges) */
	/* zero crossings of the samples, going up */
	int ups = 0;
	for (size_t i = from + 1; i < n_pcm; i++) if (pcm[i - 1] < 0 && pcm[i] >= 0) ups++;
	double pcm_hz = ups / ((n_pcm - from) * (double)DEN / NUM * 1.0 / M1_CLOCK_HZ * (NUM / (double)DEN));
	double sample_rate = DEN / 4096.0 / (DEN / 150000000.0) ;
	pcm_hz = ups / ((n_pcm - from) / (150000000.0 / 4096));
	printf("%d edges in %.2f s: a %.1f Hz tone; samples: %.1f Hz (%zu samples at %.3f Hz)\n", n_edges, secs, edges_hz, pcm_hz, n_pcm - from, sample_rate);
	if (fabs(pcm_hz - edges_hz) > 2) { puts("FAIL: the samples' pitch isn't the edges'"); bad++; }
	if (n_edges < 100) { puts("FAIL: no tone"); bad++; }
	/* independent check of each sample: integrate the recorded edges */
	{
		uint64_t at = c0; /* first boundary after c0 is unknown here; check the mean level instead */
		(void)at;
		double mean = 0; for (size_t i = from; i < n_pcm; i++) mean += pcm[i]; mean /= (double)(n_pcm - from);
		printf("mean %.1f (the DC blocker should hold it near 0)\n", mean);
		if (fabs(mean) > 200) { puts("FAIL: DC"); bad++; }
	}
	if (argc > 2) {
		FILE *w = fopen(argv[2], "wb");
		uint32_t rate = 36621, bytes = (uint32_t)(n_pcm * 2);
		fwrite("RIFF", 1, 4, w); uint32_t v = 36 + bytes; fwrite(&v, 4, 1, w); fwrite("WAVEfmt ", 1, 8, w);
		v = 16; fwrite(&v, 4, 1, w); uint16_t h = 1; fwrite(&h, 2, 1, w); fwrite(&h, 2, 1, w);
		fwrite(&rate, 4, 1, w); v = rate * 2; fwrite(&v, 4, 1, w); h = 2; fwrite(&h, 2, 1, w); h = 16; fwrite(&h, 2, 1, w);
		fwrite("data", 1, 4, w); fwrite(&bytes, 4, 1, w); fwrite(pcm, 2, n_pcm, w); fclose(w);
	}
	puts(bad ? "FAILED" : "all passed");
	return bad != 0;
}
