# src/seqlz_codes.c

Turns code lengths into codes and lookup tables. The format fixes each of its {{ch17.tables}} tables by
one length per symbol, nothing else: {{ch08.length_bytes}} bytes compiled into the codec,
{{tok.symbols}} for the token table, {{ll.symbols}} each for the two tables of length values, 256 for
each of the 8 literal tables and {{own.symbols}} for `OWN` ([the lengths](seqlz_lengths.c.html)).
This file computes from them the encoder's code for each symbol and the decoder's tables, which look a
code up with its next bits in one step: `struct seqlz_tables`, {{size.tables|grp}} bytes
([its layout](seqlz_tables.h.html)). Nothing calls it while seqlz compresses: a program on the build
machine runs it once and prints the result as C ([at build time](book:#ch-codes--kernel)). The tests
run it on other lengths.

A reading order that follows the call:

1. [`seqlz_tables_build()`](#b237), the one function: a `memset()`, then one builder per table.
2. [`first_codes()`](#b24): the canonical codes, and the check that the lengths are a complete code.
3. [`build_token()`](#b58), the pattern of every builder: hand out codes, fill one range of the
   decode table per code. Then [length values](#b83), [literals](#b104),
   [their costs](#b128) and [`OWN`](#b145) with its [`quad[]`](#b171).
4. [`has_all_symbols()`](#b217): what the encoder needs on top of a valid code.

@more {#ch-codes--kernel} How the build and the kernel get the tables: `gen_seqlz_tables.c`
Nothing runs this file while seqlz compresses. `tools/gen-tables/gen_seqlz_tables.c` is a program
for the build host that calls `seqlz_tables_build()` with the lengths of the format and prints the
result as C:

@listing tools/gen-tables/gen_seqlz_tables.c 1-{{ch15.kport.gen_main}}

`tools/gen-tables/gen-tables.sh` builds it with this file and the lengths, with the build host's
compiler also when seqlz is cross-compiled, runs it, and writes what it prints, `seqlz_tables.c`:
`const struct seqlz_tables seqlz_tables`, {{size.tables|grp}} bytes of read-only data, shared by all
CPUs. CMake runs the script, and so do the scripts that build `src/` without CMake. No program builds
tables when it starts, and none calls anything before its first block. The program reads each value
with its own type, so the build host's byte order does not matter: CI generates the tables on s390,
which is big endian, too, and compares the two files.

The kernel gets the same program. `tools/kernel-port/port.py` writes `lib/seqlz/gen_seqlz_tables.c`,
a program for the build host as `lib/crc/gen_crc32table.c` is for the CRC tables: this file, with
`seqlz_tables_build()` static, the code lengths from `lib/seqlz/seqlz_lengths.h`, and the program
above from its line `/* ---- the tables as C` on. `lib/seqlz/Makefile` builds it, runs it and
compiles `seqlz_tables.c`, and `tools/kernel-port/check.sh` compares that file with the one
`gen-tables.sh` writes. Lengths that `seqlz_tables_build()` refuses stop the build, so a kernel never
has tables that the encoder cannot use. The kernel's tables used to be built at boot, into
`__ro_after_init` memory: the kernel image then carried {{size.tables|grp}} bytes of zeros, and every
boot ran this file's loops once.
@end

## Lengths to codes

@@ 1-11
@anchor ch-codes
@anchor ch-codes--size
It includes [`seqlz.h`](seqlz.h.html), the userspace interface, for the declaration of
`seqlz_tables_build()`, and the kernel's headers for `-EINVAL` and `memset()`. In the kernel this file
is not in the kernel image: `port.py` puts it into `lib/seqlz/gen_seqlz_tables.c`, a program for the
build host, with `seqlz_tables_build()` static, the printing part after it, and the C library's
`errno.h`, `stdio.h` and `string.h`, `seqlz_tables.h` and the generated `seqlz_lengths.h` in place of
these includes ([at build time](book:#ch-codes--kernel)).

@@ 12-23
The part of a length value's decode entry that depends only on the symbol: the base in bits 8 and up,
the number of extra bits in bits 4 to 7. `build_values()` puts the code's length in bits 0 to 3.
Symbols below 16 are their own value. Symbol 18 is the values 64 to 127: base 64, 6 extra bits, entry
`0x4060`.
@more {#ch-codes--length-entry} Base and extra bits of every symbol, up to the last
In the layout of [the decode tables](#ch-internal--decode-tables), symbols below 16 are their value
with no extra bits, and a symbol $s$ from 16 on has the base $2^{s - 12}$ and $s - 12$ extra bits
([length values](#ch-header--length-values)). Symbol 18 gives `{{ch08.le.18}}`, base 64 and 6 extra
bits. The last symbol, {{ch08.le_last_symbol}}, has {{ch08.le_last_extra}} extra bits, which fit the
field's 4 bits.
@assert ch08.le_last_symbol == 23
@end

@@ 24-49
A canonical code needs only the lengths: the codes of one length are consecutive numbers, in symbol
order. So the format stores lengths and never codes ([canonical codes](book:#ch-compression--canonical-codes)).

The sum checks that the codes fill the decode table exactly. A code of `l` bits owns
`2^(bits - l)` entries of a table of `2^bits`. With `bits = 3`, lengths 1, 2, 3, 3 own 4 + 2 + 1 + 1
= 8 entries: complete. Lengths 1, 2 own 6: the indices `11x` decode to nothing, and the decoder would
need a check for that on every lookup. Lengths 1, 1, 1 own 12, more than the table: the builders below
would write past its end, so this check also keeps them inside their arrays.

The length is tested against `bits` before `of_len[len[s]]++`, because `of_len` has 16 entries and a
`u8` can be 255.
@more {#ch-codes--first-codes} Every way the lengths can fail, and the test of each
With `bits = 3`, a table of 8 entries ([complete codes](#ch-compression--complete)):

| lengths | `k` | |
| --- | --- | --- |
| 1, 1, 1 | $4 + 4 + 4 = 12$ | too many codes: code 2's range would be the entries 8 to 11 |
| 1, 2, 0 | $4 + 2 = 6$ | a gap: the indices starting with `11` belong to no code |
| 0, 0, 0 | 0 | no code at all |
| 1, 2, 4 | | $4 > 3$: returns before `k` is computed |

The test `seqlz: tables that are no prefix code are rejected` in `test/seqlz_test.cpp` builds such
tables from the real ones, and one case only the first check catches: a complete token table plus a
code of 12 bits. The sum counts lengths up to 11 only, so `k` is still exactly 2048.
@end

@@ 50-57
`next[l]` is the first code of length `l`, deflate's rule: go on where the shorter codes ended, one bit
longer. For lengths A = 2, B = 1, C = 3, D = 3, in binary: `next[1] = 0`,
`next[2] = 10`, `next[3] = 110`, so B is `0`, A is `10`, C is `110` and D is `111`. `of_len[0]` counted the symbols without a code and must not
take code space.
@more {#ch-codes--first-codes-ll} `next[]` of the real `ll` table
The rule is the one of deflate (RFC 1951, section 3.2.2) that
[`docs/format.md`](https://github.com/martinus/seqlz/blob/main/docs/format.md#prefix-codes)
specifies: `c = (c + of_len[l - 1]) << 1` is `first[l] = (first[l-1] + count[l-1]) * 2`, and setting
`of_len[0]` to 0 first is the spec's "`count[0]` is taken as 0". The builders then hand out `next[l]`,
`next[l] + 1`, ... to the symbols of that length, in symbol order.

For the real `ll` table, `{{ch08.ll.lengths}}`, the sum is
${{ch08.ll.fill.3}} + {{ch08.ll.fill.4}} + {{ch08.ll.fill.5}} + {{ch08.ll.fill.6}} + {{ch08.ll.fill.8}} = 256$,
and `next[3]` to `next[8]` are {{ch08.ll.next.3}}, {{ch08.ll.next.4}}, {{ch08.ll.next.5}},
{{ch08.ll.next.6}}, {{ch08.ll.next.7}} and {{ch08.ll.next.8}}. The 3-bit codes go to symbols 0, 1,
16 and 17: the values 0 and 1, literal runs of 15 and 16 bytes, and 16 to 63, runs of 31 to 78 bytes.
`next[7]` is {{ch08.ll.next.7}} although no code has 7 bits, and `next[8]` goes on from it. The token
table has no code shorter than 4 bits; its {{tok.codes_of.11}} codes of 11 bits are the numbers
{{ch08.tok.next.11}} to 2047, the last entries of the table.
@assert (ll.codes_of[1], ll.codes_of[2], ll.codes_of[3], ll.codes_of[4], ll.codes_of[5], ll.codes_of[6], ll.codes_of[7], ll.codes_of[8]) == (0, 0, 4, 3, 6, 7, 0, 4) and ll.len[0] == ll.len[1] == ll.len[16] == ll.len[17] == 3
@assert tok.codes_of[3] == 0 and tok.codes_of[4] > 0 and ch08.tok.next[11] + tok.codes_of[11] == 2048
@end

## The builders

@@ 58-82
A code of `l` bits is the top `l` bits of the 11-bit index, and the other `11 - l` bits can be
anything. So its entries are the range `c << (11 - l)` to `((c + 1) << (11 - l)) - 1`, all the same.
Token 49 has the 4-bit code `0000`: entries 0 to 127, one sixteenth of the table. This only works because
the bitstream is read most significant bit first ([why](book:#ch-compression--msb)).

The loop runs to `<=` for the escape, symbol 3072. A token without a code keeps `enc[s] = 0`, which
tells the encoder to escape it ([an entry of 0](book:#ch-internal--token-table)). `c = next[l]++`
hands out the next code of that length.
@anim table From code lengths to a decode table and back to symbols
@more {#ch-codes--build-token} Token 49's range, and the cache lines of the 4-bit codes
Token 49, 1 literal and a match of 7 at the repeat offset, has the {{tok.len.49}}-bit code
`{{tok.code.49}}`:

```text
first index   {{ch07.tok.49.c}} << (11 - {{tok.len.49}})        = {{ch07.tok.49.first}}   {{tok.code.49}} 0000000
last index    (({{ch07.tok.49.c}} + 1) << (11 - {{tok.len.49}})) - 1  = {{ch07.tok.49.last}}   {{tok.code.49}} 1111111
{{ch07.tok.49.entries}} entries of {{ch07.tok.49.entry}}, bytes {{ch08.tok49.byte_first}} to {{ch08.tok49.byte_last}} of the table: {{ch08.tok49.lines}} cache lines of 64 bytes
```
@assert tok.len[49] == 4 and ch07.tok[49].c == 0

The three codes of 4 bits, tokens 49 and 2097 and the escape, take the first {{ch08.tok.len4.bytes}}
bytes of the table, three sixteenths, in {{ch08.tok.len4.lines}} cache lines.
@assert tok.codes_of[4] == 3 and tok.len[49] == tok.len[2097] == tok.len[3072] == 4 and ch08.tok.len4.bytes == 1536

Read the other way, before 6th October 2026, a code's entries were spread over every cache line of
the table; [the bit order](#ch-cpu--msb-first) has the cache lines and the measurements. One more
result belongs here: on the Mi 9T's A76, leaving the token table out of the prefetches, as the decoder
then did on that core, made cold reads 1.3 to 2.6 µs faster in every run once the codes were ranges,
and only in some builds before
([explored-designs.md](https://github.com/martinus/seqlz/blob/main/docs/explored-designs.md#the-sequences-bitstream-most-significant-bit-first-the-token-tables-codes-in-one-range-on-every-cpu-kept)).
@end

@@ 83-103
@anchor ch-codes--build-values
The same for `ll` and `ml`, in tables of 256 entries. `ll` symbol 18 has the 5-bit code `11011`, 27:
`enc[18] = 27 | 5 << 16`, and the 8 entries 216 to 223 get `0x4060 | 5`. `enc[]` has 32 entries for
the encoder's masked index; those behind the last symbol stay 0 from the `memset()`.

@@ 104-127
The same for a literal table, codes of up to 10 bits. `enc[]` has the length low and the code above it,
the other way round from the token's, because that is what `lit_put()` wants
([layout](book:#ch-internal--lit-table)). The comment is for readers who know deflate: deflate reads its bits
least significant first, so its decoders bit-reverse each code before they fill a table. seqlz reads
most significant first and uses the codes as they are.
@more {#ch-codes--build-lit} The first two codes of table 0
In table 0 the space, `0x20`, and `e`, `0x65`, have the first two 4-bit codes, `{{lit.0.code.32}}`
and `{{lit.0.code.101}}`: no code in table 0 is shorter, and `0x20` comes first in symbol order. The
8 literal streams are read most significant bit first, like the sequences' bitstream and like `zstd`
reads its Huffman coded literals, so the codes go into the tables as `first_codes()` numbers them
([the decoder](#ch-literals-dec)).
@assert ch08.lit[0].min == 4 and lit[0].len[32] == lit[0].len[101] == 4 and lit[0].codes_of[4] >= 2
@end

## Literal costs

@@ 128-144
@anchor ch-codes--lit-sets
The literal tables always come from the global `seqlz_lit_sets[8][256]`, while
`seqlz_tables_build()` takes the token and length tables as a parameter, so that the tests can build
tables from other lengths.

`lit_cost[b]` packs byte `b`'s code length in all 8 tables into one `u64`, table `k` in byte `k`. The
encoder adds one `u64` per literal and gets the cost of the page's literals in all 8 tables at once
([one add](book:#ch-internal--lit-cost)). The second loop fills it in one pass over all 2048 pairs:
`k % 256` is the byte value, `k / 256` the table and `8U * (k / 256)` its lane. For `e`, `0x65`, it is
`{{ch07.lit_cost.101}}`: {{lit.0.len.101}} bits in table 0, {{lit.7.len.101}} in table 7.

The `(u64)` cast matters: without it the `u8` becomes an `int`, and shifting an `int` by 56 is
undefined.

## OWN and quad[]

@@ 145-170
@anchor ch-codes--own
`OWN` is the fixed code that a level 3 page writes its own literal table in
([format](book:#ch-format--own)). Its 19 symbols: 0 to 9 are the lengths 1 to 10, 10 to 18 a run of
bytes without a code. Its lengths are not in `struct seqlz_lengths`: `seqlz_own_lengths` is generated
into `seqlz_lengths.h`, after the literal tables ([there](seqlz_lengths.h.html#b374)). Same pattern as
`build_lit()`, symbol in the low byte. A length of 0 is rejected here: the encoder may need any of the
19 symbols, and the loop would read `next[0]`, which `first_codes()` never sets.

@@ 171-202
`quad[k]` lets the decoder take up to two lengths from 8 bits with one lookup instead of one symbol at
a time. It is filled by decoding `k` itself with `decode[]`, symbol after symbol, as far as the 8
bits go. `w` is `k` after the first `pos` bits, moved to the top with zeros below; a code that reaches
into those zeros is not in `k`, and the loop stops.

A run's value is its top bit plus the `nb - 1` bits after its code. The loop also stops at a run that
does not fit, one longer than 7 (3 bits in the entry), and a second run in a row, which the encoder
never writes. E.g. `0xd1`, `11010001`: `1101` is a run of 1, `00` the length 9, `01` the length 10.
@more {#ch-codes--quad} `0xd1` through the loop, step by step
A length takes its place in `len[]`, with the run before it in `before[]`. A run reads its `nb - 1`
bits below the top one from right after its code: `w << l` drops the code, and the shift by
`bits + 1 - nb` keeps the top `nb - 1` of the 8 bits. For a run of 1, `nb` is 1 and the shift is 8,
which leaves 0 of an 8-bit value, so `read(0)` needs no case of its own.

For `k = 0xd1`, binary `{{own.q.d1.bits}}`:

| `pos` | `w` | symbol, code | what happens |
| --- | --- | --- | --- |
| 0 | `{{own.q.d1.bits}}` | {{own.q.d1.sym}}, `{{own.code.10}}` | a run, `nb = 1`, `run = 1`, `pos = 4` |
| 4 | `{{own.q.d1.w4}}` | 8, `{{own.code.8}}` | the length 9, `before[0] = 1`, `pos = end[0] = 6` |
| 6 | `{{own.q.d1.w6}}` | 9, `{{own.code.9}}` | the length 10, `before[1] = 0`, `pos = end[1] = 8` |
@assert own.q.d1.sym == 10 and own.code[8] == "00" and own.code[9] == "01" and own.q.d1.end1 == 6

Two lengths, so the loop ends. 256 entries are 256 runs of this loop, once per build of the tables.
@end

@@ 203-216
The fields of the entry ([layout](book:#ch-internal--own)). No length at all: only bit 31, which sends
the decoder to `decode[]`. One length: `end[1] = end[0]`, because the decoder reads the second end also
when there is no second length, and must then move by the first's bits. `0xd1` gives `0x000186a9`.
@more {#ch-codes--quad-entry} Where the 8 bits of `0xd1` end up in `quad[0xd1]`
The run goes into `before[0]`, each length into `len[]`, and `end[]` gets the bits up to the end of
each length, with the run before it. [The layout](#ch-internal--own) has the same entry as a table,
field by field.
@assert len(own.code[10]) == 4 and len(own.code[8]) == len(own.code[9]) == 2 and own.q.d1.symlen == 4 and len(own.q.d1.b.free) == 9 and int(own.q.d1.b.free, 2) == 0 and own.q.d1.b.all == own.q.d1.b.alone + own.q.d1.b.free + own.q.d1.b.run2 + own.q.d1.b.run1 + own.q.d1.b.end2 + own.q.d1.b.end1 + own.q.d1.b.l2 + own.q.d1.b.l1 and int(own.q.d1.b.all, 2) == int(own.q.d1.entry, 16)

<figure class="diagram">
<div class="scroll">
<svg viewBox="0 0 720 262" role="img" aria-label="The 8 bits 0xd1 decoded with the code OWN into a run of 1, the length 9 and the length 10, and where build_own() puts them in the 32 bits of quad[0xd1]" font-size="12">
  <text x="10" y="16" font-weight="bold">k = 0xd1, the 8 bits {{own.q.d1.bits}}, read with decode[] symbol by symbol</text>
  <g class="mark"><rect x="184" y="34" width="176" height="30" class="fill-mark" fill-opacity="0.3" stroke="currentColor"/></g>
  <line x1="228" y1="59" x2="228" y2="64" stroke="currentColor" stroke-width="0.6" class="mut"/>
  <line x1="272" y1="59" x2="272" y2="64" stroke="currentColor" stroke-width="0.6" class="mut"/>
  <line x1="316" y1="59" x2="316" y2="64" stroke="currentColor" stroke-width="0.6" class="mut"/>
  <text x="202.1" y="53.7" textLength="139.8" lengthAdjust="spacing" font-weight="bold" font-size="13">{{own.code.10}}</text>
  <g class="acc"><rect x="360" y="34" width="88" height="30" class="fill-acc" fill-opacity="0.3" stroke="currentColor"/></g>
  <line x1="404" y1="59" x2="404" y2="64" stroke="currentColor" stroke-width="0.6" class="mut"/>
  <text x="378.1" y="53.7" textLength="51.8" lengthAdjust="spacing" font-weight="bold" font-size="13">{{own.code.8}}</text>
  <g><rect x="448" y="34" width="88" height="30" class="fill-sheet" stroke="currentColor"/></g>
  <line x1="492" y1="59" x2="492" y2="64" stroke="currentColor" stroke-width="0.6" class="mut"/>
  <text x="466.1" y="53.7" textLength="51.8" lengthAdjust="spacing" font-weight="bold" font-size="13">{{own.code.9}}</text>
  <line x1="184" y1="64" x2="184" y2="72" stroke="currentColor"/>
  <text x="184" y="84" text-anchor="middle" font-size="11">0</text>
  <line x1="360" y1="64" x2="360" y2="72" stroke="currentColor"/>
  <text x="360" y="84" text-anchor="middle" font-size="11">{{own.q.d1.symlen}}</text>
  <line x1="448" y1="64" x2="448" y2="72" stroke="currentColor"/>
  <text x="448" y="84" text-anchor="middle" font-size="11">{{own.q.d1.end1}}</text>
  <line x1="536" y1="64" x2="536" y2="72" stroke="currentColor"/>
  <text x="536" y="84" text-anchor="middle" font-size="11">{{own.q.d1.end2}}</text>
  <text x="172" y="84" text-anchor="end" font-size="11" class="mut">pos</text>
  <g class="mark"><text x="272.0" y="102" text-anchor="middle" font-size="11">symbol {{own.q.d1.sym}}</text><text x="272.0" y="116" text-anchor="middle" font-size="11">a run of {{own.q.d1.run1}}</text></g>
  <g class="acc"><text x="404.0" y="102" text-anchor="middle" font-size="11">symbol 8</text><text x="404.0" y="116" text-anchor="middle" font-size="11">length {{own.q.d1.l1}}</text></g>
  <g><text x="492.0" y="102" text-anchor="middle" font-size="11">symbol 9</text><text x="492.0" y="116" text-anchor="middle" font-size="11">length {{own.q.d1.l2}}</text></g>
  <text x="10" y="150" font-weight="bold">quad[0xd1] = {{own.q.d1.entry}}, bit 31 on the left</text>
  <g><rect x="24" y="176" width="21" height="26" class="fill-paper" stroke="currentColor"/></g>
  <text x="30.9" y="193.3" textLength="7.2" lengthAdjust="spacing" font-weight="bold" font-size="12">{{own.q.d1.b.alone}}</text>
  <text x="34.5" y="220" text-anchor="middle" font-weight="bold">{{own.q.d1.alone}}</text>
  <text x="34.5" y="236" text-anchor="middle" font-size="10" class="mut">31</text>
  <text x="139.5" y="170" text-anchor="middle" font-size="10">unused</text>
  <g><rect x="45" y="176" width="189" height="26" class="fill-paper" stroke="currentColor"/></g>
  <line x1="66" y1="197" x2="66" y2="202" stroke="currentColor" stroke-width="0.6" class="mut"/>
  <line x1="87" y1="197" x2="87" y2="202" stroke="currentColor" stroke-width="0.6" class="mut"/>
  <line x1="108" y1="197" x2="108" y2="202" stroke="currentColor" stroke-width="0.6" class="mut"/>
  <line x1="129" y1="197" x2="129" y2="202" stroke="currentColor" stroke-width="0.6" class="mut"/>
  <line x1="150" y1="197" x2="150" y2="202" stroke="currentColor" stroke-width="0.6" class="mut"/>
  <line x1="171" y1="197" x2="171" y2="202" stroke="currentColor" stroke-width="0.6" class="mut"/>
  <line x1="192" y1="197" x2="192" y2="202" stroke="currentColor" stroke-width="0.6" class="mut"/>
  <line x1="213" y1="197" x2="213" y2="202" stroke="currentColor" stroke-width="0.6" class="mut"/>
  <text x="51.9" y="193.3" textLength="175.2" lengthAdjust="spacing" font-weight="bold" font-size="12">{{own.q.d1.b.free}}</text>
  <text x="139.5" y="236" text-anchor="middle" font-size="10" class="mut">30 to 22</text>
  <text x="265.5" y="170" text-anchor="middle" font-size="10">before[1]</text>
  <g><rect x="234" y="176" width="63" height="26" class="fill-paper" stroke="currentColor"/></g>
  <line x1="255" y1="197" x2="255" y2="202" stroke="currentColor" stroke-width="0.6" class="mut"/>
  <line x1="276" y1="197" x2="276" y2="202" stroke="currentColor" stroke-width="0.6" class="mut"/>
  <text x="240.9" y="193.3" textLength="49.2" lengthAdjust="spacing" font-weight="bold" font-size="12">{{own.q.d1.b.run2}}</text>
  <text x="265.5" y="220" text-anchor="middle" font-weight="bold">{{own.q.d1.run2}}</text>
  <text x="265.5" y="236" text-anchor="middle" font-size="10" class="mut">21 to 19</text>
  <text x="328.5" y="170" text-anchor="middle" font-size="10">before[0]</text>
  <g class="mark"><rect x="297" y="176" width="63" height="26" class="fill-mark" fill-opacity="0.3" stroke="currentColor"/></g>
  <line x1="318" y1="197" x2="318" y2="202" stroke="currentColor" stroke-width="0.6" class="mut"/>
  <line x1="339" y1="197" x2="339" y2="202" stroke="currentColor" stroke-width="0.6" class="mut"/>
  <text x="303.9" y="193.3" textLength="49.2" lengthAdjust="spacing" font-weight="bold" font-size="12">{{own.q.d1.b.run1}}</text>
  <text x="328.5" y="220" text-anchor="middle" font-weight="bold">{{own.q.d1.run1}}</text>
  <text x="328.5" y="236" text-anchor="middle" font-size="10" class="mut">18 to 16</text>
  <text x="402.0" y="170" text-anchor="middle" font-size="10">end[1]</text>
  <g><rect x="360" y="176" width="84" height="26" class="fill-sheet" stroke="currentColor"/></g>
  <line x1="381" y1="197" x2="381" y2="202" stroke="currentColor" stroke-width="0.6" class="mut"/>
  <line x1="402" y1="197" x2="402" y2="202" stroke="currentColor" stroke-width="0.6" class="mut"/>
  <line x1="423" y1="197" x2="423" y2="202" stroke="currentColor" stroke-width="0.6" class="mut"/>
  <text x="366.9" y="193.3" textLength="70.2" lengthAdjust="spacing" font-weight="bold" font-size="12">{{own.q.d1.b.end2}}</text>
  <text x="402.0" y="220" text-anchor="middle" font-weight="bold">{{own.q.d1.end2}}</text>
  <text x="402.0" y="236" text-anchor="middle" font-size="10" class="mut">15 to 12</text>
  <text x="486.0" y="170" text-anchor="middle" font-size="10">end[0]</text>
  <g class="acc"><rect x="444" y="176" width="84" height="26" class="fill-acc" fill-opacity="0.3" stroke="currentColor"/></g>
  <line x1="465" y1="197" x2="465" y2="202" stroke="currentColor" stroke-width="0.6" class="mut"/>
  <line x1="486" y1="197" x2="486" y2="202" stroke="currentColor" stroke-width="0.6" class="mut"/>
  <line x1="507" y1="197" x2="507" y2="202" stroke="currentColor" stroke-width="0.6" class="mut"/>
  <text x="450.9" y="193.3" textLength="70.2" lengthAdjust="spacing" font-weight="bold" font-size="12">{{own.q.d1.b.end1}}</text>
  <text x="486.0" y="220" text-anchor="middle" font-weight="bold">{{own.q.d1.end1}}</text>
  <text x="486.0" y="236" text-anchor="middle" font-size="10" class="mut">11 to 8</text>
  <text x="570.0" y="170" text-anchor="middle" font-size="10">len[1]</text>
  <g><rect x="528" y="176" width="84" height="26" class="fill-sheet" stroke="currentColor"/></g>
  <line x1="549" y1="197" x2="549" y2="202" stroke="currentColor" stroke-width="0.6" class="mut"/>
  <line x1="570" y1="197" x2="570" y2="202" stroke="currentColor" stroke-width="0.6" class="mut"/>
  <line x1="591" y1="197" x2="591" y2="202" stroke="currentColor" stroke-width="0.6" class="mut"/>
  <text x="534.9" y="193.3" textLength="70.2" lengthAdjust="spacing" font-weight="bold" font-size="12">{{own.q.d1.b.l2}}</text>
  <text x="570.0" y="220" text-anchor="middle" font-weight="bold">{{own.q.d1.l2}}</text>
  <text x="570.0" y="236" text-anchor="middle" font-size="10" class="mut">7 to 4</text>
  <text x="654.0" y="170" text-anchor="middle" font-size="10">len[0]</text>
  <g class="acc"><rect x="612" y="176" width="84" height="26" class="fill-acc" fill-opacity="0.3" stroke="currentColor"/></g>
  <line x1="633" y1="197" x2="633" y2="202" stroke="currentColor" stroke-width="0.6" class="mut"/>
  <line x1="654" y1="197" x2="654" y2="202" stroke="currentColor" stroke-width="0.6" class="mut"/>
  <line x1="675" y1="197" x2="675" y2="202" stroke="currentColor" stroke-width="0.6" class="mut"/>
  <text x="618.9" y="193.3" textLength="70.2" lengthAdjust="spacing" font-weight="bold" font-size="12">{{own.q.d1.b.l1}}</text>
  <text x="654.0" y="220" text-anchor="middle" font-weight="bold">{{own.q.d1.l1}}</text>
  <text x="654.0" y="236" text-anchor="middle" font-size="10" class="mut">3 to 0</text>
  <text x="24" y="170" font-size="10">alone</text>
</svg>
</div>
<figcaption>The 8 bits <code>0xd1</code> decoded with <code>OWN</code>, and the fields of <code>quad[0xd1]</code> they fill, from the tables the build makes.</figcaption>
</figure>
@end

## Checks and the entry point

@@ 217-236
@anchor ch-codes--all-symbols
A complete code is enough for the decoder: a symbol without a code can never appear. The encoder gets
any page and needs more: each token a code or the escape one, every length value a code, as they have
no escape, and every byte a code. A byte without one would count 0 bits in `lit_cost`, and its table
would look cheapest for exactly the pages it cannot code. The test `seqlz: the tables need a code for
every symbol the compressor may write` gives it a complete `ml` table whose last symbol has no code:
the decoder could use it, and the build is `-EINVAL`.

The escape's limit, `SEQLZ_MAX_ESCAPE_LEN`, comes from the encoder's proof that a page fits into zram's
two pages: the escape code, the token's 12 bits and a class 3 offset of 12 bits must stay within 31
bits ([the escape](book:#ch-header--escape)).

@@ 237-256
The `memset()` does more than the comment says: tokens without a code need `enc[]` 0, `lit_cost` is
built with `|=`, and a test that builds twice on the same memory keeps nothing of the first set. A
failure leaves the struct half built, which nobody uses: the generator stops the build, and a test
only checks the `-EINVAL`.
@more {#ch-codes--init} Before 22nd October 2026: tables built when a program started
Until 22nd October 2026 userspace built the global at run time, with a `seqlz_tables_init()` every
program had to call first. It kept tables the encoder could not use and marked them with an
`all_symbols` field, which the encoder checked for every block. Now such lengths stop the build
([at build time](#ch-codes--kernel)).
@end
