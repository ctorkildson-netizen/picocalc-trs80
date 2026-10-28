/* fdc.c -- see fdc.h
 *
 * Image formats:
 *   JV1: single-sided, single density, 10 sectors of 256 bytes a track,
 *        numbered 0-9, tracks one after another. No header. The directory
 *        track (17) reads with the 0xFA data mark, as TRSDOS wrote it.
 *   DMK: a 16-byte header (write protect, tracks, track length, flags), then
 *        each track as recorded: a table of 64 IDAM pointers (bit 15 double
 *        density, low 14 bits the offset of the 0xFE), then the bytes. A
 *        single-density byte is stored twice unless flag 0x40 says otherwise.
 */
#include <string.h>
#include "fdc.h"
#ifdef FDC_TRACE
#include <stdio.h>
#define TR(...) fprintf(stderr, __VA_ARGS__)
#else
#define TR(...) ((void)0)
#endif

#define CYCLES_PER_REV 354816u		/* 300 rpm at 1.774 MHz */
#define INDEX_CYCLES   7000u		/* the index hole, about 4 ms */
#define RTC_CYCLES     44352u		/* 40 Hz */
#define T1_CYCLES      2000u		/* a seek's settle, plus per step */
#define STEP_CYCLES    400u
#define FIRST_BYTE     300u		/* from a command to its first byte */
#define JV1_TRACK      2560u
#define SD_TRACK       3125u		/* bytes in a single-density track */

/* status bits */
#define S_NOTREADY 0x80
#define S_WP       0x40
#define S_HEAD     0x20
#define S_SEEKERR  0x10
#define S_RNF      0x10
#define S_CRC      0x08
#define S_TRACK0   0x04
#define S_LOST     0x04
#define S_INDEX    0x02
#define S_DRQ      0x02
#define S_BUSY     0x01

enum { PH_IDLE, PH_T1, PH_RD_WAIT, PH_RD, PH_WR_WAIT, PH_WR, PH_RNF, PH_FT_WAIT, PH_FT, PH_ADDR_WAIT };

static uint16_t crc16(uint16_t crc, uint8_t b)
{
	crc ^= (uint16_t)b << 8;
	for (int i = 0; i < 8; i++) crc = crc & 0x8000 ? (uint16_t)(crc << 1) ^ 0x1021 : (uint16_t)(crc << 1);
	return crc;
}

void fdc_init(fdc_t *f, const disk_io_t *io)
{
	memset(f, 0, sizeof *f);
	f->io = *io;
	f->sel = -1;
	f->t_drive = -1;
}

void fdc_reset(fdc_t *f, uint64_t now)
{
	f->status = 0; f->track = 0; f->sector = 1; f->data = 0; f->cmd = 0;
	f->step_dir = 1; f->intrq = f->drq = f->busy = false;
	f->phase = PH_IDLE;
	f->sel = -1; f->latch = 0;
	f->rtc = false;
	f->next_rtc = now + RTC_CYCLES;
	f->t_drive = -1;
}

bool fdc_insert(fdc_t *f, int n, uint32_t size, const uint8_t h[16], bool wp)
{
	if (n < 0 || n >= FDC_DRIVES) return false;
	drive_t *d = &f->drive[n];
	memset(d, 0, sizeof *d);
	if (f->t_drive == n) f->t_drive = -1;
	uint16_t tlen = (uint16_t)(h[2] | h[3] << 8);
	uint8_t sides = (h[4] & 0x10) ? 1 : 2;
	if ((h[0] == 0x00 || h[0] == 0xFF) && h[1] && h[1] <= 96 && tlen > 128 && tlen <= FDC_TRACK_MAX &&
	    16u + (uint32_t)h[1] * sides * tlen <= size + tlen) {
		d->kind = DISK_DMK;
		d->tracks = h[1]; d->sides = sides; d->tlen = tlen; d->dmk_flags = h[4];
		d->wp = wp || h[0] == 0xFF;
		return true;
	}
	if (size && size % JV1_TRACK == 0 && size / JV1_TRACK <= 96) {
		d->kind = DISK_JV1;
		d->tracks = (uint8_t)(size / JV1_TRACK); d->sides = 1;
		d->wp = wp;
		return true;
	}
	return false;
}

void fdc_eject(fdc_t *f, int n)
{
	if (n < 0 || n >= FDC_DRIVES) return;
	f->drive[n].kind = DISK_NONE;
	if (f->t_drive == n) f->t_drive = -1;
}

