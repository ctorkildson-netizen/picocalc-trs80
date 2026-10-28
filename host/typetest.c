/* typetest.c -- host test: type at Level II through the keyboard table and
 * the event replay, exactly as the PicoCalc's keys arrive, and check the
 * screen. usage: typetest level2.rom */
#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include "m1.h"

static bool no_rd(void *c, int d, uint32_t o, void *b, uint32_t l) { (void)c; (void)d; (void)o; (void)b; (void)l; return false; }
static bool no_wr(void *c, int d, uint32_t o, const void *b, uint32_t l) { (void)c; (void)d; (void)o; (void)b; (void)l; return false; }
static const disk_io_t no_disks = { no_rd, no_wr, NULL };
#include "keys.h"

static uint8_t machine[1 << 17];
static m1_t *m;
static keys_t k;

static void fields(int n) { while (n--) { keys_field(&k); m1_set_matrix(m, k.matrix); m1_run(m, 29568); } }

/* a PicoCalc key: Shift first for the characters it sends shifted */
static void type(const char *s)
{
	for (; *s; s++) {
		uint8_t c = (uint8_t)*s;
		bool sh = strchr("!\"#$%&'()*+<=>?", c) != NULL;
		if (sh) keys_event(&k, 0xA2, true);
		keys_event(&k, c == '\n' ? 0x0A : c, true);
		keys_event(&k, c == '\n' ? 0x0A : c, false);	/* (released at once: the replay holds it) */
		if (sh) keys_event(&k, 0xA2, false);
		fields(KEYS_HOLD + KEYS_GAP + 1);	/* (about as fast as the replay goes) */
	}
	fields(60);
}

static void screen(char out[16][65])
{
	const uint8_t *v = m1_vram(m);
	for (int r = 0; r < 16; r++) {
		for (int c = 0; c < 64; c++) { uint8_t ch = v[r * 64 + c]; ch &= 0x7F; out[r][c] = ch < 0x20 ? ch + 0x40 : ch; if (v[r*64+c] & 0x80) out[r][c] = '#'; }
		out[r][64] = 0;
		for (int l = 64; l > 0 && out[r][l - 1] == ' '; l--) out[r][l - 1] = 0;
	}
}

static int has(const char *want)
{
	char s[16][65];
	screen(s);
	for (int r = 0; r < 16; r++) if (strstr(s[r], want)) return 1;
	return 0;
}

int main(int argc, char **argv)
{
	static uint8_t rom[M1_ROM_SIZE];
	FILE *f = argc > 1 ? fopen(argv[1], "rb") : NULL;
	if (!f || fread(rom, 1, sizeof rom, f) != sizeof rom) { fprintf(stderr, "usage: typetest level2.rom\n"); return 77; }
	fclose(f);
	if (m1_size() > M1_MACHINE_MAX) { printf("FAIL: the machine is %zu bytes, more than M1_MACHINE_MAX\n", m1_size()); return 1; }
	m = (m1_t *)machine;
	m1_init(m, rom, &no_disks);
	keys_init(&k);
	fields(120);
	int bad = 0;
	type("\n");
	fields(240);	/* (it tests the memory first) */
	if (!has("READY")) { puts("FAIL: no READY after MEMORY SIZE"); bad++; }
	type("PRINT 2+2\n");
	if (!has(" 4")) { puts("FAIL: PRINT 2+2"); bad++; }
	type("PRINT \"HELLO, WORLD!\";(3*4)-1;7/2<4;\"#$%&'\"\n");
	if (!has("HELLO, WORLD! 11 -1 #$%&'")) { puts("FAIL: symbols"); bad++; }
	type("10 FOR I=1 TO 3:PRINT I*I;:NEXT\nRUN\n");
	if (!has(" 1  4  9")) { puts("FAIL: a program"); bad++; }
	/* the PicoCalc names a key by what it types at the moment: Shift let go
	   first gives '(' down and '9' up, 'A' down and 'a' up */
	keys_event(&k, 0xA2, true); keys_event(&k, '(', true); keys_event(&k, 0xA2, false); keys_event(&k, '9', false);
	keys_event(&k, 0xA2, true); keys_event(&k, 'A', true); keys_event(&k, 0xA2, false); keys_event(&k, 'a', false);
	fields(30);
	for (int r = 0; r < 8; r++) if (k.matrix[r]) { printf("FAIL: stuck key, row %d %02x\n", r, k.matrix[r]); bad++; }
	type("\n20 PRINT 1234567890\nRUN\n");
	if (!has(" 1234567890")) { puts("FAIL: digits after shifted keys"); bad++; }
	type("PRINT CHR$(23)\n");
	if (!m1_wide(m)) { puts("FAIL: CHR$(23) didn't select 32 characters a line"); bad++; }
	type("CLS\n");
	if (m1_wide(m)) { puts("FAIL: CLS didn't go back to 64"); bad++; }
	type("PRINT MEM\n");
	char s[16][65];
	screen(s);
	for (int r = 0; r < 16; r++) if (s[r][0]) printf("|%s\n", s[r]);
	printf("%s (%u events dropped)\n", bad ? "FAILED" : "all passed", (unsigned)k.dropped);
	return bad ? 1 : 0;
}
