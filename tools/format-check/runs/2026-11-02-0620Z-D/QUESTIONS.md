# Questions while writing the decoder from format.md

Each entry: the section, the sentence, the readings, the one taken and why, and whether other text
in format.md decides it. Entries marked **[took long, not ambiguous]** are right as written but took
time to understand.

## 1. The bitstream of the sequences — reading past the end of `B`

> "A valid block ends where its bits end: after the last bit it needs, fewer than 8 bits of `B` are
> left, all in its last byte, and they are 0."

Readings: (a) if decoding needs more bits than `B` has, the block is invalid; (b) bits past the end
are zeros and such a block may still be valid as long as the "fewer than 8 left" check passes in
some sense (e.g. "left" clamped to 0).
Taken: (a). Decided by the next sentences: "A decoder may read zeros past the end instead of
checking at every read, and check at the end that it did not use them; ... the result is the same
as checking at every read." So using a bit past `len` is invalid.

## 2. The bitstream of the sequences — "fewer than 8 bits ... all in its last byte"  **[took long, not ambiguous]**

With `n = BLOCK` and `B` one byte `00`, no bit is needed and 8 bits are left: that is not fewer than
8, so the block is invalid. With `n = BLOCK` only `len = 2 + 4096` (raw) is valid. "All in its last
byte" is implied by "fewer than 8" plus "after the last bit it needs" (the unused bits are the tail
of the byte holding the last used bit; with 0 used bits and `|B| = 0` there are none). Confirmed by
"a block of literals only has `n = BLOCK` and no bits in `B`".

## 3. Layout of a compressed block — `w` used as a byte count  **[took long, not ambiguous]**

> "| `3 + e` | `w` | `s[0]` to `s[7]`, the size of each literal stream, `w` bits each |"

`w` is the bits of each size, and also the size in bytes of the field: 8 sizes of `w` bits are `w`
bytes. Stated in the next paragraph ("8 sizes of `w` bits fill exactly `w` bytes"). `S` can be up
to 96 bits, so it is not one machine integer; the decoder extracts bit `j*w + i` of `S` as bit
`(j*w+i) mod 8` of byte `3 + e + (j*w+i) div 8`.

## 4. Decoding — order of reads within a sequence

> "token: ll, mlf, c / x = read(offset bits of class c) / ... / if ll == 15: ll = 15 + value(LL) /
> ... copy(ll) / ... if mlf == 31: ml = 35 + value(ML)"

The order in `B` is: token (and its 12 escape bits), offset bits, `LL` value, `ML` value. One could
think the length values come before the offset (as in some other formats). Taken: the pseudocode
order. The example confirms offset bits before the `ML` value (its `read(1)` comes before
`symbol(ML)`); it has `ll = 2`, so it does not confirm offset before the `LL` value — that rests on
the pseudocode alone.

## 5. Decoding — order of the validity checks

The checks `ll > n - used`, `off == 0`, `off > |out|`, `ml > BLOCK - |out|` come at different
points. Since the result is only valid/invalid and every check must pass for a valid block, the order
does not change the result. `off > |out|` uses `|out|` after `copy(ll)` (pseudocode order); taking
it before the copy would reject valid blocks (e.g. the example: `|out| = 0` before the copy,
`off = 2`). Decided by the pseudocode and the example.

## 6. Coded literals — streams with and without literals

> "If stream `j` has literals, their codes take `b` bits with `8 * s[j] - 8 < b <= 8 * s[j]` ...
> A stream without literals, stream `n` to 7 when `n < 8`, has `s[j] = 0`. Else the block is invalid."

Readings for "Else": applies to both conditions (the bits rule and `s[j] = 0` for empty streams).
Taken: both are required. A stream with literals and `s[j] = 0` fails `b <= 0`. Not otherwise
decided by the example (it is raw).

## 7. Literals as deltas — `x` for `dt = 3` uses `last` at the time of the copy  **[took long, not ambiguous]**

`x` is fixed for the whole `copy(ll)` of one sequence: `last` changes only after the copy. For the
first sequence and for every sequence after a class-0 match, the rule is the same: the value of the
variable `last` at the time of the copy. The last sequence uses the last match's offset. Note
`last = off` is executed even for a sequence that then turns out invalid; irrelevant for the result.

## 8. Literals as deltas — raw layout

`e` exists only in the coded layout. With raw literals there is no `dt` and literals are as they are
("with raw literals and with `e = 0` as they are"). Not ambiguous, noted because the compressor
section says a delta choice is undone when literals end up raw.

## 9. A block's own literal table — "a run of byte values without a code goes past 255"

Readings: (a) invalid if the run's last byte value is above 255, i.e. `v + count > 256`; (b) invalid
if the run reaches 255 or beyond. Taken (a): "past 255" means beyond 255. A run ending exactly at
byte value 255 is not by itself invalid, but the list has not ended (sum < 1 because a run is never
the symbol that completes the sum), and any next symbol is either a code for value 256 (invalid) or a
run past 255 (invalid), so both readings give the same result. Not affecting the result.

## 10. A block's own literal table — where its bits end for `d`, and reading past `len`

> "Bits past `len` are read as 0, and `3 + w + e + d > len` then makes the block invalid."

`d = ceil(bits_used / 8)`. The table's bits may run into the bytes that later belong to the streams
only in the sense that the streams start after `d`; there is no overlap. Taken as written.

## 11. A block's own literal table — `n` = 0 or few literals with `code = 8`

