/* cr-check <dir> <count>: decodes <dir>/<k>.z for k < count with a strict decoder written from
 * format.md, checks every rule of validity, and compares with <dir>/<k>.block.
 * cr-check -f <file.z> [<file.block>]: one file. C11. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cr_tables.h"

#define BLOCK 4096

/* A decoding table of a prefix code: walk the canonical code bit by bit. */
struct code { const unsigned char *len; int n; int count[16]; int first[16]; int sym[4096]; int offs[16]; };

static int build(struct code *c, const unsigned char *len, int n)
{
    memset(c, 0, sizeof *c);
    c->len = len;
    c->n = n;
    for (int s = 0; s < n; s++) c->count[len[s]]++;
    c->count[0] = 0;
    c->first[1] = 0;
    for (int l = 2; l < 16; l++) c->first[l] = (c->first[l - 1] + c->count[l - 1]) * 2;
    int k = 0;
    for (int l = 1; l < 16; l++) {
        c->offs[l] = k;
        for (int s = 0; s < n; s++)
            if (len[s] == l) c->sym[k++] = s;
    }
    /* completeness */
    unsigned long long sum = 0;
    for (int s = 0; s < n; s++)
        if (len[s]) sum += 1ull << (15 - len[s]);
    return sum == (1ull << 15);
}

struct br { const unsigned char *b; size_t start, nbits, i; int past; };

static unsigned bit(struct br *r)
{
    if (r->i >= r->nbits) { r->past = 1; r->i++; return 0; }
    size_t p = r->start * 8 + r->i++;
    return (r->b[p >> 3] >> (7 - (p & 7))) & 1;
}

static unsigned rd(struct br *r, int n)
{
    unsigned v = 0;
    while (n-- > 0) v = v << 1 | bit(r);
    return v;
}

static int sym(struct br *r, const struct code *c)
{
    int v = 0;
    for (int l = 1; l < 16; l++) {
        v = v << 1 | (int)bit(r);
        int idx = v - c->first[l];
        if (idx >= 0 && idx < c->count[l]) return c->sym[c->offs[l] + idx];
    }
    return -1;
}

/* The bits after position i up to nbits must be fewer than 8 and zero. */
static int ends_clean(struct br *r)
{
    if (r->past) return 0;
    if (r->nbits - r->i >= 8) return 0;
    while (r->i < r->nbits)
        if (bit(r)) return 0;
    return 1;
}

static struct code TOK, LL, ML, LIT[8], OWNC, OWNT;

static unsigned value(struct br *r, const struct code *c)
{
    int s = sym(r, c);
    if (s < 16) return (unsigned)s;
    int b = s - 12;
    return (1u << b) + rd(r, b);
}

