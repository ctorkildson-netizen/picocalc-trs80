/* keys.h -- the PicoCalc's keyboard on the Model I's key matrix.
 *
 * The PicoCalc sends finished characters (Shift already applied: '"' arrives
 * as '"'), as presses and releases. The Model I's ROM reads an 8x8 matrix of
 * switches. So each PicoCalc code is looked up in a table (keys.c) giving the
 * matrix cell and whether the Model I's SHIFT must be down, up, or left to the
 * PicoCalc's own Shift; the binding is fixed when the key goes down, so its
 * release undoes exactly what its press did.
 *
 * Events are replayed at the guest's pace: after a press nothing else happens
 * for KEYS_HOLD fields, after a release for KEYS_GAP fields, so a key tapped
 * between two polls is still seen by the ROM. Portable C: no SDK headers. */
#ifndef KEYS_H
#define KEYS_H
#include <stdint.h>
#include <stdbool.h>

#define KEYS_HOLD 3		/* fields a key stays down at least (60 Hz) */
#define KEYS_GAP  2		/* fields between one key's release and the next event */
#define KEYS_QUEUE 32
#define KEYS_HELD 8

/* a PicoCalc code's place on the Model I keyboard */
enum { SH_ANY = 0, SH_ON = 1, SH_OFF = 2 };
typedef struct { uint8_t code, row, col, shift; } key_bind_t;
const key_bind_t *keys_lookup(uint8_t code);	/* NULL: not a Model I key */

typedef struct {
	struct { uint8_t code; bool down; } q[KEYS_QUEUE];
	uint8_t head, tail;
	uint8_t wait;			/* fields before the next queued event */
	struct { uint8_t code, row, col, shift; } held[KEYS_HELD];
	uint8_t n_held;
	bool pc_shift;			/* the PicoCalc's Shift, held */
	uint32_t dropped;		/* events that didn't fit the queue */
	uint8_t matrix[8];		/* the result: row bits, a set bit is a key down */
} keys_t;

void keys_init(keys_t *k);
void keys_event(keys_t *k, uint8_t code, bool down);	/* from the PicoCalc */
void keys_field(keys_t *k);	/* once per field: apply what's due, rebuild matrix[] */

#endif
