/* cr: a writer of seqlz compressed blocks (format.md), driven by choice bytes. C11.
 *
 * Every choice the format leaves open is taken from the next choice byte(s), choices[i], 0 after the
 * end. A choice among k options takes one byte modulo k (two bytes, little endian, for k > 256), and
 * option 0 is always valid, so any choice bytes give a valid block. A choice with one option reads no
 * byte. The order in which choices are read, and what each one picks, is in FEATURES.md. */
#include "cr.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cr_tables.h"

const struct cr_family cr_families[] = {
#define CR_FAM(id, name, count, items, section) {name, CRF_##id, count, items, section},
#include "cr_features.def"
#undef CR_FAM
};
const int cr_nfamilies = (int)(sizeof cr_families / sizeof cr_families[0]);

const char *cr_item_name(int f, int i, char *buf, size_t size)
{
    const char *it = cr_families[f].items;
    if (it[0] == '#') {
        snprintf(buf, size, "%s=%d", cr_families[f].name, atoi(it + 1) + i);
        return buf;
    }
    while (i > 0 && *it) {
        if (*it++ == '|') i--;
    }
    size_t k = 0;
    while (it[k] && it[k] != '|') k++;
    snprintf(buf, size, "%s: %.*s", cr_families[f].name, (int)k, it);
    return buf;
}

/* ---- prefix codes, as in "Prefix codes" ---- */

static void canon(const unsigned char *len, int n, unsigned short *code)
{
    int count[16] = {0}, next[16] = {0};
    for (int s = 0; s < n; s++)
        if (len[s]) count[len[s]]++;
    next[1] = 0;
    for (int l = 2; l < 16; l++) next[l] = (next[l - 1] + count[l - 1]) * 2;
    for (int s = 0; s < n; s++)
        if (len[s]) code[s] = (unsigned short)next[len[s]]++;
}

static unsigned short code_tok[3073], code_ll[24], code_ml[24], code_lit[8][256], code_own[19];
static int tables_ready;

static void init_tables(void)
{
    if (tables_ready) return;
    canon(cr_len_tok, 3073, code_tok);
    canon(cr_len_ll, 24, code_ll);
    canon(cr_len_ml, 24, code_ml);
    for (int t = 0; t < 8; t++) canon(cr_len_lit + 256 * t, 256, code_lit[t]);
    canon(cr_len_own, 19, code_own);
    tables_ready = 1;
}

/* ---- bits, most significant first ---- */

struct bw { unsigned char *b; size_t bits; };

static void put(struct bw *w, unsigned v, int n)
{
    for (int i = n - 1; i >= 0; i--) {
        if ((v >> i) & 1) w->b[w->bits >> 3] |= (unsigned char)(0x80 >> (w->bits & 7));
        w->bits++;
    }
}

/* ---- choice bytes ---- */

struct rd { const unsigned char *p; size_t n, i; };

static unsigned u8(struct rd *r)
{
    if (r->i < r->n) return r->p[r->i++];
    return 0;
}

/* A number in [0, k), k from 1 to 65536. */
static unsigned pick(struct rd *r, unsigned k)
{
    if (k <= 1) return 0;
    if (k <= 256) return u8(r) % k;
    unsigned v = u8(r);
    v |= u8(r) << 8;
    return v % k;
}

static unsigned range(struct rd *r, unsigned lo, unsigned hi) { return lo + pick(r, hi - lo + 1); }

/* A number in [lo, hi], its ends more often: a first byte picks lo (0), hi (1) or any (2, 3). */
static unsigned range_e(struct rd *r, unsigned lo, unsigned hi)
{
    if (hi - lo < 2) return range(r, lo, hi);
    switch (pick(r, 4)) {
    case 0: return lo;
    case 1: return hi;
    default: return range(r, lo, hi);
    }
}

/* ---- lengths and length values ---- */

/* Category c of a length: [lo, hi]. For ll, c < 15 is the ll field c, else LL symbol c - 15 (with
 * field 15); for ml, c < 31 is mlf c, else ML symbol c - 31 (with mlf 31). base: 15 or 35. */
