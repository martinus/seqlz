# src/seqlz_format.h

The format as constants, plus two small functions that turn a sequence's numbers into what the format
stores: `seqlz_len_symbol()` and `seqlz_off_class()`. Every file of the codec includes it. The long
comment at the top is the format short enough to keep in your head; [chapter 4](book:#ch-format) has
it in words and `docs/format.md` is the specification. The notes here say why each number has its
value.

A reading order:

1. [The comment at the top](#b7): sequences, the token, offset classes, length values, the block's
   layout.
2. [The table sizes](#b72) and [the token's fields](#b84): what the longest codes cost a cold read.
3. [The escape](#b100), for the tokens without a code.
4. [`seqlz_off_class()`](#b195), the class of an offset without a branch, and
   [the nibble constants](#b175) it reads.
5. Coded literals: [tables and streams](#b218), [the header](#b234), [deltas](#b256),
   [a page's own table](#b273).

## The format in one comment

@@ 1-6
@anchor ch-header
In the kernel's copy this `#include` becomes `linux/types.h` and nothing else: a program that runs on
the build machine and writes the tables includes this file too, and may only use the few headers the
kernel has for such programs ([at build time](book:#ch-codes--kernel)).
@more {#ch-header--summary} The kernel's copy: four headers, and no `static_assert()` here
The kernel's copy has four headers: `include/linux/seqlz.h`, the interface zram calls, which
`port.py` writes from `tools/kernel-port/seqlz.h`, and in `lib/seqlz/` this file, `seqlz_tables.h`
and `seqlz_internal.h`. The guards there are `_LIB_SEQLZ_SEQLZ_FORMAT_H` and so on, after the files'
place.

This file has no `static_assert()`. Until 24th October 2026 it still brought `linux/build_bug.h` to
the files that have one (commit `32a6c7a`); now it includes only what the program for the build
machine may include.
@end

@@ 7-27
A sequence is "copy `ll` literals, then copy `ml` bytes from `offset` bytes back". The token packs
all three into one Huffman symbol so that one table lookup in the decoder gives all three. E.g. 1
literal, a match of 7 at the repeat offset (class 0) is token `1 + 16 * 3 + 0 = 49`, the typical
sequence of an array of pointers. The formula in line 24 is exactly what
[`seqlz_token()`](seqlz_internal.h.html#b72) computes.

"1 to the bytes before it": a match can reach back to the block's first byte and no further, so 4095
is the largest offset in a 4 KiB block, which is what class 3's 12 bits hold
([the token](book:#ch-format--token)).

@@ 28-39
Class 0 is the repeat offset: the same offset as the sequence before, sent with no bits at all. Class 1
spends a bit that is always 1, which looks wasteful. Without it the decoder would need a special case;
with it every class's offset comes out of the same shift and mask
([token entry](book:#ch-internal--token-entry)).

Classes 4 and 5 exist because memory pages are full of 8-byte fields: offset 72 is sent as `72 / 8 =
9` in 5 bits, instead of 8 bits in class 2 ([offset classes](book:#ch-format--offsets)). On the zram
pages of a desktop, 64% of the offsets from 16 to 255 that are not the repeat offset are multiples of
8, and 52% of those from 256 on
([`docs/explored-designs.md`](https://github.com/martinus/seqlz/blob/main/docs/explored-designs.md#seqlz-fast-lit-by-the-score-no-budget-offsets-in-steps-of-8)).

@@ 40-51
A length value of 16 or more is sent as "where its highest bit is", as a symbol, then the bits below
that bit. E.g. 100 is `0b1100100`: the highest bit is bit 6, so symbol 18, then the 6 bits `100100`.
The highest bit itself is not sent, the symbol says where it is.

A block does not store how many sequences it has. The decoder stops when the literals left are exactly
the bytes left to fill: a match has at least 4 bytes, so no match can follow
([how a page ends](book:#ch-format--end)).

@@ 52-61
The `u16` is what lets the decoder read two places at once: the literals start at byte 2, the
bitstream at byte `2 + n` for `n` raw literals ([the layout](book:#ch-format--layout)). A 16-byte copy
of the last literals may read into the bitstream, which is harmless: it is still inside the input.

In the kernel's copy the last line points at `Documentation/staging/seqlz.rst`, which `port.py` writes
from `docs/format.md`: the kernel's copy may not name the repository.
@anim abbits The 9-byte example page, one field per step

## The block size and the token

@@ 62-71
`SEQLZ_PAGE_BITS` is the codec's one build setting: 12 for 4 KiB pages, 14 for 16 KiB, passed as
`-DSEQLZ_PAGE_BITS=14`. The kernel's copy fixes `SEQLZ_BLOCK_BITS` at `12U` whatever the kernel's page
size, because the format for 16 KiB blocks is not fixed yet
([`docs/format.md`](https://github.com/martinus/seqlz/blob/main/docs/format.md#status)).

@@ 72-83
The longest code sets the size of the decode table: the decoder looks up the next `L` bits in a table
of `2^L` entries ([table decoding](book:#ch-compression--table-decoding)). 11 bits are 2048 entries of
4 bytes, 8 KiB, 128 cache lines of 64 bytes. A **cold** read, of a page swapped out a while ago, finds
none of them in the cache and pays for each line it touches ([cache](book:#ch-cpu--cache)).

With 10 bits fewer tokens get a code of their own and more take the escape, so blocks get larger. The
smaller table paid off only on the Mi 9T's in-order Cortex-A55; on its A76 and in the kernel VM on
x86-64 it did not, so 11 stays.
@more {#ch-header--token} What 10-bit tokens and 9-bit length values cost, measured
The length values' tables follow the same rule: 8 bits are 256 entries of 4 bytes, 1 KiB. Codes of
length values were once allowed 9 bits, and a cold read took about 80 ns longer on the PC
([`docs/explored-designs.md`](https://github.com/martinus/seqlz/blob/main/docs/explored-designs.md#seqlz-third-decoder-round-one-repeat-offset-offset-class-in-the-token-and-why-cold-is-slow),
the row "length values with codes of at most 8 bits").
@assert ch06.MAX_BITS == 8 and ch06.TOKEN_BITS == 11

The token table is the larger trade. With 10 bits it would be 4 KiB, but fewer tokens get a code of
their own, more take the escape and a second lookup, and pages get larger. It was measured last on
11th October 2026, with a decoder that prefetches the token table on every core
([`prefetch_lines()`](#ch-dec-basics--prefetch)), so 10 bits meant 64 cache lines less to load on
every core
([`docs/explored-designs.md`](https://github.com/martinus/seqlz/blob/main/docs/explored-designs.md#the-token-table-in-10-bits-again-the-a55s-cold-reads-13-µs-faster-x86-64-2-to-4-slower-not-kept)).
Pages were 3.5 bytes larger on average. On the Mi 9T's A55, the little in-order core, cold reads of
pages with coded literals were 1.3 µs faster, about 0.9 µs less per page written with 0.73 reads per
write. On the A76, the big out-of-order core, the cold reads did not change and the warm reads were
0.16 µs slower; in the kernel VM on x86-64 swap-ins were 2% to 4% slower.

On 5th October 10 bits had read cold 2.0 to 2.7 µs faster on the A76 too
([item 1](https://github.com/martinus/seqlz/blob/main/docs/explored-designs.md#1-the-token-table-in-10-bits-cold-reads-2-to-3-µs-faster-on-both-cores-for-3-to-6-bytes)).
That gain went when the bitstream became most significant bit first: a code's entries now sit next to
each other, and a page touches fewer lines of the table anyway ([chapter 3](#ch-cpu--msb-first)). So
10 bits pays only on the in-order core, and 11 bits stay.
@end

@@ 84-99
The token holds `ll` in bits 0 to 3, `ml - 4` in bits 4 to 8, the class in bits 9 to 11, as
[the token's drawing](book:#ch-format--token) shows for token 2098: `SEQLZ_TOKEN_SYMBOLS` is `6 << 9`,
3072. Larger values need a length value after the token, which is a second lookup in another table.

The two widths cannot grow: the decoder's token entry stores `ml` as it is, the cap plus 4, in 6 bits.
`31 + 4 = 35` fits 6 bits; with 6 bits for `ml - 4` it would be `63 + 4 = 67`, which does not
([token entry](book:#ch-internal--token-entry)).
@more {#ch-header--token-bits} Why 5 bits for `ml - 4`, not 4
The fewer sequences need a length value, the fewer second lookups. With 4 bits for `ml - 4` a match
length value followed 18.7% of the matches, with 5 bits 8.5%, at the same memory, and the decoder went
from 9400 to 9180 cycles per page on the PC, measured before the token had an offset class
([`docs/explored-designs.md`](https://github.com/martinus/seqlz/blob/main/docs/explored-designs.md#second-round-branches-tails-and-table-sizes),
"Rarer escapes: a token of 512 symbols").
@end
@more {#ch-header--classes} Why 6 offset classes, and not 7 or 8
"Two more save little": `tools/seqlz-bound/space.py` priced layouts of 6 to 8 classes, each a number
of bits and a divisor of 1, 2, 4 or 8, on 20 000 pages of each of seven dumps, the token codes of each
dump trained on the other six. Against the classes before 14th October 2026, today's 6 save 5.15 bytes
per page of tokens and offset bits, the mean of the seven dumps, and the best 7 save 6.24.

Offsets that need no bits save more, e.g. 2 and 8 as classes of their own, 8.46 bytes with 8 classes.
But then the decoder chooses from three for each offset, its bits, a fixed one or the repeat offset,
and with that one more select the Mi 9T's A55 wrote 1.0% and read warm pages 1.7% slower, its A76 read
warm pages 2.2% slower
([`docs/explored-designs.md`](https://github.com/martinus/seqlz/blob/main/docs/explored-designs.md#offset-2-in-1-bit-multiples-of-8-below-2048-in-8-03-to-08-smaller-at-the-same-speed-kept)).
@end

## The escape and the caps

@@ 100-123
A code of `l` bits uses `1 / 2^l` of the code space, and the sum may not exceed 1
([complete codes](book:#ch-compression--complete)). With at most 11 bits that is room for 2048 codes,
for 3072 tokens. So rare tokens are sent as the escape, symbol 3072, then their number in 12 plain
bits. The values 3072 to 4095 are no token, and the decoder rejects them.

`SEQLZ_MAX_ESCAPE_LEN` turns a promise of the encoder into a limit on the tables. The encoder's proof
that two pages always fit assumes a token and its offset take at most 31 bits. The longest pair is an
escaped token with a class 3 offset, so the escape's code may have `31 - 12 - 12 = 7` bits on 4 KiB
blocks. The table generator checks it ([all symbols](book:#ch-codes--all-symbols)).

The caps double as flags: `ll` 15 in a token means "15 or more", and `ll - 15` follows.
@more {#ch-header--escape} How many tokens have a code, and why not all of them
With the 4 KiB tables {{ch04.tok_coded}} tokens have a code of their own, with the 16 KiB tables
{{k16.ch04.tok_coded}}. All others are sent as the escape, symbol {{ch06.ESCAPE}}, followed by the
token in 12 plain bits, the fewest that hold 3071. The escape as a whole is frequent, with a
{{ch06.esc_len}}-bit code in both tables.
@assert ch06.esc_len == k16.ch06.esc_len

"A code for every token would take code space from the frequent ones" was measured when the token had
1536 symbols: with 11 bits each needs at least $\frac{1}{2048}$ of the code space, 75% of it together.
Pages took 27.5% of their size instead of the 26.6% the frequencies promised; with the escape and the
same 11-bit table, 26.8%
([`docs/explored-designs.md`](https://github.com/martinus/seqlz/blob/main/docs/explored-designs.md#seqlz-third-decoder-round-one-repeat-offset-offset-class-in-the-token-and-why-cold-is-slow)).

The 31 bits of `SEQLZ_SEQ_BITS` are what the encoder's `static_assert`s check
([why two pages fit](#ch-encoder--layout)). They leave the escape's code
$31 - 12 - 12 = {{ch06.MAX_ESCAPE_LEN}}$ bits on 4 KiB pages and {{k16.ch06.MAX_ESCAPE_LEN}} on 16 KiB
pages. With the trained {{ch06.esc_len}} bits the longest pair is {{ch06.esc_pair}} bits on 4 KiB
pages; any other token code is at most 11 bits, $11 + 12 = 23$.
@assert ch06.esc_len <= ch06.MAX_ESCAPE_LEN and k16.ch06.esc_len <= k16.ch06.MAX_ESCAPE_LEN and ch04.longest.tok == 11
@end

## Length values and the u16

@@ 124-144
`SEQLZ_LEN_LOG_BASE` is 12 so that the two kinds of symbols meet without a gap: symbol 16 is 16 to 31
(highest bit 4), 17 is 32 to 63, and the last on 4 KiB blocks, 23, is 2048 to 4095. That is 24
symbols.

The literal count can borrow the top bit of the `u16` because it never reaches it: at most 16384
literals, 15 bits. The decoder reads the count as the `u16` masked with `SEQLZ_LIT_CODED - 1U`
([`seqlz_decode()`](book:#ch-decode)).
@more {#ch-header--length-values} The largest length value, and a symbol that went
On 16 KiB pages the length values have {{k16.ch06.LEN_SYMBOLS}} symbols, against
{{ch06.LEN_SYMBOLS}} on 4 KiB pages.
@assert ch06.last_sym == 23 and ch06.LEN_SYMBOLS == 24

The largest length value of a 4 KiB page is `ll - 15` for 4092 literals before a match of 4 bytes,
{{ch06.max_ll_value}}, or `ml - 35` for a match of 4095 bytes after 1 literal, {{ch06.max_ml_value}}:
both symbol {{ch06.max_ll_sym}}, the last, as the comment says. Until 11th October 2026 there was one
symbol more, 4096 to 8191, which no valid page sent. As the encoder wants a code for every length
value symbol, the trained tables still gave it 8 bits, 1/256 of each table's code space; it went when
the tables were trained again ([#182](https://github.com/martinus/seqlz/issues/182)).
[The format](#ch-format--length-values) has why the symbol is not sent in 5 plain bits instead.
@assert ch06.max_ll_sym == ch06.max_ml_sym == ch06.last_sym and k16.ch06.max_ll_sym == k16.ch06.max_ml_sym == k16.ch06.last_sym

The literal count is at most 4096 on 4 KiB pages and 16384 on 16 KiB pages. In the kernel's copy,
which is for 4 KiB blocks, the comment of `SEQLZ_LIT_CODED` says "at most 4 KiB of literals".
@end

## The tables as code lengths

@@ 145-156
@anchor ch-header--lengths
A canonical Huffman code follows from the code lengths alone, so one byte per symbol is the whole
table: {{size.lengths}} bytes for the sequences on 4 KiB pages, {{k16.size.lengths}} on 16 KiB pages.
The decoder and encoder tables are computed from these at build time. Because the compiled-in
lengths, `seqlz_default_lengths` from `seqlz_default_tables.c`, are part of the format,
`docs/format.md` lists their SHA-256, so that a decoder written from the spec can check it has the
right ones. The 8 literal tables are `seqlz_lit_sets[][]`, declared in [`seqlz.h`](seqlz.h.html#b41).
@anim table From code lengths to a decode table and back

@@ 157-174
`31 - __builtin_clz(v)` is the position of the highest set bit. The builtin is undefined for 0, but
`v` is at least 16 on that path. For `v = 100` it gives 6: symbol `12 + 6 = 18`, with 6 extra bits.

gcc compiles it with one branch, for `v < 16`, and a `bsr`. That is fine here: the encoder calls it
only for the few sequences that have a length value.
@more {#ch-header--len-symbol} `seqlz_len_symbol()` on 100 and on 4059, bit by bit
`put_len_value()` calls it ([the encoder](#ch-encoder--put)) and writes the low `*extra_bits` bits of
`v` after the symbol's code. For `v = 100`, `{{ch06.lensym.100.bin}}`, the highest set bit is bit 6,
so the symbol is {{ch06.lensym.100.sym}}, and the 6 bits below it, {{ch06.lensym.100.low}}, follow.
The highest bit is not sent, the symbol says where it is.

The match of 4094 bytes in [the format's worked example](#ch-format--example) has
`ml - 35 = 4059`, symbol {{ch06.lensym.4059.sym}} and {{ch06.lensym.4059.extra}} plain bits.
@assert ch06.lensym[100].extra == 6 and ch04.ab.s[0].mlval == 4059 and ch04.ab.s[0].ml == 4094
@end

## Offset classes without a branch

@@ 175-194
Each class's number of offset bits is one nibble of one constant, read with a shift and a mask instead
of a `switch` or an array. For class 3, `0x85C810 >> 12` is `0x85C`, and `& 15` leaves `0xC`, 12.
When class 3's bits were computed from the block's bits in code, gcc branched on the class; a shift
and a mask cannot become a branch. `SEQLZ_SHIFT_NIBBLES` is the same trick for the shift, `0x330010`.

Class 5 stays at 8 bits, offsets below 2048, also on 16 KiB pages: 10 bits there made pages no smaller
on average.
@more {#ch-header--offsets} The six nibbles, and what the compiler makes of them
Class 5 holds the multiples of 8 below `8 << 8`, {{ch06.class5_end}}, on both page sizes. On 16 KiB
pages 10 bits could reach 8 KiB back, but that made the pages no smaller on average
([`docs/explored-designs.md`](https://github.com/martinus/seqlz/blob/main/docs/explored-designs.md#offset-2-in-1-bit-multiples-of-8-below-2048-in-8-03-to-08-smaller-at-the-same-speed-kept)).
@assert ch06.CLASS5_BITS == k16.ch06.CLASS5_BITS == 8 and ch06.class5_end == 2048

The encoder and the decoder need each class's number of offset bits, {{ch06.RAW_BITS}} on 4 KiB pages.
`SEQLZ_RAW_NIBBLES` holds them as one nibble per class:

```text
class:     5  4  3  2  1  0
nibble:    8  5  C  8  1  0     SEQLZ_RAW_NIBBLES = 0x85C810 on 4 KiB pages, 0x85E810 on 16 KiB
```
@assert ch06.RAW_NIBBLES == "0x85C810" and k16.ch06.RAW_NIBBLES == "0x85E810"

The block size goes into the fourth nibble as `SEQLZ_BLOCK_BITS << 12`, and the compiler folds the
whole expression into one constant: gcc's code for `seqlz_off_class()` loads
`${{ch06.RAW_NIBBLES_dec}}`. In `SEQLZ_SHIFT_NIBBLES`, `0x330010`, the shift is 1 for class 1 and 3
for classes 4 and 5. The token's decode table entry has 2 bits for that shift
([token entry](#ch-internal--token-entry)).
@assert ch06.OFF_SHIFT == k16.ch06.OFF_SHIFT == "0, 1, 0, 0, 3, 3"
@end

@@ 195-217
Read the sum from 3: minus 1 below 256, minus 1 more for offset 2, plus 2 for a multiple of 8 below
2048 (`aligned`). E.g. offset 72 is `3 - 1 - 0 + 2 = 4`, offset 2 is `3 - 1 - 1 + 0 = 1`, offset 399
is 3. Offset 2048 is a multiple of 8 but not below 2048, so class 3.

`is_new` is a mask: `(off == last) - 1U` is 0 for the repeat offset and all ones otherwise, so `&
is_new` makes the class 0 without an `if`. `&` instead of `&&` for `aligned`: `&&` may skip its right
side, and compilers often keep that as a branch. Arithmetic and not a branch, because the classes of
consecutive matches follow no pattern a branch predictor could learn
([branches](book:#ch-cpu--branches)). gcc compiles the function for x86-64 without a jump.
@more {#ch-header--off-class} Which offsets each class takes, and the sum for 2, 72 and 399
The figure shows which offsets each class takes, and the sum for three offsets of
[chapter 16](#ch-trace)'s page, 2, 72 and 399. The class then picks its nibble of
`SEQLZ_RAW_NIBBLES`, the number of offset bits.
@assert ch06.off[2].cls == 1 and ch06.off[72].cls == 4 and ch06.off[399].cls == 3 and ch06.off[8].cls == ch06.off[24].cls == 4 and ch06.off[255].cls == ch06.off[15].cls == 2 and ch06.off[256].cls == ch06.off[2040].cls == 5 and ch06.off[2048].cls == ch06.off[4095].cls == 3 and ch06.class5_end == 2048

<figure class="diagram">
<div class="scroll">
<svg viewBox="0 0 720 380" role="img" aria-label="The offsets of each class on an axis stretched below 256, then the class and the offset bits of the offsets 2, 72 and 399 computed as seqlz_off_class() does" font-size="12">
  <text x="10" y="10" font-size="11" class="mut">a new offset goes to</text>
  <line x1="430.0" y1="12" x2="430.0" y2="150" stroke="currentColor" stroke-width="0.8" stroke-dasharray="3 3" class="mut"/>
  <line x1="610.0" y1="12" x2="610.0" y2="150" stroke="currentColor" stroke-width="0.8" stroke-dasharray="3 3" class="mut"/>
  <text x="10" y="28">class 1, {{ch04.raw.1}} bit</text>
  <text x="10" y="54">class 2, {{ch04.raw.2}} bits</text>
  <text x="10" y="80">class 3, {{ch04.raw.3}} bits</text>
  <text x="10" y="106">class 4, {{ch04.raw.4}} bits</text>
  <text x="10" y="132">class 5, {{ch04.raw.5}} bits</text>
  <g class="acc"><circle cx="152.2" cy="24" r="4" fill="currentColor"/></g>
  <text x="162.2" y="28" font-size="11">only 2, sent as 2 &gt;&gt; 1</text>
  <rect x="151.1" y="43" width="278.9" height="14" class="fill-sheet" stroke="currentColor" stroke-width="0.8"/>
  <text x="438.0" y="54" font-size="11" class="halo">1 to 255, but 2 and the multiples of 8</text>
  <rect x="430.0" y="69" width="280.0" height="14" class="fill-sheet" stroke="currentColor" stroke-width="0.8"/>
  <text x="438.0" y="80" font-size="11">the rest from 256 to 4095</text>
  <g class="acc">
    <rect x="157.75" y="95" width="2" height="14" class="fill-acc"/>
    <rect x="166.50" y="95" width="2" height="14" class="fill-acc"/>
    <rect x="175.25" y="95" width="2" height="14" class="fill-acc"/>
    <rect x="184.00" y="95" width="2" height="14" class="fill-acc"/>
    <rect x="192.75" y="95" width="2" height="14" class="fill-acc"/>
    <rect x="201.50" y="95" width="2" height="14" class="fill-acc"/>
    <rect x="210.25" y="95" width="2" height="14" class="fill-acc"/>
    <rect x="219.00" y="95" width="2" height="14" class="fill-acc"/>
    <rect x="227.75" y="95" width="2" height="14" class="fill-acc"/>
    <rect x="236.50" y="95" width="2" height="14" class="fill-acc"/>
    <rect x="245.25" y="95" width="2" height="14" class="fill-acc"/>
    <rect x="254.00" y="95" width="2" height="14" class="fill-acc"/>
    <rect x="262.75" y="95" width="2" height="14" class="fill-acc"/>
    <rect x="271.50" y="95" width="2" height="14" class="fill-acc"/>
    <rect x="280.25" y="95" width="2" height="14" class="fill-acc"/>
    <rect x="289.00" y="95" width="2" height="14" class="fill-acc"/>
    <rect x="297.75" y="95" width="2" height="14" class="fill-acc"/>
    <rect x="306.50" y="95" width="2" height="14" class="fill-acc"/>
    <rect x="315.25" y="95" width="2" height="14" class="fill-acc"/>
    <rect x="324.00" y="95" width="2" height="14" class="fill-acc"/>
    <rect x="332.75" y="95" width="2" height="14" class="fill-acc"/>
    <rect x="341.50" y="95" width="2" height="14" class="fill-acc"/>
    <rect x="350.25" y="95" width="2" height="14" class="fill-acc"/>
    <rect x="359.00" y="95" width="2" height="14" class="fill-acc"/>
    <rect x="367.75" y="95" width="2" height="14" class="fill-acc"/>
    <rect x="376.50" y="95" width="2" height="14" class="fill-acc"/>
    <rect x="385.25" y="95" width="2" height="14" class="fill-acc"/>
    <rect x="394.00" y="95" width="2" height="14" class="fill-acc"/>
    <rect x="402.75" y="95" width="2" height="14" class="fill-acc"/>
    <rect x="411.50" y="95" width="2" height="14" class="fill-acc"/>
    <rect x="420.25" y="95" width="2" height="14" class="fill-acc"/>
  </g>
  <text x="438.0" y="106" font-size="11" class="halo">8, 16, ..., 248, sent as off &gt;&gt; 3</text>
  <g class="acc"><rect x="430.0" y="121" width="180.0" height="14" class="fill-acc" fill-opacity="0.45" stroke="currentColor" stroke-width="0.8"/></g>
  <text x="422.0" y="132" font-size="11" text-anchor="end">256, 264, ..., 2040, sent as off &gt;&gt; 3</text>
  <line x1="150.0" y1="150" x2="710.0" y2="150" stroke="currentColor"/>
  <line x1="151.1" y1="150" x2="151.1" y2="155" stroke="currentColor"/>
  <text x="151.1" y="168" text-anchor="start">1</text>
  <line x1="430.0" y1="150" x2="430.0" y2="155" stroke="currentColor"/>
  <text x="430.0" y="168" text-anchor="middle">256</text>
  <line x1="610.0" y1="150" x2="610.0" y2="155" stroke="currentColor"/>
  <text x="610.0" y="168" text-anchor="middle">{{ch06.class5_end}}</text>
  <line x1="710.0" y1="150" x2="710.0" y2="155" stroke="currentColor"/>
  <text x="710.0" y="168" text-anchor="end">4095</text>
  <text x="10" y="168" font-size="11" class="mut">offset</text>
  <g class="mark"><path d="M152.2 149 l-4 -7 h8 z" fill="currentColor"/></g>
  <g class="mark"><path d="M228.8 149 l-4 -7 h8 z" fill="currentColor"/></g>
  <g class="mark"><path d="M444.4 149 l-4 -7 h8 z" fill="currentColor"/></g>
  <text x="10" y="188" font-size="11" class="mut">class 0 is any offset equal to last, the offset of the sequence before: no bits.</text>
  <g font-size="11" class="mut">
    <text x="30" y="222" text-anchor="end">off</text>
    <text x="64" y="222" text-anchor="middle">3</text>
    <text x="124" y="222" text-anchor="middle">off &lt; 256</text>
    <text x="204" y="222" text-anchor="middle">off == 2</text>
    <text x="290" y="222" text-anchor="middle">aligned</text>
    <text x="350" y="222" text-anchor="middle">class</text>
    <text x="470" y="222" text-anchor="middle">offset bits</text>
    <text x="628" y="222" text-anchor="middle">value sent</text>
  </g>
  <line x1="10" y1="229" x2="710" y2="229" stroke="currentColor" stroke-width="0.6" class="mut"/>
  <g class="mark"><text x="30" y="250" text-anchor="end" font-weight="bold">2</text></g>
  <text x="64" y="250" text-anchor="middle">3</text>
  <text x="94" y="250" text-anchor="middle">-</text>
  <text x="124" y="250" text-anchor="middle">{{ch06.off.2.lt256}}</text>
  <text x="164" y="250" text-anchor="middle">-</text>
  <text x="204" y="250" text-anchor="middle">{{ch06.off.2.eq2}}</text>
  <text x="247" y="250" text-anchor="middle">+ 2 *</text>
  <text x="290" y="250" text-anchor="middle">{{ch06.off.2.aligned}}</text>
  <text x="320" y="250" text-anchor="middle">=</text>
  <g class="acc"><text x="350" y="250" text-anchor="middle" font-weight="bold">{{ch06.off.2.cls}}</text></g>
  <text x="470" y="250" text-anchor="middle">nibble {{ch06.off.2.cls}}: {{ch06.off.2.raw}}</text>
  <text x="628" y="250" text-anchor="middle">2 &gt;&gt; {{ch06.off.2.shift}} = {{ch06.off.2.sent}}</text>
  <g class="mark"><text x="30" y="274" text-anchor="end" font-weight="bold">72</text></g>
  <text x="64" y="274" text-anchor="middle">3</text>
  <text x="94" y="274" text-anchor="middle">-</text>
  <text x="124" y="274" text-anchor="middle">{{ch06.off.72.lt256}}</text>
  <text x="164" y="274" text-anchor="middle">-</text>
  <text x="204" y="274" text-anchor="middle">{{ch06.off.72.eq2}}</text>
  <text x="247" y="274" text-anchor="middle">+ 2 *</text>
  <text x="290" y="274" text-anchor="middle">{{ch06.off.72.aligned}}</text>
  <text x="320" y="274" text-anchor="middle">=</text>
  <g class="acc"><text x="350" y="274" text-anchor="middle" font-weight="bold">{{ch06.off.72.cls}}</text></g>
  <text x="470" y="274" text-anchor="middle">nibble {{ch06.off.72.cls}}: {{ch06.off.72.raw}}</text>
  <text x="628" y="274" text-anchor="middle">72 &gt;&gt; {{ch06.off.72.shift}} = {{ch06.off.72.sent}}</text>
  <g class="mark"><text x="30" y="298" text-anchor="end" font-weight="bold">399</text></g>
  <text x="64" y="298" text-anchor="middle">3</text>
  <text x="94" y="298" text-anchor="middle">-</text>
  <text x="124" y="298" text-anchor="middle">{{ch06.off.399.lt256}}</text>
  <text x="164" y="298" text-anchor="middle">-</text>
  <text x="204" y="298" text-anchor="middle">{{ch06.off.399.eq2}}</text>
  <text x="247" y="298" text-anchor="middle">+ 2 *</text>
  <text x="290" y="298" text-anchor="middle">{{ch06.off.399.aligned}}</text>
  <text x="320" y="298" text-anchor="middle">=</text>
  <g class="acc"><text x="350" y="298" text-anchor="middle" font-weight="bold">{{ch06.off.399.cls}}</text></g>
  <text x="470" y="298" text-anchor="middle">nibble {{ch06.off.399.cls}}: {{ch06.off.399.raw}}</text>
  <text x="628" y="298" text-anchor="middle">399 &gt;&gt; {{ch06.off.399.shift}} = {{ch06.off.399.sent}}</text>
  <text x="10" y="331">SEQLZ_RAW_NIBBLES</text>
  <text x="10" y="347" font-size="11" class="mut">= {{ch06.RAW_NIBBLES}}</text>
  <rect x="170" y="316" width="60" height="24" class="fill-sheet" stroke="currentColor"/>
  <text x="200.0" y="333" text-anchor="middle" font-weight="bold">{{ch04.raw.5}}</text>
  <text x="200.0" y="356" text-anchor="middle" font-size="11">class 5</text>
  <text x="200.0" y="370" text-anchor="middle" font-size="11" class="mut">&gt;&gt; 20</text>
  <g class="acc"><rect x="230" y="316" width="60" height="24" class="fill-acc" fill-opacity="0.3" stroke="currentColor"/></g>
  <text x="260.0" y="333" text-anchor="middle" font-weight="bold">{{ch04.raw.4}}</text>
  <text x="260.0" y="356" text-anchor="middle" font-size="11">class 4</text>
  <text x="260.0" y="370" text-anchor="middle" font-size="11" class="mut">&gt;&gt; 16</text>
  <g class="acc"><rect x="290" y="316" width="60" height="24" class="fill-acc" fill-opacity="0.3" stroke="currentColor"/></g>
  <text x="320.0" y="333" text-anchor="middle" font-weight="bold">{{ch04.raw.3}}</text>
  <text x="320.0" y="356" text-anchor="middle" font-size="11">class 3</text>
  <text x="320.0" y="370" text-anchor="middle" font-size="11" class="mut">&gt;&gt; 12</text>
  <rect x="350" y="316" width="60" height="24" class="fill-sheet" stroke="currentColor"/>
  <text x="380.0" y="333" text-anchor="middle" font-weight="bold">{{ch04.raw.2}}</text>
  <text x="380.0" y="356" text-anchor="middle" font-size="11">class 2</text>
  <text x="380.0" y="370" text-anchor="middle" font-size="11" class="mut">&gt;&gt; 8</text>
  <g class="acc"><rect x="410" y="316" width="60" height="24" class="fill-acc" fill-opacity="0.3" stroke="currentColor"/></g>
  <text x="440.0" y="333" text-anchor="middle" font-weight="bold">{{ch04.raw.1}}</text>
  <text x="440.0" y="356" text-anchor="middle" font-size="11">class 1</text>
  <text x="440.0" y="370" text-anchor="middle" font-size="11" class="mut">&gt;&gt; 4</text>
  <rect x="470" y="316" width="60" height="24" class="fill-sheet" stroke="currentColor"/>
  <text x="500.0" y="333" text-anchor="middle" font-weight="bold">{{ch04.raw.0}}</text>
  <text x="500.0" y="356" text-anchor="middle" font-size="11">class 0</text>
  <text x="500.0" y="370" text-anchor="middle" font-size="11" class="mut">&gt;&gt; 0</text>
</svg>
</div>
<figcaption>The class <code>seqlz_off_class()</code> gives a new offset, on an axis stretched below 256, and its terms added up for the offsets 2, 72 and 399.</figcaption>
</figure>

| `off` | `aligned` | class | offset bits | value sent |
| --- | --- | --- | --- | --- |
| 2 | {{ch06.off.2.aligned}} | {{ch06.off.2.cls}} | {{ch06.off.2.raw}} | {{ch06.off.2.sent}} |
| 8 | {{ch06.off.8.aligned}} | {{ch06.off.8.cls}} | {{ch06.off.8.raw}} | {{ch06.off.8.sent}} |
| 255 | {{ch06.off.255.aligned}} | {{ch06.off.255.cls}} | {{ch06.off.255.raw}} | {{ch06.off.255.sent}} |
| 2040 | {{ch06.off.2040.aligned}} | {{ch06.off.2040.cls}} | {{ch06.off.2040.raw}} | {{ch06.off.2040.sent}} |
| 2048 | {{ch06.off.2048.aligned}} | {{ch06.off.2048.cls}} | {{ch06.off.2048.raw}} | {{ch06.off.2048.sent}} |
@assert ch06.off[8].cls == 4 and ch06.off[2048].cls == 3 and ch06.off[2].cls == 1 and ch06.off[2040].sent == 255

Offset 8 goes to class 4 in 5 bits, where class 2 could send it in 8. 2048 is a multiple of 8 beyond
class 5, so it is class 3. With `off == last` the class is 0 and no bits follow; `last` is 1 at the
start of a page, so a first match at offset 1, a run of one byte, is class 0 too. gcc 16.2.1 at `-O2`
for x86-64 compiles the function without a jump: `setcc` for the comparisons, an `add`, a `sub` and a
`lea` for the sum, a conditional move for the repeat offset.

::: try Print them yourself
```c
#include <stdio.h>
#include "seqlz.h"
int main(void)
{
	unsigned int raw, extra;
	unsigned int cls = seqlz_off_class(264, 1, &raw);
	unsigned int sym = seqlz_len_symbol(100, &extra);

	printf("0x%X %u %u\n", SEQLZ_RAW_NIBBLES, SEQLZ_LEN_SYMBOLS, SEQLZ_MAX_ESCAPE_LEN);
	printf("off 264: class %u, %u bits; v 100: symbol %u, %u bits\n", cls, raw, sym, extra);
	return 0;
}
```
@program ch06-print

`gcc -O2 -Isrc t.c && ./a.out` prints

@output ch06-print

With `-DSEQLZ_PAGE_BITS=14` the first line is `{{k16.ch06.RAW_NIBBLES}} {{k16.ch06.LEN_SYMBOLS}} {{k16.ch06.MAX_ESCAPE_LEN}}`, class 3 has {{k16.ch06.RAW_BITS3}} bits, and class 5 keeps its {{k16.ch06.RAW_BITS5}}.
:::
@end

## Coded literals

@@ 218-233
A literal table codes the 256 byte values in codes of at most 10 bits, so its decode table has 1024
entries of 2 bytes, 2 KiB. 9 bits would halve it; the comment says how that went.

Why 8 streams: the decoder knows where literal `k + 1` starts only after it has looked up literal `k`,
so one stream is one chain of lookups, each waiting for the one before. 8 streams are 8 independent
chains, which a CPU works on side by side ([chains](book:#ch-cpu--chains)). That there are also 8
tables is a coincidence: all streams of a block use its one table.
@anim streams Literals dealt out to 8 streams, and decoded a round at a time
@more {#ch-header--literals} 9-bit codes, 4 streams and 16 tables, measured
With codes of at most 9 bits the decode table would be half the cache lines on a cold read, but on the
Mi 9T that was no faster on either core, and pages got up to 64 bytes larger
([item 2 of "Six choices"](https://github.com/martinus/seqlz/blob/main/docs/explored-designs.md#2-the-literal-tables-in-9-bits-no-faster-up-to-64-bytes-larger-not-kept)).
The comment's other two choices are measured where their code is:
[16 tables against 8](#ch-codes--lit-sets-file) and [4 streams against 8](#ch-literals-dec--cost).
@end

@@ 234-255
8 sizes of `w` bits each are exactly `w` bytes, hence `3 + w`. E.g. a block whose largest stream has
100 bytes needs `w = 7` and a header of 10 bytes.

The code field has 4 bits for 9 values: 0 to 7 the fixed tables, 8 the block's own table. 9 to 15 are
invalid on purpose, so that a future format can tell itself apart
([no version](book:#ch-format--version)). The `static_assert`s that these fields fit together are in
`seqlz_tables.h` ([byte 2](book:#ch-internal--byte2)).
@more {#ch-header--header} Byte 2 and the header, bit by bit
```text
byte 2:      bit 7 | bit 6  bit 5  bit 4 | bit 3  bit 2  bit 1  bit 0
             deltas|       w - 5         |      the literals' code
byte 3:      with bit 7 set, which delta, 1 to 3, see below
bytes 3 + e: the 8 stream sizes in bytes, w bits each, lowest bit first, w bytes together,
             from byte 3 without deltas, e = 0, from byte 4 with them, e = 1
```

The header is $3 + w$ bytes, 8 to 15, one more with deltas, and `SEQLZ_LIT_HEADER(0)` is 3, the
offset of the delta's byte with deltas and of the first size byte without. The header reads in order:
byte 2 says whether the delta's byte is there, and it comes next, before the sizes.

A stream holds every 8th literal of at most 10 bits, at most 640 bytes on 4 KiB pages and 2560 on
16 KiB pages, which need 10 and 12 bits. Widths of 3 and 4 bits would need a fourth bit for the width,
and only 1.8% of the pages with coded literals would use them
([`docs/explored-designs.md`](https://github.com/martinus/seqlz/blob/main/docs/explored-designs.md#stream-sizes-in-as-many-bits-as-the-largest-needs-kept)).

Bit 7 was a free bit too, which had to be 0, until level 3 began to store its literals as deltas on
21st October 2026. E.g. the text page of [a page's own table](#ch-format--own) at level 3 has byte 2
`{{own.text.t}}`: code {{own.text.lcode}} in bits 0 to 3, $w - 5 = {{own.text.w5}}$ in bits 4 to 6,
and bit 7 0, its literals are as they are. `SEQLZ_LIT_CODE_MASK`, `SEQLZ_LIT_WIDTH_AT` and
`SEQLZ_LIT_DELTA_AT` are the masks and shifts that pick the fields.
@assert own.text.lcode == own.lit_own and own.text.w5 + 5 == own.text.w and int(own.text.t, 16) < 128
@end

@@ 256-272
A delta turns a slowly growing value into one small number that repeats. E.g. a page of pointers, each
16 larger than the one before: the low byte `0x30` minus the low byte of the pointer 8 bytes back,
`0x20`, is `0x10`, and so is the next one, `0x40 - 0x30`. A subtraction and not a XOR: `0x40` XOR
`0x30` is `0x70`, so a XOR gives one value per pattern of carries ([deltas](book:#ch-format--deltas)).

Why "4 or more" for `SEQLZ_DELTA_LAST`: the decoder adds 4 or 8 bytes at a time, which only works if
every byte it adds is at least 4 bytes back. After a match at offset 3 the bytes repeat every 3, so the
byte 6 back equals the byte 3 back. 0 is not a valid value: bit 7 already says whether there are deltas.
@more {#ch-header--deltas} The page of 512 pointers, as deltas and as a XOR
The encoder subtracts the earlier byte from the literal, the decoder adds it back, both modulo 256,
with the byte the encoder used, which the decoder has written to the page by then. A literal that
equals the byte it is to becomes 0, and one that is a little larger or smaller becomes a small number
or one just below 256, e.g. `0x01` or `0xff`. When many of a page's literals do that, their deltas are
a handful of values that come again and again, and a literal table codes those in fewer bits than the
literals as they are.

The page of 512 pointers of [chapter 9](#ch-matcher--back), `0x7f1234560010 + 16 * i`, shows the
second choice. At level 3 its sequences are mostly 1 literal, the low byte of a pointer, and the 7
bytes after it copied from the pointer before, offset 8. `SEQLZ_DELTA_8` stores each such literal as
its delta to the low byte of the pointer before, 8 bytes back: the literal `0x30` minus `0x20`, which
gives `0x10`. `SEQLZ_DELTA_LAST` would take the same bytes after the first sequence, as every offset
before them is 8, and its price is the same; the encoder keeps the first of equal prices
([chapter 11](#ch-literals-enc--deltas)). Of the {{delta.ptr.n_lit}} literals,
{{delta.ptr.distinct.plain}} different byte values as they are become {{delta.ptr.distinct.deltas}}
as deltas, and {{delta.ptr.h.10}} of them are `0x10`. Level 2 stores the page in {{delta.ptr.l2.len}}
bytes with raw literals, as coding them saves too little; level 3 without deltas would store it in
{{delta.ptr.l3plain.len}} bytes, with them in {{delta.ptr.l3.len}}. Its byte 2 is
`{{delta.ptr.l3.t}}`: code {{delta.ptr.l3.code}}, the page's own table, $w = {{delta.ptr.l3.w}}$,
bit 7 set, and byte {{delta.ptr.l3.dt_at}}, right before the sizes, is {{delta.ptr.l3.dt}},
`SEQLZ_DELTA_8`.
@assert delta.ptr.l3.dt == ch06.DELTA_8 and delta.ptr.price[2] == delta.ptr.price[3] and delta.ptr.recs_last8 == delta.ptr.recs - 1 and delta.ptr.l3.code == own.lit_own and delta.ptr.l3.len < delta.ptr.l3plain.len < delta.ptr.l2.len and delta.ptr.l3.dt_at == 3

This page shows why a delta and not a XOR. A XOR of the same literals with the same bytes, which
level 3 wrote before the deltas, gives {{delta.ptr.xor.distinct}} values: only {{delta.ptr.xor.h.10}}
are `0x10`, the others `0x30`, `0x70`, `0xf0` and more, wherever adding `0x10` carries into the
upper bits of the low byte, e.g. from `0x30` to `0x40`, where `0x30` XOR `0x40` is `0x70`. Pointers to
objects in a row, counters and sizes grow by a little from one field to the next, and a subtraction
turns that into one small value where a XOR gives one value per pattern of carries.
@assert delta.ptr.xor.distinct > delta.ptr.distinct.deltas and F("delta.ptr.xor.h.10") < F("delta.ptr.h.10") and min(F("delta.ptr.xor.h.30"), F("delta.ptr.xor.h.70"), F("delta.ptr.xor.h.f0")) > 0

The delta's byte is byte 3 with every code, a fixed table's or the page's own, and the sizes, the
description and the streams move one byte up behind it. `seqlz_delta_last()`
([its values](#ch-internal--delta-last)) computes the distance of `SEQLZ_DELTA_LAST`, the encoder picks the
delta in `seqlz_delta_literals()` ([chapter 11](#ch-literals-enc--deltas)), and the decoder adds the
bytes back in `undelta_literals()` ([chapter 13](#ch-literals-dec--undelta)). On ten samples of 20 000
pages, level 3 stores 869.7 bytes per page with deltas, 915.6 without, 5.0% less, measured on 21st and
22nd October 2026
([explored-designs.md](https://github.com/martinus/seqlz/blob/main/docs/explored-designs.md#level-3-stores-its-literals-as-deltas-not-xors-084-fewer-bytes-than-the-xor-kept),
[one rule](https://github.com/martinus/seqlz/blob/main/docs/explored-designs.md#one-rule-for-which-literals-are-deltas-045-fewer-bytes-a-simpler-spec-kept));
[chapter 17](#ch-decisions--deltas) has what it costs in time.
@end

@@ 273-298
A run's symbol says how many bits its number has. E.g. a run of 32 is `0b100000`, 6 bits: symbol
`SEQLZ_OWN_RUN + 6 = 15`, then the 5 bits below the top one, `00000`. Runs of 1 to 9 bits make 9
symbols; with the 10 lengths, 19 in all, `SEQLZ_OWN_SYMBOLS`. A code of at most
`SEQLZ_OWN_CODE_BITS`, 8 bits, makes the decoder's table for these symbols 256 entries, which one byte
of the description indexes. `seqlz_own_lengths` is generated into `seqlz_lit_sets_4k.inc` and
`_16k.inc`, after the 8 literal tables ([the file](seqlz_lit_sets_4k.inc.html#b156)).

The last paragraph is why a block's own table costs the decoder no memory. Its decode table needs
2 KiB, and work memory per CPU is what the project counts. The output page is still free while the
literals are decoded into the scratch, so the table is built there
([own table](book:#ch-literals-dec--own)).
@anim owndesc How a page's own table is described, one symbol per step
