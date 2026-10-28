# src/seqlz_tables.h

The layout of the tables, shared by the compressor, the decompressor and `seqlz_codes.c`, which builds
them. The format stores each table as code lengths ([chapter 4](book:#ch-format--codes)). The encoder
needs each symbol's code as a number to shift into its bits, the decoder a table it indexes with the
next bits of the stream. So every table here has both forms, `enc` and `decode`, computed once when
seqlz is built and stored in one global `const` struct.

Almost every line is a bit layout: which bits of an entry hold what, and `static_assert()`s that the
fields are wide enough. A reading order:

1. [How a decode table works](#b12), and the [length values' table](#b37).
2. The [token table](#b42) and the [literal tables](#b59), sized for the L1 cache.
3. [`lit_cost`](#b64): all 8 literal tables priced with one add per literal.
4. [`quad[]`](#b92): two code lengths of a page's own table per lookup.
5. [`struct seqlz_tables`](#b118), the one global.
6. The end of the file, the part to read closely: the [token entry's fields](#b137) and
   [`token_entry()`](#b169), which builds them.

## Decode tables

@@ 1-11
It includes only `seqlz_format.h`, so a program on the build machine can include it too: that program
generates the tables as C when seqlz is built ([chapter 8](book:#ch-codes--kernel)). The kernel's copy
renames the guard after its place in `lib/seqlz/`.

@@ 12-27
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

@@ 28-36
The length tables have fewer than 32 symbols, but the encoder indexes `enc[]` with
`symbol & (ENC_LEN_SYMBOLS - 1U)`, and that mask is a proof only if the array has a power of 2
entries. The first assert keeps the mask from folding a real symbol onto another one.

`CODE_LENGTHS` sizes the arrays that `first_codes()` and `own_code()` index with a code's length, so no
table may have codes of 16 bits. Three asserts and not one, because the kernel's `checkpatch` warns
about a condition that compares two constants inside a longer one.

## The tables

@@ 37-41
Both forms in one struct, because one computation makes them: `build_values()` works out each
symbol's code once and writes it to both. No function reads both, so a CPU loads only the cache lines
of the array it uses. `enc[]` has the code in the low 16 bits and its length from bit 16 on.

@@ 42-50
The decoder's table: 2048 entries of 4 bytes, 8 KiB, indexed by the next 11 bits. What an entry holds
is [the end of the file](#b137). The encoder's: one `u16` per token, 3072 tokens and the escape, about
6 KiB. The compressor reads the matcher's hash table of 8 KiB at every position and `enc[]` at every
sequence; 14 KiB together leave room in an L1 data cache of 32 KiB. Until 23rd September 2026 the
entries were `u32`, 20 KiB with the hash table.

@@ 51-58
An `enc[]` entry is the code in bits 0 to 11 and its length in bits 12 to 15. E.g. a 5-bit code `10110`
is stored as `0x5016`. A code can be the number 0, but its entry is not 0, because the length is in it
too. So an entry of 0 means only one thing, "no code": the encoder then writes the escape's code and
the token in 12 plain bits, no flag needed.

@@ 59-63
`decode[]` has the code's length in the low byte and the literal in the high byte. A byte and a length
of at most 10 fit 2 bytes, and a literal table is 2 KiB, half the cache lines it would be with 4-byte
entries.

`enc[]` has the length below the code, the other way round from the other tables, because of what
`lit_put()` does with an entry: `acc = acc << (e & 63) | e >> 8`, then `*sum += e`. The shift count is
the low byte as it is, the code one shift away, and adding whole entries sums the lengths in the low
byte of `sum` ([chapter 11](book:#ch-literals-enc--lit_put-one-code-into-an-accumulator)).

@@ 64-71
SWAR, SIMD within a register ([chapter 3](book:#ch-cpu--swar)): byte `j` of `lit_cost[b]` is the code
length of byte value `b` in table `j`. E.g. the zero byte has a 2-bit code in tables 1 and 4, so bytes
1 and 4 of `lit_cost[0]` are 2. Adding `lit_cost[b]` for each literal sums all 8 tables in one add,
where the plain way takes 8 lookups and 8 adds.

A lane must not pass 255, or it carries into the next table's lane. 25 codes of at most 10 bits sum to
at most 250, so `lit_sums()` adds at most 25 literals into one word, then spreads its lanes into
16-bit ones and starts again.
@anim pricing One add per literal, then the move into 16-bit lanes

@@ 72-78
Byte 2 of a page with coded literals: bits 0 to 3 pick the table (0 to 7 the fixed ones, 8 the page's
own), bits 4 to 6 the width of the stream sizes minus 5, bit 7 the deltas
([coded literals](book:#ch-format--coded)). These asserts tie the constants to that layout. A 9th
fixed table would collide with `SEQLZ_LIT_OWN`, and the decoder's `s[2] & (SEQLZ_LIT_SETS - 1U)` would
no longer pick the table.

@@ 79-88
The delta `SEQLZ_DELTA_LAST` subtracts the byte as far back as the offset of the sequence before. Here
because the encoder and the decoder both need it. Below 4 it is the smallest multiple of `last` that is
at least 4: `last` 1 or 2 gives 4, 3 gives 6. With offset 3 the pattern repeats every 3 bytes, so the
byte 6 back equals the byte 3 back.

At least 4, because the decoder undoes the deltas 4 bytes at a time: none of the 4 bytes it adds may lie
in the word it is writing.

@@ 89-91
Guards code, not the format: the encoder's `lit_streams()` and the decoder's
`seqlz_decode_literals()` are written out by hand for 8 streams.

## A page's own table

@@ 92-111
A page's own literal table is sent as a description, coded with the fixed code `OWN`. Decoding it is a
chain, where the next symbol starts is known only after this one. Most symbols are lengths of 7 to 10
with codes of 2 or 3 bits, so 8 bits often hold two whole, and `quad[]` returns both from one lookup.

E.g. `quad[0xd1]`: the 8 bits are a run of 1, then the length 9, then the length 10. The first ends
after 6 bits, the second after 8. `OWN_QUAD_NONE`, 11, is no length: it marks a missing second one, and
the decoder handles it without a branch ([chapter 13](book:#ch-literals-dec--own)). A run longer than 7
gets only bit 31, and the decoder takes its symbol from `decode[]`.
@anim ownread How the decoder reads a page's own table with quad[]

@@ 112-117
The three forms of `OWN`. `decode[]` is indexed by the next 8 bits, as the token table by the next 11.
Here the code's length is in the high byte of both `decode[]` and `enc[]`, the other way round from
`struct lit_table`'s `enc[]`. [Chapter 8](book:#ch-codes--own) builds all three.

## All tables in one global

@@ 118-128
One `const` global, written as C when seqlz is built, in the kernel as in userspace. It sits in
read-only data, nothing writes it at run time, and nothing has to build it before the first page.
Lengths that are no complete code stop the build instead ([chapter 8](book:#ch-codes--all-symbols)).

A page reads much less than all of it. The decoder reads `token.decode` (8 KiB), the `decode` of `ll`
and `ml` (1 KiB each), and with coded literals one literal table's `decode` (2 KiB): 12 KiB, which fits
the L1 cache. `lit_cost` comes after the literal tables with a little padding, since a `u64` needs 8-byte
alignment.

## The token entry

@@ 129-136
The asserts check the widths of the entry's fields, described next. `DROP` has 5 bits, at most 31, and
the most a token with a code moves the bitstream is 11 code bits plus class 3's 12 offset bits, 23.
`ll` has 4 bits, `ml` 6 bits for at most 35, the shift 2 bits.

`SEQLZ_OFF_SHIFT()` reads a whole nibble, so a shift of 4 fails here instead of being cut to 0 by the
`& 3U` of `TOK_SHIFT()`. The last assert: an escaped token is sent in 12 bits, which must hold every
token number.

@@ 137-153
The decoder looks up the token's entry with the next 11 bits and never decodes the token's number.
Everything it would compute from it is stored ready: `ll`, `ml` with its minimum of 4 added, the flag
for a length value, and three shift counts that cut the offset out of the bit register.

How the shifts work, for 2 literals and a match of 7 at offset 16: class 4, offset sent as `16 / 8 = 2`
in 5 bits, so `BACK` is `64 - 5 = 59` and `SHIFT` is 3. The register has the code on top, then the 5
bits. `bits << N` drops the code, `>> 59` keeps the 5 bits, `<< 3` multiplies by 8. `DROP`, `N + 5`,
moves the stream past both. The same three shifts work for every class.

On the Mi 9T's Cortex-A55 this layout took the decode loop from 16 547 to 15 539 instructions per page
(3rd October 2026); an in-order core gains time almost one for one with instructions saved.

@@ 154-168
Each macro shifts its field down and masks it to its width. Bits 30 and 31 the decoder tests directly,
and bit 29 is free. `TOK_N()` and `TOK_BACK()` are shift counts for a 64-bit register, so their
`& 63U` is again the proof that keeps `CONFIG_UBSAN_SHIFT` from adding a check.

@@ 169-185
Two callers, so it is in the header: `build_token()` fills the table with it when seqlz is built, and
the decoder calls it at run time for an escaped token, with `n = 0` because the bits are dropped
already. For the escape the entry is only bit 31 and `n` in `DROP`, so the decoder can drop the
escape's code before it reads the token's 12 bits.

`(64U - raw_bits) & 63U` is `BACK`. Class 0, the repeat offset, has no offset bits, and 64 is no valid
shift of a `u64` and does not fit 6 bits; `& 63U` makes it 0. So `BACK` is 0 only for class 0, and the
decoder uses it as the test, `off = back ? raw : last`, with no compare of the class. The `(u32)` cast
keeps every term of the OR unsigned.