bool fdc_present(const fdc_t *f) { return f->drive[0].kind != DISK_NONE; }

static drive_t *cur(fdc_t *f) { return f->sel >= 0 && f->drive[f->sel].kind != DISK_NONE ? &f->drive[f->sel] : NULL; }
static int side_of(const fdc_t *f) { return (f->latch & 0x10) ? 1 : 0; }

/* the selected drive's current track into tbuf */
static bool load_track(fdc_t *f)
{
	drive_t *d = cur(f);
	if (!d) return false;
	int side = side_of(f);
	if (d->head >= d->tracks || side >= d->sides) return false;
	if (f->t_drive == f->sel && f->t_track == d->head && f->t_side == side) return true;
	bool ok;
	if (d->kind == DISK_JV1)
		ok = f->io.read(f->io.ctx, f->sel, (uint32_t)d->head * JV1_TRACK, f->tbuf, JV1_TRACK);
	else
		ok = f->io.read(f->io.ctx, f->sel, 16u + ((uint32_t)d->head * d->sides + side) * d->tlen, f->tbuf, d->tlen);
	if (!ok) { f->errors++; f->t_drive = -1; return false; }
	f->t_drive = f->sel; f->t_track = d->head; f->t_side = side;
	return true;
}

static bool save_range(fdc_t *f, uint32_t at, uint32_t len)	/* part of tbuf back to the image */
{
	drive_t *d = cur(f);
	uint32_t base = d->kind == DISK_JV1 ? (uint32_t)d->head * JV1_TRACK
	                                    : 16u + ((uint32_t)d->head * d->sides + side_of(f)) * d->tlen;
	if (!f->io.write(f->io.ctx, f->sel, base + at, f->tbuf + at, len)) { f->errors++; return false; }
	return true;
}

/* DMK: one byte of a sector at position p, s apart */
static inline uint8_t tb(const fdc_t *f, uint32_t p) { return p < FDC_TRACK_MAX ? f->tbuf[p] : 0; }

/* Find the sector the registers name on the current track. On success:
   for JV1, *at is its offset in tbuf; for DMK, the position of its first data
   byte, *step the spacing; *len its size and *dam its data mark. */
static bool find_sector(fdc_t *f, uint8_t want, uint32_t *at, uint8_t *step, uint16_t *len, uint8_t *dam)
{
	drive_t *d = cur(f);
	if (!d || !load_track(f)) return false;
	if (d->kind == DISK_JV1) {
		if (f->track != d->head || want >= 10) return false;
		*at = (uint32_t)want * 256; *step = 1; *len = 256;
		*dam = d->head == 17 ? 0xFA : 0xFB;
		return true;
	}
	for (int i = 0; i < 64; i++) {
		uint16_t ptr = (uint16_t)(f->tbuf[2 * i] | f->tbuf[2 * i + 1] << 8);
		if (!ptr) break;
		uint8_t s = (ptr & 0x8000) || (d->dmk_flags & 0x40) ? 1 : 2;
		uint32_t p = ptr & 0x3FFF;
		if (tb(f, p) != 0xFE) continue;
		if (tb(f, p + s) != f->track || tb(f, p + 3u * s) != want) continue;
		uint16_t size = (uint16_t)(128u << (tb(f, p + 4u * s) & 3));
		for (uint32_t q = p + 7u * s; q < p + 60u * s && q < d->tlen; q += s) {
			uint8_t m = tb(f, q);
			if (m >= 0xF8 && m <= 0xFB) {
				*at = q + s; *step = s; *len = size; *dam = m;
				return true;
			}
		}
		return false;		/* an ID with no data */
	}
	return false;
}

static void finish(fdc_t *f)
{
	f->busy = false; f->drq = false; f->intrq = true;
	f->phase = PH_IDLE;
}

static uint8_t rtype(uint8_t dam)	/* the 1771's record-type bits for a data mark */
{
	return (uint8_t)((0xFB - dam) << 5) & 0x60;
}

/* read sector: the next sector (for multiple), or not found */
static void start_read(fdc_t *f, uint64_t now)
{
	uint32_t at; uint8_t step, dam; uint16_t len;
	f->status = 0;
	if (!find_sector(f, f->sector, &at, &step, &len, &dam)) {
		f->phase = PH_RNF; f->done_at = now + CYCLES_PER_REV;	/* (it looks for a while) */
		return;
	}
	if (len > sizeof f->buf) len = sizeof f->buf;
	for (uint16_t i = 0; i < len; i++) f->buf[i] = tb(f, at + (uint32_t)i * step);
	f->pos = 0; f->len = len;
	f->status = rtype(dam);
	f->phase = PH_RD_WAIT; f->done_at = now + FIRST_BYTE;
}

