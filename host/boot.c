#define _GNU_SOURCE
/* boot.c -- host test: boot Level II BASIC, show the screen as text, and time
 * the CPU. usage: boot level2.rom [seconds of guest time] */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "m1.h"

static bool no_rd(void *c, int d, uint32_t o, void *b, uint32_t l) { (void)c; (void)d; (void)o; (void)b; (void)l; return false; }
static bool no_wr(void *c, int d, uint32_t o, const void *b, uint32_t l) { (void)c; (void)d; (void)o; (void)b; (void)l; return false; }
static const disk_io_t no_disks = { no_rd, no_wr, NULL };

static uint8_t machine[1 << 17];

static void show(const uint8_t *v)
{
	for (int r = 0; r < M1_ROWS; r++) {
		char line[M1_COLS + 1];
		for (int c = 0; c < M1_COLS; c++) {
			uint8_t ch = v[r * M1_COLS + c];
			line[c] = ch >= 0x80 ? '#' : (ch & 0x7F) < 0x20 ? (char)((ch & 0x7F) + 0x40) : (char)(ch & 0x7F);
		}
		line[M1_COLS] = 0;
		int l = M1_COLS;
		while (l > 0 && line[l - 1] == ' ') line[--l] = 0;
		printf("|%s\n", line);
	}
}

int main(int argc, char **argv)
{
	if (argc < 2) { fprintf(stderr, "usage: boot level2.rom [seconds]\n"); return 2; }
	static uint8_t rom[M1_ROM_SIZE];
	FILE *f = fopen(argv[1], "rb");
	if (!f || fread(rom, 1, sizeof rom, f) != sizeof rom) { fprintf(stderr, "can't read %s\n", argv[1]); return 2; }
	fclose(f);
	double secs = argc > 2 ? atof(argv[2]) : 2.0;
	if (m1_size() > sizeof machine) { fprintf(stderr, "machine too big\n"); return 2; }
	m1_t *m = (m1_t *)machine;
	m1_init(m, rom, &no_disks);
	uint64_t want = (uint64_t)(secs * M1_CLOCK_HZ), done = 0;
	clock_t t0 = clock();
	while (done < want) done += m1_run(m, 29568);	/* (one 60 Hz field) */
	double host = (double)(clock() - t0) / CLOCKS_PER_SEC;
	show(m1_vram(m));
	printf("%.2f s of guest time, %llu instructions, %.3f s on the host (%.1fx real time)\n",
	       done / (double)M1_CLOCK_HZ, (unsigned long long)m1_instructions(m), host,
	       done / (double)M1_CLOCK_HZ / host);
	return memmem(m1_vram(m), M1_VRAM_SIZE, "MEMORY SIZE", 11) ? 0 : 1;
}
