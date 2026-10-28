/* keys.c -- see keys.h */
#include <string.h>
#include "keys.h"

/* PicoCalc key codes (picocalc-text-starter's keyboard.h); repeated here so
   the core needs no driver headers */
enum {
	PC_BACKSPACE = 0x08, PC_TAB = 0x09, PC_ENTER = 0x0A, PC_RETURN = 0x0D,
	PC_ESC = 0xB1, PC_LEFT = 0xB4, PC_UP = 0xB5, PC_DOWN = 0xB6, PC_RIGHT = 0xB7,
	PC_BREAK = 0xD0, PC_HOME = 0xD2, PC_DEL = 0xD4,
	PC_SHL = 0xA2, PC_SHR = 0xA3,
};

/* The Model I's matrix (row: address bit at 3801-3880; col: data bit):
     0: @ A B C D E F G      4: 0 1 2 3 4 5 6 7
     1: H I J K L M N O      5: 8 9 : ; , - . /
     2: P Q R S T U V W      6: ENTER CLEAR BREAK UP DOWN LEFT RIGHT SPACE
     3: X Y Z                7: SHIFT
   Symbols are SHIFT with a number or punctuation key, as printed on its
   keycaps. PicoCalc keys with no Model I key of their own: Backspace is LEFT
   (the ROM's backspace), Tab is RIGHT (its tab), Del is SHIFT-LEFT (delete
   the line), Home is CLEAR, Esc and Break are BREAK. */
static const key_bind_t table[] = {
	{ '@', 0, 0, SH_OFF },
	{ PC_ENTER, 6, 0, SH_ANY }, { PC_RETURN, 6, 0, SH_ANY },
	{ PC_HOME, 6, 1, SH_ANY },
	{ PC_ESC, 6, 2, SH_ANY }, { PC_BREAK, 6, 2, SH_ANY },
	{ PC_UP, 6, 3, SH_ANY }, { PC_DOWN, 6, 4, SH_ANY },
	{ PC_LEFT, 6, 5, SH_ANY }, { PC_BACKSPACE, 6, 5, SH_OFF }, { PC_DEL, 6, 5, SH_ON },
	{ PC_RIGHT, 6, 6, SH_ANY }, { PC_TAB, 6, 6, SH_OFF },
	{ ' ', 6, 7, SH_ANY },
	{ ':', 5, 2, SH_OFF }, { ';', 5, 3, SH_OFF }, { ',', 5, 4, SH_OFF },
	{ '-', 5, 5, SH_OFF }, { '.', 5, 6, SH_OFF }, { '/', 5, 7, SH_OFF },
	{ '!', 4, 1, SH_ON }, { '"', 4, 2, SH_ON }, { '#', 4, 3, SH_ON }, { '$', 4, 4, SH_ON },
	{ '%', 4, 5, SH_ON }, { '&', 4, 6, SH_ON }, { '\'', 4, 7, SH_ON },
	{ '(', 5, 0, SH_ON }, { ')', 5, 1, SH_ON },
	{ '*', 5, 2, SH_ON }, { '+', 5, 3, SH_ON }, { '<', 5, 4, SH_ON },
	{ '=', 5, 5, SH_ON }, { '>', 5, 6, SH_ON }, { '?', 5, 7, SH_ON },
};

const key_bind_t *keys_lookup(uint8_t code)
{
	static key_bind_t b;
	if (code >= 'a' && code <= 'z') code -= 32;
	if (code >= 'A' && code <= 'Z') {		/* letters: unshifted, whatever the case */
		int i = code - 'A' + 1;			/* (@ is cell 0) */
		b = (key_bind_t){ code, (uint8_t)(i / 8), (uint8_t)(i % 8), SH_OFF };
		return &b;
	}
	if (code >= '0' && code <= '9') {
		int i = code - '0';
		b = (key_bind_t){ code, (uint8_t)(4 + i / 8), (uint8_t)(i % 8), SH_OFF };
		return &b;
	}
	for (unsigned i = 0; i < sizeof table / sizeof table[0]; i++)
		if (table[i].code == code) return &table[i];
	return NULL;
}

