# src/seqlz_codes.c

Turns code lengths into codes and lookup tables. The format fixes each of its 12 tables by one length
per symbol, nothing else ([the lengths](book:#ch-codes)); this file computes from them the encoder's
code for each symbol and the decoder's tables, which look a code up with its next bits in one step.
Nothing calls it while seqlz compresses: a program on the build machine runs it once and prints the
result as C ([at build time](book:#ch-codes--kernel)).

A reading order that follows the call:

1. [`seqlz_tables_build()`](#b234), the one function: a `memset()`, then one builder per table.
2. [`first_codes()`](#b21): the canonical codes, and the check that the lengths are a complete code.
3. [`build_token()`](#b55), the pattern of every builder: hand out codes, fill one range of the
   decode table per code. Then [length values](#b80), [literals](#b101),
   [their costs](#b125) and [`OWN`](#b142) with its [`quad[]`](#b168).
4. [`has_all_symbols()`](#b214): what the encoder needs on top of a valid code.

## Lengths to codes

@@ 1-8
In the kernel this file is not in the kernel image either: `port.py` puts it into
`lib/seqlz/gen_seqlz_tables.c`, a program for the build host, with `seqlz_tables_build()` static and the
printing part after it ([at build time](book:#ch-codes--kernel)).

@@ 9-20
The part of a length value's decode entry that depends only on the symbol: the base in bits 8 and up,
the number of extra bits in bits 4 to 7. `build_values()` puts the code's length in bits 0 to 3.
Symbols below 16 are their own value. Symbol 18 is the values 64 to 127: base 64, 6 extra bits, entry
`0x4060`.

@@ 21-46
A canonical code needs only the lengths: the codes of one length are consecutive numbers, in symbol
order. So the format stores lengths and never codes ([canonical codes](book:#ch-compression--canonical-codes)).

The sum checks that the codes fill the decode table exactly. A code of `l` bits owns
`2^(bits - l)` entries of a table of `2^bits`. With `bits = 3`, lengths 1, 2, 3, 3 own 4 + 2 + 1 + 1
= 8 entries: complete. Lengths 1, 2 own 6: the indices `11x` decode to nothing, and the decoder would
need a check for that on every lookup. Lengths 1, 1, 1 own 12, more than the table: the builders below
would write past its end, so this check also keeps them inside their arrays.

The length is tested against `bits` before `of_len[len[s]]++`, because `of_len` has 16 entries and a
`u8` can be 255.

@@ 47-54
`next[l]` is the first code of length `l`, deflate's rule: go on where the shorter codes ended, one bit
longer. For lengths A = 2, B = 1, C = 3, D = 3, in binary: `next[1] = 0`,
`next[2] = 10`, `next[3] = 110`, so B is `0`, A is `10`, C is `110` and D is `111`. `of_len[0]` counted the symbols without a code and must not
take code space.

## The builders

@@ 55-79
A code of `l` bits is the top `l` bits of the 11-bit index, and the other `11 - l` bits can be
anything. So its entries are the range `c << (11 - l)` to `((c + 1) << (11 - l)) - 1`, all the same.
Token 49 has the 4-bit code `0000`: entries 0 to 127, one sixteenth of the table. This only works because
the bitstream is read most significant bit first ([why](book:#ch-compression--msb)).

The loop runs to `<=` for the escape, symbol 3072. A token without a code keeps `enc[s] = 0`, which
tells the encoder to escape it. `c = next[l]++` hands out the next code of that length.
@anim table From code lengths to a decode table and back to symbols

@@ 80-100
The same for `ll` and `ml`, in tables of 256 entries. `ll` symbol 18 has the 5-bit code `11011`, 27:
`enc[18] = 27 | 5 << 16`, and the 8 entries 216 to 223 get `0x4060 | 5`. `enc[]` has 32 entries for
the encoder's masked index; those behind the last symbol stay 0 from the `memset()`.

@@ 101-124
The same for a literal table, codes of up to 10 bits. `enc[]` has the length low and the code above it,
the other way round from the token's, because that is what `lit_put()` wants
([layout](book:#ch-internal--lit-table)). The comment is for readers who know deflate: deflate reads its bits
least significant first, so its decoders bit-reverse each code before they fill a table. seqlz reads
most significant first and uses the codes as they are.

## Literal costs

@@ 125-141
`lit_cost[b]` packs byte `b`'s code length in all 8 tables into one `u64`, table `k` in byte `k`. The
encoder adds one `u64` per literal and gets the cost of the page's literals in all 8 tables at once
([one add](book:#ch-internal--lit-cost)). For `e`, `0x65`, it is `0x0808080504060904`: 4 bits in
table 0, 8 in table 7.

The `(u64)` cast matters: without it the `u8` becomes an `int`, and shifting an `int` by 56 is
undefined.

## OWN and quad[]

@@ 142-167
`OWN` is the fixed code that a level 3 page writes its own literal table in
([format](book:#ch-format--own)). Its 19 symbols: 0 to 9 are the lengths 1 to 10, 10 to 18 a run of
bytes without a code. Same pattern as `build_lit()`, symbol in the low byte. A length of 0 is
rejected here: the encoder may need any of the 19 symbols, and the loop would read `next[0]`, which
`first_codes()` never sets.

@@ 168-199
`quad[k]` lets the decoder take up to two lengths from 8 bits with one lookup instead of one symbol at
a time. It is filled by decoding `k` itself with `decode[]`, symbol after symbol, as far as the 8
bits go. `w` is `k` after the first `pos` bits, moved to the top with zeros below; a code that reaches
into those zeros is not in `k`, and the loop stops.

A run's value is its top bit plus the `nb - 1` bits after its code. The loop also stops at a run that
does not fit, one longer than 7 (3 bits in the entry), and a second run in a row, which the encoder
never writes. E.g. `0xd1`, `11010001`: `1101` is a run of 1, `00` the length 9, `01` the length 10.

@@ 200-213
The fields of the entry ([layout](book:#ch-internal--own)). No length at all: only bit 31, which sends
the decoder to `decode[]`. One length: `end[1] = end[0]`, because the decoder reads the second end also
when there is no second length, and must then move by the first's bits. `0xd1` gives `0x000186a9`.

## Checks and the entry point

@@ 214-233
A complete code is enough for the decoder: a symbol without a code can never appear. The encoder gets
any page and needs more: each token a code or the escape one, every length value and every byte a code.
A byte without one would count 0 bits in `lit_cost`, and its table would look cheapest for exactly the
pages it cannot code.

The escape's limit, `SEQLZ_MAX_ESCAPE_LEN`, comes from the encoder's proof that a page fits into zram's
two pages: the escape code, the token's 12 bits and a class 3 offset of 12 bits must stay within 31
bits on 4 KiB pages ([the escape](book:#ch-header--escape)).

@@ 234-253
The `memset()` does more than the comment says: tokens without a code need `enc[]` 0, `lit_cost` is
built with `|=`, and a test that builds twice on the same memory keeps nothing of the first set. A
failure leaves the struct half built, which nobody uses: the generator stops the build.
