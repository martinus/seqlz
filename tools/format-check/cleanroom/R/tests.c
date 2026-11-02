/*
 * Tests for the seqlz decoder, built from seqlz.rst: its example, and blocks
 * made by hand for its rules, valid and invalid.
 *
 * A small bit writer and a canonical-code helper (mirroring "Prefix codes")
 * build the sequence bitstream and literal streams the way the spec reads
 * them, so blocks can be composed symbol by symbol.
 */
#include "seqlz.h"
#include "seqlz_tables.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define BLOCK 4096

static int failures;
static int checks;

static void check(int cond, const char *name)
{
    checks++;
    if (!cond) {
        failures++;
        printf("FAIL: %s\n", name);
    }
}

/* ---- canonical code of a symbol, as in "Prefix codes" ---- */
static void canon_code(const unsigned char *len, int nsym, int sym,
                       unsigned *code, int *clen)
{
    int maxlen = 0, i, l, c, count[32] = {0}, first[32], rank = 0;
    for (i = 0; i < nsym; i++)
        if (len[i] > maxlen)
            maxlen = len[i];
    for (i = 0; i < nsym; i++)
        if (len[i])
            count[len[i]]++;
    c = 0;
    for (l = 1; l <= maxlen; l++) {
        first[l] = c;
        c = (c + count[l]) << 1;
    }
    l = len[sym];
    for (i = 0; i < sym; i++)
        if (len[i] == l)
            rank++;
    *code = first[l] + rank;
    *clen = l;
}

/* ---- bit writer, most significant bit first ---- */
typedef struct {
    unsigned char buf[8192];
    size_t nbits;
} bitw;

static void bw_init(bitw *w) { memset(w, 0, sizeof(*w)); }

static void bw_bit(bitw *w, int bit)
{
    if (bit)
        w->buf[w->nbits >> 3] |= (unsigned char)(1 << (7 - (w->nbits & 7)));
    w->nbits++;
}

static void bw_bits(bitw *w, unsigned v, int n)
{
    int i;
    for (i = n - 1; i >= 0; i--)
        bw_bit(w, (v >> i) & 1);
}

static void bw_sym(bitw *w, const unsigned char *len, int nsym, int sym)
{
    unsigned code;
    int clen;
    canon_code(len, nsym, sym, &code, &clen);
    bw_bits(w, code, clen);
}

static size_t bw_bytes(const bitw *w) { return (w->nbits + 7) >> 3; }

/* value() of a length value (LL/ML), given the raw value v */
static void bw_value(bitw *w, const unsigned char *len, int nsym, long v)
{
    if (v < 16) {
        bw_sym(w, len, nsym, (int)v);
    } else {
        int b = 0;
        while ((1L << (b + 1)) <= v)
            b++;
        bw_sym(w, len, nsym, b + 12);
        bw_bits(w, (unsigned)(v - (1L << b)), b);
    }
}

/* write a token, escaped if it has no code of its own */
static void bw_token(bitw *w, unsigned token)
{
    if (token < 3072 && seqlz_tok_len[token] != 0) {
        bw_sym(w, seqlz_tok_len, 3073, (int)token);
    } else {
        bw_sym(w, seqlz_tok_len, 3073, 3072); /* escape */
        bw_bits(w, token, 12);
    }
}

/* ---- tests ---- */

static void test_example(void)
{
    unsigned char in[] = {0x02, 0x00, 0x61, 0x62, 0x23, 0xf2, 0xff, 0xfb, 0x60};
    unsigned char out[BLOCK];
    int r = cr_decode(in, sizeof(in), out);
    int ok = (r == 0);
    size_t i;
    for (i = 0; i < BLOCK && ok; i++)
        ok = out[i] == ((i & 1) ? 0x62 : 0x61);
    check(ok, "example block decodes to ab*2048");
}

/* raw all-literals block: n = BLOCK, no bitstream */
static void test_raw_all_literals(void)
{
    unsigned char in[2 + BLOCK];
    unsigned char out[BLOCK];
    size_t i;
    int r, ok;
    in[0] = BLOCK & 0xff;
    in[1] = (BLOCK >> 8) & 0xff; /* n = 4096, raw */
    for (i = 0; i < BLOCK; i++)
        in[2 + i] = (unsigned char)(i * 7 + 3);
    r = cr_decode(in, sizeof(in), out);
    ok = (r == 0);
    for (i = 0; i < BLOCK && ok; i++)
        ok = out[i] == (unsigned char)(i * 7 + 3);
    check(ok, "raw all-literals block");
}

