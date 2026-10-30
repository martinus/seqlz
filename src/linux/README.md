# src/linux/

`seqlz.h` is the codec's interface, the kernel's `include/linux/seqlz.h`.

The other files stand in for the kernel's headers of the same name outside the kernel: in the tests,
the benchmarks, the fuzzers and the tools, which find them with `-Isrc` as `<linux/types.h>` and so
on, as `tools/include/` does for the kernel's own tools. Each one defines only what the codec uses of
its namesake. Where the C library has a header of that name too, the kernel's UAPI header that libc
includes, e.g. `<linux/errno.h>` from `<errno.h>`, it includes that one first with `#include_next`.
A kernel build never sees them: it copies `src/*.c`, `src/*.h` and `src/linux/seqlz.h` alone.
