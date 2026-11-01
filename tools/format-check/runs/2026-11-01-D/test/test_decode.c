/* Tests for cr_decode, written from format.md: its example, blocks made by hand for each rule (valid
 * and invalid), random valid blocks from a small encoder written from format.md, and damaged blocks. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../cr_decode.h"
#include "../cr_tables.h"

#define BLOCK 4096
#define MAXOUT (BLOCK * 4)

static int failures, checks;

#define CHECK(cond, ...)                                                                      \
    do {                                                                                      \
        checks++;                                                                             \
        if (!(cond)) {                                                                        \
            failures++;                                                                       \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                       \
            printf(__VA_ARGS__);                                                              \
            printf("\n");                                                                     \
        }                                                                                     \
    } while (0)

/* ---------- random ---------- */
static uint64_t rng = 0x9e3779b97f4a7c15ull;
static uint32_t rnd(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 7;
    rng ^= rng << 17;
    return (uint32_t)(rng >> 16);
}
static unsigned rr(unsigned lo, unsigned hi) { return lo + rnd() % (hi - lo + 1); } /* [lo, hi] */

/* ---------- bit writer, most significant bit first ---------- */
typedef struct {
    unsigned char b[MAXOUT];
    size_t bits;
} bw_t;
static void bw_put(bw_t *w, uint32_t v, unsigned n)
{
    for (unsigned i = n; i-- > 0;) {
        unsigned bit = (v >> i) & 1;
        size_t at = w->bits / 8;
        if (w->bits % 8 == 0)
            w->b[at] = 0;
        w->b[at] |= (unsigned char)(bit << (7 - w->bits % 8));
        w->bits++;
    }
}
static size_t bw_bytes(const bw_t *w) { return (w->bits + 7) / 8; }

/* ---------- canonical codes, format.md "Prefix codes" ---------- */
typedef struct {
    uint32_t code[3073];
    unsigned char len[3073];
} code_t;
static void make_codes(const unsigned char *lens, unsigned nsym, code_t *c)
{
    unsigned count[32] = {0}, first[32] = {0};
    for (unsigned s = 0; s < nsym; s++)
        count[lens[s]]++;
    count[0] = 0;
    first[1] = 0;
    for (unsigned l = 2; l < 32; l++)
        first[l] = (first[l - 1] + count[l - 1]) * 2;
    for (unsigned s = 0; s < nsym; s++) {
        c->len[s] = lens[s];
        c->code[s] = lens[s] ? first[lens[s]]++ : 0;
    }
}
static code_t C_TOK, C_LL, C_ML, C_OWN, C_LIT[8];
static void put_sym(bw_t *w, const code_t *c, unsigned s)
{
    if (!c->len[s]) {
        printf("test bug: symbol %u has no code\n", s);
        exit(2);
    }
    bw_put(w, c->code[s], c->len[s]);
}
static void put_value(bw_t *w, const code_t *c, unsigned v)
{
    if (v < 16) {
        put_sym(w, c, v);
        return;
    }
    unsigned b = 0;
    while ((2u << b) <= v)
        b++;
    put_sym(w, c, b + 12);
    bw_put(w, v - (1u << b), b);
}

/* ---------- a plan of a block ---------- */
typedef struct {
    unsigned ll, ml, off, cls;
    int esc;           /* send the token escaped */
} seq_t;

typedef struct {
    int coded, code, e, dt, w;   /* w 0: the smallest that holds every size, at least 5 */
    unsigned char own_len[256];  /* with code 8 */
    int own_split_skips;         /* describe runs without code as skips of 1 (valid, longer) */
    unsigned nseq;
    seq_t seq[BLOCK / 4 + 2];
    unsigned n;
    unsigned char lits[BLOCK]; /* as stored: deltas when e = 1 */
    /* damage, for blocks made invalid on purpose */
    int raw_t, raw_dt;          /* >= 0: write this byte as t / dt instead */
    int stream_extra[8];        /* bytes of 0 added to stream j (and to s[j]) */
    int stream_pad_one;         /* set the padding bits of the first stream that has them */
    int own_pad_one;            /* set the padding bits of the own table */
    int b_pad_one;              /* set the padding bits of B */
    int b_extra;                /* bytes added at the end of B */
    unsigned b_extra_val;
    int extra_token;            /* a token after the last sequence (escape of token 0) */
    int force_s0;               /* >= 0: write s[0] as this */
} plan_t;

static void plan_init(plan_t *p)
{
    memset(p, 0, sizeof *p);
    p->raw_t = p->raw_dt = -1;
    p->force_s0 = -1;
}

/* Writes the own-table description of p->own_len. Returns the bit count. */
static void put_own(bw_t *w, const plan_t *p)
{
    int lastv = -1;
    for (int v = 0; v < 256; v++)
        if (p->own_len[v])
            lastv = v;
    unsigned v = 0;
    while ((int)v <= lastv) {
        if (p->own_len[v]) {
            put_sym(w, &C_OWN, p->own_len[v] - 1u);
            v++;
            continue;
        }
        unsigned r = 0;
        while (!p->own_len[v + r])
            r++;
        v += r;
        while (r) {
            unsigned chunk = p->own_split_skips ? 1 : (r > 511 ? 511 : r);
            unsigned k = 1;
            while ((1u << k) <= chunk)
                k++; /* 2^(k-1) <= chunk < 2^k */
            put_sym(w, &C_OWN, 9 + k);
            bw_put(w, chunk - (1u << (k - 1)), k - 1);
            r -= chunk;
        }
    }
}

