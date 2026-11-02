/* Decoder of the seqlz format for blocks of 4 KiB, written from format.md alone. */
#include "cr_decode.h"
#include "cr_tables.h"

#include <stdint.h>
#include <string.h>

#define P 12
#define BLOCK CR_BLOCK
#define LEN_SYMBOLS (12 + P)
#define ESCAPE 3072

/* ---- prefix codes ------------------------------------------------------------------------- */

/* A decode table for a complete prefix code whose longest code has `maxlen` bits: indexed by the
 * next `maxlen` bits, it gives the symbol whose code they start with and that code's length. */
typedef struct {
    uint16_t sym;
    uint8_t len;
} entry;

/* Builds the canonical code of format.md "Prefix codes" from the lengths and fills `lut`
 * (1 << maxlen entries). The lengths must be complete and at most maxlen. */
static void build_lut(const unsigned char *lens, unsigned nsym, unsigned maxlen, entry *lut)
{
    unsigned count[16] = {0}, first[16] = {0};
    for (unsigned i = 0; i < nsym; i++)
        count[lens[i]]++;
    count[0] = 0;
    first[1] = 0;
    for (unsigned l = 2; l <= maxlen; l++)
        first[l] = (first[l - 1] + count[l - 1]) * 2;
    for (unsigned i = 0; i < nsym; i++) {
        unsigned l = lens[i];
        if (!l)
            continue;
        unsigned code = first[l]++;
        unsigned lo = code << (maxlen - l), n = 1u << (maxlen - l);
        for (unsigned k = 0; k < n; k++) {
            lut[lo + k].sym = (uint16_t)i;
            lut[lo + k].len = (uint8_t)l;
        }
    }
}

#define TOK_BITS 11
#define LL_BITS 8
#define ML_BITS 7
#define LIT_BITS 10
#define OWN_BITS 8

static entry lut_tok[1 << TOK_BITS];
static entry lut_ll[1 << LL_BITS];
static entry lut_ml[1 << ML_BITS];
static entry lut_lit[8][1 << LIT_BITS];
static entry lut_own[1 << OWN_BITS];
static int initialized;

static void init_tables(void)
{
    build_lut(cr_len_TOK, 3073, TOK_BITS, lut_tok);
    build_lut(cr_len_LL, LEN_SYMBOLS, LL_BITS, lut_ll);
    build_lut(cr_len_ML, LEN_SYMBOLS, ML_BITS, lut_ml);
    for (int i = 0; i < 8; i++)
        build_lut(cr_len_LIT + 256 * i, 256, LIT_BITS, lut_lit[i]);
    build_lut(cr_len_OWN, 19, OWN_BITS, lut_own);
    initialized = 1;
}

/* ---- bit reader, most significant bit first, zeros past the end ---------------------------- */

typedef struct {
    const unsigned char *p; /* first byte */
    size_t n;               /* bytes that may be read */
    size_t pos;             /* bits used so far; may go past 8 * n, then the bits used were zeros */
} bits;

static inline unsigned byte_at(const bits *b, size_t i) { return i < b->n ? b->p[i] : 0; }

/* The next k bits (k <= 16) as a number, without using them. */
static inline unsigned peek(const bits *b, unsigned k)
{
    size_t i = b->pos >> 3;
    uint32_t v = ((uint32_t)byte_at(b, i) << 16) | ((uint32_t)byte_at(b, i + 1) << 8) |
                 (uint32_t)byte_at(b, i + 2);
    v = (v << (b->pos & 7)) & 0xffffffu;
    return k ? (unsigned)(v >> (24 - k)) : 0;
}

static inline unsigned readbits(bits *b, unsigned k)
{
    unsigned v = peek(b, k);
    b->pos += k;
    return v;
}

static inline unsigned symbol(bits *b, const entry *lut, unsigned maxlen)
{
    const entry *e = &lut[peek(b, maxlen)];
    b->pos += e->len;
    return e->sym;
}

