/* libFuzzer target: any input must return 0 or -1, with no sanitizer finding. */
#include "../src/cr_decode.h"
#include <stdint.h>
#include <stdlib.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    unsigned char out[CR_BLOCK];
    int r = cr_decode(data, size, out);
    if (r != 0 && r != -1)
        abort();
    if (r == 0) {
        /* make MSan look at every byte of a valid block's output */
        unsigned x = 0;
        for (int i = 0; i < CR_BLOCK; i++)
            x += out[i];
        if (x == 0xffffffffu)
            abort();
    }
    return 0;
}
