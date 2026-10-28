# src/seqlz_compress_literals.c

The compressor's literals. After `seqlz_compress.c` has written a page with raw literals, level 2
prices them in all 8 fixed literal tables, takes the cheapest, and codes them in 8 streams where that
saves enough. Level 3 can also store them as deltas to earlier bytes first, and can bring a table of
the page's own ([the format](book:#ch-format--coded)). The coded literals are written over the raw
ones in place, from a copy in the matcher's hash table, so they cost no work memory.

A reading order that follows the call:

1. The entry points: [`seqlz_code_literals()`](#b889) at level 2, [`seqlz_code_own()`](#b875) at
   level 3. Both are [`code_literals()`](#b736), compiled for their arguments.
2. [`lit_sums()`](#b271) prices all 8 tables with one add per literal, then
   [the cheapest table](#b783), and at level 3 [a table of the page's own](#b801).
3. [The rule](#b821) decides whether coding pays, then [the streams are written](#b823) by
   [`lit_streams()`](#b310).
4. Level 3's deltas come earlier, right after matching: [`seqlz_delta_literals()`](#b143), which
   prices them in [`pick_delta()`](#b77).

## Deltas, level 3

@@ 1-8
Three ways to store the literals: raw, coded with a fixed table, or coded with a table of the page's
own; at level 3 optionally as deltas first.

@@ 9-16
The deltas are priced on every 5th literal only. Every literal made pages almost no smaller and the
encoder slower; every 4th made them larger, because in fields of 4 or 8 bytes it sees the same byte of
each field every time.

@@ 17-28
Reads one sum out of its 16-bit lane: `even` has tables 0, 2, 4 and 6, `odd` tables 1, 3, 5 and 7,
4 lanes each. `k / 2` is the lane.

@@ 29-52
The sums are counted in 8-bit lanes, 8 tables in one `u64`, and moved into 16-bit lanes before an 8-bit
lane can overflow. `x & lanes` keeps the even bytes in place, `x >> 8 & lanes` moves the odd ones down
by a byte.

@@ 53-63
8 byte subtractions in one word, without a borrow from one byte into the next. Setting bit 7 of every
byte of `a` and clearing it in `b` stops every borrow at bit 7 of its own byte; the XOR then puts the
right bit 7 back ([sub_bytes()](book:#ch-literals-enc--sub-bytes)). The decoder's `add8()` is the same
trick for an addition.

@@ 64-76
A record, 3 `u16` that `encode_seq()` wrote before each sequence's literals: where they start in the
page, how many there are, and the offset before them, which gives the distance of `SEQLZ_DELTA_LAST`.

@@ 77-99
Which delta to use, priced like the tables: the literals as they are and with each of the 3 deltas,
in all 8 fixed tables at once, 4 sums. A delta pays 8 bits for its header byte, scaled to the 1 in 5
literals priced.

@@ 100-128
For each priced literal: the byte, and the bytes 4, 8 and `last` back where the literal is that far
into the page (else 0, the format's one rule for all three). The cast to `u8` is the difference modulo
256, and keeps the index inside `lit_cost`.

`j` carries the step over to the next sequence, so the priced literals are every 5th of the page, not
of each sequence.

@@ 129-142
The cheapest of 4 × 8 sums wins. A tie keeps the lower number, so no delta wins over a delta that saves
nothing.

@@ 143-158
Two passes over the records: `pick_delta()` prices, this function moves the literals down over the
records and writes their deltas into `deltas`, the matcher's state.

@@ 159-189
The literals move down by 6 bytes per record so far, 8 bytes at a time, also up to 7 bytes past a
sequence's last literal: the next sequence's literals go over those. `memcpy()` was a call per
sequence.

The next record is read before the copy, since only the first sequence's copy reaches into it. A load
before the store makes the overlapping forward copy safe.

@@ 190-216
The deltas, 8 at a time with `sub_bytes()`. Literals closer to the page's start than the distance stay
as they are. The encoder subtracts bytes of the page as it is, so a distance of 4 is no problem for a
word of 8; the decoder, which has only what it restored so far, needs words of 4 there.

## Codes into streams

@@ 217-232
One literal's code into a stream's accumulator. `*sum += e` adds the whole entry, code and length: the
low byte of the sum is then the sum of the lengths, exact below 256, which 4 codes of at most 10 bits
are. One mask per flush instead of one per literal.

@@ 233-246
The last bytes of a stream, where an 8-byte store would write into the next stream.

@@ 247-270
The streams' version of `enc_flush()`, but forwards: the bits on top, a byte swap and a
little-endian store make a big-endian store, so the stream's first bit is the top bit of its first
byte. One swap before the branch: in each branch, the compiler did not merge them.

## Pricing all 8 tables

@@ 271-309
The pricing: `lit_cost[b]` has byte `b`'s code length in table `j` in byte lane `j`, so one 64-bit add
sums all 8 tables, SWAR. The kernel may not use vector registers here.

25 literals per stream go into the 8-bit lanes (25 × 10 bits ≤ 255), then into the 16-bit lanes. The 8
sums per table are also the 8 streams' sizes, so no second pass. At level 3 the same pass counts the
bytes, even and odd literals in two histograms, so that a run of one byte does not wait for its own
count.
@anim pricing The sums of stream 0, one add per literal, and their move into the 16-bit lanes

## Four streams at a time

@@ 310-333
One stream is one chain: each code waits for the shift before. 4 accumulators run 4 chains side by
side; one stream after the other cost about 3.5 cycles per literal. Why 4 and not 8 is not written
down: 8 streams of 5 live values each would be 40, far more than 16 registers.

@@ 334-350
A run: 4 literals per stream of this half, then a flush. 4 codes of 10 bits and 7 bits left over fit
the accumulator. The loop stops early enough that at most 4 literals per stream are left for below.

@@ 351-381
The rest, at most 4 literals per stream: one loop with a switch, skipping the other half's literals
(`k += 4` after each group of 4), then the last flushes.

@@ 382-399
The 8 stream sizes in `width` bits each, lowest bit first, exactly `width` bytes.

## A table of the page's own

@@ 400-438
The work memory of a page's own table, at most `OWN_WORK`, 4 KiB, lives in the matcher's 8 KiB state
behind the copy of the literals, so it costs nothing extra. Only pages with 512 literals or more try it, and it has to
save 16 bytes against the best fixed table.

The description takes at most 256 bytes; its room is 512, a power of 2, so `own_put()` can mask its
index and the compiler sees the bound.

@@ 439-481
Code lengths close to Huffman without sorting. Each byte first gets the shortest `l` with
`f * 2^l >= n`, which keeps the code within its space except where lengths are cut to 10 bits.

`f * 2^l / n`, 1 to 2, is how much a byte saves per code space if its code were one bit shorter, the
"gain". A counting sort into 16 buckets by gain gives the order for the repair below, with a multiply
by `inv` instead of a division.
@anim ownlens own_lengths() on two real pages, bucket by bucket

@@ 482-506
The repair: an overfull code lengthens the bytes of the smallest gain first, then room left goes to
the bytes of the largest gain. The result can be a few bits worse than a real Huffman code; the
buckets save the sort and the tree.

@@ 507-521
`log2()` in 1/256 bits, to about 1/50 bit, with a table of 32 bytes and no division: the top bit's
position is the integer part, the next 5 bits index the fraction.

@@ 522-527
4 counts as the 16-bit lanes of one `u64`, for the presence test below.

@@ 528-543
Bits into the description, forwards: once 32 bits wait, 8 bytes go out big-endian and the position
moves on by 4; the other 4 bytes are written again by the next store.

@@ 544-583
The description: for each byte with a code, in order of value, the run of bytes without a code before
it, then its code length, as symbols of the fixed code `OWN` ([the format](book:#ch-format--own)).
`prev` starts at `~0U`, so the first run is from byte 0.

@@ 584-626
Adds the even and odd counts, 4 at a time, and marks which bytes occur, a bit per byte value, without
a branch: the top bit of each 16-bit lane is set where the lane is not 0, and a multiply gathers the 4
top bits into 4 adjacent bits.

@@ 627-643
The bytes that occur, in order of value (lowest set bit, then clear it), and on the way the sum of
`f * log2(f)` for the entropy.

@@ 644-658
The cheap test before any code is built: `n log2 n - sum f log2 f` is the entropy, the bits no prefix
code can beat, plus about 0.45 bytes of description per byte. If that cannot save 16 bytes, or the
page would stay raw anyway, most pages leave here.

@@ 659-671
Canonical codes from the lengths, as for the fixed tables, and on the way the exact bits of all
literals. `enc[]` has the layout of the fixed tables' `enc[]`, so `lit_streams()` takes either.

@@ 672-680
The second test, with the real description and bits: only a page that passes it pays for
`own_sizes()`, one more pass over the literals.

@@ 681-722
The bytes of each stream with the page's own code. Eight named sums, not `sizes[k % 8]`: each is a
chain of its own, and the compiler keeps them in registers.

@@ 723-735
The width of the stream sizes from one count of leading zeros of their OR. Setting bit 4 makes the
width at least 5 and keeps the OR from being 0, for which `__builtin_clz()` is undefined.

## code_literals()

@@ 736-782
The coder. It is `__always_inline`, so each entry point gets its own copy: level 2's has `own` the
constant `NULL` and no code for a page's own table at all, 2% fewer instructions per page.

The raw literals are read from a copy, so the coded ones can go where the raw ones were and `dst` needs
no room beyond `len`.

@@ 783-800
The table with the fewest bits; a tie keeps the lower number. Each stream's size is rounded up to
whole bytes on its own, and the header counts the delta's byte if there is one.

@@ 801-820
Level 3: a page's own table, if it and its description take at least 16 bytes less than the best fixed
table. Then the page is written as for a fixed table, with the description in the header.

@@ 821-822
The rule: coded only where that saves 1/16 of the literals plus 51 bytes. zsmalloc's size classes are
16 bytes apart, so a few bytes less often save nothing, while every read of a page with coded literals
pays a fixed time for the literal table and the decode ([the rule](book:#ch-literals-enc--rule)).

@@ 823-834
Only now, as the page gets coded, the raw literals are copied into `spare`. The streams start right
after the header, each where the one before ends.

The streams end before the bitstream starts: the rule guarantees that, together with the
`static_assert` below.
@anim inplace dst while the streams are written over the raw literals

@@ 835-855
The u16 with the coded flag, byte 2 (table, width, delta flag), the delta's byte, the sizes and the
description; then the bitstream moves down to right after the coded literals.

@@ 856-868
The arguments of this file, checked by the compiler: the coded literals end before the bitstream, the
raw literals' copy fits the matcher's table, and the sums fit their lanes.

## Entry points

@@ 869-874
`struct own_work` lives in the matcher's state behind the copy of the literals.

@@ 875-888
Level 3: with a delta, the deltas in the matcher's state are coded; without, the literals in `dst`. If
the deltas stay raw, the literals as they are are not tried again: that coded 1.4% of its tries for
0.71 bytes per page, and cost 0.39 µs per write on the Mi 9T's Cortex-A76.

@@ 889-898
Levels below 3: fixed tables only, the matcher's table as the copy.

@@ 899-906
The same for `seqlz_encode()`, whose caller has the raw literals, so no copy is needed. Not in the
kernel copy.
