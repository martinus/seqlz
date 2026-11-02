/* Tests of cr_decode, from format.md: its example, and blocks made by hand for its rules.
 * The blocks are built with a small encoder here that writes exactly the fields one asks for,
 * also invalid ones; the expected output of valid blocks is written down by hand where it is short
 * to say, and else computed by `simulate`, a second, plain reading of "Decoding". */
#include "../src/cr_decode.h"
#include "../src/cr_tables.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define BLOCK 4096

static int failures, checks;

#define CHECK(cond, ...)                                                                           \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(cond)) {                                                                             \
            failures++;                                                                            \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                            \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
        }                                                                                          \
    } while (0)

/* Decodes a copy of the block in a buffer of exactly len bytes, so ASan sees every overread. */
static int decode(const unsigned char *blk, size_t len, unsigned char *out)
{
    unsigned char *copy = malloc(len ? len : 1);
    memcpy(copy, blk, len);
    int r = cr_decode(copy, len, out);
    free(copy);
    return r;
}

static void expect_valid(const char *what, const unsigned char *blk, size_t len,
                         const unsigned char *want)
{
    unsigned char out[BLOCK];
    int r = decode(blk, len, out);
    CHECK(r == 0, "%s: expected valid, got %d", what, r);
    if (r == 0 && want)
        CHECK(memcmp(out, want, BLOCK) == 0, "%s: wrong output", what);
}

static void expect_invalid(const char *what, const unsigned char *blk, size_t len)
{
    unsigned char out[BLOCK];
    int r = decode(blk, len, out);
    CHECK(r == -1, "%s: expected invalid, got %d", what, r);
}

/* ---- bit writer, most significant bit first ------------------------------------------------- */

typedef struct {
    unsigned char b[16384];
    size_t n; /* bits */
} bw;

static void put(bw *w, unsigned v, unsigned k)
{
    for (unsigned i = k; i-- > 0;) {
        if (w->n % 8 == 0)
            w->b[w->n / 8] = 0;
        if ((v >> i) & 1)
            w->b[w->n / 8] |= (unsigned char)(0x80 >> (w->n % 8));
        w->n++;
    }
}

static void put_str(bw *w, const char *bits)
{
    for (; *bits; bits++)
        if (*bits == '0' || *bits == '1')
            put(w, (unsigned)(*bits - '0'), 1);
}

static size_t bytes(const bw *w) { return (w->n + 7) / 8; }

/* ---- canonical codes, format.md "Prefix codes" --------------------------------------------- */

typedef struct {
    unsigned code[3073];
    unsigned char len[3073];
} codes;

static void make_codes(const unsigned char *lens, unsigned nsym, codes *c)
{
    unsigned count[32] = {0}, first[32] = {0};
    for (unsigned i = 0; i < nsym; i++)
        count[lens[i]]++;
    count[0] = 0;
    for (unsigned l = 2; l < 32; l++)
        first[l] = (first[l - 1] + count[l - 1]) * 2;
    for (unsigned i = 0; i < nsym; i++) {
        c->len[i] = lens[i];
        c->code[i] = lens[i] ? first[lens[i]]++ : 0;
    }
}

static codes C_TOK, C_LL, C_ML, C_LIT[8], C_OWN;

static void put_sym(bw *w, const codes *c, unsigned s)
{
    if (!c->len[s]) {
        printf("test bug: symbol %u has no code\n", s);
        exit(2);
    }
    put(w, c->code[s], c->len[s]);
}

static void put_value(bw *w, const codes *c, unsigned v)
{
    if (v < 16) {
        put_sym(w, c, v);
        return;
    }
    unsigned b = 0;
    while ((2u << b) <= v)
        b++;
    put_sym(w, c, b + 12);
    put(w, v - (1u << b), b);
}

/* ---- sequences ------------------------------------------------------------------------------ */

typedef struct {
    unsigned ll, ml, off, cls;
    int esc;      /* send the token escaped */
    int raw_off;  /* if set: offset bits are `off` as they are, not off divided by the class */
} seq;

static void put_seq(bw *w, const seq *s)
{
    unsigned llf = s->ll < 15 ? s->ll : 15, mlf = s->ml - 4 < 31 ? s->ml - 4 : 31;
    unsigned token = llf | (mlf << 4) | (s->cls << 9);
    if (s->esc || !C_TOK.len[token]) {
        put_sym(w, &C_TOK, 3072);
        put(w, token, 12);
    } else {
        put_sym(w, &C_TOK, token);
    }
    unsigned div = s->raw_off ? 1 : (s->cls == 1 ? 2 : s->cls >= 4 ? 8 : 1);
    static const unsigned nbits[6] = {0, 1, 8, 12, 5, 8};
    put(w, s->off / div, nbits[s->cls]);
    if (s->ll >= 15)
        put_value(w, &C_LL, s->ll - 15);
    if (s->ml - 4 >= 31)
        put_value(w, &C_ML, s->ml - 35);
}

/* The output of a valid block, as "Decoding" and "Literals as deltas" say. stored[] are the n
 * literals as stored (with e = 1: as deltas). Returns -1 if the sequences are not valid. */
static int simulate(const seq *s, int nseq, const unsigned char *stored, unsigned n, int e, int dt,
                    unsigned char *out)
{
    unsigned used = 0, o = 0, last = 1;
    for (int i = 0;; i++) {
        unsigned ll = i < nseq ? s[i].ll : n - used;
        if (ll > n - used || ll > BLOCK - o)
            return -1;
        unsigned x = dt == 1 ? 4 : dt == 2 ? 8 : (last * ((4 + last - 1) / last));
        for (unsigned k = 0; k < ll; k++, o++, used++)
            out[o] = (unsigned char)(stored[used] + (e && o >= x ? out[o - x] : 0));
        if (i == nseq)
            return (o == BLOCK && used == n) ? 0 : -1;
        unsigned off = s[i].cls == 0 ? last : s[i].off;
        last = off;
        if (off == 0 || off > o || s[i].ml > BLOCK - o)
            return -1;
        for (unsigned k = 0; k < s[i].ml; k++, o++)
            out[o] = out[o - off];
    }
}

/* ---- blocks --------------------------------------------------------------------------------- */

typedef struct {
    int coded;
    unsigned code;         /* 0..15 */
    unsigned e, dt;
    unsigned w;            /* 0: the smallest that holds the sizes, at least 5 */
    const unsigned char *own_lens; /* code 8: the lengths of the own table, 256 bytes */
    const char *own_bits;  /* code 8: if set, the table's description as bits instead */
    int s_adjust[8];       /* added to s[j]: the stream gets zero bytes, or loses its last ones */
    int pad_one[8];        /* set the last bit of stream j's last byte */
    int own_pad_one;       /* set the last bit of the own table's last byte */
    const char *tail;      /* bits written to B after the sequences */
    long n_override;       /* if >= 0: the n written in h */
} opts;