/* raw: one literal 'A', then a match off=1 ml=4095 filling the block */
static void test_raw_one_match(void)
{
    unsigned char in[64];
    unsigned char out[BLOCK];
    bitw w;
    size_t pos, nb, i;
    int r, ok;
    unsigned token = 1u | (31u << 4) | (2u << 9); /* ll=1, mlf=31, class 2 */

    bw_init(&w);
    bw_token(&w, token);
    bw_bits(&w, 1, 8);                    /* class 2: read(8) = off = 1 */
    bw_value(&w, seqlz_ml_len, 24, 4060); /* ml = 35 + 4060 = 4095 */
    nb = bw_bytes(&w);

    in[0] = 1;
    in[1] = 0; /* n = 1, raw */
    in[2] = 'A';
    pos = 3;
    memcpy(in + pos, w.buf, nb);
    pos += nb;

    r = cr_decode(in, pos, out);
    ok = (r == 0);
    for (i = 0; i < BLOCK && ok; i++)
        ok = out[i] == 'A';
    check(ok, "raw one literal + 4095 match");
}

/* coded literals: n = 1, 'A' coded with fixed table 0, then same match.
 * Returns the built length in *plen into buf. */
static size_t build_coded_one(unsigned char *buf, int lit_table, int bad_size)
{
    bitw lit, seq;
    size_t s0, pos, nb;
    unsigned token = 1u | (31u << 4) | (2u << 9);

    memset(buf, 0, 128);
    /* stream 0 holds the one literal, streams 1..7 are empty */
    bw_init(&lit);
    bw_sym(&lit, seqlz_lit_len[lit_table], 256, 'A');
    s0 = bw_bytes(&lit);
    if (bad_size)
        s0 += 1; /* wrong size: codes do not end in the last byte */

    bw_init(&seq);
    bw_token(&seq, token);
    bw_bits(&seq, 1, 8);
    bw_value(&seq, seqlz_ml_len, 24, 4060);
    nb = bw_bytes(&seq);

    buf[0] = 0x01;
    buf[1] = 0x80;                   /* h = 0x8000 + 1, coded, n = 1 */
    buf[2] = (unsigned char)lit_table; /* code = table, w = 5, e = 0 */
    /* S: w = 5 bytes, s[0] = s0, rest 0 */
    buf[3] = (unsigned char)(s0 & 0x1f);
    buf[4] = 0;
    buf[5] = 0;
    buf[6] = 0;
    buf[7] = 0;
    pos = 8;
    memcpy(buf + pos, lit.buf, s0 > bw_bytes(&lit) ? bw_bytes(&lit) : s0);
    pos += s0;
    memcpy(buf + pos, seq.buf, nb);
    pos += nb;
    return pos;
}

static void test_coded_one_match(void)
{
    unsigned char buf[128];
    unsigned char out[BLOCK];
    size_t len = build_coded_one(buf, 0, 0);
    int r = cr_decode(buf, len, out);
    int ok = (r == 0);
    size_t i;
    for (i = 0; i < BLOCK && ok; i++)
        ok = out[i] == 'A';
    check(ok, "coded literal (table 0) + match");
}

static void test_coded_bad_stream_size(void)
{
    unsigned char buf[128];
    unsigned char out[BLOCK];
    size_t len = build_coded_one(buf, 0, 1); /* stream size too large */
    int r = cr_decode(buf, len, out);
    check(r == -1, "coded stream whose codes do not end in last byte is invalid");
}

/* ---- invalid blocks ---- */

