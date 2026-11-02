/*
 * seqlz decoder, C11, C standard library only.
 *
 * Written from seqlz.rst and the tables in tables/ alone. See QUESTIONS.md
 * for the places where a reading had to be chosen.
 */
#include "seqlz.h"
#include "seqlz_tables.h"

#include <stdint.h>
#include <string.h>

/* Block constants (seqlz.rst, "Block size"). */
#define P 12
#define BLOCK 4096
#define LEN_SYMBOLS 24 /* 12 + P */

/* ---- canonical prefix-code decode table ------------------------------- */

#define MAX_CODE_LEN 11 /* longest code of any table here (TOK) */

typedef struct {
    int maxlen;
    int count[MAX_CODE_LEN + 1]; /* symbols with a code of length l */
    int first[MAX_CODE_LEN + 1]; /* first canonical code of length l */
    int base[MAX_CODE_LEN + 1];  /* index into sorted[] for length l */
    int nsym;
    uint16_t *sorted; /* symbols in canonical (length, symbol) order */
} dectab;

/*
 * Build a decode table from code lengths. len[i] is symbol i's code length,
 * 0 if it has none. nsym is the number of symbols, sorted points to storage
 * of at least nsym entries. Returns 0 on success, -1 if the lengths are not a
 * complete prefix code (used only for the block's own table; the fixed tables
 * are known complete).
 */
static int build_dectab(dectab *t, const unsigned char *len, int nsym,
                        uint16_t *sorted)
{
    int l, i, code, idx;
    long sum; /* code space in units of 2^-maxlen, scaled below */

    t->nsym = nsym;
    t->sorted = sorted;
    t->maxlen = 0;
    for (l = 0; l <= MAX_CODE_LEN; l++)
        t->count[l] = 0;

    for (i = 0; i < nsym; i++) {
        int cl = len[i];
        if (cl < 0 || cl > MAX_CODE_LEN)
            return -1;
        if (cl > 0) {
            t->count[cl]++;
            if (cl > t->maxlen)
                t->maxlen = cl;
        }
    }
    if (t->maxlen == 0)
        return -1; /* empty table: no codes, not complete */

    /* completeness: sum of 2^-l over all codes must be exactly 1 */
    sum = 0;
    for (l = 1; l <= t->maxlen; l++)
        sum += (long)t->count[l] << (t->maxlen - l);
    if (sum != (1L << t->maxlen))
        return -1;

    /* canonical first codes and base indices */
    code = 0;
    idx = 0;
    for (l = 1; l <= t->maxlen; l++) {
        t->first[l] = code;
        t->base[l] = idx;
        code = (code + t->count[l]) << 1;
        idx += t->count[l];
    }

    /* fill sorted[] in (length, symbol) order */
    {
        int next[MAX_CODE_LEN + 1];
        for (l = 0; l <= MAX_CODE_LEN; l++)
            next[l] = l >= 1 && l <= t->maxlen ? t->base[l] : 0;
        for (i = 0; i < nsym; i++) {
            int cl = len[i];
            if (cl > 0)
                sorted[next[cl]++] = (uint16_t)i;
        }
    }
    return 0;
}

/* ---- bit reader, most significant bit first --------------------------- */

typedef struct {
    const unsigned char *p; /* block base */
    size_t start;           /* first byte of this reader's range */
    size_t nbytes;          /* bytes in range */
    size_t pos;             /* bits consumed, relative to start */
    int over;               /* set if a bit at or past the range was read */
} breader;

static void br_init(breader *b, const unsigned char *base, size_t start,
                    size_t nbytes)
{
    b->p = base;
    b->start = start;
    b->nbytes = nbytes;
    b->pos = 0;
    b->over = 0;
}

static inline int br_bit(breader *b)
{
    int bit;
    if (b->pos < b->nbytes * 8) {
        unsigned char byte = b->p[b->start + (b->pos >> 3)];
        bit = (byte >> (7 - (b->pos & 7))) & 1;
    } else {
        bit = 0;
        b->over = 1;
    }
    b->pos++;
    return bit;
}

static inline unsigned br_read(breader *b, int n)
{
    unsigned v = 0;
    int i;
    for (i = 0; i < n; i++)
        v = (v << 1) | (unsigned)br_bit(b);
    return v;
}