/* The own table's description for lengths lens[256] (runs as long as possible). */
static void put_own(bw *w, const unsigned char *lens)
{
    int lastc = -1;
    for (int v = 0; v < 256; v++)
        if (lens[v])
            lastc = v;
    int v = 0;
    while (v <= lastc) {
        if (lens[v]) {
            put_sym(w, &C_OWN, lens[v] - 1u);
            v++;
            continue;
        }
        unsigned run = 0;
        while (v + (int)run <= lastc && !lens[v + run])
            run++;
        while (run) {
            unsigned c = run < 511 ? run : 511, k = 1;
            while ((1u << k) <= c)
                k++;
            /* 2^(k-1) <= c < 2^k */
            put_sym(w, &C_OWN, 9 + k);
            put(w, c - (1u << (k - 1)), k - 1);
            run -= c;
            v += (int)c;
        }
    }
}

/* Writes a block: literals stored[0..n), the sequences, then the last one. Returns its length. */
static size_t build(const opts *op, const seq *s, int nseq, const unsigned char *stored, unsigned n,
                    unsigned char *dst)
{
    bw B = {0};
    for (int i = 0; i < nseq; i++)
        put_seq(&B, &s[i]);
    if (op->tail)
        put_str(&B, op->tail);

    unsigned hn = op->n_override >= 0 ? (unsigned)op->n_override : n;
    size_t len = 0;
    if (!op->coded) {
        dst[0] = (unsigned char)hn;
        dst[1] = (unsigned char)(hn >> 8);
        memcpy(dst + 2, stored, n);
        len = 2 + n;
    } else {
        static bw str[8];
        size_t sz[8];
        unsigned char own[256];
        codes ownc;
        const codes *c;
        if (op->code == 8) {
            if (op->own_lens) {
                memcpy(own, op->own_lens, 256);
            } else {
                memset(own, 8, 256); /* complete: 256 codes of 8 bits */
            }
            make_codes(own, 256, &ownc);
            c = &ownc;
        } else {
            c = &C_LIT[op->code & 7];
        }
        unsigned maxs = 0;
        for (int j = 0; j < 8; j++) {
            str[j].n = 0;
            for (unsigned i = (unsigned)j; i < n; i += 8)
                put_sym(&str[j], c, stored[i]);
            long sj = (long)bytes(&str[j]) + op->s_adjust[j];
            for (size_t k = bytes(&str[j]); k < (size_t)(sj > 0 ? sj : 0); k++)
                str[j].b[k] = 0;
            if (op->pad_one[j] && bytes(&str[j]))
                str[j].b[bytes(&str[j]) - 1] |= 1;
            sz[j] = (size_t)(sj > 0 ? sj : 0);
            if (sz[j] > maxs)
                maxs = (unsigned)sz[j];
        }
        unsigned w = op->w;
        if (!w) {
            w = 5;
            while ((1u << w) <= maxs)
                w++;
        }
        if ((1u << w) <= maxs) {
            printf("test bug: a stream size does not fit in w = %u\n", w);
            exit(2);
        }
        dst[0] = (unsigned char)hn;
        dst[1] = (unsigned char)((hn >> 8) | 0x80);
        dst[2] = (unsigned char)((op->code & 15) | ((w - 5) << 4) | (op->e << 7));
        len = 3;
        if (op->e)
            dst[len++] = (unsigned char)op->dt;
        /* S, little endian, s[j] at bits j*w */
        memset(dst + len, 0, w);
        for (unsigned j = 0; j < 8; j++)
            for (unsigned k = 0; k < w; k++)
                if ((sz[j] >> k) & 1) {
                    unsigned bit = j * w + k;
                    dst[len + bit / 8] |= (unsigned char)(1u << (bit % 8));
                }
        len += w;
        if (op->code == 8) {
            bw t = {0};
            if (op->own_bits)
                put_str(&t, op->own_bits);
            else
                put_own(&t, own);
            if (op->own_pad_one)
                t.b[bytes(&t) - 1] |= 1;
            memcpy(dst + len, t.b, bytes(&t));
            len += bytes(&t);
        }
        for (int j = 0; j < 8; j++) {
            memcpy(dst + len, str[j].b, sz[j]);
            len += sz[j];
        }
    }
    memcpy(dst + len, B.b, bytes(&B));
    return len + bytes(&B);
}

static opts raw(void)
{
    opts o;
    memset(&o, 0, sizeof o);
    o.n_override = -1;
    return o;
}

static opts coded(unsigned code)
{
    opts o = raw();
    o.coded = 1;
    o.code = code;
    return o;
}

/* Builds, simulates and expects valid with the simulated output. */
static void roundtrip(const char *what, const opts *op, const seq *s, int nseq,
                      const unsigned char *stored, unsigned n)
{
    static unsigned char blk[20000], want[BLOCK];
    if (simulate(s, nseq, stored, n, (int)op->e, (int)op->dt, want)) {
        printf("test bug: %s is not valid\n", what);
        exit(2);
    }
    size_t len = build(op, s, nseq, stored, n, blk);
    expect_valid(what, blk, len, want);
}

static void build_invalid(const char *what, const opts *op, const seq *s, int nseq,
                          const unsigned char *stored, unsigned n)
{
    static unsigned char blk[20000];
    size_t len = build(op, s, nseq, stored, n, blk);
    expect_invalid(what, blk, len);
}

/* ---- the tests ------------------------------------------------------------------------------ */

static const unsigned char example[] = {0x02, 0x00, 0x61, 0x62, 0x23, 0xf2, 0xff, 0xfb, 0x60};

