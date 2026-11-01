/* seqlz decoder, written from seqlz.rst alone.
 *
 *   int cr_decode(const unsigned char *src, size_t len, unsigned char *out);
 *
 * Returns 0 if src[0..len) is a valid compressed seqlz block, leaving the
 * 4096 bytes of the decoded block in out; returns -1 otherwise.  It returns
 * for every input and reads only src[0..len), writes only out[0..4096).
 */
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "seqlz_tables.h"

#define P          12
#define BLOCK      4096
#define LEN_SYMBOLS 24

/* ---- canonical prefix-code decode tables (deflate style, RFC 1951) ---- */

typedef struct {
    int maxlen;
    int count[16];      /* count[l] = number of symbols with a code of length l */
    int symbols[3073];  /* symbols sorted by (length, symbol number) */
} Huff;

/* Build a decode table from an array of code lengths (0 = no code). */
static void huff_build(Huff *h, const unsigned char *len, int nsym)
{
    int i, l, idx;
    for (l = 0; l < 16; l++) h->count[l] = 0;
    h->maxlen = 0;
    for (i = 0; i < nsym; i++) {
        l = len[i];
        if (l) { h->count[l]++; if (l > h->maxlen) h->maxlen = l; }
    }
    idx = 0;
    for (l = 1; l <= h->maxlen; l++)
        for (i = 0; i < nsym; i++)
            if (len[i] == l) h->symbols[idx++] = i;
}

/* ---- bit reader: MSB-first over a region [base, len) of src ---- */
/* bit i of the region is bit 7-(i mod 8) of byte base + i/8;
 * bits at or past len read as 0 (allowed by the spec, checked at the end). */

typedef struct {
    const unsigned char *p;
    size_t len;
    size_t base;   /* byte where the region starts */
    size_t bi;     /* next bit index within the region */
} Bits;

static inline int get_bit(Bits *b)
{
    size_t byte = b->base + (b->bi >> 3);
    int v = 0;
    if (byte < b->len) v = (b->p[byte] >> (7 - (b->bi & 7))) & 1;
    b->bi++;
    return v;
}

static inline uint32_t read_n(Bits *b, int n)
{
    uint32_t v = 0;
    while (n--) v = (v << 1) | (uint32_t)get_bit(b);
    return v;
}

/* Decode one symbol; returns -1 if no code within maxlen bits (cannot happen
 * for a complete table, but guards built-from-block tables too). */
static int huff_sym(Bits *b, const Huff *h)
{
    int len, code = 0, first = 0, index = 0, c;
    for (len = 1; len <= h->maxlen; len++) {
        code |= get_bit(b);
        c = h->count[len];
        if (code - first < c) return h->symbols[index + (code - first)];
        index += c;
        first = (first + c) << 1;
        code <<= 1;
    }
    return -1;
}

/* A length value, section "Length values". */
static int read_value(Bits *b, const Huff *h)
{
    int s = huff_sym(b, h);
    if (s < 0) return -1;
    if (s < 16) return s;
    { int bb = s - 12; return (1 << bb) + (int)read_n(b, bb); }
}

/* ---- fixed tables, built once on the first call ---- */

static Huff H_tok, H_ll, H_ml, H_lit[8], H_own;
static int tables_ready = 0;

static void build_fixed(void)
{
    int i;
    huff_build(&H_tok, seqlz_tok_len, 3073);
    huff_build(&H_ll, seqlz_ll_len, LEN_SYMBOLS);
    huff_build(&H_ml, seqlz_ml_len, LEN_SYMBOLS);
    for (i = 0; i < 8; i++) huff_build(&H_lit[i], seqlz_lit_len[i], 256);
    huff_build(&H_own, seqlz_own_len, 19);
    tables_ready = 1;
}

/* Little-endian extraction of `cnt` bits starting at bit `off` (bit 0 = LSB of
 * byte base).  Used for the packed stream sizes S. */
static uint32_t le_bits(const unsigned char *p, size_t len, size_t base,
                        size_t off, int cnt)
{
    uint32_t v = 0;
    int t;
    for (t = 0; t < cnt; t++) {
        size_t bit = off + (size_t)t;
        size_t byte = base + (bit >> 3);
        int one = (byte < len) ? ((p[byte] >> (bit & 7)) & 1) : 0;
        v |= (uint32_t)one << t;
    }
    return v;
}