static inline unsigned value(bits *b, const entry *lut, unsigned maxlen)
{
    unsigned s = symbol(b, lut, maxlen);
    if (s < 16)
        return s;
    unsigned k = s - 12;
    return (1u << k) + readbits(b, k);
}

/* After using b->pos bits of a region of b->n bytes: it must end in the region's last byte, and the
 * bits after it in that byte must be 0. Returns 0 if so. */
static int check_end(const bits *b)
{
    size_t total = 8 * b->n;
    if (b->pos > total)
        return -1;
    size_t rest = total - b->pos;
    if (rest >= 8)
        return -1;
    if (rest && (b->p[b->n - 1] & ((1u << rest) - 1)))
        return -1;
    return 0;
}

/* ---- a block's own literal table ------------------------------------------------------------ */

/* Reads the table from src[at..len), zeros past len. Fills lut (1 << LIT_BITS entries) and *d.
 * Returns 0 if the table is valid. */
static int read_own_table(const unsigned char *src, size_t len, size_t at, entry *lut, size_t *d)
{
    unsigned char lens[256];
    bits b = {src + at, len - at, 0};
    unsigned v = 0;      /* next byte value */
    unsigned sum = 0;    /* sum of 2^-l, in units of 2^-LIT_BITS */
    const unsigned one = 1u << LIT_BITS;

    memset(lens, 0, sizeof lens);
    for (;;) {
        unsigned s = symbol(&b, lut_own, OWN_BITS);
        if (s <= 9) {               /* a code of s + 1 bits */
            unsigned l = s + 1;
            if (v > 255)
                return -1;          /* a byte value with a code above 255 */
            lens[v++] = (unsigned char)l;
            sum += one >> l;
            if (sum > one)
                return -1;          /* the sum goes above 1 */
            if (sum == one)
                break;              /* complete: the list ends */
        } else {                    /* a run of 2^(k-1) + read(k-1) byte values without a code */
            unsigned k = s - 9;
            unsigned run = (1u << (k - 1)) + readbits(&b, k - 1);
            if (v + run > 256)
                return -1;          /* the run goes past 255 */
            v += run;
        }
        /* each symbol is for at least one byte value, so v grows and the loop ends by v > 256 */
    }
    size_t used = (b.pos + 7) / 8;
    if (used > b.n)
        return -1;                  /* 3 + w + e + d > len */
    unsigned rest = (unsigned)(8 * used - b.pos);
    if (rest && (src[at + used - 1] & ((1u << rest) - 1)))
        return -1;                  /* bits after the last symbol in its byte are not 0 */
    *d = used;
    build_lut(lens, 256, LIT_BITS, lut);
    return 0;
}

/* ---- the block ------------------------------------------------------------------------------ */

/* copy(k) of format.md "Decoding": appends lits[*used .. *used + k) to out at *o, as they are or,
 * with e = 1, as deltas to the byte x before (format.md "Literals as deltas"). */
static void copy_lits(unsigned char *out, size_t *o, const unsigned char *lits, size_t *used,
                      size_t k, unsigned e, unsigned dt, unsigned last)
{
    size_t p = *o;
    if (!e) {
        memcpy(out + p, lits + *used, k);
    } else {
        size_t x = dt == 1 ? 4 : dt == 2 ? 8 : last <= 2 ? 4 : last == 3 ? 6 : last;
        for (size_t i = 0; i < k; i++, p++)
            out[p] = (unsigned char)(lits[*used + i] + (p >= x ? out[p - x] : 0));
    }
    *o += k;
    *used += k;
}