static void cat_range(int c, int direct, int base, int add, unsigned *lo, unsigned *hi)
{
    if (c < direct) { *lo = *hi = (unsigned)(c + add); return; }
    int s = c - direct;
    if (s < 16) { *lo = *hi = (unsigned)(base + s); return; }
    int b = s - 12;
    *lo = (unsigned)(base + (1 << b));
    *hi = (unsigned)(base + (1 << (b + 1)) - 1);
}

/* Pick a length in [mn, mx] (mn <= mx): first its category, uniformly among the valid ones in the
 * block's window [wlo, whi] (the valid one nearest the window if none is), then the value in it
 * with range_e(). */
static unsigned pick_len(struct rd *r, int ncat, int direct, int base, int add, unsigned mn, unsigned mx,
                         int wlo, int whi)
{
    int valid[64], nv = 0, below = -1, above = -1;
    for (int c = 0; c < ncat; c++) {
        unsigned lo, hi;
        cat_range(c, direct, base, add, &lo, &hi);
        if (lo > mx || hi < mn) continue;
        if (c < wlo) below = c;
        else if (c > whi) { if (above < 0) above = c; }
        else valid[nv++] = c;
    }
    if (nv == 0) valid[nv++] = below >= 0 ? below : above;
    int c = valid[pick(r, (unsigned)nv)];
    unsigned lo, hi;
    cat_range(c, direct, base, add, &lo, &hi);
    if (lo < mn) lo = mn;
    if (hi > mx) hi = mx;
    return range_e(r, lo, hi);
}

static int ilog2(unsigned v)
{
    int b = 0;
    while (v >> (b + 1)) b++;
    return b;
}

static void put_value(struct bw *w, const unsigned char *len, const unsigned short *code, unsigned v,
                      unsigned long *cnt, int fam)
{
    if (v < 16) {
        put(w, code[v], len[v]);
        if (cnt) cnt[fam + (int)v]++;
        return;
    }
    int b = ilog2(v);
    put(w, code[b + 12], len[b + 12]);
    put(w, v - (1u << b), b);
    if (cnt) cnt[fam + b + 12]++;
}

/* ---- a block's own literal table ---- */

static int need_codes(unsigned u) /* fewest codes of 1 to 10 bits that fill u / 1024 */
{
    if (u == 1024) return 2;
    int c = 0;
    while (u) { c += (int)(u & 1); u >>= 1; }
    return c;
}

static int fits(unsigned u, int values) { return u == 0 || need_codes(u) <= values; }

/* Chooses the code lengths own[0..255], writes the description to dw. Returns the number of codes. */
static int make_own(struct rd *r, unsigned char *own, struct bw *dw, unsigned long *cnt)
{
    unsigned zq = pick(r, 9);       /* a byte value gets no code with chance zq / 8 */
    int lo = (int)range_e(r, 1, 10);    /* preferred lengths: [lo, hi] */
    int hi = (int)range_e(r, (unsigned)lo, 10);
    unsigned u = 1024;              /* code space left, in units of 2^-10 */
    int ncodes = 0, lastv = -1;
    memset(own, 0, 256);
    for (int v = 0; v < 256 && u > 0; v++) {
        int after = 255 - v;
        int ok[11], nok = 0, nlo = 0;
        for (int l = 1; l <= 10; l++) {
            unsigned c = 1u << (10 - l);
            if (c <= u && fits(u - c, after)) ok[nok++] = l;
        }
        for (int i = 0; i < nok; i++) nlo += ok[i] >= lo && ok[i] <= hi;
        int skip_ok = fits(u, after);
        if (skip_ok && (nok == 0 || pick(r, 8) < zq)) continue;
        int l;
        if (nlo > 0) {
            int k = (int)pick(r, (unsigned)nlo);
            int i = 0;
            while (ok[i] < lo) i++;
            l = ok[i + k];
        } else {
            l = ok[pick(r, (unsigned)nok)];
        }
        own[v] = (unsigned char)l;
        u -= 1u << (10 - l);
        ncodes++;
        lastv = v;
    }
    /* The description: lengths, and runs of byte values without a code split in any way. */
    for (int v = 0; v <= lastv;) {
        if (own[v]) {
            int s = own[v] - 1;
            put(dw, code_own[s], cr_len_own[s]);
            if (cnt) cnt[CRF_OWNSYM + s]++;
            v++;
            continue;
        }
        int z = 0;
        while (!own[v + z]) z++;
        int pieces = 0;
        while (z > 0) {
            int kmax = ilog2((unsigned)(z < 511 ? z : 511)) + 1;
            int k = 1 + (int)pick(r, (unsigned)kmax);
            unsigned base = 1u << (k - 1);
            unsigned xmax = base - 1;
            if (xmax > (unsigned)z - base) xmax = (unsigned)z - base;
            unsigned x = pick(r, xmax + 1);
            put(dw, code_own[9 + k], cr_len_own[9 + k]);
            put(dw, x, k - 1);
            if (cnt) cnt[CRF_OWNSYM + 9 + k]++;
            z -= (int)(base + x);
            v += (int)(base + x);
            pieces++;
        }
        if (cnt && pieces > 1) cnt[CRF_OWNX + 0] += (unsigned long)(pieces - 1);
    }
    if (cnt) {
        if (lastv < 255) cnt[CRF_OWNX + 2]++;
        cnt[CRF_OWNX + ((dw->bits & 7) ? 4 : 3)]++;
        cnt[CRF_OWNX + (ncodes == 2 ? 5 : ncodes < 16 ? 6 : ncodes < 256 ? 7 : 8)]++;
    }
    return ncodes;
}