/* Builds the compressed block. Returns its length. */
static size_t encode(const plan_t *p, unsigned char *dst)
{
    size_t pos = 0;
    dst[pos++] = (unsigned char)(p->n & 0xff);
    dst[pos++] = (unsigned char)((p->n >> 8) | (p->coded ? 0x80 : 0));
    if (!p->coded) {
        memcpy(dst + pos, p->lits, p->n);
        pos += p->n;
    } else {
        const code_t *lc = p->code == 8 ? &C_LIT[0] : &C_LIT[p->code & 7];
        static code_t own;
        if (p->code == 8) {
            make_codes(p->own_len, 256, &own);
            lc = &own;
        }
        static bw_t st[8];
        size_t s[8];
        for (int j = 0; j < 8; j++) {
            st[j].bits = 0;
            for (unsigned i = (unsigned)j; i < p->n; i += 8)
                put_sym(&st[j], lc, p->lits[i]);
            if (p->stream_pad_one && st[j].bits % 8) {
                bw_put(&st[j], (1u << (8 - st[j].bits % 8)) - 1, 8 - st[j].bits % 8);
                ((plan_t *)p)->stream_pad_one = 0; /* only the first one */
            }
            s[j] = bw_bytes(&st[j]);
            for (int k = 0; k < p->stream_extra[j]; k++)
                st[j].b[s[j]++] = 0;
        }
        if (p->force_s0 >= 0)
            s[0] = (size_t)p->force_s0;
        unsigned w = (unsigned)p->w;
        if (!w) {
            w = 5;
            for (int j = 0; j < 8; j++)
                while (s[j] >= (1u << w))
                    w++;
        }
        unsigned t = (unsigned)p->code | ((w - 5) << 4) | (p->e ? 0x80 : 0);
        dst[pos++] = (unsigned char)(p->raw_t >= 0 ? (unsigned)p->raw_t : t);
        if (p->e)
            dst[pos++] = (unsigned char)(p->raw_dt >= 0 ? p->raw_dt : p->dt);
        /* S: 8 * w bits, little endian, s[j] at bit j * w */
        memset(dst + pos, 0, w);
        for (unsigned j = 0; j < 8; j++)
            for (unsigned i = 0; i < w; i++)
                if ((s[j] >> i) & 1)
                    dst[pos + (j * w + i) / 8] |= (unsigned char)(1u << ((j * w + i) % 8));
        pos += w;
        if (p->code == 8) {
            static bw_t ot;
            ot.bits = 0;
            put_own(&ot, p);
            if (p->own_pad_one && ot.bits % 8)
                bw_put(&ot, (1u << (8 - ot.bits % 8)) - 1, 8 - ot.bits % 8);
            memcpy(dst + pos, ot.b, bw_bytes(&ot));
            pos += bw_bytes(&ot);
        }
        for (int j = 0; j < 8; j++) {
            size_t sz = (j == 0 && p->force_s0 >= 0) ? bw_bytes(&st[0]) : s[j];
            memcpy(dst + pos, st[j].b, sz);
            pos += sz;
        }
    }
    static bw_t b;
    b.bits = 0;
    for (unsigned i = 0; i < p->nseq; i++) {
        const seq_t *q = &p->seq[i];
        unsigned tll = q->ll < 15 ? q->ll : 15;
        unsigned tml = q->ml - 4 < 31 ? q->ml - 4 : 31;
        unsigned tok = tll | tml << 4 | q->cls << 9;
        if (q->esc || !C_TOK.len[tok]) {
            put_sym(&b, &C_TOK, 3072);
            bw_put(&b, tok, 12);
        } else {
            put_sym(&b, &C_TOK, tok);
        }
        switch (q->cls) {
        case 1: bw_put(&b, q->off / 2, 1); break;
        case 2: bw_put(&b, q->off, 8); break;
        case 3: bw_put(&b, q->off, 12); break;
        case 4: bw_put(&b, q->off / 8, 5); break;
        case 5: bw_put(&b, q->off / 8, 8); break;
        default: break;
        }
        if (q->ll >= 15)
            put_value(&b, &C_LL, q->ll - 15);
        if (q->ml >= 35)
            put_value(&b, &C_ML, q->ml - 35);
    }
    if (p->extra_token) {
        put_sym(&b, &C_TOK, 3072);
        bw_put(&b, 0, 12);
    }
    if (p->b_pad_one && b.bits % 8)
        bw_put(&b, (1u << (8 - b.bits % 8)) - 1, 8 - b.bits % 8);
    memcpy(dst + pos, b.b, bw_bytes(&b));
    pos += bw_bytes(&b);
    for (int k = 0; k < p->b_extra; k++)
        dst[pos++] = (unsigned char)p->b_extra_val;
    return pos;
}

/* The model: what a plan decodes to, straight from "Decoding" and "Literals as deltas". */
static void model(const plan_t *p, unsigned char *out)
{
    size_t o = 0, used = 0;
    unsigned last = 1;
    for (unsigned i = 0; i <= p->nseq; i++) {
        unsigned ll = i < p->nseq ? p->seq[i].ll : p->n - (unsigned)used;
        size_t x = p->dt == 1 ? 4 : p->dt == 2 ? 8 : last == 1 || last == 2 ? 4 : last == 3 ? 6 : last;
        for (unsigned k = 0; k < ll; k++, o++, used++) {
            unsigned char v = p->lits[used];
            if (p->coded && p->e && o >= x)
                v = (unsigned char)(v + out[o - x]);
            out[o] = v;
        }
        if (i == p->nseq)
            break;
        unsigned off = p->seq[i].cls == 0 ? last : p->seq[i].off;
        last = off;
        for (unsigned k = 0; k < p->seq[i].ml; k++, o++)
            out[o] = out[o - off];
    }
    if (o != BLOCK) {
        printf("test bug: plan writes %zu bytes\n", o);
        exit(2);
    }
}

