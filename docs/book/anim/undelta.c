// SPDX-License-Identifier: MIT OR GPL-2.0-only
/*
 * undelta.c: the data of the animation "undelta" in chapter 13. The page of counters of pages.h, which
 * level 3 stores with SEQLZ_DELTA_4, decoded by the real seqlz_decompress(), with a hook on its stores of
 * 4 bytes: in seqlz_decompress.c only add4() makes them. Prints, as JSON, the first two sequences and
 * the first W bytes of the page step by step: the deltas as the copy left them, after each add4(), the
 * byte loop and the matches. Fails if the decoder took another path than the text says.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <linux/unaligned.h> /* before the hook below renames its store */

#include "pages.h"
#include "seqlz.h"

/* every add4() of the decoder: where it stored, the page before the store and the 4 bytes stored */
#define W 56U
#define MAX_HOOK 16U
static u8* hook_dst;
static unsigned int n_hook, hook_at[MAX_HOOK];
static u8 hook_before[MAX_HOOK][W], hook_val[MAX_HOOK][4];

static void hook_le32(u32 v, void* p) {
    unsigned long at = (unsigned long)p - (unsigned long)hook_dst;

    if (at < W && n_hook < MAX_HOOK) {
        hook_at[n_hook] = (unsigned int)at;
        memcpy(hook_before[n_hook], hook_dst, W);
        put_unaligned_le32(v, hook_val[n_hook]);
        n_hook++;
    }
    put_unaligned_le32(v, p);
}

#define put_unaligned_le32(v, p) hook_le32(v, p)
#include "seqlz_decompress.c"

struct seq {
    unsigned int pos, ll, ml, off;
};
static struct seq seqs[SEQLZ_MAX_SEQUENCES];
static unsigned int n_seqs;

static int fail(const char* what) {
    fprintf(stderr, "undelta.c: %s\n", what);
    exit(1);
}

/* one step: what the page holds, the kind of each byte, and the bytes the step works on */
enum { EMPTY, DELTA, SPARE, LIT, MATCH };
static u8 cur_b[W], cur_c[W];
static unsigned int n_frames;

static void frame(const char* kind, unsigned int seq, unsigned int at, unsigned int n, int src) {
    unsigned int i;

    printf(
        "%s{\"kind\":\"%s\",\"seq\":%u,\"at\":%u,\"n\":%u,\"src\":%d,\"b\":[", n_frames++ ? "," : "", kind, seq, at, n, src);
    for (i = 0; i < W; i++)
        printf("%s%d", i ? "," : "", cur_c[i] == EMPTY ? -1 : cur_b[i]);
    printf("],\"c\":[");
    for (i = 0; i < W; i++)
        printf("%s%u", i ? "," : "", cur_c[i]);
    printf("]}");
}

