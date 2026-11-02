/* cr_write: a writer of seqlz compressed blocks, written from format.md. C11, standard library only.
 * It is not a compressor: it makes any block the format allows, each choice decided by the choice
 * bytes, see FEATURES.md for what each choice is and how it is picked. */
#include "cr.h"
#include "cr_tables.h"
#include <string.h>
#include <stdio.h>

#define BLOCK 4096
#define P 12

/* ---- features (FEATURES.md) ---- */
enum {
    F_LAYOUT = 0,                   /* raw, coded */
    F_CODE = F_LAYOUT + 2,          /* 0..8 */
    F_W = F_CODE + 9,               /* 5..12 */
    F_W_ABOVE_MIN = F_W + 8,
    F_E = F_W_ABOVE_MIN + 1,        /* 0, 1 */
    F_DT = F_E + 2,                 /* 1..3 */
    F_END = F_DT + 3,               /* literals only, last match reaches the end, last literals > 0 */
    F_EMPTY_STREAMS = F_END + 3,
    F_TOKCODE = F_EMPTY_STREAMS + 1,/* own code, escape of a token with a code, escape of one without */
    F_LLF = F_TOKCODE + 3,          /* 0..15 */
    F_MLF = F_LLF + 16,             /* 0..31 */
    F_CLASS = F_MLF + 32,           /* 0..5 */
    F_CLASS0_FIRST = F_CLASS + 6,
    F_LLSYM = F_CLASS0_FIRST + 1,   /* 0..23 */
    F_MLSYM = F_LLSYM + 24,         /* 0..23 */
    F_OFF = F_MLSYM + 24,           /* off 1, off == |out|, off < ml */
    F_DELTA = F_OFF + 3,            /* p < x as it is, delta added */
    F_DT3X = F_DELTA + 2,           /* x = 4, x = 6, x = last */
    F_OWNSYM = F_DT3X + 3,          /* 0..18 */
    F_OWN = F_OWNSYM + 19,          /* run split in several symbols, byte values after the last code, last code 255 */
    F_LITLEN = F_OWN + 3,           /* code length of a literal 1..10 */
    F_NSEQ = F_LITLEN + 10,         /* sequences with a token: 1, 2-15, 16-255, 256-1023 */
    F_NFEAT = F_NSEQ + 4
};

static const struct { int base, count, first; const char *name; const char *const *names; } fams[] = {
    {F_LAYOUT, 2, 0, "layout", (const char *const[]){"raw", "coded"}},
    {F_CODE, 9, 0, "code", NULL},
    {F_W, 8, 5, "w", NULL},
    {F_W_ABOVE_MIN, 1, 0, "w", (const char *const[]){"above-smallest"}},
    {F_E, 2, 0, "e", NULL},
    {F_DT, 3, 1, "dt", NULL},
    {F_END, 3, 0, "end", (const char *const[]){"literals-only", "match-reaches-end", "last-literals"}},
    {F_EMPTY_STREAMS, 1, 0, "coded", (const char *const[]){"empty-streams"}},
    {F_TOKCODE, 3, 0, "token", (const char *const[]){"own-code", "escape-has-code", "escape-no-code"}},
    {F_LLF, 16, 0, "ll-field", NULL},
    {F_MLF, 32, 0, "mlf", NULL},
    {F_CLASS, 6, 0, "class", NULL},
    {F_CLASS0_FIRST, 1, 0, "class", (const char *const[]){"0-first-sequence"}},
    {F_LLSYM, 24, 0, "LL-symbol", NULL},
    {F_MLSYM, 24, 0, "ML-symbol", NULL},
    {F_OFF, 3, 0, "offset", (const char *const[]){"1", "equals-out", "below-ml"}},
    {F_DELTA, 2, 0, "delta", (const char *const[]){"before-x-as-is", "added"}},
    {F_DT3X, 3, 0, "dt3-x", (const char *const[]){"4", "6", "last"}},
    {F_OWNSYM, 19, 0, "OWN-symbol", NULL},
    {F_OWN, 3, 0, "own", (const char *const[]){"run-split", "uncoded-after-last", "last-code-255"}},
    {F_LITLEN, 10, 1, "literal-code-length", NULL},
    {F_NSEQ, 4, 0, "sequences", (const char *const[]){"1", "2-15", "16-255", "256-1023"}},
};

int cr_feature_count(void) { return F_NFEAT; }