static void test_example(void)
{
    unsigned char want[BLOCK];
    for (int i = 0; i < BLOCK; i++)
        want[i] = i % 2 ? 'b' : 'a';
    expect_valid("example", example, sizeof example, want);

    /* the same block from the encoder here: checks the encoder against format.md */
    static unsigned char blk[64];
    seq s = {2, 4094, 2, 1, 1, 0};
    opts o = raw();
    size_t len = build(&o, &s, 1, (const unsigned char *)"ab", 2, blk);
    CHECK(len == sizeof example && memcmp(blk, example, len) == 0, "encoder does not give the example");

    unsigned char b[sizeof example];
    memcpy(b, example, sizeof b);
    expect_invalid("example cut by one byte (bits past the end of B)", example, sizeof example - 1);
    expect_invalid("example cut in the literals", example, 3);
    b[8] = 0x68; /* a 1 in the 5 bits after the end */
    expect_invalid("example with a padding bit 1", b, sizeof b);
    b[8] = 0x61;
    expect_invalid("example with the last padding bit 1", b, sizeof b);
    unsigned char b2[sizeof example + 1];
    memcpy(b2, example, sizeof example);
    b2[sizeof example] = 0;
    expect_invalid("example with a zero byte after it (8 bits left)", b2, sizeof b2);
    /* ml one more: 35 + 2048 + 2012 = 4095 > 4094 bytes left */
    {
        bw w = {0};
        put_str(&w, "0010 001111110010 1 1111111 11111011100 00000");
        expect_invalid("example with ml = 4095 > BLOCK - |out|", (unsigned char[]){0x02, 0x00, 0x61, 0x62, w.b[0], w.b[1], w.b[2], w.b[3], w.b[4]}, 9);
        bw v = {0};
        put_str(&v, "0010 001111110010 1 1111111 11111011010 00000");
        unsigned char blk2[] = {0x02, 0x00, 0x61, 0x62, v.b[0], v.b[1], v.b[2], v.b[3], v.b[4]};
        expect_invalid("example with ml = 4093: one byte left for no literals", blk2, 9);
    }
}

static void test_header(void)
{
    unsigned char out[BLOCK];
    CHECK(cr_decode((const unsigned char *)"", 0, out) == -1, "len 0");
    expect_invalid("len 0", (const unsigned char *)"", 0);
    expect_invalid("len 1", (const unsigned char *)"\x00", 1);
    expect_invalid("len 1, coded bit", (const unsigned char *)"\x80", 1);

    static unsigned char lits[BLOCK + 1], blk[20000];
    for (int i = 0; i <= BLOCK; i++)
        lits[i] = (unsigned char)(i * 7 + 3);

    /* a block of literals only: n = BLOCK, no bits in B */
    opts o = raw();
    roundtrip("raw literals only", &o, NULL, 0, lits, BLOCK);
    size_t len = build(&o, NULL, 0, lits, BLOCK, blk);
    CHECK(len == 2 + BLOCK, "raw literals only: length");
    expect_invalid("raw literals only, one literal short", blk, len - 1);
    blk[len] = 0;
    expect_invalid("raw literals only + a zero byte", blk, len + 1);
    blk[len] = 0x80;
    expect_invalid("raw literals only + a token's bits", blk, len + 1);

    /* n > BLOCK, raw: 4097 literals present */
    o.n_override = BLOCK + 1;
    len = build(&o, NULL, 0, lits, BLOCK + 1, blk);
    expect_invalid("raw, n = 4097", blk, len);
    o.n_override = 0x7fff;
    len = build(&o, NULL, 0, lits, BLOCK, blk);
    expect_invalid("raw, n = 32767", blk, len);

    /* n = 0: the first match has no byte before it */
    {
        seq s = {0, 4096, 1, 2, 0, 0};
        o = raw();
        build_invalid("raw, n = 0, off 1 at |out| 0", &o, &s, 1, lits, 0);
        unsigned char z[] = {0, 0};
        expect_invalid("raw, n = 0, empty B", z, 2);
    }

    /* coded: too short */
    expect_invalid("coded, len 2", (const unsigned char *)"\x00\x90", 2);
    {
        /* t = 0: w = 5, e = 0, code 0: needs 3 + 5 bytes */
        unsigned char b[8] = {0x00, 0x90, 0x00, 0, 0, 0, 0, 0};
        expect_invalid("coded, len < 3 + w", b, 7);
        unsigned char b2[9] = {0x00, 0x90, 0x80, 1, 0, 0, 0, 0, 0};
        expect_invalid("coded, len < 3 + w + e", b2, 8);
    }

    /* coded, literals only, each fixed table, and n > BLOCK */
    for (unsigned code = 0; code < 8; code++) {
        o = coded(code);
        char name[64];
        snprintf(name, sizeof name, "coded literals only, table %u", code);
        roundtrip(name, &o, NULL, 0, lits, BLOCK);
    }
    o = coded(3);
    o.n_override = BLOCK + 1;
    build_invalid("coded, n = 4097", &o, NULL, 0, lits, BLOCK);

    /* codes 9 to 15 */
    for (unsigned code = 9; code < 16; code++) {
        o = coded(0);
        len = build(&o, NULL, 0, lits, BLOCK, blk);
        blk[2] = (unsigned char)((blk[2] & 0xf0) | code);
        char name[64];
        snprintf(name, sizeof name, "coded, code %u", code);
        expect_invalid(name, blk, len);
    }

    /* dt */
    for (unsigned dt = 0; dt < 256; dt++) {
        o = coded(1);
        o.e = 1;
        o.dt = dt;
        len = build(&o, NULL, 0, lits, BLOCK, blk);
        unsigned char out2[BLOCK];
        int r = decode(blk, len, out2);
        CHECK((r == 0) == (dt >= 1 && dt <= 3), "dt %u gives %d", dt, r);
    }

    /* w: the sizes with a larger w than needed are fine; every w from 5 to 12 */
    for (unsigned w = 10; w <= 12; w++) {
        o = coded(2);
        o.w = w;
        char name[64];
        snprintf(name, sizeof name, "coded, w = %u", w);
        roundtrip(name, &o, NULL, 0, lits, BLOCK);
    }
    for (unsigned w = 5; w <= 12; w++) {
        /* few literals: small streams, so every w holds them */
        seq s = {0, BLOCK - 16, 1, 0, 0, 0};
        o = coded(5);
        o.w = w;
        char name[64];
        snprintf(name, sizeof name, "coded, 16 literals, w = %u", w);
        s.ll = 16;
        roundtrip(name, &o, &s, 1, lits, 16);
    }

    /* streams past len */
    o = coded(0);
    len = build(&o, NULL, 0, lits, BLOCK, blk);
    expect_invalid("coded, last stream cut", blk, len - 1);
    expect_invalid("coded, streams cut in the middle", blk, len / 2);
}