/* ---------- plans ---------- */

/* A random own table: a random complete code of m leaves, at most 10 bits, on random byte values. */
static void random_own(plan_t *p, unsigned char *alpha, unsigned *nalpha)
{
    unsigned m = rr(2, rnd() % 4 ? 40 : 256);
    unsigned char lens[256];
    unsigned cnt = 2;
    lens[0] = lens[1] = 1;
    unsigned tries = 0;
    while (cnt < m && tries++ < 100000) {
        unsigned i = rnd() % cnt;
        if (lens[i] >= 10)
            continue;
        lens[i]++;
        lens[cnt++] = lens[i];
    }
    m = cnt;
    unsigned char vals[256];
    for (unsigned v = 0; v < 256; v++)
        vals[v] = (unsigned char)v;
    for (unsigned v = 255; v > 0; v--) {
        unsigned j = rnd() % (v + 1);
        unsigned char t = vals[v];
        vals[v] = vals[j];
        vals[j] = t;
    }
    memset(p->own_len, 0, 256);
    for (unsigned i = 0; i < m; i++)
        p->own_len[vals[i]] = lens[i];
    *nalpha = m;
    memcpy(alpha, vals, m);
}

static void random_plan(plan_t *p)
{
    plan_init(p);
    unsigned char alpha[256];
    unsigned nalpha = 256;
    for (unsigned v = 0; v < 256; v++)
        alpha[v] = (unsigned char)v;
    unsigned kind = rnd() % 4;
    if (kind) {
        p->coded = 1;
        p->code = kind == 3 ? 8 : (int)(rnd() % 8);
        if (p->code == 8)
            random_own(p, alpha, &nalpha);
        p->e = rnd() & 1;
        p->dt = p->e ? (int)rr(1, 3) : 0;
        if (rnd() % 8 == 0)
            p->w = 12;
        p->own_split_skips = rnd() % 8 == 0;
    } else if (rnd() % 2) {
        nalpha = rr(1, 8);
    }
    unsigned o = 0, last = 1;
    unsigned style = rnd() % 4;
    while (o < BLOCK && p->nseq < BLOCK / 4 + 1) {
        if (rnd() % 64 == 0)
            break;
        unsigned ll = style == 0 ? rr(0, 3) : rnd() % 8 ? rr(0, 20) : rr(0, 400);
        if (o + ll == 0)
            ll = 1;
        if (o + ll + 4 > BLOCK)
            break;
        unsigned room = BLOCK - o - ll;
        unsigned ml = rnd() % 8 ? rr(4, 40) : rr(4, 4096);
        if (ml > room)
            ml = room;
        unsigned avail = o + ll;
        seq_t q = {ll, ml, 0, 0, rnd() % 16 == 0};
        for (int tries = 0; tries < 20; tries++) {
            unsigned c = rnd() % 6, off = 0;
            switch (c) {
            case 0: off = last; break;
            case 1: off = 2; break;
            case 2: off = rr(1, 255); break;
            case 3: off = rr(1, 4095); break;
            case 4: off = 8 * rr(1, 31); break;
            case 5: off = 8 * rr(1, 255); break;
            }
            if (off <= avail) {
                q.cls = c;
                q.off = off;
                break;
            }
        }
        if (!q.off) {
            q.cls = 2;
            q.off = 1;
        }
        p->seq[p->nseq++] = q;
        last = q.off;
        o += ll + ml;
        p->n += ll;
    }
    p->n += BLOCK - o;
    for (unsigned i = 0; i < p->n; i++)
        p->lits[i] = alpha[rnd() % 3 == 0 ? 0 : rnd() % nalpha];
}

/* ---------- helpers ---------- */
static unsigned char blk[MAXOUT + 16];
static unsigned char got[BLOCK], want[BLOCK];

static int dec(const unsigned char *src, size_t len)
{
    /* copy to a buffer of exactly len bytes, so that ASan sees any read past the end */
    unsigned char *c = calloc(len ? len : 1, 1);
    if (len)
        memcpy(c, src, len);
    int r = cr_decode(c, len, got);
    free(c);
    return r;
}

static int plan_valid(plan_t *p, const char *what)
{
    model(p, want);
    size_t len = encode(p, blk);
    int r = dec(blk, len);
    CHECK(r == 0, "%s: valid block rejected", what);
    if (r == 0)
        CHECK(memcmp(got, want, BLOCK) == 0, "%s: wrong output", what);
    return r == 0;
}
static void plan_invalid(plan_t *p, const char *what)
{
    size_t len = encode(p, blk);
    CHECK(dec(blk, len) == -1, "%s: invalid block accepted", what);
}

/* A simple plan: "ab" then off 2 for the rest, as the example. */
static void plan_ab(plan_t *p)
{
    plan_init(p);
    p->n = 2;
    p->lits[0] = 'a';
    p->lits[1] = 'b';
    p->nseq = 1;
    p->seq[0] = (seq_t){2, BLOCK - 2, 2, 1, 0};
}

