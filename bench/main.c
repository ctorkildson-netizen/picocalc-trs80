/* bench -- the two Z80 cores on the PicoCalc: boot Level II BASIC with each
 * and time 10 seconds of guest time. A test, not the emulator. */
#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "drivers/lcd.h"
#include "drivers/font.h"
#include "drivers/southbridge.h"
#include "drivers/keyboard.h"
#include "drivers/fat32.h"
#include "picocalc_shell.h"
#include "m1.h"
#include "rom_l2.h"

/* the two builds of the machine, one per core (see CMakeLists.txt) */
void m1sz_init(void *m, const uint8_t *rom);  uint32_t m1sz_run(void *m, uint32_t c);
const uint8_t *m1sz_vram(const void *m);      uint64_t m1sz_instructions(const void *m);
void m1ch_init(void *m, const uint8_t *rom);  uint32_t m1ch_run(void *m, uint32_t c);
const uint8_t *m1ch_vram(const void *m);      uint64_t m1ch_instructions(const void *m);
size_t m1sz_size(void); size_t m1ch_size(void);

static uint8_t machine[80 * 1024] __attribute__((aligned(8)));
static char out[32][65];
static int nout;
static void say(const char *s) { if (nout < 32) snprintf(out[nout++], 65, "%s", s); }

static void screen_text(const uint8_t *v, int rows)
{
	for (int r = 0; r < rows; r++) {
		char l[65];
		for (int c = 0; c < 64; c++) { uint8_t ch = v[r * 64 + c] & 0x7F; l[c] = ch < 0x20 ? ch + 0x40 : ch; if (v[r*64+c] & 0x80) l[c] = '#'; }
		l[64] = 0;
		say(l);
	}
}

static void bench(const char *name, void (*init)(void *, const uint8_t *), uint32_t (*run)(void *, uint32_t),
                  const uint8_t *(*vram)(const void *), uint64_t (*ins)(const void *))
{
	char b[80];
	init(machine, rom_l2);
	const uint64_t want = 10ull * M1_CLOCK_HZ;
	uint64_t done = 0, t0 = time_us_64();
	while (done < want) done += run(machine, 29568);
	uint64_t us = time_us_64() - t0;
	double ratio = (done / (double)M1_CLOCK_HZ) / (us / 1e6);
	snprintf(b, sizeof b, "%s: %.2f x real time, %.0f%% of a core", name, ratio, 100.0 / ratio); say(b);
	snprintf(b, sizeof b, "  %llu instr in %.2f s: %.0f host cycles/instr", (unsigned long long)ins(machine), us / 1e6,
	         (double)clock_get_hz(clk_sys) * (us / 1e6) / (double)ins(machine)); say(b);
	screen_text(vram(machine), 2);
}

int main(void)
{
	sb_init();
	lcd_init();
	lcd_set_font(&font_5x10);
	lcd_enable_cursor(false);
	lcd_clear_screen();
	fat32_init();
	keyboard_init();
	char b[80];
	snprintf(b, sizeof b, "Z80 bench: Level II BASIC, clk_sys %lu MHz", (unsigned long)(clock_get_hz(clk_sys) / 1000000)); say(b);
	snprintf(b, sizeof b, "machine %u / %u bytes", (unsigned)m1sz_size(), (unsigned)m1ch_size()); say(b);
	say("");
	for (int r = 0; r < nout; r++) lcd_putstr(0, (uint8_t)r, out[r]);
	bench("superzazu/z80", m1sz_init, m1sz_run, m1sz_vram, m1sz_instructions);
	say("");
	for (int r = 0; r < nout; r++) lcd_putstr(0, (uint8_t)r, out[r]);
	bench("floooh/chips z80", m1ch_init, m1ch_run, m1ch_vram, m1ch_instructions);
	say(""); say("F5: save this as /screenshots/z80bench.txt   Esc: leave");
	for (int r = 0; r < nout; r++) lcd_putstr(0, (uint8_t)r, out[r]);
	for (;;) {
		uint16_t k = sb_read_keyboard();
		if ((k >> 8) == KEY_STATE_PRESSED) {
			if ((k & 0xFF) == KEY_ESC) { lcd_clear_screen(); picocalc_shell_exit(); }
			if ((k & 0xFF) == KEY_F5) {
				fat32_file_t f;
				size_t put;
				if (fat32_dir_create(&f, "/screenshots") == FAT32_OK) fat32_close(&f);
				fat32_delete("/screenshots/z80bench.txt");
				bool ok = fat32_create(&f, "/screenshots/z80bench.txt") == FAT32_OK;
				for (int r = 0; ok && r < nout; r++) {
					char l[70];
					int n = snprintf(l, sizeof l, "%s\r\n", out[r]);
					ok = fat32_write(&f, l, (size_t)n, &put) == FAT32_OK;
				}
				if (ok) fat32_close(&f);
				lcd_putstr(0, 31, ok ? "Saved /screenshots/z80bench.txt          " : "Couldn't save                             ");
			}
		}
		sleep_ms(10);
	}
}
