/* cr-gen <seed> <count> <dir>: writes count blocks from random choice bytes, as <dir>/<k>.z and
 * <dir>/<k>.block, and prints how often each item of FEATURES.md occurred. */
#include "cr.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

static uint64_t splitmix(uint64_t *s) {
    uint64_t z = (*s += 0x9e3779b97f4a7c15ull);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
    return z ^ (z >> 31);
}

#define NCHOICES 32768   /* more than one block ever reads, see FEATURES.md */

int main(int argc, char **argv) {
    if (argc != 4) { fprintf(stderr, "usage: cr-gen <seed> <count> <dir>\n"); return 2; }
    uint64_t seed = strtoull(argv[1], NULL, 0);
    long count = atol(argv[2]);
    const char *dir = argv[3];
    int nf = cr_feature_count();
    unsigned long long *cnt = calloc((size_t)nf, sizeof *cnt);
    static unsigned char ch[NCHOICES], dst[CR_MAX_BLOCK], block[4096];
    if (!cnt) return 1;
    cr_stats st = {cnt};
    for (long k = 0; k < count; k++) {
        for (int i = 0; i < NCHOICES; i += 8) {
            uint64_t v = splitmix(&seed);
            memcpy(ch + i, &v, 8);
        }
        size_t len = cr_write_stats(ch, NCHOICES, dst, sizeof dst, block, &st);
        if (!len) { fprintf(stderr, "block %ld: cap too small\n", k); return 1; }
        char p[4096];
        snprintf(p, sizeof p, "%s/%ld.z", dir, k);
        FILE *f = fopen(p, "wb");
        if (!f || fwrite(dst, 1, len, f) != len || fclose(f)) { perror(p); return 1; }
        snprintf(p, sizeof p, "%s/%ld.block", dir, k);
        f = fopen(p, "wb");
        if (!f || fwrite(block, 1, 4096, f) != 4096 || fclose(f)) { perror(p); return 1; }
    }
    for (int i = 0; i < nf; i++) printf("%-28s %llu\n", cr_feature_name(i), cnt[i]);
    free(cnt);
    return 0;
}