/* Decode one symbol. Returns the symbol, or -1 if no code matched within
 * maxlen bits (cannot happen for a complete table). */
static int br_symbol(breader *b, const dectab *t)
{
    int code = 0, l;
    for (l = 1; l <= t->maxlen; l++) {
        int idx;
        code = (code << 1) | br_bit(b);
        idx = code - t->first[l];
        if (t->count[l] > 0 && idx >= 0 && idx < t->count[l])
            return t->sorted[t->base[l] + idx];
    }
    return -1;
}

/* value(table): a length value (seqlz.rst, "Length values"). */
static long br_value(breader *b, const dectab *t)
{
    int s = br_symbol(b, t);
    if (s < 0)
        return -1;
    if (s < 16)
        return s;
    {
        int bits = s - 12;
        return (1L << bits) + (long)br_read(b, bits);
    }
}

/* ---- fixed tables, built once ----------------------------------------- */

static dectab tok_tab, ll_tab, ml_tab, lit_tab[8];
static uint16_t tok_sorted[3073];
static uint16_t ll_sorted[24];
static uint16_t ml_sorted[24];
static uint16_t lit_sorted[8][256];
static int tables_ready;

static void init_tables(void)
{
    int i;
    build_dectab(&tok_tab, seqlz_tok_len, 3073, tok_sorted);
    build_dectab(&ll_tab, seqlz_ll_len, 24, ll_sorted);
    build_dectab(&ml_tab, seqlz_ml_len, 24, ml_sorted);
    for (i = 0; i < 8; i++)
        build_dectab(&lit_tab[i], seqlz_lit_len[i], 256, lit_sorted[i]);
    tables_ready = 1;
}

/* ---- block's own literal table ---------------------------------------- */

static dectab own_descr_tab;
static uint16_t own_descr_sorted[19];
static int own_descr_ready;

/*
 * Parse the block's own literal table starting at byte off0 of the block.
 * On success fills lit_len[256] with the code lengths, sets *d_out to the
 * number of bytes the table occupies, and returns 0. Returns -1 if the
 * description is invalid (see "A block's own literal table").
 */
static int parse_own_table(const unsigned char *src, size_t len, size_t off0,
                           unsigned char *lit_len, size_t *d_out)
{
    breader b;
    long sum;          /* code space in units of 2^-10 */
    const long full = 1L << 10;
    int bv = 0;        /* next byte value to describe */
    size_t d, k;

    memset(lit_len, 0, 256);
    br_init(&b, src, off0, len - off0); /* may read as 0 past len */

    sum = 0;
    for (;;) {
        int s = br_symbol(&b, &own_descr_tab);
        if (s < 0)
            return -1;
        if (s <= 9) {
            int l = s + 1; /* code of l bits for byte value bv */
            if (bv > 255)
                return -1; /* a byte value with a code is above 255 */
            lit_len[bv] = (unsigned char)l;
            bv++;
            sum += full >> l;
            if (sum > full)
                return -1; /* the sum goes above 1 */
            if (sum == full)
                break; /* table complete */
        } else {
            int kk = s - 9;          /* k from 1 to 9 */
            long r = br_read(&b, kk - 1);
            long run = (1L << (kk - 1)) + r;
            if ((long)bv + run > 256)
                return -1; /* a run of byte values without a code past 255 */
            bv += (int)run;
            /* a run never completes the table (sum unchanged) */
        }
    }

    /* d = number of bytes the description occupies; trailing bits are 0 */
    d = (b.pos + 7) >> 3;
    if (off0 + d > len)
        return -1; /* 3 + w + e + d > len; also catches reads past len */

    /* bits after the last symbol in its byte must be 0 */
    for (k = b.pos; k < d * 8; k++) {
        size_t bytei = off0 + (k >> 3);
        int bit = (src[bytei] >> (7 - (k & 7))) & 1;
        if (bit)
            return -1;
    }

    *d_out = d;
    return 0;
}

/* ---- the decoder ------------------------------------------------------ */

