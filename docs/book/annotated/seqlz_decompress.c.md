# src/seqlz_decompress.c

The decoder of one page. A compressed page is a 2-byte header (the number of literals and a flag for
coded literals), the literals, and a bitstream with one token per sequence: a sequence is "copy `nl`
literals, then copy `len` bytes from `off` bytes back" ([the layout](book:#ch-format--layout)). The
coded literals are decoded in another file, `seqlz_decompress_literals.c`
([chapter 13](book:#ch-literals-dec)).

C wants a helper before its caller, so the file reads bottom up. A reading order that follows the
call:

1. [`seqlz_decode()`](#b601), the only function zram calls: it picks one of two copies of the loop.
2. [`decode_block()`](#b392): the header, the literals, and the three limits of the fast path.
3. The loop: [escape](#b485), [offset](#b501), [fast path](#b513), [slow path](#b547), [end checks](#b581).
4. The helpers, as the loop reaches them: [`next_token_fast()`](#b360),
   [`copy_match_fast()`](#b303), [`copy_literals()`](#b31), [`copy_match()`](#b89), the
   [bit reader](#b148).

The step-through under [the loop](#b485) runs the real `decode_block()` on two pages, one statement
at a time.

## Copies

@@ 1-6
The whole file has one function that is not `static`, `seqlz_decode()` at the bottom. Everything else
is inlined into it.

@@ 7-30
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

@@ 31-61
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
the scratch, which has 16 spare bytes for this. The caller, the slow path of `seqlz_decode()`, has
checked that the `nl` literals are there and fit into the page.
@end

@@ 62-67
For an offset of 1 to 7, how far on the same 8 bytes of a repeated pattern fit again: the largest
multiple of `off` up to 8. Offset 3 repeats `abc`, and `abcabcab` fits again 6 bytes on. Entry 0 is a
placeholder, offset 0 is rejected before.

@@ 68-88
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

@@ 89-124
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
end copies all of the match. The comment on lines 112 to 115 bounds the load of `repeat_pattern()`: it
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

@@ 125-147
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

@@ 148-163
@anchor ch-dec-basics--bit-reader
After the literals comes the bitstream: per sequence the token's code, the offset bits and the length
values, one after the other without regard to bytes ([the bitstream](#ch-format--bitstream)).

`bits` is left-aligned: the next bit of the stream is bit 63, so the next `n` bits are
`bits >> (64 - n)`, and a code indexes its table with the top bits directly. `count` says how many of
the top bits are real.

`count` is an `int` on purpose. Reading past the end gives zeros and makes `count` negative, and one
check at [the end of the page](#b581) catches that, instead of a check on every read. Wrong bits can
make wrong lengths and offsets, never a wrong memory access: each one is checked where it is used.

@@ 164-175
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
bytes that went in whole, and `count` has to grow by 8 for each of them. The fast path of
[chapter 14](#ch-decode--next-token) calls this half alone, where it knows that 8 bytes are there.

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

@@ 176-187
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
`lit_load_tail()` does for the literal streams ([chapter 13](#ch-literals-dec--refill))? Because the end
of `decode_block()` rejects a page unless every byte of the bitstream was read, `br.p == br.end`, fewer
than 8 bits are left, and those are 0 ([chapter 14](#ch-decode--end)). A load with zeros behind it
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

@@ 188-194
`(int)n` keeps the subtraction signed, so `count` can go negative. The largest drop is a token's code
with its offset bits, 11 + 14 bits, never 64.

@@ 195-215
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
([chapter 14](#ch-decode--slow)).

The value is sent as a symbol's code and extra bits ([chapter 4](#ch-format--length-values)). Values 0
to 15 are their own symbol, with no extra bits. From 16 on, the symbol says where the value's top bit
is, and the bits below that top bit follow the code; the top bit itself is not sent, the symbol says it.
One lookup with the next 8 bits gives three things packed into one entry, `code length | extra bits << 4
| base << 8` ([chapter 7](#ch-internal--value-table)): the code's length `n`, the number of extra bits
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

`seqlz_decode()` adds the token's {{ch12.ml_tok}}, and the match is {{ch12.ab.match}} bytes, as the
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

@@ 216-236
Adds 4 or 8 bytes to the bytes `dist` before them, each byte on its own, modulo 256, in one word.
The low 7 bits of each byte add without a carry into the next byte (at most `0x7f + 0x7f`); bit 7
is then fixed with an XOR of both top bits. A plain word addition would carry from one byte into the
next.

`add8()` only for a distance of 8 or more: with distance 4 its two loads overlap the bytes it writes.

@@ 237-248
How far back the byte is that a literal is a delta to: 4, 8, or derived from the offset of the
sequence before, at least 4 ([chapter 4](book:#ch-format--deltas)).

@@ 249-277
Level 3 stores some literals as the difference to an earlier byte of the page. They can only be turned
back in the page, right after the copy and before the match after them, because that match may copy
from them.

`k` starts at `dist - at` near the page's start: literals before byte `dist` have nothing to be a delta
to. Then whole words forward, each reading `dist` bytes back, which are final by then. The index `k`
and not a pointer, because `d + dist` could be past the page, and C does not allow forming that
pointer.

@@ 278-302
The same for the fast path, which copied 16 bytes whatever `nl` is: all 16 bytes, no loop, no branch
on `nl`. The bytes past `nl` become garbage, which they were already; the match writes over them.
Near the page's start it falls back to `undelta_literals()`.

## Helpers of the fast path

@@ 303-326
A fast match has at most 34 bytes, and the caller guarantees 40 bytes of room, so the copy needs no
loop: 16, 32 or 40 bytes in `copy8()`s. With `8 <= off < 16` a load reads what the store before
wrote, as the overlap needs.

`m = d - off` once, because clang computed `d + 8 - off` again for every load. Always copying 40
bytes, without the branches on `len`, was slower: the extra loads read bytes the stores just before
them wrote.

@@ 327-344
Below offset 8: the pattern register, stored 5 times without a loop. The smallest step is 5, so 5
stores cover at least 28 bytes, with step 8 they cover 40, every fast match. The loop runs only for a
longer match with a step of 5, 6 or 7. The caller has rejected offset 0, so `repeat_step[off]` never
reads the placeholder entry 0.

@@ 345-359
The token: the top 11 bits index a table of 2048 entries. Every index is valid because the token code
is complete. The entry has the lengths and the offset's class, so the offset's bits follow without a
second lookup.

It refills only when fewer than 11 + 12 bits are left (a code and the longest offset), so most lookups
use bits that are already in the register and do not wait for a load. Used for the first sequence and
after a slow one.

@@ 360-382
Two tricks, both for a page the CPU decodes once (a swap-in):

- The next token is looked up before this sequence's copies. No length value follows on the fast path,
  so the next code starts right after the offset bits. An in-order core such as the Cortex-A55 copies
  while the lookup is on its way ([in-order cores](book:#ch-cpu--in-order)).
- The refill comes every second fast sequence, without checking the bits left. Whether the bits run
  short depends on the codes before, a branch the predictor cannot learn on a page it has not seen; a
  branch that alternates it gets right. Two fast sequences take at most 2 × (11 + 14) = 50 bits, and a
  refill leaves 56.

@@ 383-391
The three limits of the fast path. In the page: it writes at most 14 literal bytes and 40 bytes of
match, rounded up to 64. In the literals: one `copy16()`. In the input: one refill of 8 bytes. With
these, the fast path checks nothing else.

## decode_block(): the loop

@@ 392-404
The room of the block above, checked by the compiler. A sequence takes the fast path only with `ll`
and `ml` below their caps, so it has at most {{ch14.fast_nl}} literals and a match of at most
{{ch14.fast_len}} bytes. `copy_match_fast()` writes 40 bytes with its five stores, or `len + 7` with its
loop, for a longer match at an offset below 8: {{ch14.fast_nl}} + 40 and {{ch14.fast_nl}} +
{{ch14.fast_len}} + 7 bytes, both within the 64. A change of the caps that lets the fast path write
past its room stops the build, instead of waiting for a fuzzer to find it.

@@ 405-413
`deltas` is a constant at both calls in `seqlz_decode()`, so the compiler makes two copies of the loop,
and a page without deltas runs none of their code. A check of `deltas` for every sequence made levels
1 and 2 slower to read. The cost is code size.

@@ 414-423
`last = 1` is the format's starting repeat offset ([offsets](book:#ch-format--offsets)). `skip = 0`
has the first fast sequence refill. `lit_bound` is where reading has to stop, behind `lit_end`.

@@ 424-436
The tables are prefetched before the header is read, so that their cache misses overlap with each
other and with the work on the header and the literals. An in-order core stops at every miss it did not
see coming.

The header's low 15 bits are the number of literals; more than a page is invalid, raw or coded.
@anim misses A model of 8 cache misses, one after another or overlapped

@@ 437-460
Bit 15 of the header picks the layout. Coded literals are decoded into the scratch first
([chapter 13](book:#ch-literals-dec)), which returns where the bitstream starts. It also gets `d`: a page
with a literal table of its own has that table built in the output page, before the loop overwrites
it ([own table](book:#ch-literals-dec--own)).

Raw literals are right after the header, with the bitstream behind them. Reading the bitstream as
literals is allowed, it is inside the input, so `lit_bound` is the end of the input.
@anim abbits The 9-byte example page, one field per step

@@ 461-473
The fast path copies without checking the room, so it is taken only where there is enough: three
compares against these limits. They are numbers and not pointers, because for a short input
`s_end - 8` would point before the buffer, which C does not allow. As numbers they are then below `lit`
and `br.p`, and the fast path is not taken.

@@ 474-484
The format has no count of sequences, and the last one has no token: the page ends when the literals
left fill the bytes left. `d_end - d == lit_end - lit` is the same as `d - lit == d_end - lit_end`,
whose right side never changes. So the loop compares two pointers it moves anyway against `end_gap`
([the end](book:#ch-format--end)).

A page of only literals has no token at all and jumps to the end.

@@ 485-500
Rare tokens have no code of their own: the escape's code, then the token's number in 12 bits. The
escape's entry has bit 31 set. After the refill and the 12 bits, `token_entry(idx, 0)` builds the entry
the table would have had, with a code length of 0 because the bits are dropped already. From here on an
escaped token is like any other ([token entry](book:#ch-internal--token-entry)).
@stepper decode The real decode_block() on two pages, statement by statement

@@ 501-512
The offset without a branch, from the token's entry: `<< TOK_N` shifts the code out, `>> back` brings
the offset bits down, `<< TOK_SHIFT` scales them (by 2 or 8 for some classes). Class 0 is the repeat
offset: it has no bits, `raw` is meaningless, and the select takes `last`. gcc makes the select a
`cmov`.

`TOK_LL` and `TOK_ML` are the lengths as they are, the match length already with its minimum of 4.

@@ 513-546
Most sequences go here. The condition: no length value follows (one bit of the entry), and the three
limits. The body: the next token first, 16 bytes of literals, the offset check, the match.

The offset check `off - 1U >= d - dst` is one unsigned compare for `1 <= off <= d - dst`: `off = 0`
wraps to the largest `unsigned int`.

`nl` is not checked against the literals left. On a damaged page `lit` can run past `lit_end`, but it
stays inside `lit_bound`, and the slow path, where every page ends, rejects it. The page's last match
never takes the fast path: fewer than 8 bits are left after it, and the fast path wants 8 bytes.

@@ 547-558
The slow path, with every check. `nl == 15` means a length value follows.

The order of the checks matters: `lit > lit_end` first, else `lit_end - lit` wraps and any `nl`
passes. The first fast path in September 2026 missed exactly this, and a test builds such a page since
then.

@@ 559-580
A match length of 35 in the token means a length value follows. With it `len` can be longer than a
page, which the check of room catches. `last` is set before the checks; when they fail it no longer
matters.

When the literals left fill the bytes left, no match can follow (a match has at least 4 bytes): the
loop ends. Otherwise the next token comes through the checked `next_token()`, and `skip = 0` restarts
the fast path's refill schedule.

@@ 581-600
The last sequence: the literals left, exactly the room left. Then the page has to end where its bits
end: fewer than 8 bits left (a negative `count` wraps to large as `unsigned int`, one compare for
both), every byte read, and the padding bits zero.

This strictness is the only damage check there is: zram stores no checksum
([why so strict](book:#ch-format--end)). Why no input can make the decoder read or write out of bounds
is [one list in the book](book:#ch-decode--safety).

## seqlz_decode()

@@ 601-612
The entry point. Deltas exist only with coded literals, flagged in bit 7 of byte 2; with raw literals
byte 2 is a literal. Each call passes a constant to `decode_block()`, which gives the two copies of
the loop.