/* Returns NULL if valid and decoded to out, else the reason. */
const char *cr_decode(const unsigned char *z, size_t len, unsigned char *out)
{
    static unsigned char lits[BLOCK];
    static unsigned char ownlen[256];
    if (len < 2) return "len < 2";
    unsigned h = z[0] | z[1] << 8;
    unsigned n = h & 0x7fff;
    if (n > BLOCK) return "n > BLOCK";
    size_t bstart;
    int e = 0, dt = 0;
    if (!(h & 0x8000)) {
        if (2 + n > len) return "raw: 2 + n > len";
        memcpy(lits, z + 2, n);
        bstart = 2 + n;
    } else {
        if (len < 3) return "coded: len < 3";
        unsigned t = z[2];
        unsigned code = t & 15, w = 5 + ((t >> 4) & 7);
        e = (int)(t >> 7);
        if (len < 3 + w + (unsigned)e) return "coded: len < 3 + w + e";
        if (e) {
            dt = z[3];
            if (dt < 1 || dt > 3) return "dt not 1..3";
        }
        if (code > 8) return "code > 8";
        unsigned s[8];
        for (int j = 0; j < 8; j++) {
            unsigned v = 0;
            for (unsigned b = 0; b < w; b++) {
                unsigned p = (unsigned)j * w + b;
                v |= ((z[3 + e + p / 8] >> (p % 8)) & 1u) << b;
            }
            s[j] = v;
        }
        size_t d = 0;
        const struct code *lc;
        if (code == 8) {
            struct br r = {z, 3 + w + (size_t)e, (len - 3 - w - (size_t)e) * 8, 0, 0};
            memset(ownlen, 0, sizeof ownlen);
            unsigned long sum = 0; /* in units of 2^-10 */
            unsigned v = 0;
            while (sum < 1024) {
                int sy = sym(&r, &OWNC);
                if (r.past) return "own table past len";
                if (sy < 10) {
                    if (v > 255) return "own: code for byte value > 255";
                    ownlen[v++] = (unsigned char)(sy + 1);
                    sum += 1u << (10 - (sy + 1));
                    if (sum > 1024) return "own: sum above 1";
                } else {
                    int k = sy - 9;
                    v += (1u << (k - 1)) + rd(&r, k - 1);
                    if (r.past) return "own table past len";
                }
            }
            d = (r.i + 7) / 8;
            /* bits after the last symbol in its byte must be 0 */
            while (r.i % 8) if (bit(&r)) return "own: nonzero bits after end";
            if (3 + w + e + d > len) return "own: past len";
            if (!build(&OWNT, ownlen, 256)) return "own: incomplete (internal)";
            lc = &OWNT;
        } else {
            lc = &LIT[code];
        }
        size_t start = 3 + w + (size_t)e + d, tot = 0;
        for (int j = 0; j < 8; j++) tot += s[j];
        if (start + tot > len) return "streams past len";
        size_t st = start;
        for (int j = 0; j < 8; j++) {
            struct br r = {z, st, (size_t)s[j] * 8, 0, 0};
            int any = 0;
            for (unsigned i = (unsigned)j; i < n; i += 8) {
                int sy = sym(&r, lc);
                if (sy < 0) return "literal: no code";
                lits[i] = (unsigned char)sy;
                any = 1;
            }
            if (!any && s[j] != 0) return "empty stream with s[j] != 0";
            if (any && !ends_clean(&r)) return "stream does not end in its last byte with zeros";
            st += s[j];
        }
        bstart = start + tot;
    }
    struct br B = {z, bstart, (len - bstart) * 8, 0, 0};
    unsigned used = 0, o = 0, last = 1;
    for (;;) {
        unsigned k;
        if (n - used == BLOCK - o) k = n - used;
        else k = 0;
        int final = n - used == BLOCK - o;
        unsigned ll = 0, ml = 0, off = 0, c = 0;
        if (!final) {
            int t = sym(&B, &TOK);
            if (t == 3072) {
                t = (int)rd(&B, 12);
                if (t >= 3072) return "escaped token >= 3072";
            }
            ll = (unsigned)t & 15;
            unsigned mlf = ((unsigned)t >> 4) & 31;
            c = (unsigned)t >> 9;
            unsigned x = 0;
            switch (c) {
            case 0: break;
            case 1: x = rd(&B, 1) * 2; break;
            case 2: x = rd(&B, 8); break;
            case 3: x = rd(&B, 12); break;
            case 4: x = rd(&B, 5) * 8; break;
            default: x = rd(&B, 8) * 8; break;
            }
            off = c == 0 ? last : x;
            if (ll == 15) ll = 15 + value(&B, &LL);
            if (ll > n - used || ll > BLOCK - o) return "ll too large";
            k = ll;
            ml = mlf + 4;
            if (mlf == 31) ml = 35 + value(&B, &ML);
        }
        for (unsigned i = 0; i < k; i++) {
            unsigned v = lits[used++];
            if (e) {
                unsigned xx = dt == 1 ? 4 : dt == 2 ? 8 : (last <= 2 ? 4 : last == 3 ? 6 : last);
                if (o >= xx) v = (v + out[o - xx]) & 255;
            }
            out[o++] = (unsigned char)v;
        }
        if (final) break;
        last = off;
        if (off == 0 || off > o || ml > BLOCK - o) return "bad match";
        for (unsigned i = 0; i < ml; i++, o++) out[o] = out[o - off];
    }
    if (!ends_clean(&B)) return "B does not end cleanly";
    return NULL;
}

int cr_check_init(void)
{
    int ok = build(&TOK, cr_len_tok, 3073) & build(&LL, cr_len_ll, 24) & build(&ML, cr_len_ml, 24) &
             build(&OWNC, cr_len_own, 19);
    for (int t = 0; t < 8; t++) ok &= build(&LIT[t], cr_len_lit + 256 * t, 256);
    return ok;
}

#ifndef CR_CHECK_NO_MAIN
static unsigned char *slurp(const char *path, size_t *n)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    unsigned char *b = malloc(1 << 16);
    *n = fread(b, 1, 1 << 16, f);
    fclose(f);
    return b;
}

static int check(const char *zp, const char *bp)
{
    size_t zn, bn;
    unsigned char out[BLOCK];
    unsigned char *z = slurp(zp, &zn);
    if (!z) { fprintf(stderr, "%s: cannot read\n", zp); return 1; }
    const char *err = cr_decode(z, zn, out);
    free(z);
    if (err) { fprintf(stderr, "%s: invalid: %s\n", zp, err); return 1; }
    if (bp) {
        unsigned char *b = slurp(bp, &bn);
        if (!b || bn != BLOCK || memcmp(b, out, BLOCK)) { fprintf(stderr, "%s: decodes to other bytes than %s\n", zp, bp); free(b); return 1; }
        free(b);
    }
    return 0;
}

int main(int argc, char **argv)
{
    if (!cr_check_init()) { fprintf(stderr, "a table is not complete\n"); return 1; }
    if (argc >= 3 && !strcmp(argv[1], "-f")) return check(argv[2], argc > 3 ? argv[3] : NULL);
    if (argc != 3) { fprintf(stderr, "usage: cr-check <dir> <count> | cr-check -f <file.z> [<file.block>]\n"); return 2; }
    long cnt = atol(argv[2]), bad = 0;
    char zp[4096], bp[4096];
    for (long k = 0; k < cnt; k++) {
        snprintf(zp, sizeof zp, "%s/%ld.z", argv[1], k);
        snprintf(bp, sizeof bp, "%s/%ld.block", argv[1], k);
        bad += check(zp, bp);
    }
    printf("%ld of %ld blocks valid and equal\n", cnt - bad, cnt);
    return bad != 0;
}
#endif
