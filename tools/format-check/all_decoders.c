// SPDX-License-Identifier: MIT OR GPL-2.0-only
/*
 * seqlz-all-decoders <pages>...: step 4 of docs/format-check.md, every decoder of fuzz/decoders.h on the
 * same blocks. Each 4 KiB page of each file is compressed at levels 1 to 3, and every decoder must decode
 * the block to the page. Then the damaged blocks, on which the decoders must agree: of one block in 10
 * every cut, every length from 0 to len - 1, of one in 100 every single bit flipped, and of each one
 * random damage of 1 to 8 bytes. A difference prints the file, the page, the level and the damage, never
 * the page's bytes: pages from memory hold keys and passwords. Exits 1 on a difference.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "decoders.h"

#define CUT_EVERY 10U
#define FLIP_EVERY 100U

static uint64_t rng = 0x9e3779b97f4a7c15ULL;
static unsigned long long blocks, decoded, differences;

static uint64_t next(void) {
    rng ^= rng << 13;
    rng ^= rng >> 7;
    rng ^= rng << 17;
    return rng;
}

/* decoders_agree() on len bytes of block, counted, and where it fails, which page and damage */
static void check(const unsigned char* block,
                  size_t len,
                  const unsigned char* page,
                  const char* file,
                  unsigned long page_no,
                  unsigned int level,
                  const char* damage,
                  size_t at) {
    decoded++;
    if (!decoders_agree(block, len, page)) {
        differences++;
        fprintf(stderr, "  %s page %lu level %u: %s %zu\n", file, page_no, level, damage, at);
    }
}

int main(int argc, char** argv) {
    static struct seqlz_state st;
    static unsigned char page[SEQLZ_BLOCK_SIZE], dst[SEQLZ_COMPRESS_BOUND], damaged[SEQLZ_COMPRESS_BOUND];
    int a;

    if (argc < 2) {
        fprintf(stderr, "usage: seqlz-all-decoders <pages>...\n");
        return 2;
    }
    decoders_init();
    for (a = 1; a < argc; a++) {
        FILE* f = fopen(argv[a], "rb");
        unsigned long page_no;

        if (!f) {
            perror(argv[a]);
            return 2;
        }
        for (page_no = 0; fread(page, 1, sizeof page, f) == sizeof page; page_no++) {
            unsigned int level;

            for (level = 1; level <= SEQLZ_LEVEL_MAX; level++) {
                size_t len = seqlz_compress_block(&st, page, dst, sizeof dst, level), at, k;
                unsigned int bit;

                if (!len) {
                    fprintf(stderr, "%s page %lu level %u: seqlz_compress_block() failed\n", argv[a], page_no, level);
                    return 2;
                }
                blocks++;
                check(dst, len, page, argv[a], page_no, level, "as written, length", len);
                if (blocks % CUT_EVERY == 0)
                    for (at = 0; at < len; at++)
                        check(dst, at, NULL, argv[a], page_no, level, "cut to", at);
                if (blocks % FLIP_EVERY == 0)
                    for (at = 0; at < len; at++)
                        for (bit = 0; bit < 8; bit++) {
                            memcpy(damaged, dst, len);
                            damaged[at] ^= (unsigned char)(1U << bit);
                            check(damaged, len, NULL, argv[a], page_no, level, "bit flipped in byte", at);
                        }
                memcpy(damaged, dst, len);
                for (k = 1 + next() % 8; k > 0; k--)
                    damaged[next() % len] = (unsigned char)next();
                check(damaged, len, NULL, argv[a], page_no, level, "random damage, seed state", (size_t)rng);
            }
        }
        fclose(f);
        printf("%s: %lu pages\n", argv[a], page_no);
    }
    printf("%llu blocks, %llu decoded by every decoder, %llu differences\n", blocks, decoded, differences);
    return differences ? 1 : 0;
}