int cr_decode(const unsigned char *src, size_t len, unsigned char *out)
{
    unsigned char lits_coded[BLOCK];
    const unsigned char *lits;
    size_t bstart;
    unsigned e = 0, dt = 0;

    if (!initialized)
        init_tables();

    if (len < 2)
        return -1;
    unsigned h = (unsigned)src[0] | ((unsigned)src[1] << 8);
    unsigned n = h & 0x7fff;
    if (n > BLOCK)
        return -1;

    if (!(h & 0x8000)) {
        /* raw literals */
        if (2 + (size_t)n > len)
            return -1;
        lits = src + 2;
        bstart = 2 + (size_t)n;
    } else {
        /* coded literals */
        if (len < 3)
            return -1;
        unsigned t = src[2];
        unsigned code = t & 15, w = 5 + ((t >> 4) & 7);
        e = t >> 7;
        if (len < 3 + w + e)
            return -1;
        if (e) {
            dt = src[3];
            if (dt < 1 || dt > 3)
                return -1;
        }
        if (code > 8)
            return -1;

        /* S: w bytes from 3 + e, little endian; s[j] = (S >> (j * w)) & ((1 << w) - 1) */
        size_t s[8];
        for (unsigned j = 0; j < 8; j++) {
            unsigned v = 0;
            for (unsigned k = 0; k < w; k++) {
                unsigned bit = j * w + k;
                v |= ((src[3 + e + bit / 8] >> (bit % 8)) & 1u) << k;
            }
            s[j] = v;
        }

        size_t at = 3 + w + e, dsize = 0;
        entry own[1 << LIT_BITS];
        const entry *lut;
        if (code == 8) {
            if (read_own_table(src, len, at, own, &dsize))
                return -1;
            lut = own;
        } else {
            lut = lut_lit[code];
        }

        size_t start = at + dsize;
        size_t total = 0;
        for (unsigned j = 0; j < 8; j++)
            total += s[j];
        if (start + total > len)
            return -1;

        for (unsigned j = 0; j < 8; j++) {
            if (j >= n) {           /* a stream without literals */
                if (s[j])
                    return -1;
                continue;
            }
            bits b = {src + start, s[j], 0};
            for (unsigned i = j; i < n; i += 8)
                lits_coded[i] = (unsigned char)symbol(&b, lut, LIT_BITS);
            /* 8 * s[j] - 8 < bits <= 8 * s[j], and the bits after the last code are 0 */
            if (s[j] == 0 || check_end(&b))
                return -1;
            start += s[j];
        }
        lits = lits_coded;
        bstart = start;
    }

    /* the sequences */
    bits b = {src + bstart, len - bstart, 0};
    size_t used = 0, o = 0;
    unsigned last = 1;

    for (;;) {
        if (n - used == BLOCK - o) {    /* the last sequence */
            copy_lits(out, &o, lits, &used, n - used, e, dt, last);
            break;
        }
        unsigned token = symbol(&b, lut_tok, TOK_BITS);
        if (token == ESCAPE) {
            token = readbits(&b, 12);
            if (token >= ESCAPE)
                return -1;
        }
        size_t ll = token & 15;
        unsigned mlf = (token >> 4) & 31, c = token >> 9;
        unsigned off;
        switch (c) {
        case 0: off = last; break;
        case 1: off = readbits(&b, 1) * 2; break;
        case 2: off = readbits(&b, 8); break;
        case 3: off = readbits(&b, P); break;
        case 4: off = readbits(&b, 5) * 8; break;
        default: off = readbits(&b, 8) * 8; break;
        }
        if (ll == 15)
            ll = 15 + value(&b, lut_ll, LL_BITS);
        if (ll > n - used || ll > BLOCK - o)
            return -1;
        copy_lits(out, &o, lits, &used, ll, e, dt, last);
        size_t ml = mlf + 4;
        if (mlf == 31)
            ml = 35 + value(&b, lut_ml, ML_BITS);
        last = off;
        if (off == 0 || off > o || ml > BLOCK - o)
            return -1;
        for (size_t k = 0; k < ml; k++, o++)
            out[o] = out[o - off];
        if (b.pos > 8 * b.n)
            return -1;                  /* needed a bit past the end of B */
    }

    return check_end(&b);
}
