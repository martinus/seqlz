// SPDX-License-Identifier: MIT OR GPL-2.0-only
/*
 * ownread.c: the data of the animation "ownread" in chapter 13. Compresses chapter 4's text page and
 * the base64 page at level 3, each with a literal table of its own, and runs the loop of
 * read_own_table() again step by step on its description, recording the 64-bit window, the lookup of
 * quad[], the symbols the step takes, the buckets and the code space. Prints them as JSON, with the
 * buckets that own_fill() writes length by length. Fails if the replay does not give the decode table
 * that the real read_own_table() builds and end where it ends, or if a page does not decode.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pages.h"
#include "seqlz_decompress_literals.c"

static int fail(const char* page, const char* what) {
    fprintf(stderr, "ownread.c: %s: %s\n", page, what);
    exit(1);
}

/* the bits of OWN's symbol s, and the run's bits after it for a run */
static unsigned int sym_bits(unsigned int s) {
    return seqlz_tables.own.enc[s] >> 8;
}

static unsigned int run_bits(unsigned int run) {
    unsigned int nb = 32U - (unsigned int)__builtin_clz(run);

    return sym_bits(SEQLZ_OWN_RUN + nb) + nb - 1U;
}

/* returns the steps that took two lengths */
static unsigned int one_page(const char* id, void (*make)(u8*), int comma) {
    static u8 page[SEQLZ_BLOCK_SIZE], dst[2 * SEQLZ_BLOCK_SIZE], out[SEQLZ_BLOCK_SIZE + 16], scratch[SEQLZ_SCRATCH];
    static u8 lt_page[SEQLZ_BLOCK_SIZE], bucket[SEQLZ_SCRATCH], real_bucket[SEQLZ_SCRATCH];
    static u16 lt[1U << SEQLZ_LIT_BITS];
    static struct seqlz_state st;
    const unsigned int full = 1U << SEQLZ_LIT_BITS;
    unsigned int n, w, len, count[OWN_QUAD_NONE + 1] = {0}, at = 0, avail = 0, space = 0, b = 0, k, used, steps = 0, l, o;
    unsigned int twos = 0;
    const u8 *p, *end;
    u64 bits = 0;

    make(page);
    n = seqlz_compress(&st, page, dst, sizeof dst, SEQLZ_LEVEL_OWN);
    if (!n || !(get_unaligned_le16(dst) & SEQLZ_LIT_CODED) || (dst[2] & SEQLZ_LIT_CODE_MASK) != SEQLZ_LIT_OWN)
        fail(id, "no table of its own at level 3");
    if (seqlz_decode(dst, n, out, scratch) || memcmp(out, page, SEQLZ_BLOCK_SIZE))
        fail(id, "does not decode");
    w = SEQLZ_SIZE_BITS_MIN + (dst[2] >> SEQLZ_LIT_WIDTH_AT & 7U);
    p = dst + SEQLZ_LIT_HEADER(w) + (dst[2] >> SEQLZ_LIT_DELTA_AT);
    end = dst + n;
    len = (unsigned int)(end - p);
    printf("%s{\"id\":\"%s\",\"n\":%u,\"at\":%u,\"steps\":[", comma ? "," : "", id, n, (unsigned int)(p - dst));
    /* the loop of read_own_table(), statement by statement, with what each step did */
    while (space < full) {
        unsigned int top, e, l1, l2, next, two, kk, b0 = b, loaded = 0, avail0;
        u64 win;

        if (avail < OWN_STEP_MAX) {
            unsigned int nn = (63U - avail) >> 3;
            u64 ww = at + 8U <= len ? get_unaligned_be64(p + at) : lit_load_tail(p + min(at, len), end);

            bits = (bits | ww >> avail) & ~(~0ULL >> (avail + 8U * nn));
            at += nn;
            avail += 8U * nn;
            loaded = nn;
        }
        win = bits;
        avail0 = avail;
        top = (unsigned int)(bits >> (64U - SEQLZ_OWN_CODE_BITS));
        e = seqlz_tables.own.quad[top];
        printf("%s{\"load\":%u,\"at\":%u,\"avail\":%u,\"w\":\"%016llx\",\"top\":%u,\"e\":%u,",
               steps ? "," : "",
               loaded,
               at,
               avail0,
               (unsigned long long)win,
               top,
               e);
        if (e >> OWN_QUAD_ALONE_AT) {
            unsigned int d = seqlz_tables.own.decode[top];
            unsigned int nb = (d & 0xffU) - SEQLZ_OWN_RUN, run;

            bits <<= d >> 8;
            run = (1U << (nb - 1U)) + (unsigned int)((bits >> 1) >> (64U - nb));
            b += run;
            bits <<= nb - 1U;
            avail -= (d >> 8) + nb - 1U;
            if (run_bits(run) != (d >> 8) + nb - 1U)
                fail(id, "a run alone takes other bits than OWN gives it");
            printf("\"kind\":\"alone\",\"sym\":%u,\"segs\":[[\"run\",%u,%u]],\"k\":%u,\"b0\":%u,\"b\":%u,\"space\":%u}",
                   d & 0xffU,
                   run_bits(run),
                   run,
                   (d >> 8) + nb - 1U,
                   b0,
                   b,
                   space);
            if (b > 256U)
                fail(id, "more than 256 bytes");
            steps++;
            continue;
        }
        l1 = e & 15U;
        b += e >> OWN_QUAD_RUN1_AT & OWN_QUAD_RUN_MAX;
        next = space + (full >> l1);
        two = next < full;
        l2 = two ? e >> 4 & 15U : OWN_QUAD_NONE;
        kk = e >> (two ? OWN_QUAD_END2_AT : OWN_QUAD_END1_AT) & 15U;
        bits <<= kk;
        avail -= kk;
        printf("\"kind\":\"%s\",\"cut\":%u,\"segs\":[",
               two && l2 != OWN_QUAD_NONE ? "two" : "one",
               !two && (e >> 4 & 15U) != OWN_QUAD_NONE);
        {
            /* the symbols of the step, which must add up to the bits it takes */
            unsigned int r1 = e >> OWN_QUAD_RUN1_AT & OWN_QUAD_RUN_MAX, r2 = e >> OWN_QUAD_RUN2_AT & OWN_QUAD_RUN_MAX;
            unsigned int sum = 0, c = 0;

            if (r1) {
                printf("[\"run\",%u,%u]", run_bits(r1), r1);
                sum += run_bits(r1);
                c++;
            }
            printf("%s[\"len\",%u,%u]", c++ ? "," : "", sym_bits(l1 - 1U), l1);
            sum += sym_bits(l1 - 1U);
            if (two && l2 != OWN_QUAD_NONE) {
                if (r2) {
                    printf(",[\"run\",%u,%u]", run_bits(r2), r2);
                    sum += run_bits(r2);
                }
                printf(",[\"len\",%u,%u]", sym_bits(l2 - 1U), l2);
                sum += sym_bits(l2 - 1U);
            }
            if (sum != kk)
                fail(id, "the symbols of a step do not add up to the bits it takes");
        }
        printf("],\"k\":%u,\"b0\":%u,\"l1\":%u,\"b1\":%u,", kk, b0, l1, b);
        bucket[l1 * 256U + (count[l1]++ & 255U)] = (u8)b;
        b += 1U + (two ? e >> OWN_QUAD_RUN2_AT & OWN_QUAD_RUN_MAX : 0U);
        printf("\"l2\":%u,\"b2\":%u,", l2, b);
        twos += two && l2 != OWN_QUAD_NONE;
        bucket[l2 * 256U + (count[l2]++ & 255U)] = (u8)b;
        space = next + (full >> l2);
        b += l2 != OWN_QUAD_NONE;
        printf("\"b\":%u,\"space\":%u}", b, space);
        steps++;
    }
    used = 8U * at - avail;
    if (space != full || b > 256U || (used + 7U) / 8U > len || ((used & 7U) && bits >> (64U - (8U - (used & 7U)))))
        fail(id, "the replay finds the table invalid");
    /* own_fill(), as the real one does it: length by length, each byte's entries in a row */
    printf("],\"used\":%u,\"desc\":[", used);
    for (k = 0; k < (used + 7U) / 8U; k++)
        printf("%s%u", k ? "," : "", p[k]);
    printf("],\"buckets\":[");
    for (l = 1, o = 0; l <= SEQLZ_LIT_BITS; l++) {
        printf("%s[", l > 1 ? "," : "");
        for (k = 0; k < count[l]; k++) {
            unsigned int r;

            printf("%s%u", k ? "," : "", bucket[l * 256U + k]);
            for (r = 0; r < full >> l; r++)
                lt[o++] = (u16)(l | bucket[l * 256U + k] << 8);
        }
        printf("]");
    }
    printf("]}");
    if (o != full)
        fail(id, "the buckets do not fill the decode table");
    if (read_own_table(&seqlz_tables.own, p, end, (u16*)lt_page, real_bucket) != p + (used + 7U) / 8U)
        fail(id, "read_own_table() ends elsewhere");
    if (memcmp(lt_page, lt, sizeof lt))
        fail(id, "the replay gives another decode table than read_own_table()");
    return 2 * twos > steps ? twos : 0;
}

int main(void) {
    unsigned int s;

    printf("{\"full\":%u,\"step_max\":%u,\"none\":%u,\"own\":[", 1U << SEQLZ_LIT_BITS, OWN_STEP_MAX, OWN_QUAD_NONE);
    for (s = 0; s < SEQLZ_OWN_SYMBOLS; s++)
        printf("%s%u", s ? "," : "", sym_bits(s));
    printf("],\"pages\":[");
    one_page("text", words_page, 0);
    /* chapter 13 says that most lookups take two lengths on the base64 page */
    if (!one_page("base64", base64_page, 1))
        fail("base64", "most steps take one length or a run");
    printf("]}\n");
    return 0;
}
