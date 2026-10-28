# src/seqlz_internal.h

What the codec's own files share and nobody else needs: the compressor (`seqlz_compress.c`,
`seqlz_compress_literals.c`) and the decompressor (`seqlz_decompress.c`,
`seqlz_decompress_literals.c`). The format's constants are in [`seqlz_format.h`](seqlz_format.h.html),
the tables' layout in [`seqlz_tables.h`](seqlz_tables.h.html), and the API for the harness, tests and
tools in [`seqlz.h`](seqlz.h.html). Most of the file is constants with the reason for their value next to
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
The 51 bytes also cover the larger header, at most 13 bytes more than the 2 of a page with raw literals, which
a `static_assert` in `seqlz_compress_literals.c` checks, because the coded literals are written over
the raw ones in place ([in place](book:#ch-literals-enc--in-place)).

@@ 35-41
The scratch is part of the decoder's work memory, one per CPU: 4112 bytes for 4 KiB pages,
{{k16.const.SCRATCH|grp}} for 16 KiB pages. The loop
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
@more {#ch-header--compressor} The three levels side by side, measured
A **level** is what zram calls the knob a user sets per device, `level=` in
`/sys/block/zram0/algorithm_params` ([the backend](#ch-zram-glue--backend)). seqlz's three levels
write the same format, and one decoder reads them all:

| level | harness name | matcher | literals |
| --- | --- | --- | --- |
| 1 | `seqlz-1` | greedy | raw |
| 2, `SEQLZ_LEVEL_CODED` | `seqlz-2` | greedy | one of the 8 fixed tables, where it saves enough |
| 3, `SEQLZ_LEVEL_OWN` | `seqlz-3` | a lazy step for matches below 20 bytes, each match extended backwards | also a table of the page's own, and stored as deltas to earlier bytes of the page first |
@assert ch09.lazy_len == 20

On the Mi 9T's Cortex-A76, in the phone's own kernel on 15th October 2026, level 3 stored a page of a
phone session in 952.0 bytes of zsmalloc memory against 997.0 for level 2, and took about 16 µs to
write one against 12 µs
([explored-designs.md](https://github.com/martinus/seqlz/blob/main/docs/explored-designs.md#level-3-a-lazy-matcher-and-a-literal-table-of-the-pages-own-zstd-3s-memory-in-less-than-half-its-time-on-the-phone-kept)).
That was before the deltas, which made level 3's writes about 4 µs slower on the A76 in userspace
([chapter 17](#ch-decisions--deltas)).
@end

@@ 72-82
A token is three fields in one number: `ll` in the low 4 bits, `ml - 4` in the next 5, the offset's
class above. Each field is capped, and a capped field means a length value follows in the bitstream.
E.g. 1 literal, a match of 7, class 0: `1 + (3 << 4) = 49`. 20 literals, a match of 100, class 3:
`15 + (31 << 4) + (3 << 9) = 2047`, both at their caps.

Only a sequence with a match has a token, so `ml` is at least 4 and `ml - SEQLZ_MIN_MATCH` never
wraps; the last sequence's `ml` of 0 would wrap, and `min()` would make it 31. gcc compiles each
`min()` to a conditional move, no branch.
@more {#ch-header--token-number} Tokens 49 and 2047 in the bitstream, and the line before 11th October
Token 49 is the sequence of an array of pointers, 1 literal and 7 bytes at the repeat offset
([the page of pointers](#ch-matcher--back)), with a {{tok.len.49}}-bit code. Token 2047 has both
fields at their caps, so both length values follow, `ll - 15 = 5` and `ml - 35 = 65`.
@assert ch06.tok[1].tok == 49 and ch06.tok[3].tok == 2047

Until 11th October 2026 the last sequence had a token, and the line was
`ml ? min(ml - 4, SEQLZ_ML_CAP) : 0`.
@end

@@ 83-96
The compressor's work memory, one per CPU, since zram compresses on every CPU at once. The union reuses
the hash table, 8 KiB for 4 KiB pages: once the matcher is through the page, the table is garbage, and the
literal coder keeps a copy of the raw literals there while it writes the coded ones over their old
place in `dst` ([in place](book:#ch-literals-enc--in-place)).

`work` adds no byte, `(sizeof(u16) << SEQLZ_HASH_BITS) / sizeof(u64)` is the table's size in `u64`s.
What it adds is the alignment of 8: level 3 puts `struct own_work`, with `u32` members, behind the copy
of the literals.
@more {#ch-header--state} What lies where in `struct seqlz_state`
A `static_assert` in `seqlz_compress_literals.c` checks that the hash table alone has room for a page,
so the union is {{size.state}} bytes, no more than the table, and {{k16.size.state|grp}} on 16 KiB
pages. At level 3 with deltas the copy of the literals is the deltas, which `seqlz_delta_literals()`
writes there. `struct own_work`, what the encoder needs for a page's own table, lies at byte
{{own.enc.work_at}}, behind that copy, which on a page that gets such a table is never longer
([own table](#ch-literals-enc--own)).

In the kernel the hash table and the decoder's scratch share one allocation of 8 KiB per CPU
([the backend](#ch-zram-glue--backend)).
@end

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
@more {#ch-dec-basics--prefetch} Why the tables are prefetched, 8 lines at a time
The decoder looks things up in tables: one lookup in the token table, 8 KiB, per sequence, and for
coded literals one lookup per literal in a literal table of 2 KiB. Left alone, it would pay the misses
one after the other, each time a lookup reaches a line that is not there yet. `prefetch(p)` says "the
line of `p` will be needed soon" and does not wait for it. Issued for every line of the tables at the
start, before the header is read, the trips to memory run at the same time, and by the time the loop
does its lookups most lines have arrived. `prefetch()` is `prfm` on arm64 and `prefetcht0` on x86-64,
the latter with a patch to the kernel ([`prefetch()` without SSE](#ch-compat--prefetch)); `L1_CACHE_BYTES` is the
line size, 64 on both.

The inner loop has constant bounds, and the compiler unrolls it to 8 prefetches in a row. The token
table takes {{ch12.tok.rounds}} rounds, each length table {{ch12.ll.rounds}} and a literal table
{{ch12.lit.rounds}}: {{ch12.lines_raw}} cache lines for a page with raw literals, {{ch12.lines_coded}},
{{ch12.kib_coded}} KiB, for one with coded literals, which prefetches the one literal table it uses. A
page with a table of its own prefetches none: it builds its table ([chapter 13](#ch-literals-dec--own)).
The tables do not grow with the page, so it is the same with 16 KiB pages.
@assert F("k16.ch12.lines_coded") == ch12.lines_coded and ch12.lines_coded - ch12.lines_raw == ch12.lit.cachelines

::: why 8 lines per round, not 1
With one line per round, the loop's add, compare and branch were more instructions than the
prefetches. On the Mi 9T's little Cortex-A55, 8 lines per round took the decoder from 16 809 to 16 547
instructions per page and from 16 676 to 16 247 cycles
([explored-designs.md](https://github.com/martinus/seqlz/blob/main/docs/explored-designs.md#seqlzs-decoder-for-an-in-order-core-15-fewer-instructions-07-to-08-µs-less-at-cold-p99-on-the-a55-kept)).
Measured again in October 2026, the plain loop made the big Cortex-A76 read pages with coded literals
cold in 8.15 to 8.84 µs over the rounds, against 7.38 to 7.99 µs for two builds with 8 lines
([explored-designs.md](https://github.com/martinus/seqlz/blob/main/docs/explored-designs.md#five-simplifications-measured-in-the-vm-and-on-the-phone-the-literal-streams-as-arrays-kept-four-not)).
Why 8 and not all 128 lines of the token table without a loop is not recorded.
:::

[Chapter 3](#ch-cpu--prefetch) has what the prefetches are worth on the A55, 14.5 µs per cold read. On
the A76 they once cost 1.2 µs, and for a while only in-order cores prefetched the token table. Since
10th October 2026 every core runs the same loop with the prefetch
([chapter 17](#ch-decisions--one-loop-for-all-cores)), and the cost did not show again: cold reads in
7.44 and 7.65 µs with it, 8.07 and 7.60 µs without, in two runs
([explored-designs.md](https://github.com/martinus/seqlz/blob/main/docs/explored-designs.md#the-kernel-copys-choices-measured-in-one-boot-and-on-the-phone--o3-kept-one-decode-loop-prefetch-no-__aligned64-no-load16)).
@end

@@ 123-134
The cast is there because the harness compiles this header as C++, where `void *` does not convert to
`const u8 *` by itself. The inner loop has constant bounds, and the compiler unrolls it to 8
`prefetch()` calls in a row: `prfm` on arm64, `prefetcht0` on x86-64.

`prefetch(end - 1)` covers a table that does not start a line. E.g. a table of 2048 bytes that starts 8
bytes into a line spans 33 lines; the loop asks for the lines of `p`, `p + 64`, ... `p + 1984`, 32
of them, and the last 8 bytes lie in the 33rd. Where `p` starts a line, `end - 1` is in a line the loop
asked for already, and the extra prefetch costs nothing.
@more {#ch-dec-basics--end-line} Which tables start inside a cache line
In `struct seqlz_tables` on a 64-bit build the token table is at offset {{ch12.tok.off}}, `ll.decode`
and `ml.decode` at {{ch12.ll.off|grp}} and {{ch12.ml.off|grp}}, {{ch12.ll.mod}} bytes past a
multiple of 64, and the literal tables at {{ch12.lit.off|grp}}, {{ch12.lit1.off|grp}} and so on,
{{ch12.lit.mod}} bytes past one. The harness aligns the struct to a page, so there the last
{{ch12.ll.tail}} entries of each length table and the last {{ch12.lit.tail}} of each literal table are
in a line of their own. The kernel's copy is one static object, which C aligns to its widest member,
the `u64` of `lit_cost`; a kernel build puts it at a multiple of 32 bytes with gcc for x86-64 and of 8
with gcc for arm64 and clang for x86-64. So there any table can start inside a line, and the literal
tables always do.
@assert ch12.ml.mod == ch12.ll.mod and ch12.tok.mod == 0 and ch12.ll.last_entry - ch12.ll.first_extra + 1 == ch12.ll.tail and ch12.lit.last_entry - ch12.lit.first_extra + 1 == ch12.lit.tail and ch12.lit.mod % 8 != 0

Until 11th October 2026 the decoder left that line out (#179). With canonical codes the last entries
belong to the longest codes, the rarest symbols, so it cost a miss only on a rare code. Line 132 is one
`prefetch()` more per table; where the table starts a line, it asks for a line the loop asked for
already. Aligning the tables to 64 bytes instead would need a struct aligned to 64, which the tests and
tools that get it from `malloc()` do not have. On x86-64 the extra prefetch made no difference in a quick
bench; it was not measured on the phone (commit `bafef14`).
@end

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
