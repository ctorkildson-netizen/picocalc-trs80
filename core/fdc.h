/* fdc.h -- the Model I's Expansion Interface: the 40 Hz clock interrupt, the
 * drive-select latch, and a Western Digital FD1771 floppy controller with up
 * to four drives, holding JV1 or DMK disk images.
 *
 * The images stay on the card: the controller asks the host for a track at a
 * time (and writes back the sectors it changes) through disk_io_t, so card
 * time is host time and invisible to the guest. Every delay is counted in
 * guest cycles. Portable C: no SDK headers, no allocation. */
#ifndef FDC_H
#define FDC_H
#include <stdint.h>
#include <stdbool.h>

#define FDC_DRIVES 4
#define FDC_TRACK_MAX 0x2940		/* the longest DMK track (with its 128-byte table) */

typedef struct {
	/* read or write len bytes at off in drive's image; false on failure */
	bool (*read)(void *ctx, int drive, uint32_t off, void *buf, uint32_t len);
	bool (*write)(void *ctx, int drive, uint32_t off, const void *buf, uint32_t len);
	void *ctx;
} disk_io_t;

enum { DISK_NONE, DISK_JV1, DISK_DMK };

typedef struct {
	uint8_t kind;			/* DISK_ */
	bool wp;			/* write-protected */
	uint8_t tracks, sides;
	uint16_t tlen;			/* DMK: bytes per track, its table included */
	uint8_t dmk_flags;
	uint8_t head;			/* where the head is (physical track) */
} drive_t;

typedef struct {
	disk_io_t io;
	drive_t drive[FDC_DRIVES];
	int sel;			/* selected drive, -1 for none */
	uint8_t latch;			/* 37E1 as last written */
	/* the FD1771's registers */
	uint8_t status, track, sector, data, cmd;
	int8_t step_dir;
	bool intrq, drq, busy;
	uint64_t done_at;		/* cycle at which the command in progress ends or hands over its first byte */
	/* a sector or ID being transferred */
	uint8_t buf[1024 + 16];
	uint16_t pos, len;
	uint8_t phase;			/* what the command is doing */
	uint32_t wr_off;		/* JV1: where a sector being written goes */
	uint16_t wr_tpos;		/* DMK: where in the track */
	uint8_t wr_step, wr_dam;
	/* write track (formatting) */
	uint16_t ft_pos;
	uint8_t ft_id[4];
	int ft_idn, ft_data;
	uint8_t ft_ids;			/* DMK: IDAMs recorded */
	/* the track in memory */
	uint8_t tbuf[FDC_TRACK_MAX];
	int t_drive, t_track, t_side;	/* -1 when empty */
	uint8_t rid;			/* read address: the next ID to hand out */
	/* the interrupt latch, 37E0 */
	bool rtc;
	uint64_t next_rtc;
	uint32_t errors;		/* image reads or writes that failed */
} fdc_t;

void fdc_init(fdc_t *f, const disk_io_t *io);
void fdc_reset(fdc_t *f, uint64_t now);		/* as at power-on; the disks stay in */
/* a disk into a drive: size is the image's length, wp its write protection.
   Returns false (and leaves the drive empty) for an image it doesn't know. */
bool fdc_insert(fdc_t *f, int drive, uint32_t size, const uint8_t header[16], bool wp);
void fdc_eject(fdc_t *f, int drive);
bool fdc_present(const fdc_t *f);			/* a disk in drive 0 */

uint8_t fdc_read(fdc_t *f, uint16_t addr, uint64_t now);	/* 37E0-37EF */
void fdc_write(fdc_t *f, uint16_t addr, uint8_t v, uint64_t now);
bool fdc_int(fdc_t *f, uint64_t now);		/* the Z80's INT line */
uint64_t fdc_next_event(const fdc_t *f);	/* the next cycle at which something changes */

#endif