/* A plan with several sequences and coded literals. */
static void plan_mixed(plan_t *p, int code)
{
    plan_init(p);
    p->coded = 1;
    p->code = code;
    if (code == 8) {
        memset(p->own_len, 0, 256);
        /* 'a' 1, 'b' 2, 'c' 3, 250 3: 1/2 + 1/4 + 1/8 + 1/8 = 1 */
        p->own_len['a'] = 1;
        p->own_len['b'] = 2;
        p->own_len['c'] = 3;
        p->own_len[250] = 3;
    }
    const char *L = "abcabcbaabcaacbbca"; /* 18 literals: 10 then 8 */
    p->n = 18 + 30;
    for (unsigned i = 0; i < 18; i++)
        p->lits[i] = (unsigned char)L[i];
    for (unsigned i = 18; i < p->n; i++)
        p->lits[i] = i % 3 ? 'a' : 250;
    p->nseq = 3;
    p->seq[0] = (seq_t){10, 20, 3, 2, 0};
    p->seq[1] = (seq_t){8, 100, 3, 0, 0};
    p->seq[2] = (seq_t){0, BLOCK - 138 - 30, 16, 4, 0};
}

/* ---------- the tests ---------- */

static void test_example(void)
{
    static const unsigned char ex[] = {0x02, 0x00, 0x61, 0x62, 0x23, 0xf2, 0xff, 0xfb, 0x60};
    int r = dec(ex, sizeof ex);
    CHECK(r == 0, "example rejected");
    int ok = 1;
    for (int i = 0; i < BLOCK; i++)
        ok &= got[i] == (i % 2 ? 'b' : 'a');
    CHECK(r == 0 && ok, "example output is not ab 2048 times");
    /* our encoder makes the same bytes (the token 1010 has no code, so it is escaped) */
    plan_t p;
    plan_ab(&p);
    size_t len = encode(&p, blk);
    CHECK(len == sizeof ex && memcmp(blk, ex, len) == 0, "encoder does not reproduce the example");
    /* each cut and each appended byte is invalid; every bit flip must not crash */
    for (size_t l = 0; l < sizeof ex; l++)
        CHECK(dec(ex, l) == -1, "example cut to %zu bytes accepted", l);
    unsigned char ex2[sizeof ex + 1];
    memcpy(ex2, ex, sizeof ex);
    ex2[sizeof ex] = 0;
    CHECK(dec(ex2, sizeof ex2) == -1, "example with a zero byte appended accepted");
    /* the last 5 bits are padding: setting any of them is invalid */
    for (int bit = 0; bit < 5; bit++) {
        memcpy(ex2, ex, sizeof ex);
        ex2[8] ^= (unsigned char)(1 << bit);
        CHECK(dec(ex2, sizeof ex) == -1, "example with padding bit %d set accepted", bit);
    }
}

