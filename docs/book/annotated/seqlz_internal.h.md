# src/seqlz_internal.h

What the codec's own files share and nobody else needs: the compressor (`seqlz_compress.c`,
`seqlz_compress_literals.c`) and the decompressor (`seqlz_decompress.c`,
`seqlz_decompress_literals.c`). The format's constants are in `seqlz_format.h`, the tables' layout in
`seqlz_tables.h`, and the API for the harness, tests and tools in `seqlz.h`
([chapter 6](book:#ch-header)). Most of the file is constants with the reason for their value next to
them.

A reading order:

1. The literals: [rounds per refill](#b22), [when coding pays](#b27), [the scratch](#b35).
2. The compressor: [its hash table](#b52), [the levels](#b59), [a token's number](#b72),
   [its per-CPU state](#b83), [level 3's records](#b97).
3. The decoder: [`prefetch_lines()`](#b108), which asks for the tables before a page is decoded.
4. [The functions](#b143) the two literal files export to the two main files.

## The head of the file

@@ 1-11
`seqlz_tables.h` brings `seqlz_format.h` with it, so this include gives the codec's files the format,
the tables and the shared helpers at once.

@@ 12-21
A comment without code: why the codec calls `__builtin_ctzll()`, `__builtin_clz()` and
`__builtin_memcpy()` and not the kernel's helpers. The compiler can neither fold a constant through
inline assembly nor merge it with the code around it: with the kernel's `__ffs()` and `fls()`, gcc
wrote `seqlz_decode_literals()` and `seqlz_code_literals()` in another shape (commit `e814b1c`).

`CONFIG_FORTIFY_SOURCE` replaces `memcpy()` with a version that checks sizes. On the Mi 9T's 4.14
kernel, built with clang, that turned every 8- and 16-byte copy into a call: 63 calls to `memcpy()`
in one module of the codec (commit `48a7043`).

## Literals

@@ 22-26
The decoder of the coded literals refills a stream with 8 bytes, then decodes several literals before
the next refill. 64 bits minus up to 7 bits already used in the first byte, minus one marker bit
([the sentinel](book:#ch-literals-dec--sentinel)), leave 56. With codes of at most 10 bits that is
`56 / 10 = 5` literals per stream and round, 40 for all 8 streams.

@@ 27-34
The encoder's rule is `d + coded + 51 < n - n / 16`: `n` literals, `coded` the bytes of the 8
streams, `d` the bytes of a page's own table (0 with a fixed table). E.g. 1000 literals with a fixed
table may take at most 886 bytes coded, 100 literals must fit below 43 bytes.

The 51 bytes pay for a cost that does not shrink with the literals: a cold literal table and a second
pass over a buffer. [Chapter 11](book:#ch-literals-enc--rule) has the measurements behind the number.

@@ 35-41
The scratch is part of the decoder's work memory, one per CPU: 4112 bytes for 4 KiB pages. The loop
over the sequences then reads the decoded literals from it as if they were raw, with the same 16-byte
copies. For raw literals the bytes after them in the compressed page are the 16 bytes of slack.

@@ 42-51
`LIT_STREAM_MAX` is the worst case of one stream: 512 literals of a 4 KiB page with 10-bit codes,
`(512 * 10 + 7) / 8 = 640` bytes. The first assert proves the stream's size field, at most 12 bits
wide, holds it, so the encoder needs no check per page.

The second: class 5's offsets are multiples of 8 sent in `SEQLZ_CLASS5_BITS` bits, so they stay
below `8 << 8 = 2048`, which has to be inside the block.

## The compressor

@@ 52-58
Each entry is a position in the page, so a `u16` is enough for blocks up to 64 KiB. 4096 entries of 2
bytes are one per position of a 4 KiB page, if the hash spread them perfectly. The kernel's copy has
`12U` in place of the `?:`, it compresses 4 KiB blocks only. The measurements of 2048 and 8192
entries are in [chapter 9](book:#ch-matcher--hash).

@@ 59-71
All three levels write the same format and one decoder reads them; they differ in how hard the
compressor works. The code compares with `>=` and `<`, e.g. `level >= SEQLZ_LEVEL_OWN`, never with
`==`, so a level 4 would inherit what levels 2 and 3 do.

The lazy step is [chapter 9](book:#ch-matcher--lazy), the own table and the deltas
[chapter 11](book:#ch-literals-enc--own).

@@ 72-82
A token is three fields in one number: `ll` in the low 4 bits, `ml - 4` in the next 5, the offset's
class above. Each field is capped, and a capped field means a length value follows in the bitstream.
E.g. 1 literal, a match of 7, class 0: `1 + (3 << 4) = 49`. 20 literals, a match of 100, class 3:
`15 + (31 << 4) + (3 << 9) = 2047`, both at their caps.

Only a sequence with a match has a token, so `ml` is at least 4 and `ml - SEQLZ_MIN_MATCH` never
wraps. gcc compiles each `min()` to a conditional move, no branch.

@@ 83-96
The compressor's work memory, one per CPU, since zram compresses on every CPU at once. The union reuses
the hash table, 8 KiB for 4 KiB pages: once the matcher is through the page, the table is garbage, and the
literal coder keeps a copy of the raw literals there while it writes the coded ones over their old
place in `dst` ([in place](book:#ch-literals-enc--in-place)).

`work` adds no byte, `(sizeof(u16) << SEQLZ_HASH_BITS) / sizeof(u64)` is the table's size in `u64`s.
What it adds is the alignment of 8: level 3 puts `struct own_work`, with `u32` members, behind the copy
of the literals.

@@ 97-107
Level 3 decides on deltas once for the whole page, and only after the last sequence does it know which
delta is cheapest. For that it needs each literal's position and the offset of the sequence before,
which plain literals do not carry, so the encoder writes a 6-byte record in front of each sequence's
literals.

The records go into `dst` and not the state, because the state is the hash table until the matcher is
done. The price is room: a page that does not fit with its records is matched again without them, and
stored without deltas.

## Prefetching the tables

@@ 108-122
When a swap-in comes, whatever ran since the last one may have pushed the decoder's tables out of the
caches, and each first lookup into a line is then a miss of about 100 ns. Prefetches issued back to
back let these misses overlap ([prefetches](book:#ch-cpu--prefetch)).

`PREFETCH_STEP` is 512 bytes, 8 lines of 64 bytes per round of the outer loop. The second paragraph is
about the last line of a table, see the next block.
@anim misses A model of 8 cache misses, one after another or overlapped

@@ 123-134
The cast is there because the harness compiles this header as C++, where `void *` does not convert to
`const u8 *` by itself. The inner loop has constant bounds, and the compiler unrolls it to 8
`prefetch()` calls in a row: `prfm` on arm64, `prefetcht0` on x86-64.

`prefetch(end - 1)` covers a table that does not start a line. E.g. a table of 2048 bytes that starts 8
bytes into a line spans 33 lines; the loop asks for the lines of `p`, `p + 64`, ... `p + 1984`, 32
of them, and the last 8 bytes lie in the 33rd. Where `p` starts a line, `end - 1` is in a line the loop
asked for already, and the extra prefetch costs nothing.

@@ 135-142
The loop forms `q + k` up to `q + size - 64` only when `size` is a multiple of 512; otherwise its last
round would prefetch lines behind the table. These asserts stop the build if a table stops being one,
e.g. length codes of at most 6 bits would give a table of 64 entries of 4 bytes, 256 bytes.
`sizeof_field(type, member)` is the kernel's `sizeof(((type *)0)->member)`.

## The literal coder's entry points

@@ 143-156
The literals are coded and decoded in files of their own, so these five are the only functions those
files export; everything else in them is `static`. `seqlz_code_literals()` is level 2, with one of
the fixed tables; `seqlz_code_own()` is level 3, which may also give the page a table of its own and
codes the deltas `seqlz_delta_literals()` left in the state, or the plain literals; `seqlz_code_raw_literals()` is for
`seqlz_encode()`, whose caller has the raw literals elsewhere.

`seqlz_decode_literals()` decodes all coded literals into the scratch and returns where the bitstream
starts. It gets `block`, the output page, because a page's own literal table is built there, before the
loop over the sequences writes the page ([own table](book:#ch-literals-dec--own)).