static void start_write(fdc_t *f, uint64_t now)
{
	uint32_t at; uint8_t step, dam; uint16_t len;
	f->status = 0;
	if (cur(f)->wp) { f->status = S_WP; finish(f); return; }
	if (!find_sector(f, f->sector, &at, &step, &len, &dam)) {
		f->phase = PH_RNF; f->done_at = now + CYCLES_PER_REV;
		return;
	}
	if (len > sizeof f->buf) len = sizeof f->buf;
	f->wr_off = at; f->wr_step = step;
	f->wr_dam = (uint8_t)(0xFB - (f->cmd & 3));
	f->pos = 0; f->len = len;
	f->phase = PH_WR_WAIT; f->done_at = now + FIRST_BYTE;
}

static void commit_write(fdc_t *f)
{
	drive_t *d = cur(f);
	if (!d) { f->status |= S_NOTREADY; return; }
	if (d->kind == DISK_JV1) {
		memcpy(f->tbuf + f->wr_off, f->buf, f->len);
		if (!save_range(f, f->wr_off, f->len)) f->status |= S_CRC;	/* (a write that failed) */
		return;
	}
	uint32_t s = f->wr_step, p = f->wr_off;
	uint16_t crc = crc16(0xFFFF, f->wr_dam);
	for (uint32_t k = 0; k < s; k++) f->tbuf[p - s + k] = f->wr_dam;
	for (uint16_t i = 0; i < f->len; i++) {
		crc = crc16(crc, f->buf[i]);
		for (uint32_t k = 0; k < s; k++) f->tbuf[p + i * s + k] = f->buf[i];
	}
	uint32_t c = p + (uint32_t)f->len * s;
	for (uint32_t k = 0; k < s; k++) { f->tbuf[c + k] = (uint8_t)(crc >> 8); f->tbuf[c + s + k] = (uint8_t)crc; }
	if (!save_range(f, p - s, (uint32_t)(f->len + 3) * s)) f->status |= S_CRC;
}

/* write track: one byte from the CPU */
static void format_byte(fdc_t *f, uint8_t b)
{
	drive_t *d = cur(f);
	if (d->kind == DISK_JV1) {
		/* keep only the sectors: an ID (FE t s n z), then a data mark and 256 bytes */
		if (f->ft_data > 0) {
			int i = 256 - f->ft_data;
			f->buf[i] = b;
			if (--f->ft_data == 0 && f->ft_id[0] == d->head && f->ft_id[2] < 10) {
				memcpy(f->tbuf + f->ft_id[2] * 256, f->buf, 256);
				save_range(f, f->ft_id[2] * 256u, 256);
			}
		} else if (f->ft_idn > 0) {
			f->ft_id[4 - f->ft_idn] = b;
			f->ft_idn--;
		} else if (b == 0xFE) {
			f->ft_idn = 4;
		} else if (b >= 0xF8 && b <= 0xFB) {
			f->ft_data = 256;
		}
		f->ft_pos++;
		return;
	}
	/* DMK: record the track as written, single density, bytes twice */
	uint32_t s = (d->dmk_flags & 0x40) ? 1 : 2;
	uint32_t p = 128u + f->ft_pos * s;
	if (p + 2 * s > d->tlen) { f->ft_pos++; return; }
	static uint16_t crc;
	uint8_t out[2]; int n = 1;
	if (b == 0xF7) { out[0] = (uint8_t)(crc >> 8); out[1] = (uint8_t)crc; n = 2; }
	else {
		out[0] = b;
		if (b == 0xFE || (b >= 0xF8 && b <= 0xFB)) {
			crc = 0xFFFF;
			if (b == 0xFE && f->ft_ids < 64) {
				f->tbuf[2 * f->ft_ids] = (uint8_t)p; f->tbuf[2 * f->ft_ids + 1] = (uint8_t)(p >> 8);
				f->ft_ids++;
			}
		}
		crc = crc16(crc, b);
	}
	for (int i = 0; i < n; i++) {
		for (uint32_t k = 0; k < s; k++) if (p + k < d->tlen) f->tbuf[p + k] = out[i];
		p += s;
		f->ft_pos++;
	}
}

static void end_format(fdc_t *f)
{
	drive_t *d = cur(f);
	if (d && d->kind == DISK_DMK) save_range(f, 0, d->tlen);
}