static void test_layout(void)
{
    unsigned char b[4200];
    plan_t p;
    CHECK(dec(b, 0) == -1, "len 0 accepted");
    b[0] = 0;
    CHECK(dec(b, 1) == -1, "len 1 accepted");

    /* raw, literals only: n = BLOCK, no bits in B */
    plan_init(&p);
    p.n = BLOCK;
    for (unsigned i = 0; i < BLOCK; i++)
        p.lits[i] = (unsigned char)(i * 7);
    plan_valid(&p, "raw literals only");
    size_t len = encode(&p, blk);
    CHECK(len == 2 + BLOCK, "raw literals-only length");
    CHECK(dec(blk, len - 1) == -1, "raw: 2 + n > len accepted");
    blk[len] = 0;
    CHECK(dec(blk, len + 1) == -1, "raw literals only with a byte of B accepted");
    /* n > BLOCK, raw and coded */
    memset(b, 0, sizeof b);
    b[0] = 0x01;
    b[1] = 0x10; /* n = 4097 */
    CHECK(dec(b, 2 + 4097) == -1, "raw n = 4097 accepted");
    b[1] = 0x7f;
    b[0] = 0xff;
    CHECK(dec(b, sizeof b) == -1, "raw n = 32767 accepted");
    b[0] = 0x01;
    b[1] = 0x90;
    CHECK(dec(b, sizeof b) == -1, "coded n = 4097 accepted");

    /* n = 0: the first match has no byte before it */
    memset(b, 0, sizeof b);
    for (unsigned l = 0; l < 64; l++)
        CHECK(dec(b, l) == -1, "n = 0 block of %u zero bytes accepted", l);

    /* coded, literals only, each fixed table, each w, and the own table */
    for (int code = 0; code <= 8; code++) {
        plan_init(&p);
        p.coded = 1;
        p.code = code;
        if (code == 8) {
            for (int v = 0; v < 256; v++)
                p.own_len[v] = 8;
        }
        p.n = BLOCK;
        for (unsigned i = 0; i < BLOCK; i++)
            p.lits[i] = (unsigned char)(i % 97 < 50 ? 0 : i * 13);
        char what[64];
        snprintf(what, sizeof what, "coded literals only, code %d", code);
        plan_valid(&p, what);
        /* t with a code 9 to 15 */
        for (int c = 9; c <= 15; c++) {
            p.raw_t = (int)(c | ((encode(&p, blk), blk[2]) & 0xf0));
            snprintf(what, sizeof what, "code %d", c);
            plan_invalid(&p, what);
        }
        p.raw_t = -1;
        /* cut */
        len = encode(&p, blk);
        CHECK(dec(blk, len - 1) == -1, "coded literals only cut by one byte accepted");
    }

    /* every w from 5 to 12 holds these sizes; w larger than needed is valid */
    for (int w = 5; w <= 12; w++) {
        plan_mixed(&p, 1);
        p.w = w;
        char what[64];
        snprintf(what, sizeof what, "w = %d", w);
        plan_valid(&p, what);
    }

    /* len < 3 + w + e: header cut short */
    plan_mixed(&p, 2);
    p.e = 1;
    p.dt = 2;
    len = encode(&p, blk);
    for (size_t l = 2; l < 3 + 5 + 1 + 1; l++)
        CHECK(dec(blk, l) == -1, "coded header cut to %zu accepted", l);

    /* dt: 1, 2, 3 valid, others invalid */
    for (int dt = 0; dt < 256; dt++) {
        plan_mixed(&p, 3);
        p.e = 1;
        p.dt = dt >= 1 && dt <= 3 ? dt : 1;
        p.raw_dt = dt;
        char what[64];
        snprintf(what, sizeof what, "dt = %d", dt);
        if (dt >= 1 && dt <= 3)
            plan_valid(&p, what);
        else
            plan_invalid(&p, what);
    }

    /* the streams */
    for (int j = 0; j < 8; j++) {
        plan_mixed(&p, 4);
        p.stream_extra[j] = 1;
        plan_invalid(&p, "a stream one zero byte longer than its codes");
    }
    plan_mixed(&p, 4);
    p.stream_pad_one = 1;
    plan_invalid(&p, "a stream with padding bits set");
    plan_mixed(&p, 4);
    len = encode(&p, blk);
    {
        /* s[0] one byte shorter: its last code runs into stream 1 */
        unsigned w = 5 + ((blk[2] >> 4) & 7);
        unsigned s0 = blk[3] & ((1u << w) - 1);
        p.force_s0 = (int)s0 - 1;
        plan_invalid(&p, "s[0] one byte short");
        p.force_s0 = (int)s0 + 1;
        plan_invalid(&p, "s[0] one byte long");
    }
    /* sum of the sizes past len */
    plan_init(&p);
    p.coded = 1;
    p.n = BLOCK;
    memset(p.lits, 0, BLOCK);
    len = encode(&p, blk);
    CHECK(dec(blk, len) == 0, "literals only, coded zeros");
    CHECK(len > 0 && dec(blk, len - 1) == -1, "streams past len accepted");

    /* a stream without literals must have size 0: n = 3, streams 3 to 7 are empty */
    plan_init(&p);
    p.coded = 1;
    p.code = 0;
    p.n = 3;
    p.lits[0] = 'x';
    p.lits[1] = 'y';
    p.lits[2] = 'z';
    p.nseq = 1;
    p.seq[0] = (seq_t){3, BLOCK - 3, 3, 2, 0};
    plan_valid(&p, "n = 3, five empty streams");
    for (int j = 3; j < 8; j++) {
        p.stream_extra[j] = 1;
        plan_invalid(&p, "an empty stream with size 1");
        p.stream_extra[j] = 0;
    }
}

static void test_own_table(void)
{
    plan_t p;
    plan_mixed(&p, 8);
    plan_valid(&p, "own table 4 codes");
    p.own_split_skips = 1;
    plan_valid(&p, "own table, runs as skips of 1");
    p.e = 1;
    p.dt = 3;
    plan_valid(&p, "own table with deltas, dt 3");
    p.e = 0;
    p.own_split_skips = 0;
    p.own_pad_one = 1;
    plan_invalid(&p, "own table with padding bits set");

    /* lengths 1 and 10: every length symbol */
    plan_init(&p);
    p.coded = 1;
    p.code = 8;
    /* byte 0: 1, byte 1: 2, ..., byte 8: 9, bytes 9 and 10: 10 */
    for (int v = 0; v <= 8; v++)
        p.own_len[v] = (unsigned char)(v + 1);
    p.own_len[9] = p.own_len[10] = 10;
    p.n = BLOCK;
    for (unsigned i = 0; i < BLOCK; i++)
        p.lits[i] = (unsigned char)(i % 11);
    plan_valid(&p, "own table with lengths 1 to 10");
    /* the last code on byte 255, a skip of 511 is not needed but 254 skipped values are */
    plan_init(&p);
    p.coded = 1;
    p.code = 8;
    p.own_len[0] = 1;
    p.own_len[255] = 1;
    p.n = BLOCK;
    for (unsigned i = 0; i < BLOCK; i++)
        p.lits[i] = i % 3 ? 0 : 255;
    plan_valid(&p, "own table: codes on 0 and 255");
    encode(&p, blk);
    {
        /* own table cut short: 3 + w + e + d > len (the block ends inside the table) */
        unsigned w = 5 + ((blk[2] >> 4) & 7);
        for (size_t l = 3 + w; l < 3 + w + 3; l++)
            CHECK(dec(blk, l) == -1, "own table cut to %zu accepted", l);
    }

    /* the sum goes above 1: by hand. byte 0 length 1, byte 1 length 2, byte 2 length 1 */
    {
        bw_t w;
        w.bits = 0;
        put_sym(&w, &C_OWN, 0); /* 1/2 */
        put_sym(&w, &C_OWN, 1); /* 3/4 */
        put_sym(&w, &C_OWN, 0); /* 5/4 */
        put_sym(&w, &C_OWN, 1);
        unsigned char b[64] = {0x00, 0x80 | 0x00, 0x08 /* code 8, w 5 */, 0, 0, 0, 0, 0};
        memcpy(b + 8, w.b, bw_bytes(&w));
        size_t l = 8 + bw_bytes(&w) + 8;
        CHECK(dec(b, l) == -1, "own table above 1 accepted");
    }
    /* a byte value above 255 with a code: skip 511 (k = 9, x = 255), then length 1 twice */
    {
        bw_t w;
        w.bits = 0;
        put_sym(&w, &C_OWN, 9 + 9);
        bw_put(&w, 255, 8);
        put_sym(&w, &C_OWN, 0);
        put_sym(&w, &C_OWN, 0);
        unsigned char b[64] = {0x00, 0x80, 0x08, 0, 0, 0, 0, 0};
        memcpy(b + 8, w.b, bw_bytes(&w));
        CHECK(dec(b, 8 + bw_bytes(&w) + 8) == -1, "own table code on byte 511 accepted");
        /* skip 255 then 1 then 1: byte 255 gets a code, byte 256 too */
        w.bits = 0;
        put_sym(&w, &C_OWN, 9 + 8);
        bw_put(&w, 127, 7);
        put_sym(&w, &C_OWN, 0);
        put_sym(&w, &C_OWN, 0);
        memcpy(b + 8, w.b, bw_bytes(&w));
        CHECK(dec(b, 8 + bw_bytes(&w) + 8) == -1, "own table code on byte 256 accepted");
        /* the same with a skip of 254: codes on bytes 254 and 255, valid table; with n = 0 the
         * block is invalid anyway, so use a block of literals */
    }
    /* random own tables */
    for (int i = 0; i < 300; i++) {
        plan_init(&p);
        p.coded = 1;
        p.code = 8;
        unsigned char alpha[256];
        unsigned na;
        random_own(&p, alpha, &na);
        p.n = BLOCK;
        for (unsigned k = 0; k < BLOCK; k++)
            p.lits[k] = alpha[rnd() % na];
        p.own_split_skips = rnd() & 1;
        plan_valid(&p, "random own table, literals only");
    }
}

