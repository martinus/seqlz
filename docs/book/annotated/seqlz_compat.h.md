# src/seqlz_compat.h

Every file of `src/` is written with the kernel's names: `u8`, `u64`, `get_unaligned_le64()`,
`prefetch()`, `min()`. This header makes those names exist in two worlds. In a kernel build
(`__KERNEL__` defined) it includes the kernel's headers. Everywhere else, the tests, benchmarks,
fuzzers and tools, it defines the same names itself, from the headers the compiler brings along. The
kernel's own copy, written by `tools/kernel-port/port.py`, does not have this file at all: the
`#include` is replaced by the kernel's headers, and nothing else in the code changes.

A reading order:

1. [The promise at the top](#b1): what the header must not define.
2. [The kernel half](#b16), and [its one changed value](#b47), `L1_CACHE_BYTES`.
3. [The userspace half](#b56): [types and attributes](#b79), [`prefetch()`](#b105), the
   [small functions](#b121), and the [byte order accessors](#b142).
@more {#ch-compat--builds} Three ways `src/` gets compiled
| build | who does it | where the names come from |
| --- | --- | --- |
| the kernel's copy | `tools/kernel-port/port.py` writes `lib/seqlz/` into a kernel tree | the kernel's headers; `seqlz_compat.h` is not in the copy |
| `src/` as it is, inside a kernel | `tools/zram-vm/run.sh` (the kernel VM) and `tools/zram-phone/build.sh` (a kernel module for the Mi 9T's 4.14, [chapter 15](#ch-zram-glue)) | [the first half](#b16) of this header, `__KERNEL__` is defined |
| userspace | tests, benchmarks, fuzzers, tools, built with CMake | [the second half](#b56) of this header |

The middle row is why the first half has fallbacks for old kernels: the phone runs Linux 4.14, from
2017.
@end

## The promise

@@ 1-15
@anchor ch-compat
`MIT OR GPL-2.0-only`: the kernel needs GPL-2.0, MIT lets other projects use the code too. `port.py`
writes the kernel's copy with the two the other way round, the kernel's order.

The last sentence of the comment shapes the file. The tests and benchmarks are C++ and include
`seqlz.h` inside `extern "C" { }`. A macro named `bool` or `NULL` would break them, so the header
takes those from the compiler's headers and never defines them itself.
@more {#ch-compat--top} Where the licence and the pattern come from
`zstd` is licensed the same way, `MIT OR GPL-2.0-only`
([`docs/plan.md`](https://github.com/martinus/seqlz/blob/main/docs/plan.md#project-constraints)).

The pattern of the header has precedent in `lib/`: the kernel's own `tools/` builds
`lib/bootconfig.c`, whose `#ifdef __KERNEL__` picks the kernel's headers only in a kernel build, and
`lib/rbtree.c` against the stand-in headers of `tools/include/`. The comment named
`lib/decompress_unxz.c` until 11th October 2026; nothing in `tools/` builds that one
([#183](https://github.com/martinus/seqlz/issues/183)).

The switch between the two halves is line 16: the kernel's build compiles every file with
`-D__KERNEL__`.
@end

## In a kernel build

@@ 16-46
Each kernel header brings one or two names, e.g. `linux/math64.h` brings `div_u64()` and `linux/swab.h`
brings `swab64()`. The `__has_include` and `#ifndef` lines are for old kernels: the Mi 9T test phone
runs Linux 4.14 from 2017, and `tools/zram-phone/` builds `src/` as it is into a module for it. Each
fallback is a name that arrived later, e.g. `linux/unaligned.h` only in 6.12.

C11's `_Static_assert` needs a message, the kernel's `static_assert()` does not. So every
`static_assert()` in `src/` has one, to work with the fallback.
@more {#ch-compat--kernel} Which kernel header brings which name, and since when
| header | brings |
| --- | --- |
| `linux/build_bug.h` | `static_assert()` |
| `linux/cache.h` | `L1_CACHE_BYTES` |
| `linux/compiler.h` | `__always_inline`, `noinline`, `__cold` |
| `linux/errno.h` | `EINVAL` |
| `linux/math64.h` | `div_u64()` |
| `linux/minmax.h`, or `linux/kernel.h` | `min()` |
| `linux/prefetch.h` | `prefetch()` |
| `linux/stddef.h` | `sizeof_field()` |
| `linux/string.h` | the kernel's own `memcpy()`, `memmove()` and `memset()` |
| `linux/swab.h` | `swab64()` |
| `linux/types.h` | `u8` to `u64` |
| `linux/unaligned.h`, or `asm/unaligned.h` | `get_unaligned_le64()` and the other accessors |

The fallbacks are for 4.14, each for a name that arrived later: `min()` moved from `linux/kernel.h`
into `linux/minmax.h` in 5.10, the unaligned accessors from each architecture's `asm/unaligned.h` into
`linux/unaligned.h` in 6.12, the kernel's `static_assert()` came with 5.1 and `sizeof_field()` with
5.5.

The commit that brought the header, `e814b1c`, built the codec against a current tree with gcc, clang
and arm64 clang with `W=1`, the kernel's build with extra warnings, without a warning, and against the
Mi 9T's 4.14 tree.
@end

@@ 47-55
The only place where the header changes a kernel value. Older arm64 kernels say a cache line is 128
bytes, the phone's cores have 64. The decoder prefetches its tables with one `prefetch()` every
`L1_CACHE_BYTES`. With 128 it asks for the lines at 0, 128, 256, and never for the ones at 64, 192,
320: every second line of the token table would arrive late.

The `#if` can compare the value because the kernel defines it as `(1 << L1_CACHE_SHIFT)`, a number
the preprocessor can compute. The `#undef` only affects the codec's files.
@more {#ch-compat--cache-line} 64 or 128 bytes a line, prefetch by prefetch
The Mi 9T's Cortex-A76 and A55 have lines of 64 bytes, and mainline arm64 says 64 too; 4.14 says 128.
The decoder prefetches its tables before it decodes a page, in
[`prefetch_lines()`](#ch-dec-basics--prefetch), in steps of {{ch05.prefetch_step}} bytes with one
`prefetch()` every `L1_CACHE_BYTES` inside a step:

```c
for (; q < end; q += PREFETCH_STEP)          /* PREFETCH_STEP is {{ch05.prefetch_step}} */
	for (k = 0; k < PREFETCH_STEP; k += L1_CACHE_BYTES)
		prefetch(q + k);
```

With 64 that is {{ch03.prefetch_lines}} prefetches per {{ch05.prefetch_step}} bytes, one per line.
With 128 it would be {{ch05.prefetches_128}}, at offsets 0, 128, 256 and 384, and the lines at 64,
192, 320 and 448 would never be asked for.
@assert ch05.prefetch_step == 512

The rest of the kernel stays at 128. The kernel's copy is for current kernels and does not have the
block.
@end

## Outside the kernel

@@ 56-78
@anchor ch-compat--userspace
`stdbool.h` and `stddef.h` are **freestanding** headers: they come with gcc and clang, not with the C
library. The benchmarks also build the codec with the kernel's flags, including `-nostdinc`, which
hides the C library's headers. Then `__has_include(<string.h>)` is 0 and the header only declares the
three functions the codec calls; the linker still finds them in the C library.

Why build with the kernel's flags at all: with userspace flags the compiler uses SSE registers to
vectorize loops, which the kernel forbids (`-mno-sse` on x86-64), and the benchmark would not measure
what the kernel runs
([`docs/plan.md`](https://github.com/martinus/seqlz/blob/main/docs/plan.md#51-codecs-under-test)).
`cmake/kernel_codecs.cmake` builds `lz4`, `lzo` and `zstd` from a kernel tree the same way.

@@ 79-94
The kernel's `u64` is `unsigned long long` on every architecture; glibc's `uint64_t` is
`unsigned long` on 64-bit Linux. Same width, different type, which matters for `printf()` formats and
for picking `__builtin_ctzll()`. Defining it the kernel's way gives userspace exactly the types the
kernel code sees.

`noinline` and `__cold` are only defined for C. In C++ the macros would also replace the words inside
`[[gnu::noinline]]` and `[[gnu::cold]]`, which gcc 16 then ignores with a warning.
@more {#ch-compat--attributes} Who uses `__always_inline`, `noinline` and `__cold`
The codec marks {{ch05.inline_c}} small helpers in its `.c` files and `seqlz_match.h`
`__always_inline`, e.g. `enc_put()`, and {{ch05.inline_compat}} more in this header: a call in a loop
that runs once per sequence or per literal costs more than the helper's work. glibc's `sys/cdefs.h`
has its own `__always_inline`, which means the same, so line 86 defines it only where it is missing.

The decoder uses `noinline` and `__cold` on `lit_load_tail()`, which runs only at the end of a
literal stream, and `noinline` on `seqlz_decode_literals()` ([chapter 13](#ch-literals-dec)).
@end

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
same instruction. This copy is for the benchmarks, so they measure what a patched kernel runs. The
`"m"` operand lets the compiler fold an offset into the instruction, e.g. `prefetcht0 64(%rdi)`.
`cmake/check_prefetch.cmake` fails the tests if the instruction is missing from the decoder.
@anim misses A model of 8 cache misses, one after another or overlapped
@more {#ch-compat--prefetch} What clang drops, what that cost, and the patch
On x86-64 the kernel's `prefetch()` is `__builtin_prefetch()`, so a kernel built with clang has none
of the codec's prefetches. In the kernel VM that made `seqlz-2`'s cold reads at p99 24% slower with
clang than with gcc, on pages a desktop swapped into zram (Ryzen 9 7950X at 4.5 GHz, gcc 16.2.1 and
clang 22.1.8,
[`docs/explored-designs.md`](https://github.com/martinus/seqlz/blob/main/docs/explored-designs.md#a-kernel-built-with-clang-dropped-the-decoders-prefetches-fixed-p99-back-at-gccs)).

::: try What clang makes of a prefetch without SSE
```c
#include "seqlz_compat.h"
void pf(const u8 *p)  { prefetch(p + 64); }
void pfb(const u8 *p) { __builtin_prefetch(p + 64); }
```
`clang -O2 -mno-sse -Isrc -S -o - pf.c` gives `prefetcht0 64(%rdi)` for `pf()` and nothing but `retq`
for `pfb()`. gcc emits `prefetcht0` for both.
:::

The fix belongs to the kernel: the patch gives x86-64 a `prefetch()` of its own, `prefetcht0` in
inline assembly, as 32-bit x86 already had, and `port.py` applies it to the tree it writes into. Its
commit message has the measurement: with clang 22.1.8 in the kernel VM, decompression took 2086 to
2155 ns per page without the patch and 1978 to 1988 ns with it, and a whole swap-in 3887 to 3950 ns
against 3761 to 3775 ns, three boots each
([`docs/explored-designs.md`](https://github.com/martinus/seqlz/blob/main/docs/explored-designs.md#the-kernels-prefetch-on-x86-64-fixed-in-the-kernel-a-patch-gives-clang-its-prefetches-back)).

In line 115 nothing is loaded at C level, and an `asm` without outputs is volatile, so it stays.
`cmake/check_prefetch.cmake`, run by `ctest`, disassembles `seqlz_decompress.c` from the build with
the kernel's flags and looks for `prefetcht0`.
@end
@more {#ch-compat--prefetch-arm64} The kernel's `prefetch()` on arm64 costs a little on the A55
arm64's `prefetch()` in the kernel is inline assembly too, with a `"p"` operand, an address instead of
a memory operand. clang then computes each line's address with an `add` instead of an offset inside
the `prfm` instruction. On the Mi 9T's A55 that cost 0.1 to 0.3 µs per warm read, on the A76 nothing
(NDK r21e's clang 9, the phone's own 4.14 kernel). It was taken anyway, for using the kernel's
`prefetch()` instead of assembly of the codec's own
([`docs/explored-designs.md`](https://github.com/martinus/seqlz/blob/main/docs/explored-designs.md#the-kernel-copys-choices-measured-in-one-boot-and-on-the-phone--o3-kept-one-decode-loop-prefetch-no-__aligned64-no-load16)).
@end

@@ 121-126
@anchor ch-compat--min
A function and not a macro: a macro `min(a, b)` would also expand inside `std::min(a, b)` in the C++
files. `unsigned int` is all its callers need: [`seqlz_token()`](seqlz_internal.h.html#b72) caps `ll`
at {{ch05.ll_cap}} and `ml - 4` at {{ch05.ml_cap}} with it.

@@ 127-135
On 32-bit arm and x86 the compiler turns a `u64 / u32` into a call to a helper of its runtime
library, `__aeabi_uldivmod` or `__udivdi3`. The kernel has neither, so the module would not link. The
kernel's `div_u64()` avoids the call. The codec has one such division, in `own_lengths()`
([chapter 11](book:#ch-literals-enc--own)). `tools/kernel-port/check.sh` looks for calls of those
helpers in the objects of `lib/seqlz/`, because its builds do not link.

@@ 136-141
One caller, [`lit_flush()`](seqlz_compress_literals.c.html#b247) in the literal coder. In the kernel it
is the same builtin. `lit_flush()` called `__builtin_bswap64()` itself until 24th October 2026, when
commit `32a6c7a` brought the kernel copy in line with the kernel's process docs in five small places.

## Byte order at any address

@@ 142-157
On a little-endian CPU (x86-64, arm64) a little-endian value needs nothing and a big-endian one needs a
byte swap, one instruction (`bswap`, `rev`). On a big-endian CPU it is the other way round. Nothing in
the codec reads a big-endian number of 16 or 32 bits, so there is only `SEQLZ_BE64()`.

The `#if` relies on gcc and clang defining both macros. A compiler with neither would see `0 == 0` and
take the big-endian branch.

@@ 158-185
`memcpy()` into a local is the defined way in C to read a number from any address; a cast to `u16 *`
of an odd address is undefined behaviour. With a constant size, gcc and clang make the `memcpy()` one
plain load, e.g. `movzwl 1(%rdi), %eax`. `__always_inline` matters: the `memcpy()` has to see the
caller's address to become one load.

Until 5th October 2026 some loads used the CPU's own byte order, and on s390x every page failed to
round trip. Since then all loads go through these, and CI checks that x86-64, 32-bit x86 and s390x
write the same bytes.
@more {#ch-compat--loads} What the accessors compile to
gcc 16.2.1 for x86-64 and clang 22.1.8 for arm64, both with `-O2`:

```text
get_unaligned_le64(p + 3)    x86-64: movq 3(%rdi), %rax        arm64: ldur x0, [x0, #3]
get_unaligned_be64(p + 3)    x86-64: movq 3(%rdi), %rax        arm64: ldur x8, [x0, #3]
                                     bswap %rax                       rev  x0, x8
get_unaligned_le16(p + 1)    x86-64: movzwl 1(%rdi), %eax      arm64: ldurh w0, [x0, #1]
```

On s390x, which is big endian, clang compiles `get_unaligned_le64()` to `lrvg`, a load that reverses
the bytes on the way, and `get_unaligned_be64()` to a plain `lg`. The kernel's own accessors use
`__builtin_memcpy()` the same way (`include/vdso/unaligned.h` of a current tree).
@end
@more {#ch-compat--endian} 4656 of 4656 pages failed on s390x, and the check since
Until 5th October 2026 the bit reader, the matcher's hash and compare, and the copy of short offsets
loaded 4 and 8 bytes in the CPU's order. On s390x under qemu every page failed to round trip, 4656 of
4656. Commit `91fa53e` routed every load through accessors like these. Since then
`test/seqlz_endian.c` compresses {{ch05.endian_pages}} generated pages, hashes the output, and CI
compares that hash between x86-64, 32-bit x86 and s390x.
@end

@@ 186-216
Little endian puts the byte at `p` in the low bits. The matcher's match length wants that: XOR two
words, and the lowest set bit is in the first byte that differs. The decoder's pattern copy needs it
too. Big endian puts it in the high bits: the bitstreams are read most significant bit
first, so the next bit is the top bit of the loaded word.

The pair has one trick. The encoder writes its bitstream backwards and turns it around at the end with
`put_unaligned_be64(get_unaligned_le64(end - 8 - k), out + k)`: the little-endian load and big-endian
store reverse 8 bytes at once, a load, a `bswap` and a store on x86-64.
@more {#ch-compat--users} Which accessor each caller uses, and why that order
The `u16` at the start of a page and the stream sizes are little endian
([the layout](#ch-format--layout)), the bitstreams are read most significant bit first
([chapter 3](#ch-cpu--endian)).

| accessor | used by | why that order |
| --- | --- | --- |
| `get_unaligned_le16()`, `put_unaligned_le16()` | the `u16` at the start of a page, in `seqlz_decode()` and the encoder | the format says little endian |
| `get_unaligned_le32()` | the matcher's 4-byte compare of a candidate, the stream sizes of coded literals | any fixed order works for a compare; the sizes are little endian |
| `get_unaligned_le32()`, `put_unaligned_le32()`, `get_unaligned_le64()`, `put_unaligned_le64()` | the deltas of level 3's literals, 4 or 8 bytes at a time, in `seqlz_delta_literals()` and in the decoder's `add4()` and `add8()` | any order works, as long as the load and the store use the same one |
| `get_unaligned_le64()` | the matcher's hash and match length, the decoder's pattern for offsets below 8 | the byte at `p` in the low bits, so that bit tricks find the first byte that differs at the bottom |
| `get_unaligned_be64()` | the decoder's bit reader for the sequences, the literal streams | the bitstreams are most significant bit first |
| `put_unaligned_le64()` | the encoder's flush of its bits, the literal coder, the decoder's pattern copy | see [`enc_flush()`](seqlz_compress.c.html#b136) |
| `put_unaligned_be64()` | the encoder's final copy of the bitstream | turns 8 bytes around |
@end
@more {#ch-compat--reverse} The bitstream turned around, on 8 example bytes
The encoder writes the bitstream backwards, from the end of the output buffer down, and at the end
copies it the right way round behind the literals ([`encoder_finish()`](seqlz_compress.c.html#b304)). Byte `k` of the
bitstream is then at `end - 1 - k`. Say the bitstream is the 8 bytes `11 22 33 44 55 66 77 88`. In the
buffer they are the other way round, `88` at `end - 8` and `11` at `end - 1`.

1. `get_unaligned_le64(end - 8)` puts the byte at `end - 8` into the low bits and the byte at
   `end - 1` into the high bits.
2. `put_unaligned_be64()` writes the high bits first, so `out[0]` gets the byte from `end - 1`, the
   bitstream's byte 0, and so on.
3. With more than 8 bytes, the next round, `k = 8`, does the same with the 8 bytes from `end - 16`.

This program does the first round with the accessors of this header:

```c
#include <stdio.h>
#include "seqlz_compat.h"

int main(void)
{
	/* the bitstream 11 22 ... 88, as the encoder left it: backwards */
	static const u8 buf[8] = { 0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11 };
	const u8 *end = buf + sizeof(buf);
	u64 v = get_unaligned_le64(end - 8);
	u8 out[8];
	int k;

	put_unaligned_be64(v, out);
	printf("loaded  0x%016llx\nwritten", v);
	for (k = 0; k < 8; k++)
		printf(" %02x", out[k]);
	printf("\n");
	return 0;
}
```
@program ch05-reverse

It prints:

@output ch05-reverse
@end
