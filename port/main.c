/* trs80 -- a TRS-80 Model I on the PicoCalc: Level II BASIC, the keyboard,
 * and the Expansion Interface's disk drives, with JV1 and DMK images on the
 * SD card. One core for now: the guest runs a 60 Hz field, the rows of the
 * screen that changed are drawn, the keyboard is read, and the loop waits for
 * the field's deadline. */
#include <stdio.h>
#include <string.h>
#include <time.h>
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
#include "picker.h"
#include "dosclock.h"

#define VERSION "0.6"
#define FIELD_CYCLES (M1_CLOCK_HZ / 60)
#define CELL_W 5
#define CELL_H 18
#define TOP 16				/* the screen's band above the Model I's */
#define GLYPH_TOP 4			/* the 10-row glyph's place in an 18-row cell */
#define C_FG 0xFFFF
#define C_BG 0x0000
#define C_DIM 0x8410

static uint8_t machine[80 * 1024] __attribute__((aligned(8)));
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
	snprintf(b, sizeof b, "%-64.64s", s);
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

/* ------------------------------------------------------------ the disks */

#define DRIVES 2			/* drives the menu offers (the controller has 4) */
#define DRIVES_FILE "/trs80/drives.txt"
static fat32_file_t dfile[FDC_DRIVES];
static char dpath[FDC_DRIVES][PICKER_PATH];

static bool d_read(void *c, int d, uint32_t off, void *buf, uint32_t len)
{
	(void)c;
	size_t got = 0;
	return dfile[d].is_open && fat32_seek(&dfile[d], off) == FAT32_OK &&
	       fat32_read(&dfile[d], buf, len, &got) == FAT32_OK && got == len;
}

static bool d_write(void *c, int d, uint32_t off, const void *buf, uint32_t len)
{
	(void)c;
	size_t put = 0;
	return dfile[d].is_open && fat32_seek(&dfile[d], off) == FAT32_OK &&
	       fat32_write(&dfile[d], buf, len, &put) == FAT32_OK && put == len;
}
static const disk_io_t disk_io = { d_read, d_write, NULL };

static void eject(int d)
{
	fdc_eject(m1_fdc(m), d);
	if (dfile[d].is_open) fat32_close(&dfile[d]);
	dpath[d][0] = 0;
}

static bool insert(int d, const char *path)
{
	eject(d);
	if (fat32_open(&dfile[d], path) != FAT32_OK) return false;
	uint8_t h[16] = { 0 };
	size_t got = 0;
	fat32_read(&dfile[d], h, 16, &got);
	bool wp = (dfile[d].attributes & 0x01) != 0;		/* a read-only file is a protected disk */
	if (!fdc_insert(m1_fdc(m), d, fat32_size(&dfile[d]), h, wp)) { fat32_close(&dfile[d]); return false; }
	snprintf(dpath[d], sizeof dpath[d], "%s", path);
	return true;
}

/* a game's keys: /trs80/layouts/NAME.txt (see keys.h) */
static char lpath[PICKER_PATH];
static char lproblem[96];

static const char *base_name(const char *p);
static bool use_layout(const char *path)
{
	static char text[2048];
	static key_bind_t b[KEYS_LAYOUT];
	lproblem[0] = 0;
	fat32_file_t f;
	if (fat32_open(&f, path) != FAT32_OK) { snprintf(lproblem, sizeof lproblem, "Can't open %.40s", path); return false; }
	size_t n = 0;
	fat32_read(&f, text, sizeof text - 1, &n);
	fat32_close(&f);
	text[n] = 0;
	int r = keys_parse_layout(text, b, KEYS_LAYOUT);
	if (r < 0) { snprintf(lproblem, sizeof lproblem, "%.30s: line %d is wrong, not used", base_name(path), -r); return false; }
	keys_set_layout(&keys, b, r);
	snprintf(lpath, sizeof lpath, "%s", path);
	return true;
}

static void no_layout(void) { keys_set_layout(&keys, NULL, 0); lpath[0] = 0; }

/* /trs80/drives.txt: a line per drive, "0 /trs80/disks/newdos.dsk", and
   "L /trs80/layouts/meteor.txt" for the keys */
static void load_drives(void)
{
	fat32_file_t f;
	if (fat32_open(&f, DRIVES_FILE) != FAT32_OK) return;
	char b[1024];
	size_t n = 0;
	fat32_read(&f, b, sizeof b - 1, &n);
	fat32_close(&f);
	b[n] = 0;
	for (char *l = strtok(b, "\r\n"); l; l = strtok(NULL, "\r\n"))
		if (l[0] >= '0' && l[0] < '0' + DRIVES && l[1] == ' ') insert(l[0] - '0', l + 2);
		else if (l[0] == 'L' && l[1] == ' ') use_layout(l + 2);
}

