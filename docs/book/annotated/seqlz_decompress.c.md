# src/seqlz_decompress.c

The decoder of one page. A compressed page is a 2-byte header (the number of literals and a flag for
coded literals), the literals, and a bitstream with one token per sequence: a sequence is "copy `nl`
literals, then copy `len` bytes from `off` bytes back" ([the layout](book:#ch-format--layout)). The
coded literals are decoded in another file,
[`seqlz_decompress_literals.c`](seqlz_decompress_literals.c.html).

C wants a helper before its caller, so the file reads bottom up. A reading order that follows the
call:

1. [`seqlz_decompress()`](#b624), the only function zram calls: it picks one of two copies of the loop.
2. [`decode_block()`](#b405): the header, the literals, and the three limits of the fast path.
3. The loop: [escape](#b507), [offset](#b523), [fast path](#b535), [slow path](#b570), [end checks](#b604).
4. The helpers, as the loop reaches them: [`next_token_fast()`](#b373),
   [`copy_match_fast()`](#b316), [`copy_literals()`](#b35), [`copy_match()`](#b93), the
   [bit reader](#b152).

The step-through under [the loop](#b507) runs the real `decode_block()` on two pages, one statement
at a time.
@more {#ch-decode--safety} Why no input makes it read or write out of bounds
Constraint C6 of the project is a decoder that is safe and bounded in time for any input; a decoder that
was not was one of the reasons a compressor for zram was not merged before
([plan.md](https://github.com/martinus/seqlz/blob/main/docs/plan.md#1-goal-and-success-criteria)).
For any bytes and any `src_len`:

- **The input.** The header after `src_len >= 2`, raw literals after `2 + n_lit <= src_len`, coded ones
  in `seqlz_decode_literals()`. The bitstream's 8-byte loads run only with 8 bytes left, by the check in
  `seqlz_br_refill()` or by `br.p <= in_fast`, and move `br.p` by at most 7; the byte loop checks every
  byte. `br.end[-1]` is a byte of the input, which has at least 2.
- **The literals.** The fast path reads 16 bytes only at `lit <= lit_bound - 16` and moves `lit` by at most
  14; the slow path checks `lit` and `nl` first, and `copy_literals()` stays below `lit_bound`.
- **The tables.** 11 bits index 2048 token entries, 8 bits 256 length entries; an escaped token's number
  is checked before `token_entry()`, which reads no table.
- **The deltas.** `undelta_literals()` and `undelta_fast()` write only the literals just copied, or the
  16 bytes of the fast path, and read only bytes of the page before them
  ([the literals' safety](#ch-literals-dec--safety)).
- **The page, for matches.** `1 <= off <= d - dst` before each match. `repeat_pattern()` reads up to
  `d + 6`, and its bytes at `d` and after are masked away.
- **Writes.** The fast path writes at most {{ch14.cmf.max_write}} bytes from at least
  {{ch14.room.page}} before `d_end` ([the rooms](#ch-decode--rooms)); the slow path checks `nl` and
  `len` against the room, and its copies check before each 8 or 16 bytes. The scratch gets up to 8
  bytes past the literals, inside its 16 spare ones ([the literals' safety](#ch-literals-dec--safety)).
- **Undefined behaviour.** Every shift count is 0 to 63, pointers are formed only inside their buffers,
  the limits that could fall before one are numbers, and counts that may go negative are `int`.
- **Time.** Every round writes a match of at least 4 bytes inside the page, so after at most
  {{ch14.max_rounds}} rounds the page is full, and each round's work is bounded by the bytes it writes.
  A page's own table takes at most 1024 + 257 steps of `read_own_table()`, whatever `src_len` is
  ([the bound](seqlz_decompress_literals.c.html#b162)).
@assert ch14.max_rounds == 1024

The slowest valid page found is made up: a match of 4 bytes at an offset below 8 in every sequence, every
token escaped, 1022 sequences. It takes 107 099 instructions on arm64 and 71 468 cycles on the A55, about
40 µs at 1.8 GHz. Counted in instructions, the costliest valid pages found take 1.59 times the p99 of real
pages with `seqlz-2`, against 2.58 times for `lz4`; timed on the A55, 1.33 against 1.57
([explored-designs.md](https://github.com/martinus/seqlz/blob/main/docs/explored-designs.md#the-worst-case-the-slowest-pages-found-cost-13-times-the-p99-of-real-ones-as-for-lz4)).
The argument is checked by fuzz targets under the sanitizers and a second decoder written from
`docs/format.md` alone ([the fuzzers](#ch-zram-glue--fuzzing)). On 9th October 2026, 12 instances of AFL++
ran for 2 hours: 1.42 billion inputs on the decoder, 1.94 billion on the decoder against the reference,
nothing found
([plan.md](https://github.com/martinus/seqlz/blob/main/docs/plan.md#phase-4-reference-implementation-format-spec-fuzzing-20-weeks)).
@end
@more {#ch-decode--limits} Where a swap-in still loses time against `lz4`
In a swap-in in the kernel VM, decompression is 2.0 µs of `seqlz-2`'s cold swap-in, against
1.4 µs for `lz4` ([plan.md](https://github.com/martinus/seqlz/blob/main/docs/plan.md#11-the-score-memory-against-time-not-bars)),
and most of the difference is branch mispredictions on a page the CPU has not seen: about 110 more per page
than when the same page was decoded just before
([explored-designs.md](https://github.com/martinus/seqlz/blob/main/docs/explored-designs.md#the-decoder-in-a-fault-found-110-branch-mispredictions-per-page-that-a-decode-of-the-same-page-before-hides)).
The refill's branch is gone from the fast path ([the refill without its branch](#ch-decode--refill-branch)).
The next largest are [the fast path's condition](#b535), about 23 misses per page, and the test
`off >= 8` of [`copy_match_fast()`](#b316), 19. The condition misses once for every sequence with a
length value, which is data, and four ways around it were slower
([explored-designs.md](https://github.com/martinus/seqlz/blob/main/docs/explored-designs.md#the-fast-paths-condition-its-22-misses-per-page-are-the-length-values-four-ways-around-them-slower-not-kept)).
One copy for every offset instead of the test took out 12 misses per page and cost 10 to 19 instructions
per fast sequence, which was more
([explored-designs.md](https://github.com/martinus/seqlz/blob/main/docs/explored-designs.md#the-offset-below-8-one-copy-for-every-offset-saves-12-misses-per-page-and-costs-more-not-kept)).
Fewer length values would need a wider token, a change of the format that was not tried. And the one loop
for all cores costs the in-order A55 up to about 0.3 µs per warm read, the price of not asking the CPU
which core it is.
@end

## Copies

@@ 1-10
The whole file has one function that is not `static`, `seqlz_decompress()` at the bottom, the
kernel's interface itself, with its export and the module's licence. Everything else is inlined into
it.

@@ 11-34
@anchor ch-dec-basics
`__builtin_memcpy()` of a constant size is one unaligned load and one store. It is the builtin because
the kernel's `memcpy()` can become a call with `CONFIG_FORTIFY_SOURCE`.

The two differ in the order of loads and stores. `copy8()` loads, then stores, so a second `copy8()`
may read the bytes the first just wrote: that is what a match with an offset of 8 to 15 needs.
`copy16()` loads both halves first, so it is only for literals, whose source never overlaps the page.
@more {#ch-dec-basics--copies} Why a match is copied with copy8(), on an example
A match may copy bytes it is writing itself. A match of offset 10 and length 16 after `0123456789` must
write `0123456789012345`: its last 6 bytes are copies of its own first 6, as the format copies byte by
byte ([chapter 2](#ch-compression--overlap)).

| copy | loads | stores at | gives |
| --- | --- | --- | --- |
| first `copy8()` | `d - 10` to `d - 3`: `01234567` | `d` | `01234567` |
| second `copy8()` | `d - 2` to `d + 5`: `89` and the `012345` just stored | `d + 8` | `89012345`, right |
| one `copy16()` instead | `d - 10` to `d + 5`, before anything is stored | `d` | its last 6 bytes are what was at `d` to `d + 5` before: wrong |

So `copy16()` is only for literals, whose source, the compressed page or the scratch, never overlaps
the page being written, and matches are copied in pieces of 8, which is right from offset 8 on.

**Why not `copy16()` for matches from offset 16 on?** From offset 16 the source ends before the
destination starts, and `copy16()` would give the same bytes. It would not be faster: here it is two
loads and two stores of 8 bytes, the same four operations as two `copy8()`, only in another order. And
choosing between the two needs a test of `off >= 16`, a branch whose direction depends on the page,
which the CPU guesses wrong often; a wrong guess costs more than the copy
([chapter 3](#ch-cpu--branches)).

::: cpu A load right after a store to the same bytes
A store waits in a store buffer before it reaches the cache. A load of bytes a recent store wrote takes
them from there, **store forwarding**, but on most cores only when all its bytes come from one store.
With offsets from 9 to 15, the second `copy8()` reads bytes of the store just before and of older
memory, and waits until the store reached the cache. `ls_bad_status2.stli_other`, a counter of the
Ryzen 9 7950X, counted 359 such loads per page for `seqlz-1`'s decoder and 363 for `lz4`'s in
September 2026
([explored-designs.md](https://github.com/martinus/seqlz/blob/main/docs/explored-designs.md#zram-prefetch-the-compressed-data-before-decompression)).
Offsets below 8 avoid it: they store one register again and again, with no load after the first.
:::
@end

@@ 35-65
A wild copy: 16 bytes at a time, also past `nl`. The bytes past `nl` are garbage for a moment, the
match after the literals writes over them.

The first `copy16()` has no loop, because most sequences have fewer than 16 literals and a loop that
runs 0 or 1 times mispredicts. `lit_bound` is the end of what may be read, not the end of the literals:
the end of the input for raw literals, the end of the scratch for coded ones. Only within 15 bytes of
either end does it copy byte by byte.
@more {#ch-dec-basics--literals} Why copying 16 bytes is faster than copying 3
A sequence with 3 literals gets 16 bytes copied. The 13 bytes too many land where the sequence's match
goes, and the match writes over them right after. Copying exactly 3 would need a loop that runs 3
times, and `nl` is different for every sequence, so the CPU would guess the loop's end wrong again and
again. One copy of 16 bytes is always the same two loads and two stores, and most sequences have fewer
than 16 literals. This is the wild copy of [chapter 3](#ch-cpu--wild-copy).

Two limits make it safe. The 16 bytes written must fit into the page, `d_end - d >= 16`, and the 16
bytes read must be readable, `lit_bound - lit >= 16`. For raw literals `lit_bound` is the end of the
compressed page, and the bytes after the literals are the bitstream; for coded ones it is the end of
the scratch, which has 16 spare bytes for this. The caller, the slow path of `seqlz_decompress()`, has
checked that the `nl` literals are there and fit into the page.
@end

@@ 66-71
For an offset of 1 to 7, how far on the same 8 bytes of a repeated pattern fit again: the largest
multiple of `off` up to 8. Offset 3 repeats `abc`, and `abcabcab` fits again 6 bytes on. Entry 0 is a
placeholder, offset 0 is rejected before.

@@ 72-92
The 8 bytes that a byte-by-byte copy with offset `off` would write, built in one register. Load 8 bytes
at `d - off`, keep the low `off` bytes, multiply by a constant with a 1 in every byte at a multiple of
`off`. The copies of the pattern do not overlap, so no carry crosses a byte: `abc` times
`0x0001000001000001` is `abcabcab`.

The load is little endian on every CPU because the multiply needs the pattern's first byte in the
lowest byte.
@anim overlap What a match with offset 1, 2 or 3 writes
@more {#ch-dec-basics--pattern} `abc` with offset 3, step by step
A match may start fewer bytes back than it is long, and then repeats what it just wrote: offset 1 after
an `a` with length 6 writes `aaaaaa`, which is how a run of zeros compresses to a few bits.

`repeat_pattern(d, off)` returns the 8 bytes a byte-by-byte copy would write at `d`. The load takes 8
bytes from `d - off`; the first `off` are the pattern, the rest are at `d` and after, not written yet.
The mask keeps the low `8 * off` bits: for `off = 3`, `~0ULL >> {{ch12.rep.3.shift}}` is
`{{ch12.rep.3.mask}}`. The multiply by `repeat[off]` puts a copy of the pattern at every byte where the
constant has a 1. `repeat[3]` is `{{ch12.rep.3.mul}}`, ones in bytes 0, 3 and 6, and the pattern `abc`
is `0x636261`: the three copies do not overlap, so no carry crosses a byte, and the third copy's `c`
falls off the top. The result is `{{ch12.rep.3.prod}}`, the bytes `a b c a b c a b`. Some more, each
computed with `repeat_pattern()` on the `off` bytes `a`, `ab`, ... before `d`:

| `off` | pattern | `repeat[off]` | bytes at `d` | `repeat_step[off]` |
| --- | --- | --- | --- | --- |
| 1 | `{{ch12.rep.1.pat}}` | `{{ch12.rep.1.mul}}` | `{{ch12.rep.1.bytes}}` | {{ch12.rep.1.step}} |
| 3 | `{{ch12.rep.3.pat}}` | `{{ch12.rep.3.mul}}` | `{{ch12.rep.3.bytes}}` | {{ch12.rep.3.step}} |
| 5 | `{{ch12.rep.5.pat}}` | `{{ch12.rep.5.mul}}` | `{{ch12.rep.5.bytes}}` | {{ch12.rep.5.step}} |
| 7 | `{{ch12.rep.7.pat}}` | `{{ch12.rep.7.mul}}` | `{{ch12.rep.7.bytes}}` | {{ch12.rep.7.step}} |
@assert ch12.rep[3].step == 6 and [ch12.rep[k].step for k in (1, 2, 4)] == [8, 8, 8]

`repeat_step[off]` is the largest multiple of `off` up to 8, how far on the same 8 bytes fit again: for
`off = 3` the bytes at `d + 6` start with `a` again. So after the first store at `d`, stores of the same
register at `d + 6`, `d + 12`, ... write the rest of the match, without another load. For offsets 1, 2
and 4 the step is the full 8. Entry 0 of both tables is a placeholder: offset 0 is invalid and never gets
here, and its mask would be a shift by 64. The load and store are little endian on every CPU because the
multiply needs the pattern's first byte in the lowest byte ([chapter 3](#ch-cpu--endian)).

<figure class="diagram">
<div class="scroll">
<svg viewBox="0 0 720 300" role="img" aria-label="A match with offset 3 and length 20: the register abcabcab is stored at d, d plus 6, d plus 12 and d plus 18, each store overlapping the one before by 2 bytes">
  <g font-size="12">
    <text x="10" y="34">page</text>
    <rect x="100" y="20" width="20" height="20" class="fill-sheet" stroke="currentColor"/><text x="106" y="34">a</text><rect x="120" y="20" width="20" height="20" class="fill-sheet" stroke="currentColor"/><text x="126" y="34">b</text><rect x="140" y="20" width="20" height="20" class="fill-sheet" stroke="currentColor"/><text x="146" y="34">c</text>
    <rect x="160" y="20" width="520" height="20" fill="none" stroke="currentColor" stroke-dasharray="3 3"/><text x="300" y="34">not written yet</text>
    <text x="96" y="14" font-size="11">d - 3</text><text x="160" y="14" font-size="11">d</text>
    <text x="10" y="78">pat</text>
    <g class="acc"><rect x="160" y="64" width="20" height="20" class="fill-acc" stroke="currentColor"/><text class="on-fill" x="166" y="78">a</text><rect x="180" y="64" width="20" height="20" class="fill-acc" stroke="currentColor"/><text class="on-fill" x="186" y="78">b</text><rect x="200" y="64" width="20" height="20" class="fill-acc" stroke="currentColor"/><text class="on-fill" x="206" y="78">c</text><rect x="220" y="64" width="20" height="20" class="fill-acc" stroke="currentColor"/><text class="on-fill" x="226" y="78">a</text><rect x="240" y="64" width="20" height="20" class="fill-acc" stroke="currentColor"/><text class="on-fill" x="246" y="78">b</text><rect x="260" y="64" width="20" height="20" class="fill-acc" stroke="currentColor"/><text class="on-fill" x="266" y="78">c</text><rect x="280" y="64" width="20" height="20" class="fill-acc" stroke="currentColor"/><text class="on-fill" x="286" y="78">a</text><rect x="300" y="64" width="20" height="20" class="fill-acc" stroke="currentColor"/><text class="on-fill" x="306" y="78">b</text></g>
    <text x="330" y="78" font-size="11">repeat_pattern(d, 3) = {{ch12.rep.3.prod}}</text>
    <text x="10" y="122">k = 0</text><rect x="160" y="108" width="20" height="20" class="fill-sheet" stroke="currentColor"/><text x="166" y="122">a</text><rect x="180" y="108" width="20" height="20" class="fill-sheet" stroke="currentColor"/><text x="186" y="122">b</text><rect x="200" y="108" width="20" height="20" class="fill-sheet" stroke="currentColor"/><text x="206" y="122">c</text><rect x="220" y="108" width="20" height="20" class="fill-sheet" stroke="currentColor"/><text x="226" y="122">a</text><rect x="240" y="108" width="20" height="20" class="fill-sheet" stroke="currentColor"/><text x="246" y="122">b</text><rect x="260" y="108" width="20" height="20" class="fill-sheet" stroke="currentColor"/><text x="266" y="122">c</text><rect x="280" y="108" width="20" height="20" class="fill-sheet" stroke="currentColor"/><text x="286" y="122">a</text><rect x="300" y="108" width="20" height="20" class="fill-sheet" stroke="currentColor"/><text x="306" y="122">b</text>
    <text x="10" y="152">k = 6</text><rect x="280" y="138" width="20" height="20" class="fill-sheet" stroke="currentColor"/><text x="286" y="152">a</text><rect x="300" y="138" width="20" height="20" class="fill-sheet" stroke="currentColor"/><text x="306" y="152">b</text><rect x="320" y="138" width="20" height="20" class="fill-sheet" stroke="currentColor"/><text x="326" y="152">c</text><rect x="340" y="138" width="20" height="20" class="fill-sheet" stroke="currentColor"/><text x="346" y="152">a</text><rect x="360" y="138" width="20" height="20" class="fill-sheet" stroke="currentColor"/><text x="366" y="152">b</text><rect x="380" y="138" width="20" height="20" class="fill-sheet" stroke="currentColor"/><text x="386" y="152">c</text><rect x="400" y="138" width="20" height="20" class="fill-sheet" stroke="currentColor"/><text x="406" y="152">a</text><rect x="420" y="138" width="20" height="20" class="fill-sheet" stroke="currentColor"/><text x="426" y="152">b</text>
    <text x="10" y="182">k = 12</text><rect x="400" y="168" width="20" height="20" class="fill-sheet" stroke="currentColor"/><text x="406" y="182">a</text><rect x="420" y="168" width="20" height="20" class="fill-sheet" stroke="currentColor"/><text x="426" y="182">b</text><rect x="440" y="168" width="20" height="20" class="fill-sheet" stroke="currentColor"/><text x="446" y="182">c</text><rect x="460" y="168" width="20" height="20" class="fill-sheet" stroke="currentColor"/><text x="466" y="182">a</text><rect x="480" y="168" width="20" height="20" class="fill-sheet" stroke="currentColor"/><text x="486" y="182">b</text><rect x="500" y="168" width="20" height="20" class="fill-sheet" stroke="currentColor"/><text x="506" y="182">c</text><rect x="520" y="168" width="20" height="20" class="fill-sheet" stroke="currentColor"/><text x="526" y="182">a</text><rect x="540" y="168" width="20" height="20" class="fill-sheet" stroke="currentColor"/><text x="546" y="182">b</text>
    <text x="10" y="212">k = 18</text><rect x="520" y="198" width="20" height="20" class="fill-sheet" stroke="currentColor"/><text x="526" y="212">a</text><rect x="540" y="198" width="20" height="20" class="fill-sheet" stroke="currentColor"/><text x="546" y="212">b</text><rect x="560" y="198" width="20" height="20" class="fill-sheet" stroke="currentColor"/><text x="566" y="212">c</text><rect x="580" y="198" width="20" height="20" class="fill-sheet" stroke="currentColor"/><text x="586" y="212">a</text><rect x="600" y="198" width="20" height="20" class="fill-sheet" stroke="currentColor"/><text x="606" y="212">b</text><rect x="620" y="198" width="20" height="20" class="fill-sheet" stroke="currentColor"/><text x="626" y="212">c</text><rect x="640" y="198" width="20" height="20" class="fill-sheet" stroke="currentColor"/><text x="646" y="212">a</text><rect x="660" y="198" width="20" height="20" class="fill-sheet" stroke="currentColor"/><text x="666" y="212">b</text>
    <text x="10" y="262">result</text>
    <rect x="160" y="248" width="20" height="20" class="fill-sheet" stroke="currentColor"/><text x="166" y="262">a</text><rect x="180" y="248" width="20" height="20" class="fill-sheet" stroke="currentColor"/><text x="186" y="262">b</text><rect x="200" y="248" width="20" height="20" class="fill-sheet" stroke="currentColor"/><text x="206" y="262">c</text><rect x="220" y="248" width="20" height="20" class="fill-sheet" stroke="currentColor"/><text x="226" y="262">a</text><rect x="240" y="248" width="20" height="20" class="fill-sheet" stroke="currentColor"/><text x="246" y="262">b</text><rect x="260" y="248" width="20" height="20" class="fill-sheet" stroke="currentColor"/><text x="266" y="262">c</text><rect x="280" y="248" width="20" height="20" class="fill-sheet" stroke="currentColor"/><text x="286" y="262">a</text><rect x="300" y="248" width="20" height="20" class="fill-sheet" stroke="currentColor"/><text x="306" y="262">b</text><rect x="320" y="248" width="20" height="20" class="fill-sheet" stroke="currentColor"/><text x="326" y="262">c</text><rect x="340" y="248" width="20" height="20" class="fill-sheet" stroke="currentColor"/><text x="346" y="262">a</text><rect x="360" y="248" width="20" height="20" class="fill-sheet" stroke="currentColor"/><text x="366" y="262">b</text><rect x="380" y="248" width="20" height="20" class="fill-sheet" stroke="currentColor"/><text x="386" y="262">c</text><rect x="400" y="248" width="20" height="20" class="fill-sheet" stroke="currentColor"/><text x="406" y="262">a</text><rect x="420" y="248" width="20" height="20" class="fill-sheet" stroke="currentColor"/><text x="426" y="262">b</text><rect x="440" y="248" width="20" height="20" class="fill-sheet" stroke="currentColor"/><text x="446" y="262">c</text><rect x="460" y="248" width="20" height="20" class="fill-sheet" stroke="currentColor"/><text x="466" y="262">a</text><rect x="480" y="248" width="20" height="20" class="fill-sheet" stroke="currentColor"/><text x="486" y="262">b</text><rect x="500" y="248" width="20" height="20" class="fill-sheet" stroke="currentColor"/><text x="506" y="262">c</text><rect x="520" y="248" width="20" height="20" class="fill-sheet" stroke="currentColor"/><text x="526" y="262">a</text><rect x="540" y="248" width="20" height="20" class="fill-sheet" stroke="currentColor"/><text x="546" y="262">b</text>
    <g class="mark"><rect x="560" y="248" width="20" height="20" class="fill-mark" stroke="currentColor"/><text class="on-fill" x="566" y="262">c</text><rect x="580" y="248" width="20" height="20" class="fill-mark" stroke="currentColor"/><text class="on-fill" x="586" y="262">a</text><rect x="600" y="248" width="20" height="20" class="fill-mark" stroke="currentColor"/><text class="on-fill" x="606" y="262">b</text><rect x="620" y="248" width="20" height="20" class="fill-mark" stroke="currentColor"/><text class="on-fill" x="626" y="262">c</text><rect x="640" y="248" width="20" height="20" class="fill-mark" stroke="currentColor"/><text class="on-fill" x="646" y="262">a</text><rect x="660" y="248" width="20" height="20" class="fill-mark" stroke="currentColor"/><text class="on-fill" x="666" y="262">b</text></g>
    <text x="160" y="290" font-size="11">d .. d + 19: the match, 20 bytes</text><text x="680" y="290" font-size="11" text-anchor="end">d + 20 .. d + 25: past it</text>
  </g>
</svg>
</div>
<figcaption>A match of offset 3 and length 20: one register, stored 4 times 6 bytes apart; the stores overlap by 2 bytes, and the last one writes 6 bytes past the match, which the next sequence overwrites.</figcaption>
</figure>
@end

@@ 93-128
The match copy of the slow path: matches with a length value (up to a page long) and matches near the
end of the page.

Below offset 8 the bytes overlap the bytes being written. So the pattern is built once and stored every
`step` bytes, stores only, no load waits for a store. `repeat_step[off]` needs no mask, `off` is below 8
here. With fewer than 8 bytes left in the page everything goes to the byte loop at the bottom.

The first decoder copied a match up to the end of the page byte by byte, and its slowest 1% of pages
took more than 10 times as long as `lz4`'s. That is the last sentence of the comment.
@more {#ch-dec-basics--copy-match} copy_match() on two matches, and what stores only bought
Below offset 8 it is the pattern of [`repeat_pattern()`](#ch-dec-basics--pattern): build the register once, store it every `step`
bytes, and no load waits for a store. With fewer than 8 bytes left in the page, the byte loop at the
end copies all of the match. The comment on lines 116 to 119 bounds the load of `repeat_pattern()`: it
reads up to `d - off + 7`, at most `d + 6`, inside the page also when the page is nearly full. For
`off = 3, len = 20` the stores are at `k = 0, 6, 12, 18`, as in [its figure](#ch-dec-basics--pattern). For the example page's match
of [chapter 4](#ch-format--example), `off = 2, len = 4094`, the loop stops at `k = 4088`, where 8 more
bytes would not fit, and the byte loop copies the last 6.
@assert ch10.ab.s1.ml == 4094 and ch10.ab.s1.off == 2

**Until 28th October 2026 the index was `off & 7U`.** It was left over from the line
`step = off >= 8 ? 8U : repeat_step[off & 7U]` before commit `b163902` moved it into this branch.
There the mask made the array read safe for any `off`, most likely so that the compiler could read
before it knew which side of the `?:` it needed; the commit does not say. Inside `if (off < 8)` it
changed nothing: for `off` from 1 to 7, `off & 7U` is `off`, and gcc 16.2.1 and clang 22.1.8 made the
same machine code with and without it, on x86-64 and arm64. Commit `51cb119` removed it.

The caller has checked that `d - off` is inside the page and `d + len` at most `d_end`. The last sentence
of the first paragraph has a measurement behind it: the first decoder of seqlz, in September 2026,
copied a match that ran up to the end of the page byte by byte, all of it. On a page that ends in a long
run of zeros that is up to 4000 single bytes with offset 1, each load waiting for the store before it,
and the slowest 1% of pages took 5700 ns, where `lz4` took 500
([explored-designs.md](https://github.com/martinus/seqlz/blob/main/docs/explored-designs.md#seqlz-lz4s-matches-huffman-coded-sequences-with-static-tables)).

::: measured Stores only, no loads, for short offsets
Before 24th September 2026 the code built the first 8 bytes in a register as now, and then copied `step`
bytes on by loading what the store before had written: the same 8 bytes as the register. Commit
`8e8f0d1` stores the register instead. Decode cycles per page went from 7750 to 7276 in a loop over 2000
pages on the PC, cold p50 from 1405 to 1380 ns in the harness; in the kernel VM the change was within
the scatter. A copy of the first 4 bytes one by one, as `lz4` does it, was slower: the load after four
single-byte stores waits for them
([explored-designs.md](https://github.com/martinus/seqlz/blob/main/docs/explored-designs.md#second-round-branches-tails-and-table-sizes)).
The format has changed since; the numbers say which way the change went, not what it is worth now.
:::
@end

@@ 129-151
From offset 8 on, `copy8()` is right: each load ends before the bytes its store writes. Two copies
without a loop first, since most matches are at most 16 bytes. 32 bytes first made the slowest pages
slower: they have many short matches, and each copied 32 bytes.

The byte loop at the end is the format's own definition of a match. It runs only in the last 7 bytes
of the page.
@more {#ch-dec-basics--first16} Why 16 bytes first, not 32
`k = 16` also when `len` was shorter: the extra bytes are inside the page, and the next sequence
overwrites them. The second of the first two copies reads what the first wrote, e.g. `d - 2` to `d + 5`
for `off = 10`, the table of [the copies](#ch-dec-basics--copies).

79% of the matches are at most 16 bytes, 32 bytes cover 91%
([explored-designs.md](https://github.com/martinus/seqlz/blob/main/docs/explored-designs.md#numbers-that-were-only-in-the-codecs-comments-until-10th-october-2026)).
When this function still copied every match, 32 bytes first made the median page faster and the slow
ones slower, p99 in a decode loop on the PC from 3570 to 4150 ns: the slowest pages have many short
matches, and each copied 32 bytes
([explored-designs.md](https://github.com/martinus/seqlz/blob/main/docs/explored-designs.md#second-round-branches-tails-and-table-sizes)).
Today the fast path takes the short matches, and this function gets mostly those with a length value,
{{ch12.ml_tok}} bytes and more. Measured again in a swap-in in the kernel VM, Ryzen 9 7950X at 4.5 GHz,
gcc 16.2.1, 32 bytes first was 3.5 ns faster on the mean of 6 boots, less than the boots scatter, so 16
stayed
([explored-designs.md](https://github.com/martinus/seqlz/blob/main/docs/explored-designs.md#a-branch-or-more-work-again-in-a-swap-in-the-old-choices-hold-and-loops-over-2000-pages-were-never-trained)).
@assert ch12.ml_tok == ch03.token_ml_max + 1
@end

## The bit reader

@@ 152-167
@anchor ch-dec-basics--bit-reader
After the literals comes the bitstream: per sequence the token's code, the offset bits and the length
values, one after the other without regard to bytes ([the bitstream](#ch-format--bitstream)).

`bits` is left-aligned: the next bit of the stream is bit 63, so the next `n` bits are
`bits >> (64 - n)`, and a code indexes its table with the top bits directly. `count` says how many of
the top bits are real.

`count` is an `int` on purpose. Reading past the end gives zeros and makes `count` negative, and one
check at [the end of the page](#b604) catches that, instead of a check on every read. Wrong bits can
make wrong lengths and offsets, never a wrong memory access: each one is checked where it is used.

@@ 168-179
One unaligned big-endian load of 8 bytes, shifted down below the `count` real bits and ORed in. `p`
moves on by the bytes that went in whole, `(63 - count) >> 3`.

`count | 56` is `56 + count % 8`, so after a refill `count` is 56 to 63 and the decoder can rely on 56
bits. The partial byte at the bottom is not counted; the next refill loads it again to the same place,
so the OR needs no mask.
@anim refill One refill, one operation per step
@more {#ch-dec-basics--or56} Why count |= 56 is the count after a refill
A refill loads the next 8 bytes at `p`, big-endian, so that the byte at `p` is on top, the stream's
order; on x86-64 and arm64 `get_unaligned_be64()` is a load and a byte swap. `>> count` puts them right
below the `count` real bits, and what does not fit falls off the bottom. Then `p` has to move on by the
bytes that went in whole, and `count` has to grow by 8 for each of them. The fast path's
[`next_token_fast()`](#ch-decode--bits-left) calls this half alone, where it knows that 8 bytes are there.

So the new count should be `count + 8 * ((63 - count) >> 3)`, and the code writes `count |= 56`. Why
are they the same? Write `count` as $8a + b$: $a$ whole bytes, 0 to 7, and $b$ bits of a byte that is
only partly in, 0 to 7.

- The bytes that go in whole are $(63 - 8a - b) \gg 3 = 7 - a$, and `p` moves on by them.
- The new count is $8a + b + 8(7 - a) = 56 + b$.
- As a 6-bit number, `count` has $a$ in bits 3 to 5 and $b$ in bits 0 to 2. 56 is `0b111000`, so
  `count | 56` sets bits 3 to 5 and keeps $b$: also $56 + b$.

The OR throws $a$ away and puts 7 in its place, which is exactly what filling the register up to whole
bytes does. It is one instruction, and it does not wait for the shift that works out how far `p` moves,
so the two updates run side by side. After a refill `count` is 56 to 63, and the rest of the decoder can
count on 56 bits.

::: try Check it for every count
This program prints the two forms for a few counts and compares them for all 64; the book runs it when
it is built:

```c
#include <stdio.h>

int main(void)
{
	static const int some[] = { 0, 13, 40, 57, 63 };
	int k, c, bad = 0;

	printf("count  bytes in whole  count + 8 * bytes  count | 56\n");
	for (k = 0; k < 5; k++) {
		c = some[k];
		printf("%5d  %14d  %17d  %10d\n", c, (63 - c) >> 3,
		       c + 8 * ((63 - c) >> 3), c | 56);
	}
	for (c = 0; c < 64; c++)
		bad += (c | 56) != c + 8 * ((63 - c) >> 3);
	printf("counts 0 to 63: %d differ\n", bad);
	return 0;
}
```
@program ch12-or56

It prints:

@output ch12-or56
:::

The $b$ bits of the partly loaded byte at the bottom are not counted. The next refill loads that byte
again, from `p`, and shifts it to the same place, so the OR writes the same bits over them and needs no
mask.

A made-up stream, `dc 65 ef db 8a 11 22 33 44 55 66 77 88 99 aa bb ...`, run through
`seqlz_br_refill()` and `seqlz_br_drop()`:

| step | `bits` | `count` | `p` |
| --- | --- | --- | --- |
| start | `0x0000000000000000` | 0 | 0 |
| refill: `v = {{ch12.br.1.v}}`, `v >> {{ch12.br.1.c0}}` | `{{ch12.br.1.bits}}` | {{ch12.br.1.c0}} \| 56 = {{ch12.br.1.count}} | {{ch12.br.1.p0}} + {{ch12.br.1.num}} / 8 = {{ch12.br.1.p}} |
| drop 15 bits | `{{ch12.br.drop1.bits}}` | {{ch12.br.drop1.count}} | {{ch12.br.drop1.p}} |
| refill: `v = {{ch12.br.2.v}}`, `v >> {{ch12.br.2.c0}} = {{ch12.br.2.vs}}` | `{{ch12.br.2.bits}}` | {{ch12.br.2.c0}} \| 56 = {{ch12.br.2.count}} | {{ch12.br.2.p0}} + {{ch12.br.2.num}} / 8 = {{ch12.br.2.p}} |
| drop 23 bits | `{{ch12.br.drop2.bits}}` | {{ch12.br.drop2.count}} | {{ch12.br.drop2.p}} |
| refill: `v = {{ch12.br.3.v}}`, `v >> {{ch12.br.3.c0}}` | `{{ch12.br.3.bits}}` | {{ch12.br.3.c0}} \| 56 = {{ch12.br.3.count}} | {{ch12.br.3.p0}} + {{ch12.br.3.num}} / 8 = {{ch12.br.3.p}} |

The first refill counts 7 bytes: `0x33` is at the bottom, not counted, and after the drop of 15 bits at
bits 22 to 15, `{{ch12.br.drop1.low}}`. The second refill loads from `p = {{ch12.br.2.p0}}`, so `v`
starts with `0x33`, and `v >> {{ch12.br.2.c0}}`, `{{ch12.br.2.vs}}`, puts it at bits 22 to 15 again,
followed by `0x44` and the top 7 bits of `0x55`. `p` moves on by 2 and stops at `0x55`.
@assert ch12.br[1].p == 7 and ch12.br.drop1.low == "0x198000" and ch12.br[2].vs == "0x19a22a" and ch12.br[2].p == 9

The animation "One refill" under the note runs this table one operation per step, with 64-bit integers in the
browser, as the code does it.
@end

@@ 180-191
With fewer than 8 bytes left, byte by byte: zram hands the decoder a buffer of exactly the compressed
size, and the byte after it can belong to another allocation. The loop also stops at a negative
`count`, where `56 - count` would be a shift by too much.
@more {#ch-dec-basics--byte-loop} Why the refill has a byte loop
While 8 bytes of the stream are left, the refill is [`seqlz_br_refill_8()`](#ch-dec-basics--or56). In the last 7 bytes
it goes byte by byte, for two reasons.

**The end of the buffer.** zram hands the decoder exactly the compressed bytes, and the byte after them
may belong to another allocation, so an 8-byte load near the end could read memory that is not the
decoder's.

**An exact count for the end check.** Why not load the last bytes with zeros behind them, as
`lit_load_tail()` does for the literal streams ([`lit_refill()`](#ch-literals-dec--refill))? Because the end
of `decode_block()` rejects a page unless every byte of the bitstream was read, `br.p == br.end`, fewer
than 8 bits are left, and those are 0 ([the end checks](#ch-decode--end)). A load with zeros behind it
would move `p` past `end` and count the zeros as real bits, and a page cut short would look complete.
The byte loop counts only bytes that are there, and when the bits run out, `count` goes negative, which
that check catches.

Each of the loop's three conditions has a job:

- `p < end`: stop at the end of the input.
- `count <= 56`: a whole byte still fits below the real bits, so `56 - count` is not negative.
- `count >= 0`: once more bits were taken than there are, `56 - count` would be a shift by more than
  56, and below -7 by 64 or more, which C leaves undefined.

The fast half needs `count` from 0 to 63 for its shift. It never sees a negative one, which only happens
after the stream ran out, and never 64, which only the byte half reaches, after which `end - p` stays
below 8. The branch `end - p >= 8` goes the same way for every refill but the last few of a page, which
the CPU predicts. The example page of [chapter 4](#ch-format--example) has a bitstream of
{{ab.b.bytes}} bytes, `{{ab.b.hex}}`, so its first refill is the byte half: `bits = {{ch12.ab.bits}}`,
`count = {{ch12.ab.count}}`, `p = end`, and every later refill does nothing.
@assert ab.b.bytes < 8 and ch12.ab.at_end == 1
@end

@@ 192-198
`(int)n` keeps the subtraction signed, so `count` can go negative. The largest drop is a token's code
with its offset bits, 11 + 14 bits, never 64.

@@ 199-219
A length value: one lookup with the top 8 bits gives the code's length `n`, the number of extra bits
`x` and the base, packed into one `u32`. The value is the base plus the `x` bits after the code.

`(bits << n) >> (64 - x)` would be a shift by 64 for `x = 0`, which is undefined. `>> 1` and then
`>> (63 - x)` gives 0 for `x = 0` and the same otherwise, without a branch.
@more {#ch-dec-basics--value} What seqlz_br_value() returns, on the example page
A plain number: how many literals or match bytes a long sequence has beyond what its token can say. The
token holds `ll` from 0 to 15 and `ml` from 4 to 35, and the top value of each means "this many or
more". With `ll` 15 a literal length value follows and the count of literals is 15 plus the value; with
`ml` 35 a match length value follows and the match is 35 plus the value. That is what `decode_block()`
does with it, `nl += seqlz_br_value(...)` and `len += seqlz_br_value(...)`
([the slow path](#ch-decode--slow)).

The value is sent as a symbol's code and extra bits ([chapter 4](#ch-format--length-values)). Values 0
to 15 are their own symbol, with no extra bits. From 16 on, the symbol says where the value's top bit
is, and the bits below that top bit follow the code; the top bit itself is not sent, the symbol says it.
One lookup with the next 8 bits gives three things packed into one entry, `code length | extra bits << 4
| base << 8` ([the length tables](#ch-internal--decode-tables)): the code's length `n`, the number of extra bits
`x`, and the base, `2^x` from symbol 16 on and the value itself below. The function returns the base plus
the `x` bits right after the code, and drops `n + x` bits.

The match length value of the example page, with the 4 KiB tables: after the escape's code, the token's
12 bits and the offset, {{ch12.ab.tokdrop}} bits, the register is `{{ch12.ab.bits2}}` with
`count = {{ch12.ab.count2}}`.

- The index is the top 8 bits, {{ch12.ab.idx}}, and with `t` the `ml` table, `t->decode[{{ch12.ab.idx}}]` is `{{ch12.ab.entry}}`.
- `n = {{ch12.ab.n}}`: the code `{{ch12.ab.code}}`, symbol {{ch12.ab.sym}}. `x = {{ch12.ab.x}}`, and the
  base is {{ch12.ab.base}}, $2^{ {{ch12.ab.x}} }$.
- `bits << {{ch12.ab.n}}` is `{{ch12.ab.shl}}`, `>> 1` is `{{ch12.ab.shr1}}`, `>> {{ch12.ab.shr2}}` is
  {{ch12.ab.xval}}, the bits `{{ch12.ab.xbits}}`.
- The value is `{{ch12.ab.base}} + {{ch12.ab.xval}} = {{ch12.ab.value}}`, {{ch12.ab.dropped}} bits are
  dropped and `count = {{ch12.ab.count3}}`, the zeros at the end of the last byte.
@assert ch12.ab.code == ml.code[ch12.ab.sym] and ch12.ab.base == 2 ** ch12.ab.x and ch12.ab.count3 == ab.b.pad and ch12.ab.escaped == 1

`seqlz_decompress()` adds the token's {{ch12.ml_tok}}, and the match is {{ch12.ab.match}} bytes, as the
format's example says. A direct symbol: the code `{{ch12.ml0.code}}` gives
`t->decode[{{ch12.ml0.index}}] = {{ch12.ml0.entry}}`, `n = {{ch12.ml0.n}}`, `x = 0`, and the value
{{ch12.ml0.base}}, the shift by 63 gives 0 for the extra bits.
@assert ch12.ml0.x == 0 and ml.code[ch12.ml0.base] == ch12.ml0.code and ch12.ab.match == 4094

The comment's bound, `SEQLZ_MAX_BITS + SEQLZ_BLOCK_BITS`, 20 or 22 bits, has a bit to spare: a code
has at most 8 bits, a value at most {{ch10.len_extra_max}} extra bits with 4 KiB pages and
{{k16.ch10.len_extra_max}} with 16 KiB pages, and the caller refills before, which leaves at least 56.
The largest base, {{k16.ch12.base_max|grp}} with 16 KiB pages, fits the entry's 16 bits.
@assert F("k16.ch12.base_max") < 65536
@end

## Deltas, level 3 only

@@ 220-240
Adds 4 or 8 bytes to the bytes `dist` before them, each byte on its own, modulo 256, in one word.
The low 7 bits of each byte add without a carry into the next byte (at most `0x7f + 0x7f`); bit 7
is then fixed with an XOR of both top bits. A plain word addition would carry from one byte into the
next.

`add8()` only for a distance of 8 or more: with distance 4 its two loads overlap the bytes it writes.
@anchor ch-literals-dec--undelta
@more {#ch-literals-dec--add8} `add8()` on `f0 01` plus `10 00`, step by step
`add4()` and `add8()` add the word `dist` bytes before `p` to the word at `p`, byte by byte, modulo
256: a load, a load, the addition and a store, `put_unaligned_le32()` being the store of
[`src/linux/unaligned.h`](book:#ch-compat--loads). They are the inverse of the encoder's
[`sub_bytes()`](#ch-literals-enc--sub-bytes), which subtracts the same way. `a & low` and `b & low`
keep bits 0 to 6 of each byte; two such bytes sum to at most `0x7f + 0x7f = 0xfe`, so no byte carries
into the next, and the sum's bit 7 is the carry out of bit 6. The true bit 7 of a byte's sum is bit 7
of `a`, XOR bit 7 of `b`, XOR that carry. `(a ^ b) & ~low` is the first two in bit 7 of each byte, and
XORed into the sum it gives the true bit. The carry out of bit 7, which a byte's sum modulo 256 drops,
is never made. The deltas `f0 01` of [`sub_bytes()`](#ch-literals-enc--sub-bytes)'s example plus the
bytes `10 00` they are to:

```text
a, the deltas           {{delta.sub.r}}
b, the bytes before     {{delta.sub.b}}
a & low                 {{delta.add.a_lo}}
b & low                 {{delta.add.b_lo}}
sum                     {{delta.add.sum}}
(a ^ b) & ~low          {{delta.add.fix}}
add8()                  {{delta.add.r}}      bytes 00 01, the literals again
a + b                   {{delta.add.plain}}      bytes 00 02: the carry of 0xf0 + 0x10 went into byte 1
```

@assert delta.add.r == delta.sub.a and delta.add.plain == "0x0000000000000200"

Compiled for arm64 by clang with `-O2`, `add8()` without its load and store is 6 instructions where the
XOR it replaced was 1.
@end

@@ 241-269
How far back the byte is that a literal is a delta to: 4, 8, or derived from the offset of the
sequence before, at least 4 ([chapter 4](book:#ch-format--deltas), [its values](book:#ch-internal--delta-last)).
Both restoring functions below start with it.

@@ 270-298
Level 3 stores some literals as the difference to an earlier byte of the page. They come out of
`seqlz_decode_literals()` still as deltas, and can only be turned back in the page, right after the
copy and before the match after them ([the deltas in the loop](book:#ch-decode--deltas)), because the
byte a delta is to may be one of a match, and that match may copy from them.

`k` starts at `dist - at` near the page's start: literals before byte `dist` have nothing to be a delta
to. Then whole words forward, each reading `dist` bytes back, which are final by then. The index `k`
and not a pointer, because `d + dist` could be past the page, and C does not allow forming that
pointer.
@more Why the bytes `dist` back are always final
`undelta_literals()` restores the `nl` literals at `d`, `at` bytes into the page. `k` is the first
literal that is a delta, by the one rule of the format ([the deltas](#ch-format--deltas)), the same
for all three: 0, the sequence's first literal, when the sequence starts `dist` or more bytes into the
page, else `dist - at`. Then whole words, 8 bytes for a distance of 8 or more, else 4, as long as the
word ends inside the literals, and the last bytes one at a time, where the cast to `u8` is the modulo
256.

Each word reads the word `dist` bytes back, which ends before the word starts, so its bytes are the
page's own already. For the first literals of a sequence they are bytes before `d`: of the match
before, which the loop copied in full before these literals, or of earlier sequences, and nothing
writes there again. For the later literals some of them are earlier literals of the same sequence,
restored by the word before, since the loop goes forward. That is the reason the distance of
`SEQLZ_DELTA_LAST` is never below 4.
@end
@more The first sequence of the page of pointers
The first sequence of the page of pointers is {{delta.ptr.rec.0.ll}} literals at the page's start, with
`SEQLZ_DELTA_8`, so `dist` is 8 and `k` starts at 8. As in [the encoder](#ch-literals-enc--delta-write)
they arrive as `{{delta.ptr.deltas}}` (the first 9 of these bytes). No word of 8 fits before the 9th
literal, so the first 8 bytes stay as they are, the first pointer, and the byte loop restores byte 8,
`10 + 10 = 20`, the low byte of the second pointer: `{{delta.ptr.plain}}`.
@assert delta.ptr.rec[0].ll == 9 and delta.ptr.rec[0].pos == 0 and delta.ptr.pick == ch06.DELTA_8 and delta.ptr.plain == "10 00 56 34 12 7f 00 00 20 30 40 50" and delta.ptr.deltas == "10 00 56 34 12 7f 00 00 10 10 10 10"
@end

@@ 299-315
The same for the fast path, which copied 16 bytes whatever `nl` is: all 16 bytes, no loop, no branch
on `nl`. The bytes past `nl` become garbage, which they were already; the match writes over them.
Near the page's start it falls back to `undelta_literals()`.
@more The second sequence of the page of pointers, on the fast path
The fast path copies 16 bytes of literals whatever `nl` is, at most 14
([the fast path](#ch-decode--fast)), and `undelta_fast()` restores all 16, one path for all three
deltas: the same `dist` as `undelta_literals()`, then two `add8()` for a distance of 8 or more, else
four `add4()`. The first word's source lies wholly before `d`, in bytes the match before or earlier
sequences wrote, which are final; each word after it reads `dist` bytes back, into bytes before `d` or
of a word restored before it. The bytes past `nl`, which `copy16()` copied from the next sequence's
literals, turn into garbage; the match after the literals writes over them before anything reads them.
Only near the page's start, where `d - dst < dist`, do the first literals have no byte to be a delta
to, and there it calls `undelta_literals()` and its loops.

The second sequence of [the page of pointers](#b270), 1 literal at byte {{delta.ptr.rec.1.pos}}, as the
fast path would see it:

| bytes at `d` | plus the 8 bytes at `d - 8` | after the first `add8()` |
| --- | --- | --- |
| `{{delta.ptr.fast.before}}` | `{{delta.ptr.fast.ref}}` | `{{delta.ptr.fast.after}}` |

The 8 bytes at `d - 8` are the first sequence's last literal and the 7 bytes of its match. The first
byte is the low byte of the third pointer again, `10 + 20 = 30`; the other 7, and the 8 of the second
`add8()`, are written over by the 7 bytes of the match and by the sequences after it.
@assert delta.ptr.rec[1].ll == 1 and delta.ptr.rec[1].pos == 16 and delta.ptr.rec[0].ll + 7 == 16 and delta.ptr.fast.before[:2] == "10" and delta.ptr.fast.after[:2] == "30"
@end
@more {#ch-literals-dec--counters} The page of counters, word by word
One literal is too few to see the words at work. The animation takes a page of counters of 4 bytes
instead, each 0 to 11 more than the one before, which level 3 stores with `SEQLZ_DELTA_4`: its first
sequence goes through `undelta_literals()`, with the bytes kept as they are at the start and the byte
loop at the end, and its second through `undelta_fast()`.

@anim undelta The first bytes of the page of counters, one copy, `add4()`, byte or match per step: the deltas copied into the page, the bytes 4 before added back word by word, then the fast path's 16 bytes. Every byte is one the real `seqlz_decompress()` had in the page at that step, recorded with a hook on its `add4()`.
@end
@more {#ch-literals-dec--undelta-cost} What the deltas cost a read, measured
The 16 bytes for every sequence, whatever `nl` is, make the deltas cost more than their count of
literals suggests: with a distance below 8 they are 4 steps through memory, each waiting for the store
of the one before, and each step is 3 instructions longer with the addition than it was with the XOR.
On the Mi 9T, userspace with the kernel's compile flags, 21st October 2026, a level 3 page read 0.13 to
0.24 µs slower with deltas than with the XOR on the A76 and 0.34 to 0.46 µs slower on the A55. Doing
the 16 bytes in registers instead of through memory gained 0.01 to 0.03 µs on the A76 and 0.13 to 0.16
µs on the A55, within the 0.16 µs that two builds of the same level 2 code differed by, and was not
kept
([explored-designs.md](https://github.com/martinus/seqlz/blob/main/docs/explored-designs.md#level-3-stores-its-literals-as-deltas-not-xors-084-fewer-bytes-than-the-xor-kept)).
@end

## Helpers of the fast path

@@ 316-339
A fast match has at most 34 bytes, and the caller guarantees 40 bytes of room, so the copy needs no
loop: 16, 32 or 40 bytes in `copy8()`s. With `8 <= off < 16` a load reads what the store before
wrote, as the overlap needs.

`m = d - off` once, because clang computed `d + 8 - off` again for every load. Always copying 40
bytes, without the branches on `len`, was slower: the extra loads read bytes the stores just before
them wrote.
@more {#ch-decode--copy-match-fast} What `m` and the branches on `len` were measured at
Computing `m = d - off` once, instead of the `d + 8 - off` that clang computed anew for every load,
took the decoder from 17 842 to 17 542 instructions per page on the Mi 9T's little Cortex-A55
([explored-designs.md](https://github.com/martinus/seqlz/blob/main/docs/explored-designs.md#seqlzs-decoder-for-an-in-order-core-15-fewer-instructions-07-to-08-µs-less-at-cold-p99-on-the-a55-kept)).

::: measured Branches on len, not always 40 bytes
On the Mi 9T, 32 bytes always and 40 bytes always both read slower than the code as it is: warm reads
by 0.1 µs on the A76 and 0.5 to 0.8 µs on the A55, cold reads by 1.1 and 1.2 µs on the A76
([explored-designs.md](https://github.com/martinus/seqlz/blob/main/docs/explored-designs.md#6-the-fast-paths-copies-as-they-are)).
Measured again in a swap-in in the kernel VM, where the predictor has not seen the page, the copies
without branches took out 14 mispredictions per page and were still about 30 ns slower per decode, 250
ns at the swap-in's p99, most likely because the added loads read bytes the stores just before them
wrote
([explored-designs.md](https://github.com/martinus/seqlz/blob/main/docs/explored-designs.md#a-branch-or-more-work-again-in-a-swap-in-the-old-choices-hold-and-loops-over-2000-pages-were-never-trained)).
:::
@end

@@ 340-357
Below offset 8: the pattern register, stored 5 times without a loop. The smallest step is 5, so 5
stores cover at least 28 bytes, with step 8 they cover 40, every fast match. The loop runs only for a
longer match with a step of 5, 6 or 7. The caller has rejected offset 0, so `repeat_step[off]` never
reads the placeholder entry 0.
@more {#ch-decode--five-stores} The five stores on numbers, and the loop that was slower
The register is the pattern of [`repeat_pattern()`](#ch-dec-basics--pattern). The smallest step is
{{ch14.cmf.min_step}}, for `off = {{ch14.cmf.min_step_off}}`, and then the 5 stores cover
$4 \times {{ch14.cmf.min_step}} + 8 = {{ch14.cmf.five_stores}}$ bytes, the comment's "at least 28"; with
step 8, for offsets 1, 2 and 4, they cover {{ch14.cmf.five_stores_8}}. The loop runs only for a longer
match with a smaller step, e.g. `off = 5, len = 34` stores at 25 and 30. Run on all offsets 1 to 7 and
lengths 4 to {{ch14.fast_len}} when the book is built, the furthest byte written is
`d + {{ch14.cmf.max_byte}}`: that is the {{ch14.cmf.room}} bytes of room ([the rooms](#ch-decode--rooms)).
@assert ch14.cmf.min_step == 5 and ch14.cmf.min_step_off == 5 and ch14.cmf.step1 == ch14.cmf.step2 == ch14.cmf.step4 == 8 and ch14.cmf.room == 40

A loop instead of the 5 stores, tried again for simpler code before 10th October 2026, was 0.5 to 0.7%
slower in the kernel VM with gcc and clang
([explored-designs.md](https://github.com/martinus/seqlz/blob/main/docs/explored-designs.md#five-simplifications-measured-in-the-vm-and-on-the-phone-the-literal-streams-as-arrays-kept-four-not)).
@end

@@ 358-372
The token: the top 11 bits index a table of 2048 entries. Every index is valid because the token code
is complete ([complete](book:#ch-compression--complete)). The entry has the lengths and the offset's
class, so the offset's bits follow without a second lookup.

It refills only when fewer than 11 + 12 bits are left (a code and the longest offset), so most lookups
use bits that are already in the register and do not wait for a load. Used for the first sequence and
after a slow one.
@more {#ch-decode--next-token} Why a refill only below 23 bits, measured
11 + 12 bits are {{ch14.need}} bits with 4 KiB pages; with 16 KiB pages the longest offset is longer,
and the limit is {{k16.ch14.need}}. The `(int)` keeps the compare signed, so a negative `count` reads as
what it is. A refill puts a load in front of the lookup, whose index comes from `bits`; with the check,
most lookups use bits already in the register, and the refill's load comes about every two or three
sequences.

::: measured Refill below 23 bits instead of every sequence
September 2026, `seqlz-1` in the kernel VM, Ryzen 9 7950X at 4.5 GHz: cold reads, with another page
read first, went from 2811 / 4501 to 2610 / 4300 ns at p50 / p99. A hot loop over 2000 pages got slower,
6883 to 7115 cycles per page, with 39 more mispredictions: the check is a branch on data. The kernel's
numbers decided
([explored-designs.md](https://github.com/martinus/seqlz/blob/main/docs/explored-designs.md#zram-prefetch-the-compressed-data-before-decompression)).
The format of then had other tables and another bit order.
:::
@end

@@ 373-395
Two tricks, both for a page the CPU decodes once (a swap-in):

- The next token is looked up before this sequence's copies. No length value follows on the fast path,
  so the next code starts right after the offset bits. An in-order core such as the Cortex-A55 copies
  while the lookup is on its way ([in-order cores](book:#ch-cpu--in-order)).
- The refill comes every second fast sequence, without checking the bits left. Whether the bits run
  short depends on the codes before, a branch the predictor cannot learn on a page it has not seen; a
  branch that alternates it gets right. Two fast sequences take at most 2 × (11 + 14) = 50 bits, and a
  refill leaves 56.
@more {#ch-decode--next-token-first} The next token before the copies, measured
An out-of-order core would overlap the lookup and the copies anyway; an in-order core such as the A55
would wait for the lookup at the top of the next round. On 3rd October 2026, phone pages in the
userspace harness, the little core's cold p50 against `lz4` went from 2169 to 2007 ns more, the big
core's from 456 to 330
([explored-designs.md](https://github.com/martinus/seqlz/blob/main/docs/explored-designs.md#the-next-token-before-the-copies-reads-faster-on-both-phone-cores-kept)).
@end
@more {#ch-decode--bits-left} Why the bits never run short, for both page sizes
The refill is `seqlz_br_refill_8()`, the fast half of [`seqlz_br_refill()`](#ch-dec-basics--byte-loop),
without the check of bytes left. After it `count` is at least 56
([`count |= 56`](#ch-dec-basics--or56)), and until the next one two fast sequences drop their code and
offset bits, the second one before its own `next_token_fast()` refills.

| page size | most offset bits | one fast sequence | two | left of 56 |
| --- | --- | --- | --- | --- |
| 4 KiB | {{ch14.max_off_bits}}, class 3 | {{ch14.fast_seq_bits}} | {{ch14.fast_two_bits}} | {{ch14.fast_left}} |
| 16 KiB | {{k16.ch14.max_off_bits}}, class 3 | {{k16.ch14.fast_seq_bits}} | {{k16.ch14.fast_two_bits}} | {{k16.ch14.fast_left}} |
@assert ch14.class3_bits == ch14.max_off_bits and F("k16.ch14.class3_bits") == F("k16.ch14.max_off_bits")

The peek at the next token needs 11 of them, and after the first sequence at least
{{k16.ch14.fast_after_one}} are left. An escape refills on its own before its 12 bits, which only adds
bits. After a slow sequence, whose `next_token()` guarantees only {{ch14.need}} or {{k16.ch14.need}}
bits, `skip = 0` restarts the schedule ([the slow path's end](#b582)). The load has its two
preconditions from the fast path's condition: `br.p <= in_fast` leaves 8 bytes of input, and the bounds
above keep `count` at 0 or more.
@end
@more {#ch-decode--refill-branch} The refill without its branch, measured
Whether 23 bits are left depends on the code lengths before, which the predictor learns only for a page
it has seen, and a swap-in decodes a page once. In a page seen once that branch was the largest single
source of mispredictions in the decoder
([explored-designs.md](https://github.com/martinus/seqlz/blob/main/docs/explored-designs.md#which-branches-three-in-the-sequence-loop-are-58-of-the-130-misses-of-a-page-seen-once)).
So the fast path refills on a schedule: `*skip` alternates, and a branch that alternates is one every
predictor gets right.

::: measured The refill without its branch
7th October 2026, x86-64 in userspace, gcc 16.2.1, 19 577 desktop zram pages each decoded once: branch
misses per page from 130 to 101, cycles from about 6960 to about 6700. In the kernel VM, `MODE=swap`,
CPU 2 of the Ryzen 9 7950X at 4.5 GHz, 3 boots each: `zcomp_decompress()` of `seqlz-2` took 1935
to 1941 ns instead of 2010 to 2011 with gcc, 1976 to 1983 instead of 2020 to 2040 with clang 22.1.8. On
the Mi 9T the A76 decoded a page seen once in 3241 to 3272 ns instead of 3309 to 3340; the A55 got
slower and kept a loop of its own until 10th October 2026, when every core got this one loop
([chapter 6](#ch-decisions--one-loop-for-all-cores),
[explored-designs.md](https://github.com/martinus/seqlz/blob/main/docs/explored-designs.md#the-refill-without-its-branch-every-second-fast-sequence-63-ns-less-per-swap-in-on-x86-64-and-the-a76-kept)).
:::
@end

@@ 396-404
The three limits of the fast path. In the page: it writes at most 14 literal bytes and 40 bytes of
match, rounded up to 64. In the literals: one `copy16()`. In the input: one refill of 8 bytes. With
these, the fast path checks nothing else.
@more {#ch-decode--rooms} The three rooms in a figure
A fast sequence writes the 16 bytes of `copy16()` at `d`, moves on by at most {{ch14.fast_nl}}
literals and writes at most {{ch14.cmf.room}} bytes of match, {{ch14.cmf.max_write}} bytes in all, below
the {{ch14.room.page}} of `FAST_BLOCK_ROOM`. The compiler checks that with the `static_assert` of
[the next block](#b405).
@assert ch14.room.page == 64 and ch14.room.lit == 16 and ch14.room.input == 8 and ch14.cmf.max_write == 54 and ch14.fast_nl == 14
@assert ch14.fast_nl + 40 <= ch14.room.page and ch14.fast_nl + ch14.fast_len + 7 <= ch14.room.page and ch14.fast_len <= 40

<figure class="diagram">
<div class="scroll">
<svg viewBox="0 0 720 330" role="img" aria-label="The three rooms of the fast path: in the page it starts no later than 64 bytes before the end and writes at most 54 bytes; in the literals it reads 16 bytes and starts no later than 16 bytes before lit_bound; in the input a refill loads 8 bytes and starts no later than 8 bytes before the end">
  <g font-size="12">
    <text x="10" y="20" font-weight="bold">the page, dst to d_end</text>
    <rect x="20" y="30" width="420" height="24" class="fill-sheet" stroke="currentColor"/>
    <text x="30" y="47">written so far, and room</text>
    <g class="mark"><rect x="440" y="30" width="240" height="24" class="fill-mark" stroke="currentColor"/></g>
    <text x="450" y="47" class="on-fill">last 64 bytes: slow path only</text>
    <text x="440" y="70" font-size="11">d_fast = d_end - 64</text>
    <text x="680" y="70" font-size="11" text-anchor="end">d_end</text>
    <g class="acc"><rect x="440" y="80" width="60.0" height="18" class="fill-acc" stroke="currentColor"/>
    <rect x="492.5" y="100" width="150.0" height="18" class="fill-acc" stroke="currentColor"/></g>
    <text x="434" y="94" font-size="11" text-anchor="end">copy16() of the literals, 16 bytes at d</text>
    <text x="434" y="114" font-size="11" text-anchor="end">copy_match_fast() at d + nl, nl &lt;= 14: up to 40 bytes</text>
    <text x="648.5" y="114" font-size="11">d + 54</text>
    <text x="10" y="150" font-weight="bold">the literals, lit to lit_bound</text>
    <rect x="20" y="160" width="520" height="24" class="fill-sheet" stroke="currentColor"/>
    <text x="30" y="177">literals not copied yet</text>
    <rect x="540" y="160" width="80" height="24" fill="none" stroke="currentColor" stroke-dasharray="3 3"/>
    <text x="546" y="177" font-size="11">readable</text>
    <g class="mark"><rect x="620" y="160" width="60" height="24" class="fill-mark" stroke="currentColor"/></g>
    <text x="540" y="200" font-size="11">lit_end</text>
    <text x="620" y="214" font-size="11" text-anchor="end">lit_fast = lit_bound - 16</text>
    <text x="680" y="200" font-size="11" text-anchor="end">lit_bound</text>
    <text x="20" y="232" font-size="11">raw literals: lit_bound = s_end, the bitstream lies behind lit_end; coded: lit_bound = scratch + {{const.SCRATCH}}</text>
    <text x="10" y="262" font-weight="bold">the bitstream, br.p to s_end</text>
    <rect x="20" y="272" width="630" height="24" class="fill-sheet" stroke="currentColor"/>
    <text x="30" y="289">bytes not yet in the register</text>
    <g class="mark"><rect x="650" y="272" width="30" height="24" class="fill-mark" stroke="currentColor"/></g>
    <text x="650" y="314" font-size="11" text-anchor="end">in_fast = s_end - 8</text>
    <text x="680" y="326" font-size="11" text-anchor="end">s_end</text>
  </g>
</svg>
</div>
<figcaption>Where the fast path may start: at least 64 bytes before the end of the page, of which it writes at most 54; at least 16 readable bytes of literals; at least 8 bytes of bitstream.</figcaption>
</figure>
@end

## decode_block(): the loop

@@ 405-417
The room of the block above, checked by the compiler. A sequence takes the fast path only with `ll`
and `ml` below their caps, so it has at most {{ch14.fast_nl}} literals and a match of at most
{{ch14.fast_len}} bytes. `copy_match_fast()` writes 40 bytes with its five stores, or `len + 7` with its
loop, for a longer match at an offset below 8: {{ch14.fast_nl}} + 40 and {{ch14.fast_nl}} +
{{ch14.fast_len}} + 7 bytes, both within the 64. A change of the caps that lets the fast path write
past its room stops the build, instead of waiting for a fuzzer to find it.

@@ 418-427
@anchor ch-decode
`deltas` is a constant at both calls in `seqlz_decompress()`, so the compiler makes two copies of the loop,
and a page without deltas runs none of their code. A check of `deltas` for every sequence made levels
1 and 2 slower to read. The cost is code size.
@more {#ch-decode--paths} The loop as a flowchart, and how often each path runs
`decode_block()` is the whole decoder: the header, the literals raw or decoded, then one loop over the
sequences, one lookup in the token table each, the offset from the bits after the code, and the copies
of the literals and the match. There is no count of the sequences, and the last one has no token: the
loop ends when the literals left fill the rest of the page.

The loop has two paths. The **fast path** takes a sequence without a length value that is far from the
end of the page, of the literals and of the input: fixed copies without loops and without checks of
room, and the next token's lookup before the copies. The **slow path** takes everything else, with every
check. On 7th October 2026, with the tables and offset classes of then, 178.5 of the 207 sequences of an
average desktop zram page took the fast path
([explored-designs.md](https://github.com/martinus/seqlz/blob/main/docs/explored-designs.md#the-fast-paths-condition-its-22-misses-per-page-are-the-length-values-four-ways-around-them-slower-not-kept)).

<pre class="mermaid">
flowchart TB
    S["decode_block():<br/>prefetch tables, header,<br/>literals raw or decoded"] --> N0["tok = next_token()"]
    N0 --> E{"escape?"}
    E -->|yes| X["refill, read 12 bits,<br/>tok = token_entry(idx, 0)"]
    E -->|no| O
    X --> O["offset from the entry,<br/>drop code and offset bits"]
    O --> C{"no length value,<br/>d &lt;= d_fast,<br/>lit &lt;= lit_fast,<br/>br.p &lt;= in_fast?"}
    C -->|"yes: fast path"| F["next_token_fast():<br/>next token first,<br/>refill every second time"]
    F --> FC["copy16() literals,<br/>undelta_fast() if deltas,<br/>check off,<br/>copy_match_fast()"]
    FC --> E
    C -->|"no: slow path"| L["ll value if any,<br/>check nl, copy_literals(),<br/>undelta_literals() if deltas"]
    L --> M["ml value if any,<br/>check off and len,<br/>copy_match()"]
    M --> D{"literals left =<br/>bytes left?"}
    D -->|yes| END["last sequence:<br/>copy_literals() the literals left,<br/>then the end checks"]
    D -->|no| NT["tok = next_token(), skip = 0"]
    NT --> E
</pre>
@end
@more {#ch-decode--deltas} What the copy with deltas does, and what a test per sequence cost
In the copy with `true` the loop adds the bytes back right after each sequence's literals are copied,
with `undelta_fast()` on the fast path and `undelta_literals()` on the slow path and for the last
sequence ([the deltas](#ch-literals-dec--undelta)): the next match may copy from those literals, so they
have to be the page's own before it. In the copy with `false` the `if (deltas)` lines are gone, and a
page without deltas runs the loop as it was before level 3 had them. [`seqlz_decompress()`](#b624) checks
only which copy to run; `seqlz_decode_literals()` reads and checks the byte that says which delta.

::: why A second copy of the loop, not a test per sequence
The two copies came with the XOR that the deltas replaced. A check of the XOR for every sequence in
the one loop, and of the records in the encoder's, made levels 1 and 2 slower to read and about 6%
slower to write on the Mi 9T's Cortex-A76, userspace, 21st October 2026. With the two copies they read
as fast as before, within the run's spread, and level 3 read 0.37 µs slower per page on the A76 and
1.0 µs on the A55 than without the XOR, the mean of two phone dumps
([explored-designs.md](https://github.com/martinus/seqlz/blob/main/docs/explored-designs.md#level-3-xors-its-literals-with-an-earlier-byte-of-the-page-38-fewer-bytes-than-seqlz-3-of-15-october-kept));
the deltas read another 0.13 to 0.24 µs slower on the A76 and 0.34 to 0.46 µs on the A55
([what the deltas cost](#ch-literals-dec--undelta-cost)). The cost is code size: the loop is in the decoder twice.
:::
@end

@@ 428-437
@anchor ch-decode--setup
`last = 1` is the format's starting repeat offset ([offsets](book:#ch-format--offsets)). `skip = 0`
has the first fast sequence refill. `lit_bound` is where reading has to stop, behind `lit_end`.
`delta`, which delta the literals are, is set by `seqlz_decode_literals()` for coded literals and stays
0 for raw ones. The return values, 0 or `-EINVAL`, are those of
[`seqlz_decompress()` in `seqlz.h`](book:#ch-header--encode-decode).

@@ 438-450
The tables are prefetched before the header is read, so that their cache misses overlap with each
other and with the work on the header and the literals. An in-order core stops at every miss it did not
see coming. That is {{ch14.pf.token}} prefetches for the token table and {{ch14.pf.len}} for each
length table ([why](book:#ch-dec-basics--prefetch)).

The header's low 15 bits are the number of literals; more than a page is invalid, raw or coded.
@anim misses A model of 8 cache misses, one after another or overlapped
@more {#ch-decode--header} Since when `n_lit` is checked here, and `get_unaligned_le16()`
Until 23rd October 2026 only `seqlz_decode_literals()` checked `n_lit` against the page, for coded
literals; a raw page with more literals than bytes was rejected too, but only because the loop never
found its end (commit `3ae2e09`). `seqlz_decode_literals()` still checks it, for the compiler's sake
([the second check](#ch-literals-dec--the-sizes-checked-against-the-input)).

`get_unaligned_le16()` replaced a helper of seqlz's own on 10th October 2026, within the noise in the
kernel VM and on the phone
([explored-designs.md](https://github.com/martinus/seqlz/blob/main/docs/explored-designs.md#the-kernel-copys-choices-measured-in-one-boot-and-on-the-phone--o3-kept-one-decode-loop-prefetch-no-__aligned64-no-load16)).
@end

@@ 451-482
Bit 15 of the header picks the layout. Coded literals are decoded into the scratch first
([`seqlz_decode_literals()`](book:#ch-literals-dec)), which returns where the bitstream starts. It also gets `d`: a page
with a literal table of its own has that table built in the output page, before the loop overwrites
it ([own table](book:#ch-literals-dec--own)). Only a page with coded literals needs the scratch: zram
at level 1 writes only raw literals and may call the decoder without one
([the backend](book:#ch-zram-glue--backend)).

Raw literals are right after the header, with the bitstream behind them. Reading the bitstream as
literals is allowed, it is inside the input, so `lit_bound` is the end of the input.
@anim abbits The 9-byte example page, one field per step
@more {#ch-decode--lit-bound} Where `lit_bound` is for coded literals, and the `u64` sum
Coded literals end at `lit_end` in the scratch, and `lit_bound` is the end of the scratch,
`SEQLZ_SCRATCH` bytes after its start, 16 bytes more than the most literals a page has, for the 16-byte
copies. `seqlz_decode_literals()` also gets `&delta`, for the byte that says which delta.

For raw literals, the `u64` sum `SEQLZ_HEADER + n_lit` cannot wrap. With 15 bits of `n_lit` neither
could a 32-bit one, but then the reader would have to know that.
@end

@@ 483-495
The fast path copies without checking the room, so it is taken only where there is enough: three
compares against these limits. They are numbers and not pointers, because for a short input
`s_end - 8` would point before the buffer, which C does not allow. As numbers they are then below `lit`
and `br.p`, and the fast path is not taken.
@more {#ch-decode--fast-limits} The limits on a page of 9 bytes
`unsigned long` has the size of a pointer on every architecture the kernel has. With a short input the
limits are below `lit` and `br.p`, which never point before byte 2 of the input, so the fast path,
whose refill loads 8 bytes unchecked, is not taken: in a page of 9 bytes `in_fast` is `s + 1`, and
`br.p` is at least `s + 2`. The same holds for `lit_bound - 16` with raw literals. The scratch of coded
literals always has its 16 bytes of room.

Until 23rd October 2026 a `?:` set `in_fast` to 0 for an input shorter than 8 bytes, which the compare
already covered (commit `3ae2e09`).
@end

@@ 496-506
The format has no count of sequences, and the last one has no token: the page ends when the literals
left fill the bytes left. `d_end - d == lit_end - lit` is the same as `d - lit == d_end - lit_end`,
whose right side never changes. So the loop compares two pointers it moves anyway against `end_gap`
([the end](book:#ch-format--end)).

A page of only literals has no token at all and jumps to the end. With raw literals such a page is
never stored, with coded ones it can be smaller than a page. The first `next_token()` finds
`count = 0` and refills.

@@ 507-522
Rare tokens have no code of their own: the escape's code, then the token's number in 12 bits. The
escape's entry has bit 31 set. After the refill and the 12 bits, `token_entry(idx, 0)` builds the entry
the table would have had, with a code length of 0 because the bits are dropped already. From here on an
escaped token is like any other ([token entry](book:#ch-internal--token-entry)).
@stepper decode The real decode_block() on two pages, statement by statement
@more {#ch-internal--the-escape} Token 1797 through the escape, step by step
The escape's {{tok.len.3072}}-bit code `{{tok.code.3072}}` fills the indices {{ch07.tok.3072.first}}
to {{ch07.tok.3072.last}} of the token table with `1U << 31 | {{tok.len.3072}} << 14 =
{{ch07.tok.3072.entry}}` ([the entry's bits](#ch-internal--token-entry)).

Token 1797, 5 literals and a match of 20 at a class 3 offset, has no code. The encoder writes the
escape's `{{tok.code.3072}}`, 1797 in 12 bits, `{{ch07.esc1797.bits}}`, and then the offset, say 1000,
in 12 bits. The decoder's index is {{ch07.esc1797.index}}, in the escape's range; bit 31 is set, so it
drops {{ch07.tok.3072.DROP}} bits, refills, reads 1797, checks it against 3072 and calls
`token_entry(1797, 0)`. That entry describes a token whose code is gone already, `N` 0, `BACK`
{{ch07.esc1797.BACK}}, `DROP` {{ch07.esc1797.DROP}}, `{{ch07.esc1797.entry}}`, and from there the
escaped token goes through the same lines as any other.

This is the one place where the decoder takes a token apart at run time. With the tables and classes
of 14th October 2026 that is about 15 of about 205 sequences per page on desktop pages and 10 of 170 on
phone pages (20 000 pages of each dump, same-filled pages left out, measured on 14th October); those
pay for a refill and 12 more bits anyway.
@end
@more {#ch-decode--escape} Which tokens are escaped, and token 513
Only {{ch14.tokens_coded}} of the {{const.TOKEN_SYMBOLS}} tokens have a code of their own in the 4 KiB
tables; the others are the escape's code, `{{tok.code.3072}}`, and the token's number in 12 bits
([the token](#ch-format--token)). The refill is needed because `next_token()` made sure of only
{{ch14.need}} or {{k16.ch14.need}} bits before the escape's code. Of the 4096 numbers 12 bits can hold,
{{ch14.no_token}} are no token, and the check rejects them.

Token 513, {{ch14.t513.ll}} literal and a match of {{ch14.t513.ml}} at class {{ch14.t513.cls}}, has no
code and comes as `{{tok.code.3072}}`, `{{ch14.t513.bin}}` and {{ch14.t513.raw_bits}} offset bit;
`token_entry(513, 0)` is `{{ch14.t513.entry}}`.
@assert tok.len[513] == 0 and ch14.t513.N == 0
@end
@more {#ch-decode--example} The two pages of the step-through, sequence by sequence
The step-through under the note was recorded while `seqlz_decompress()` decoded the format's example of
[chapter 4](#ch-format--example) and the page of [chapter 5](#ch-trace) when the book was built. It
runs the copy with `deltas` false, as neither page has deltas, so the `if (deltas)` lines never take a
step. Each sequence comes with its cost in bits against `lz4`'s format. Step with the buttons or the
arrow keys, with shift from one sequence to the next.

The example page, {{ab.len}} bytes, has raw literals, `n_lit = {{ab.lits}}`, and a bitstream of
{{ab.b.bytes}} bytes, so the first refill is the byte loop. Its one token is escaped, token
{{ch14.ab.tok}}: `ll` {{ch14.ab.ll}}, `ml` {{ch14.ab.ml}} with a length value, offset {{ch14.ab.raw}}.
It goes the slow path for three reasons at once: the length value, `lit` above `lit_fast`, and `br.p`
above `in_fast`. `copy_literals()` copies `a b` byte by byte, `seqlz_br_value()` reads
{{ch14.ab.value}} ([the example](#ch-dec-basics--value)), and `copy_match()` stores `abababab` up
to `d + {{ch14.ab.store_end}}` and the last {{ch14.ab.bytewise}} bytes one by one. Then `d - lit` is
`end_gap`, 0 literals for 0 bytes, and the {{ch14.ab.count3}} bits left are the zero padding.
@assert ab.len == 9 and ab.lits == 2 and ab.b.bytes == 5 and ch14.ab.escaped == 1 and ch14.ab.value_bit == 1 and ch14.ab.ml == 35 and ch14.ab.count3 == ab.b.pad and ch14.ab.raw == 2

[Chapter 5](#ch-trace)'s page shows the fast path at work. With coded literals it compresses to
{{slab.coded.len}} bytes, and its decode goes like this at the start and at the end:

```text
{{ch14.trace.0}}
{{ch14.trace.1}}
{{ch14.trace.2}}
{{ch14.trace.3}}
{{ch14.trace.4}}
{{ch14.trace.5}}
{{ch14.trace.6}}
{{ch14.trace.7}}
...
{{ch14.trace.8}}
{{ch14.trace.9}}
{{ch14.trace.10}}
{{ch14.trace.11}}
{{ch14.trace.12}}
{{ch14.trace.13}}
```

{{ch14.fast}} of its {{ch14.seqs}} sequences with a token take the fast path. The slow ones have a length
value, or come in the last 8 bytes of the input.
@assert ch14.seqs == slab.tokens and ch14.fast + ch14.slow == ch14.seqs and ch16.slow_novalue_past_in == 1
@end

@@ 523-534
The offset without a branch, from the token's entry: `<< TOK_N` shifts the code out, `>> back` brings
the offset bits down, `<< TOK_SHIFT` scales them (by 2 or 8 for some classes). Class 0 is the repeat
offset: it has no bits, `raw` is meaningless, and the select takes `last`. gcc makes the select a
`cmov`.

`TOK_LL` and `TOK_ML` are the lengths as they are, the match length already with its minimum of 4.
@more {#ch-internal--token-2098-two-literals-a-match-of-7-16-bytes-back} Token 2098's offset, shift by shift
Token 2098 is 2 literals and a match of 7 at offset 16, [the format's example](#ch-format--token):
offset 16 is a multiple of 8 below 256, class 4, sent as $16 / 8 = 2$ in 5 bits. Its code has
{{tok.len.2098}} bits, `{{tok.code.2098}}`, and its entry is `{{ch07.tok.2098.entry}}`
([its bits](#ch-internal--token-entry)): `N` {{ch07.tok.2098.N}}, `BACK` `(64 - 5) & 63` =
{{ch07.tok.2098.BACK}}, `SHIFT` {{ch07.tok.2098.SHIFT}}, `DROP`
${{tok.len.2098}} + 5 = {{ch07.tok.2098.DROP}}$, `LL` {{ch07.tok.2098.LL}}, `ML`
{{ch07.tok.2098.ML}}.

Say the register holds the code, then `{{ch07.reg_raw_bits}}`, then whatever follows:
`{{ch07.reg}}`, the first bit of the stream as bit 63. The top 11 bits are `{{ch07.reg_index_bits}}`,
index {{ch07.reg_index}}, one of the {{ch07.tok.2098.entries}} entries {{ch07.tok.2098.first}} to
{{ch07.tok.2098.last}} of this code. Then:

```text
br.bits                   {{ch07.reg_bin}} ... {{ch07.reg_low}}    {{ch07.reg}}
br.bits << TOK_N   ({{ch07.tok.2098.N}})    {{ch07.reg_shl_bin}} ... {{ch07.reg_shl_low}}    {{ch07.reg_shl}}
             >> TOK_BACK  ({{ch07.tok.2098.BACK}})                         {{ch07.reg_raw_bits}}   {{ch07.reg_raw}}
             << TOK_SHIFT ({{ch07.tok.2098.SHIFT}})                                  {{ch07.reg_off}}
seqlz_br_drop(&br, TOK_DROP) moves on by {{ch07.tok.2098.DROP}} bits, to the next sequence
```

The left shift throws the code away, the right shift keeps the 5 bits behind it, and the shift by 3
multiplies by 8. For token 49, 1 literal and 7 bytes at the repeat offset, `BACK` is 0: the decoder
computes a `raw` it ignores and takes `last`.
@end
@more {#ch-decode--offset} Tokens of classes 1, 4 and 0, side by side
`back` is `64 - raw_bits`, and `<< TOK_SHIFT` multiplies by 2 for class 1 and by 8 for classes 4 and 5.
For class 0, the repeat offset, `back` is stored as `(64 - raw_bits) & 63`, 0 and not 64. Three tokens
from the 4 KiB tables, with made-up bits `0101...` after the code and the offset bits:

| | class {{ch14.tk.a.cls}}: token {{ch14.tk.a.s}} | class {{ch14.tk.b.cls}}: token {{ch14.tk.b.s}} | class {{ch14.tk.c.cls}}: token {{ch14.tk.c.s}} |
| --- | --- | --- | --- |
| sequence | `ll` {{ch14.tk.a.ll}}, `ml` {{ch14.tk.a.ml}}, offset {{ch14.tk.a.off}} | `ll` {{ch14.tk.b.ll}}, `ml` {{ch14.tk.b.ml}}, offset {{ch14.tk.b.off}} | `ll` {{ch14.tk.c.ll}}, `ml` {{ch14.tk.c.ml}}, the last offset |
| code, offset bits | `{{ch14.tk.a.code}}`, `{{ch14.tk.a.ob}}` | `{{ch14.tk.b.code}}`, `{{ch14.tk.b.ob}}` | `{{ch14.tk.c.code}}`, none |
| `TOK_N` / `TOK_BACK` / `TOK_SHIFT` / `TOK_DROP` | {{ch14.tk.a.N}} / {{ch14.tk.a.BACK}} / {{ch14.tk.a.SHIFT}} / {{ch14.tk.a.DROP}} | {{ch14.tk.b.N}} / {{ch14.tk.b.BACK}} / {{ch14.tk.b.SHIFT}} / {{ch14.tk.b.DROP}} | {{ch14.tk.c.N}} / {{ch14.tk.c.BACK}} / {{ch14.tk.c.SHIFT}} / {{ch14.tk.c.DROP}} |
| `br.bits` | `{{ch14.tk.a.bits}}` | `{{ch14.tk.b.bits}}` | `{{ch14.tk.c.bits}}` |
| `<< TOK_N` | `{{ch14.tk.a.shl}}` | `{{ch14.tk.b.shl}}` | `{{ch14.tk.c.shl}}` |
| `>> back` | `{{ch14.tk.a.shr}}` | `{{ch14.tk.b.shr}}` | `{{ch14.tk.c.shr}}` |
| `off` | {{ch14.tk.a.ob_val}} << {{ch14.tk.a.SHIFT}} = {{ch14.tk.a.off}} | {{ch14.tk.b.ob_val}} << {{ch14.tk.b.SHIFT}} = {{ch14.tk.b.off}} | `last` |
@assert ch14.tk.a.SHIFT == 1 and ch14.tk.a.raw_bits == 1 and ch14.tk.c.BACK == 0 and ch14.tk.c.n == 4

The made-up bits do not change the lookup: token {{ch14.tk.c.s}}'s {{ch14.tk.c.n}}-bit code fills the
entries {{ch14.tk.c.idx_lo}} to {{ch14.tk.c.idx_hi}}.
@end
@more {#ch-decode--entry-cost} What the offset from the entry saved, measured
gcc 16.2.1 at `-O2` for x86-64 makes the select a `cmovne`, no branch, and the `& 63U` of `TOK_N()`
costs no instruction, since x86-64 uses only the low 6 bits of a shift count. Before 3rd October 2026
the entry had 2 bytes, and the decoder computed the class, the offset bits and the shift at run time:
18 of about 60 instructions per sequence on the A55. The entries of 4 bytes took the decoder there from
16 547 to 15 539 instructions per page, the length value flag in bit 30 to 15 129
([explored-designs.md](https://github.com/martinus/seqlz/blob/main/docs/explored-designs.md#seqlzs-decoder-for-an-in-order-core-15-fewer-instructions-07-to-08-µs-less-at-cold-p99-on-the-a55-kept)).
@end

@@ 535-569
Most sequences go here. The condition: no length value follows (one bit of the entry), and the three
limits. The body: the next token first, 16 bytes of literals, the offset check, the match.

The offset check `off - 1U >= d - dst` is one unsigned compare for `1 <= off <= d - dst`: `off = 0`
wraps to the largest `unsigned int`.

`nl` is not checked against the literals left. On a damaged page `lit` can run past `lit_end`, but it
stays inside `lit_bound`, and the slow path, where every page ends, rejects it. The page's last match
never takes the fast path: fewer than 8 bits are left after it, and the fast path wants 8 bytes.
@more {#ch-decode--fast} The fast path's condition and body, part by part
Each part of the condition lets the body skip a check. Bit 30 of the entry, `TOK_VALUE_AT`, says that no
length value follows, so `nl` is at most 14 and `len` at most 34; one test instead of two compares, and
without it the A55 read raw pages 0.2 µs slower
([explored-designs.md](https://github.com/martinus/seqlz/blob/main/docs/explored-designs.md#five-simplifications-measured-in-the-vm-and-on-the-phone-the-literal-streams-as-arrays-kept-four-not)).
The other three are [the rooms](#ch-decode--rooms).

Since the last match never takes the fast path, every page ends on the slow path, where the end is
checked. On a damaged page the literals left could equal the bytes left right after a fast sequence;
then every further match moves them apart by its length, they never meet again, and a check of room
rejects the page.

The offset check on numbers: with 24 bytes written, `off = 24` gives `23 >= 24`, false, a valid offset;
`off = 25` gives `24 >= 24`, invalid.

With `deltas`, `undelta_fast()` adds the bytes back on the 16 bytes `copy16()` wrote, before `last`
becomes this sequence's offset: the distance of `SEQLZ_DELTA_LAST` is the offset of the sequence before.
[The animation of the deltas](#anim-undelta) steps through its four `add4()` on a page of counters.
@end
@more {#ch-decode--nl-unchecked} Why `nl` is not checked here, and the check that moved
Leaving out the check of `nl` against the literals left saved 3 instructions per sequence on 3rd
October 2026 (commit `5c769f5`). A page whose tokens claim more literals than its header has moves `lit`
past `lit_end` and copies the bytes behind the literals, still inside `lit_bound`. The next slow
sequence checks `lit > lit_end` first ([the slow path](#b570)), and only the slow path ends the loop,
so the page is rejected.

::: warn The check that moved
The first fast path, in September 2026, needed a check of `nl` that no test covered: without it `lit`
ran past the literals, the slow path computed `lit_end - lit` as a negative number, cast to unsigned, so
every `nl` passed, and `copy_literals()` read behind the input. A test builds such a page since then
([explored-designs.md](https://github.com/martinus/seqlz/blob/main/docs/explored-designs.md#zram-prefetch-the-compressed-data-before-decompression)).
When the check left the fast path on purpose, the `lit > lit_end` of the slow path came with it; without
it, AddressSanitizer reported a heap overflow in that test
([explored-designs.md](https://github.com/martinus/seqlz/blob/main/docs/explored-designs.md#seqlzs-decoder-for-an-in-order-core-15-fewer-instructions-07-to-08-µs-less-at-cold-p99-on-the-a55-kept)).
:::
@end

@@ 570-581
The slow path, with every check. `nl == 15` means a length value follows.

The order of the checks matters: `lit > lit_end` first, else `lit_end - lit` wraps and any `nl`
passes. The first fast path in September 2026 missed exactly this, and a test builds such a page since
then.
@more {#ch-decode--slow} The literal length value, and the token the last sequence lost
An `ll` of 15 means a literal length value follows the offset bits
([length values](#ch-format--length-values)); the refill makes sure of the 20 or 22 bits
[`seqlz_br_value()`](#ch-dec-basics--value) may take. [`copy_literals()`](#ch-dec-basics--literals)
may read up to `lit_bound`. With `deltas`, `undelta_literals()` restores exactly the `nl` literals, again
before `last` changes.

Until 11th October 2026 the last sequence had a token too, with a match of nothing, and the decoder
checked for it here; it said what the decoder knew already and cost about 1.2 bytes per page
([explored-designs.md](https://github.com/martinus/seqlz/blob/main/docs/explored-designs.md#the-last-sequence-without-a-token-12-bytes-per-page-less-kept)).
@end

@@ 582-603
A match length of 35 in the token means a length value follows. With it `len` can be longer than a
page, which the check of room catches. `last` is set before the checks; when they fail it no longer
matters.

When the literals left fill the bytes left, no match can follow (a match has at least 4 bytes): the
loop ends. Otherwise the next token comes through the checked `next_token()`, and `skip = 0` restarts
the fast path's refill schedule.
@more {#ch-decode--long-match} How long `len` can get
With 4 KiB pages a match length value is at most {{ch14.max_value}}, so `len` can be {{ch14.max_len}},
more than a page; `len > d_end - d` rejects it before [`copy_match()`](#ch-dec-basics--copy-match).
@assert ch14.max_len > 4096
@end

@@ 604-623
The last sequence: the literals left, exactly the room left. Then the page has to end where its bits
end: fewer than 8 bits left (a negative `count` wraps to large as `unsigned int`, one compare for
both), every byte read, and the padding bits zero.

This strictness is the only damage check there is: zram stores no checksum
([why so strict](book:#ch-format--end)). Why no input can make the decoder read or write out of bounds
is [a panel under the file's intro](#ch-decode--safety).
@more {#ch-decode--last-copy} Why the last copy needs no guard
The copy fills the page and uses every literal. A page with deltas restores them as any sequence's
literals, the last sequence being no different in the format. `undelta_literals()` gets
`lit_end - lit` with no guard, as on both ways to `last_literals` that count is the room left in the
page, `d_end - d`, and `lit` is at most `lit_end`:

- A page of literals only jumps there with `d` at `dst` and `lit_end - lit` equal to
  `SEQLZ_BLOCK_SIZE`.
- The `break` comes where `d - lit` is `end_gap`, which is `d_end - d` equal to `lit_end - lit`, right
  after the slow path checked `lit <= lit_end` and `nl <= lit_end - lit` before it moved `lit` on by
  `nl`.

Until 22nd October 2026 the call had a guard `lit <= lit_end` and the `min()` of the two counts, which
changed nothing on either way (commit `753a707`). A `memcpy()` here, a call instead of a second inlined
copy loop, made the swap-in 0.02 µs slower in the kernel VM, at the edge of what the boots differ by
([explored-designs.md](https://github.com/martinus/seqlz/blob/main/docs/explored-designs.md#the-last-sequence-without-a-token-12-bytes-per-page-less-kept)).
@end
@more {#ch-decode--end} The end checks, and what each one rejects
| check | rejects |
| --- | --- |
| `(unsigned int)br.count >= 8U`, `count` negative | a page cut short: the reader filled in zeros |
| `(unsigned int)br.count >= 8U`, `count` 8 or more | a whole byte after the last code, also a zero byte |
| `br.p != br.end` | bytes of the bitstream never read |
| `br.end[-1] & ((1U << br.count) - 1U)` | a bit 1 in the padding of the last byte |

For `count` 0 the mask is 0. Until 23rd October 2026 the checks of `count` were three compares
(commit `3ae2e09`).

A header with more literals than the sequences use fails before, in a check of room. The padding is read
from `br.end[-1]` and not from the register, which would be a shift by `64 - count`, 64 for
`count = 0`. The checks came on 8th October 2026; the compressor always wrote pages that pass.
@end

## seqlz_decompress()

@@ 624-643
The entry point. Deltas exist only with coded literals, flagged in bit 7 of byte 2; with raw literals
byte 2 is a literal. Byte 2 is read only if the page has one, `src_len > 2`. Each call passes a
constant to `decode_block()`, which gives the two copies of the loop
([what each copy does](#ch-decode--deltas)).

It is the kernel's `seqlz_decompress()` itself, declared in [`linux/seqlz.h`](linux-seqlz.h.html#b65):
`wrkmem`, the kernel's name for work memory, is the buffer of coded literals, `SEQLZ_SCRATCH` bytes,
and the `static_assert()` below checks that the interface's `SEQLZ_MEM_DECOMPRESS` is that size. The
export makes it callable from a module, zram's backend among them
([`EXPORT_SYMBOL_GPL`](book:#ch-zram-glue--api)); `MODULE_LICENSE()` and `MODULE_DESCRIPTION()` are the
module's, once for the whole module. Outside the kernel [`linux/export.h`](book:#ch-linux) and `linux/module.h`
make them declarations that do nothing.