static void test_streams(void)
{
    static unsigned char lits[BLOCK];
    for (int i = 0; i < BLOCK; i++)
        lits[i] = (unsigned char)(i * 13 + i / 7);

    for (int j = 0; j < 8; j++) {
        opts o = coded(4);
        o.s_adjust[j] = 1;
        char name[96];
        snprintf(name, sizeof name, "stream %d one zero byte longer", j);
        build_invalid(name, &o, NULL, 0, lits, BLOCK);
        o = coded(4);
        o.s_adjust[j] = -1;
        snprintf(name, sizeof name, "stream %d one byte short", j);
        build_invalid(name, &o, NULL, 0, lits, BLOCK);
    }
    /* padding bits: need a stream whose codes do not end on a byte boundary */
    {
        unsigned char l1[BLOCK];
        memset(l1, 0, sizeof l1);
        /* 8 literals: one per stream; a code of length < 8 or not a multiple of 8 leaves padding */
        unsigned char pick = 0;
        for (unsigned v = 0; v < 256; v++)
            if (C_LIT[6].len[v] % 8) {
                pick = (unsigned char)v;
                break;
            }
        memset(l1, pick, 8);
        seq s = {8, BLOCK - 8, 1, 2, 0, 0};
        opts o = coded(6);
        roundtrip("8 literals, one per stream", &o, &s, 1, l1, 8);
        for (int j = 0; j < 8; j++) {
            o = coded(6);
            o.pad_one[j] = 1;
            char name[96];
            snprintf(name, sizeof name, "stream %d with a padding bit 1", j);
            build_invalid(name, &o, &s, 1, l1, 8);
        }
    }
    /* n < 8: streams n to 7 have no literals and s[j] must be 0 */
    {
        seq s = {3, BLOCK - 3, 3, 2, 0, 0};
        opts o = coded(0);
        roundtrip("coded, n = 3", &o, &s, 1, lits, 3);
        for (int j = 0; j < 8; j++) {
            o = coded(0);
            o.s_adjust[j] = 1;
            char name[96];
            snprintf(name, sizeof name, "coded, n = 3, s[%d] one larger", j);
            build_invalid(name, &o, &s, 1, lits, 3);
        }
        /* the same, s[j] = 1 declared but no byte added: B read from the right place would be
         * valid, so only the rule "no literals and s[j] not 0" makes these invalid */
        for (unsigned j = 3; j < 8; j++) {
            static unsigned char blk[256];
            o = coded(0);
            size_t len = build(&o, &s, 1, lits, 3, blk);
            unsigned w = 5 + ((blk[2] >> 4) & 7), bit = j * w;
            CHECK(((blk[3 + bit / 8] >> (bit % 8)) & 1) == 0, "s[j] is 0");
            blk[3 + bit / 8] |= (unsigned char)(1u << (bit % 8));
            char name[96];
            snprintf(name, sizeof name, "coded, n = 3, s[%u] = 1 declared, no byte", j);
            expect_invalid(name, blk, len);
        }
        /* n = 1 */
        seq s1 = {1, BLOCK - 1, 1, 2, 0, 0};
        o = coded(7);
        roundtrip("coded, n = 1", &o, &s1, 1, lits, 1);
    }
    /* coded n = 0: valid streams but no first match possible */
    {
        seq s = {0, BLOCK, 1, 2, 0, 0};
        opts o = coded(0);
        build_invalid("coded, n = 0", &o, &s, 1, lits, 0);
    }
}

static void test_own_table(void)
{
    static unsigned char lits[BLOCK];
    unsigned char lens[256];

    /* 'a' and 'b' with one bit each: a run of 97 (k = 7, r = 33), then two symbols 0 */
    memset(lens, 0, 256);
    lens['a'] = lens['b'] = 1;
    for (int i = 0; i < BLOCK; i++)
        lits[i] = (i * 5 / 3) % 2 ? 'a' : 'b';
    opts o = coded(8);
    o.own_lens = lens;
    roundtrip("own table a, b", &o, NULL, 0, lits, BLOCK);
    {
        /* the same, by bits written here from format.md: run 97: symbol 16 (k = 7), read(6) = 33;
         * then symbol 0, symbol 0 */
        bw t = {0};
        put_sym(&t, &C_OWN, 16);
        put(&t, 33, 6);
        put_sym(&t, &C_OWN, 0);
        put_sym(&t, &C_OWN, 0);
        bw u = {0};
        put_own(&u, lens);
        CHECK(t.n == u.n && memcmp(t.b, u.b, bytes(&t)) == 0, "own table encoder");
    }

    /* the byte values 254 and 255 the last ones: a run of 254 (k = 8, r = 126) */
    memset(lens, 0, 256);
    lens[254] = lens[255] = 1;
    for (int i = 0; i < BLOCK; i++)
        lits[i] = (unsigned char)(254 + (i % 3 == 0));
    o = coded(8);
    o.own_lens = lens;
    roundtrip("own table 254, 255", &o, NULL, 0, lits, BLOCK);

    /* every byte value, lengths 1 to 10 and 8 */
    memset(lens, 0, 256);
    {
        /* 1,2,...,9,10,10 is complete: 11 values */
        for (int i = 0; i < 9; i++)
            lens[i * 20] = (unsigned char)(i + 1);
        lens[200] = 10;
        lens[230] = 10;
        for (int i = 0; i < BLOCK; i++)
            lits[i] = (unsigned char)(i % 11 == 10 ? 230 : i % 11 == 9 ? 200 : (i % 11) * 20);
        o = coded(8);
        o.own_lens = lens;
        roundtrip("own table lengths 1 to 10", &o, NULL, 0, lits, BLOCK);
        /* with deltas */
        o.e = 1;
        o.dt = 2;
        roundtrip("own table lengths 1 to 10, dt 2", &o, NULL, 0, lits, BLOCK);
    }
    o = coded(8);
    for (int i = 0; i < BLOCK; i++)
        lits[i] = (unsigned char)(i * 31);
    roundtrip("own table, 256 codes of 8 bits", &o, NULL, 0, lits, BLOCK);

    /* invalid descriptions, given as bits; symbols of OWN as their codes */
    struct {
        const char *name;
        unsigned syms[8];
        int nsym;
        unsigned rbits[8]; /* the read(k-1) after a run symbol */
        int valid;
    } cases[] = {
        {"own: 1, 1 (complete)", {0, 0}, 2, {0}, 1},
        {"own: 1, 2, 2", {0, 1, 1}, 3, {0}, 1},
        {"own: sum above 1 (1, 2, 1)", {0, 1, 0}, 3, {0}, 0},
        {"own: sum above 1 (2, 2, 2, 1)", {1, 1, 1, 0}, 4, {0}, 0},
        {"own: run 255, then 1, then a code for 256", {17, 0, 0}, 3, {127}, 0},
        {"own: run 254, then 1, 1 (valid edge)", {17, 0, 0}, 3, {126}, 1},
        {"own: run 255 then run 2: past 255", {17, 11}, 2, {127, 0}, 0},
        {"own: run 255 then run 1: up to 256, then code", {17, 10, 0}, 3, {127, 0}, 0},
        {"own: symbol 18, r = 0 (run 0..255)", {18, 0, 0}, 3, {0}, 0},
        {"own: symbol 18, r = 5", {18, 0, 0}, 3, {5}, 0},
        {"own: run 1 (k=1, read(0)), then 1, 1", {10, 0, 0}, 3, {0}, 1},
    };
    for (size_t ci = 0; ci < sizeof cases / sizeof cases[0]; ci++) {
        bw t = {0};
        int ri = 0;
        unsigned char used[256];
        memset(used, 0, 256);
        unsigned v = 0;
        for (int i = 0; i < cases[ci].nsym; i++) {
            unsigned s = cases[ci].syms[i];
            put_sym(&t, &C_OWN, s);
            if (s >= 10) {
                unsigned k = s - 9, r = cases[ci].rbits[ri++];
                put(&t, r, k - 1);
                v += (1u << (k - 1)) + r;
            } else {
                if (v < 256)
                    used[v] = (unsigned char)(s + 1);
                v++;
            }
        }
        char bits[200];
        size_t m = 0;
        for (size_t i = 0; i < t.n; i++)
            bits[m++] = (char)('0' + ((t.b[i / 8] >> (7 - i % 8)) & 1));
        bits[m] = 0;
        o = coded(8);
        o.own_bits = bits;
        o.own_lens = used; /* for the streams; only literals with a code */
        unsigned char lv = 0;
        for (int i = 0; i < 256; i++)
            if (used[i]) {
                lv = (unsigned char)i;
                break;
            }
        if (cases[ci].valid) {
            memset(lits, lv, BLOCK);
            roundtrip(cases[ci].name, &o, NULL, 0, lits, BLOCK);
            o.own_pad_one = 1;
            if (t.n % 8) {
                char name[128];
                snprintf(name, sizeof name, "%s, padding bit 1", cases[ci].name);
                build_invalid(name, &o, NULL, 0, lits, BLOCK);
            }
        } else {
            /* streams: one literal; its symbol has some code in the 8-bit table if needed */
            o.own_lens = NULL;
            memset(lits, 0, BLOCK);
            build_invalid(cases[ci].name, &o, NULL, 0, lits, BLOCK);
        }
    }

    /* a table that is not complete before len ends: bits past len are 0, and d > len - (3+w+e) */
    {
        static unsigned char blk[20000];
        unsigned char hdr[3 + 5];
        memset(hdr, 0, sizeof hdr);
        hdr[0] = 0x00;
        hdr[1] = 0x90; /* n = 4096 */
        hdr[2] = 0x08; /* code 8, w = 5 */
        memcpy(blk, hdr, sizeof hdr);
        expect_invalid("own table: no bytes at all", blk, sizeof hdr);
        /* one byte: symbol for length 1 then zeros: OWN symbol with all-zero code continues */
        memset(lens, 0, 256);
        lens[0] = lens[1] = 1;
        memset(lits, 0, BLOCK);
        o = coded(8);
        o.own_lens = lens;
        size_t len = build(&o, NULL, 0, lits, BLOCK, blk);
        expect_valid("own table 0, 1", blk, len, NULL);
        bw t = {0};
        put_own(&t, lens);
        for (size_t cut = 8; cut < 8 + bytes(&t); cut++)
            expect_invalid("own table cut by len", blk, cut);
        /* a table of 1, 2, 2 at byte values 1, 2, 3: run 1 (k = 1), symbols 0, 1, 1: 8+5+5+5 bits
         * in OWN = 23 bits; cut so that its third byte is missing, and n = 0 so no stream follows */
        memset(lens, 0, 256);
        lens[1] = 1;
        lens[2] = lens[3] = 2;
        bw u = {0};
        put_own(&u, lens);
        CHECK(u.n % 8 != 0, "own table bits not a multiple of 8 (%zu)", u.n);
        unsigned char b[3 + 5 + 8];
        memset(b, 0, sizeof b);
        b[0] = 0x00;
        b[1] = 0x80; /* coded, n = 0 */
        b[2] = 0x08;
        memcpy(b + 8, u.b, bytes(&u));
        for (size_t cut = 8; cut < 8 + bytes(&u); cut++)
            expect_invalid("own table (23 bits) cut by len, n = 0", b, cut);
    }
}