static void save_drives(void)
{
	char b[1024];
	int n = 0;
	for (int d = 0; d < DRIVES; d++)
		if (dpath[d][0]) n += snprintf(b + n, sizeof b - (size_t)n, "%d %s\r\n", d, dpath[d]);
	if (lpath[0]) n += snprintf(b + n, sizeof b - (size_t)n, "L %s\r\n", lpath);
	fat32_delete(DRIVES_FILE);
	fat32_file_t f;
	if (fat32_create(&f, DRIVES_FILE) != FAT32_OK) return;
	size_t put;
	fat32_write(&f, b, (size_t)n, &put);
	fat32_close(&f);
}

static bool is_disk(const char *name)
{
	const char *e = strrchr(name, '.');
	if (!e) return false;
	char x[5] = { 0 };
	for (int i = 0; i < 4 && e[i + 1]; i++) x[i] = (char)(e[i + 1] | 0x20);
	return !strcmp(x, "dsk") || !strcmp(x, "dmk") || !strcmp(x, "jv1");
}

static bool is_text(const char *name)
{
	const char *e = strrchr(name, '.');
	return e && (e[1] | 0x20) == 't' && (e[2] | 0x20) == 'x' && (e[3] | 0x20) == 't' && !e[4];
}

static const char *base_name(const char *p) { const char *s = strrchr(p, '/'); return s ? s + 1 : p; }

/* ------------------------------------------------------------ running */

volatile bool user_interrupt;		/* (the keyboard driver sets it on Brk; not used here) */
static int ram_kb = 48;
static char title[80];

static void redraw(void)
{
	lcd_clear_screen();
	lcd_set_foreground(C_DIM);
	lcd_putstr(0, 0, title);
	lcd_set_foreground(C_FG);
	shadow_valid = false;
}

static void set_title(void)
{
	snprintf(title, sizeof title, "Model I %s %dK 0:%.16s 1:%.16s keys:%.10s", VERSION, ram_kb,
	         dpath[0][0] ? base_name(dpath[0]) : "-", dpath[1][0] ? base_name(dpath[1]) : "-",
	         lpath[0] ? base_name(lpath) : "usual");
	lcd_set_foreground(C_DIM);
	char b[65];
	snprintf(b, sizeof b, "%-64.64s", title);
	lcd_putstr(0, 0, b);
	lcd_set_foreground(C_FG);
}

static dosclock_t dosclock;

/* the shell's local time, or false */
static bool now_local(struct tm *tm)
{
#ifdef PICOCALC_SHELL_HAS_TIME
	int64_t ms;
	int offset;
	if (!picocalc_shell_time(&ms, &offset)) return false;
	time_t t = (time_t)(ms / 1000) + (time_t)offset * 60;
	gmtime_r(&t, tm);
	return tm->tm_year + 1900 >= 1980;
#else
	(void)tm;
	return false;
#endif
}

static void power_on(void)
{
	dosclock_reset(&dosclock);
	m1_power_on(m, ram_kb);
	keys_init(&keys);
	shadow_valid = false;
}

/* the picker, over the Model I's screen; puts everything back after */
static int pick(const picker_style_t *style, char *dir, const char *start, char *path)
{
	keyboard_set_background_poll(true);
	fat32_file_t t;
	if (fat32_open(&t, dir) == FAT32_OK) fat32_close(&t);
	else strcpy(dir, "/");
	int r = picker_run(style, dir, start, path);
	keyboard_set_background_poll(false);
	while (keyboard_key_available()) keyboard_get_key();
	lcd_set_font(&font_5x10);
	lcd_set_foreground(C_FG);
	lcd_set_background(C_BG);
	return r;
}

/* F3, F4: choose the disk for drive 0 or 1 (Esc: leave it; Del: empty it) */
static void choose(int d)
{
	static const char *const help[] = { "  Del            empty the drive", NULL };
	static const picker_style_t style = {
		.title = "Choose a disk", .filter = is_disk, .noun = "disk", .nouns = "disks",
		.keys = "\x7F\xD4", .keys_help = help,
	};
	static char dir[PICKER_PATH] = "/trs80/disks";
	char path[PICKER_PATH];
	int r = pick(&style, dir, dpath[d][0] ? base_name(dpath[d]) : NULL, path);
	if (r == PICKER_CHOSEN) {
		if (!insert(d, path)) eject(d);
		save_drives();
	} else if (r == 0x7F || r == 0xD4) {
		eject(d);
		save_drives();
	}
	redraw();
	set_title();
}

