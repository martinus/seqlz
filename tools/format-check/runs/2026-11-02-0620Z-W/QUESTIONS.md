# Questions

Places where format.md could be read two ways, or does not say whether a block is valid. For each:
the section, the sentence, the readings, the reading taken and why. Written while working, in the
order the questions came up.

## 1. Offsets — when is "the bytes written so far" counted

> For a sequence with a match, an offset of 0, or one larger than the bytes written so far, makes the
> block invalid.

- (a) the bytes written before the sequence, before its literals are copied;
- (b) the bytes written when the match starts, after the sequence's `ll` literals.

Taken: (b). The pseudocode in "Decoding" checks `off > |out|` after `copy(ll)`, and "Decoding" says
"the first match's offset is at most that sequence's `ll`", which only holds with (b). The writer
lets a match's offset reach back into the literals of its own sequence.

## 2. Offsets — class 0 in the first sequence

> | 0 | none | the last offset again | the offset of the sequence before |
> The last offset starts as 1 for each block.

- (a) class 0 needs a sequence before it, so the first sequence cannot use it;
- (b) class 0 in the first sequence gives the starting value, offset 1.

Taken: (b). "The last offset starts as 1 for each block" only matters if it can be used, and the
pseudocode `off = (c == 0) ? last : ...` with `last` starting at 1 has no exception. The writer uses
class 0 in the first sequence (offset 1, needs `ll >= 1`, which the first sequence always has).

## 3. Layout — must `w` be the smallest that holds the sizes

> `w = 5 + ((t >> 4) & 7)` the bits of each stream size

- (a) any `w` from 5 to 12 for which every `s[j] < 2^w`;
- (b) only the smallest such `w`.

Taken: (a). The rules for invalid blocks in "Layout of a compressed block" do not mention it, and
"the smallest `w` that holds every `s[j]`, at least 5" is in "What the compressor writes", which is not
part of the format. The writer picks `w` from every value that holds the largest `s[j]`.

## 4. A block's own literal table — several run symbols in a row

> symbol `9 + k`, for `k` from 1 to 9, then `x = read(k - 1)` from the table's bits: the next
> `2^(k-1) + x` byte values have no code.

- (a) a run of byte values without a code is one symbol, the longest it can be;
- (b) a run can be sent as several run symbols one after the other, e.g. 5 as 4 then 1.

Taken: (b). Nothing in the list of invalid tables forbids two run symbols in a row; each symbol just
says "the next ... byte values have no code". The writer splits each run into pieces of sizes picked by
the choice bytes.

## 5. Coded literals — a coded layout where it does not pay

> `code` 0 to 7 is fixed literal table `code` ... `code` 8 is a table of the block's own

- (a) coded literals only where they are smaller than raw ones (as the compressor writes them);
- (b) coded literals for any block, any table, any `n` from 1 to 4096.

Taken: (b). The condition `m + d + ... < n - n div 16` is in "What the compressor writes", which is not
part of the format, and no rule of invalid blocks depends on it. The same for a block's own table on
blocks below 512 literals, and for deltas that take more bits than the literals as they are.

## 6. A block's own literal table — byte values with a code that no literal uses

> It is a list of symbols of the table `OWN`, which say the code length of each byte value with a code

- (a) the table may give codes to byte values that no literal of the block has;
- (b) only to byte values the literals use (as a Huffman code of the block would).

Taken: (a). Only completeness and the listed rules are required; "Its code lengths are close to a
Huffman code's" is in "What the compressor writes". The writer builds any complete code with lengths
1 to 10 over any 2 to 256 byte values, and picks literals among the byte values with a code.

## 7. Literals as deltas — with a block's own table, and where no literal is a delta

> | 2 | 1 | `t`: `code = t & 15` is how the literals are coded, ... `e = t >> 7` whether they are deltas |

- (a) `e = 1` only with the fixed tables, or only where some literal is stored as a delta;
- (b) `e = 1` with any `code` from 0 to 8, also when every literal has `p < x` (e.g. `n = 1`, or all
  literals in the first 3 bytes and the last sequence empty), so that `dt` changes nothing.

Taken: (b). The rules for invalid blocks only ask that `dt` is 1, 2 or 3; `code` and `e` are separate
fields of `t`, and the introduction says level 3 uses both its own table and deltas. The writer picks
`e` and `dt` independently of `code` and of the literals.
