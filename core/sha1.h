/* sha1.h -- SHA-1 of a buffer, to tell ROM files apart. Portable C. */
#ifndef SHA1_H
#define SHA1_H
#include <stdint.h>
#include <stddef.h>
void sha1(const void *data, size_t len, uint8_t out[20]);
void sha1_hex(const void *data, size_t len, char out[41]);
#endif
