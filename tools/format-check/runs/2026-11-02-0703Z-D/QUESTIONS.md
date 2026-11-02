# Questions while writing the decoder from format.md

Each entry: section, the sentence, the readings, the one taken and why, and whether other text decided
it. Entries marked **[took long, not ambiguous]** are right as written but took a while to understand.

The tables in `tables/` all match the SHA-256 in format.md; each is complete (Kraft sum exactly 1),
and their longest codes are the ones in the table of "The tables" (TOK 11, LL 8, ML 7, LIT 10, OWN 8).
So "Every table is complete" holds, and no question arises of what to do with bits that are no code
of a fixed table.

## 1. Layout of a compressed block — packing of the stream sizes [took long, not ambiguous]

> The `w` bytes from byte `3 + e` on are one number `S` of `8 * w` bits, little endian, and
> `s[j] = (S >> (j * w)) & ((1 << w) - 1)`

The sizes are packed least significant bit first (as a little-endian integer), while every bitstream
in the format (B, literal streams, own table) is read most significant bit first. Easy to mix up.
Taken: exactly the formula, `s[0]` is the low `w` bits of `S`. Nothing else in the file exercises it.

## 2. Layout — the offset of the own table [took long, not ambiguous]

> | `3 + w + e` | `d` | with `code = 8` the block's own literal table ...

The table's length `d` is not stored; it is known only after the table is parsed, so the start of the
streams (`3 + w + e + d`) depends on parsing the own table first. The list of invalid conditions in
"Layout" names `3 + w + e + d + s[0] + ... + s[7] > len`, and "A block's own literal table" names
`3 + w + e + d > len` separately. Taken: both checks.

## 3. The bitstream of the sequences — order of bits within a sequence [took long, not ambiguous]

> `token: ll, mlf, c` / `x = read(offset bits of class c)` / ... `if ll == 15: ll = 15 + value(LL)` /
> ... `if mlf == 31: ml = 35 + value(ML)`

The bits of one sequence are: token (possibly escape + 12 bits), then the offset bits, then the `LL`
value, then the `ML` value — the offset comes *before* the lengths, not after as one might expect
from lz4/zstd. Taken: the order of the pseudo-code. The example confirms offset bits before the `ML`
value (`read(1)` then `symbol(ML)`); it has no `LL` value, so the position of `LL` between offset and
`ML` is decided by the pseudo-code alone.

## 4. Decoding — order of the validity checks

> `if ll > n - used or ll > BLOCK - |out|: invalid` ... `if off == 0 or off > |out| or ml > BLOCK - |out|: invalid`

Since the decoder only answers valid/invalid, the order in which the checks are made does not change
the answer as long as all are made; the only order that matters is which `|out|` the offset is
compared with: the one *after* this sequence's literals were copied ("one larger than `|out|` after
this sequence's literals are copied", section Offsets). Taken: the pseudo-code as written.

## 5. The bitstream — the end condition

> after the last bit it needs, fewer than 8 bits of `B` are left, all in its last byte, and they are 0.

Readings: (a) `0 <= 8*|B| - used_bits < 8` and those bits are 0; (b) also allow trailing whole zero
bytes. Taken (a): "fewer than 8 bits" is explicit. Decided further by Decoding: "a block of literals
only has `n = BLOCK` and no bits in `B`", i.e. an empty B is valid when no bit is needed, and one zero
byte after it (8 bits left) is not. A block that needs no bits of B and has an empty B is valid.

## 6. A block's own literal table — symbol 18

> Symbol 18 (`k = 9`), a run of 256 byte values or more, is in no valid block

Readings: (a) a separate rule: symbol 18 is invalid at once; (b) a consequence of the other rules.
Checked: it is a consequence. A run of `256 + r` starting at byte value `v` goes past 255 unless
`v = 0, r = 0`; in that case the run covers 0..255 with the sum still 0, the list cannot end, and the
next symbol either names a byte value 256 (> 255) or a run past 255: invalid. So (a) and (b) give the
same answer. Taken: treat symbol 18 as invalid at once (same result).

## 7. A block's own literal table — where its bits may come from

> Bits past `len` are read as 0, and `3 + w + e + d > len` then makes the block invalid.

The table's bits are read from byte `3 + w + e` on, through bytes that belong to the streams and B if
it were longer; only `len` bounds it. Taken: read the table from `src[3+w+e .. len)`, zeros after,
then `d = ceil(bits / 8)`, check `3 + w + e + d <= len` and the padding bits in byte `d - 1` are 0.

## 8. Coded literals — streams with literals that read past their size [took long, not ambiguous]

> not `8 * s[j] - 8 < bits <= 8 * s[j]`

Taken: decode stream `j` from `src[start[j] .. start[j] + s[j])` with zeros past its end, sum the code
lengths, and check the rule; reading zeros past the end is then the same as checking it (a code that
needs them makes `bits > 8 * s[j]`). This also makes a stream with literals and `s[j] = 0` invalid.

## 9. Literals as deltas — `last` for `dt = 3`

> `x` the smallest multiple of `last` that is 4 or more, where `last` is the offset of the sequence
> before, 1 for the first

