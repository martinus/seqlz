// SPDX-License-Identifier: MIT OR GPL-2.0-only
/*
 * inplace.c: the data of the animation "inplace" in chapter 11. Compresses chapter 16's page with raw
 * literals, then lets code_literals() code them at level 2, and runs lit_streams() again in place from the
 * copy, as level 3 writes the pages it prices on every literal and level 2 wrote all until 31 Oct 2026
 * with the encoder's own lit_put() and lit_flush(), recording for each run of literals which literals
 * it reads from the copy and where each stream's flush stores in dst. Prints them as JSON with the
 * layout of dst before and after. Fails if the replay writes other bytes than code_literals(), if a
 * store leaves its stream's room or reaches the bitstream, or if the page does not decode.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pages.h"
#include "seqlz_compress_literals.c"

static int fail(const char* what) {
    fprintf(stderr, "inplace.c: %s\n", what);
    exit(1);
}

static u8 raw[2 * SEQLZ_BLOCK_SIZE], coded[2 * SEQLZ_BLOCK_SIZE], replay[2 * SEQLZ_BLOCK_SIZE];
static u8* q[SEQLZ_LIT_STREAMS + 1];
static unsigned int bitstream_at, steps;

/* one flush of stream j, as lit_streams() does it, and where it stored */
static u8* flush(unsigned int j, u8* p, u64 a, unsigned int* c, unsigned int* s) {
    unsigned int n = *c + (*s & 255U), store = q[j + 1] - p >= 8 ? 8U : (n + 7U) >> 3;
    u8* r = lit_flush(p, q[j + 1], a, c, s);

    if (p < q[j] || p + store > q[j + 1] || (unsigned int)(p + store - replay) > bitstream_at)
        fail("a store leaves its stream or reaches the bitstream");
    printf("%s[%u,%u,%u,%u]", j % 4 ? "," : "", j, (unsigned int)(p - replay), (unsigned int)(r - replay), store);
    return r;
}

/* lit_streams() again, statement by statement, with a record per run of literals and its flushes */
static void streams(const u32* enc, const u8* literals, unsigned int n_literals) {
    const unsigned int run = 4U * SEQLZ_LIT_STREAMS;
    unsigned int half, k, j;

    for (half = 0; half < SEQLZ_LIT_STREAMS; half += 4) {
        u8 *p0 = q[half], *p1 = q[half + 1], *p2 = q[half + 2], *p3 = q[half + 3];
        u64 a0 = 0, a1 = 0, a2 = 0, a3 = 0;
        unsigned int c0 = 0, c1 = 0, c2 = 0, c3 = 0, s0 = 0, s1 = 0, s2 = 0, s3 = 0, from;

        for (k = half; k + run - 4U < n_literals; k += run) {
            for (j = 0; j < run; j += SEQLZ_LIT_STREAMS) {
                a0 = lit_put(a0, &s0, enc, literals[k + j]);
                a1 = lit_put(a1, &s1, enc, literals[k + j + 1]);
                a2 = lit_put(a2, &s2, enc, literals[k + j + 2]);
                a3 = lit_put(a3, &s3, enc, literals[k + j + 3]);
            }
            printf("%s{\"half\":%u,\"from\":%u,\"to\":%u,\"tail\":0,\"flush\":[", steps++ ? "," : "", half, k, k + run - 4U);
            p0 = flush(half, p0, a0, &c0, &s0);
            p1 = flush(half + 1, p1, a1, &c1, &s1);
            p2 = flush(half + 2, p2, a2, &c2, &s2);
            p3 = flush(half + 3, p3, a3, &c3, &s3);
            printf("]}");
            /*
             * chapter 11 says that from the first run on, a stream stores past the byte of dst where
             * the run's last literal was: in place, without the copy, it would overwrite literals
             * not read yet
             */
            if (steps == 1 && (unsigned int)(p3 - replay) <= SEQLZ_HEADER + k + run - 4U)
                fail("the first run stores nothing past the raw literals it reads");
        }
        for (from = k; k < n_literals; k++) {
            switch (k % SEQLZ_LIT_STREAMS) {
            case 0:
            case 4:
                a0 = lit_put(a0, &s0, enc, literals[k]);
                break;
            case 1:
            case 5:
                a1 = lit_put(a1, &s1, enc, literals[k]);
                break;
            case 2:
            case 6:
                a2 = lit_put(a2, &s2, enc, literals[k]);
                break;
            default:
                a3 = lit_put(a3, &s3, enc, literals[k]);
            }
            if ((k & 3U) == 3U)
                k += 4;
        }
        printf(
            "%s{\"half\":%u,\"from\":%u,\"to\":%u,\"tail\":1,\"flush\":[", steps++ ? "," : "", half, from, min(k, n_literals));
        p0 = flush(half, p0, a0, &c0, &s0);
        p1 = flush(half + 1, p1, a1, &c1, &s1);
        p2 = flush(half + 2, p2, a2, &c2, &s2);
        p3 = flush(half + 3, p3, a3, &c3, &s3);
        printf("]}");
    }
}

