/* cr-gen <seed> <count> <dir>: writes <dir>/<k>.z and <dir>/<k>.block for k < count, each from
 * random choice bytes, and prints how often each item of FEATURES.md occurred: the number of
 * times in all blocks, and the number of blocks with it. C11. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cr.h"

#define NCHOICES 32768 /* more than any block reads; cr_write() reads 0 after them anyway */

static uint64_t rng;

static uint64_t next(void) /* splitmix64 */
{
    uint64_t z = (rng += 0x9e3779b97f4a7c15ull);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
    return z ^ (z >> 31);
}

static int writefile(const char *path, const unsigned char *b, size_t n)
{
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); return 1; }
    int bad = fwrite(b, 1, n, f) != n;
    bad |= fclose(f) != 0;
    if (bad) perror(path);
    return bad;
}

int main(int argc, char **argv)
{
    if (argc != 4) { fprintf(stderr, "usage: cr-gen <seed> <count> <dir>\n"); return 2; }
    rng = strtoull(argv[1], NULL, 0);
    long count = atol(argv[2]);
    const char *dir = argv[3];
    static unsigned char ch[NCHOICES], z[CR_MAX], block[CR_BLOCK];
    static unsigned long total[CRF_COUNT], blocks[CRF_COUNT], one[CRF_COUNT];
    char path[4096];
    size_t zmax = 0, zsum = 0;
    for (long k = 0; k < count; k++) {
        for (size_t i = 0; i < NCHOICES; i += 8) {
            uint64_t v = next();
            for (int j = 0; j < 8; j++) ch[i + (size_t)j] = (unsigned char)(v >> (8 * j));
        }
        memcpy(one, total, sizeof(unsigned long) * CR_FIRST_LARGE);
        size_t n = cr_write_stats(ch, NCHOICES, z, sizeof z, block, total);
        if (n == 0) { fprintf(stderr, "block %ld: cap too small\n", k); return 1; }
        if (n > zmax) zmax = n;
        zsum += n;
        for (int i = 0; i < CR_FIRST_LARGE; i++) blocks[i] += total[i] != one[i];
        snprintf(path, sizeof path, "%s/%ld.z", dir, k);
        if (writefile(path, z, n)) return 1;
        snprintf(path, sizeof path, "%s/%ld.block", dir, k);
        if (writefile(path, block, CR_BLOCK)) return 1;
    }
    printf("%ld blocks, seed %s, compressed size mean %.1f max %zu\n", count, argv[1],
           count ? (double)zsum / (double)count : 0.0, zmax);
    printf("%-52s %12s %9s\n", "item", "times", "blocks");
    char name[256];
    for (int f = 0; f < cr_nfamilies; f++) {
        if (cr_families[f].base >= CR_FIRST_LARGE) {
            int seen = 0;
            unsigned long mn = 0;
            for (int i = 0; i < cr_families[f].count; i++) {
                unsigned long t = total[cr_families[f].base + i];
                if (t) { if (!seen || t < mn) mn = t; seen++; }
            }
            printf("-- %s (%s): %d of %d items occurred, the rarest of them %lu times\n",
                   cr_families[f].name, cr_families[f].section, seen, cr_families[f].count, mn);
            continue;
        }
        printf("-- %s (%s)\n", cr_families[f].name, cr_families[f].section);
        for (int i = 0; i < cr_families[f].count; i++) {
            int id = cr_families[f].base + i;
            printf("%-52s %12lu %9lu\n", cr_item_name(f, i, name, sizeof name), total[id], blocks[id]);
        }
    }
    return 0;
}
