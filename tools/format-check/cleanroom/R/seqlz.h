#ifndef SEQLZ_H
#define SEQLZ_H

#include <stddef.h>

/*
 * Decode one seqlz compressed block.
 *
 * src[0..len) is the compressed block, out must have room for 4096 bytes.
 * Returns 0 if the block is valid; out then holds the 4096 decoded bytes.
 * Returns -1 if the block is not valid; out is then undefined.
 *
 * Returns for every input of any length; never reads outside src[0..len)
 * nor writes outside out[0..4096).
 */
int cr_decode(const unsigned char *src, size_t len, unsigned char *out);

#endif /* SEQLZ_H */
