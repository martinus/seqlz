#ifndef CR_DECODE_H
#define CR_DECODE_H

#include <stddef.h>

/* Decodes one seqlz block of 4096 bytes. Returns 0 if src[0..len) is a valid
 * compressed block (out then holds the 4096 bytes), -1 otherwise. */
int cr_decode(const unsigned char *src, size_t len, unsigned char *out);

#endif
