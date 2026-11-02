# Questions

Places where format.md could be read two ways, or does not say whether a block is valid.
Each one gives the section, the sentence, the readings, and the reading taken. Written while working.

## 1. Stream sizes: may `w` be larger than needed?

Section: *Layout of a compressed block*.
> `w = 5 + ((t >> 4) & 7)` the bits of each stream size

Readings: (a) any `w` from 5 to 12 whose `w` bits hold every `s[j]` is valid; (b) `w` must be the
smallest one that holds them.
Taken: (a). The rules for invalid blocks say nothing about `w` except `len < 3 + w + e`. "The
smallest `w` that holds every `s[j]`" only appears under *What the compressor writes*, which is
not part of the format. The writer picks any `w` from the smallest that holds the largest `s[j]`
(at least 5) up to 12.

## 2. Offset classes other than the cheapest one

Section: *Offsets*.
> The last column has the offsets of a valid block

Readings: (a) an offset may be sent in any class whose last column holds it (class 3 for offset 2,
class 2 for an offset equal to the last one, and so on); (b) only the class the compressor would use.
Taken: (a). The table gives each class's range, and the rules for invalid blocks are only about offset
0 and offsets above `|out|`. "The class with the fewest offset bits" is in *What the compressor
writes*. Class 0 is also allowed on the first sequence, where it gives `last = 1`.

## 3. Coded literals that cost more than raw ones

Section: *Layout of a compressed block*.
> There are two layouts: one with the literals stored as they are (**raw literals**), one with them coded

Readings: (a) either layout is valid for any `n` and any literals; (b) coded literals only where
they are smaller (the compressor's rule `m + d + s[0] + ... + s[7] < n - n div 16`).
Taken: (a); the size rule is in *What the compressor writes*. Same for `e = 1` where deltas do not
pay, and for a fixed table that is not the best one.

## 4. A block's own table that gives codes to byte values no literal uses

Section: *A block's own literal table*.
> they say the code length of each byte value with a code, in the order of the byte values

Readings: (a) any complete table is valid, whatever the literals are; (b) the table must fit the
literals (e.g. no codes for absent byte values).
Taken: (a). No rule ties the table to the literals; only the codes of the literals have to be in it.
The writer builds the table first and then picks each literal among the byte values with a code.

## 5. Splitting one run of byte values without a code into several run symbols

Section: *A block's own literal table*.
> symbol `9 + k`, for `k` from 1 to 9, then `r = read(k - 1)` from the table's bits: the next
> `2^(k-1) + r` byte values have no code.

Readings: (a) two or more run symbols may follow each other, so a run of e.g. 10 byte values may be
sent as 8 + 2, 2 + 8, 1 + 1 + ... ; (b) a run must be one symbol where one symbol can hold it.
Taken: (a). Nothing says runs must be maximal, and each symbol just advances the byte value. The
writer splits each run into a random list of parts, each from 1 to 255.

## 6. A block with `n = BLOCK`: must `B` be empty?

Section: *The bitstream of the sequences*.
> after the last bit it needs, fewer than 8 bits of `B` are left, all in its last byte, and they are 0.

Readings: (a) with no bits needed, `|B|` must be 0; (b) one byte of 0 may follow (all its bits are
"in its last byte" and 0, but 8 bits are not fewer than 8).
Taken: (a): "fewer than 8 bits" excludes a whole byte of padding, and *Decoding* says a block of
literals only has "no bits in `B`".

## 7. Where the bits of `value(LL)` go: before or after the offset bits

Section: *Decoding* (and *Tokens*).
> token: ll, mlf, c
> x = read(offset bits of class c)
> off = (c == 0) ? last : x as the offset of class c
> if ll == 15: ll = 15 + value(LL)

Readings: (a) token, offset bits, then `value(LL)`, then `value(ML)`, as the pseudocode reads them;
(b) the order of the token's fields, `ll`, `mlf`, `c`: `value(LL)`, `value(ML)`, then the offset bits.
Taken: (a). The pseudocode is the rule; *Tokens* only lists what the token holds. The example
(token, `read(1)`, then `symbol(ML)`) has the offset bit before `value(ML)`, which fits (a) and not
(b); it has no `value(LL)`, so for `value(LL)` only the pseudocode says it.

## 8. `dt = 3` in a block of literals only

Section: *Literals as deltas*.
> `x` the smallest multiple of `last` that is 4 or more, where `last` is the offset of the
> sequence before, 1 for the first

Readings: (a) with no match at all, the only sequence is the first one, and `last = 1`, `x = 4`;
(b) "the last sequence's literals the offset of the last match" has no match to take, so such a
block is not defined or not valid.
Taken: (a): `last` starts as 1 in *Decoding*, and nothing makes `e = 1` invalid without matches.
The writer makes such blocks (n = 4096, `e = 1`, `dt = 3`), with `x = 4`.

## 9. Class 0 on the first sequence

Section: *Offsets*.
> | 0 | none | the last offset again | the offset of the sequence before |

Readings: (a) valid, the offset is the starting `last`, 1 ("The last offset starts as 1 for each
block"); (b) invalid, as there is no sequence before.
Taken: (a); the start value is given exactly so that class 0 is defined on the first sequence, and the
invalid offsets are only 0 and those above `|out|`. Needs `ll >= 1`, which every first sequence has.
