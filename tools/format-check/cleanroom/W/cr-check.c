/* cr-check: a decoder of seqlz blocks written from format.md, separate from the writer, slow and
 * bit by bit. cr-check <dir> <count>: decodes <dir>/<k>.z for k < count and compares with <k>.block.
 * cr-check with no arguments decodes the example of format.md. */
#include "cr_tables.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BLOCK 4096
#define P 12

typedef struct { const unsigned char *b; size_t start, nbytes, pos; int past; } bits;

static unsigned rbit(bits *s) {
    size_t i = s->pos++;
    if (i >= s->nbytes * 8) { s->past = 1; return 0; }
    return (s->b[s->start + i / 8] >> (7 - i % 8)) & 1;
}
static unsigned rd(bits *s, int n) { unsigned v = 0; while (n-- > 0) v = v << 1 | rbit(s); return v; }

/* decode a symbol with canonical codes, bit by bit */
typedef struct { int nsym; const unsigned char *len; unsigned first[17], count[17]; int sorted[4096]; int base[17]; } dec;
static void mkdec(dec *d, const unsigned char *len, int nsym) {
    memset(d, 0, sizeof *d);
    d->nsym = nsym; d->len = len;
    for (int s = 0; s < nsym; s++) if (len[s]) d->count[len[s]]++;
    d->first[1] = 0;
    for (int l = 2; l <= 16; l++) d->first[l] = (d->first[l - 1] + d->count[l - 1]) * 2;
    int k = 0;
    for (int l = 1; l <= 16; l++) { d->base[l] = k; for (int s = 0; s < nsym; s++) if (len[s] == l) d->sorted[k++] = s; }
}
static int sym(const dec *d, bits *s) {
    unsigned c = 0;
    for (int l = 1; l <= 16; l++) {
        c = c << 1 | rbit(s);
        if (c - d->first[l] < d->count[l]) return d->sorted[d->base[l] + (int)(c - d->first[l])];
    }
    return -1;
}
static int value(const dec *d, bits *s) {
    int v = sym(d, s);
    if (v < 0) return -1;
    if (v < 16) return v;
    int b = v - 12;
    return (1 << b) + (int)rd(s, b);
}

static dec TOK, LL, ML, LIT[8], OWN, OWNT;

