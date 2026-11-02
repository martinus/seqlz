// SPDX-License-Identifier: MIT OR GPL-2.0-only
/*
 * seqlz_decompress() against the decoders written from docs/format.md alone, tools/seqlz-ref/seqlz_ref.c
 * and the clean-room agents' D and R (decoders.h), on any input: all must call the same inputs valid,
 * and decode a valid one to the same page. A difference is a bug in one of them or a place where
 * docs/format.md is not clear. Each buffer gets an allocation of exactly its size, as in
 * seqlz_decode_fuzz.c.
 */
#include "decoders.h"

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    /* zram never stores more than a page; twice that covers lengths the decoders have to reject */
    if (size > 2U * SEQLZ_BLOCK_SIZE)
        return 0;
    decoders_init();
    if (!decoders_agree(data, size, NULL))
        abort();
    return 0;
}
