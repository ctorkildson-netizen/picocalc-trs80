/* m1 -- a TRS-80 Model I on the PicoCalc: first milestone, Level II BASIC on
 * the screen and the keyboard. One core for now: the guest runs a 60 Hz field,
 * the rows of the screen that changed are drawn, the keyboard is read, and the
 * loop waits for the field's deadline. */
#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "drivers/lcd.h"
#include "drivers/font.h"
#include "drivers/southbridge.h"
#include "drivers/keyboard.h"
#include "drivers/fat32.h"
#include "picocalc_shell.h"
#include "m1.h"
#include "keys.h"
#include "sha1.h"

#define VERSION "0.2"
#define FIELD_CYCLES (M1_CLOCK_HZ / 60)
#define CELL_W 5
#define CELL_H 18
#define TOP 16				/* the screen's band above the Model I's */
#define GLYPH_TOP 4			/* the 10-row glyph's place in an 18-row cell */
#define C_FG 0xFFFF
#define C_BG 0x0000
#define C_DIM 0x8410

static uint8_t machine[70 * 1024] __attribute__((aligned(8)));
static m1_t *m = (m1_t *)machine;
static keys_t keys;
static uint8_t rom[M1_ROM_SIZE];
static char rom_name[48];

/* ------------------------------------------------------------ the ROM */

/* the Level II ROMs we know, by SHA-1 of the 12 KB */
static const struct { const char *sha1, *name; } known[] = {
	{ "5e318bf5d37db5795061326fb88aac29123d4b7d", "Level II BASIC" },
	{ "b778082bb3ea46fa5922993447b8976cc12ba625", "Level II BASIC (later ROM)" },
};

static bool read_file(const char *path, uint8_t *buf, size_t want)
{
	fat32_file_t f;
	if (fat32_open(&f, path) != FAT32_OK) return false;
	size_t got = 0;
	bool ok = fat32_read(&f, buf, want, &got) == FAT32_OK && got == want;
	fat32_close(&f);
	return ok;
}

/* /trs80/level2.rom (12 KB), or MAME's three 4 KB parts */
static bool load_rom(char *problem, size_t n)
{
	bool ok = read_file("/trs80/level2.rom", rom, M1_ROM_SIZE);
	if (!ok) ok = read_file("/trs80/rom-a.z1", rom, 4096) && read_file("/trs80/rom-b.z2", rom + 4096, 4096) && read_file("/trs80/rom-c.z3", rom + 8192, 4096);
	if (!ok) ok = read_file("/trs80/rom-a_alt.z1", rom, 4096) && read_file("/trs80/rom-b_alt.z2", rom + 4096, 4096) && read_file("/trs80/rom-c_alt.z3", rom + 8192, 4096);
	if (!ok) { snprintf(problem, n, "No Level II ROM in /trs80"); return false; }
	char h[41];
	sha1_hex(rom, M1_ROM_SIZE, h);
	for (unsigned i = 0; i < sizeof known / sizeof known[0]; i++)
		if (!strcmp(h, known[i].sha1)) { snprintf(rom_name, sizeof rom_name, "%s", known[i].name); return true; }
	snprintf(rom_name, sizeof rom_name, "unknown ROM %.8s", h);	/* (it may still work) */
	return true;
}

static void no_rom(const char *problem)
{
	lcd_set_font(&font_8x10);
	const char *l[] = {
		"TRS-80 Model I", "", problem, "",
		"Put Level II BASIC on the SD card:", "",
		"  /trs80/level2.rom    (12 KB)", "or MAME's trs80l2 set:",
		"  /trs80/rom-a.z1  rom-b.z2  rom-c.z3", "", "", "Any key: back to the shell", NULL };
	for (int r = 0; l[r]; r++) lcd_putstr(0, (uint8_t)(r + 2), l[r]);
	for (;;) {
		uint16_t k = sb_read_keyboard();
		if ((k >> 8) == KEY_STATE_PRESSED) { lcd_clear_screen(); picocalc_shell_exit(); }
		sleep_ms(10);
	}
}

/* ------------------------------------------------------------ the screen */

static uint8_t shadow[M1_VRAM_SIZE];
static bool shadow_valid;
static uint16_t rowbuf[CELL_H][320];

/* 4x7 arrows for the Model I's 5B-5E (its character set has them where ASCII
   has [ \ ] ^), in the 5x10 font's layout: bit 4 is the left column */
static const uint8_t arrows[4][10] = {
	{ 0x04, 0x0E, 0x15, 0x04, 0x04, 0x04, 0x04, 0, 0, 0 },	/* up */
	{ 0x04, 0x04, 0x04, 0x04, 0x15, 0x0E, 0x04, 0, 0, 0 },	/* down */
	{ 0x00, 0x04, 0x08, 0x1F, 0x08, 0x04, 0x00, 0, 0, 0 },	/* left */
	{ 0x00, 0x04, 0x02, 0x1F, 0x02, 0x04, 0x00, 0, 0, 0 },	/* right */
};