void keys_init(keys_t *k)	/* (the layout stays) */
{
	key_bind_t l[KEYS_LAYOUT];
	uint8_t n = k->n_layout <= KEYS_LAYOUT ? k->n_layout : 0;
	memcpy(l, k->layout, sizeof l);
	memset(k, 0, sizeof *k);
	memcpy(k->layout, l, sizeof l);
	k->n_layout = n;
}

void keys_event(keys_t *k, uint8_t code, bool down)
{
	uint8_t next = (uint8_t)((k->tail + 1) % KEYS_QUEUE);
	if (next == k->head) { k->dropped++; return; }
	k->q[k->tail].code = code;
	k->q[k->tail].down = down;
	k->tail = next;
}

static void rebuild(keys_t *k)
{
	memset(k->matrix, 0, sizeof k->matrix);
	bool need_on = false, need_off = false;
	for (int i = 0; i < k->n_held; i++) {
		k->matrix[k->held[i].row] |= (uint8_t)(1u << k->held[i].col);
		if (k->held[i].shift == SH_ON) need_on = true;
		if (k->held[i].shift == SH_OFF) need_off = true;
	}
	/* SHIFT on its own counts too: games read it as a button */
	if (need_on || (k->pc_shift && !need_off)) k->matrix[7] |= 1;
}

/* apply one event; returns the fields to wait before the next */
static uint8_t apply(keys_t *k, uint8_t code, bool down)
{
	if (code == PC_SHL || code == PC_SHR) { k->pc_shift = down; return 0; }
	if (code >= 'a' && code <= 'z') code -= 32;	/* (one key, whichever case it came as) */
	if (down) {
		for (int i = 0; i < k->n_held; i++) if (k->held[i].code == code) return 0;	/* (already down) */
		const key_bind_t *b = NULL;
		for (int i = 0; i < k->n_layout; i++) if (k->layout[i].code == code) b = &k->layout[i];
		if (!b) b = keys_lookup(code);
		if (!b || k->n_held == KEYS_HELD) return 0;
		k->held[k->n_held].code = code;
		k->held[k->n_held].row = b->row;
		k->held[k->n_held].col = b->col;
		k->held[k->n_held].shift = b->shift;
		k->held[k->n_held].with_shift = k->pc_shift;
		k->n_held++;
		return KEYS_HOLD;
	}
	for (int i = 0; i < k->n_held; i++)
		if (k->held[i].code == code) {
			k->held[i] = k->held[--k->n_held];
			return KEYS_GAP;
		}
	/* A release we never saw pressed: the PicoCalc names a key by what it
	   types now, so a key pressed with Shift and let go after Shift comes up
	   as another character ('(' down, '9' up). Let go of the keys that went
	   down with Shift, which is where it must be. */
	bool any = false;
	for (int i = 0; i < k->n_held; )
		if (k->held[i].with_shift) { k->held[i] = k->held[--k->n_held]; any = true; }
		else i++;
	return any ? KEYS_GAP : 0;
}

void keys_field(keys_t *k)
{
	if (k->wait) k->wait--;
	while (!k->wait && k->head != k->tail) {
		k->wait = apply(k, k->q[k->head].code, k->q[k->head].down);
		k->head = (uint8_t)((k->head + 1) % KEYS_QUEUE);
	}
	rebuild(k);
}

/* ------------------------------------------------------------ layouts */