static void test_invalids(void)
{
    unsigned char out[BLOCK];
    {
        unsigned char in[1] = {0};
        check(cr_decode(in, 1, out) == -1, "len < 2 invalid");
    }
    {
        check(cr_decode(NULL, 0, out) == -1, "len 0 invalid");
    }
    {
        /* raw, n = 4097 > BLOCK */
        unsigned char in[4] = {0x01, 0x10, 0, 0}; /* n = 0x1001 = 4097 */
        check(cr_decode(in, 4, out) == -1, "n > BLOCK invalid");
    }
    {
        /* raw, 2 + n > len */
        unsigned char in[3] = {0x05, 0x00, 'A'}; /* n = 5 but only 1 literal */
        check(cr_decode(in, 3, out) == -1, "raw 2+n > len invalid");
    }
    {
        /* coded, len < 3 (no byte 2) */
        unsigned char in[2] = {0x01, 0x80};
        check(cr_decode(in, 2, out) == -1, "coded len < 3 invalid");
    }
    {
        /* coded, dt not 1..3 (e = 1, dt = 0) */
        unsigned char in[16];
        memset(in, 0, sizeof(in));
        in[0] = 0x01;
        in[1] = 0x80;
        in[2] = 0x80; /* e = 1, w = 5, code = 0 */
        in[3] = 0;    /* dt = 0 -> invalid */
        check(cr_decode(in, 16, out) == -1, "dt = 0 invalid");
    }
    {
        /* coded, code = 9 > 8 */
        unsigned char in[16];
        memset(in, 0, sizeof(in));
        in[0] = 0x01;
        in[1] = 0x80;
        in[2] = 0x09; /* code = 9, w = 5, e = 0 */
        check(cr_decode(in, 16, out) == -1, "code = 9 invalid");
    }
    {
        /* example block with a trailing bit set in the final byte */
        unsigned char in[] = {0x02, 0x00, 0x61, 0x62,
                              0x23, 0xf2, 0xff, 0xfb, 0x61};
        check(cr_decode(in, sizeof(in), out) == -1,
              "nonzero trailing bit invalid");
    }
    {
        /* example block with an extra zero byte: a whole byte left over */
        unsigned char in[] = {0x02, 0x00, 0x61, 0x62,
                              0x23, 0xf2, 0xff, 0xfb, 0x60, 0x00};
        check(cr_decode(in, sizeof(in), out) == -1,
              "a full byte of leftover bits invalid");
    }
    {
        /* example truncated by one byte: needs a bit past the end */
        unsigned char in[] = {0x02, 0x00, 0x61, 0x62, 0x23, 0xf2, 0xff, 0xfb};
        check(cr_decode(in, sizeof(in), out) == -1,
              "truncated bitstream invalid");
    }
}

/* first match offset larger than its ll: off > |out| */
static void test_first_offset_too_large(void)
{
    unsigned char in[64];
    unsigned char out[BLOCK];
    bitw w;
    size_t pos, nb;
    unsigned token = 1u | (31u << 4) | (2u << 9); /* ll=1, mlf=31, class 2 */

    bw_init(&w);
    bw_token(&w, token);
    bw_bits(&w, 2, 8); /* off = 2, but only 1 byte written -> off > |out| */
    bw_value(&w, seqlz_ml_len, 24, 4060);
    nb = bw_bytes(&w);
    in[0] = 1;
    in[1] = 0;
    in[2] = 'A';
    pos = 3;
    memcpy(in + pos, w.buf, nb);
    pos += nb;
    check(cr_decode(in, pos, out) == -1, "first match off > |out| invalid");
}

/* offset 0 (class 2 read(8) = 0) is invalid */
static void test_offset_zero(void)
{
    unsigned char in[64];
    unsigned char out[BLOCK];
    bitw w;
    size_t pos, nb;
    unsigned token = 1u | (31u << 4) | (2u << 9);

    bw_init(&w);
    bw_token(&w, token);
    bw_bits(&w, 0, 8); /* off = 0 -> invalid */
    bw_value(&w, seqlz_ml_len, 24, 4060);
    nb = bw_bytes(&w);
    in[0] = 1;
    in[1] = 0;
    in[2] = 'A';
    pos = 3;
    memcpy(in + pos, w.buf, nb);
    pos += nb;
    check(cr_decode(in, pos, out) == -1, "offset 0 invalid");
}

