/* Test-only encoder helpers, built from the same code-length tables, to make
 * blocks by hand from the rules in seqlz.rst. Not part of the decoder. */
#ifndef ENC_H
#define ENC_H
#include <stdint.h>
#include <string.h>
#include <stddef.h>
#include "seqlz_tables.h"

/* MSB-first bit writer into a byte buffer. */
typedef struct { unsigned char *buf; size_t cap; size_t nbits; } BW;

static void bw_init(BW *w, unsigned char *buf, size_t cap)
{ w->buf = buf; w->cap = cap; w->nbits = 0; memset(buf, 0, cap); }

static void bw_bit(BW *w, int bit)
{
    size_t byte = w->nbits >> 3; int off = 7 - (int)(w->nbits & 7);
    if (byte < w->cap && bit) w->buf[byte] |= (unsigned char)(1 << off);
    w->nbits++;
}
/* write n bits of val, most significant first */
static void bw_bits(BW *w, uint32_t val, int n)
{ while (n--) bw_bit(w, (int)((val >> n) & 1)); }

static void bw_pad_byte(BW *w) { while (w->nbits & 7) bw_bit(w, 0); }

/* Canonical codes from a length table (deflate order). */
typedef struct { uint32_t code[3073]; int len[3073]; } Canon;
static void canon(Canon *c, const unsigned char *len, int nsym)
{
    int l, i, count[20] = {0}; uint32_t next[20] = {0}, cur = 0;
    for (i = 0; i < nsym; i++) { c->len[i] = len[i]; if (len[i]) count[len[i]]++; }
    for (l = 1; l < 20; l++) { cur = (cur + (uint32_t)count[l-1]) << 1; next[l] = cur; }
    for (i = 0; i < nsym; i++) if (len[i]) c->code[i] = next[len[i]]++;
}
static void bw_sym(BW *w, const Canon *c, int sym) { bw_bits(w, c->code[sym], c->len[sym]); }

#endif
