/* cr: a writer of seqlz compressed blocks (format.md), driven by choice bytes. C11. */
#ifndef CR_H
#define CR_H
#include <stddef.h>

/* The largest compressed block cr_write() writes. A cap of CR_MAX bytes always suffices; a smaller
 * cap works for the blocks that fit it, and cr_write() returns 0 for the others.
 * Bound: coded layout header 3 + 1 + 12, own table description at most 736 bytes (256 codes of at
 * most 8 bits of OWN, 256 runs of at most 8 + 7 bits), literal streams and B together at most
 * 10.37 bits per block byte (a sequence of 15 literals of 10 bits, an escaped token of 16 bits,
 * 12 offset bits and an LL value of at most 19 bits for 19 bytes), 5310 bytes, plus 9 bytes of
 * padding: 6071 < 6144. The raw layout is at most 2 + 4096 + 1 bytes. */
#define CR_MAX 6144
#define CR_BLOCK 4096

size_t cr_write(const unsigned char *choices, size_t n, unsigned char *dst, size_t cap,
                unsigned char *block);

/* Features of FEATURES.md, counted by cr_write_stats(). */
enum {
#define CR_FAM(id, name, count, items, section) CRF_##id, CRF_##id##_END_ = CRF_##id + (count) - 1,
#include "cr_features.def"
#undef CR_FAM
    CRF_COUNT
};

struct cr_family { const char *name; int base, count; const char *items, *section; };
extern const struct cr_family cr_families[];
extern const int cr_nfamilies;
/* Name of item i of a family, e.g. "class=3"; written to buf. */
const char *cr_item_name(int family, int i, char *buf, size_t size);

/* The families from TOKV on are large and printed as summaries. */
#define CR_FIRST_LARGE CRF_TOKV

/* cr_write(), adding the number of times each feature occurs to counts[CRF_COUNT] (may be NULL). */
size_t cr_write_stats(const unsigned char *choices, size_t n, unsigned char *dst, size_t cap,
                      unsigned char *block, unsigned long *counts);
#endif