/* a block whose match overruns the block end: ml > BLOCK - |out| */
static void test_match_too_long(void)
{
    unsigned char in[64];
    unsigned char out[BLOCK];
    bitw w;
    size_t pos, nb;
    unsigned token = 1u | (31u << 4) | (2u << 9);

    bw_init(&w);
    bw_token(&w, token);
    bw_bits(&w, 1, 8);
    bw_value(&w, seqlz_ml_len, 24, 4061); /* ml = 4096 > 4095 available */
    nb = bw_bytes(&w);
    in[0] = 1;
    in[1] = 0;
    in[2] = 'A';
    pos = 3;
    memcpy(in + pos, w.buf, nb);
    pos += nb;
    check(cr_decode(in, pos, out) == -1, "match past block end invalid");
}

/* delta literals: e = 1, dt = 1 (x = 4), coded table 0.
 * Build a block of n literals only is impossible with delta alone unless
 * matches exist; instead build: 8 literals coded, then a match filling rest,
 * and verify the deltas are added back. */
static void test_delta_roundtrip(void)
{
    /* Plan: 4 literals stored as deltas with dt=1 (x=4). With |out| < 4 for
     * the first 4 bytes, they are stored as-is. Then a match, then the rest.
     * We make out start with 4 known bytes, then 4 delta literals that each
     * refer 4 back. */
    unsigned char buf[256];
    unsigned char out[BLOCK];
    bitw lit[8], seq;
    size_t s[8], pos, i, nb, stotal;
    unsigned token1, token2;
    int j;
    /* literals: 8 of them, one per stream, values chosen so bytes 0..3 are
     * stored as-is (p<4) and bytes 4..7 are deltas to bytes 0..3. */
    unsigned char want[8] = {10, 20, 30, 40, 15, 25, 35, 45};
    unsigned char stored[8];
    for (i = 0; i < 4; i++)
        stored[i] = want[i];
    for (i = 4; i < 8; i++)
        stored[i] = (unsigned char)((want[i] - want[i - 4]) & 0xff);

    for (j = 0; j < 8; j++) {
        bw_init(&lit[j]);
        bw_sym(&lit[j], seqlz_lit_len[0], 256, stored[j]);
        s[j] = bw_bytes(&lit[j]);
    }

    /* sequence: ll=8 copies the 8 delta literals; then a match off=1 fills
     * the remaining 4088 bytes. Need the first match off <= |out| = 8. */
    /* token1: ll=8, mlf=31, class 2 (off read(8)) */
    token1 = 8u | (31u << 4) | (2u << 9);
    (void)token2;

    bw_init(&seq);
    bw_token(&seq, token1);
    bw_bits(&seq, 1, 8);                  /* off = 1 */
    bw_value(&seq, seqlz_ml_len, 24, 4096 - 8 - 35); /* ml = 4088 */
    nb = bw_bytes(&seq);

    buf[0] = 0x08;
    buf[1] = 0x80; /* h = 0x8000 + 8, coded, n = 8 */
    buf[2] = 0x80; /* e = 1, w = 5, code = 0 */
    buf[3] = 1;    /* dt = 1 -> x = 4 */
    /* S: w=5 bytes, each s[j] in 5 bits little endian across 40 bits */
    {
        uint64_t S = 0;
        for (j = 0; j < 8; j++)
            S |= (uint64_t)(s[j] & 0x1f) << (j * 5);
        for (i = 0; i < 5; i++)
            buf[4 + i] = (unsigned char)((S >> (i * 8)) & 0xff);
    }
    pos = 9;
    stotal = 0;
    for (j = 0; j < 8; j++) {
        memcpy(buf + pos, lit[j].buf, s[j]);
        pos += s[j];
        stotal += s[j];
    }
    (void)stotal;
    memcpy(buf + pos, seq.buf, nb);
    pos += nb;

    int r = cr_decode(buf, pos, out);
    int ok = (r == 0);
    for (i = 0; i < 8 && ok; i++)
        ok = out[i] == want[i];
    /* the match copies byte 7 (off=1) forward */
    for (i = 8; i < BLOCK && ok; i++)
        ok = out[i] == want[7];
    check(ok, "delta literals dt=1 decode with deltas added back");
}

/* a block's own literal table (code = 8): two byte values 'A','B' each a
 * 1-bit code. OWN description: symbol 0 (l=1) for 'A' needs runs to skip
 * byte values 0..64. Simpler: build the smallest complete own table of two
 * symbols covering byte values 0 and 1. */