/* Decode a block's own literal table into own_len[256]; returns the number of
 * bytes d it occupies, or -1 if invalid.  base is byte 3 + w + e. */
static long decode_own(const unsigned char *src, size_t len, size_t base,
                       unsigned char own_len[256])
{
    Bits b; int idx = 0; long sum = 0; const long FULL = 1L << 20; /* 2^-l scaled by 2^20 */
    int guard = 0;
    b.p = src; b.len = len; b.base = base; b.bi = 0;
    memset(own_len, 0, 256);
    while (sum < FULL) {
        int s;
        if (++guard > 1100) return -1;
        s = huff_sym(&b, &H_own);
        if (s < 0) return -1;
        if (s <= 9) {               /* code length l = s + 1 */
            int l = s + 1;
            if (idx > 255) return -1;        /* byte value with a code above 255 */
            own_len[idx++] = (unsigned char)l;
            sum += FULL >> l;
            if (sum > FULL) return -1;        /* sum goes above 1 */
        } else {                    /* no-code run, k = s - 9 */
            int k = s - 9;
            uint32_t x = read_n(&b, k - 1);
            long run = ((long)1 << (k - 1)) + (long)x;
            idx += (int)run;
            if (idx > 256 && sum < FULL) return -1;
        }
        if (sum < FULL && idx >= 256) return -1; /* no room left to complete */
    }
    /* sum is exactly 1 here */
    {
        size_t bits = b.bi;
        long d = (long)((bits + 7) >> 3);
        size_t t;
        for (t = bits; t < (size_t)d * 8; t++) {           /* trailing bits are 0 */
            size_t byte = base + (t >> 3);
            int one = (byte < len) ? ((src[byte] >> (7 - (t & 7))) & 1) : 0;
            if (one) return -1;
        }
        if (base + (size_t)d > len) return -1;             /* 3+w+e+d > len */
        return d;
    }
}

