# src/seqlz_compat.h

Every file of `src/` is written with the kernel's names: `u8`, `u64`, `get_unaligned_le64()`,
`prefetch()`, `min()`. This header makes those names exist in two worlds. In a kernel build
(`__KERNEL__` defined) it includes the kernel's headers. Everywhere else, the tests, benchmarks,
fuzzers and tools, it defines the same names itself, from the headers the compiler brings along. The
kernel's own copy, written by `tools/kernel-port/port.py`, does not have this file at all: the
`#include` is replaced by the kernel's headers ([three ways to build](book:#ch-compat)).

A reading order:

1. [The promise at the top](#b1): what the header must not define.
2. [The kernel half](#b16), and [its one changed value](#b47), `L1_CACHE_BYTES`.
3. [The userspace half](#b56): [types and attributes](#b79), [`prefetch()`](#b105), the
   [small functions](#b121), and the [byte order accessors](#b142).

## The promise

@@ 1-15
`MIT OR GPL-2.0-only`: the kernel needs GPL-2.0, MIT lets other projects use the code too. `port.py`
writes the kernel's copy with the two the other way round, the kernel's order.

The last sentence of the comment shapes the file. The tests and benchmarks are C++ and include
`seqlz.h` inside `extern "C" { }`. A macro named `bool` or `NULL` would break them, so the header
takes those from the compiler's headers and never defines them itself.

## In a kernel build

@@ 16-46
Each kernel header brings one or two names, e.g. `linux/math64.h` brings `div_u64()` and `linux/swab.h`
brings `swab64()`. The `__has_include` and `#ifndef` lines are for old kernels: the Mi 9T test phone
runs Linux 4.14 from 2017, and `tools/zram-phone/` builds `src/` as it is into a module for it. Each
fallback is a name that arrived later, e.g. `linux/unaligned.h` only in 6.12.

C11's `_Static_assert` needs a message, the kernel's `static_assert()` does not. So every
`static_assert()` in `src/` has one, to work with the fallback.

@@ 47-55
The only place where the header changes a kernel value. Older arm64 kernels say a cache line is 128
bytes, the phone's cores have 64. The decoder prefetches its tables with one `prefetch()` every
`L1_CACHE_BYTES`. With 128 it asks for the lines at 0, 128, 256, and never for the ones at 64, 192,
320: every second line of the token table would arrive late.

The `#if` can compare the value because the kernel defines it as `(1 << L1_CACHE_SHIFT)`, a number
the preprocessor can compute. The `#undef` only affects the codec's files.

## Outside the kernel

@@ 56-78
`stdbool.h` and `stddef.h` are **freestanding** headers: they come with gcc and clang, not with the C
library. The benchmarks also build the codec with the kernel's flags, including `-nostdinc`, which
hides the C library's headers. Then `__has_include(<string.h>)` is 0 and the header only declares the
three functions the codec calls; the linker still finds them in the C library.

Why build with the kernel's flags at all: with userspace flags the compiler uses SSE registers to
vectorize loops, which the kernel forbids, and the benchmark would not measure what the kernel runs
([outside the kernel](book:#ch-compat--userspace)).

@@ 79-94
The kernel's `u64` is `unsigned long long` on every architecture; glibc's `uint64_t` is
`unsigned long` on 64-bit Linux. Same width, different type, which matters for `printf()` formats and
for picking `__builtin_ctzll()`. Defining it the kernel's way gives userspace exactly the types the
kernel code sees.

`noinline` and `__cold` are only defined for C. In C++ the macro would also replace the word inside
`[[gnu::noinline]]`, which then turns into nonsense that gcc warns about.

@@ 95-104
`static_assert` is a keyword in C23 and C++, so the macro is only for older C.

`sizeof_field()` gives the size of a struct member without an object. The null pointer is never
dereferenced: `sizeof` does not evaluate its operand, it only looks at the type.

`L1_CACHE_BYTES` 64 is right for every x86-64 CPU and for the measured arm64 cores. It decides only how
many prefetches are sent, never a result.

@@ 105-120
The only inline assembly in `src/`, and only in userspace. The kernel builds x86-64 with `-mno-sse`.
The instruction `prefetcht0` came with SSE, though it uses no SSE register. gcc still emits it for
`__builtin_prefetch()`; clang silently emits nothing. So a clang build would lose every prefetch of the
decoder.

For the kernel itself, `tools/kernel-port/x86-prefetcht0.patch` makes the kernel's `prefetch()` this
same instruction ([prefetch()](book:#ch-compat--prefetch)). This copy is for the benchmarks, so they
measure what a patched kernel runs. The `"m"` operand lets the compiler fold an offset into the
instruction, e.g. `prefetcht0 64(%rdi)`. `cmake/check_prefetch.cmake` fails the tests if the
instruction is missing from the decoder.
@anim misses A model of 8 cache misses, one after another or overlapped

@@ 121-126
A function and not a macro: a macro `min(a, b)` would also expand inside `std::min(a, b)` in the C++
files. `unsigned int` is all its callers need: `seqlz_token()` caps `ll` and `ml - 4` with it.

@@ 127-135
On 32-bit arm and x86 the compiler turns a `u64 / u32` into a call to a helper of its runtime
library, `__aeabi_uldivmod` or `__udivdi3`. The kernel has neither, so the module would not link. The
kernel's `div_u64()` avoids the call. The codec has one such division, in `own_lengths()`
([chapter 11](book:#ch-literals-enc--own)).

@@ 136-141
One caller, `lit_flush()` in the literal coder. In the kernel it is the same builtin.

## Byte order at any address

@@ 142-157
On a little-endian CPU (x86-64, arm64) a little-endian value needs nothing and a big-endian one needs a
byte swap, one instruction (`bswap`, `rev`). On a big-endian CPU it is the other way round.

The `#if` relies on gcc and clang defining both macros. A compiler with neither would see `0 == 0` and
take the big-endian branch.

@@ 158-185
`memcpy()` into a local is the defined way in C to read a number from any address; a cast to `u16 *`
of an odd address is undefined behaviour. With a constant size, gcc and clang make the `memcpy()` one
plain load, e.g. `movzwl 1(%rdi), %eax`. `__always_inline` matters: the `memcpy()` has to see the
caller's address to become one load.

Until 5th October 2026 some loads used the CPU's own byte order, and on s390x every page failed to
round trip. Since then all loads go through these, and CI checks that x86-64, 32-bit x86 and s390x
write the same bytes ([endian](book:#ch-compat--endian)).

@@ 186-216
Little endian puts the byte at `p` in the low bits. The matcher's match length wants that: XOR two
words, and the lowest set bit is in the first byte that differs. The decoder's pattern copy needs it
too. Big endian puts it in the high bits: the bitstreams are read most significant bit
first, so the next bit is the top bit of the loaded word.

The pair has one trick. The encoder writes its bitstream backwards and turns it around at the end with
`put_unaligned_be64(get_unaligned_le64(end - 8 - k), out + k)`: the little-endian load and big-endian
store reverse 8 bytes at once, a load, a `bswap` and a store on x86-64.
