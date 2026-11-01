/* libFuzzer target: make build/fuzz && ./build/fuzz -max_total_time=60 */
#include <stddef.h>
#include <stdint.h>
#include "../cr_decode.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    unsigned char out[4096];
    (void)cr_decode(data, size, out);
    return 0;
}