The table is read and checked regardless of how many literals there are. A block with `n = 0` is
anyway invalid (first match has no byte before it). Taken: always parse and check the table when
`code = 8`.

## 12. Prefix codes — fixed tables complete

> "Every table is **complete**"

Checked by the generator script for TOK, LL, ML, the 8 literal tables and OWN; all have Kraft sum
exactly 1. So `symbol()` on a fixed table always finds a code (reading zeros past the end).

## 13. What the compressor writes — which of its habits are rules

> "Not part of the format: any block that decodes is fine, and one block can be written in many
> ways"

The section lists things a decoder could be tempted to check: "the smallest `w` that holds every
`s[j]`, at least 5", "the escape only for a token without a code", "coded literals only when
`m + d + ... < n - n div 16`", "each offset with class 0 when it is the last one, else the class with
the fewest offset bits", own tables only for blocks with 512 literals or more, own-table code
lengths "at most 10 bits". Readings: (a) these are also validity rules; (b) they are not. Taken (b):
the section says so in its first sentence, and "Tokens" says "Any token can be sent escaped, also one
that has a code of its own; it means the same". The decoder accepts any `w` 5..12 that fits, escaped
tokens with a code, coded literals that save nothing, offsets in any class that can hold them. The
"at most 10 bits" for own-table codes is a rule anyway, by the format: `OWN` symbols only say lengths
1 to 10.

## 14. The two copies of the text (kernel's copy and this file)

The file has HTML comments with text "only for the kernel's copy" and blocks "not in the kernel's
copy". Readings: the format is (a) the union, (b) only what both copies have. Taken: the normative
text is the same in both (the table of tables is duplicated identically except for the header-file
path); nothing format-relevant is only in one copy, so the choice does not matter. Not decided by
other text; checked by reading both.

## 15. Coded literals — "A decoder may read past the end of a stream, as long as it checks this."  **[took long, not ambiguous]**

Whether the bits read past a stream's end are the next stream's bytes or zeros does not change the
result: once a code starts or extends past `8 * s[j]`, `b > 8 * s[j]` and the block is invalid either
way. The decoder gives each stream a reader limited to its own `s[j]` bytes that reads zeros past it.

## 16. A block's own literal table — the 257th symbol and symbol 18  **[took long, not ambiguous]**

> "a 257th always makes the block invalid: the list always ends" / "Symbol 18 (`k = 9`) ... is in no
> valid block"

These are not extra rules, they are consequences: each symbol consumes at least one byte value, and a
code for value 256 or a run past 255 is invalid, so a 257th symbol is always one of those; symbol 18
is a run of `256 + x` values, `v` to `v + 255 + x`. That is past 255 for every start except `v = 0`,
`x = 0`, which covers exactly 0..255 and so does not itself "go past 255"; but the sum is then still
0, the list goes on, and the next symbol is a code for 256 or a run past 255, invalid either way. The
decoder implements only the four stated conditions and both statements follow; a test checks symbol
18 after a code (`v = 1`), which is invalid at once.

## 17. A block's own literal table — the sum reaching exactly 1 is checked after each length symbol  **[took long, not ambiguous]**

> "The list ends as soon as the codes fill the code space"

So a run symbol can never be the last; after the length symbol that makes the sum exactly 1, no
more symbols are read, even if the next bits would decode to something. Measured in units of 2^-10
(lengths are 1..10) the sum is an exact integer comparison against 1024.

## 18. Decoding — termination and the condition at the top of the loop  **[took long, not ambiguous]**

> "if n - used == BLOCK - |out|: the last sequence"

When `n - used > BLOCK - |out|` the loop goes on; once `out` is full with literals left, the next
token has `ll > 0 = BLOCK - |out|` or `ml >= 4 > 0`, so the block is invalid; no extra rule "too many
literals" is needed or added. When `n - used < BLOCK - |out|` a token is read as usual.

## 19. Prefix codes — fixed tables that give symbols no code

> "A table here holds only the **length** of each symbol's code, 0 for a symbol without a code."

`TOK` gives 2432 of its 3073 symbols no code (only 641 have one). Not ambiguous: those tokens can only
be sent escaped. Noted because `symbol(TOK)` can never return them directly.

## 20. Offsets — "an offset of 0 ... makes the block invalid" for each class

Classes 1, 2, 3, 4 and 5 can all send 0 (`read(1) * 2`, `read(8)`, `read(P)`, `read(5) * 8`,
`read(8) * 8` with all-zero bits); class 0 cannot (the last offset is always valid by then). The
table's column "offsets it can send" lists only the valid ones ("1 to 255", "8 to 248"...), which
could be read as saying 0 is not representable. Taken: 0 is representable and invalid, as the
sentence after the table says. Decided by that sentence.

## 21. Offsets — the class 5 column says "8 to 2040" and class 3 "1 to `BLOCK - 1`"  **[took long, not ambiguous]**

These are the ranges of the bits, not extra rules: an offset that the bits give is valid iff it is
not 0 and at most `|out|`. E.g. class 5 with 255 gives 2040, valid only if `|out| >= 2040`.

## 22. Layout — `e`, `w` and `code` when `n = 0` or `n` small with coded literals

With `h & 0x8000` and `n < 8`, the header, sizes and (with `code = 8`) the own table are still read
and checked; empty streams need `s[j] = 0`. Taken as written: no special case for small `n`.
Not decided by the example.
