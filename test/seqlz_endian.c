// SPDX-License-Identifier: MIT OR GPL-2.0-only
/*
 * seqlz_endian: generated pages through seqlz_compress_block() and seqlz_decompress(), at every level, and a hash
 * of all the compressed bytes. The format is little endian, so the hash must be the same on every CPU:
 * CI runs this on x86-64 and, under qemu, on big-endian s390x, and compares the two lines. Plain C
 * without the test framework, so that it builds in seconds under qemu.
 * It also prints a hash per level. A build with -DSEQLZ_LEVELS=1 or 2, for the kernel series' first
 * patches, must print the same hashes for the levels it has.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "seqlz.h"

static unsigned long long rng_state = 0x9e3779b97f4a7c15ULL;

static unsigned long long next(void) {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 7;
    rng_state ^= rng_state << 17;
    return rng_state;
}

/* page k: random bytes, a short pattern repeated (offsets 1 to 7 take the decoder's pattern copies),
 * 8-byte numbers counting up, few byte values with long runs, words from a small vocabulary (coded
 * literals and matches), or a page that is half zeros */
static void make_page(unsigned char* p, unsigned int k) {
    static const char letters[] = "eeeeettttaaaooiinnsshrdlu       \n";
    unsigned int i, j;

    switch (k % 6) {
    case 0:
        for (i = 0; i < SEQLZ_BLOCK_SIZE; i++)
            p[i] = (unsigned char)next();
        break;
    case 1: {
        unsigned int len = 1 + k / 6 % 7;
        unsigned char pat[7];

        for (j = 0; j < len; j++)
            pat[j] = (unsigned char)next();
        for (i = 0; i < SEQLZ_BLOCK_SIZE; i++)
            p[i] = i % 61 == 0 ? (unsigned char)next() : pat[i % len];
        break;
    }
    case 2: {
        unsigned long long v = 0x00007f1234560000ULL + (next() & 0xfff0U);

        for (i = 0; i < SEQLZ_BLOCK_SIZE; i += 8, v += 16 + (next() & 8U))
            for (j = 0; j < 8; j++)
                p[i + j] = (unsigned char)(v >> (8 * j));
        break;
    }
    case 3:
        for (i = 0; i < SEQLZ_BLOCK_SIZE;) {
            unsigned char b = (unsigned char)(next() % 5);
            unsigned int run = 1 + (unsigned int)(next() % 40);

            for (j = 0; j < run && i < SEQLZ_BLOCK_SIZE; j++)
                p[i++] = b;
        }
        break;
    case 4: {
        char words[32][8];

        for (j = 0; j < 32; j++) {
            unsigned int len = 2 + (unsigned int)(next() % 6), c;

            for (c = 0; c < len; c++)
                words[j][c] = letters[next() % (sizeof(letters) - 1)];
            words[j][len] = 0;
        }
        for (i = 0; i < SEQLZ_BLOCK_SIZE;) {
            const char* w = next() % 3 ? words[next() % 32] : &letters[next() % (sizeof(letters) - 1)];

            for (j = 0; w[j] && i < SEQLZ_BLOCK_SIZE; j++)
                p[i++] = (unsigned char)w[j];
        }
        break;
    }
    default:
        memset(p, 0, SEQLZ_BLOCK_SIZE);
        for (i = 0; i < SEQLZ_BLOCK_SIZE / 2; i++)
            p[(next() % SEQLZ_BLOCK_SIZE)] = (unsigned char)(next() % 7);
    }
}

/*
 * A build with fewer than 3 levels takes the blocks of the levels it lacks for invalid: with
 * SEQLZ_LEVELS=1 coded literals, the top bit of the u16; with 2 a block's own table, literal code 8,
 * and deltas, bit 7 of byte 2. Each such change of a block it wrote must fail to decode.
 */
static unsigned int count_accepted(unsigned char* dst, unsigned int n, unsigned char* out, void* scratch) {
    unsigned int accepted = 0;

#if SEQLZ_LEVELS < 2
    dst[1] ^= 0x80U;
    accepted += seqlz_decompress(dst, n, out, scratch) == 0;
    dst[1] ^= 0x80U;
#elif SEQLZ_LEVELS < 3
    if (n > 3 && (dst[1] & 0x80U)) {
        unsigned char b2 = dst[2];

        dst[2] = (unsigned char)((b2 & ~SEQLZ_LIT_CODE_MASK) | SEQLZ_LIT_SETS);
        accepted += seqlz_decompress(dst, n, out, scratch) == 0;
        /*
         * deltas as level 3 writes them, the delta's byte after byte 2: the rest of the block is as it
         * was, and it would decode, to literals that are not the page's
         */
        memmove(dst + 4, dst + 3, n - 3);
        dst[3] = 1; /* SEQLZ_DELTA_4 */
        dst[2] = (unsigned char)(b2 | 1U << SEQLZ_LIT_DELTA_AT);
        accepted += seqlz_decompress(dst, n + 1, out, scratch) == 0;
        memmove(dst + 3, dst + 4, n - 3);
        dst[2] = b2;
    }
#endif
    return accepted;
}

int main(void) {
    static unsigned char page[SEQLZ_BLOCK_SIZE], dst[2 * SEQLZ_BLOCK_SIZE], out[SEQLZ_BLOCK_SIZE];
#if SEQLZ_LEVELS >= 2
    static unsigned char scratch[SEQLZ_SCRATCH];
#else
    /* level 1 decompresses without work memory */
    unsigned char* const scratch = NULL;
#endif
    static struct seqlz_state st;
    unsigned long long hash = 0xcbf29ce484222325ULL;
    /* the same hash per level, which builds with fewer levels must have too */
    unsigned long long level_hash[SEQLZ_LEVEL_MAX + 1];
    unsigned int k, i, coded_pages = 0, own_pages = 0, failed = 0, accepted = 0;
    unsigned int level;

    for (level = 1; level <= SEQLZ_LEVEL_MAX; level++)
        level_hash[level] = 0xcbf29ce484222325ULL;
    for (k = 0; k < 600; k++) {
        make_page(page, k);
        for (level = 1; level <= SEQLZ_LEVEL_MAX; level++) {
            unsigned int n = seqlz_compress_block(&st, page, dst, sizeof dst, level);

            if (!n || seqlz_decompress(dst, n, out, scratch) || memcmp(out, page, SEQLZ_BLOCK_SIZE))
                failed++;
            coded_pages += n > 1 && (dst[1] & 0x80U);
#if SEQLZ_LEVELS >= 3
            own_pages += n > 2 && (dst[1] & 0x80U) && (dst[2] & SEQLZ_LIT_CODE_MASK) == SEQLZ_LIT_OWN;
#endif
            accepted += count_accepted(dst, n, out, scratch);
            for (i = 0; i < n; i++) {
                hash = (hash ^ dst[i]) * 0x100000001b3ULL;
                level_hash[level] = (level_hash[level] ^ dst[i]) * 0x100000001b3ULL;
            }
        }
    }
    printf("pages 600, coded %u, own table %u, failed %u, hash %016llx\n", coded_pages, own_pages, failed, hash);
    for (level = 1; level <= SEQLZ_LEVEL_MAX; level++)
        printf("level %u hash %016llx\n", level, level_hash[level]);
    if (accepted)
        printf("blocks of a level not built accepted: %u\n", accepted);
    return failed != 0 || accepted != 0 || (SEQLZ_LEVEL_MAX >= 2 && coded_pages == 0);
}
