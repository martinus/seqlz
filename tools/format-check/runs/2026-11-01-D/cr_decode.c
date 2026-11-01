/* Decoder for the seqlz format (format.md), 4 KiB blocks. C11, standard library only. */
#include "cr_decode.h"

#include <stdint.h>
#include <string.h>

#include "cr_tables.h"

#define P 12
#define BLOCK 4096
#define LEN_SYMBOLS (12 + P)

#define TOK_BITS 11
#define LEN_BITS 8
#define LIT_BITS 10
#define OWN_BITS 8

/* A decode table entry: symbol << 4 | code length. Indexed by the next maxbits bits. */
typedef uint16_t lut_t;

static lut_t lut_tok[1 << TOK_BITS];
static lut_t lut_ll[1 << LEN_BITS];
static lut_t lut_ml[1 << LEN_BITS];
static lut_t lut_lit[8][1 << LIT_BITS];
static lut_t lut_own[1 << OWN_BITS];
static int tables_built;

/* Builds the decode table of a complete code (canonical codes as in format.md, "Prefix codes").
 * Returns 0 on success, -1 if the code is not complete or a length exceeds maxbits. */
static int build_lut(const unsigned char *lens, unsigned nsym, unsigned maxbits, lut_t *lut)
{
    unsigned count[16] = {0}, next[16];
    uint32_t space = 0;
    for (unsigned s = 0; s < nsym; s++) {
        unsigned l = lens[s];
        if (l > maxbits)
            return -1;
        if (l) {
            count[l]++;
            space += (uint32_t)1 << (maxbits - l);
        }
    }
    if (space != ((uint32_t)1 << maxbits))
        return -1;
    unsigned code = 0;
    count[0] = 0;
    for (unsigned l = 1; l <= maxbits; l++) {
        code = (code + count[l - 1]) << 1;
        next[l] = code;
    }
    for (unsigned s = 0; s < nsym; s++) {
        unsigned l = lens[s];
        if (!l)
            continue;
        unsigned c = next[l]++;
        unsigned lo = c << (maxbits - l), hi = (c + 1) << (maxbits - l);
        for (unsigned i = lo; i < hi; i++)
            lut[i] = (lut_t)((s << 4) | l);
    }
    return 0;
}

static void build_tables(void)
{
    build_lut(cr_len_tok, 3073, TOK_BITS, lut_tok);
    build_lut(cr_len_ll, LEN_SYMBOLS, LEN_BITS, lut_ll);
    build_lut(cr_len_ml, LEN_SYMBOLS, LEN_BITS, lut_ml);
    for (int i = 0; i < 8; i++)
        build_lut(cr_len_lit + 256 * i, 256, LIT_BITS, lut_lit[i]);
    build_lut(cr_len_own, 19, OWN_BITS, lut_own);
    tables_built = 1;
}

/* MSB-first bit reader over p[0..n); reads zeros past the end. */
typedef struct {
    const unsigned char *p;
    size_t n;
    size_t idx;   /* next byte to load */
    uint64_t buf; /* valid bits left-aligned */
    unsigned cnt; /* number of valid bits in buf */
} br_t;

static void br_init(br_t *b, const unsigned char *p, size_t n)
{
    b->p = p;
    b->n = n;
    b->idx = 0;
    b->buf = 0;
    b->cnt = 0;
}

static inline void br_refill(br_t *b)
{
    while (b->cnt <= 56) {
        uint64_t byte = b->idx < b->n ? b->p[b->idx] : 0;
        b->idx++;
        b->buf |= byte << (56 - b->cnt);
        b->cnt += 8;
    }
}

/* k from 0 to 32 */
static inline uint32_t br_read(br_t *b, unsigned k)
{
    if (k == 0)
        return 0;
    br_refill(b);
    uint32_t v = (uint32_t)(b->buf >> (64 - k));
    b->buf <<= k;
    b->cnt -= k;
    return v;
}

static inline unsigned br_symbol(br_t *b, const lut_t *lut, unsigned maxbits)
{
    br_refill(b);
    lut_t e = lut[b->buf >> (64 - maxbits)];
    unsigned l = e & 15;
    b->buf <<= l;
    b->cnt -= l;
    return e >> 4;
}

static inline size_t br_used(const br_t *b)
{
    return b->idx * 8 - b->cnt;
}

/* The end rule: after the bits used, fewer than 8 bits of p[0..n) are left, and they are 0. */
static int br_check_end(const br_t *b)
{
    size_t used = br_used(b);
    size_t total = b->n * 8;
    if (used > total)
        return -1;
    size_t left = total - used;
    if (left >= 8)
        return -1;
    if (left && (b->p[b->n - 1] & ((1u << left) - 1)))
        return -1;
    return 0;
}

static inline unsigned read_value(br_t *b, const lut_t *lut)
{
    unsigned s = br_symbol(b, lut, LEN_BITS);
    if (s < 16)
        return s;
    unsigned bits = s - 12;
    return (1u << bits) + br_read(b, bits);
}