/* F6 (Shift-F1): choose a game's keys (Del: the usual keys) */
static void choose_layout(void)
{
	static const char *const help[] = { "  Del            the usual keys", NULL };
	static const picker_style_t style = {
		.title = "Keys for a game", .filter = is_text, .noun = "layout", .nouns = "layouts",
		.keys = "\x7F\xD4", .keys_help = help,
	};
	static char dir[PICKER_PATH] = "/trs80/layouts";
	char path[PICKER_PATH];
	int r = pick(&style, dir, lpath[0] ? base_name(lpath) : NULL, path);
	if (r == PICKER_CHOSEN) { if (use_layout(path)) save_drives(); }
	else if (r == 0x7F || r == 0xD4) { no_layout(); save_drives(); }
	redraw();
	set_title();
}

/* the shell's clock, for dating the files trs80 writes (screenshots, the
   drives file, and the disk images as the Model I writes to them) */
static bool shell_clock(uint16_t *date, uint16_t *time)
{
#ifdef PICOCALC_SHELL_HAS_TIME
	int64_t ms;
	int offset;
	if (!picocalc_shell_time(&ms, &offset)) return false;
	time_t t = (time_t)(ms / 1000) + (time_t)offset * 60;
	struct tm tm;
	gmtime_r(&t, &tm);
	if (tm.tm_year + 1900 < 1980 || tm.tm_year + 1900 > 2107) return false;
	fat32_pack_time(tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, date, time);
	return true;
#else
	(void)date;
	(void)time;
	return false;
#endif
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
	fat32_set_time_source(shell_clock);
	keyboard_init();

	char problem[48];
	if (m1_size() > sizeof machine) no_rom("Internal: the machine doesn't fit");
	if (!load_rom(problem, sizeof problem)) no_rom(problem);
	m1_init(m, rom, &disk_io);
	load_drives();
	power_on();
	set_title();
	const char *help = "F1 power on  F2 leave  F3 F4 disks  F5 save screen  F6 keys";
	status(lproblem[0] ? lproblem : help);
	int note = lproblem[0] ? 600 : 0;	/* fields until the status line shows the help again */

	absolute_time_t deadline = get_absolute_time();
	for (;;) {
		/* the keyboard: everything waiting */
		for (int i = 0; i < 8; i++) {
			uint16_t k = sb_read_keyboard();
			if (!k) break;
			uint8_t st = k >> 8, code = k & 0xFF;
			if (st == KEY_STATE_PRESSED && code == KEY_F1) { power_on(); continue; }
			if (st == KEY_STATE_PRESSED && code == KEY_F2) { lcd_clear_screen(); picocalc_shell_exit(); continue; }
			if (st == KEY_STATE_PRESSED && (code == KEY_F3 || code == KEY_F4)) {
				choose(code == KEY_F3 ? 0 : 1);
				status(code == KEY_F3 ? "Drive 0 changed: F1 to start again from it" : help);
				note = code == KEY_F3 ? 300 : 0;
				deadline = get_absolute_time();
				continue;
			}
			if (st == KEY_STATE_PRESSED && code == KEY_F5) { screenshot(); note = 180; continue; }
			if (st == KEY_STATE_PRESSED && code == KEY_F6) {
				choose_layout();
				status(lproblem[0] ? lproblem : help);
				note = lproblem[0] ? 600 : 0;
				deadline = get_absolute_time();
				continue;
			}
			if (st == KEY_STATE_PRESSED) keys_event(&keys, code, true);
			else if (st == KEY_STATE_RELEASED) keys_event(&keys, code, false);
		}
		keys_field(&keys);
		m1_set_matrix(m, keys.matrix);

		m1_run(m, FIELD_CYCLES);
		if (!dosclock.done) {
			struct tm tm;
			if (!now_local(&tm)) dosclock.done = true;
			else dosclock_field(&dosclock, m, tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
		}
		present();
		if (note && !--note) status(help);

		/* pace on an absolute deadline, so a late field doesn't add up */
		deadline = delayed_by_us(deadline, 16667);
		if (absolute_time_diff_us(get_absolute_time(), deadline) < -100000) deadline = get_absolute_time();
		busy_wait_until(deadline);
	}
}