static void test_sequences(void)
{
    plan_t p;
    /* escape of a token that has its own code means the same */
    plan_init(&p);
    p.n = 1;
    p.lits[0] = 'z';
    p.nseq = 1;
    p.seq[0] = (seq_t){1, 20, 1, 0, 0}; /* class 0: the last offset, 1 at the start */
    p.seq[0].ml = 20;
    unsigned tok = 1 | 16 << 4;
    CHECK(C_TOK.len[tok] != 0, "test assumes token %u has a code", tok);
    /* fill the rest with matches of off 1 */
    p.nseq = 1;
    unsigned o = 21;
    while (BLOCK - o >= 4) {
        unsigned ml = BLOCK - o > 2000 ? 2000 : BLOCK - o;
        p.seq[p.nseq++] = (seq_t){0, ml, 1, 0, 0};
        o += ml;
    }
    p.n += BLOCK - o;
    for (unsigned i = 1; i < p.n; i++)
        p.lits[i] = 'q';
    plan_valid(&p, "off 1 repeats, class 0 at the start");
    p.seq[0].esc = 1;
    plan_valid(&p, "a token with its own code sent escaped");

    /* escape followed by 12 bits >= 3072 */
    for (unsigned t = 3072; t < 4096; t += 97) {
        bw_t w;
        w.bits = 0;
        put_sym(&w, &C_TOK, 3072);
        bw_put(&w, t, 12);
        unsigned char b[64] = {1, 0, 'a'};
        memcpy(b + 3, w.b, bw_bytes(&w));
        memset(b + 3 + bw_bytes(&w), 0, 16);
        int bad = 1;
        for (size_t l = 3 + bw_bytes(&w); l < 3 + bw_bytes(&w) + 16; l++)
            bad &= dec(b, l) == -1;
        CHECK(bad, "escaped token %u accepted", t);
    }

    /* each offset class, valid */
    for (unsigned c = 0; c < 6; c++) {
        plan_init(&p);
        p.n = 300;
        for (unsigned i = 0; i < 300; i++)
            p.lits[i] = (unsigned char)(i * 31 + 7);
        static const unsigned offs[6] = {1, 2, 255, 4000, 248, 2040};
        unsigned off = offs[c];
        /* first sequence: 300 literals and a match of off 1, so that off bytes are written */
        p.seq[0] = (seq_t){300, off > 304 ? off - 300 : 4, 1, 2, 0};
        unsigned o2 = 300 + p.seq[0].ml;
        p.seq[1] = (seq_t){0, 40, off, c, 0};
        o2 += 40;
        p.seq[2] = (seq_t){0, BLOCK - o2, 7, 2, 0};
        p.nseq = 3;
        char what[64];
        snprintf(what, sizeof what, "offset class %u", c);
        plan_valid(&p, what);
    }

    /* offset 0 from classes 1, 2, 3, 4, 5 is invalid */
    for (unsigned c = 1; c < 6; c++) {
        plan_init(&p);
        p.n = 10;
        memset(p.lits, 'k', 10);
        p.seq[0] = (seq_t){10, BLOCK - 10, 0, c, 0};
        p.nseq = 1;
        char what[64];
        snprintf(what, sizeof what, "offset 0 of class %u", c);
        plan_invalid(&p, what);
    }
    /* offset larger than the bytes written so far */
    plan_init(&p);
    p.n = 10;
    memset(p.lits, 'k', 10);
    p.seq[0] = (seq_t){10, BLOCK - 10, 11, 2, 0};
    p.nseq = 1;
    plan_invalid(&p, "offset 11 after 10 bytes");
    p.seq[0].off = 10;
    plan_valid(&p, "offset 10 after 10 bytes");
    /* class 0 at the start with no literals: off 1 > 0 bytes */
    plan_init(&p);
    p.n = 1;
    p.lits[0] = 1;
    p.seq[0] = (seq_t){0, 4, 1, 0, 0};
    p.seq[1] = (seq_t){1, BLOCK - 5, 1, 0, 0};
    p.nseq = 2;
    plan_invalid(&p, "a first match with no byte before it");

    /* ml past the end of the block: by hand, a token that asks for too much */
    plan_ab(&p);
    p.seq[0].ml = BLOCK - 1;
    plan_invalid(&p, "match past the end of the block");
    plan_ab(&p);
    p.seq[0].ml = BLOCK - 3; /* then 1 byte left, 0 literals: not the last sequence, needs a token */
    plan_invalid(&p, "1 byte left and no literals");
    /* ll larger than the literals left */
    plan_init(&p);
    p.n = 20;
    memset(p.lits, 'u', 20);
    p.seq[0] = (seq_t){20, 100, 1, 2, 0};
    p.seq[1] = (seq_t){5, BLOCK - 125, 1, 2, 0}; /* the plan has 20 literals, this asks 5 more */
    p.nseq = 2;
    plan_invalid(&p, "ll > n - used");
    /* lengths with LL and ML values: ll 15 and 16, 31+, ml 34, 35, 36, large */
    {
        static const unsigned lls[] = {14, 15, 16, 30, 31, 32, 100, 1000, 2047, 4000};
        static const unsigned mls[] = {4, 34, 35, 36, 50, 66, 67, 300, 2083};
        for (size_t a = 0; a < sizeof lls / sizeof *lls; a++)
            for (size_t m = 0; m < sizeof mls / sizeof *mls; m++) {
                if (lls[a] + mls[m] > BLOCK)
                    continue;
                plan_init(&p);
                p.n = lls[a];
                for (unsigned i = 0; i < p.n; i++)
                    p.lits[i] = (unsigned char)(i ^ (i >> 3));
                p.seq[0] = (seq_t){lls[a], mls[m], 5 < lls[a] ? 5 : 1, 2, 0};
                unsigned o3 = lls[a] + mls[m];
                p.nseq = 1;
                if (BLOCK - o3 >= 4)
                    p.seq[p.nseq++] = (seq_t){0, BLOCK - o3, 1, 2, 0};
                else
                    p.n += BLOCK - o3, memset(p.lits + lls[a], 'e', BLOCK - o3);
                char what[64];
                snprintf(what, sizeof what, "ll %u ml %u", lls[a], mls[m]);
                plan_valid(&p, what);
            }
        /* the largest ll of a block: BLOCK - 4 literals before the only match */
        plan_init(&p);
        p.n = BLOCK - 4;
        for (unsigned i = 0; i < p.n; i++)
            p.lits[i] = (unsigned char)i;
        p.seq[0] = (seq_t){BLOCK - 4, 4, 1, 2, 0};
        p.nseq = 1;
        plan_valid(&p, "ll 4092, the largest");
    }

    /* the last sequence with a token is invalid */
    plan_ab(&p);
    p.extra_token = 1;
    plan_invalid(&p, "a token after the last sequence");
    plan_mixed(&p, 0);
    p.extra_token = 1;
    plan_invalid(&p, "a token after the last sequence, coded");
    /* B: padding bits set, a byte too many */
    plan_mixed(&p, 5);
    p.b_pad_one = 1;
    plan_invalid(&p, "B with padding bits set");
    plan_mixed(&p, 5);
    p.b_extra = 1;
    plan_invalid(&p, "B with a zero byte after its end");
    p.b_extra_val = 0xff;
    plan_invalid(&p, "B with a byte 0xff after its end");
}