const char *cr_feature_name(int i) {
    static char buf[F_NFEAT][48];
    for (size_t f = 0; f < sizeof fams / sizeof fams[0]; f++) {
        if (i >= fams[f].base && i < fams[f].base + fams[f].count) {
            int k = i - fams[f].base;
            if (!buf[i][0]) {
                if (fams[f].names) snprintf(buf[i], sizeof buf[i], "%s.%s", fams[f].name, fams[f].names[k]);
                else snprintf(buf[i], sizeof buf[i], "%s.%d", fams[f].name, fams[f].first + k);
            }
            return buf[i];
        }
    }
    return "?";
}

/* ---- choice bytes ---- */
typedef struct { const unsigned char *c; size_t n, i; } reader;

static unsigned rbyte(reader *r) { unsigned v = r->i < r->n ? r->c[r->i] : 0; r->i++; return v; }

/* a number in [0, range), range 1 to 65536; reads no byte for range 1, one up to 256, else two */
static unsigned pick(reader *r, unsigned range) {
    if (range <= 1) return 0;
    if (range <= 256) return rbyte(r) % range;
    unsigned v = rbyte(r);
    v |= rbyte(r) << 8;
    return v % range;
}

/* a number in [lo, hi], 1 <= lo <= hi: first one of the ranges [2^b, 2^(b+1)) that meet it, then a
 * number in that part; so small and large numbers are both frequent */
static unsigned pick_log(reader *r, unsigned lo, unsigned hi) {
    unsigned blo = 0, bhi = 0;
    while ((2u << blo) <= lo) blo++;
    while ((2u << bhi) <= hi) bhi++;
    unsigned b = blo + pick(r, bhi - blo + 1);
    unsigned a = 1u << b, z = (2u << b) - 1;
    if (a < lo) a = lo;
    if (z > hi) z = hi;
    return a + pick(r, z - a + 1);
}

/* buckets of a length: one per way it is coded, see FEATURES.md */
typedef struct { unsigned lo, hi; } range;

/* picks one of the buckets[0..cap) that meet [lo, hi], or the lowest bucket that meets it if none
 * of those does, and with to_hi (only given with every bucket allowed) also the value hi as one more
 * bucket of its own; then a value in the bucket */
static unsigned pick_bucket(reader *r, const range *bk, int nb, int cap, unsigned lo, unsigned hi,
                            int to_hi) {
    int ok[64], k = 0;
    for (int i = 0; i < cap; i++)
        if (bk[i].lo <= hi && bk[i].hi >= lo) ok[k++] = i;
    if (k == 0)
        for (int i = 0; i < nb && k == 0; i++)
            if (bk[i].lo <= hi && bk[i].hi >= lo) ok[k++] = i;
    unsigned q = pick(r, (unsigned)k + (to_hi ? 1 : 0));
    if (q == (unsigned)k) return hi;
    int i = ok[q];
    unsigned a = bk[i].lo > lo ? bk[i].lo : lo, z = bk[i].hi < hi ? bk[i].hi : hi;
    return a + pick(r, z - a + 1);
}

/* ---- bits, most significant first ---- */
typedef struct { unsigned char *b; size_t cap, nbits; } bitw;

static void put(bitw *w, unsigned v, int n) {
    for (int i = n - 1; i >= 0; i--) {
        size_t byte = w->nbits >> 3;
        if (byte >= w->cap) { w->nbits++; continue; }  /* cannot happen with the sizes below */
        if ((w->nbits & 7) == 0) w->b[byte] = 0;
        if ((v >> i) & 1) w->b[byte] |= (unsigned char)(0x80 >> (w->nbits & 7));
        w->nbits++;
    }
}
static size_t bytes_of(const bitw *w) { return (w->nbits + 7) >> 3; }

/* ---- prefix codes as in "Prefix codes" ---- */
static void make_codes(const unsigned char *len, int nsym, unsigned short *code) {
    unsigned count[16] = {0}, first[16] = {0};
    for (int s = 0; s < nsym; s++) if (len[s]) count[len[s]]++;
    count[0] = 0;
    first[1] = 0;
    for (int l = 2; l < 16; l++) first[l] = (first[l - 1] + count[l - 1]) * 2;
    for (int s = 0; s < nsym; s++) if (len[s]) code[s] = (unsigned short)first[len[s]]++;
}

static unsigned short TOK_C[3073], LL_C[24], ML_C[24], LIT_C[8][256], OWN_C[19];
static int codes_made;
static void init_codes(void) {
    if (codes_made) return;
    make_codes(TOK_LEN, 3073, TOK_C);
    make_codes(LL_LEN, 24, LL_C);
    make_codes(ML_LEN, 24, ML_C);
    for (int t = 0; t < 8; t++) make_codes(LIT_LEN + 256 * t, 256, LIT_C[t]);
    make_codes(OWN_LEN, 19, OWN_C);
    codes_made = 1;
}