/* returns NULL if valid (out filled), else why invalid */
static const char *decode(const unsigned char *z, size_t len, unsigned char *out) {
    if (len < 2) return "len < 2";
    unsigned h = z[0] | z[1] << 8, n = h & 0x7fff;
    if (n > BLOCK) return "n > BLOCK";
    unsigned char lits[BLOCK];
    int e = 0, dt = 0;
    size_t bstart;
    if (!(h & 0x8000)) {
        if (2 + n > len) return "raw: 2 + n > len";
        memcpy(lits, z + 2, n);
        bstart = 2 + n;
    } else {
        if (len < 3) return "len < 3";
        unsigned t = z[2], code = t & 15, w = 5 + ((t >> 4) & 7);
        e = t >> 7;
        if (len < 3 + w + e) return "len < 3 + w + e";
        if (e) { dt = z[3]; if (dt < 1 || dt > 3) return "dt"; }
        if (code > 8) return "code > 8";
        unsigned s[8];
        for (int j = 0; j < 8; j++) {
            unsigned v = 0;
            for (unsigned b = 0; b < w; b++) {
                unsigned k = j * w + b;
                if ((z[3 + e + k / 8] >> (k % 8)) & 1) v |= 1u << b;
            }
            s[j] = v;
        }
        size_t d = 0;
        const dec *lt;
        if (code == 8) {
            static unsigned char ol[256];
            memset(ol, 0, sizeof ol);
            bits tb = {z, 3 + w + e, len - (3 + w + e), 0, 0};
            unsigned long sum = 0; /* in units of 2^-10 */
            int v = 0, nsy = 0;
            while (sum < 1024) {
                if (++nsy > 256) return "own: 257th symbol";
                int sy = sym(&OWN, &tb);
                if (sy < 0) return "own: no code";
                if (sy <= 9) {
                    if (v > 255) return "own: byte value above 255";
                    ol[v++] = (unsigned char)(sy + 1);
                    sum += 1024u >> (sy + 1);
                    if (sum > 1024) return "own: sum above 1";
                } else {
                    int k = sy - 9;
                    int run = (1 << (k - 1)) + (int)rd(&tb, k - 1);
                    v += run;
                    if (v > 256) return "own: run past 255";
                }
            }
            if (tb.past) return "own: past len";
            d = (tb.pos + 7) / 8;
            if (tb.pos % 8 && rd(&tb, 8 - tb.pos % 8)) return "own: bits after end";
            if (3 + w + e + d > len) return "own: 3+w+e+d > len";
            mkdec(&OWNT, ol, 256);
            lt = &OWNT;
        } else lt = &LIT[code];
        size_t start = 3 + w + e + d, tot = 0;
        for (int j = 0; j < 8; j++) tot += s[j];
        if (start + tot > len) return "streams past len";
        for (int j = 0; j < 8; j++) {
            bits sb = {z, start, s[j], 0, 0};
            int any = 0;
            for (unsigned i = j; i < n; i += 8) {
                int v = sym(lt, &sb);
                if (v < 0) return "literal: no code";
                lits[i] = (unsigned char)v;
                any = 1;
            }
            if (!any) { if (s[j]) return "empty stream with size"; }
            else {
                if (sb.past) return "literal past stream";
                if (!(8 * s[j] - 8 < sb.pos && sb.pos <= 8 * s[j])) return "stream size";
                if (sb.pos % 8 && rd(&sb, 8 - sb.pos % 8)) return "stream padding";
            }
            start += s[j];
        }
        bstart = start;
    }
    bits B = {z, bstart, len - bstart, 0, 0};
    unsigned used = 0, o = 0, last = 1;
#define COPY(k) do { unsigned kk_ = (k); for (unsigned q = 0; q < kk_; q++) {                          \
        unsigned lv = lits[used++];                                                   \
        if (e) { unsigned x = dt == 1 ? 4 : dt == 2 ? 8 : (last <= 2 ? 4 : last == 3 ? 6 : last); \
                 if (o >= x) lv = (lv + out[o - x]) & 255; }                          \
        out[o++] = (unsigned char)lv; } } while (0)
    for (;;) {
        if (n - used == BLOCK - o) { COPY(n - used); break; }
        int tok = sym(&TOK, &B);
        if (tok < 0) return "token: no code";
        if (tok == 3072) { tok = (int)rd(&B, 12); if (tok >= 3072) return "escaped token >= 3072"; }
        unsigned ll = tok & 15, mlf = (tok >> 4) & 31, c = tok >> 9, off = 0;
        switch (c) {
        case 0: off = last; break;
        case 1: off = rd(&B, 1) * 2; break;
        case 2: off = rd(&B, 8); break;
        case 3: off = rd(&B, P); break;
        case 4: off = rd(&B, 5) * 8; break;
        case 5: off = rd(&B, 8) * 8; break;
        }
        if (ll == 15) { int v = value(&LL, &B); if (v < 0) return "LL"; ll = 15 + v; }
        if (ll > n - used || ll > BLOCK - o) return "ll too large";
        COPY(ll);
        unsigned ml = mlf + 4;
        if (mlf == 31) { int v = value(&ML, &B); if (v < 0) return "ML"; ml = 35 + v; }
        last = off;
        if (off == 0 || off > o || ml > BLOCK - o) return "bad offset or ml";
        for (unsigned q = 0; q < ml; q++, o++) out[o] = out[o - off];
        if (B.past) return "B: read past end";
    }
    if (B.past) return "B: read past end";
    size_t left = B.nbytes * 8 - B.pos;
    if (left >= 8) return "B: 8 or more bits left";
    if (rd(&B, (int)left)) return "B: bits left not 0";
    return NULL;
}

int main(int argc, char **argv) {
    mkdec(&TOK, TOK_LEN, 3073); mkdec(&LL, LL_LEN, 24); mkdec(&ML, ML_LEN, 24); mkdec(&OWN, OWN_LEN, 19);
    for (int t = 0; t < 8; t++) mkdec(&LIT[t], LIT_LEN + 256 * t, 256);
    static unsigned char out[BLOCK], want[BLOCK], z[1 << 16];
    if (argc < 3) {
        static const unsigned char ex[] = {0x02, 0x00, 0x61, 0x62, 0x23, 0xf2, 0xff, 0xfb, 0x60};
        const char *why = decode(ex, sizeof ex, out);
        if (why) { printf("example: invalid: %s\n", why); return 1; }
        for (int i = 0; i < BLOCK; i++) if (out[i] != "ab"[i & 1]) { printf("example: wrong byte %d\n", i); return 1; }
        printf("example: ok\n");
        return 0;
    }
    long count = atol(argv[2]), bad = 0;
    for (long k = 0; k < count; k++) {
        char p[4096];
        snprintf(p, sizeof p, "%s/%ld.z", argv[1], k);
        FILE *f = fopen(p, "rb");
        if (!f) { perror(p); return 1; }
        size_t len = fread(z, 1, sizeof z, f); fclose(f);
        snprintf(p, sizeof p, "%s/%ld.block", argv[1], k);
        f = fopen(p, "rb");
        if (!f) { perror(p); return 1; }
        size_t bl = fread(want, 1, sizeof want, f); fclose(f);
        const char *why = decode(z, len, out);
        if (why) { if (bad++ < 10) printf("%ld: invalid: %s\n", k, why); continue; }
        if (bl != BLOCK || memcmp(out, want, BLOCK)) { int q = 0; while (q < BLOCK && out[q] == want[q]) q++; if (bad++ < 10) printf("%ld: decodes to other bytes from %d\n", k, q); }
    }
    printf("%ld blocks, %ld bad\n", count, bad);
    return bad != 0;
}
