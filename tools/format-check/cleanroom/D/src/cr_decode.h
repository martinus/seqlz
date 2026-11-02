#ifndef CR_DECODE_H
#define CR_DECODE_H

#include <stddef.h>

#define CR_BLOCK 4096

/* Decodes one seqlz block of 4 KiB (format.md).
 * Returns 0 if src[0..len) is a valid compressed block; out then holds the CR_BLOCK bytes.
 * Returns -1 otherwise; out is then undefined.
 * Never reads outside src[0..len) nor writes outside out[0..CR_BLOCK).
 * Builds its fixed decode tables on the first call (not thread-safe on that first call;
 * call it once before using it from several threads). */
int cr_decode(const unsigned char *src, size_t len, unsigned char *out);

#endif
