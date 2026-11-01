# Features

Every choice that format.md leaves open, the section that allows it, and how `cr_write()` picks it from
the choice bytes. The names in **bold** are the items that `cr-gen` counts (family, then item); the
families are defined in `cr_features.def`.

## How choice bytes become choices

- `pick(k)`, a choice among `k` options: no byte if `k = 1`, one byte modulo `k` if `k <= 256`, else two
  bytes (low, high) modulo `k`. Bytes after the end of `choices` are 0, and option 0 is always valid,
  so any choice bytes, also none, give a valid block.
- `range_e(lo, hi)`, a number in `[lo, hi]`: if it has 3 or more values, one `pick(4)` first: 0 gives `lo`,
  1 gives `hi`, 2 and 3 a `pick(hi - lo + 1)` over all of it. So the ends of a range, which are the
  edge cases (offset 1, the offset back to byte 0, a match to the end of the block, the largest extra
  bits of a length symbol, ...) come often, and every value can still be picked.
- Every choice is among all the options that the format allows at that point *and* that can still be
  completed to a valid block; nothing else limits it. So every valid block (under the readings of
  QUESTIONS.md) is reachable from some choice bytes. The per-block parameters below (end chance,
  length windows, own-table preferences) only change how likely options are; their widest settings
  allow every option.

## The order in which choices are read

1. `pick(2)`: raw (0) or coded (1) literals.
2. Coded: `pick(9)` the `code`, `pick(2)` `e`, with `e = 1` `pick(3)` `dt - 1`.
3. `code = 8`: the block's own table (see below), its lengths, then its description.
4. Per block: `pick(14)` `ek`, the chance to end before each sequence, `2^-ek` (0: end at once, 13:
   never end early); `pick(8)` the mode: 0 is "the shortest sequences" (both length windows
   `[0, 0]`), else four `range_e` give the windows `[lo, hi]` of length categories for `ll` (0..38)
   and `ml` (0..54).
5. Per sequence, while at least `4` bytes (5 for the first) are left: the end decision
   (`pick(2^ek)`, 0 ends); `ll`; its literals; the class; the offset; `ml`; whether to escape.
6. The last sequence's literals, all the bytes left.
7. Coded: `w`.

Length categories: for `ll`, category `c < 15` is the ll field `c`, category `15 + s` is the ll field 15
with `LL` symbol `s`; for `ml`, category `c < 31` is `mlf = c`, category `31 + s` is `mlf = 31` with `ML`
symbol `s`. A length is picked as a category, `pick` among the categories in the block's window that
hold a length that fits (the nearest that fits, if none in the window does), then its value in that
category's range, cut to what fits, with `range_e`. A length has exactly one encoding, so the
category decides the field and symbol.

## Layout of a compressed block

| item | what | how picked |
| --- | --- | --- |
| **layout: raw**, **layout: coded** | `h & 0x8000` | `pick(2)` |
| **code=0** .. **code=8** | `t & 15`: one of the 8 fixed literal tables, or the block's own | `pick(9)` |
| **e/dt: e=0**, **dt=1**, **dt=2**, **dt=3** | `t >> 7`, and the byte `dt` | `pick(2)`, then `1 + pick(3)` |
| **w=5** .. **w=12** | the bits of each stream size | after the streams: `wmin + pick(13 - wmin)`, `wmin` the smallest `w >= 5` with every `s[j] < 2^w` |
| **w vs smallest: smallest / larger** | `w` larger than needed (QUESTIONS 1) | as above |
| **stream: empty** | a stream with no literals, `s[j] = 0` (n < 8) | follows from `n` |
| **stream: pad 0 bits / pad 1-7 bits** | a stream's codes end on a byte boundary or not | follows from the literals |
| **n: 1, 2-15, 16-255, 256-4095, 4096** | the number of literals, the 15 bits of `h` | follows from the sequences |
| **raw literal value** (summary) | each byte value as a raw literal | one byte each, `pick(256)` |

The sizes `s[j]` are not choices: a stream must end in its last byte ("Coded literals").

## A block's own literal table

The table is any complete prefix code over byte values 0..255 with lengths 1 to 10. Per table:
`pick(9)` `zq`, the chance `zq/8` that a byte value gets no code; `range_e(1, 10)` and
`range_e(lo, 10)` a preferred range of lengths `[lo, hi]`. Then for each byte value from 0 until the
code space is full: the options are "no code" and each length `l` such that the code space left can
still be filled exactly by the byte values left (`fewest codes for the space <= values left`); if "no
code" is an option, `pick(8) < zq` takes it; else `pick` among the possible lengths in `[lo, hi]`, or
among all possible lengths if none is in it. Then the description is written: a length `l` as `OWN`
symbol `l - 1`; each gap of `z` byte values without a code is split into runs: `k = 1 + pick(kmax)`
with `kmax` the largest `k` with `2^(k-1) <= z`, `x = pick(min(2^(k-1) - 1, z - 2^(k-1)) + 1)`, repeated
until the gap is covered, so a gap can be sent in every way.