Readings: (a) `last` is the decoder's variable `last` of Decoding at the time `copy()` runs; (b) the
offset of the previous *match* in some other sense (e.g. the decoded offset bits). Taken (a): the
section says so ("`copy(ll)` comes before `last = off`, so a sequence's literals use the offset of the
sequence before, as class 0 does"). With class 0 the offset is the previous one, so (a) and (b) agree.
For a block of literals only, `last` is 1 and `x = 4`.

## 10. Literals as deltas — `x` for `dt = 3` and large `last`

> `x = 4` for `last` 1 or 2, 6 for `last = 3`, else `last`.

"else last" for any `last >= 4` (up to 4095); with `p < x` the literal is stored as it is. Consistent
with "smallest multiple of `last` that is 4 or more". Not ambiguous.

## 11. Layout / Decoding — "Two things follow from the rules"

> Two things follow from the rules: a valid block has `n >= 1`, because the first match needs a byte
> before it, and the first match's offset is at most that sequence's `ll`.

Readings: (a) two extra rules to check; (b) consequences of the others. Taken (b): "follow from the
rules" says so, and they do (`n = 0` cannot reach `n - used == BLOCK - |out|` without a match, and
the first match's offset is checked against `|out| = ll`). No separate check; tests "raw, n = 0" and
"coded, n = 0" are invalid through the offset rule. Same result either way.

## 12. What the compressor writes — not rules

> Not part of the format: any block that decodes is fine

The section lists choices of `seqlz_compress()`: the smallest `w`, at least 5; the escape only for a
token without a code; coded literals only when they save enough (`m + d + ... < n - n div 16`); own
tables only for 512 literals or more; class 0 when the offset repeats, else the smallest class.
Readings: (a) a decoder may reject blocks that break them; (b) they are not rules. Taken (b), as the
text says; the decoder accepts e.g. a larger `w` than needed, an escaped token that has a code, a
non-minimal class, an own table for 16 literals. Decided by the text. Tests: "coded, w = 10..12",
"every class, all tokens escaped", "token with a code" / "the same token escaped".

## 13. Length values — the range `0 .. 2^P - 1` and the *Why*

> A length value is a number from 0 up to `2^P - 1`. ... *Why `2^P - 1`:* the largest value a block
> needs is `ll - 15` of `BLOCK - 4` literals ... `BLOCK - 19`

Readings: (a) a rule that a value above `BLOCK - 19` is invalid; (b) only an explanation. Taken (b):
it is a *Why* and not part of the format; the symbols 0..23 cannot give more than 4095 anyway. Too
large `ll` or `ml` are invalid by the checks of Decoding (`ll > n - used`, `ml > BLOCK - |out|`).

## 14. Decoding — more literals left than bytes left [took long, not ambiguous]

> `if n - used == BLOCK - |out|:` the last sequence

The test is an equality. After a long match the literals left can exceed the bytes left (e.g.
`n = 100`, first sequence `ll = 1`, `ml = 4000`); then the loop does not stop, reads another token,
and the block becomes invalid by `ll > BLOCK - |out|`, `ml > BLOCK - |out|` or by bits past the end
of B. That is why both halves of `ll > n - used or ll > BLOCK - |out|` are needed; tests "ll 96 with
95 bytes left and 99 literals" etc. The loop still ends: every sequence writes at least 4 bytes
("a decoder stops after at most `BLOCK / 4 + 1` sequences, also on blocks that are not valid").

## 15. Tokens — an escaped number 3072 or more [not ambiguous, noted]

> `if token >= 3072: the block is invalid`

Without this rule such a token would have class 6, for which the table of Offsets has no row. The
test "escaped token 3072 + 0x1f8 (class 6)" makes a block that would be valid if class 6 were read
as class 5, so the rule is checked on its own.

## 16. Rules that never change the answer on their own [took long, not ambiguous]

Found by removing each check from the decoder in turn and running the tests (and reasoning about why
nothing failed). Each rule is in the decoder as format.md lists it, but on its own it cannot change
the answer:

- A block's own literal table, "the sum goes above 1": when the sum is above 1 it can never be exactly
  1 again, the list cannot end, and every following symbol eventually names a byte value above 255 or
  a run past 255.
- "a run of byte values without a code goes past 255": a run never ends the list (only a code does),
  so the next symbol is for byte value 256 or more: invalid too. Same for symbol 18 (entry 6).
- "`3 + w + e + d > len`": then `3 + w + e + d + s[0] + ... + s[7] > len` of the Layout fails as
  well. But the check matters for memory safety: without it the decoder would read the padding bits
  of a last table byte that is past `len` (test "own table (23 bits) cut by len, n = 0" catches that
  under ASan).
- At the end of B, "no bit read past the end of B" with "fewer than 8 bits left": in the decoder
  both are one subtraction of sizes; removing the first alone is caught by the second via unsigned
  wrap-around, so it is kept explicitly.

So the rules are not contradictory; format.md lists some for clarity ("A run past 255 makes the block
invalid at once, the list does not go on after it" says as much).

## 17. The comments "in the kernel's copy" [not ambiguous, noted]

format.md has HTML comments with text for a kernel copy. They repeat the table of tables with another
header path and the same lengths, symbols and longest codes; nothing in them changes the format.
Ignored.
