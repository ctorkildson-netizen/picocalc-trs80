/* disks.c -- host test: boot a disk image (drive 0, and optionally drive 1)
 * and type at it. Writes go to copies in /tmp, never the images given.
 * usage: disks level2.rom drive0.dsk [drive1.dsk] [seconds] [keys...] */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "m1.h"
#include "keys.h"

static FILE *img[FDC_DRIVES];
static bool rd(void *c, int d, uint32_t o, void *b, uint32_t l)
{ (void)c; return img[d] && !fseek(img[d], o, SEEK_SET) && fread(b, 1, l, img[d]) == l; }
static bool wr(void *c, int d, uint32_t o, const void *b, uint32_t l)
{ (void)c; return img[d] && !fseek(img[d], o, SEEK_SET) && fwrite(b, 1, l, img[d]) == l && !fflush(img[d]); }
static const disk_io_t io = { rd, wr, NULL };

static uint8_t machine[1 << 17];
static m1_t *m;
static keys_t k;
static void fields(int n) { while (n--) { keys_field(&k); m1_set_matrix(m, k.matrix); m1_run(m, 29568); } }
static void type(const char *s)
{
	for (; *s; s++) {
		uint8_t c = (uint8_t)*s;
		if (c == '~') { fields(60); continue; }		/* a pause */
		bool sh = strchr("!\"#$%&'()*+<=>?", c) != NULL;
		if (sh) keys_event(&k, 0xA2, true);
		keys_event(&k, c == '\n' ? 0x0A : c, true);
		keys_event(&k, c == '\n' ? 0x0A : c, false);
		if (sh) keys_event(&k, 0xA2, false);
		fields(KEYS_HOLD + KEYS_GAP + 1);
	}
}
static void show(void)
{
	const uint8_t *v = m1_vram(m);
	for (int r = 0; r < 16; r++) {
		char l[65];
		for (int c = 0; c < 64; c++) { uint8_t ch = v[r*64+c]; l[c] = ch & 0x80 ? (ch == 0x80 ? ' ' : '#') : (ch & 0x7F) < 0x20 ? (ch & 0x7F) + 0x40 : ch & 0x7F; }
		l[64] = 0; for (int i = 63; i >= 0 && l[i] == ' '; i--) l[i] = 0;
		printf("|%s\n", l);
	}
}

static int copy_in(int d, const char *path)
{
	FILE *s = fopen(path, "rb");
	if (!s) return 0;
	char t[64]; snprintf(t, sizeof t, "/tmp/m1disk%d.dsk", d);
	FILE *o = fopen(t, "w+b");
	char b[4096]; size_t n; uint32_t size = 0;
	while ((n = fread(b, 1, sizeof b, s))) { fwrite(b, 1, n, o); size += n; }
	fclose(s);
	img[d] = o;
	uint8_t h[16] = { 0 }; fseek(o, 0, SEEK_SET); if (fread(h, 1, 16, o) != 16) return 0;
	return fdc_insert(m1_fdc(m), d, size, h, false);
}

int main(int argc, char **argv)
{
	if (argc < 3) { fprintf(stderr, "usage: disks level2.rom drive0.dsk [drive1.dsk|-] [seconds] [keys]\n"); return 2; }
	static uint8_t rom[M1_ROM_SIZE];
	FILE *f = fopen(argv[1], "rb");
	if (!f || fread(rom, 1, sizeof rom, f) != sizeof rom) return 77;
	fclose(f);
	m = (m1_t *)machine;
	m1_init(m, rom, &io);
	keys_init(&k);
	if (!copy_in(0, argv[2])) { fprintf(stderr, "drive 0: not an image I know\n"); return 2; }
	if (argc > 3 && strcmp(argv[3], "-") && !copy_in(1, argv[3])) { fprintf(stderr, "drive 1: not an image I know\n"); return 2; }
	m1_power_on(m, 48);
	int secs = argc > 4 ? atoi(argv[4]) : 10;
	fields(secs * 60);
	for (int i = 5; i < argc; i++) { type(argv[i]); fields(120); }
	show();
	printf("ei %d, %.1f s, %u image errors\n", m1_has_ei(m), m1_cycles(m) / (double)M1_CLOCK_HZ, (unsigned)m1_fdc(m)->errors);
	return 0;
}