| item | what | how picked |
| --- | --- | --- |
| **OWN symbol=0** .. **=9** | a byte value with a code of 1 .. 10 bits | the lengths above |
| **OWN symbol=10** .. **=17** | a run of `2^(k-1) + x` byte values with no code, `k = 1..8` | the splitting above |
| **OWN symbol=18** | a run with `k = 9`, 256 to 511 values | **unreachable**: a code must follow the run, at a byte value above 255 (QUESTIONS 5) |
| **own table: runs adjacent** | two runs in a row (QUESTIONS 3) | the splitting above |
| **own table: code no literal uses** | a byte value with a code that no literal has (QUESTIONS 4) | follows from the table and the literals |
| **own table: values after last code** | the list ends before byte value 255 | follows from the lengths |
| **own table: pad 0 bits / pad 1-7 bits** | the description ends on a byte boundary or not | follows |
| **own table: codes=2, 3-15, 16-255, 256** | the number of byte values with a code | follows from `zq`, `[lo, hi]` and the picks |

## Coded literals and literals as deltas

| item | what | how picked |
| --- | --- | --- |
| **literal code bits=1** .. **=10** | literals with codes of 1 .. 10 bits | follows from the literal and the table |
| **stored literal, 256 * code + value** (summary) | each stored value under each table | `pick` among the byte values that have a code in the table: 256 for the fixed tables, the coded ones for the own table |
| **delta literal: p<x stored as is** | a literal at `p < x` with `e = 1` | follows from the position |
| **delta literal: p>=x delta** | a literal stored as its delta to `out[p - x]` (QUESTIONS 2) | follows |
| **dt=3 last: last=1, last=2, last=3, last>=4** | with `dt = 3`, `x` = 4, 4, 6, `last` | follows from the offset of the sequence before |

With `e = 1` the writer picks the *stored* value; the byte in the block is the stored value plus
`out[p - x]`. Every stored value, and so every block byte, can be picked.

## Tokens, offsets, length values, decoding

| item | what | how picked |
| --- | --- | --- |
| **matches: 0, 1, 2-15, 16-255, 256-1022, 1023** | the number of sequences with a match | the end decisions; 1023 is the most a block can have |
| **last sequence: 0 literals, 1-3 literals, 4+ literals** | the last sequence's literals; 0 is a match to the end of the block | follows; fewer than 4 (5 at the start) bytes left ends the block |
| **token sent: with its code** | a token with a code of `TOK` sent with it | `pick(2)` = 0 if it has a code |
| **token sent: escaped, has a code** | the escape, 3072, then the token in 12 bits, although it has a code | `pick(2)` = 1 |
| **token sent: escaped, has no code** | the escape for a token without a code | forced |
| **token (any way)**, **token sent with its code** (summaries) | each of the 3072 tokens; each of the 640 with a code, sent with it | follows from `ll`, `ml` and the class |
| **ll field=0** .. **=15** | `token & 15` | the `ll` category |
| **mlf=0** .. **=31** | `(token >> 4) & 31` | the `ml` category |
| **LL symbol=0** .. **=23** | `ll - 15` as a length value | the `ll` category, then its extra bits by `range_e` |
| **ML symbol=0** .. **=23** | `ml - 35` as a length value | the `ml` category, then `range_e` |
| **ll of a sequence with a match**, **ml** (summaries) | each value | as above: `ll` from 0 (1 in the first sequence) to the bytes left minus 4, `ml` from 4 to the bytes left |
| **class=0** .. **=5** | `token >> 9` | `pick` among the classes that can send an offset in range: 0 always (QUESTIONS 7), 1 with 2 or more bytes written, 2 and 3 always, 4 and 5 with 8 or more |
| **offset of class 2, 3, 4, 5** (summaries) | each offset a class can send | class 0: `last`; 1: 2; 2: `range_e(1, min(255, o))`; 3: `range_e(1, min(4095, o))`; 4: `8 * range_e(1, min(31, o / 8))`; 5: `8 * range_e(1, min(255, o / 8))`, `o` the bytes written, after the literals (QUESTIONS 6) |
| **offset: class 0 in first sequence (1)** | class 0 before any offset, meaning 1 | the class pick |
| **offset: class>0 equal to last** | the last offset sent with its bits, not class 0 | the class and offset picks |
| **offset: off<ml (overlap)** | a match that copies bytes it writes itself | follows |
| **offset: off = bytes written (from byte 0)** | a match from the first byte of the block | `range_e`'s upper end |
| **offset: off=1** | a run of one byte | `range_e`'s lower end, class 0 in the first sequence |
| **offset: match reaches block end** | `ml` = all bytes left | `range_e`'s upper end |
| **B: empty / pad 0 bits / pad 1-7 bits** | no sequence bits (a block of literals only); `B`'s bits end on a byte boundary or not | follows; the padding bits must be 0 and `B` must end in its last byte, so they are not choices |

Not choices, because the format fixes them: `h`, the sizes `s[j]` and `d` (the codes must end in the
last byte), the padding bits (0), the length of `B`, the encoding of a given length (one category per
value), the canonical codes. The literals of the last sequence are all the bytes left.

Reachable values in the summaries: tokens 3072 of 3072; tokens with a code 640 of the 3072 numbers; `ll`
0..4092, `ml` 4..4095; class 2 offsets 1..255, class 3 1..4092 (a match of 4 must follow), class 4
the 31 multiples of 8 up to 248, class 5 the 255 up to 2040; stored literals 9 * 256 (the own table can
give every value a code); raw literals 256.
