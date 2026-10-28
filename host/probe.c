/* probe.c -- hold Model I keys directly in a running game and see what moves.
 * usage: probe level2.rom drive0 drive1 "COMMAND" then steps on stdin:
 *   w N          run N fields with no keys
 *   k R C [R C]  hold matrix keys (row col pairs) for the next w
 *   s            show the screen */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "m1.h"
#include "keys.h"
static FILE *img[FDC_DRIVES];
static bool rd(void *c, int d, uint32_t o, void *b, uint32_t l) { (void)c; return img[d] && !fseek(img[d], o, SEEK_SET) && fread(b, 1, l, img[d]) == l; }
static bool wr(void *c, int d, uint32_t o, const void *b, uint32_t l) { (void)c; (void)d; (void)o; (void)b; (void)l; return true; }
static const disk_io_t io = { rd, wr, NULL };
static uint8_t machine[1 << 17];
static m1_t *m; static keys_t k; static uint8_t held[8];
static void fields(int n) { while (n--) { keys_field(&k); uint8_t mx[8]; for (int i = 0; i < 8; i++) mx[i] = k.matrix[i] | held[i]; m1_set_matrix(m, mx); m1_run(m, 29568); } }
static void type(const char *s) { for (; *s; s++) { uint8_t c = *s == '\n' ? 0x0A : (uint8_t)*s; keys_event(&k, c, true); keys_event(&k, c, false); fields(KEYS_HOLD + KEYS_GAP + 1); } }
static void show(void) { const uint8_t *v = m1_vram(m); for (int r = 0; r < 16; r++) { char l[65]; for (int c = 0; c < 64; c++) { uint8_t ch = v[r*64+c]; l[c] = ch & 0x80 ? (ch == 0x80 ? ' ' : '#') : (ch & 0x7F) < 0x20 ? (ch & 0x7F) + 0x40 : ch & 0x7F; } l[64] = 0; printf("|%s\n", l); } }
static void ins(int d, const char *p) { FILE *f = fopen(p, "rb"); img[d] = f; fseek(f, 0, SEEK_END); uint32_t sz = ftell(f); uint8_t h[16]; fseek(f, 0, SEEK_SET); fread(h, 1, 16, f); fdc_insert(m1_fdc(m), d, sz, h, false); }
static int phase; static unsigned hits[4][256];
void m1_pchook(uint16_t pc, uint8_t a, uint8_t b, uint8_t c) { (void)b; (void)c; if (pc == 0x5DF7) hits[phase][a]++; if (pc == 0x5E10) hits[3][a | (phase << 4)]++; }
int main(int argc, char **argv)
{
	static uint8_t rom[M1_ROM_SIZE]; FILE *f = fopen(argv[1], "rb"); fread(rom, 1, sizeof rom, f);
	m = (m1_t *)machine; m1_init(m, rom, &io); keys_init(&k);
	ins(0, argv[2]); ins(1, argv[3]); m1_power_on(m, 48);
	fields(300); type(argv[4]); type("\n");
	char cmd[64];
	while (fgets(cmd, sizeof cmd, stdin)) {
		if (cmd[0] == 'w') { fields(atoi(cmd + 1)); memset(held, 0, 8); }
		else if (cmd[0] == 'k') { int r, c, n; char *p = cmd + 1; while (sscanf(p, "%d %d%n", &r, &c, &n) == 2) { held[r] |= 1 << c; p += n; } }
		else if (cmd[0] == 's') { puts("----"); show(); }
		else if (cmd[0] == 'p') { phase = atoi(cmd + 1); }
		else if (cmd[0] == 'r') { for (int i = 0; i < 4; i++) { printf("phase %d:", i); for (int v = 0; v < 256; v++) if (hits[i][v]) printf(" %02x x%u", v, hits[i][v]); puts(""); } }
		else if (cmd[0] == 'd') { FILE *o = fopen("/tmp/ram.bin", "wb"); for (int a = 0; a < 0x10000; a++) fputc(m1_peek(m, (uint16_t)a), o); fclose(o); }
	}
	return 0;
}
