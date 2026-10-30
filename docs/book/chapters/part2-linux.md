# `src/linux/`: the kernel's headers outside the kernel {#ch-linux}

::: key In short
- Every file of `src/` includes the kernel's headers by name, `<linux/types.h>` and so on, exactly as
  the kernel's copy does ([three builds](#ch-compat--builds)).
- Outside the kernel, `-Isrc` finds the files of `src/linux/` under those names: each defines only what
  the codec uses of its namesake ([the promise](#ch-compat--top)).
- The ones whose name the C library uses too chain to the system's header first, with
  `#include_next` ([types](#ch-compat--userspace)).
- `prefetch()` is the one place with inline assembly, for userspace only, so that the benchmarks
  measure what a patched kernel runs ([prefetch](#ch-compat--prefetch)).
- Linux 4.14 on the test phone lacks four of these names; `tools/zram-phone/compat/` fills them in
  there ([4.14](#ch-compat--kernel)).
:::

The code of `src/` is written as the kernel's code is: it says `u8`, `get_unaligned_le64()`,
`prefetch()` and `min()`, and includes `<linux/types.h>`, `<linux/unaligned.h>`, `<linux/prefetch.h>`
and `<linux/minmax.h>` for them. In a kernel build those are the kernel's headers. Everywhere else, in
the tests, benchmarks, fuzzers and tools, the compiler finds the files of `src/linux/` under the same
names, because every build of `src/` passes `-Isrc`. The kernel's own `tools/include/` does the same
for the parts of `lib/` that its tools build, e.g. `lib/rbtree.c`.

`src/linux/` also holds `seqlz.h`, the interface of the kernel's copy, which is code of the kernel and
has [its own notes](#f-linux-seqlz-h). The other {{ch00.linux_files}} files, {{ch00.linux_lines}}
lines in all, are not in the kernel's copy. Until 30th October 2026 one header, `seqlz_compat.h`, did
their job with an `#ifdef __KERNEL__` for its two halves, and `port.py` rewrote the includes of every
file; now the files of `src/` have the kernel's includes themselves.

## Three ways `src/` gets compiled {#ch-compat--builds}

| build | who does it | where the names come from |
| --- | --- | --- |
| the kernel's copy | `tools/kernel-port/port.py` writes `lib/seqlz/` into a kernel tree | the kernel's headers |
| `src/` as it is, inside a kernel | `tools/zram-vm/run.sh` (the kernel VM) and `tools/zram-phone/build.sh` (a module for the Mi 9T's 4.14, [zram's glue](#ch-zram-glue)) | the kernel's headers; for 4.14 also [`tools/zram-phone/compat/`](#ch-compat--kernel) |
| userspace | tests, benchmarks, fuzzers, tools, built with CMake | the files of `src/linux/` |

The middle row copies the files of `src/` and its interface, `src/linux/seqlz.h`, into the kernel's
build, and none of the other files of `src/linux/`: there the kernel's own headers have those names.

## The promise {#ch-compat--top}

@listing src/linux/types.h 1-26

Each file defines only what the codec uses of the kernel's header of that name: `types.h` the four
types, no `s8` or `__le32`. `GPL-2.0-only OR MIT` as every file of `src/`: the kernel needs GPL-2.0, MIT
lets other projects use the code too, as `zstd` is licensed
([`docs/plan.md`](https://github.com/martinus/seqlz/blob/main/docs/plan.md#project-constraints)).

The comment in the middle shapes all of them. The tests and benchmarks are C++ and include `seqlz.h`
inside `extern "C" { }`. A macro named `bool` or `NULL` would break them, so the files take those from
the compiler's headers, `stdbool.h` and `stddef.h`, and never define them.

### Types, and a header that libc includes too {#ch-compat--userspace}

The kernel's `u64` is `unsigned long long` on every architecture; glibc's `uint64_t` is `unsigned long`
on 64-bit Linux. Same width, different type, which matters for `printf()` formats and for picking
`__builtin_ctzll()`. Defining it the kernel's way gives userspace exactly the types the kernel code
sees.

The C library has headers named `linux/types.h`, `linux/errno.h`, `linux/stddef.h` and `linux/swab.h`
too, the kernel's UAPI headers, and its own headers include them: glibc's `<errno.h>` includes
`<linux/errno.h>`. With `-Isrc` those includes find the files here first. So each of the four includes
the system's header of the same name first, with `#include_next`, which searches the directories after
the one the current file came from, and then adds its own names. `#pragma GCC system_header` keeps
`-Wpedantic` quiet about `#include_next`, a GCC extension that clang has too.

`stdbool.h` and `stddef.h` are **freestanding** headers: they come with gcc and clang, not with the C
library. The benchmarks also build the codec with the kernel's flags, including `-nostdinc`, which
hides the C library's headers. Then `__has_include_next()` finds nothing, and the files define what the
codec needs on their own; the linker still finds `memcpy()` in the C library.

Why build with the kernel's flags at all: with userspace flags the compiler uses SSE registers to
vectorize loops, which the kernel forbids (`-mno-sse` on x86-64), and the benchmark would not measure
what the kernel runs
([`docs/plan.md`](https://github.com/martinus/seqlz/blob/main/docs/plan.md#51-codecs-under-test)).
`cmake/kernel_codecs.cmake` builds `lz4`, `lzo` and `zstd` from a kernel tree the same way, with
stand-ins of their own in `bench/kernel_codecs/compat/`; seqlz's build there uses `src/linux/`.

@listing src/linux/errno.h 1-19
@listing src/linux/stddef.h 1-17

`sizeof_field()` gives the size of a struct member without an object. The null pointer is never
dereferenced: `sizeof` does not evaluate its operand, it only looks at the type.

## Attributes {#ch-compat--attributes}

@listing src/linux/compiler_types.h 1-21
@listing src/linux/compiler.h 1-7

The kernel's build includes `compiler_types.h` in every file, so no file of the kernel names it. Here
the stand-ins that need its names include it, `prefetch.h` among them, which `seqlz_internal.h`
includes; none of the four that libc's headers include does. Else a C file that includes only
`<errno.h>` and writes `__attribute__((noinline))` would see `noinline` replaced by the macro.

`noinline` and `__cold` are only defined for C. In C++ the macros would also replace the words inside
`[[gnu::noinline]]` and `[[gnu::cold]]`, which gcc 16 then ignores with a warning.

::: why Who uses `__always_inline`, `noinline` and `__cold`
The codec marks {{ch05.inline_c}} small helpers in its `.c` files and its headers `__always_inline`,
e.g. `enc_put()`, and {{ch05.inline_compat}} more in `src/linux/`: a call in a loop that runs once per
sequence or per literal costs more than the helper's work. glibc's `sys/cdefs.h` has its own
`__always_inline`, which means the same, so it is defined only where it is missing.

The decoder uses `noinline` and `__cold` on `lit_load_tail()`, which runs only at the end of a literal
stream, and `noinline` on `seqlz_decode_literals()` ([why](#ch-literals-dec--main)).
:::

## `static_assert()`, exports and the module

@listing src/linux/build_bug.h 1-16

The kernel's `static_assert()` takes an optional message; C11's `_Static_assert` needs one, and only
C23 made it optional. The interface's functions in `seqlz_compress.c` and `seqlz_decompress.c` have
`static_assert()`s without a message, as kernel code does. The kernel's own definition,
`__static_assert(expr, ##__VA_ARGS__, #expr)`, leans on a GCC extension that clang flags before C23;
this one passes `#__VA_ARGS__`, the expression as text, as the message, and an empty string so that
`...` is never empty. In C23 and C++ `static_assert` is a keyword and the file stays out of the way.

@listing src/linux/export.h 1-8
@listing src/linux/module.h 1-11

Outside a module there is nothing to export, so `EXPORT_SYMBOL_GPL(seqlz_compress)` becomes
`extern __typeof__(seqlz_compress) seqlz_compress`, a second declaration of the function, which is
valid C and takes the semicolon after the macro. `MODULE_LICENSE()` and `MODULE_DESCRIPTION()` become a
`static_assert()` that holds, for the same reason. [The kernel's export](#ch-zram-glue--api) says what
they do in a kernel.

## The cache line, and Linux 4.14 {#ch-compat--kernel}

@listing src/linux/cache.h 1-7

64 bytes is right for every x86-64 CPU and for the measured arm64 cores. `L1_CACHE_BYTES` decides only
how many prefetches are sent, never a result.

The test phone, a Xiaomi Mi 9T, runs Linux 4.14 from 2017, and `tools/zram-phone/build.sh` builds
`src/` as it is into a module for it. Four names of `src/`'s includes are younger than that kernel:
`min()` moved from `linux/kernel.h` into `linux/minmax.h` in 5.10, the unaligned accessors from each
architecture's `asm/unaligned.h` into `linux/unaligned.h` in 6.12, the kernel's `static_assert()` came
with 5.1 and `sizeof_field()` with 5.5. `tools/zram-phone/compat/` fills them in, for the phone's
module only:

@listing tools/zram-phone/compat/seqlz_4_14.h 1-32
@listing tools/zram-phone/compat/linux/unaligned.h 1-3

`build.sh` adds `compat/` to the include path after the kernel's own directories, so its
`linux/unaligned.h` and `linux/minmax.h` are found only where 4.14 has no file of that name, and
includes `seqlz_4_14.h` before each file of the module. Nothing is exported there: several builds of
seqlz are loaded side by side, and a symbol may be exported by one module only.

### 64 or 128 bytes a line {#ch-compat--cache-line}

@listing tools/zram-phone/compat/seqlz_4_14_cache.h 1-21

Older arm64 kernels, 4.14 among them, say a cache line is 128 bytes; the phone's Cortex-A76 and A55
have 64, and mainline arm64 says 64 too. The decoder prefetches its tables before it decodes a page, in
[`prefetch_lines()`](#ch-dec-basics--prefetch), in steps of {{ch05.prefetch_step}} bytes with one
`prefetch()` every `L1_CACHE_BYTES` inside a step:

```c
for (; q < end; q += PREFETCH_STEP)          /* PREFETCH_STEP is {{ch05.prefetch_step}} */
	for (k = 0; k < PREFETCH_STEP; k += L1_CACHE_BYTES)
		prefetch(q + k);
```

With 64 that is {{ch03.prefetch_lines}} prefetches per {{ch05.prefetch_step}} bytes, one per line.
With 128 it would be {{ch05.prefetches_128}}, at offsets 0, 128, 256 and 384, and the lines at 64,
192, 320 and 448 would never be asked for: every second line of the token table would arrive late.
@assert ch05.prefetch_step == 512

`L1_CACHE_BYTES` also sets the alignment of kernel structs, through `ARCH_DMA_MINALIGN` and
`____cacheline_aligned`, e.g. that of the crypto context the phone's glue gets from the kernel. So
`build.sh` includes this file only before the codec's files, not before `glue.c`, and only after every
kernel header the codec includes, which it lists from their `#include` lines: those headers see the
kernel's 128, the codec's code sees 64. Until 30th October 2026 the 4.14 part was the first half of
`seqlz_compat.h`, and in the module's every file the override came after the kernel headers that file
had included so far.

## `prefetch()` {#ch-compat--prefetch}

@listing src/linux/prefetch.h 1-23

The only inline assembly in `src/`, and only in userspace. The kernel builds x86-64 with `-mno-sse`.
The instruction `prefetcht0` came with SSE, though it uses no SSE register. gcc still emits it for
`__builtin_prefetch()`; clang silently emits nothing. So a clang build would lose every prefetch of the
decoder.

For the kernel itself, `tools/kernel-port/x86-prefetcht0.patch` makes the kernel's `prefetch()` this
same instruction. This copy is for the benchmarks, so they measure what a patched kernel runs. The
`"m"` operand lets the compiler fold an offset into the instruction, e.g. `prefetcht0 64(%rdi)`.
`cmake/check_prefetch.cmake`, run by `ctest`, disassembles `seqlz_decompress.c` from the build with the
kernel's flags and fails if `prefetcht0` is missing. Nothing is loaded at C level, and an `asm` without
outputs is volatile, so it stays.

@anim misses A model of 8 cache misses, one after another or overlapped

::: measured What clang drops, what that cost, and the patch
On x86-64 the kernel's `prefetch()` is `__builtin_prefetch()`, so a kernel built with clang has none
of the codec's prefetches. In the kernel VM that made `seqlz-2`'s cold reads at p99 24% slower with
clang than with gcc, on pages a desktop swapped into zram (Ryzen 9 7950X at 4.5 GHz, gcc 16.2.1 and
clang 22.1.8,
[`docs/explored-designs.md`](https://github.com/martinus/seqlz/blob/main/docs/explored-designs.md#a-kernel-built-with-clang-dropped-the-decoders-prefetches-fixed-p99-back-at-gccs)).

The fix belongs to the kernel: the patch gives x86-64 a `prefetch()` of its own, `prefetcht0` in
inline assembly, as 32-bit x86 already had, and `port.py` applies it to the tree it writes into. Its
commit message has the measurement: with clang 22.1.8 in the kernel VM, decompression took 2086 to
2155 ns per page without the patch and 1978 to 1988 ns with it, and a whole swap-in 3887 to 3950 ns
against 3761 to 3775 ns, three boots each
([`docs/explored-designs.md`](https://github.com/martinus/seqlz/blob/main/docs/explored-designs.md#the-kernels-prefetch-on-x86-64-fixed-in-the-kernel-a-patch-gives-clang-its-prefetches-back)).
:::

::: try What clang makes of a prefetch without SSE
```c
#include <linux/prefetch.h>
void pf(const unsigned char *p)  { prefetch(p + 64); }
void pfb(const unsigned char *p) { __builtin_prefetch(p + 64); }
```
`clang -O2 -mno-sse -Isrc -S -o - pf.c` gives `prefetcht0 64(%rdi)` for `pf()` and nothing but `retq`
for `pfb()`. gcc emits `prefetcht0` for both.
:::

### The kernel's `prefetch()` on arm64 {#ch-compat--prefetch-arm64}

arm64's `prefetch()` in the kernel is inline assembly too, with a `"p"` operand, an address instead of
a memory operand. clang then computes each line's address with an `add` instead of an offset inside
the `prfm` instruction. On the Mi 9T's A55 that cost 0.1 to 0.3 µs per warm read, on the A76 nothing
(NDK r21e's clang 9, the phone's own 4.14 kernel). It was taken anyway, for using the kernel's
`prefetch()` instead of assembly of the codec's own
([`docs/explored-designs.md`](https://github.com/martinus/seqlz/blob/main/docs/explored-designs.md#the-kernel-copys-choices-measured-in-one-boot-and-on-the-phone--o3-kept-one-decode-loop-prefetch-no-__aligned64-no-load16)).

## Small functions {#ch-compat--min}

@listing src/linux/minmax.h 1-11

A function and not a macro: a macro `min(a, b)` would also expand inside `std::min(a, b)` in the C++
files. `unsigned int` is all its callers need: [`seqlz_token()`](#f-seqlz_internal-h) caps `ll` at
{{ch05.ll_cap}} and `ml - 4` at {{ch05.ml_cap}} with it.

@listing src/linux/math64.h 1-16

On 32-bit arm and x86 the compiler turns a `u64 / u32` into a call to a helper of its runtime library,
`__aeabi_uldivmod` or `__udivdi3`. The kernel has neither, so the module would not link. The kernel's
`div_u64()` avoids the call. The codec has one such division, in `own_lengths()`
([the gain](#f-seqlz_compress_literals-c)). `tools/kernel-port/check.sh` looks for calls of those
helpers in the objects of `lib/seqlz/`, because its builds do not link.

@listing src/linux/swab.h 1-24

One caller, [`lit_flush()`](#f-seqlz_compress_literals-c) in the literal coder; in the kernel it is the
same builtin. It is `static inline`, not `__always_inline`, because libc's headers include this file
too; the compiler inlines it anyway.

@listing src/linux/string.h 1-20

`memcpy()`, `memmove()` and `memset()`, from the C library when there is one; the C library's
`<linux/string.h>` only includes `<string.h>` too.

## Byte order at any address {#ch-compat--loads}

@listing src/linux/unaligned.h 1-24

On a little-endian CPU (x86-64, arm64) a little-endian value needs nothing and a big-endian one needs a
byte swap, one instruction (`bswap`, `rev`). On a big-endian CPU it is the other way round. Nothing in
the codec reads a big-endian number of 16 or 32 bits, so there is only `SEQLZ_BE64()`. The `#if`
relies on gcc and clang defining both macros; a compiler with neither would see `0 == 0` and take the
big-endian branch.

@listing src/linux/unaligned.h 25-59

`memcpy()` into a local is the defined way in C to read a number from any address; a cast to `u16 *` of
an odd address is undefined behaviour. With a constant size, gcc and clang make the `memcpy()` one plain
load, e.g. `movzwl 1(%rdi), %eax`. `__always_inline` matters: the `memcpy()` has to see the caller's
address to become one load. gcc 16.2.1 for x86-64 and clang 22.1.8 for arm64, both with `-O2`:

```text
get_unaligned_le64(p + 3)    x86-64: movq 3(%rdi), %rax        arm64: ldur x0, [x0, #3]
get_unaligned_be64(p + 3)    x86-64: movq 3(%rdi), %rax        arm64: ldur x8, [x0, #3]
                                     bswap %rax                       rev  x0, x8
get_unaligned_le16(p + 1)    x86-64: movzwl 1(%rdi), %eax      arm64: ldurh w0, [x0, #1]
```

On s390x, which is big endian, clang compiles `get_unaligned_le64()` to `lrvg`, a load that reverses
the bytes on the way, and `get_unaligned_be64()` to a plain `lg`. The kernel's own accessors use
`__builtin_memcpy()` the same way (`include/vdso/unaligned.h` of a current tree).

### 4656 of 4656 pages failed on s390x, and the check since {#ch-compat--endian}

Until 5th October 2026 the bit reader, the matcher's hash and compare, and the copy of short offsets
loaded 4 and 8 bytes in the CPU's order. On s390x under qemu every page failed to round trip, 4656 of
4656. Commit `91fa53e` routed every load through accessors like these. Since then
`test/seqlz_endian.c` compresses {{ch05.endian_pages}} generated pages, hashes the output, and CI
compares that hash between x86-64, 32-bit x86 and s390x.

@listing src/linux/unaligned.h 60-81

### Which accessor each caller uses {#ch-compat--users}

Little endian puts the byte at `p` in the low bits. The matcher's match length wants that: XOR two
words, and the lowest set bit is in the first byte that differs. The decoder's pattern copy needs it
too. Big endian puts it in the high bits: the bitstreams are read most significant bit first, so the
next bit is the top bit of the loaded word. The `u16` at the start of a page and the stream sizes are
little endian ([the layout](#ch-format--layout)), the bitstreams are read most significant bit first
([chapter 3](#ch-cpu--endian)).

| accessor | used by | why that order |
| --- | --- | --- |
| `get_unaligned_le16()`, `put_unaligned_le16()` | the `u16` at the start of a page, in `seqlz_decompress()` and the encoder | the format says little endian |
| `get_unaligned_le32()` | the matcher's 4-byte compare of a candidate, the stream sizes of coded literals | any fixed order works for a compare; the sizes are little endian |
| `get_unaligned_le32()`, `put_unaligned_le32()`, `get_unaligned_le64()`, `put_unaligned_le64()` | the deltas of level 3's literals, 4 or 8 bytes at a time, in `seqlz_delta_literals()` and in the decoder's `add4()` and `add8()` | any order works, as long as the load and the store use the same one |
| `get_unaligned_le64()` | the matcher's hash and match length, the decoder's pattern for offsets below 8 | the byte at `p` in the low bits, so that bit tricks find the first byte that differs at the bottom |
| `get_unaligned_be64()` | the decoder's bit reader for the sequences, the literal streams | the bitstreams are most significant bit first |
| `put_unaligned_le64()` | the encoder's flush of its bits, the literal coder, the decoder's pattern copy | see [`enc_flush()`](#f-seqlz_encoder-h) |
| `put_unaligned_be64()` | the encoder's final copy of the bitstream | turns 8 bytes around |

### The bitstream turned around, on 8 example bytes {#ch-compat--reverse}

The encoder writes its bitstream backwards and turns it around at the end with
`put_unaligned_be64(get_unaligned_le64(end - 8 - k), out + k)` ([`encoder_finish()`](#f-seqlz_encoder-h)):
the little-endian load and big-endian store reverse 8 bytes at once, a load, a `bswap` and a store on
x86-64. Byte `k` of the bitstream is at `end - 1 - k`. Say the bitstream is the 8 bytes
`11 22 33 44 55 66 77 88`. In the buffer they are the other way round, `88` at `end - 8` and `11` at
`end - 1`.

1. `get_unaligned_le64(end - 8)` puts the byte at `end - 8` into the low bits and the byte at
   `end - 1` into the high bits.
2. `put_unaligned_be64()` writes the high bits first, so `out[0]` gets the byte from `end - 1`, the
   bitstream's byte 0, and so on.
3. With more than 8 bytes, the next round, `k = 8`, does the same with the 8 bytes from `end - 16`.

This program does the first round with these accessors:

```c
#include <stdio.h>
#include <linux/unaligned.h>

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