static void test_tokens(void)
{
    static unsigned char lits[BLOCK], want[BLOCK];
    for (int i = 0; i < BLOCK; i++)
        lits[i] = (unsigned char)(i * 3 + 1);
    opts o = raw();

    /* each class, valid */
    {
        seq s[] = {
            {300, 2000, 255, 2, 0, 0}, /* class 2 */
            {1, 20, 300, 3, 0, 0},    /* class 3 */
            {0, 5, 2, 1, 0, 0},       /* class 1 */
            {2, 6, 248, 4, 0, 0},     /* class 4 */
            {3, 40, 2040, 5, 0, 0},   /* class 5, ml 40 via ML */
            {4, 4, 0, 0, 0, 0},       /* class 0: 2040 again */
            {15, 34, 8, 4, 0, 0},     /* ll 15 exactly: value 0; ml 34: mlf 30 */
            {16, 35, 1, 2, 0, 0},     /* ll 16: value 1; ml 35: value 0 */
            {15 + 31, 35 + 16, 2500, 3, 0, 0}, /* LL value 31 (symbol 16 b = 4), ML value 16 */
        };
        int ns = sizeof s / sizeof s[0];
        unsigned tot = 0, out = 0;
        for (int i = 0; i < ns; i++) {
            tot += s[i].ll;
            out += s[i].ll + s[i].ml;
        }
        /* the last sequence fills the rest */
        roundtrip("every class, LL and ML values", &o, s, ns, lits, tot + BLOCK - out);
        for (int i = 0; i < ns; i++)
            s[i].esc = 1;
        roundtrip("every class, all tokens escaped", &o, s, ns, lits, tot + BLOCK - out);
    }
    /* class 0 on the first sequence: last is 1 */
    {
        seq s = {1, BLOCK - 1, 0, 0, 0, 0};
        unsigned char one = 'z';
        memset(want, 'z', BLOCK);
        static unsigned char blk[64];
        size_t len = build(&o, &s, 1, &one, 1, blk);
        expect_valid("class 0 first: off 1, ml 4095", blk, len, want);
        s.ll = 0;
        build_invalid("class 0 first with no literal: off 1 > |out| 0", &o, &s, 1, &one, 1);
    }
    /* offset 0 in every class that can give it */
    for (unsigned cls = 1; cls <= 5; cls++) {
        seq s = {8, BLOCK - 8, 0, cls, 0, 0};
        char name[64];
        snprintf(name, sizeof name, "offset 0 in class %u", cls);
        build_invalid(name, &o, &s, 1, lits, 8);
        s.off = cls == 1 ? 2 : 8;
        snprintf(name, sizeof name, "offset %u in class %u", s.off, cls);
        roundtrip(name, &o, &s, 1, lits, 8);
    }
    /* offset larger than |out| after this sequence's literals */
    {
        seq s = {8, BLOCK - 8, 9, 2, 0, 0};
        build_invalid("offset 9 after 8 bytes", &o, &s, 1, lits, 8);
        s.off = 8;
        roundtrip("offset 8 after 8 bytes", &o, &s, 1, lits, 8);
        seq s2[] = {{8, 8, 8, 2, 0, 0}, {4, BLOCK - 20, 21, 2, 0, 0}};
        build_invalid("offset 21 after 20 bytes", &o, s2, 2, lits, 12);
        s2[1].off = 20;
        roundtrip("offset 20 after 20 bytes", &o, s2, 2, lits, 12);
    }
    /* overlapping copy: off 1 repeats the last byte */
    {
        unsigned char l[2] = {'x', 'y'};
        seq s = {2, BLOCK - 2, 1, 2, 0, 0};
        memset(want, 'y', BLOCK);
        want[0] = 'x';
        static unsigned char blk[64];
        size_t len = build(&o, &s, 1, l, 2, blk);
        expect_valid("off 1, ml 4094", blk, len, want);
        /* off 3 < ml */
        unsigned char l3[3] = {1, 2, 3};
        seq s3 = {3, BLOCK - 3, 3, 2, 0, 0};
        for (int i = 0; i < BLOCK; i++)
            want[i] = (unsigned char)(1 + i % 3);
        len = build(&o, &s3, 1, l3, 3, blk);
        expect_valid("off 3, ml 4093", blk, len, want);
    }
    /* ll larger than the literals left */
    {
        seq s = {9, BLOCK - 9, 1, 2, 0, 0};
        build_invalid("ll 9 with n 8", &o, &s, 1, lits, 8);
        s.ll = 15 + 300;
        build_invalid("ll 315 (LL value) with n 8", &o, &s, 1, lits, 8);
    }
    /* ll larger than the bytes left, but not than the literals left */
    {
        seq s[] = {{1, 4000, 1, 2, 0, 0}, {96, 4, 1, 2, 0, 0}};
        build_invalid("ll 96 with 95 bytes left and 99 literals", &o, s, 2, lits, 100);
        /* ll 95 fits, but then no byte is left for the match of 4 */
        s[1].ll = 95;
        build_invalid("ll 95 then ml 4 with 0 bytes left", &o, s, 2, lits, 100);
        /* with 99 literals left and 95 bytes: the last sequence never comes */
        build_invalid("more literals than bytes left, no more tokens", &o, s, 1, lits, 100);
    }
    /* ml larger than the bytes left */
    {
        seq s[] = {{8, BLOCK - 8 - 4, 8, 2, 0, 0}, {0, 5, 1, 2, 0, 0}};
        build_invalid("ml 5 with 4 bytes left", &o, s, 2, lits, 8);
        s[1].ml = 4;
        roundtrip("ml 4 with 4 bytes left: last match reaches the end", &o, s, 2, lits, 8);
    }
    /* last sequence after a match */
    {
        seq s = {8, 1000, 8, 2, 0, 0};
        roundtrip("literals after the last match", &o, &s, 1, lits, BLOCK - 1000);
    }
    /* escape with a token number of 3072 or more */
    {
        unsigned char l[8];
        memset(l, 1, 8);
        for (unsigned tok = 3072; tok < 4096; tok += 341) {
            static unsigned char blk[64];
            bw B = {0};
            put_sym(&B, &C_TOK, 3072);
            put(&B, tok, 12);
            blk[0] = 8;
            blk[1] = 0;
            memcpy(blk + 2, l, 8);
            memcpy(blk + 10, B.b, bytes(&B));
            char name[64];
            snprintf(name, sizeof name, "escaped token %u", tok);
            expect_invalid(name, blk, 10 + bytes(&B));
        }
        /* 3072 + (ll 8, mlf 31): class "6", then 8 offset bits 1 and an ML value that would fill
         * the block if class 6 were read as class 5 */
        {
            static unsigned char blk[64];
            bw B = {0};
            put_sym(&B, &C_TOK, 3072);
            put(&B, 3072 | 8 | (31 << 4), 12);
            put(&B, 1, 8);
            put_value(&B, &C_ML, BLOCK - 8 - 35);
            blk[0] = 8;
            blk[1] = 0;
            memcpy(blk + 2, l, 8);
            memcpy(blk + 10, B.b, bytes(&B));
            expect_invalid("escaped token 3072 + 0x1f8 (class 6)", blk, 10 + bytes(&B));
            /* the same as class 5 is valid */
            B.n = 0;
            put_sym(&B, &C_TOK, 3072);
            put(&B, (5u << 9) | 8 | (31 << 4), 12);
            put(&B, 1, 8);
            put_value(&B, &C_ML, BLOCK - 8 - 35);
            memcpy(blk + 10, B.b, bytes(&B));
            expect_valid("escaped token class 5, the same fields", blk, 10 + bytes(&B), NULL);
        }
        /* and 4095 */
        static unsigned char blk[64];
        bw B = {0};
        put_sym(&B, &C_TOK, 3072);
        put(&B, 4095, 12);
        blk[0] = 8;
        blk[1] = 0;
        memcpy(blk + 2, l, 8);
        memcpy(blk + 10, B.b, bytes(&B));
        expect_invalid("escaped token 4095", blk, 10 + bytes(&B));
    }
    /* a token with a code of its own sent escaped means the same */
    {
        seq s = {2, BLOCK - 2, 2, 1, 0, 0};
        unsigned tok = 2 | (31 << 4) | (1 << 9);
        (void)tok;
        unsigned token = 4 | ((4 - 4) << 4) | (2u << 9);
        if (C_TOK.len[token]) {
            seq a[] = {{4, 4, 3, 2, 0, 0}};
            roundtrip("token with a code", &o, a, 1, lits, BLOCK - 4);
            a[0].esc = 1;
            roundtrip("the same token escaped", &o, a, 1, lits, BLOCK - 4);
        }
        (void)s;
    }
    /* sequences cut: B too short */
    {
        seq s[] = {{8, 100, 8, 3, 0, 0}, {5, 200, 100, 3, 0, 0}, {0, 300, 0, 0, 0, 0}};
        static unsigned char blk[20000];
        unsigned n = BLOCK - 600;
        roundtrip("three sequences", &o, s, 3, lits, n);
        size_t len = build(&o, s, 3, lits, n, blk);
        for (size_t cut = 2 + n; cut < len; cut++)
            expect_invalid("three sequences cut", blk, cut);
    }
    /* a valid block with a tail: any extra bit makes it invalid */
    {
        seq s = {8, BLOCK - 8, 8, 2, 0, 0};
        o.tail = "1";
        build_invalid("1 bit after the last sequence", &o, &s, 1, lits, 8);
        o.tail = "00000000";
        build_invalid("8 zero bits after the last sequence", &o, &s, 1, lits, 8);
        o.tail = NULL;
    }
}