static void test_deltas(void)
{
    plan_t p;
    /* dt 1 and 2 by hand: stored 10 20 30 40 50 60 70 80 then 1s */
    for (int dt = 1; dt <= 2; dt++) {
        plan_init(&p);
        p.coded = 1;
        p.code = 0;
        p.e = 1;
        p.dt = dt;
        p.n = BLOCK;
        for (unsigned i = 0; i < BLOCK; i++)
            p.lits[i] = i < 8 ? (unsigned char)(10 * (i + 1)) : 1;
        size_t len = encode(&p, blk);
        int r = dec(blk, len);
        CHECK(r == 0, "deltas dt %d rejected", dt);
        /* out[i] = stored[i] for i < x, out[i - x] + stored[i] after */
        int ok = 1;
        for (unsigned i = 0; i < BLOCK; i++) {
            unsigned v;
            if (dt == 2)
                v = 10 * (i % 8 + 1) + i / 8;
            else if (i < 4)
                v = 10 * (i + 1);
            else if (i < 8)
                v = 10 * (i - 3) + 10 * (i + 1);
            else
                v = 10 * (i % 4 + 1) + 10 * (i % 4 + 5) + (i / 4 - 1);
            ok &= got[i] == (unsigned char)v;
        }
        CHECK(r == 0 && ok, "deltas dt %d: wrong bytes", dt);
        model(&p, want);
        CHECK(memcmp(got, want, BLOCK) == 0, "deltas dt %d: model differs", dt);
    }
    /* dt 3: x from the offset of the sequence before */
    static const unsigned lasts[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 100, 1000};
    for (size_t i = 0; i < sizeof lasts / sizeof *lasts; i++) {
        plan_init(&p);
        p.coded = 1;
        p.code = 6;
        p.e = 1;
        p.dt = 3;
        unsigned L = lasts[i];
        /* sequence 0: 1100 literals, match off 1 of 4; sequence 1: 20 literals (x from last 1),
         * match off L of 10; last sequence: 100 literals with x from L */
        p.seq[0] = (seq_t){1100, 4, 1, 2, 0};
        p.seq[1] = (seq_t){20, 10, L, L == 2 ? 1 : L < 256 ? 2 : 3, 0};
        p.nseq = 2;
        unsigned lastlits = BLOCK - 1100 - 4 - 20 - 10;
        p.n = 1100 + 20 + lastlits;
        for (unsigned k = 0; k < p.n; k++)
            p.lits[k] = (unsigned char)(k < 1100 ? k * 37 : k % 5 == 0);
        char what[64];
        snprintf(what, sizeof what, "dt 3 with last %u", L);
        if (plan_valid(&p, what)) {
            /* check one byte of the last sequence by hand: position 1134 + 50 */
            unsigned pos = 1134 + 50;
            unsigned x = L <= 2 ? 4 : L == 3 ? 6 : L;
            unsigned stored = p.lits[1120 + 50];
            CHECK(got[pos] == (unsigned char)(stored + got[pos - x]), "dt 3 last %u: x", L);
        }
    }
    /* deltas at the start of the block: p < x, stored as they are */
    plan_init(&p);
    p.coded = 1;
    p.code = 7;
    p.e = 1;
    p.dt = 2;
    p.n = BLOCK;
    for (unsigned i = 0; i < BLOCK; i++)
        p.lits[i] = 200;
    size_t len = encode(&p, blk);
    CHECK(dec(blk, len) == 0 && got[0] == 200 && got[7] == 200 && got[8] == 144,
          "deltas: first 8 bytes as stored");
}

