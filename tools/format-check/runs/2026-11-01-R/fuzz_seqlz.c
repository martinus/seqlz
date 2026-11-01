#include <stddef.h>
int cr_decode(const unsigned char *src, size_t len, unsigned char *out);
int LLVMFuzzerTestOneInput(const unsigned char *data, size_t size)
{
    unsigned char out[4096];
    cr_decode(data, size, out);
    return 0;
}
