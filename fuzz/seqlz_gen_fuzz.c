// SPDX-License-Identifier: MIT OR GPL-2.0-only
/*
 * Valid blocks by construction, step 5 of docs/format-check.md: the clean-room agent W's writer,
 * cr_write() of tools/format-check/cleanroom/W/, takes the input as the bytes that pick each choice the
 * format leaves open, also the ones seqlz_compress() never makes, and writes a valid block and the page it
 * decodes to. Every decoder of decoders.h must decode the block to that page. Then the last three bytes
 * of the input damage the block at one place, a bit flipped or a cut, and the decoders must agree on it.
 * A difference is a bug in W, in a decoder, or a place where docs/format.md is not clear: W read it too.
 */
#include "cr.h"
#include "decoders.h"

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    static unsigned char dst[CR_MAX_BLOCK], page[SEQLZ_BLOCK_SIZE];
    size_t len, at;

    decoders_init();
    len = cr_write(data, size, dst, sizeof dst, page);
    if (!len)
        abort();
    if (!decoders_agree(dst, len, page))
        abort();
    if (size < 3)
        return 0;
    /* the damage: data[size - 3] and [size - 2] where, data[size - 1] which bit, or a cut there */
    at = ((size_t)data[size - 3] << 8 | data[size - 2]) % len;
    if (data[size - 1] < 128)
        dst[at] ^= (unsigned char)(1U << (data[size - 1] & 7U));
    else
        len = at;
    if (!decoders_agree(dst, len, NULL))
        abort();
    return 0;
}