int main(void) {
    static u8 page[SEQLZ_BLOCK_SIZE], out[SEQLZ_BLOCK_SIZE], scratch[SEQLZ_SCRATCH], level2[2 * SEQLZ_BLOCK_SIZE];
    static struct seqlz_state st;
    unsigned int len, n, n_lit, set, width, header, j, sz[SEQLZ_LIT_STREAMS];
    unsigned long long h = 0;

    slab_page(page);
    len = seqlz_compress_block(&st, page, raw, sizeof raw, 1);
    if (!len || seqlz_compress_block(&st, page, level2, sizeof level2, 2) == 0)
        fail("compress");
    n_lit = get_unaligned_le16(raw);
    bitstream_at = SEQLZ_HEADER + n_lit;
    /* level 2 in place, as seqlz_compress_block() calls it, with the matcher's table as the copy */
    memcpy(coded, raw, len);
    n = seqlz_code_literals(coded, len, st.literals);
    if (n == len || memcmp(coded, level2, n))
        fail("the page's literals are not coded as at level 2");
    if (seqlz_decompress(coded, n, out, scratch) || memcmp(out, page, SEQLZ_BLOCK_SIZE))
        fail("the page does not decode");
    set = coded[2] & (SEQLZ_LIT_SETS - 1U);
    width = SEQLZ_SIZE_BITS_MIN + (coded[2] >> SEQLZ_LIT_WIDTH_AT & 7U);
    header = SEQLZ_LIT_HEADER(width);
    for (j = 0; j < width; j++)
        h |= (unsigned long long)coded[SEQLZ_LIT_HEADER(0) + j] << (8 * j);
    /* the streams in the raw page, where code_literals() writes them */
    memcpy(replay, raw, len);
    q[0] = replay + header;
    for (j = 0; j < SEQLZ_LIT_STREAMS; j++) {
        sz[j] = (unsigned int)(h >> (j * width)) & ((1U << width) - 1U);
        q[j + 1] = q[j] + sz[j];
    }
    if ((unsigned int)(q[8] - replay) >= bitstream_at)
        fail("the streams reach the bitstream");
    printf("{\"len\":%u,\"n_lit\":%u,\"set\":%u,\"width\":%u,\"header\":%u,\"coded\":%u,\"q\":[",
           len,
           n_lit,
           set,
           width,
           header,
           n);
    for (j = 0; j <= SEQLZ_LIT_STREAMS; j++)
        printf("%s%u", j ? "," : "", (unsigned int)(q[j] - replay));
    printf("],\"head\":[");
    for (j = 0; j < header; j++)
        printf("%s%u", j ? "," : "", coded[j]);
    printf("],\"runs\":[");
    streams(seqlz_tables.lit[set].enc, raw + SEQLZ_HEADER, n_lit);
    printf("]}\n");
    /* the replay wrote the streams code_literals() wrote, and left the raw bitstream alone */
    if (memcmp(replay + header, coded + header, q[8] - q[0]) ||
        memcmp(replay + bitstream_at, raw + bitstream_at, len - bitstream_at))
        fail("the replay of lit_streams() writes other bytes");
    if (memcmp(coded + (q[8] - replay), raw + bitstream_at, len - bitstream_at) || n != (q[8] - replay) + len - bitstream_at)
        fail("the bitstream does not move down behind the streams");
    return 0;
}
