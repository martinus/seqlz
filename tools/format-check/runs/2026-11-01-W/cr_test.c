/* cr-test: cr_write() on choice bytes of many kinds; every block must be valid (cr_check.c's
 * decoder) and decode to the block cr_write() gives. C11. */
#define CR_CHECK_NO_MAIN
#include "cr_check.c"
#include "cr.h"

static unsigned long long rs = 88172645463325252ull;
static unsigned rnd(void) { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return (unsigned)(rs >> 32); }

static long fails, runs;
static unsigned char z[CR_MAX], blk[CR_BLOCK], dec[CR_BLOCK];

static void one(const unsigned char *ch, size_t n, const char *what)
{
    size_t len = cr_write(ch, n, z, sizeof z, blk);
    runs++;
    const char *err = len ? cr_decode(z, len, dec) : "cap too small";
    if (!err && memcmp(dec, blk, CR_BLOCK)) err = "decodes to other bytes";
    if (!err && len > 2) {  /* the cap: exact works, one less gives 0 */
        unsigned char *t = malloc(len);
        if (cr_write(ch, n, t, len, blk) != len || memcmp(t, z, len)) err = "exact cap";
        else if (cr_write(ch, n, t, len - 1, blk) != 0) err = "cap - 1 not 0";
        free(t);
    }
    if (err) { if (fails++ < 10) fprintf(stderr, "%s (n=%zu): %s\n", what, n, err); }
}

int main(void)
{
    static unsigned char ch[40000];
    if (!cr_check_init()) return 1;
    {   /* the example of format.md, from choice bytes worked out by hand with FEATURES.md */
        static const unsigned char ex_ch[] = {0x00, 0x0d, 0x01, 0x02, 0x02, 0x00, 0x01, 0x61, 0x62, 0x01, 0x01};
        static const unsigned char ex_z[] = {0x02, 0x00, 0x61, 0x62, 0x23, 0xf2, 0xff, 0xfb, 0x60};
        size_t len = cr_write(ex_ch, sizeof ex_ch, z, sizeof z, blk);
        if (len != sizeof ex_z || memcmp(z, ex_z, len)) { fprintf(stderr, "the example is not written\n"); fails++; }
        else printf("the example of format.md: written from 11 choice bytes\n");
    }
    one(NULL, 0, "no choice bytes");
    for (int v = 0; v < 256; v++) {
        memset(ch, v, sizeof ch);
        one(ch, sizeof ch, "constant");
        for (size_t n = 1; n < 64; n++) one(ch, n, "constant prefix");
    }
    for (int k = 0; k < 200000; k++) {
        size_t n = rnd() % 4 == 0 ? rnd() % 64 : rnd() % 4 == 0 ? rnd() % 2000 : sizeof ch;
        int kind = (int)(rnd() % 6);
        unsigned a = rnd() & 255, b = rnd() & 255;
        for (size_t i = 0; i < n; i++) {
            unsigned r = rnd();
            switch (kind) {
            case 0: ch[i] = (unsigned char)r; break;
            case 1: ch[i] = (unsigned char)(r % 4); break;                    /* small */
            case 2: ch[i] = (unsigned char)(255 - r % 4); break;              /* large */
            case 3: ch[i] = (unsigned char)((r & 1) ? a : b); break;          /* two values */
            case 4: ch[i] = (unsigned char)((r % 16) ? 0 : r >> 8); break;    /* mostly 0 */
            default: ch[i] = (unsigned char)((r % 16) ? 255 : r >> 8); break; /* mostly 255 */
            }
        }
        one(ch, n, "random");
    }
    printf("%ld runs, %ld failures\n", runs, fails);
    return fails != 0;
}