/* Parses a block's own literal table from p[0..n). Returns d, its size in bytes, or -1. */
static long parse_own(const unsigned char *p, size_t n, lut_t *lut)
{
    unsigned char lens[256];
    memset(lens, 0, sizeof lens);
    br_t b;
    br_init(&b, p, n);
    unsigned v = 0;      /* next byte value */
    unsigned sum = 0;    /* sum of 2^-l, in units of 2^-10 */
    for (;;) {
        unsigned s = br_symbol(&b, lut_own, OWN_BITS);
        if (s < 10) {
            unsigned l = s + 1;
            if (v > 255)
                return -1;
            lens[v++] = (unsigned char)l;
            sum += 1u << (10 - l);
            if (sum > 1024)
                return -1;
            if (sum == 1024)
                break;
        } else {
            unsigned k = s - 9;
            v += (1u << (k - 1)) + br_read(&b, k - 1);
            /* The list can only end with a length symbol, which would then be for a byte value
             * above 255: invalid either way (QUESTIONS.md 3). This also bounds the loop. */
            if (v > 255)
                return -1;
        }
    }
    size_t used = br_used(&b);
    size_t d = (used + 7) / 8;
    if (d > n)
        return -1;
    unsigned pad = (unsigned)(d * 8 - used);
    if (pad && (p[d - 1] & ((1u << pad) - 1)))
        return -1;
    if (build_lut(lens, 256, LIT_BITS, lut) != 0)
        return -1; /* cannot happen: the sum is exactly 1 */
    return (long)d;
}

/* copy(k) of format.md, "Decoding" and "Literals as deltas" */
static void copy_lits(unsigned char *out, size_t *o, const unsigned char *lp, size_t *used, size_t k,
                      unsigned e, unsigned dt, unsigned last)
{
    size_t p = *o;
    const unsigned char *l = lp + *used;
    if (!e) {
        memcpy(out + p, l, k);
    } else {
        size_t x = dt == 1 ? 4 : dt == 2 ? 8 : (last <= 2 ? 4 : last == 3 ? 6 : last);
        for (size_t i = 0; i < k; i++, p++)
            out[p] = p >= x ? (unsigned char)(l[i] + out[p - x]) : l[i];
    }
    *o += k;
    *used += k;
}

int cr_decode(const unsigned char *src, size_t len, unsigned char *out)
{
    if (!tables_built)
        build_tables();

    if (len < 2)
        return -1;
    unsigned h = src[0] | (unsigned)src[1] << 8;
    size_t n = h & 0x7fff;
    if (n > BLOCK)
        return -1;

    unsigned char lits[BLOCK];
    const unsigned char *lp; /* the literals, as stored (maybe deltas) */
    size_t bstart;
    unsigned e = 0, dt = 0;

    if (!(h & 0x8000)) {
        if (2 + n > len)
            return -1;
        lp = src + 2;
        bstart = 2 + n;
    } else {
        if (len < 3)
            return -1;
        unsigned t = src[2];
        unsigned code = t & 15;
        unsigned w = 5 + ((t >> 4) & 7);
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
        size_t s[8];
        {
            const unsigned char *sp = src + 3 + e;
            uint64_t acc = 0;
            unsigned have = 0, bi = 0;
            for (int j = 0; j < 8; j++) {
                while (have < w) {
                    acc |= (uint64_t)sp[bi++] << have;
                    have += 8;
                }
                s[j] = (size_t)(acc & ((1u << w) - 1));
                acc >>= w;
                have -= w;
            }
        }
        size_t pos = 3 + w + e;
        lut_t own[1 << LIT_BITS];
        const lut_t *lit;
        if (code == 8) {
            long d = parse_own(src + pos, len - pos, own);
            if (d < 0)
                return -1;
            pos += (size_t)d;
            lit = own;
        } else {
            lit = lut_lit[code];
        }
        size_t total = 0;
        for (int j = 0; j < 8; j++)
            total += s[j];
        if (total > len - pos)
            return -1;
        for (size_t j = 0; j < 8; j++) {
            br_t b;
            br_init(&b, src + pos, s[j]);
            for (size_t i = j; i < n; i += 8)
                lits[i] = (unsigned char)br_symbol(&b, lit, LIT_BITS);
            if (br_check_end(&b) != 0)
                return -1;
            pos += s[j];
        }
        lp = lits;
        bstart = pos;
    }

    br_t b;
    br_init(&b, src + bstart, len - bstart);
    size_t used = 0, o = 0;
    unsigned last = 1;
    for (;;) {
        if (n - used == BLOCK - o) {
            /* the last sequence: the literals left fill the rest of the block */
            copy_lits(out, &o, lp, &used, n - used, e, dt, last);
            break;
        }
        {
            unsigned tok = br_symbol(&b, lut_tok, TOK_BITS);
            if (tok == 3072) {
                tok = br_read(&b, 12);
                if (tok >= 3072)
                    return -1;
            }
            unsigned ll = tok & 15, mlf = (tok >> 4) & 31, c = tok >> 9;
            unsigned off;
            switch (c) {
            case 0: off = last; break;
            case 1: off = br_read(&b, 1) * 2; break;
            case 2: off = br_read(&b, 8); break;
            case 3: off = br_read(&b, P); break;
            case 4: off = br_read(&b, 5) * 8; break;
            default: off = br_read(&b, 8) * 8; break;
            }
            if (ll == 15)
                ll = 15 + read_value(&b, lut_ll);
            if (ll > n - used || ll > BLOCK - o)
                return -1;
            /* copy(ll) uses `last` of the sequence before: it comes before last = off */
            copy_lits(out, &o, lp, &used, ll, e, dt, last);
            unsigned ml = mlf + 4;
            if (mlf == 31)
                ml = 35 + read_value(&b, lut_ml);
            last = off;
            if (off == 0 || off > o || ml > BLOCK - o)
                return -1;
            for (unsigned i = 0; i < ml; i++, o++)
                out[o] = out[o - off];
        }
    }
    return br_check_end(&b);
}