static void test_deltas(void)
{
    static unsigned char stored[BLOCK], want[BLOCK];

    /* dt 1: x = 4, all stored 1: out[p] = 1 + p div 4 (mod 256) */
    memset(stored, 1, BLOCK);
    for (int p = 0; p < BLOCK; p++)
        want[p] = (unsigned char)(1 + p / 4);
    static unsigned char blk[20000];
    opts o = coded(0);
    o.e = 1;
    o.dt = 1;
    size_t len = build(&o, NULL, 0, stored, BLOCK, blk);
    expect_valid("deltas dt 1", blk, len, want);

    /* dt 2: x = 8 */
    for (int p = 0; p < BLOCK; p++)
        want[p] = (unsigned char)(1 + p / 8);
    o.dt = 2;
    len = build(&o, NULL, 0, stored, BLOCK, blk);
    expect_valid("deltas dt 2", blk, len, want);

    /* dt 3: x from `last`, worked out by hand */
    {
        unsigned char st[BLOCK];
        memset(st, 0, sizeof st);
        unsigned char a[] = {10, 20, 30, 40, 1, 1, 1, 1, 5, 5};
        memcpy(st, a, sizeof a);
        seq s[] = {{8, 4, 3, 2, 0, 0}, {2, 4, 2, 1, 0, 0}};
        unsigned char w[18] = {10, 20, 30, 40, 11, 21, 31, 41, /* x = 4 (last 1) */
                               21, 31, 41, 21,                 /* match off 3 */
                               36, 46,                         /* x = 6 (last 3): 5+31, 5+41 */
                               36, 46, 36, 46};                /* match off 2 */
        memcpy(want, w, 18);
        for (int p = 18; p < BLOCK; p++)
            want[p] = want[p - 4]; /* last sequence: x = 4 (last 2), stored 0 */
        o = coded(0);
        o.e = 1;
        o.dt = 3;
        len = build(&o, s, 2, st, BLOCK - 8, blk);
        expect_valid("deltas dt 3 by hand", blk, len, want);
        unsigned char got[BLOCK];
        CHECK(simulate(s, 2, st, BLOCK - 8, 1, 3, got) == 0 && memcmp(got, want, BLOCK) == 0,
              "simulate agrees with dt 3 by hand");
        /* the same block as dt 1: the second sequence's literals use x = 4 */
        o.dt = 1;
        want[12] = 5 + want[8];
        want[13] = 5 + want[9];
        len = build(&o, s, 2, st, BLOCK - 8, blk);
        unsigned char got1[BLOCK];
        CHECK(decode(blk, len, got1) == 0 && got1[12] == (unsigned char)(5 + 21) &&
                  got1[13] == (unsigned char)(5 + 31),
              "dt 1 with the same sequences");
    }
    /* dt 3 with last 5 (x = 5) and last 300 (x = 300: literals before byte 300 as they are) */
    {
        unsigned char st[BLOCK];
        for (int i = 0; i < BLOCK; i++)
            st[i] = (unsigned char)(i % 7);
        seq s[] = {{10, 4, 5, 2, 0, 0}, {6, 300, 4, 2, 0, 0}, {3, 4, 300, 3, 0, 0}, {7, 9, 0, 0, 0, 0}};
        o = coded(1);
        o.e = 1;
        o.dt = 3;
        roundtrip("deltas dt 3, last 5, 4, 300", &o, s, 4, st, BLOCK - 317);
        o.dt = 1;
        roundtrip("deltas dt 1, same sequences", &o, s, 4, st, BLOCK - 317);
        o.dt = 2;
        roundtrip("deltas dt 2, same sequences", &o, s, 4, st, BLOCK - 317);
    }
    /* raw literals have no deltas: the same bytes as raw give the bytes as they are */
    {
        memset(stored, 1, BLOCK);
        memset(want, 1, BLOCK);
        opts r = raw();
        len = build(&r, NULL, 0, stored, BLOCK, blk);
        expect_valid("raw literals: no deltas", blk, len, want);
    }
}