static void test_random(int count)
{
    static plan_t p;
    unsigned char *d = malloc(MAXOUT + 16);
    for (int i = 0; i < count; i++) {
        random_plan(&p);
        model(&p, want);
        size_t len = encode(&p, blk);
        int r = dec(blk, len);
        if (r != 0 || memcmp(got, want, BLOCK) != 0) {
            CHECK(0, "random block %d (coded %d code %d e %d dt %d nseq %u n %u): %s", i, p.coded,
                  p.code, p.e, p.dt, p.nseq, p.n, r ? "rejected" : "wrong output");
            continue;
        }
        checks++;
        /* every cut is invalid, one appended byte is invalid */
        size_t cut = len ? rnd() % len : 0;
        CHECK(dec(blk, cut) == -1, "random block %d cut to %zu of %zu accepted", i, cut, len);
        blk[len] = (unsigned char)rnd();
        CHECK(dec(blk, len + 1) == -1, "random block %d with a byte appended accepted", i);
        /* damage: must not crash; the result is not checked */
        for (int k = 0; k < 4; k++) {
            memcpy(d, blk, len);
            unsigned nflip = rr(1, 4);
            for (unsigned f = 0; f < nflip; f++) {
                size_t at = rnd() % len;
                d[at] ^= (unsigned char)(1u << (rnd() % 8));
            }
            (void)dec(d, len);
        }
    }
    /* random bytes */
    for (int i = 0; i < count; i++) {
        size_t len = rnd() % 300;
        for (size_t k = 0; k < len; k++)
            d[k] = (unsigned char)rnd();
        if (len >= 2 && rnd() % 2)
            d[1] &= 0x8f; /* small n */
        (void)dec(d, len);
    }
    free(d);
}

static void bench(void)
{
    static plan_t p;
    enum { NB = 64 };
    static unsigned char bufs[NB][MAXOUT];
    static size_t lens[NB];
    for (int i = 0; i < NB; i++) {
        random_plan(&p);
        lens[i] = encode(&p, bufs[i]);
    }
    clock_t t0 = clock();
    long nb = 0;
    while (clock() - t0 < CLOCKS_PER_SEC / 2) {
        for (int i = 0; i < NB; i++)
            if (cr_decode(bufs[i], lens[i], got) != 0)
                CHECK(0, "bench block rejected");
        nb += NB;
    }
    double s = (double)(clock() - t0) / CLOCKS_PER_SEC;
    printf("speed: %.0f blocks/s\n", (double)nb / s);
}

int main(int argc, char **argv)
{
    int count = argc > 1 ? atoi(argv[1]) : 3000;
    make_codes(cr_len_tok, 3073, &C_TOK);
    make_codes(cr_len_ll, 24, &C_LL);
    make_codes(cr_len_ml, 24, &C_ML);
    make_codes(cr_len_own, 19, &C_OWN);
    for (int i = 0; i < 8; i++)
        make_codes(cr_len_lit + 256 * i, 256, &C_LIT[i]);

    test_example();
    test_layout();
    test_own_table();
    test_sequences();
    test_deltas();
    test_random(count);
    bench();
    printf("%d checks, %d failures\n", checks, failures);
    return failures != 0;
}
