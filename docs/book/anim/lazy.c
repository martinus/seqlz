// SPDX-License-Identifier: MIT OR GPL-2.0-only
/*
 * lazy.c: chapter 9's made-up page for level 3's lazy step and its step back, both of which take
 * bytes there. Random bytes as random_page() of facts/ch07_09.c makes them, with three copies: bytes
 * 10 to 29 at 60, a match of 20 that puts only position 78 into the hash table; the 5 bytes at 77 at
 * A = 130; and the bytes 76 to 89 at 199. At P = 200 the table has A, a match of 5; at P + 1 it has
 * 78, a longer one; and the step back finds 76 and 77 before it. Prints the bytes around the three
 * places, the lengths by comparing bytes, and the sequences of seqlz_find() at levels 2 and 3. Fails
 * unless the matcher does what the animation says.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pages.h"
#include "seqlz_encoder.h" /* the matcher hands its sequences to encode_seq() */
#include "seqlz_match.h"

#define A 130U
#define B 78U /* the source of the longer match at P + 1 */
#define P 200U
#define L 12U /* the length of the copy at P + 1 */

static void fail(const char* what) {
    fprintf(stderr, "lazy.c: %s\n", what);
    exit(1);
}

static void bytes(const char* name, const u8* page, unsigned int from, unsigned int n) {
    unsigned int k;

    printf("\"%s\":{\"from\":%u,\"b\":[", name, from);
    for (k = 0; k < n; k++)
        printf("%s%u", k ? "," : "", page[from + k]);
    printf("]},");
}

/* the sequences of seqlz_find() at a level, as [at, ll, ml, off] */
static unsigned int seqs(const u8* page, unsigned int level, unsigned int out[][4]) {
    static struct seqlz_state st;
    static struct seqlz_sequence seq[SEQLZ_MAX_SEQUENCES];
    unsigned int n = seqlz_find(&st, page, seq, level), k, pos = 0;

    for (k = 0; k + 1 < n; k++) {
        out[k][0] = pos + seq[k].literals;
        out[k][1] = seq[k].literals;
        out[k][2] = seq[k].match;
        out[k][3] = seq[k].offset;
        pos += seq[k].literals + seq[k].match;
    }
    return n - 1;
}

static unsigned int same(const u8* page, unsigned int a, unsigned int b) {
    unsigned int n = 0;

    while (b + n < SEQLZ_BLOCK_SIZE && page[a + n] == page[b + n])
        n++;
    return n;
}

int main(void) {
    static u8 page[SEQLZ_BLOCK_SIZE];
    static unsigned int s2[SEQLZ_MAX_SEQUENCES][4], s3[SEQLZ_MAX_SEQUENCES][4];
    unsigned int k, r = 12345, n2, n3, len_a, len_b, back = 0, i2, i3;

    for (k = 0; k < SEQLZ_BLOCK_SIZE; k++) {
        r = r * 1103515245U + 12345U;
        page[k] = (u8)(r >> 16);
    }
    /* each copy, then the bytes right before and after it made different from its source's */
    memcpy(page + 60, page + 10, 20);
    if (page[80] == page[30])
        page[80] ^= 1;
    if (page[59] == page[9])
        page[59] ^= 1;
    memcpy(page + A, page + B - 1, 5);
    if (page[A + 5] == page[B + 4])
        page[A + 5] ^= 1;
    memcpy(page + P - 1, page + B - 2, L + 2);
    if (page[P + 1 + L] == page[B + L])
        page[P + 1 + L] ^= 1;
    if (page[P - 2] == page[B - 3])
        page[P - 2] ^= 1;

    len_a = same(page, A, P);
    len_b = same(page, B, P + 1);
    while (page[P - back] == page[B - 1 - back])
        back++;
    if (len_a != 5 || len_b != L || back != 2)
        fail("the copies are not as the animation says");
    /* the repeat offset of the match before, 50, misses at P - 1 and at P + 1 */
    if (!memcmp(page + P - 1 - 50, page + P - 1, 4) || !memcmp(page + P + 1 - 50, page + P + 1, 4))
        fail("the repeat offset matches where the animation says it misses");

    n2 = seqs(page, 2, s2);
    n3 = seqs(page, 3, s3);
    /* level 2 takes the match at P from A, then the rest from B; level 3 one match from B - 1 */
    for (i2 = 0; i2 < n2 && s2[i2][0] != P; i2++)
        ;
    for (i3 = 0; i3 < n3 && s3[i3][0] != P + 1 - back; i3++)
        ;
    if (i2 == 0 || i2 + 1 >= n2 || i3 == 0 || i3 >= n3 || s2[i2][2] != len_a || s2[i2][3] != P - A ||
        s2[i2 + 1][0] != P + len_a || s2[i2 + 1][3] != P + 1 - B || s3[i3][2] != len_b + back || s3[i3][3] != P + 1 - B ||
        s2[i2 - 1][0] != 60 || s2[i2 - 1][2] != 20 || s3[i3 - 1][0] != 60)
        fail("seqlz_find() does not find what the animation says");
    if (s2[i2 - 1][3] != 50 || s3[i3 - 1][3] != 50 || s2[i2 - 1][0] + s2[i2 - 1][2] != 80)
        fail("the match before is not the same at both levels");

    printf("{");
    bytes("src", page, B - 4, L + 6);
    bytes("a", page, A - 2, 10);
    bytes("p", page, P - 3, L + 6);
    printf("\"A\":%u,\"B\":%u,\"P\":%u,\"lenA\":%u,\"lenB\":%u,\"back\":%u,\"lazy\":%u,\"rep\":%u,",
           A,
           B,
           P,
           len_a,
           len_b,
           back,
           SEQLZ_LAZY_LEN,
           s2[i2 - 1][3]);
    printf("\"before\":[%u,%u,%u,%u],", s2[i2 - 1][0], s2[i2 - 1][1], s2[i2 - 1][2], s2[i2 - 1][3]);
    printf("\"l2\":[[%u,%u,%u,%u],[%u,%u,%u,%u]],",
           s2[i2][0],
           s2[i2][1],
           s2[i2][2],
           s2[i2][3],
           s2[i2 + 1][0],
           s2[i2 + 1][1],
           s2[i2 + 1][2],
           s2[i2 + 1][3]);
    printf("\"l3\":[%u,%u,%u,%u]}\n", s3[i3][0], s3[i3][1], s3[i3][2], s3[i3][3]);
    return 0;
}
