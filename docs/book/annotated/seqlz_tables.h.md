# src/seqlz_tables.h

The layout of the tables, shared by the compressor, the decompressor and `seqlz_codes.c`, which builds
them. The format stores each table as code lengths ([chapter 4](book:#ch-format--codes)). The encoder
needs each symbol's code as a number to shift into its bits, the decoder a table it indexes with the
next bits. So every table here has both forms, `enc` and `decode`, computed once when seqlz is built
and stored in one global `const` struct: {{ch17.tables}} tables, {{size.tables|grp}} bytes, read by
every CPU.

Almost every line is a bit layout: which bits of an entry hold what, and `static_assert()`s that the
fields are wide enough. A reading order:

1. [How a decode table works](#b14), and the [length values' table](#b39).
2. The [token table](#b45) and the [literal tables](#b62), sized for the L1 cache.
3. [`lit_cost`](#b67): all 8 literal tables priced with one add per literal.
4. [`quad[]`](#b95): two code lengths of a page's own table per lookup.
5. [`struct seqlz_tables`](#b121), the one global.
6. The end of the file, the part to read closely: the [token entry's fields](#b140) and
   [`token_entry()`](#b172), which builds them.

## Decode tables

@@ 1-13
@anchor ch-internal
@anchor ch-internal--head
It includes only `linux/build_bug.h`, for its `static_assert()`s, and `seqlz_format.h`, so a program on
the build machine can include it too: that program generates the tables as C when seqlz is built
([at build time](book:#ch-codes--kernel)). The guard, `_LIB_SEQLZ_SEQLZ_TABLES_H`, is named after the
file's place in the kernel, `lib/seqlz/`, as every guard of `src/`.

@@ 14-29
The first paragraph is the trick of every table decoder here. In a table indexed by 8 bits, a 3-bit
code `101` fills the 32 entries `10100000` to `10111111`, 160 to 191. Whatever 5 bits follow the code,
one lookup with the next 8 bits lands on an entry that says "this symbol, 3 bits".

An entry of a length table packs three numbers into one `u32`. E.g. symbol 18 of `ll` stands for the
values 64 to 127: base 64 and 6 extra bits, so its entries are `64 << 8 | 6 << 4 | l`, with `l` its
code's length. The base can need more than 8 bits, hence `u32`.

The third paragraph explains masks that change no value, such as `n & 63U` on a shift count that is
below 64 anyway. A kernel with UBSan adds a compare and a branch before every shift and index the
compiler cannot prove in range; the mask is that proof ([UBSan](book:#ch-cpu--ubsan)).
@anim table From code lengths to a decode table and back
@more {#ch-internal--decode-tables} Symbol 18 of `ll`: its entry, and the entries it fills
The rule is [table decoding](#ch-compression--table-decoding): a code of $l$ bits in a table of $2^L$
entries fills the $2^{L-l}$ entries that start with its bits, and they all say the same, this symbol,
$l$ bits long. Symbol 18 of the `ll` table, the values 64 to 127:

| bits | field | symbol 18 of `ll` |
| --- | --- | --- |
| 0 to 3 | the code's length | {{ll.len.18}} |
| 4 to 7 | extra bits after the code | 6 |
| 8 to 23 | the base value | 64 |

Its {{ll.len.18}}-bit code is `{{ll.code.18}}`, so the entry
`64 << 8 | 6 << 4 | {{ll.len.18}} = {{ch07.ll18.entry}}` fills the {{ch07.ll18.entries}} entries
{{ch07.ll18.first}} to {{ch07.ll18.last}} of the 256, and the value is `64 + read(6)`
([`seqlz_br_value()`](#ch-dec-basics--value)). The largest base is {{ch07.len_base_max}} on 4 KiB
pages and {{k16.ch07.len_base_max}} on 16 KiB pages.
@end

@@ 30-38
The length tables have fewer than 32 symbols, but the encoder indexes `enc[]` with
`symbol & (ENC_LEN_SYMBOLS - 1U)`, and that mask is a proof only if the array has a power of 2
entries. The first assert keeps the mask from folding a real symbol onto another one.

`CODE_LENGTHS` sizes the arrays that [`first_codes()`](book:#ch-codes--first-codes) and
[`own_code()`](book:#ch-literals-enc--own) index with a code's length, so no table may have codes of 16
bits. Three asserts and not one, because the kernel's `checkpatch` warns about a condition that
compares two constants inside a longer one ([checks](book:#ch-zram-glue--check)).
@more {#ch-internal--masks} Which masks, and what they saved
`enc_put()` shifts the accumulator by `n & 63U`, its count of bits masked
([the encoder](#ch-encoder--put)), and `put_len_value()` indexes the length values' `enc[]` with the
symbol of the value `v`, `seqlz_len_symbol(v, &n_extra) & (ENC_LEN_SYMBOLS - 1U)`. The symbol is at
most {{ch08.le_last_symbol}} ({{k16.ch08.le_last_symbol}} on 16 KiB pages), and `& 31` changes
nothing; it only tells the compiler what the code knows. For that `enc[]` has `ENC_LEN_SYMBOLS`, 32,
entries, where {{const.LEN_SYMBOLS}} would do ({{k16.const.LEN_SYMBOLS}} on 16 KiB pages). On 23rd
September 2026 the masks took the compressor to 4.7% fewer instructions ([chapter 3](#ch-cpu--ubsan)
has the cycles).
@end

## The tables

@@ 39-44
Both forms in one struct, because one computation makes them: `build_values()` works out each
symbol's code once and writes it to both. No function reads both, so a CPU loads only the cache lines
of the array it uses. `enc[]` has the code in the low 16 bits and its length from bit 16 on. The
comment on `decode[]` is the layout of its entries, which `length_entry()` builds
([its layout](book:#ch-codes--length-entry)) and [`seqlz_br_value()`](seqlz_decompress.c.html#b199)
takes apart.
@more {#ch-internal--value-table} Symbol 18 of `ll` in `enc[]`, and the sizes
`decode[256]` is indexed by the top 8 bits of the decoder's bit register, {{ch07.size.value_decode}}
bytes, and the whole struct is {{ch07.size.value_table}} bytes. Symbol 18 of `ll`, whose `decode[]`
entries [the decode tables](#ch-internal--decode-tables) show, is
`{{ch07.ll18.c}} | {{ll.len.18}} << 16 = {{ch07.ll18.enc}}` in `enc[]`. `enc_put_code()` takes it
apart with `entry >> 16 & 15U` and `entry & 0xffffU` ([the encoder](#ch-encoder--codes)).
@end

@@ 45-53
The decoder's table: 2048 entries of 4 bytes, 8 KiB, indexed by the next 11 bits. What an entry holds
is [the end of the file](#b140). The encoder's: one `u16` per token, 3072 tokens and the escape, about
6 KiB. The compressor reads the matcher's hash table of 8 KiB at every position and `enc[]` at every
sequence; 14 KiB together leave room in an L1 data cache of 32 KiB. Until 23rd September 2026 the
entries were `u32`, {{ch07.size.token_enc_u32|grp}} bytes, 20 KiB with the hash table (commit
`c84ba25`).

@@ 54-61
An `enc[]` entry is the code in bits 0 to 11 and its length in bits 12 to 15. E.g. a 5-bit code `10110`
is stored as `0x5016`. A code can be the number 0, but its entry is not 0, because the length is in it
too. So an entry of 0 means only one thing, "no code": the encoder then writes the escape's code and
the token in 12 plain bits, no flag needed. The two asserts check that 11 bits of code fit below bit 12
and 4 bits of length fit the `u16`.
@more {#ch-internal--token-table} Three tokens' entries: 49, the escape, 1797
As `token_code()` reads them ([the encoder](#ch-encoder--codes)):

| token | what it is | code | length | `enc[]` |
| --- | --- | --- | --- | --- |
| 49 | 1 literal, match of 7, repeat offset | `{{tok.code.49}}` | {{tok.len.49}} | `{{ch07.tok.49.enc}}` |
| 3072 | the escape | `{{tok.code.3072}}` | {{tok.len.3072}} | `{{ch07.tok.3072.enc}}` |
| 1797 | 5 literals, match of 20, class 3 | none | {{tok.len.1797}} | `{{ch07.tok.1797.enc}}` |
@assert tok.len[1797] == 0 and ch07.tok[49].c == 0

Token 49's code is the number 0, and its entry is not 0. Token 1797 has no code: its entry is 0, and it
goes out as the escape's code and 1797 in 12 bits.
@end

@@ 62-66
`decode[]` has the code's length in the low byte and the literal in the high byte. A byte and a length
of at most 10 fit 2 bytes, and a literal table is 2 KiB, half the cache lines it would be with 4-byte
entries.

`enc[]` has the length below the code, the other way round from the other tables, because of what
`lit_put()` does with an entry: `acc = acc << (e & 63) | e >> 8`, then `*sum += e`. The shift count is
the low byte as it is, the code one shift away, and adding whole entries sums the lengths in the low
byte of `sum` ([stream 0, code by code](book:#ch-literals-enc--lit_put-one-code-into-an-accumulator)).
@more {#ch-internal--lit-table} Byte `e` of table 0, in both arrays
In table 0, byte `0x65`, the letter `e`, has the {{lit.0.len.101}}-bit code `{{lit.0.code.101}}`. So
the {{ch07.lit_e.entries}} entries {{ch07.lit_e.first}} to {{ch07.lit_e.last}} of `decode[]` are
`{{lit.0.len.101}} | 0x65 << 8 = {{ch07.lit_e.entry}}`, and `enc[0x65]` is
`{{lit.0.len.101}} | {{ch07.lit_e.c}} << 8 = {{ch07.lit_e.enc}}`.

When `lit_put()` adds whole entries to `sum`, the code bits above the low byte come along; they are
dropped later with `& 255`. The 8 tables are {{ch07.size.lit_table}} bytes each,
{{ch07.size.lit|grp}} together, and a page reads one of them.
@end

@@ 67-74
SWAR, SIMD within a register ([chapter 3](book:#ch-cpu--swar)): byte `j` of `lit_cost[b]` is the code
length of byte value `b` in table `j`. E.g. the zero byte has a 2-bit code in tables 1 and 4, so bytes
1 and 4 of `lit_cost[0]` are 2. Adding `lit_cost[b]` for each literal sums all 8 tables in one add,
where the plain way takes 8 lookups and 8 adds.

A lane must not pass 255, or it carries into the next table's lane. 25 codes of at most 10 bits sum to
at most 250, so `lit_sums()` adds at most 25 literals into one word, then spreads its lanes into
16-bit ones and starts again.
@anim pricing One add per literal, then the move into 16-bit lanes
@more {#ch-internal--lit-cost} `0x00` and `e` priced in all 8 tables with one add
<figure class="diagram">
<div class="scroll">
<svg viewBox="0 0 720 170" role="img" aria-label="The lit_cost words of bytes 0x00 and 0x65, eight 8-bit lanes each, and their sum, where lane 3 is the smallest">
  <g font-size="11">
    <text x="230" y="16" text-anchor="middle">table 7</text>
    <text x="290" y="16" text-anchor="middle">table 6</text>
    <text x="350" y="16" text-anchor="middle">table 5</text>
    <text x="410" y="16" text-anchor="middle">table 4</text>
    <text x="470" y="16" text-anchor="middle">table 3</text>
    <text x="530" y="16" text-anchor="middle">table 2</text>
    <text x="590" y="16" text-anchor="middle">table 1</text>
    <text x="650" y="16" text-anchor="middle">table 0</text>
    <text x="230" y="30" text-anchor="middle">bits 63..56</text>
    <text x="650" y="30" text-anchor="middle">bits 7..0</text>
  </g>
  <g font-size="12">
    <text x="4" y="55">lit_cost[0x00]</text>
    <text x="4" y="69" font-size="11">{{ch07.lit_cost.0}}</text>
    <text x="4" y="97">+ lit_cost[0x65]</text>
    <text x="4" y="111" font-size="11">{{ch07.lit_cost.101}}</text>
    <text x="4" y="147">= the sums</text>
    <text x="4" y="161" font-size="11">{{ch07.lit_cost.sum}}</text>
  </g>
  <g stroke="currentColor" stroke-width="1">
    <rect x="200" y="40" width="480" height="30" class="fill-sheet"/>
    <rect x="200" y="82" width="480" height="30" class="fill-sheet"/>
    <rect x="200" y="132" width="480" height="30" class="fill-paper"/>
    <rect x="380" y="132" width="60" height="30" class="fill-acc"/>
    <line x1="260" y1="40" x2="260" y2="70"/><line x1="320" y1="40" x2="320" y2="70"/>
    <line x1="380" y1="40" x2="380" y2="70"/><line x1="440" y1="40" x2="440" y2="70"/>
    <line x1="500" y1="40" x2="500" y2="70"/><line x1="560" y1="40" x2="560" y2="70"/>
    <line x1="620" y1="40" x2="620" y2="70"/>
    <line x1="260" y1="82" x2="260" y2="112"/><line x1="320" y1="82" x2="320" y2="112"/>
    <line x1="380" y1="82" x2="380" y2="112"/><line x1="440" y1="82" x2="440" y2="112"/>
    <line x1="500" y1="82" x2="500" y2="112"/><line x1="560" y1="82" x2="560" y2="112"/>
    <line x1="620" y1="82" x2="620" y2="112"/>
    <line x1="260" y1="132" x2="260" y2="162"/><line x1="320" y1="132" x2="320" y2="162"/>
    <line x1="380" y1="132" x2="380" y2="162"/><line x1="440" y1="132" x2="440" y2="162"/>
    <line x1="500" y1="132" x2="500" y2="162"/><line x1="560" y1="132" x2="560" y2="162"/>
    <line x1="620" y1="132" x2="620" y2="162"/>
    <line x1="200" y1="122" x2="680" y2="122"/>
  </g>
  <g font-size="13" text-anchor="middle">
    <text x="230" y="60">{{lit.7.len.0}}</text><text x="290" y="60">{{lit.6.len.0}}</text><text x="350" y="60">{{lit.5.len.0}}</text>
    <text x="410" y="60">{{lit.4.len.0}}</text><text x="470" y="60">{{lit.3.len.0}}</text><text x="530" y="60">{{lit.2.len.0}}</text>
    <text x="590" y="60">{{lit.1.len.0}}</text><text x="650" y="60">{{lit.0.len.0}}</text>
    <text x="230" y="102">{{lit.7.len.101}}</text><text x="290" y="102">{{lit.6.len.101}}</text><text x="350" y="102">{{lit.5.len.101}}</text>
    <text x="410" y="102">{{lit.4.len.101}}</text><text x="470" y="102">{{lit.3.len.101}}</text><text x="530" y="102">{{lit.2.len.101}}</text>
    <text x="590" y="102">{{lit.1.len.101}}</text><text x="650" y="102">{{lit.0.len.101}}</text>
    <text x="230" y="152">{{ch07.lit_sum.7}}</text><text x="290" y="152">{{ch07.lit_sum.6}}</text><text x="350" y="152">{{ch07.lit_sum.5}}</text>
    <text x="410" y="152" class="on-fill">{{ch07.lit_sum.4}}</text><text x="470" y="152">{{ch07.lit_sum.3}}</text><text x="530" y="152">{{ch07.lit_sum.2}}</text>
    <text x="590" y="152">{{ch07.lit_sum.1}}</text><text x="650" y="152">{{ch07.lit_sum.0}}</text>
  </g>
</svg>
</div>
<figcaption>Two literals, 0x00 and 0x65, priced in all 8 literal tables with one 64-bit add; table {{ch07.lit_best}} codes them in {{ch07.lit_best_bits}} bits.</figcaption>
</figure>

Tables 1 and 4, where the zero byte has a 2-bit code, are for pages with many zeros; table 0, where `e`
has a {{lit.0.len.101}}-bit code, fits text. For these two literals table {{ch07.lit_best}} wins with
{{ch07.lit_best_bits}} bits, against 16 as raw bytes; [`lit_sums()`](#ch-literals-enc--pricing) prices
four literals the same way.
@assert lit[1].len[0] == lit[4].len[0] == 2 and min(lit[0].len[0], lit[2].len[0], lit[3].len[0], lit[5].len[0], lit[6].len[0], lit[7].len[0]) > 2 and ch07.lit_best == 4

A word has 8 lanes because byte 2 of the format holds the number of fixed tables at 8
([the asserts](#b75)). `lit_sums()` adds 25 literals per stream into 8-bit lanes, then spreads them
into two words of 16-bit lanes with the mask `0x00ff00ff00ff00ff`: every 200 literals of the page, 25
for each of the 8 streams. The animation under the note shows it on a real page.
@end

@@ 75-81
@anchor ch-internal--byte2
Byte 2 of a page with coded literals: bits 0 to 3 pick the table (0 to 7 the fixed ones, 8 the page's
own), bits 4 to 6 the width of the stream sizes minus 5, bit 7 the deltas
([coded literals](book:#ch-format--coded)). These asserts tie the constants to that layout. A 9th
fixed table would collide with `SEQLZ_LIT_OWN`, and the decoder's `s[2] & (SEQLZ_LIT_SETS - 1U)` would
no longer pick the table. The second assert: 3 bits hold the 8 widths, 5 to 12, and they end where
the delta bit starts.

@@ 82-91
The delta `SEQLZ_DELTA_LAST` subtracts the byte as far back as the offset of the sequence before. Here
because the encoder and the decoder both need it. Below 4 it is the smallest multiple of `last` that is
at least 4: `last` 1 or 2 gives 4, 3 gives 6. With offset 3 the pattern repeats every 3 bytes, so the
byte 6 back equals the byte 3 back.

At least 4, because the decoder undoes the deltas 4 bytes at a time: none of the 4 bytes it adds may lie
in the word it is writing.
@more {#ch-internal--delta-last} `seqlz_delta_last()` for offsets 1 to 8, and who calls it
The encoder's `seqlz_delta_literals()` and the decoder's `undelta_literals()` and `undelta_fast()`
call it, so it is in the header both include. What the deltas are for: [the deltas](#ch-header--deltas).

| `last` | 1 | 2 | 3 | 4 | 5 | 8 |
| --- | --- | --- | --- | --- | --- | --- |
| `seqlz_delta_last(last)` | {{delta.last.1}} | {{delta.last.2}} | {{delta.last.3}} | {{delta.last.4}} | {{delta.last.5}} | {{delta.last.8}} |
@assert delta.last[1] == delta.last[2] == delta.last[4] == 4 and delta.last[3] == 6 and delta.last[8] == 8

A match with offset 1 or 2 repeats a pattern of 1 or 2 bytes, so the byte 4 back is the one the match
would have copied next, as the byte 6 back is with offset 3. An offset of 4 or more is the distance as
it is.
@end

@@ 92-94
Guards code, not the format: the encoder's `lit_streams()` and the decoder's
`seqlz_decode_literals()` are written out by hand for 8 streams.

## A page's own table

@@ 95-114
A page's own literal table is sent as a description, coded with the fixed code `OWN`. Decoding it is a
chain, where the next symbol starts is known only after this one. Most symbols are lengths of 7 to 10
with codes of 2 or 3 bits, so 8 bits often hold two whole, and `quad[]` returns both from one lookup.

E.g. `quad[0xd1]`: the 8 bits are a run of 1, then the length 9, then the length 10. The first ends
after 6 bits, the second after 8. `OWN_QUAD_NONE`, 11, is no length: it marks a missing second one, and
the decoder handles it without a branch ([`read_own_table()`](seqlz_decompress_literals.c.html#b233)). A run longer than 7
gets only bit 31, and the decoder takes its symbol from `decode[]`.
@anim ownread How the decoder reads a page's own table with quad[]
@more {#ch-internal--own} `quad[0xd1]` and `quad[0xfc]`, field by field
For the 8 bits `{{own.q.d1.bits}}`: `{{own.code.10}}` is a run of {{own.q.d1.run1}}, `{{own.code.8}}`
the length {{own.q.d1.l1}}, `{{own.code.9}}` the length {{own.q.d1.l2}}, so `quad[0xd1]` is
`{{own.q.d1.entry}}`:

| bits | field | value |
| --- | --- | --- |
| 0 to 3 | the first length | {{own.q.d1.l1}} |
| 4 to 7 | the second | {{own.q.d1.l2}} |
| 8 to 11 | bits up to the end of the first, run and length | {{own.q.d1.end1}} |
| 12 to 15 | bits up to the end of the second | {{own.q.d1.end2}} |
| 16 to 18 | the run before the first | {{own.q.d1.run1}} |
| 19 to 21 | the run before the second | {{own.q.d1.run2}} |
| 31 | alone | {{own.q.d1.alone}} |
@assert own.q.d1.l1 == 9 and own.q.d1.l2 == 10 and own.q.d1.end1 == 6 and own.q.d1.end2 == 8 and own.q.d1.run1 == 1 and own.q.d1.alone == 0

An entry with one length has `OWN_QUAD_NONE`, {{own.quad_none}}, as the second. The decoder turns it
into a bucket nobody reads and $1024 \gg 11 = 0$ code space, so it needs no branch for it.

A run of more than 7 does not fit 3 bits, and the long runs have codes of 8 bits anyway. 8 bits that
start with such a run, e.g. `{{own.q.fc.bits}}`, symbol {{own.q.fc.sym}}, a run of 32 whose 5 more bits
are in the next byte, get only bit 31, `{{own.q.fc.entry}}`: the decoder then takes the symbol from
`decode[]` and the run's bits one by one.
@assert own.q.fc.alone == 1 and own.q.fc.sym == 15
@end

@@ 115-120
The three forms of `OWN`. `decode[]` is indexed by the next 8 bits, as the token table by the next 11.
Here the code's length is in the high byte of both `decode[]` and `enc[]`, the other way round from
`struct lit_table`'s `enc[]`. [`build_own()`](seqlz_codes.c.html#b145) builds all three.

## All tables in one global

@@ 121-131
One `const` global, written as C when seqlz is built, in the kernel as in userspace. It sits in
read-only data, nothing writes it at run time, and nothing has to build it before the first page.
Lengths that are no complete code, or that leave out a symbol the encoder may write, stop the build
instead ([`has_all_symbols()`](seqlz_codes.c.html#b217)).

A page reads much less than all of it. The decoder reads `token.decode` (8 KiB), the `decode` of `ll`
and `ml` (1 KiB each), and with coded literals one literal table's `decode` (2 KiB): 12 KiB, which fits
the L1 cache. `lit_cost` comes after the literal tables with a little padding, since a `u64` needs 8-byte
alignment.
@more {#ch-internal--tables} What lies where, and which parts a page reads
The layout on x86-64, from `sizeof` and `offsetof`; arm64 aligns a `u64` the same way:

| field | offset | bytes | read by |
| --- | --- | --- | --- |
| `token.decode` | 0 | {{ch07.size.token_decode}} | decoder, every sequence |
| `token.enc` | {{ch07.off.token_enc}} | {{ch07.size.token_enc}} | encoder, every sequence |
| `lit[0]` to `lit[7]` | {{ch07.off.lit|grp}} | $8 \times {{ch07.size.lit_table}} = {{ch07.size.lit|grp}}$ | decoder: one `decode`; encoder: one `enc` |
| (padding) | {{ch07.off.pad|grp}} | {{ch07.pad}} | |
| `lit_cost` | {{ch07.off.lit_cost|grp}} | {{ch07.size.lit_cost}} | encoder, every literal |
| `own` | {{ch07.off.own|grp}} | {{ch07.size.own}} | encoder and decoder, a page's own table |
| `ll` | {{ch07.off.ll|grp}} | {{ch07.size.value_table}} | both, for long literal runs |
| `ml` | {{ch07.off.ml|grp}} | {{ch07.size.value_table}} | both, for long matches |

The 16 KiB build has the same size, {{k16.size.tables|grp}} bytes: its length tables have
{{k16.const.LEN_SYMBOLS}} symbols, and `enc[]` has 32 entries either way.

The decoder's 12 KiB fit the L1 cache ([cache](#ch-cpu--cache)). With a table of the page's own it
reads `own.quad` and `own.decode`, 1.5 KiB, instead of a 2 KiB literal table, and builds the 2 KiB
table in the page. The encoder reads `token.enc`, 6 KiB, the `enc` of `ll` and `ml`, `lit_cost`, 2 KiB,
and one literal table's `enc`, 1 KiB, at level 3 also `own.enc`.
@end

## The token entry

@@ 132-139
The asserts check the widths of the entry's fields, described next. `DROP` has 5 bits, at most 31, and
the most a token with a code moves the bitstream is 11 code bits plus class 3's 12 offset bits, 23 (25
on 16 KiB pages). `ll` has 4 bits, `ml` 6 bits for at most 35, the shift 2 bits.

`SEQLZ_OFF_SHIFT()` reads a whole nibble ([the nibbles](book:#ch-header--offsets)), so a shift of 4
fails here instead of being cut to 0 by the `& 3U` of `TOK_SHIFT()`. The last assert: an escaped token
is sent in 12 bits, which must hold every token number.

@@ 140-156
The decoder looks up the token's entry with the next 11 bits and never decodes the token's number.
Everything it would compute from it is stored ready: `ll`, `ml` with its minimum of 4 added, the flag
for a length value, and three shift counts that cut the offset out of the bit register.

How the shifts work, for 2 literals and a match of 7 at offset 16: class 4, offset sent as `16 / 8 = 2`
in 5 bits, so `BACK` is `64 - 5 = 59` and `SHIFT` is 3. The register has the code on top, then the 5
bits. `bits << N` drops the code, `>> 59` keeps the 5 bits, `<< 3` multiplies by 8. `DROP`, `N + 5`,
moves the stream past both. The same three shifts work for every class
([on a register, bit by bit](#ch-internal--token-2098-two-literals-a-match-of-7-16-bytes-back)).

On the Mi 9T's Cortex-A55 this layout took the decode loop from 16 547 to 15 539 instructions per page
(3rd October 2026); an in-order core gains time almost one for one with instructions saved.
@more {#ch-internal--token-entry} The 32 bits of token 2098 and of the escape
Every field is something the decoder would otherwise compute from the token's number for every
sequence: split off `ll`, `ml` and the class, look up the class's offset bits and shift, add
`SEQLZ_MIN_MATCH` to `ml`, compare `ll` and `ml` with their caps.

The entry, bit 31 on the left, for token 2098 with its {{tok.len.2098}}-bit code and for the escape
with its {{tok.len.3072}}-bit code:

<figure class="diagram">
<div class="scroll">
<svg viewBox="0 0 720 230" role="img" aria-label="The 32 bits of a token table entry: escape, value flag, an unused bit, ml, ll, drop, shift, back and n, with the bits of token 2098 and of the escape">
  <g font-size="11" text-anchor="middle">
    <text x="50" y="14">31</text><text x="70" y="14">30</text><text x="90" y="14">29</text>
    <text x="110" y="14">28</text><text x="210" y="14">23</text><text x="230" y="14">22</text>
    <text x="290" y="14">19</text><text x="310" y="14">18</text><text x="390" y="14">14</text>
    <text x="410" y="14">13</text><text x="430" y="14">12</text><text x="450" y="14">11</text>
    <text x="550" y="14">6</text><text x="570" y="14">5</text><text x="670" y="14">0</text>
  </g>
  <g stroke="currentColor" stroke-width="1">
    <rect x="40" y="22" width="20" height="30" class="fill-mark"/>
    <rect x="60" y="22" width="20" height="30" class="fill-sheet"/>
    <rect x="80" y="22" width="20" height="30" fill="none"/>
    <rect x="100" y="22" width="120" height="30" class="fill-sheet"/>
    <rect x="220" y="22" width="80" height="30" class="fill-sheet"/>
    <rect x="300" y="22" width="100" height="30" class="fill-acc"/>
    <rect x="400" y="22" width="40" height="30" class="fill-acc"/>
    <rect x="440" y="22" width="120" height="30" class="fill-acc"/>
    <rect x="560" y="22" width="120" height="30" class="fill-acc"/>
  </g>
  <g font-size="12" text-anchor="middle">
    <text x="50" y="42" class="on-fill">E</text><text x="70" y="42">V</text><text x="90" y="42">-</text>
    <text x="160" y="42">ML</text><text x="260" y="42">LL</text><text x="350" y="42" class="on-fill">DROP</text>
    <text x="420" y="41" class="on-fill" font-size="10">SHIFT</text><text x="500" y="42" class="on-fill">BACK</text><text x="620" y="42" class="on-fill">N</text>
  </g>
  <g font-size="11">
    <text x="4" y="82">2098</text>
    <text x="4" y="164">esc</text>
  </g>
  <g stroke="currentColor" stroke-width="1" fill="none">
    <rect x="40" y="66" width="640" height="24"/>
    <rect x="40" y="148" width="640" height="24"/>
    <line x1="100" y1="66" x2="100" y2="90"/><line x1="220" y1="66" x2="220" y2="90"/>
    <line x1="300" y1="66" x2="300" y2="90"/><line x1="400" y1="66" x2="400" y2="90"/>
    <line x1="440" y1="66" x2="440" y2="90"/><line x1="560" y1="66" x2="560" y2="90"/>
    <line x1="60" y1="66" x2="60" y2="90"/><line x1="80" y1="66" x2="80" y2="90"/>
    <line x1="100" y1="148" x2="100" y2="172"/><line x1="220" y1="148" x2="220" y2="172"/>
    <line x1="300" y1="148" x2="300" y2="172"/><line x1="400" y1="148" x2="400" y2="172"/>
    <line x1="440" y1="148" x2="440" y2="172"/><line x1="560" y1="148" x2="560" y2="172"/>
    <line x1="60" y1="148" x2="60" y2="172"/><line x1="80" y1="148" x2="80" y2="172"/>
  </g>
  <g font-size="12" text-anchor="middle">
    <text x="50" y="83">{{ch07.tok.2098.bits.escape}}</text><text x="70" y="83">{{ch07.tok.2098.bits.value}}</text><text x="90" y="83">{{ch07.tok.2098.bits.free}}</text>
    <text x="160" y="83">{{ch07.tok.2098.bits.ml}}</text><text x="260" y="83">{{ch07.tok.2098.bits.ll}}</text><text x="350" y="83">{{ch07.tok.2098.bits.drop}}</text>
    <text x="420" y="83">{{ch07.tok.2098.bits.shift}}</text><text x="500" y="83">{{ch07.tok.2098.bits.back}}</text><text x="620" y="83">{{ch07.tok.2098.bits.n}}</text>
  </g>
  <g font-size="12" text-anchor="middle" class="acc">
    <text x="160" y="108">ml {{ch07.tok.2098.ML}}</text><text x="260" y="108">ll {{ch07.tok.2098.LL}}</text><text x="350" y="108">{{ch07.tok.2098.DROP}}</text>
    <text x="420" y="108">{{ch07.tok.2098.SHIFT}}</text><text x="500" y="108">{{ch07.tok.2098.BACK}}</text><text x="620" y="108">{{ch07.tok.2098.N}}</text>
  </g>
  <g font-size="11">
    <text x="40" y="128">{{ch07.tok.2098.entry}}: a {{tok.len.2098}}-bit code, then 5 offset bits, the offset is them times 8</text>
  </g>
  <g font-size="12" text-anchor="middle">
    <text x="50" y="165">{{ch07.tok.3072.bits.escape}}</text><text x="70" y="165">{{ch07.tok.3072.bits.value}}</text><text x="90" y="165">{{ch07.tok.3072.bits.free}}</text>
    <text x="160" y="165">{{ch07.tok.3072.bits.ml}}</text><text x="260" y="165">{{ch07.tok.3072.bits.ll}}</text><text x="350" y="165">{{ch07.tok.3072.bits.drop}}</text>
    <text x="420" y="165">{{ch07.tok.3072.bits.shift}}</text><text x="500" y="165">{{ch07.tok.3072.bits.back}}</text><text x="620" y="165">{{ch07.tok.3072.bits.n}}</text>
  </g>
  <g font-size="12" text-anchor="middle" class="mark">
    <text x="50" y="190">esc</text><text x="350" y="190">{{ch07.tok.3072.DROP}}</text>
  </g>
  <g font-size="11">
    <text x="40" y="214">{{ch07.tok.3072.entry}}: drop the escape's {{tok.len.3072}} bits, then read the token in 12 bits</text>
  </g>
</svg>
</div>
<figcaption>A token table entry: bits 0 to 18 tell the decoder how to cut the offset out of the bitstream, bits 19 to 28 are the lengths, the top bits flags.</figcaption>
</figure>
@end
@more {#ch-internal--token-entry-measured} 1418 fewer instructions per page on the A55, measured
On an in-order core such as the Cortex-A55 an instruction saved is time saved, almost one for one
([in-order cores](#ch-cpu--in-order)). On the Cortex-A55 of a Xiaomi Mi 9T, `simpleperf` showed that
18 of about 60 instructions per sequence went into unpacking the token. With 4-byte entries holding the
fields ready to use, the decode loop went from 16 547 to 15 539 instructions per page and from 16 247
to 15 776 cycles, on phone pages. The bit for "a length value follows" saved another 410 instructions
and no cycles: there the chain from one token's lookup to the next was the limit. The table grew from
4 KiB to 8 KiB, which did not make cold reads slower. That was 3rd October 2026, commit `5c769f5`
([explored-designs.md](https://github.com/martinus/seqlz/blob/main/docs/explored-designs.md#seqlzs-decoder-for-an-in-order-core-15-fewer-instructions-07-to-08-µs-less-at-cold-p99-on-the-a55-kept)).
@end

@@ 157-171
Each macro shifts its field down and masks it to its width. Bits 30 and 31 the decoder tests directly,
and bit 29 is free. `TOK_N()` and `TOK_BACK()` are shift counts for a 64-bit register, so their
`& 63U` is again the proof that keeps `CONFIG_UBSAN_SHIFT` from adding a check.
@more {#ch-internal--examples} Print any token's entry
```c
#include <stdio.h>
#include "seqlz.h"

int main(void)
{
	unsigned int s = 2098, n = seqlz_default_lengths.token[s];
	u32 e = token_entry(s, n);

	printf("%#010x n %u back %u shift %u drop %u ll %u ml %u value %u\n",
	       e, TOK_N(e), TOK_BACK(e), TOK_SHIFT(e), TOK_DROP(e), TOK_LL(e),
	       TOK_ML(e), (e >> TOK_VALUE_AT) & 1);
	return 0;
}
```
@program ch07-entry

Build it from the repository's root with `gcc -O2 -Isrc t.c src/seqlz_lengths.c` and run it.
It prints

@output ch07-entry

Change `s` for any other token.
@end

@@ 172-188
Two callers, so it is in the header: `build_token()` fills the table with it when seqlz is built, and
the decoder calls it at run time for an escaped token, with `n = 0` because the bits are dropped
already. For the escape the entry is only bit 31 and `n` in `DROP`, so the decoder can drop the
escape's code before it reads the token's 12 bits.

`(64U - raw_bits) & 63U` is `BACK`. Class 0, the repeat offset, has no offset bits, and 64 is no valid
shift of a `u64` and does not fit 6 bits; `& 63U` makes it 0. So `BACK` is 0 only for class 0, and the
decoder uses it as the test, `off = back ? raw : last`, with no compare of the class. The `(u32)` cast
keeps every term of the OR unsigned.
@more {#ch-internal--token-entry-code} Why the entry holds `ml + 4`, and the `BACK` of each class
For the escape `cls` and `raw_bits` are computed too: class 6, nibble 6 of `{{ch06.RAW_NIBBLES}}`, 0.
The compiler drops them.

**`ml + SEQLZ_MIN_MATCH`.** The entry stores the match length itself, 4 to 35, not the token's field.
That is one add per sequence less in the decoder, which copies `TOK_ML(tok)` bytes, and its test for a
length value is `len == SEQLZ_ML_CAP + SEQLZ_MIN_MATCH`. The price is 6 bits instead of 5, and bit 29
is still free.

**`(64U - raw_bits) & 63U`.** `BACK` is the right shift that brings the offset bits down after the
code was shifted out on top of the 64-bit register: for class 3, $64 - 12 = 52$. `& 63U` turns class
0's 64 into 0 and leaves 52, 56, 59 and 63 as they are. Every class but 0 has a `BACK` of at least 50
(class 3 on 16 KiB pages), so one field is a shift and a flag at once.
@end
@more {#ch-internal--a-class-3-token-and-one-with-a-length-value} Tokens 1552 and 1567: class 3, and a length value to follow
Token 1552 is no literals and a match of 5 bytes at an offset in 12 bits. It has a
{{tok.len.1552}}-bit code: on desktop pages 29% of the matches with a class 3 offset are 5 bytes long,
against 16% of all matches (20 000 pages of each desktop dump, `seqlz_find()` and the classes of 14th
October 2026, same-filled pages left out, measured on 14th October). Its entry `{{ch07.tok.1552.entry}}`
has `BACK` {{ch07.tok.1552.BACK}} and `DROP` ${{tok.len.1552}} + 12 = {{ch07.tok.1552.DROP}}$.

Token 1567 is the same with `ll` 15. Its entry `{{ch07.tok.1567.entry}}` has bit 30 set, from
`ll == SEQLZ_LL_CAP`, so the decoder leaves the fast path and, after the offset, reads an `ll` value
and adds it to 15.
@assert ch07.tok[1567].value == 1 and ch07.tok[1552].value == 0
@end
