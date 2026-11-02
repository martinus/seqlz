#ifndef CR_H
#define CR_H
#include <stddef.h>

/* Writes one valid seqlz compressed block (format.md) for 4 KiB blocks.
 *
 * Reads choices[0..n) in order, 0 after the end; each byte read decides one choice the format leaves
 * open. Writes the compressed block to dst and the 4096 bytes it decodes to into block, and returns the
 * compressed length, or 0 if cap is too small (dst is then not written).
 *
 * cap: every block this writer makes is at most CR_MAX_BLOCK = 5407 bytes, so cap >= 5407 always
 * suffices (FEATURES.md, "How large cap has to be"): with coded literals at most 16 bytes of header
 * (3 + e + w with w = 12), 256 bytes of a block's own table, and 5135 bytes of literal streams and
 * B: at most 41017 bits of literal codes and sequences, found by a search over every split of the
 * block into literals and matches with 10-bit literal codes and the longest code of each field, plus
 * at most 7 bits of padding in each of the 9 parts. Raw literals need at most 4142 bytes. */
#define CR_MAX_BLOCK 5407

size_t cr_write(const unsigned char *choices, size_t n, unsigned char *dst, size_t cap,
                unsigned char *block);

/* Feature counters for cr-gen: one counter per item of FEATURES.md. */
int cr_feature_count(void);
const char *cr_feature_name(int i);
typedef struct cr_stats { unsigned long long *count; } cr_stats;
size_t cr_write_stats(const unsigned char *choices, size_t n, unsigned char *dst, size_t cap,
                      unsigned char *block, cr_stats *stats);

#endif