int main(void) {
    static u8 page[SEQLZ_BLOCK_SIZE], comp[2 * SEQLZ_BLOCK_SIZE], out[SEQLZ_BLOCK_SIZE], scratch[SEQLZ_SCRATCH];
    static struct seqlz_state st;
    unsigned int n, i, h = 0, s, k, dist = 4, lit = 0, end;

    counter_page(page);
    n = seqlz_compress_block(&st, page, comp, sizeof(comp), SEQLZ_LEVEL_OWN);
    if (!n || !(comp[2] >> SEQLZ_LIT_DELTA_AT) || comp[SEQLZ_LIT_HEADER(0)] != SEQLZ_DELTA_4)
        fail("level 3 does not store the page's literals with SEQLZ_DELTA_4");
    /* the sequences, as level 3's matcher finds them */
    {
        static struct seqlz_sequence found[SEQLZ_MAX_SEQUENCES];
        unsigned int pos = 0;

        n_seqs = seqlz_find(&st, page, found, SEQLZ_LEVEL_OWN);
        for (i = 0; i < n_seqs; i++) {
            seqs[i] = (struct seq){pos, found[i].literals, found[i].match, found[i].offset};
            pos += found[i].literals + found[i].match;
        }
    }
    if (n_seqs < 3 || seqs[0].pos || seqs[1].pos != seqs[0].ll + seqs[0].ml || seqs[1].pos + 16 + 4 > W)
        fail("the first two sequences do not fit the figure");
    /* the first takes the slow path, with a length value; the second the fast path, without */
    if (seqs[0].ll < SEQLZ_LL_CAP || seqs[1].ll >= SEQLZ_LL_CAP || seqs[1].ml >= SEQLZ_ML_CAP + SEQLZ_MIN_MATCH)
        fail("the sequences' paths differ from the figure's");
    if (seqs[0].off != dist || seqs[1].off != dist || seqs[0].ll % 4 == 0)
        fail("the first two sequences are not as the figure says");
    memset(scratch, 0xaa, sizeof(scratch));
    hook_dst = out;
    if (seqlz_decompress(comp, n, out, scratch) || memcmp(out, page, SEQLZ_BLOCK_SIZE))
        fail("the page does not decode to itself");
    /*
     * the decoder's add4() calls in the first W bytes: the first sequence's words from byte 4 while
     * one fits before its end, then undelta_fast()'s four at the second sequence, whatever its nl
     */
    for (k = 4; k + 4 <= seqs[0].ll; k += 4)
        if (h >= n_hook || hook_at[h++] != k)
            fail("the first sequence's words are not add4() from byte 4 on");
    for (k = 0; k < 16; k += 4)
        if (h >= n_hook || hook_at[h++] != seqs[1].pos + k)
            fail("the second sequence is not undelta_fast()'s four add4()");
    if (h != n_hook)
        fail("more add4() than the figure shows");

    printf("{\"dist\":%u,\"delta\":%u,\"w\":%u,\"seqs\":[", dist, SEQLZ_DELTA_4, W);
    for (s = 0; s < 2; s++)
        printf(
            "%s{\"pos\":%u,\"ll\":%u,\"ml\":%u,\"off\":%u}", s ? "," : "", seqs[s].pos, seqs[s].ll, seqs[s].ml, seqs[s].off);
    printf("],\"frames\":[");
    h = 0;
    for (s = 0; s < 2; s++) {
        const unsigned int pos = seqs[s].pos, ll = seqs[s].ll;

        /* the copy: what the page held at the first add4() of the sequence, the deltas and past them */
        end = s == 0 ? (ll + 15) / 16 * 16 : pos + 16;
        for (i = pos; i < end; i++) {
            cur_b[i] = hook_before[h][i];
            cur_c[i] = i < pos + ll ? DELTA : SPARE;
            if (cur_b[i] != scratch[lit + i - pos])
                fail("the copy is not the decoder's literals");
        }
        frame("copy", s, pos, end - pos, -1);
        k = 0;
        if (pos < dist) {
            k = dist - pos;
            for (i = pos; i < dist; i++) {
                cur_c[i] = LIT;
                if (cur_b[i] != page[i])
                    fail("a byte before dist is not stored as it is");
            }
            frame("keep", s, pos, k, -1);
        }
        for (; h < n_hook && hook_at[h] < pos + ll + seqs[s].ml; h++) {
            unsigned int at = hook_at[h];

            for (i = 0; i < 4; i++) {
                cur_b[at + i] = hook_val[h][i];
                if (cur_c[at + i] == DELTA)
                    cur_c[at + i] = LIT;
            }
            k = at + 4 - pos;
            frame("word", s, at, 4, (int)(at - dist));
        }
        /* the byte loop, which only the slow path has */
        for (; k < ll; k++) {
            cur_b[pos + k] = out[pos + k];
            cur_c[pos + k] = LIT;
            frame("byte", s, pos + k, 1, (int)(pos + k - dist));
        }
        for (i = pos; i < pos + ll; i++)
            if (cur_b[i] != page[i])
                fail("a literal is not the page's after its sequence");
        /* the match after the literals, over the bytes copied past them */
        for (i = pos + ll; i < pos + ll + seqs[s].ml && i < W; i++) {
            cur_b[i] = out[i];
            cur_c[i] = MATCH;
        }
        frame("match", s, pos + ll, seqs[s].ml, (int)(pos + ll - seqs[s].off));
        lit += ll;
    }
    for (i = 0; i < W; i++)
        if (cur_c[i] == EMPTY || cur_c[i] == SPARE || cur_b[i] != page[i])
            fail("the figure's last step is not the page");
    printf("]}\n");
    return 0;
}