/* ---- the writer ---- */

struct st {
    struct rd r;
    unsigned long *cnt;
    int coded, code, e, dt;
    int vals[256], nvals;  /* the byte values with a code, for the literals */
    unsigned char out[CR_BLOCK];
    unsigned pos, last;
    unsigned char lit[CR_BLOCK]; /* the literals as stored */
    unsigned nlit;
};

#define CNT(fam, i) do { if (s->cnt) s->cnt[CRF_##fam + (i)]++; } while (0)

static void literals(struct st *s, unsigned k)
{
    for (unsigned i = 0; i < k; i++) {
        unsigned v = s->coded ? (unsigned)s->vals[pick(&s->r, (unsigned)s->nvals)] : u8(&s->r);
        unsigned b = v;
        if (s->coded && s->e) {
            unsigned x = s->dt == 1 ? 4 : s->dt == 2 ? 8 : (s->last <= 2 ? 4 : s->last == 3 ? 6 : s->last);
            if (s->dt == 3) CNT(DT3, s->last >= 4 ? 3 : (int)s->last - 1);
            if (s->pos >= x) {
                b = (v + s->out[s->pos - x]) & 255;
                CNT(DELTAP, 1);
            } else {
                CNT(DELTAP, 0);
            }
        }
        s->lit[s->nlit++] = (unsigned char)v;
        s->out[s->pos++] = (unsigned char)b;
    }
}

