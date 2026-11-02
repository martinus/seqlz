# Features

Every choice that format.md leaves open, with the section that allows it, how the choice bytes
pick it in `cr_write()` (cr.c), and the name under which `cr-gen` counts it.

## How choice bytes are turned into choices

`cr_write()` reads `choices[0..n)` in order, 0 after the end. Every choice is a number in `[0, R)`:

- `pick(R)`: for `R = 1` (a forced choice) no byte is read; for `R <= 256` one byte, `byte mod R`; for
  larger `R` (at most 4096) two bytes, `(b0 + 256 * b1) mod R`. Every value is reachable.
- `pick_log(lo, hi)`: first one of the ranges `[2^b, 2^(b+1))` that meet `[lo, hi]`, by `pick`, then a
  number in that part of it, by `pick`. Small and large numbers are both frequent, and every number
  is reachable.
- `bucket(lo, hi)`: for `ll` and `ml`, see below: first a **bucket**, one per way a length is coded,
  among the buckets allowed for the block that meet `[lo, hi]`, then a value in it.

The writer simulates the decoder while it writes, so each choice is taken only among the values that
keep the block valid: it never needs to undo one, and any choice bytes give a valid block.

Order of the choices: layout, code, `e`, `dt`; a block's own table; three per-block choices that
shape the sequences (below); then for each sequence: whether it is the last one, `ll`, each literal,
class, offset, `ml`, escape; the literals of the last sequence; `w`.

## Layout (*Layout of a compressed block*)

| item | how it is picked | counted as |
| --- | --- | --- |
| raw literals | `pick(2) = 0` | `layout.raw` |
| coded literals | `pick(2) = 1` | `layout.coded` |
| `code` 0 to 8: fixed literal table 0 to 7, or a block's own table | `pick(9)` | `code.0` .. `code.8` |
| `e = 0` or `e = 1`, literals as they are or as deltas (*Literals as deltas*) | `pick(2)` | `e.0`, `e.1` |
| `dt` 1, 2, 3 | `1 + pick(3)` | `dt.1` .. `dt.3` |
| `w` 5 to 12, any from the smallest that holds every `s[j]` (at least 5) up to 12 | `wmin + pick(13 - wmin)`, picked last, when the stream sizes are known | `w.5` .. `w.12`, `w.above-smallest` |
| `n`, the number of literals, 1 to 4092 or 4096 | follows from the sequences: the sum of their `ll` and the last sequence's literals | |
| streams with no literals (`n < 8`), `s[j] = 0` | follows from `n` | `coded.empty-streams` |