static void command(fdc_t *f, uint8_t v, uint64_t now)
{
	if ((v & 0xF0) == 0xD0) {			/* force interrupt */
		if (f->phase == PH_FT) end_format(f);
		f->busy = false; f->drq = false; f->phase = PH_IDLE;
		f->cmd = v;
		f->status = 0;
		if (v & 0x0F) f->intrq = true;
		return;
	}
	if (f->busy) return;
	f->cmd = v;
	f->intrq = false; f->drq = false;
	drive_t *d = cur(f);
	if (v < 0x80) {					/* type I: restore, seek, step */
		int steps = 0;
		f->status = 0;
		if (v < 0x10) {
			steps = d ? d->head : 0;
			f->track = 0;
			if (d) d->head = 0;
		} else if (v < 0x20) {
			int diff = (int)f->data - (int)f->track;
			steps = diff < 0 ? -diff : diff;
			f->step_dir = diff < 0 ? -1 : 1;
			f->track = f->data;
			if (d) { int h = d->head + diff; d->head = (uint8_t)(h < 0 ? 0 : h > 95 ? 95 : h); }
		} else {
			if (v >= 0x40) f->step_dir = v < 0x60 ? 1 : -1;
			steps = 1;
			if (v & 0x10) f->track = (uint8_t)(f->track + f->step_dir);
			if (d) { int h = d->head + f->step_dir; d->head = (uint8_t)(h < 0 ? 0 : h > 95 ? 95 : h); }
		}
		if ((v & 0x04) && (!d || d->head >= d->tracks || (d->kind == DISK_JV1 && f->track != d->head)))
			f->status |= S_SEEKERR;		/* verify: no such track here */
		f->busy = true; f->phase = PH_T1;
		f->done_at = now + T1_CYCLES + (uint64_t)steps * STEP_CYCLES;
		return;
	}
	if (!d) { f->status = S_NOTREADY; f->intrq = true; return; }
	f->busy = true;
	if (v < 0xA0) start_read(f, now);
	else if (v < 0xC0) start_write(f, now);
	else if (v < 0xE0) {				/* read address: the next ID to pass under the head */
		f->status = 0;
		if (!load_track(f)) { f->phase = PH_RNF; f->done_at = now + CYCLES_PER_REV; return; }
		uint8_t id[6] = { d->head, (uint8_t)side_of(f), 0, 1, 0, 0 };
		if (d->kind == DISK_JV1) id[2] = (uint8_t)(f->rid++ % 10);
		else {
			int n = 0;
			while (n < 64 && (f->tbuf[2 * n] | f->tbuf[2 * n + 1])) n++;
			if (!n) { f->phase = PH_RNF; f->done_at = now + CYCLES_PER_REV; return; }
			uint16_t ptr = (uint16_t)(f->tbuf[2 * (f->rid % n)] | f->tbuf[2 * (f->rid % n) + 1] << 8);
			f->rid++;
			uint8_t s = (ptr & 0x8000) || (d->dmk_flags & 0x40) ? 1 : 2;
			for (int k = 0; k < 6; k++) id[k] = tb(f, (ptr & 0x3FFFu) + (uint32_t)(k + 1) * s);
		}
		memcpy(f->buf, id, 6);
		f->pos = 0; f->len = 6;
		f->sector = id[0];			/* (the 1771 puts the track there) */
		f->phase = PH_ADDR_WAIT; f->done_at = now + FIRST_BYTE;
	} else if (v < 0xF0) {				/* read track: not kept */
		f->status = 0;
		f->phase = PH_RNF; f->done_at = now + CYCLES_PER_REV;
	} else {					/* write track: formatting */
		f->status = 0;
		if (d->wp) { f->status = S_WP; finish(f); return; }
		if (!load_track(f) && d->head < d->tracks) { f->status = S_NOTREADY; finish(f); return; }
		if (d->kind == DISK_DMK) { memset(f->tbuf, 0, d->tlen); f->ft_ids = 0; }
		f->ft_pos = 0; f->ft_idn = 0; f->ft_data = 0;
		f->phase = PH_FT_WAIT; f->done_at = now + FIRST_BYTE;
	}
}