static void test_own_table(void)
{
    /* own table: byte value 0 -> code length 1, byte value 1 -> code length 1.
     * Description symbols: 0 (l=1) then 0 (l=1). After two length-1 codes the
     * sum is 1, table complete. Literals 'A','B' are not 0/1, so use literals
     * 0 and 1 instead. */
    unsigned char buf[256];
    unsigned char out[BLOCK];
    bitw own, lit0, lit1, seq;
    size_t pos, i, nb, s[8];
    int j;
    unsigned char ownlen[256];
    unsigned token;

    /* build the OWN description bitstream */
    bw_init(&own);
    bw_sym(&own, seqlz_own_len, 19, 0); /* l = 1 for byte value 0 */
    bw_sym(&own, seqlz_own_len, 19, 0); /* l = 1 for byte value 1 */
    size_t d = bw_bytes(&own);

    /* the own literal table lengths, to encode the two literals */
    memset(ownlen, 0, sizeof(ownlen));
    ownlen[0] = 1;
    ownlen[1] = 1;

    /* two literals: value 0 in stream 0, value 1 in stream 1 */
    bw_init(&lit0);
    bw_sym(&lit0, ownlen, 256, 0);
    bw_init(&lit1);
    bw_sym(&lit1, ownlen, 256, 1);
    for (j = 0; j < 8; j++)
        s[j] = 0;
    s[0] = bw_bytes(&lit0);
    s[1] = bw_bytes(&lit1);

    /* sequence: ll=2, then match off=1 fills the rest */
    token = 2u | (31u << 4) | (2u << 9); /* ll=2, mlf=31, class 2 */
    bw_init(&seq);
    bw_token(&seq, token);
    bw_bits(&seq, 1, 8);
    bw_value(&seq, seqlz_ml_len, 24, 4096 - 2 - 35); /* ml = 4059 */
    nb = bw_bytes(&seq);

    buf[0] = 0x02;
    buf[1] = 0x80; /* h = 0x8000 + 2, coded, n = 2 */
    buf[2] = 0x08; /* code = 8 (own), w = 5, e = 0 */
    {
        uint64_t S = 0;
        for (j = 0; j < 8; j++)
            S |= (uint64_t)(s[j] & 0x1f) << (j * 5);
        for (i = 0; i < 5; i++)
            buf[3 + i] = (unsigned char)((S >> (i * 8)) & 0xff);
    }
    pos = 8;
    memcpy(buf + pos, own.buf, d);
    pos += d;
    memcpy(buf + pos, lit0.buf, s[0]);
    pos += s[0];
    memcpy(buf + pos, lit1.buf, s[1]);
    pos += s[1];
    memcpy(buf + pos, seq.buf, nb);
    pos += nb;

    int r = cr_decode(buf, pos, out);
    int ok = (r == 0);
    if (ok)
        ok = out[0] == 0 && out[1] == 1;
    for (i = 2; i < BLOCK && ok; i++)
        ok = out[i] == 1; /* match off=1 copies byte 1 (value 1) forward */
    check(ok, "block's own literal table decodes");
}

static void test_perf(void)
{
    unsigned char in[] = {0x02, 0x00, 0x61, 0x62, 0x23, 0xf2, 0xff, 0xfb, 0x60};
    unsigned char out[BLOCK];
    long i, iters = 200000;
    clock_t t0 = clock();
    volatile int sink = 0;
    for (i = 0; i < iters; i++)
        sink ^= cr_decode(in, sizeof(in), out);
    clock_t t1 = clock();
    double sec = (double)(t1 - t0) / CLOCKS_PER_SEC;
    double rate = sec > 0 ? iters / sec : 0;
    printf("perf: %ld blocks in %.3f s = %.0f blocks/s\n", iters, sec, rate);
    check(rate >= 10000.0, "decodes at least 10000 blocks/s");
    (void)sink;
}

int main(void)
{
    test_example();
    test_raw_all_literals();
    test_raw_one_match();
    test_coded_one_match();
    test_coded_bad_stream_size();
    test_invalids();
    test_first_offset_too_large();
    test_offset_zero();
    test_match_too_long();
    test_delta_roundtrip();
    test_own_table();
    test_perf();

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
