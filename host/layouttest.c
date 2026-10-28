/* layouttest.c -- the layout parser, and a layout's keys reaching the matrix */
#include <stdio.h>
#include <string.h>
#include "keys.h"

static int bad;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL: " __VA_ARGS__); puts(""); bad++; } } while (0)

static keys_t k;
static void tap(uint8_t code, bool down) { keys_event(&k, code, down); for (int i = 0; i < 6; i++) keys_field(&k); }

int main(void)
{
	key_bind_t b[KEYS_LAYOUT];
	const char *meteor =
		"# Meteor Mission II: < > move, R fires\r\n"
		"LEFT  = ,\r\n"
		"right = .    # the other way\r\n"
		"UP = R\r\n"
		"space = r\r\n"
		"\r\n"
		"x = SHIFT\n";
	int n = keys_parse_layout(meteor, b, KEYS_LAYOUT);
	CHECK(n == 5, "meteor: %d keys", n);
	CHECK(keys_parse_layout("LEFT = ,\nLEFT = .\n", b, KEYS_LAYOUT) == -2, "a key twice");
	CHECK(keys_parse_layout("LEFT ,\n", b, KEYS_LAYOUT) == -1, "no =");
	CHECK(keys_parse_layout("a = b\nWIBBLE = A\n", b, KEYS_LAYOUT) == -2, "unknown PicoCalc key");
	CHECK(keys_parse_layout("a = <\n", b, KEYS_LAYOUT) == -1, "a shifted symbol isn't a Model I key of its own");
	CHECK(keys_parse_layout("= = ENTER\n", b, KEYS_LAYOUT) == 1, "= itself");
	CHECK(keys_parse_layout("", b, KEYS_LAYOUT) == 0, "empty");

	n = keys_parse_layout(meteor, b, KEYS_LAYOUT);
	keys_init(&k);
	keys_set_layout(&k, b, n);
	keys_init(&k);				/* (power on keeps it) */
	CHECK(k.n_layout == 5, "layout kept over keys_init");
	tap(0xB4, true);			/* LEFT down: the comma key, no SHIFT */
	CHECK(k.matrix[5] == 0x10 && !k.matrix[7] && !k.matrix[6], "LEFT gives , : %02x %02x %02x", k.matrix[5], k.matrix[6], k.matrix[7]);
	tap(' ', true);				/* with fire held too */
	CHECK(k.matrix[5] == 0x10 && k.matrix[2] == 0x04, "LEFT + SPACE: , and R: %02x %02x", k.matrix[5], k.matrix[2]);
	tap(0xB4, false); tap(' ', false);
	for (int r = 0; r < 8; r++) CHECK(!k.matrix[r], "row %d left down", r);
	tap('9', true);				/* everything else as usual */
	CHECK(k.matrix[5] == 0x02, "9 as usual");
	tap('9', false);
	printf("%s\n", bad ? "FAILED" : "all passed");
	return bad != 0;
}