int cr_decode(const unsigned char *src, size_t len, unsigned char *out)
{
    unsigned char lits[BLOCK];
    unsigned char own_len[256];
    Huff H_blockown;
    const Huff *litH = NULL;
    unsigned h;
    unsigned n;
    int coded;
    int e = 0, dt = 0;
    size_t bstart;
    size_t olen = 0, used = 0;
    unsigned last = 1;
    Bits B;
    int iter;

    if (!tables_ready) build_fixed();

    if (len < 2) return -1;
    h = (unsigned)src[0] | ((unsigned)src[1] << 8);
    coded = (h & 0x8000u) != 0;
    n = h & 0x7fffu;
    if (n > BLOCK) return -1;

    if (!coded) {
        if ((size_t)2 + n > len) return -1;
        memcpy(lits, src + 2, n);
        bstart = (size_t)2 + n;
    } else {
        int t, code, w;
        size_t s[8];
        size_t sum_s = 0;
        long d = 0;
        size_t start_j;
        int j;

        if (len < 3) return -1;
        t = src[2];
        code = t & 15;
        w = 5 + ((t >> 4) & 7);
        e = (t >> 7) & 1;
        if ((size_t)len < (size_t)3 + (size_t)w + (size_t)e) return -1;
        if (e) {
            dt = src[3];
            if (dt < 1 || dt > 3) return -1;
        }
        if (code > 8) return -1;

        /* 8 stream sizes, w bits each, packed little-endian from byte 3 + e. */
        for (j = 0; j < 8; j++)
            s[j] = le_bits(src, len, (size_t)3 + (size_t)e, (size_t)j * w, w);

        if (code == 8) {
            d = decode_own(src, len, (size_t)3 + (size_t)w + (size_t)e, own_len);
            if (d < 0) return -1;
            huff_build(&H_blockown, own_len, 256);
            litH = &H_blockown;
        } else {
            litH = &H_lit[code];
        }

        for (j = 0; j < 8; j++) sum_s += s[j];
        start_j = (size_t)3 + (size_t)w + (size_t)e + (size_t)d;
        if (start_j + sum_s > len) return -1;
        bstart = start_j + sum_s;

        /* Decode the 8 literal streams into lits[0..n). */
        {
            size_t off = start_j;
            for (j = 0; j < 8; j++) {
                Bits sb;
                size_t cnt = (n > (unsigned)j) ? ((n - 1 - (unsigned)j) / 8 + 1) : 0;
                size_t k, bits_used;
                sb.p = src; sb.len = len; sb.base = off; sb.bi = 0;
                for (k = 0; k < cnt; k++) {
                    int sym = huff_sym(&sb, litH);
                    if (sym < 0) return -1;
                    lits[(size_t)j + k * 8] = (unsigned char)sym;
                }
                bits_used = sb.bi;
                /* codes must exactly fill s[j] bytes, trailing bits 0 */
                if ((bits_used + 7) / 8 != s[j]) return -1;
                {
                    size_t tb;
                    for (tb = bits_used; tb < s[j] * 8; tb++) {
                        size_t byte = off + (tb >> 3);
                        int one = (byte < len) ?
                            ((src[byte] >> (7 - (tb & 7))) & 1) : 0;
                        if (one) return -1;
                    }
                }
                off += s[j];
            }
        }
    }

    /* ---- main sequence loop ---- */
    B.p = src; B.len = len; B.base = bstart; B.bi = 0;

    for (iter = 0; iter <= BLOCK; iter++) {
        unsigned ll, mlf, c, ml;
        unsigned off;
        int tok;

        if ((size_t)(n - used) == (size_t)(BLOCK - olen)) {
            /* last sequence: the literals left fill the rest */
            size_t k = (size_t)(n - used);
            for (; k; k--) {
                size_t p = olen;
                unsigned stored = lits[used];
                if (e) {
                    unsigned x;
                    if (dt == 1) x = 4;
                    else if (dt == 2) x = 8;
                    else x = (last < 3) ? 4 : (last == 3 ? 6 : last);
                    if (p >= x) stored = (stored + out[p - x]) & 0xff;
                }
                out[olen++] = (unsigned char)stored;
                used++;
            }
            goto done;
        }

        tok = huff_sym(&B, &H_tok);
        if (tok < 0) return -1;
        if (tok == 3072) {
            tok = (int)read_n(&B, 12);
            if (tok >= 3072) return -1;
        }
        ll = (unsigned)(tok & 15);
        mlf = (unsigned)((tok >> 4) & 31);
        c = (unsigned)(tok >> 9);

        /* offset */
        switch (c) {
            case 0: off = last; break;
            case 1: off = (unsigned)read_n(&B, 1) * 2; break;
            case 2: off = (unsigned)read_n(&B, 8); break;
            case 3: off = (unsigned)read_n(&B, P); break;
            case 4: off = (unsigned)read_n(&B, 5) * 8; break;
            default: off = (unsigned)read_n(&B, 8) * 8; break; /* class 5 */
        }

        if (ll == 15) {
            int v = read_value(&B, &H_ll);
            if (v < 0) return -1;
            ll = 15 + (unsigned)v;
        }
        if (ll > (unsigned)(n - used) || ll > (unsigned)(BLOCK - olen)) return -1;

        /* copy(ll) */
        {
            size_t k = ll;
            for (; k; k--) {
                size_t p = olen;
                unsigned stored = lits[used];
                if (e) {
                    unsigned x;
                    if (dt == 1) x = 4;
                    else if (dt == 2) x = 8;
                    else x = (last < 3) ? 4 : (last == 3 ? 6 : last);
                    if (p >= x) stored = (stored + out[p - x]) & 0xff;
                }
                out[olen++] = (unsigned char)stored;
                used++;
            }
        }

        ml = mlf + 4;
        if (mlf == 31) {
            int v = read_value(&B, &H_ml);
            if (v < 0) return -1;
            ml = 35 + (unsigned)v;
        }
        last = off;
        if (off == 0 || (size_t)off > olen || ml > (unsigned)(BLOCK - olen))
            return -1;
        {
            unsigned k;
            for (k = 0; k < ml; k++) {
                out[olen] = out[olen - off];
                olen++;
            }
        }
    }
    return -1; /* too many sequences: malformed */

done:
    /* End of bitstream: fewer than 8 bits of B left, all 0. */
    {
        size_t totalbits = (len - bstart) * 8;
        size_t consumed = B.bi;
        size_t t;
        if (consumed > totalbits) return -1;          /* used bits past the end */
        if (totalbits - consumed >= 8) return -1;      /* a whole byte or more left */
        for (t = consumed; t < totalbits; t++) {
            size_t byte = bstart + (t >> 3);
            if ((src[byte] >> (7 - (t & 7))) & 1) return -1;
        }
    }
    (void)olen;
    return 0;
}