static const struct { const char *name; uint8_t code; } pc_names[] = {
	{ "LEFT", PC_LEFT }, { "RIGHT", PC_RIGHT }, { "UP", PC_UP }, { "DOWN", PC_DOWN },
	{ "SPACE", ' ' }, { "ENTER", PC_ENTER }, { "TAB", PC_TAB }, { "BACKSPACE", PC_BACKSPACE },
	{ "DEL", PC_DEL }, { "HOME", PC_HOME }, { "ESC", PC_ESC },
};
static const struct { const char *name; uint8_t row, col; } m1_names[] = {
	{ "ENTER", 6, 0 }, { "CLEAR", 6, 1 }, { "BREAK", 6, 2 }, { "UP", 6, 3 }, { "DOWN", 6, 4 },
	{ "LEFT", 6, 5 }, { "RIGHT", 6, 6 }, { "SPACE", 6, 7 }, { "SHIFT", 7, 0 },
};

static int same(const char *a, const char *b, int n)	/* a[0..n) equals b, any case */
{
	int i;
	for (i = 0; i < n && b[i]; i++) {
		char x = a[i], y = b[i];
		if (x >= 'a' && x <= 'z') x -= 32;
		if (x != y) return 0;
	}
	return i == n && !b[i];
}

static int pc_key(const char *w, int n)
{
	if (n == 1) return (uint8_t)w[0] >= 'a' && (uint8_t)w[0] <= 'z' ? w[0] - 32 : (uint8_t)w[0];
	for (unsigned i = 0; i < sizeof pc_names / sizeof pc_names[0]; i++) if (same(w, pc_names[i].name, n)) return pc_names[i].code;
	return -1;
}

static bool m1_key(const char *w, int n, uint8_t *row, uint8_t *col)
{
	if (n == 1) {
		uint8_t c = (uint8_t)w[0];
		if (c >= 'a' && c <= 'z') c -= 32;
		const key_bind_t *b = keys_lookup(c);
		/* (only keys with a place of their own: not the shifted symbols) */
		if (!b || b->shift == SH_ON || (b->row == 6 && c != ' ')) return false;
		*row = b->row; *col = b->col;
		return true;
	}
	for (unsigned i = 0; i < sizeof m1_names / sizeof m1_names[0]; i++)
		if (same(w, m1_names[i].name, n)) { *row = m1_names[i].row; *col = m1_names[i].col; return true; }
	return false;
}

int keys_parse_layout(const char *text, key_bind_t *out, int max)
{
	int n = 0, line = 0;
	const char *p = text;
	while (*p) {
		line++;
		const char *e = p;
		while (*e && *e != '\n') e++;
		const char *q = p, *end = e;
		if (end > q && end[-1] == '\r') end--;
		/* a comment: # at the start, or after a space */
		for (const char *c = q; c < end; c++) if (*c == '#' && (c == q || c[-1] == ' ' || c[-1] == '\t')) { end = c; break; }
		while (q < end && (*q == ' ' || *q == '\t')) q++;
		while (end > q && (end[-1] == ' ' || end[-1] == '\t')) end--;
		if (q < end) {
			const char *eq = NULL;
			for (const char *c = q + 1; c < end; c++) if (*c == '=') { eq = c; break; }	/* (q itself may be '=') */
			if (!eq) return -line;
			const char *a = q, *ae = eq, *b = eq + 1, *be = end;
			while (ae > a && (ae[-1] == ' ' || ae[-1] == '\t')) ae--;
			while (b < be && (*b == ' ' || *b == '\t')) b++;
			int code = pc_key(a, (int)(ae - a));
			uint8_t row, col;
			if (code < 0 || code == '#' || !m1_key(b, (int)(be - b), &row, &col)) return -line;
			for (int i = 0; i < n; i++) if (out[i].code == code) return -line;	/* twice */
			if (n == max) return -line;
			out[n++] = (key_bind_t){ (uint8_t)code, row, col, SH_ANY };
		}
		p = *e ? e + 1 : e;
	}
	return n;
}

void keys_set_layout(keys_t *k, const key_bind_t *b, int n)
{
	if (n < 0) n = 0;
	if (n > KEYS_LAYOUT) n = KEYS_LAYOUT;
	memcpy(k->layout, b, sizeof *b * (size_t)n);
	k->n_layout = (uint8_t)n;
}