/* ---- random blocks: valid ones round trip, damaged ones do not crash ------------------------- */

static unsigned long long rng = 88172645463325252ull;
static unsigned rnd(unsigned m)
{
    rng ^= rng << 13;
    rng ^= rng >> 7;
    rng ^= rng << 17;
    return (unsigned)(rng % m);
}

/* random complete lengths of at most 10 bits for a random set of byte values */
static void random_lens(unsigned char *lens)
{
    memset(lens, 0, 256);
    /* split a code: start with lengths {1, 1} on two values, then split leaves */
    unsigned vals[256], nv = 0;
    unsigned char l[256];
    unsigned want = 2 + rnd(200);
    l[0] = l[1] = 1;
    nv = 2;
    while (nv < want) {
        unsigned i = rnd(nv);
        if (l[i] >= 10)
            break;
        l[i]++;
        l[nv++] = l[i];
    }
    /* pick nv distinct byte values */
    unsigned char perm[256];
    for (int i = 0; i < 256; i++)
        perm[i] = (unsigned char)i;
    for (int i = 255; i > 0; i--) {
        unsigned j = rnd((unsigned)i + 1);
        unsigned char t = perm[i];
        perm[i] = perm[j];
        perm[j] = t;
    }
    for (unsigned i = 0; i < nv; i++) {
        vals[i] = perm[i];
        lens[vals[i]] = l[i];
    }
}