size_t cr_write_stats(const unsigned char *choices, size_t n, unsigned char *dst, size_t cap,
                      unsigned char *block, unsigned long *counts)
{
    static struct st S;  /* not reentrant; large */
    static unsigned char bbuf[8192], desc[1024], sbuf[8][1024], tmp[8192];
    struct st *s = &S;
    init_tables();
    memset(s, 0, sizeof *s);
    s->r.p = choices;
    s->r.n = n;
    s->cnt = counts;
    s->last = 1;

    /* layout */
    unsigned char own[256];
    struct bw dw = {desc, 0};
    memset(desc, 0, sizeof desc);
    s->coded = (int)pick(&s->r, 2);
    CNT(LAYOUT, s->coded);
    const unsigned char *litlen = NULL;
    const unsigned short *litcode = NULL;
    unsigned short owncode[256];
    if (s->coded) {
        s->code = (int)pick(&s->r, 9);
        s->e = (int)pick(&s->r, 2);
        if (s->e) s->dt = 1 + (int)pick(&s->r, 3);
        CNT(CODE, s->code);
        CNT(DELTA, s->dt);
        if (s->code == 8) {
            make_own(&s->r, own, &dw, counts);
            canon(own, 256, owncode);
            litlen = own;
            litcode = owncode;
        } else {
            litlen = cr_len_lit + 256 * s->code;
            litcode = code_lit[s->code];
        }
        for (int v = 0; v < 256; v++)
            if (litlen[v]) s->vals[s->nvals++] = v;
    }

    /* the sequences */
    /* per block: the chance to end before each sequence, 2^-ek (ek 0: at once, 13: never), and the
     * windows of length categories; mode 0: the shortest sequences, else windows [lo, hi] */
    unsigned ek = pick(&s->r, 14);
    int llw[2] = {0, 0}, mlw[2] = {0, 0};
    if (pick(&s->r, 8) != 0) {
        llw[0] = (int)range_e(&s->r, 0, 38);
        llw[1] = (int)range_e(&s->r, (unsigned)llw[0], 38);
        mlw[0] = (int)range_e(&s->r, 0, 54);
        mlw[1] = (int)range_e(&s->r, (unsigned)mlw[0], 54);
    }
    struct bw bb = {bbuf, 0};
    memset(bbuf, 0, sizeof bbuf);
    unsigned nseq = 0;
    for (;;) {
        unsigned left = CR_BLOCK - s->pos;
        unsigned llmin = s->pos == 0 ? 1 : 0;
        if (left < 4 + llmin || ek == 0 || (ek < 13 && pick(&s->r, 1u << ek) == 0)) {
            if (nseq) CNT(LASTLL, left == 0 ? 0 : left < 4 ? 1 : 2);
            literals(s, left);
            break;
        }
        unsigned ll = pick_len(&s->r, 39, 15, 15, 0, llmin, left - 4, llw[0], llw[1]);
        literals(s, ll);
        unsigned o = s->pos;
        int cls[6], ncls = 0;
        cls[ncls++] = 0;
        if (o >= 2) cls[ncls++] = 1;
        cls[ncls++] = 2;
        cls[ncls++] = 3;
        if (o >= 8) { cls[ncls++] = 4; cls[ncls++] = 5; }
        int c = cls[pick(&s->r, (unsigned)ncls)];
        unsigned off;
        switch (c) {
        case 0: off = s->last; break;
        case 1: off = 2; break;
        case 2: off = range_e(&s->r, 1, o < 255 ? o : 255); break;
        case 3: off = range_e(&s->r, 1, o < 4095 ? o : 4095); break;
        case 4: off = 8 * range_e(&s->r, 1, o / 8 < 31 ? o / 8 : 31); break;
        default: off = 8 * range_e(&s->r, 1, o / 8 < 255 ? o / 8 : 255); break;
        }
        unsigned ml = pick_len(&s->r, 55, 31, 35, 4, 4, CR_BLOCK - o, mlw[0], mlw[1]);
        unsigned llf = ll < 15 ? ll : 15, mlf = ml - 4 < 31 ? ml - 4 : 31;
        unsigned tok = llf | mlf << 4 | (unsigned)c << 9;
        unsigned long tok_before = s->cnt ? s->cnt[CRF_TOK] : 0;
        if (cr_len_tok[tok] && pick(&s->r, 2) == 0) {
            put(&bb, code_tok[tok], cr_len_tok[tok]);
            CNT(TOK, 0);
        } else {
            put(&bb, code_tok[3072], cr_len_tok[3072]);
            put(&bb, tok, 12);
            CNT(TOK, cr_len_tok[tok] ? 1 : 2);
        }
        switch (c) {
        case 1: put(&bb, 1, 1); break;
        case 2: put(&bb, off, 8); break;
        case 3: put(&bb, off, 12); break;
        case 4: put(&bb, off / 8, 5); break;
        case 5: put(&bb, off / 8, 8); break;
        default: break;
        }
        if (llf == 15) put_value(&bb, cr_len_ll, code_ll, ll - 15, counts, CRF_LLSYM);
        if (mlf == 31) put_value(&bb, cr_len_ml, code_ml, ml - 35, counts, CRF_MLSYM);
        CNT(TOKV, (int)tok);
        if (cr_len_tok[tok] && s->cnt && s->cnt[CRF_TOK] != tok_before) CNT(TOKC, (int)tok);
        CNT(LLV, (int)ll);
        CNT(MLV, (int)ml);
        if (c >= 2) { if (s->cnt) s->cnt[CRF_OFFV2 + 4096 * (c - 2) + (int)off]++; }
        CNT(LLF, (int)llf);
        CNT(MLF, (int)mlf);
        CNT(CLASS, c);
        if (c == 0 && nseq == 0) CNT(OFF, 0);
        if (c != 0 && off == s->last) CNT(OFF, 1);
        if (off < ml) CNT(OFF, 2);
        if (off == o) CNT(OFF, 3);
        if (off == 1) CNT(OFF, 4);
        for (unsigned i = 0; i < ml; i++, s->pos++) s->out[s->pos] = s->out[s->pos - off];
        if (s->pos == CR_BLOCK) CNT(OFF, 5);
        s->last = off;
        nseq++;
    }
    size_t blen = (bb.bits + 7) / 8;
    CNT(BPAD, bb.bits == 0 ? 0 : (bb.bits & 7) ? 2 : 1);
    CNT(NSEQ, nseq == 0 ? 0 : nseq == 1 ? 1 : nseq < 16 ? 2 : nseq < 256 ? 3 : nseq < 1023 ? 4 : 5);
    CNT(NLIT, s->nlit == 1 ? 0 : s->nlit < 16 ? 1 : s->nlit < 256 ? 2 : s->nlit < 4096 ? 3 : 4);

    /* assemble */
    size_t len = 0;
    tmp[len++] = (unsigned char)(s->nlit & 255);
    tmp[len++] = (unsigned char)((s->nlit >> 8) | (s->coded ? 0x80 : 0));
    if (!s->coded) {
        memcpy(tmp + len, s->lit, s->nlit);
        for (unsigned i = 0; i < s->nlit; i++) CNT(RAWV, s->lit[i]);
        len += s->nlit;
    } else {
        size_t sz[8];
        unsigned smax = 0;
        memset(sbuf, 0, sizeof sbuf);
        struct bw sw[8];
        for (int j = 0; j < 8; j++) { sw[j].b = sbuf[j]; sw[j].bits = 0; }
        for (unsigned i = 0; i < s->nlit; i++) {
            unsigned v = s->lit[i];
            put(&sw[i & 7], litcode[v], litlen[v]);
            CNT(LITLEN, litlen[v] - 1);
            CNT(LITV, 256 * s->code + (int)v);
        }
        for (int j = 0; j < 8; j++) {
            sz[j] = (sw[j].bits + 7) / 8;
            if (sz[j] > smax) smax = (unsigned)sz[j];
            CNT(STREAM, sz[j] == 0 ? 0 : (sw[j].bits & 7) ? 2 : 1);
        }
        int wmin = 5;
        while (smax >> wmin) wmin++;
        int w = wmin + (int)pick(&s->r, (unsigned)(13 - wmin));
        CNT(W, w - 5);
        CNT(WMIN, w > wmin);
        if (s->code == 8 && counts) {
            int used[256] = {0};
            for (unsigned i = 0; i < s->nlit; i++) used[s->lit[i]] = 1;
            for (int v = 0; v < 256; v++)
                if (own[v] && !used[v]) counts[CRF_OWNX + 1]++;
        }
        tmp[len++] = (unsigned char)(s->code | (w - 5) << 4 | s->e << 7);
        if (s->e) tmp[len++] = (unsigned char)s->dt;
        /* S: 8 sizes of w bits, little endian, in w bytes */
        unsigned char S[12] = {0};
        for (int j = 0; j < 8; j++)
            for (int b = 0; b < w; b++)
                if ((sz[j] >> b) & 1) S[(j * w + b) / 8] |= (unsigned char)(1 << ((j * w + b) % 8));
        memcpy(tmp + len, S, (size_t)w);
        len += (size_t)w;
        size_t d = (dw.bits + 7) / 8;
        memcpy(tmp + len, desc, d);
        len += d;
        for (int j = 0; j < 8; j++) {
            memcpy(tmp + len, sbuf[j], sz[j]);
            len += sz[j];
        }
    }
    memcpy(tmp + len, bbuf, blen);
    len += blen;
    memcpy(block, s->out, CR_BLOCK);
    if (len > cap) return 0;
    memcpy(dst, tmp, len);
    return len;
}

size_t cr_write(const unsigned char *choices, size_t n, unsigned char *dst, size_t cap,
                unsigned char *block)
{
    return cr_write_stats(choices, n, dst, cap, block, NULL);
}