int cr_decode(const unsigned char *src, size_t len, unsigned char *out)
{
    unsigned n, h;
    int coded;
    size_t lit_off;       /* where the raw/decoded literals sit (raw only) */
    unsigned char lits[BLOCK]; /* decoded literal bytes (coded path) */
    const unsigned char *lit_src; /* source of literal values */
    int delta = 0;        /* e: literals are deltas */
    int dt = 0;           /* which delta */
    size_t b_start;       /* first byte of the sequence bitstream */

    if (!tables_ready)
        init_tables();

    /* ---- header ---- */
    if (len < 2)
        return -1;
    h = (unsigned)src[0] | ((unsigned)src[1] << 8);
    n = h & 0x7fff;
    coded = (h & 0x8000) != 0;
    if (n > BLOCK)
        return -1;

    if (!coded) {
        /* raw literals */
        if ((size_t)2 + n > len)
            return -1;
        lit_src = src + 2;
        (void)lit_off;
        b_start = 2 + n;
    } else {
        /* coded literals */
        unsigned t, code, w, e;
        size_t off_sizes, off_own, d = 0, sum_s, start_j;
        unsigned s[8];
        dectab *lt;                 /* table used for the literals */
        dectab own_tab;             /* block's own table, if code == 8 */
        uint16_t own_sorted[256];
        unsigned char own_len[256];
        int j;

        if (len < 3)
            return -1; /* no byte 2 */
        t = src[2];
        code = t & 15;
        w = 5 + ((t >> 4) & 7); /* 5..12 */
        e = t >> 7;             /* 0 or 1 */

        if (len < (size_t)3 + w + e)
            return -1;
        if (e) {
            dt = src[3];
            if (dt < 1 || dt > 3)
                return -1;
            delta = 1;
        }
        if (code > 8)
            return -1;

        /* the w bytes from 3+e are one number S; s[j] are w-bit fields */
        off_sizes = 3 + e;
        {
            /* S has 8*w bits, little endian; extract each w-bit field */
            unsigned mask = (w == 32) ? 0xffffffffu : ((1u << w) - 1u);
            int jj;
            for (jj = 0; jj < 8; jj++) {
                /* bit index of field jj within S */
                unsigned bitpos = (unsigned)jj * w;
                unsigned val = 0;
                int gi;
                for (gi = 0; gi < (int)w; gi++) {
                    unsigned bi = bitpos + (unsigned)gi;
                    unsigned byte = src[off_sizes + (bi >> 3)];
                    unsigned bit = (byte >> (bi & 7)) & 1u; /* little endian */
                    val |= bit << gi;
                }
                s[jj] = val & mask;
            }
        }

        off_own = 3 + w + e;
        if (code == 8) {
            if (!own_descr_ready) {
                if (build_dectab(&own_descr_tab, seqlz_own_len, 19,
                                 own_descr_sorted) != 0)
                    return -1;
                own_descr_ready = 1;
            }
            if (parse_own_table(src, len, off_own, own_len, &d) != 0)
                return -1;
            if (build_dectab(&own_tab, own_len, 256, own_sorted) != 0)
                return -1;
            lt = &own_tab;
        } else {
            d = 0;
            lt = &lit_tab[code];
        }

        sum_s = 0;
        for (j = 0; j < 8; j++)
            sum_s += s[j];
        if (off_own + d + sum_s > len)
            return -1;

        /* decode the 8 literal streams */
        start_j = off_own + d;
        {
            size_t cur = start_j;
            for (j = 0; j < 8; j++) {
                /* number of literals in stream j */
                unsigned cnt = (n > (unsigned)j)
                                   ? ((n - 1 - (unsigned)j) / 8 + 1)
                                   : 0;
                breader sb;
                unsigned k2;
                if (cnt == 0) {
                    if (s[j] != 0)
                        return -1; /* empty stream must have size 0 */
                    cur += s[j];
                    continue;
                }
                br_init(&sb, src, cur, s[j]);
                for (k2 = 0; k2 < cnt; k2++) {
                    int sym = br_symbol(&sb, lt);
                    if (sym < 0)
                        return -1;
                    lits[(size_t)j + (size_t)k2 * 8] = (unsigned char)sym;
                }
                /* codes must end within the stream's last byte */
                {
                    size_t bits = sb.pos;
                    size_t cap = (size_t)s[j] * 8;
                    size_t kk;
                    if (sb.over || bits > cap)
                        return -1;
                    if (!(cap - bits < 8))
                        return -1; /* must end in the last byte */
                    for (kk = bits; kk < cap; kk++) {
                        int bit = (src[cur + (kk >> 3)] >> (7 - (kk & 7))) & 1;
                        if (bit)
                            return -1;
                    }
                }
                cur += s[j];
            }
        }

        lit_src = lits;
        b_start = off_own + d + sum_s;
    }

    /* ---- sequence bitstream ---- */
    {
        breader B;
        size_t olen = 0;   /* |out| */
        unsigned used = 0; /* literals consumed */
        unsigned last = 1; /* last offset */
        int iter;

        br_init(&B, src, b_start, len - b_start);

        for (iter = 0; iter <= BLOCK; iter++) {
            unsigned ll, mlf, c;
            long mlv;
            unsigned ml;
            unsigned off;
            unsigned x;
            int token;

            if ((long)(n - used) == (long)(BLOCK - olen)) {
                /* last sequence: the literals left fill the rest */
                unsigned k = n - used;
                unsigned i;
                for (i = 0; i < k; i++) {
                    unsigned p = (unsigned)olen;
                    unsigned v = lit_src[used];
                    if (delta) {
                        unsigned xoff = (dt == 1) ? 4u : (dt == 2) ? 8u
                                        : (last == 1 || last == 2) ? 4u
                                        : (last == 3) ? 6u : last;
                        if (p >= xoff)
                            v = (v + out[p - xoff]) & 0xff;
                    }
                    out[olen++] = (unsigned char)v;
                    used++;
                }
                goto done;
            }

            token = br_symbol(&B, &tok_tab);
            if (token < 0)
                return -1;
            if (token == 3072) {
                token = (int)br_read(&B, 12);
                if (token >= 3072)
                    return -1;
            }
            ll = (unsigned)token & 15;
            mlf = ((unsigned)token >> 4) & 31;
            c = (unsigned)token >> 9;

            /* offset bits, then the offset */
            switch (c) {
            case 0: x = 0; off = last; break;
            case 1: x = br_read(&B, 1); off = x * 2; break;
            case 2: x = br_read(&B, 8); off = x; break;
            case 3: x = br_read(&B, P); off = x; break;
            case 4: x = br_read(&B, 5); off = x * 8; break;
            default: x = br_read(&B, 8); off = x * 8; break; /* class 5 */
            }

            if (ll == 15) {
                long v = br_value(&B, &ll_tab);
                if (v < 0)
                    return -1;
                ll = 15 + (unsigned)v;
            }
            if (ll > n - used || (long)ll > (long)(BLOCK - olen))
                return -1;

            /* copy(ll) */
            {
                unsigned i;
                unsigned xoff = 0;
                if (delta)
                    xoff = (dt == 1) ? 4u : (dt == 2) ? 8u
                           : (last == 1 || last == 2) ? 4u
                           : (last == 3) ? 6u : last;
                for (i = 0; i < ll; i++) {
                    unsigned p = (unsigned)olen;
                    unsigned v = lit_src[used];
                    if (delta && p >= xoff)
                        v = (v + out[p - xoff]) & 0xff;
                    out[olen++] = (unsigned char)v;
                    used++;
                }
            }

            ml = mlf + 4;
            if (mlf == 31) {
                mlv = br_value(&B, &ml_tab);
                if (mlv < 0)
                    return -1;
                ml = (unsigned)(35 + mlv);
            }

            last = off;
            if (off == 0 || (size_t)off > olen ||
                (long)ml > (long)(BLOCK - olen))
                return -1;

            {
                unsigned i;
                for (i = 0; i < ml; i++) {
                    out[olen] = out[olen - off];
                    olen++;
                }
            }
        }
        /* too many sequences: cannot be valid */
        return -1;

    done:
        if (olen != BLOCK)
            return -1;
        /* bits must end here: no bit read past B, fewer than 8 bits left,
         * all 0 (seqlz.rst, "The bitstream of the sequences"). */
        if (B.over)
            return -1;
        {
            size_t total = (len - b_start) * 8;
            size_t k;
            if (B.pos > total)
                return -1;
            if (total - B.pos >= 8)
                return -1;
            for (k = B.pos; k < total; k++) {
                int bit = (src[b_start + (k >> 3)] >> (7 - (k & 7))) & 1;
                if (bit)
                    return -1;
            }
        }
        return 0;
    }
}