static void update(fdc_t *f, uint64_t now)
{
	while (now >= f->next_rtc) { f->rtc = true; f->next_rtc += RTC_CYCLES; }
	if (!f->busy || now < f->done_at) return;
	switch (f->phase) {
	case PH_T1: finish(f); break;
	case PH_RD_WAIT: f->phase = PH_RD; f->drq = true; break;
	case PH_ADDR_WAIT: f->phase = PH_RD; f->drq = true; break;
	case PH_WR_WAIT: f->phase = PH_WR; f->drq = true; break;
	case PH_FT_WAIT: f->phase = PH_FT; f->drq = true; break;
	case PH_RNF: f->status |= S_RNF; finish(f); break;
	default: break;
	}
}

static uint8_t status_now(fdc_t *f, uint64_t now)
{
	drive_t *d = cur(f);
	uint8_t s = f->status;
	bool type1 = f->cmd < 0x80 || (f->cmd & 0xF0) == 0xD0;
	if (!d) s |= S_NOTREADY;
	if (type1) {
		s &= (uint8_t)~(S_TRACK0 | S_INDEX | S_WP | S_HEAD);
		if (d) {
			if (d->wp) s |= S_WP;
			s |= S_HEAD;
			if (d->head == 0) s |= S_TRACK0;
			if (now % CYCLES_PER_REV < INDEX_CYCLES) s |= S_INDEX;
		}
	} else {
		s &= (uint8_t)~S_DRQ;
		if (f->drq) s |= S_DRQ;
	}
	if (f->busy) s |= S_BUSY;
	return s;
}

uint8_t fdc_read(fdc_t *f, uint16_t a, uint64_t now)
{
	update(f, now);
	switch (a) {
	case 0x37E0: case 0x37E1: case 0x37E2: case 0x37E3: {	/* interrupt latch: reading clears the clock's */
		uint8_t v = (uint8_t)((f->rtc ? 0x80 : 0) | (f->intrq ? 0x40 : 0));
		f->rtc = false;
		return v;
	}
	case 0x37E8: case 0x37E9: case 0x37EA: case 0x37EB:
		return 0x30;					/* printer: ready, nothing to report */
	case 0x37EC: {
		uint8_t s = status_now(f, now);
		{ static uint8_t last = 0xAA; static unsigned rep; if (s != last || ++rep % 20000 == 0) TR("%10llu status %02X\n", (unsigned long long)now, s); last = s; }
		f->intrq = false;
		return s;
	}
	case 0x37ED: return f->track;
	case 0x37EE: return f->sector;
	case 0x37EF:
		if (f->phase == PH_RD && f->drq) {
			f->data = f->buf[f->pos++];
			if (f->pos >= f->len) {
				f->drq = false;
				if ((f->cmd & 0xE0) == 0x80 && (f->cmd & 0x10)) {	/* multiple: on to the next */
					f->sector++;
					start_read(f, now);
				} else finish(f);
			}
		}
		return f->data;
	}
	return 0xFF;
}

void fdc_write(fdc_t *f, uint16_t a, uint8_t v, uint64_t now)
{
	update(f, now);
	switch (a) {
	case 0x37E0: case 0x37E1: case 0x37E2: case 0x37E3:	/* drive select */
		if (v != f->latch) TR("%10llu select %02X\n", (unsigned long long)now, v);
		f->latch = v;
		f->sel = (v & 1) ? 0 : (v & 2) ? 1 : (v & 4) ? 2 : (v & 8) ? 3 : -1;
		break;
	case 0x37EC: TR("%10llu cmd %02X trk %d sec %d data %d sel %d head %d\n", (unsigned long long)now, v, f->track, f->sector, f->data, f->sel, cur(f) ? cur(f)->head : -1); command(f, v, now); break;
	case 0x37ED: f->track = v; break;
	case 0x37EE: f->sector = v; break;
	case 0x37EF:
		f->data = v;
		if (f->phase == PH_WR && f->drq) {
			f->buf[f->pos++] = v;
			if (f->pos >= f->len) { commit_write(f); finish(f); }
		} else if (f->phase == PH_FT && f->drq) {
			if (!cur(f)) { finish(f); break; }
			format_byte(f, v);
			uint32_t limit = cur(f)->kind == DISK_JV1 ? SD_TRACK
			               : (uint32_t)(cur(f)->tlen - 128) / ((cur(f)->dmk_flags & 0x40) ? 1u : 2u);
			if (f->ft_pos >= limit) { end_format(f); finish(f); }
		}
		break;
	}
}

bool fdc_int(fdc_t *f, uint64_t now)
{
	update(f, now);
	return f->rtc || f->intrq;
}

uint64_t fdc_next_event(const fdc_t *f)
{
	uint64_t n = f->next_rtc;
	if (f->busy && f->done_at < n) n = f->done_at;
	return n;
}