/* the symbol of a length value and its extra bits, "Length values" */
static int len_symbol(unsigned v, int *b, unsigned *extra) {
    if (v < 16) { *b = 0; *extra = 0; return (int)v; }
    int bb = 0;
    while ((2u << bb) <= v) bb++;
    *b = bb; *extra = v - (1u << bb);
    return bb + 12;
}

static void put_value(bitw *w, const unsigned char *len, const unsigned short *code, unsigned v,
                      unsigned long long *cnt, int fam) {
    int b; unsigned extra;
    int s = len_symbol(v, &b, &extra);
    put(w, code[s], len[s]);
    put(w, extra, b);
    if (cnt) cnt[fam + s]++;
}

static range LL_BK[39], ML_BK[55];
static void init_buckets(void) {
    /* ll: 0..14 in the token; 15 + value(LL): 16 symbols of one value, 8 of a range */
    for (int i = 0; i < 15; i++) LL_BK[i] = (range){(unsigned)i, (unsigned)i};
    for (int s = 0; s < 16; s++) LL_BK[15 + s] = (range){15u + s, 15u + s};
    for (int s = 16; s < 24; s++) LL_BK[15 + s] = (range){15u + (1u << (s - 12)), 15u + (2u << (s - 12)) - 1};
    /* ml: 4..34 in the token; 35 + value(ML) */
    for (int i = 0; i < 31; i++) ML_BK[i] = (range){4u + i, 4u + i};
    for (int s = 0; s < 16; s++) ML_BK[31 + s] = (range){35u + s, 35u + s};
    for (int s = 16; s < 24; s++) ML_BK[31 + s] = (range){35u + (1u << (s - 12)), 35u + (2u << (s - 12)) - 1};
}

#define CNT(i) do { if (cnt) cnt[(i)]++; } while (0)

size_t cr_write(const unsigned char *choices, size_t n, unsigned char *dst, size_t cap,
                unsigned char *block) {
    return cr_write_stats(choices, n, dst, cap, block, NULL);
}

