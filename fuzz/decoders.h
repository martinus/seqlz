// SPDX-License-Identifier: MIT OR GPL-2.0-only
/*
 * Every decoder of a block, for seqlz_diff_fuzz.c and seqlz_gen_fuzz.c: seqlz_decompress(), and the three
 * written from docs/format.md alone, tools/seqlz-ref/seqlz_ref.c and the clean-room agents' D and R of
 * tools/format-check/cleanroom/, whose cr_decode() CMake renames. decoders_agree() fails on a block that
 * the decoders call valid and invalid, or decode to different blocks, and prints which said what.
 */
#ifndef SEQLZ_FUZZ_DECODERS_H
#define SEQLZ_FUZZ_DECODERS_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "seqlz.h"
#include "seqlz_ref.h"
#include "unwritten.h"

int cr_decode_d(const unsigned char* src, size_t len, unsigned char* out);
int cr_decode_r(const unsigned char* src, size_t len, unsigned char* out);

#define DECODERS 4
static const char* const decoder_names[DECODERS] = {"seqlz_decompress", "seqlz_ref.c", "cleanroom D", "cleanroom R"};
static struct seqlz_ref decoders_ref;
static unsigned char *decoders_out[DECODERS], *decoders_scratch;

static void decoders_init(void) {
    static int done;
    const struct seqlz_ref_tables t = {
        seqlz_default_lengths.token, seqlz_default_lengths.ll, seqlz_default_lengths.ml, seqlz_lit_sets, seqlz_own_lengths};
    int k;

    if (done)
        return;
    done = 1;
    for (k = 0; k < DECODERS; k++)
        if (!(decoders_out[k] = malloc(SEQLZ_BLOCK_SIZE)))
            abort();
    decoders_scratch = malloc(SEQLZ_SCRATCH);
    if (!decoders_scratch || seqlz_ref_init(&decoders_ref, &t))
        abort();
}

/*
 * Every decoder on the len bytes at data, in an allocation of exactly that size, as in
 * seqlz_decode_fuzz.c. Returns 1 if they agree, and with want also decode the block to want; else
 * prints what each said and returns 0.
 */
static int decoders_agree(const unsigned char* data, size_t len, const unsigned char* want) {
    unsigned char* src = malloc(len ? len : 1);
    int valid[DECODERS], agree = 1, k;

    if (!src)
        abort();
    memcpy(src, data, len);
    for (k = 0; k < DECODERS; k++) {
        memset(decoders_out[k], 0x11 * (k + 1), SEQLZ_BLOCK_SIZE);
        unwritten(decoders_out[k], SEQLZ_BLOCK_SIZE);
    }
    unwritten(decoders_scratch, SEQLZ_SCRATCH);
    valid[0] = seqlz_decompress(src, (unsigned int)len, decoders_out[0], decoders_scratch) == 0;
    valid[1] = seqlz_ref_decode(&decoders_ref, src, len, decoders_out[1]) == 0;
    valid[2] = cr_decode_d(src, len, decoders_out[2]) == 0;
    valid[3] = cr_decode_r(src, len, decoders_out[3]) == 0;
    free(src);
    for (k = 0; k < DECODERS; k++) {
        const unsigned char* same = want ? want : decoders_out[0];

        if (valid[k] != (want ? 1 : valid[0]) || (valid[k] && memcmp(decoders_out[k], same, SEQLZ_BLOCK_SIZE) != 0))
            agree = 0;
    }
    if (!agree) {
        /* "another block" against the page the writer made, else against seqlz_decompress()'s */
        const unsigned char* page = want ? want : valid[0] ? decoders_out[0] : NULL;

        fprintf(stderr, "decoders differ on %zu bytes%s:", len, want ? " that the writer made valid" : "");
        for (k = 0; k < DECODERS; k++) {
            const char* said = "invalid";

            if (valid[k])
                said = page && memcmp(decoders_out[k], page, SEQLZ_BLOCK_SIZE) ? "valid, another block" : "valid";
            fprintf(stderr, " %s %s", decoder_names[k], said);
        }
        fprintf(stderr, "\n");
    }
    return agree;
}

#endif