Fixed by the format, no choice: bit 15 of `h` and `n`; `t` from `code`, `w`, `e`; every `s[j]`
(exactly the bytes of stream `j`'s codes); the padding bits (0); the length of `B`.
`n` is never 0 or 4093 to 4095: no valid block has those (a match needs a byte before it and is at
least 4 bytes).

## Sequences (*Decoding*, *The idea*)

Three per-block choices shape the sequences; they bias which values are frequent, and every
value stays reachable through some value of them:

- `endk = pick(13)`: at each sequence start where at least 4 bytes are left, the block ends there with
  chance `2^-endk`: `pick(2^endk) = 0` ends it. With fewer than 4 bytes left it must end.
- `llcap`: `pick(2) = 0` allows all 39 buckets of `ll`, else only the first `pick_log(1, 39)`.
- `mlcap`: `pick(2) = 0` allows all 55 buckets of `ml` plus a bucket "the match fills the block to its
  end", else only the first `pick_log(1, 55)`.

If no allowed bucket meets the range a sequence needs, the lowest bucket that meets it is taken.

| item | how it is picked | counted as |
| --- | --- | --- |
| a block of literals only (`n = 4096`, no token, `B` empty) | the end at the first sequence | `end.literals-only` |
| the last match reaches the end of the block (no literals left) | the end with 0 bytes left | `end.match-reaches-end` |
| the last sequence has literals | the end with bytes left | `end.last-literals` |
| number of sequences with a token, 1 to 1023 | follows from the end choices and lengths | `sequences.1`, `sequences.2-15`, `sequences.16-255`, `sequences.256-1023` |
| `ll`, 0 to `BLOCK - |out| - 4` (at least 1 in the first sequence, whose match needs a byte before it) | `bucket`: buckets `{0}` .. `{14}` (in the token), `{15 + v}` for LL symbols `v` 0 to 15, `[15 + 2^b, 15 + 2^(b+1))` for LL symbols 16 to 23 | `ll-field.0` .. `ll-field.15`, `LL-symbol.0` .. `LL-symbol.23` |
| `ml`, 4 to `BLOCK - |out|` | `bucket`: `{4}` .. `{34}` (in the token), `{35 + v}` for ML symbols 0 to 15, `[35 + 2^b, 35 + 2^(b+1))` for ML symbols 16 to 23, and with every bucket allowed also `{BLOCK - |out|}` | `mlf.0` .. `mlf.31`, `ML-symbol.0` .. `ML-symbol.23` |
| where a match copies from: offset 1 to `|out|` | below, by class | `offset.1`, `offset.equals-out` (from the block's first byte), `offset.below-ml` (copies bytes it writes) |

How a length value is coded (*Length values*) is not a choice: each value has one symbol and one
`read(b)`. Whether `ll` uses field 15 and `value(LL)` follows from `ll >= 15`, same for `ml >= 35`.

## Offsets (*Offsets*)

The class is `pick` among the classes that can send some offset now (`|out|` after this sequence's
literals): 0 always, 1 if `|out| >= 2`, 2 and 3 always, 4 and 5 if `|out| >= 8`. Then the offset:

| item | how it is picked | counted as |
| --- | --- | --- |
| class 0, the last offset again (also on the first sequence, `last = 1`) | no byte | `class.0`, `class.0-first-sequence` |
| class 1, offset 2 | no byte | `class.1` |
| class 2, offset 1 to 255 | `pick_log(1, min(255, |out|))` | `class.2` |
| class 3, offset 1 to 4095 | `pick_log(1, min(4095, |out|))` | `class.3` |
| class 4, offset 8 to 248 | `8 * pick_log(1, min(31, |out| / 8))` | `class.4` |
| class 5, offset 8 to 2040 | `8 * pick_log(1, min(255, |out| / 8))` | `class.5` |

Any class that holds the offset is allowed, not only the cheapest (QUESTIONS.md, 2).

## Tokens (*Tokens*)

| item | how it is picked | counted as |
| --- | --- | --- |
| a token with a code of its own sent with that code | `pick(2) = 0` | `token.own-code` |
| a token with a code of its own sent escaped | `pick(2) = 1` | `token.escape-has-code` |
| a token without a code, escaped (forced) | no byte | `token.escape-no-code` |

The token itself follows from `ll`, `ml` and the class. All 3072 tokens are reachable; 640 have a code.

## Literals (*Layout*, *Coded literals*, *Literals as deltas*)

| item | how it is picked | counted as |
| --- | --- | --- |
| each raw literal, 0 to 255 | `pick(256)` | |
| each coded literal: its symbol in the table, any of the 256 of a fixed table or any byte value with a code of a block's own table | `pick(256)`, or `pick(m)` among the `m` byte values with a code | `literal-code-length.1` .. `.10` (the length of its code) |
| with `e = 1`: the stored symbol is picked, the block's byte follows | as above | `delta.before-x-as-is` (`p < x`), `delta.added` |
| `dt = 3`: `x` = 4 (`last` 1 or 2), 6 (`last = 3`), `last` | follows from the offsets | `dt3-x.4`, `dt3-x.6`, `dt3-x.last` |

## A block's own literal table (*A block's own literal table*)

| item | how it is picked | counted as |
| --- | --- | --- |
| `m`, the byte values with a code, 2 to 256 | `pick_log(2, 256)` | |
| the code lengths, any complete set of `m` lengths of 1 to 10 bits | a full binary tree of depth at most 10: start with 2 leaves of depth 1, then `m - 2` times split a leaf: its depth by `pick` among the depths below 10 that leaves have, the leaf by `pick` among those | |
| which length each byte value with a code gets | a permutation of the `m` lengths, Fisher–Yates with `pick(i + 1)` | `OWN-symbol.0` .. `OWN-symbol.9` (lengths 1 to 10) |
| which byte values have a code | before each of the `m`, the number of byte values without a code: `pick_log(1, left + 1) - 1` | `own.uncoded-after-last`, `own.last-code-255` |
| how each run of byte values without a code is split into run symbols | until the run is used up: `k = 1 + pick(kmax)` with `2^(kmax-1) <=` what is left (`k <= 8`), then `r = pick(...)` so that `2^(k-1) + r` fits | `OWN-symbol.10` .. `OWN-symbol.17` (`k` 1 to 8), `own.run-split` (two run symbols in a row) |
| symbol 18 (`k = 9`) | is in no valid block (*A block's own literal table*); never written | `OWN-symbol.18` (always 0) |

Fixed: the order of the description (byte values from 0), its end (when the sum reaches 1), its
padding, `d`.

## How large cap has to be

`cr_write()` returns 0 without writing to `dst` if the block does not fit `cap`. Every block it writes
fits `CR_MAX_BLOCK` = 5407 bytes:

- header with coded literals: `3 + e + w <= 16` bytes;
- a block's own table: at most 256 byte values are described, and each costs at most 8 bits (a length
  symbol has at most 8 bits; a run symbol `k` covers at least `2^(k-1)` byte values for
  `OWN[9 + k] + k - 1` bits, at most 8 per byte value), so `d <= 256`;
- the literal streams and `B`: a search over every split of 4096 bytes into literals (10 bits, the
  longest code) and sequences (escaped token 16 bits, 12 offset bits, the longest `value(LL)` and
  `value(ML)` codes for each length) gives at most 41017 bits; with at most 7 bits of padding in each
  of the 8 streams and in `B`, at most 5135 bytes.

16 + 256 + 5135 = 5407. With raw literals the bound is 2 + 4140 bytes. The largest of the 100000
blocks of `cr-gen 1 100000` has 4972 bytes.

## Checked

`cr-check <dir> <count>` is a separate decoder written from format.md; it decodes every `<k>.z` and
compares with `<k>.block`, and `cr-check` alone decodes the example of format.md. Every block of
`cr-gen` passes.