size_t cr_write_stats(const unsigned char *choices, size_t nchoices, unsigned char *dst, size_t cap,
                      unsigned char *block, cr_stats *stats) {
    unsigned long long *cnt = stats ? stats->count : NULL;
    reader rd = {choices, nchoices, 0}, *r = &rd;
    init_codes();
    init_buckets();

    /* layout */
    int coded = (int)pick(r, 2);
    int code = 0, e = 0, dt = 0;
    if (coded) {
        code = (int)pick(r, 9);
        e = (int)pick(r, 2);
        if (e) dt = 1 + (int)pick(r, 3);
    }

    /* a block's own literal table */
    unsigned char own_len[256];
    unsigned short own_code[256];
    unsigned char own_desc[512];
    bitw ownw = {own_desc, sizeof own_desc, 0};
    int own_sym[256], own_m = 0;
    if (coded && code == 8) {
        int m = (int)pick_log(r, 2, 256);         /* byte values with a code, 2 to 256 */
        unsigned char depth[256];
        int leaves = 2;
        depth[0] = depth[1] = 1;
        while (leaves < m) {                      /* split a leaf: first its depth, below 10 */
            int dl[10], nd = 0;
            for (int dd = 1; dd < 10; dd++)
                for (int i = 0; i < leaves; i++) if (depth[i] == dd) { dl[nd++] = dd; break; }
            int dd = dl[pick(r, (unsigned)nd)];
            int cand[256], k = 0;
            for (int i = 0; i < leaves; i++) if (depth[i] == dd) cand[k++] = i;
            int i = cand[pick(r, (unsigned)k)];
            depth[i]++;
            depth[leaves++] = depth[i];
        }
        for (int i = m - 1; i > 0; i--) {         /* which length the i-th byte value with a code gets */
            int j = (int)pick(r, (unsigned)i + 1);
            unsigned char t = depth[i]; depth[i] = depth[j]; depth[j] = t;
        }
        memset(own_len, 0, sizeof own_len);
        int left = 256 - m, v0 = 0;
        for (int i = 0; i < m; i++) {             /* byte values without a code before each one */
            int gap = (int)pick_log(r, 1, (unsigned)left + 1) - 1;
            left -= gap;
            v0 += gap;
            own_len[v0++] = depth[i];
        }
        int lastv = 255;
        while (!own_len[lastv]) lastv--;
        for (int v = 0; v < 256; v++) if (own_len[v]) own_sym[own_m++] = v;
        /* the description */
        int prev_run = 0;
        for (int v = 0; v <= lastv;) {
            if (own_len[v]) {
                int s = own_len[v] - 1;
                put(&ownw, OWN_C[s], OWN_LEN[s]);
                CNT(F_OWNSYM + s);
                prev_run = 0;
                v++;
            } else {
                int z = 0;
                while (!own_len[v + z]) z++;
                v += z;
                while (z > 0) {
                    int kmax = 1;
                    while ((1 << kmax) <= z) kmax++;  /* 2^(kmax-1) <= z */
                    int k = 1 + (int)pick(r, (unsigned)kmax);
                    int base = 1 << (k - 1);
                    int rmax = base - 1 < z - base ? base - 1 : z - base;
                    int rr = (int)pick(r, (unsigned)rmax + 1);
                    put(&ownw, OWN_C[9 + k], OWN_LEN[9 + k]);
                    put(&ownw, (unsigned)rr, k - 1);
                    CNT(F_OWNSYM + 9 + k);
                    if (prev_run) CNT(F_OWN + 0);
                    prev_run = 1;
                    z -= base + rr;
                }
            }
        }
        CNT(lastv < 255 ? F_OWN + 1 : F_OWN + 2);
        make_codes(own_len, 256, own_code);
    }
    const unsigned char *lit_len = coded ? (code == 8 ? own_len : LIT_LEN + 256 * code) : NULL;
    const unsigned short *lit_code = coded ? (code == 8 ? own_code : LIT_C[code]) : NULL;

    /* how this block's sequences tend to be: chance to end, buckets of the lengths */
    int endk = (int)pick(r, 13);                  /* each step ends with chance 2^-endk */
    int llcap = pick(r, 2) ? (int)pick_log(r, 1, 39) : 39;   /* half the blocks: every bucket */
    int mlcap = pick(r, 2) ? (int)pick_log(r, 1, 55) : 55;

    unsigned char seqbuf[16384];           /* B: at most 1023 sequences of at most 72 bits */
    bitw bw = {seqbuf, sizeof seqbuf, 0};
    unsigned char stored[BLOCK];                  /* the literals as the layout stores them */
    unsigned nl = 0, pos = 0, last = 1, nseq = 0;

#define LITERAL() do {                                                              \
        unsigned p_ = pos, s_, x_ = 0;                                              \
        if (!coded) s_ = pick(r, 256);                                              \
        else if (code == 8) s_ = (unsigned)own_sym[pick(r, (unsigned)own_m)];      \
        else s_ = pick(r, 256);                                                     \
        unsigned o_ = s_;                                                           \
        if (e) {                                                                    \
            if (dt == 1) x_ = 4;                                                    \
            else if (dt == 2) x_ = 8;                                               \
            else {                                                                  \
                x_ = last <= 2 ? 4 : last == 3 ? 6 : last;                          \
                CNT(F_DT3X + (last <= 2 ? 0 : last == 3 ? 1 : 2));                  \
            }                                                                       \
            if (p_ >= x_) { o_ = (s_ + block[p_ - x_]) & 255; CNT(F_DELTA + 1); }   \
            else CNT(F_DELTA + 0);                                                  \
        }                                                                           \
        if (coded) CNT(F_LITLEN + lit_len[s_] - 1);                                 \
        stored[nl++] = (unsigned char)s_;                                           \
        block[pos++] = (unsigned char)o_;                                           \
    } while (0)

    for (;;) {
        unsigned rem = BLOCK - pos;
        int end = rem < 4 || pick(r, 1u << endk) == 0;
        if (end) {
            if (nseq == 0) CNT(F_END + 0);
            else CNT(rem == 0 ? F_END + 1 : F_END + 2);
            while (pos < BLOCK) LITERAL();
            break;
        }
        /* literals */
        unsigned ll = pick_bucket(r, LL_BK, 39, llcap, pos == 0 ? 1 : 0, rem - 4, 0);
        for (unsigned i = 0; i < ll; i++) LITERAL();
        /* offset */
        unsigned have = pos;
        int classes[6], nc = 0;
        classes[nc++] = 0;
        if (have >= 2) classes[nc++] = 1;
        classes[nc++] = 2;
        classes[nc++] = 3;
        if (have >= 8) { classes[nc++] = 4; classes[nc++] = 5; }
        int c = classes[pick(r, (unsigned)nc)];
        unsigned off, x = 0;
        switch (c) {
        case 0: off = last; break;
        case 1: off = 2; x = 1; break;
        case 2: off = x = pick_log(r, 1, have < 255 ? have : 255); break;
        case 3: off = x = pick_log(r, 1, have < 4095 ? have : 4095); break;
        case 4: x = pick_log(r, 1, have / 8 < 31 ? have / 8 : 31); off = 8 * x; break;
        default: x = pick_log(r, 1, have / 8 < 255 ? have / 8 : 255); off = 8 * x; break;
        }
        /* match length */
        unsigned ml = pick_bucket(r, ML_BK, 55, mlcap, 4, BLOCK - pos, mlcap == 55);
        unsigned llf = ll < 15 ? ll : 15, mlf = ml - 4 < 31 ? ml - 4 : 31;
        unsigned token = llf | mlf << 4 | (unsigned)c << 9;
        int esc = TOK_LEN[token] ? (int)pick(r, 2) : 1;
        if (esc) {
            put(&bw, TOK_C[3072], TOK_LEN[3072]);
            put(&bw, token, 12);
            CNT(TOK_LEN[token] ? F_TOKCODE + 1 : F_TOKCODE + 2);
        } else {
            put(&bw, TOK_C[token], TOK_LEN[token]);
            CNT(F_TOKCODE + 0);
        }
        static const int obits[6] = {0, 1, 8, P, 5, 8};
        put(&bw, x, obits[c]);
        if (ll >= 15) put_value(&bw, LL_LEN, LL_C, ll - 15, cnt, F_LLSYM);
        if (ml >= 35) put_value(&bw, ML_LEN, ML_C, ml - 35, cnt, F_MLSYM);
        CNT(F_LLF + llf); CNT(F_MLF + mlf); CNT(F_CLASS + c);
        if (c == 0 && nseq == 0) CNT(F_CLASS0_FIRST);
        if (off == 1) CNT(F_OFF + 0);
        if (off == pos) CNT(F_OFF + 1);
        if (off < ml) CNT(F_OFF + 2);
        for (unsigned i = 0; i < ml; i++, pos++) block[pos] = block[pos - off];
        last = off;
        nseq++;
    }
    CNT(F_LAYOUT + coded);
    if (nseq) CNT(F_NSEQ + (nseq == 1 ? 0 : nseq < 16 ? 1 : nseq < 256 ? 2 : 3));

    /* assemble */
    size_t len = 0;
    size_t bbytes = bytes_of(&bw);
    if (!coded) {
        len = 2 + nl + bbytes;
        if (len > cap) return 0;
        dst[0] = (unsigned char)(nl & 255);
        dst[1] = (unsigned char)(nl >> 8);
        memcpy(dst + 2, stored, nl);
        memcpy(dst + 2 + nl, seqbuf, bbytes);
        return len;
    }
    unsigned char sbuf[8][1024];           /* a stream: at most 512 codes of 10 bits */
    bitw sw[8];
    for (int j = 0; j < 8; j++) sw[j] = (bitw){sbuf[j], sizeof sbuf[j], 0};
    for (unsigned i = 0; i < nl; i++) put(&sw[i & 7], lit_code[stored[i]], lit_len[stored[i]]);
    unsigned s[8], smax = 0;
    size_t stot = 0;
    for (int j = 0; j < 8; j++) {
        s[j] = (unsigned)bytes_of(&sw[j]);
        if (s[j] > smax) smax = s[j];
        stot += s[j];
    }
    int wmin = 5;
    while ((1u << wmin) <= smax) wmin++;
    int w = wmin + (int)pick(r, (unsigned)(13 - wmin));
    CNT(F_W + w - 5);
    if (w > wmin) CNT(F_W_ABOVE_MIN);
    CNT(F_CODE + code);
    CNT(F_E + e);
    if (e) CNT(F_DT + dt - 1);
    if (nl < 8) CNT(F_EMPTY_STREAMS);
    size_t d = bytes_of(&ownw);
    len = 3 + (size_t)e + (size_t)w + d + stot + bbytes;
    if (len > cap) return 0;
    size_t o = 0;
    dst[o++] = (unsigned char)(nl & 255);
    dst[o++] = (unsigned char)(0x80 | nl >> 8);
    dst[o++] = (unsigned char)(code | (w - 5) << 4 | e << 7);
    if (e) dst[o++] = (unsigned char)dt;
    for (int i = 0; i < w; i++) {                 /* S: s[j] at bits j*w, little endian */
        unsigned v = 0;
        for (int bit = 0; bit < 8; bit++) {
            int k = 8 * i + bit, j = k / w, sb = k % w;
            if ((s[j] >> sb) & 1) v |= 1u << bit;
        }
        dst[o++] = (unsigned char)v;
    }
    memcpy(dst + o, own_desc, d); o += d;
    for (int j = 0; j < 8; j++) { memcpy(dst + o, sbuf[j], s[j]); o += s[j]; }
    memcpy(dst + o, seqbuf, bbytes); o += bbytes;
    return o;
}
