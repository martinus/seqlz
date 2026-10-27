// SPDX-License-Identifier: MIT OR GPL-2.0-only
/*
 * ownlens.c: the data of the animation "ownlens" in chapter 11. Compresses chapter 4's text page and the
 * base64 page as level 3 does, counts their literals with lit_sums() and builds their own code with
 * own_code(), then runs own_lengths() again on the same counts, recording each byte's first length and
 * gain, the order of the counting sort, and every length the two loops change. Prints them as JSON.
 * Fails if the replay ends with other lengths or gains than own_lengths(), or if a page does not get a
 * table of its own.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pages.h"
#include "seqlz_compress.c"
#include "seqlz_compress_literals.c"

static int fail(const char* page, const char* what) {
    fprintf(stderr, "ownlens.c: %s: %s\n", page, what);
    exit(1);
}

/* returns the lengths the two loops changed */
static unsigned int one_page(const char* id, void (*make)(u8*), int comma) {
    static u8 page[SEQLZ_BLOCK_SIZE], dst[2 * SEQLZ_BLOCK_SIZE], level3[2 * SEQLZ_BLOCK_SIZE];
    static struct seqlz_state st;
    struct own_work* w = own_work(&st);
    const unsigned int full = 1U << SEQLZ_LIT_BITS;
    u64 even[SEQLZ_LIT_STREAMS] = {0}, odd[SEQLZ_LIT_STREAMS] = {0};
    unsigned int len, delta, n_lit, n, k, j, bits = ~0U, set = 0, coded = 0, sizes[SEQLZ_LIT_STREAMS], header;
    unsigned int gain[256], at[16 + 1] = {0}, by_gain[256], cur[256], space = 0, rows = 0, round, changed = 0;
    u64 inv;

    make(page);
    if ((seqlz_compress(&st, page, level3, sizeof level3, SEQLZ_LEVEL_OWN) == 0) ||
        (level3[2] & SEQLZ_LIT_CODE_MASK) != SEQLZ_LIT_OWN)
        fail(id, "no table of its own at level 3");
    /* seqlz_compress() at level 3 up to own_code(), as seqlz_code_own() and code_literals() do it */
    len = compress_own(&st, page, dst, sizeof dst, &delta);
    if (!len || delta)
        fail(id, "the literals are deltas, which this figure does not show");
    n_lit = get_unaligned_le16(dst);
    memset(w->freq, 0, sizeof w->freq);
    lit_sums(dst + SEQLZ_HEADER, n_lit, even, odd, w->freq);
    for (k = 0; k < SEQLZ_LIT_SETS; k++) {
        unsigned int b = 0;

        for (j = 0; j < SEQLZ_LIT_STREAMS; j++)
            b += lit_bits(even, odd, j, k);
        if (b < bits) {
            bits = b;
            set = k;
        }
    }
    for (j = 0; j < SEQLZ_LIT_STREAMS; j++) {
        sizes[j] = (lit_bits(even, odd, j, set) + 7U) / 8U;
        coded += sizes[j];
    }
    header = SEQLZ_LIT_HEADER(size_width(sizes));
    if (!own_code(w, n_lit, header + coded))
        fail(id, "own_code() builds no code");
    /* the bytes that are literals, which own_code() put in w->bytes[] in order, and summed counts */
    for (k = 0, n = 0; k < 256; k++)
        if (w->freq[0][k] && w->bytes[n++] != k)
            fail(id, "w->bytes[] is not the bytes that are literals");
    /* own_lengths() again, on the bytes and the counts own_code() gave it */
    inv = div_u64(1ULL << 40, n_lit);
    printf("%s{\"id\":\"%s\",\"n_lit\":%u,\"inv\":%llu,\"bytes\":[", comma ? "," : "", id, n_lit, (unsigned long long)inv);
    for (k = 0; k < n; k++) {
        unsigned int b = w->bytes[k], f = w->freq[0][b], l = (unsigned int)(__builtin_clz(f) - __builtin_clz(n_lit));
        u64 q;

        l += (f << l) < n_lit;
        l += !l;
        q = ((u64)f << l) * inv >> 36;
        q = l > SEQLZ_LIT_BITS || q < 16U ? 16U : q > 31U ? 31U : q;
        if (q - 16U != w->gain[k])
            fail(id, "the replay gives another gain");
        printf("%s[%u,%u,%u,%u,%llu]", k ? "," : "", b, f, min(l, SEQLZ_LIT_BITS), l, (unsigned long long)q);
        l = min(l, SEQLZ_LIT_BITS);
        cur[b] = l;
        gain[k] = (unsigned int)(q - 16U);
        at[q - 15U]++;
        space += full >> l;
    }
    for (k = 1; k <= 16; k++)
        at[k] += at[k - 1];
    for (k = 0; k < n; k++)
        by_gain[at[gain[k]]++] = w->bytes[k];
    printf("],\"space0\":%u,\"by_gain\":[", space);
    for (k = 0; k < n; k++) {
        if (by_gain[k] != w->by_gain[k])
            fail(id, "the replay sorts otherwise");
        printf("%s%u", k ? "," : "", by_gain[k]);
    }
    /*
     * the two loops: every byte a pass visits, its length before, whether it changed, and the space
     * after
     */
    printf("],\"visits\":[");
    for (round = 0; space > full; round++)
        for (k = 0; space > full && k < n; k++) {
            unsigned int b = by_gain[k], l = cur[b], c = l < SEQLZ_LIT_BITS;

            changed += c;
            if (c) {
                space -= full >> (l + 1U);
                cur[b] = l + 1U;
            }
            printf("%s[\"over\",%u,%u,%u,%u,%u,%u]", rows++ ? "," : "", round, k, b, l, c, space);
        }
    for (round = 0; space < full; round++)
        for (k = n; k-- > 0;) {
            unsigned int b = by_gain[k], l = cur[b], c = l > 1U && space + (full >> l) <= full;

            changed += c;
            if (c) {
                space += full >> l;
                cur[b] = l - 1U;
            }
            printf("%s[\"room\",%u,%u,%u,%u,%u,%u]", rows++ ? "," : "", round, k, b, l, c, space);
        }
    printf("]}");
    for (k = 0; k < n; k++)
        if (cur[w->bytes[k]] != w->len[w->bytes[k]])
            fail(id, "the replay gives other lengths than own_lengths()");
    return changed;
}

int main(void) {
    unsigned int text;

    printf("{\"full\":%u,\"max\":%u,\"pages\":[", 1U << SEQLZ_LIT_BITS, SEQLZ_LIT_BITS);
    /* chapter 11 says that the second loop has more to do on the base64 page */
    text = one_page("text", words_page, 0);
    if (one_page("base64", base64_page, 1) <= text)
        fail("base64", "fewer lengths change than on the text page");
    printf("]}\n");
    return 0;
}