static void draw_cell(int col, uint8_t v)
{
	int x0 = col * CELL_W;
	for (int y = 0; y < CELL_H; y++) for (int x = 0; x < CELL_W; x++) rowbuf[y][x0 + x] = C_BG;
	if (v & 0x80) {			/* graphics: 2 x 3 blocks, bit 0 top left ... bit 5 bottom right */
		for (int b = 0; b < 6; b++) {
			if (!(v & (1u << b))) continue;
			int bx = (b & 1) ? 3 : 0, bw = (b & 1) ? 2 : 3, by = (b >> 1) * 6;
			for (int y = by; y < by + 6; y++) for (int x = bx; x < bx + bw; x++) rowbuf[y][x0 + x] = C_FG;
		}
		return;
	}
	uint8_t c = v & 0x7F;
	if (c < 0x20) c += 0x40;
	const uint8_t *g = c >= 0x5B && c <= 0x5E ? arrows[c - 0x5B] : &font_5x10.glyphs[c * GLYPH_HEIGHT];
	for (int y = 0; y < GLYPH_HEIGHT; y++)
		for (int x = 0; x < CELL_W; x++)
			if (g[y] & (0x10 >> x)) rowbuf[GLYPH_TOP + y][x0 + x] = C_FG;
}

/* draw the rows that changed since the last time */
static void present(void)
{
	const uint8_t *v = m1_vram(m);
	for (int r = 0; r < M1_ROWS; r++) {
		const uint8_t *row = v + r * M1_COLS;
		if (shadow_valid && !memcmp(row, shadow + r * M1_COLS, M1_COLS)) continue;
		for (int c = 0; c < M1_COLS; c++) draw_cell(c, row[c]);
		lcd_blit(&rowbuf[0][0], 0, (uint16_t)(TOP + r * CELL_H), 320, CELL_H);
		memcpy(shadow + r * M1_COLS, row, M1_COLS);
	}
	shadow_valid = true;
}

static void status(const char *s)
{
	char b[65];
	snprintf(b, sizeof b, "%-64s", s);
	lcd_set_foreground(C_DIM);
	lcd_putstr(0, 31, b);
	lcd_set_foreground(C_FG);
}

/* F5: the Model I's screen as text, /screenshots/trs80-NNN.txt */
static void screenshot(void)
{
	fat32_file_t f;
	char path[48];
	if (fat32_dir_create(&f, "/screenshots") == FAT32_OK) fat32_close(&f);
	for (int n = 1; n < 1000; n++) {
		snprintf(path, sizeof path, "/screenshots/trs80-%03d.txt", n);
		if (fat32_open(&f, path) == FAT32_OK) { fat32_close(&f); continue; }
		if (fat32_create(&f, path) != FAT32_OK) break;
		const uint8_t *v = m1_vram(m);
		for (int r = 0; r < M1_ROWS; r++) {
			char l[M1_COLS + 3];
			int k = 0;
			for (int c = 0; c < M1_COLS; c++) {
				uint8_t ch = v[r * M1_COLS + c];
				l[k++] = ch & 0x80 ? '#' : (ch & 0x7F) < 0x20 ? (char)((ch & 0x7F) + 0x40) : (char)(ch & 0x7F);
			}
			while (k > 0 && l[k - 1] == ' ') k--;
			l[k++] = '\r', l[k++] = '\n';
			size_t put;
			fat32_write(&f, l, (size_t)k, &put);
		}
		fat32_close(&f);
		char b[64];
		snprintf(b, sizeof b, "Saved %s", path + 13);
		status(b);
		return;
	}
	status("Couldn't save the screen");
}

/* ------------------------------------------------------------ running */

static void reset(void)
{
	m1_init(m, rom);
	keys_init(&keys);
	shadow_valid = false;
}

int main(void)
{
	sb_init();
	lcd_init();
	lcd_set_font(&font_5x10);
	lcd_enable_cursor(false);
	lcd_set_background(C_BG);
	lcd_set_foreground(C_FG);
	lcd_clear_screen();
	fat32_init();
	keyboard_init();

	char problem[48];
	if (m1_size() > sizeof machine) no_rom("Internal: the machine doesn't fit");
	if (!load_rom(problem, sizeof problem)) no_rom(problem);
	reset();

	char b[80];
	snprintf(b, sizeof b, "TRS-80 Model I %s   %s, 48K", VERSION, rom_name);
	lcd_set_foreground(C_DIM);
	lcd_putstr(0, 0, b);
	lcd_set_foreground(C_FG);
	const char *help = "F1 reset  F2 leave  F5 save screen  Esc BREAK  Home CLEAR";
	status(help);

	absolute_time_t deadline = get_absolute_time();
	int note = 0;				/* fields until the status line shows the help again */
	for (;;) {
		/* the keyboard: everything waiting */
		for (int i = 0; i < 8; i++) {
			uint16_t k = sb_read_keyboard();
			if (!k) break;
			uint8_t st = k >> 8, code = k & 0xFF;
			if (st == KEY_STATE_PRESSED && code == KEY_F1) { reset(); continue; }
			if (st == KEY_STATE_PRESSED && code == KEY_F2) { lcd_clear_screen(); picocalc_shell_exit(); continue; }
			if (st == KEY_STATE_PRESSED && code == KEY_F5) { screenshot(); note = 180; continue; }
			if (st == KEY_STATE_PRESSED) keys_event(&keys, code, true);
			else if (st == KEY_STATE_RELEASED) keys_event(&keys, code, false);
		}
		keys_field(&keys);
		m1_set_matrix(m, keys.matrix);

		m1_run(m, FIELD_CYCLES);
		present();
		if (note && !--note) status(help);

		/* pace on an absolute deadline, so a late field doesn't add up */
		deadline = delayed_by_us(deadline, 16667);
		if (absolute_time_diff_us(get_absolute_time(), deadline) < -100000) deadline = get_absolute_time();
		busy_wait_until(deadline);
	}
}