static unsigned make_random(seq *s, int *nseq, unsigned char *stored, opts *o, unsigned char *lens)
{
    *o = rnd(3) ? coded(rnd(9)) : raw();
    if (o->coded && rnd(2)) {
        o->e = 1;
        o->dt = 1 + rnd(3);
    }
    if (o->coded && o->code == 8) {
        random_lens(lens);
        o->own_lens = lens;
    }
    unsigned out = 0, n = 0, last = 1;
    int ns = 0;
    int mode = rnd(4);
    while (out < BLOCK) {
        unsigned left = BLOCK - out;
        if (ns > 0 && (rnd(8) == 0 || left < 4)) {
            n += left; /* the last sequence */
            break;
        }
        if (left < 4) {
            n += left;
            break;
        }
        unsigned ll = mode == 0 ? rnd(4) : mode == 1 ? rnd(40) : rnd(rnd(2) ? 16 : 600);
        if (out == 0 && ll == 0)
            ll = 1;
        if (ll > left - 4)
            ll = left - 4;
        unsigned outl = out + ll;
        unsigned ml = 4 + (rnd(2) ? rnd(32) : rnd(rnd(2) ? 300 : 4000));
        if (ml > BLOCK - outl)
            ml = BLOCK - outl;
        /* stop exactly so that the literals left are not equal to the bytes left by accident:
         * after this sequence, the last one is decided by n, which we only know at the end. */
        unsigned off, cls;
        unsigned r = rnd(6);
        if (r == 0 && last <= outl) {
            cls = 0;
            off = last;
        } else if (r == 1 && outl >= 2) {
            cls = 1;
            off = 2;
        } else if (r == 2) {
            cls = 2;
            off = 1 + rnd(outl < 255 ? outl : 255);
        } else if (r == 4 && outl >= 8) {
            cls = 4;
            off = 8 * (1 + rnd((outl < 248 ? outl : 248) / 8));
        } else if (r == 5 && outl >= 8) {
            cls = 5;
            off = 8 * (1 + rnd((outl < 2040 ? outl : 2040) / 8));
        } else {
            cls = 3;
            off = 1 + rnd(outl);
        }
        s[ns].ll = ll;
        s[ns].ml = ml;
        s[ns].off = off;
        s[ns].cls = cls;
        s[ns].esc = rnd(10) == 0;
        s[ns].raw_off = 0;
        ns++;
        n += ll;
        out = outl + ml;
        last = off;
    }
    /* the decoder stops at the first point where literals left == bytes left; make sure that is
     * the last sequence: if it happens earlier, cut the sequences there. */
    {
        unsigned used = 0, o2 = 0;
        for (int i = 0; i < ns; i++) {
            if (n - used == BLOCK - o2) {
                ns = i;
                break;
            }
            used += s[i].ll;
            o2 += s[i].ll + s[i].ml;
        }
    }
    *nseq = ns;
    /* stored literals: values with a code */
    unsigned char alpha[256];
    unsigned na = 0;
    for (unsigned v = 0; v < 256; v++)
        if (!(o->coded && o->code == 8) || lens[v])
            alpha[na++] = (unsigned char)v;
    int lmode = rnd(3);
    for (unsigned i = 0; i < n; i++)
        stored[i] = lmode == 0 ? alpha[rnd(na)] : lmode == 1 ? alpha[rnd(na < 4 ? na : 4)] : alpha[(i / 3) % na];
    return n;
}

static const char *corpus_dir; /* if set: the kept random blocks are written there */

static void test_random(int count, double *blocks_per_sec)
{
    static seq s[2000];
    static unsigned char stored[BLOCK], want[BLOCK], got[BLOCK], blk[20000], lens[256];
    static unsigned char keep[64][20000];
    static size_t keeplen[64];
    int nkeep = 0;
    int bad = 0;
    for (int it = 0; it < count; it++) {
        int ns;
        opts o;
        unsigned n = make_random(s, &ns, stored, &o, lens);
        if (simulate(s, ns, stored, n, (int)o.e, (int)o.dt, want)) {
            printf("test bug: random block not valid\n");
            exit(2);
        }
        size_t len = build(&o, s, ns, stored, n, blk);
        int r = decode(blk, len, got);
        if (r != 0 || memcmp(got, want, BLOCK)) {
            if (bad++ < 5)
                printf("FAIL random block %d: r=%d coded=%d code=%u e=%u dt=%u n=%u ns=%d\n", it, r,
                       o.coded, o.code, o.e, o.dt, n, ns);
        }
        if (nkeep < 64) {
            memcpy(keep[nkeep], blk, len);
            keeplen[nkeep++] = len;
        }
        /* damage: cuts, flipped bits, a byte added; whatever the answer, it must return safely */
        for (int d = 0; d < 4; d++) {
            static unsigned char dam[20001];
            size_t dl = len;
            memcpy(dam, blk, len);
            switch (rnd(4)) {
            case 0: dl = rnd((unsigned)len + 1); break;
            case 1: dam[rnd((unsigned)len)] ^= (unsigned char)(1u << rnd(8)); break;
            case 2: dam[rnd((unsigned)len)] = (unsigned char)rnd(256); break;
            default: dam[dl++] = (unsigned char)rnd(256); break;
            }
            int rr = decode(dam, dl, got);
            checks++;
            if (rr != 0 && rr != -1) {
                failures++;
                printf("FAIL damaged block returned %d\n", rr);
            }
            /* a damaged block that is a cut or has an extra byte other than ... is invalid */
            if (dl == len + 1 && rr == 0) {
                /* one more byte after a valid block: at least 8 bits left in B: invalid */
                failures++;
                printf("FAIL block with a byte appended is valid\n");
            }
        }
    }
    checks += count;
    failures += bad;
    if (corpus_dir) {
        for (int i = 0; i < nkeep; i++) {
            char path[512];
            snprintf(path, sizeof path, "%s/seed%02d", corpus_dir, i);
            FILE *f = fopen(path, "wb");
            if (f) {
                fwrite(keep[i], 1, keeplen[i], f);
                fclose(f);
            }
        }
        FILE *f = fopen(strcat(strcpy((char[512]){0}, corpus_dir), "/example"), "wb");
        if (f) {
            fwrite(example, 1, sizeof example, f);
            fclose(f);
        }
    }
    /* speed, on the kept blocks */
    clock_t t0 = clock();
    long dec = 0;
    unsigned sink = 0;
    while (clock() - t0 < CLOCKS_PER_SEC / 2) {
        for (int i = 0; i < nkeep; i++) {
            sink += (unsigned)cr_decode(keep[i], keeplen[i], got);
            sink += got[i];
        }
        dec += nkeep;
    }
    double secs = (double)(clock() - t0) / CLOCKS_PER_SEC;
    *blocks_per_sec = dec / secs;
    if (sink == 12345)
        printf(" ");
}

int main(int argc, char **argv)
{
    int nrandom = argc > 1 ? atoi(argv[1]) : 20000;
    corpus_dir = argc > 2 ? argv[2] : NULL;
    make_codes(cr_len_TOK, 3073, &C_TOK);
    make_codes(cr_len_LL, 24, &C_LL);
    make_codes(cr_len_ML, 24, &C_ML);
    for (int i = 0; i < 8; i++)
        make_codes(cr_len_LIT + 256 * i, 256, &C_LIT[i]);
    make_codes(cr_len_OWN, 19, &C_OWN);

    test_example();
    test_header();
    test_streams();
    test_own_table();
    test_tokens();
    test_deltas();
    double bps = 0;
    test_random(nrandom, &bps);

    printf("%d checks, %d failures; %d random blocks; %.0f blocks/s in this build\n", checks,
           failures, nrandom, bps);
    return failures ? 1 : 0;
}
