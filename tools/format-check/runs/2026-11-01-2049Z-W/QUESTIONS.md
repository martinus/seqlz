# Questions

Places where format.md has two readings, or does not say whether a block is valid. For each:
the section, the sentence, the readings, the one taken and why. Written while working, in the
order met.

## 1. Layout of a compressed block — may `w` be larger than needed?

> `w = 5 + ((t >> 4) & 7)` the bits of each stream size

The rules of validity say nothing about `w` except that it is 5 to 12. Only "What the compressor
writes", which is not the format, says "the smallest `w` that holds every `s[j]`, at least 5".

- Reading A: any `w` from 5 to 12 whose `w` bits hold every `s[j]` is valid.
- Reading B: only the smallest such `w` is valid.

Taken: A. The list of what makes a block invalid does not name `w`, and the compressor section is
declared not part of the format. The writer picks any `w` from the smallest that holds every `s[j]`
(at least 5) up to 12.

## 2. Literals as deltas — is "of the match before or a literal of the same sequence" a rule?

> `out[p - x]` is a byte already written, of the match before or a literal of the same sequence, as it
> is in the block.

With `dt = 2` (`x = 8`) after a match of 4 bytes, `out[p - 8]` of the first literal is before that match,
in an earlier sequence; with `dt = 3` and a large `last` it can be far back too.

- Reading A: a description of where the byte usually is; any `p >= x` takes `out[p - x]` wherever it is.
- Reading B: a rule, and a block where `out[p - x]` is elsewhere is invalid.

Taken: A. The decoding pseudo-code and the list of invalid blocks have no such check, the sentence
begins by saying what the byte is ("a byte already written"), and with `dt = 2` reading B would make
most blocks with short matches invalid, which the rest of the section does not suggest.

## 3. A block's own literal table — may two runs of byte values without a code follow each other?

> symbol `9 + k`, for `k` from 1 to 9, then `x = read(k - 1)`: the next `2^(k-1) + x` byte values
> have no code.

- Reading A: yes; e.g. a run of 3 may be sent as one run (`k = 2`, `x = 1`) or as 1 + 2, 2 + 1, 1 + 1 + 1.
- Reading B: a run must be as long as possible, the gap between two codes being one symbol.

Taken: A. Nothing makes such a block invalid; the list of invalid cases is explicit (sum above 1, byte
value above 255, non-zero bits after the end, `3 + w + e + d > len`). The writer splits gaps in all ways.

## 4. A block's own literal table — may it give codes to byte values that no literal uses?

> the code length of each byte value with a code

- Reading A: yes, the table is any complete code; only the literals that occur must have a code.
- Reading B: only byte values that occur as (stored) literals may have a code.

Taken: A. No rule of validity asks for it; a literal without a code simply cannot be written, so the
writer chooses the table first and then the literals among the byte values that have a code.

## 5. A block's own literal table — symbol 18 (`k = 9`)

> symbol `9 + k`, for `k` from 1 to 9

A run of `k = 9` is 256 to 511 byte values. Since the list only ends when the codes fill the code space,
a run must be followed by a code, at byte value at least 256, which is invalid. Not two readings, but
a symbol of `OWN` that no valid block can contain: the writer never writes it, and FEATURES.md lists it
as unreachable. (Runs of `k = 8`, 128 to 255 values, are possible: e.g. byte values 0 to 253 without a
code, as runs of 128 and 126, then 254 and 255 with codes of 1 bit.)

## 6. Offsets — "the bytes written so far": before or after this sequence's literals?

> For a sequence with a match, an offset of 0, or one larger than the bytes written so far, makes the
> block invalid.

- Reading A: the bytes written including this sequence's `ll` literals (the match starts after them).
- Reading B: the bytes written before the sequence began.

Taken: A. The pseudo-code in "Decoding" checks `off > |out|` after `copy(ll)`, and "Two things follow
from the rules" says "the first match's offset is at most that sequence's `ll`", which only holds in
reading A. The writer allows offsets up to `|out|` after the literals, `off = |out|` included.

## 7. Offsets — class 0 in the first sequence

> | 0 | none | the last offset again | the offset of the sequence before |

The first sequence has no sequence before.

- Reading A: class 0 in the first sequence is valid and means offset 1, from "The last offset starts as
  1 for each block".
- Reading B: class 0 needs a sequence before, and is invalid in the first one.

Taken: A. The sentence "The last offset starts as 1 for each block" is there for exactly this, the
pseudo-code starts with `last` = 1 and has no check, and a first sequence always has `ll >= 1`, so
offset 1 is always in range. The writer offers class 0 in every sequence, also the first.
