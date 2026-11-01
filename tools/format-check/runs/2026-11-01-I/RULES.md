# Rules of the seqlz format that make a compressed block invalid

Written from `format.md` only. No code was written and no table file was opened. Sentences marked
*Why:* were not used as rules.

## Conventions

- `b[i]` is byte `i` of the compressed block, `len` its length, `h = b[0] + 256 * b[1]`,
  `n = h & 0x7fff`. For coded literals `t = b[2]`, `code = t & 15`, `w = 5 + ((t >> 4) & 7)`,
  `e = t >> 7`. `E` is the example block of format.md, `02 00 61 62 23 f2 ff fb 60` (n = 2, one
  sequence `ll = 2`, `off = 2` with class 1, `ml = 4094`, 35 bits of `B` used, 5 padding bits).
- `L` is the raw block of literals only: `00 10` followed by the 4096 bytes of the block, `len = 4098`,
  `B` empty. `K` is any valid block with coded literals; `K8` one with `code = 8`; `KL` one with
  coded literals only (`n = 4096`, `B` empty).
- "Smallest change": the decoder applies the rules in the order of the pseudo-code in format.md. A
  change counts if this rule is the first one it breaks and, where that is possible, the only one.
  Where every change that breaks a rule also breaks another one, the row says so.
- The bytes of `E` and of `R20`'s block were worked out by hand from the example's own claims
  (escape = `0010`, `ML` symbol 23 = `1111111`); see CLAIMS.md C57 and C61, which might be wrong.

## Stated rules

| ID | section | sentence quoted | condition on the bytes (invalid if) | smallest change of a valid block |
| --- | --- | --- | --- | --- |
| R1 | Layout of a compressed block | "A block is invalid if - `len < 2`" | `len < 2` | Cut `E` to its first byte, `02`. Can't be broken alone: with `len < 2`, R3 (raw: `2 + n > len`) or R4 (coded) would hold too, if `h` could be read. It is the first check, because `h` can't be read. |
| R2 | Layout of a compressed block | "a block has at most `BLOCK` literals, and `n > BLOCK` is invalid in both layouts" / "A block is invalid if ... `n > BLOCK`" | `(h & 0x7fff) > 4096` | `L`: `b[0..1]` `00 10` → `01 10` (n = 4097) and append one byte (len 4099), so that R3 still holds. Changing only `h` of `E` (e.g. `02 10`) also breaks R3. |
| R3 | Layout of a compressed block | "with raw literals `2 + n > len`" | `(h & 0x8000) == 0` and `2 + n > len` | `E`: `b[0]` `02` → `08` (n = 8 > len − 2 = 7). |
| R4 | Layout of a compressed block | "with coded literals `len < 3 + w + e`" | `(h & 0x8000) != 0` and `len < 3 + w + e` (and `len = 2`, where `t` is missing, see QUESTIONS Q4) | `E`: `b[1]` `00` → `80`: coded, `t = 0x61`, `w = 11`, `e = 0`, `3 + 11 > 9`. Note that R7 then holds too, as `d` and the `s[j]` are ≥ 0; R4 is the check that comes first, before the sizes can be read. |
| R5 | Layout of a compressed block; Layout table | "`dt` is not 1, 2 or 3" | coded, `e = 1`, and `b[3]` not in {1, 2, 3} | `K` with `e = 1`: `b[3]` → `00` (or `04`). |
| R6 | Layout of a compressed block | "`code > 8`" / "`code` 0 to 7 is one of the 8 fixed literal tables, `code` 8 a table of the block's own, and 9 to 15 are invalid." | coded and `(b[2] & 15) > 8` | `K` with `code` 1: `b[2]` low nibble 1 → 9 (`b[2] \| 8`). |
| R7 | Layout of a compressed block | "`3 + w + e + d + s[0] + ... + s[7] > len`" with `s[j] = (S >> (j * w)) & ((1 << w) - 1)` | coded and `3 + w + e + d + Σ s[j] > len` | `KL`: delete its last byte (stream 7 now ends past `len`). |
| R8 | A block's own literal table | "The block is invalid if the sum goes above 1" | `code = 8` and the running sum of `2^-l` over the description's lengths becomes > 1 (it never was exactly 1 before, else the list would have ended) | `K8`: replace the description's last length symbol, `l − 1`, by `l − 2` (code length `l − 1`): the sum becomes `1 + 2^-l`. If the last length is 1, shorten an earlier one. |
| R9 | A block's own literal table | "a byte value with a code is above 255" | `code = 8` and a length symbol (0 to 9) comes when the next byte value is ≥ 256 | `K8`: replace a skip symbol before the last length symbol by symbol 18 (`k = 9`, skips 256 to 511 values), with any 8 bits; insert one if there is none. |
| R10 | A block's own literal table | "a bit after the last symbol in its byte is not 0" / "the bits after that one in it are 0" | `code = 8` and a bit after the description's last bit, in the same byte, is 1 | `K8` whose description does not end on a byte boundary: set one of those bits. (A block whose description ends on a byte boundary cannot break R10.) |
| R11 | A block's own literal table | "or `3 + w + e + d > len`" | `code = 8` and the description needs bits past byte `len − 1` | `K8`: cut it inside the description. Can't be broken alone: `3 + w + e + d > len` implies R7. |
| R12 | Coded literals | "The codes of stream `j`'s literals must end in its last byte: they take at most `8 * s[j]` bits" | coded and the codes of stream `j`'s literals take more than `8 * s[j]` bits | `KL`: delete the last byte of stream 7 and lower `s[7]` by 1 (in `S`); `len` and R7 still agree. |
| R13 | Coded literals | "... and fewer than 8 less" | coded and the codes of stream `j` take `≤ 8 * s[j] − 8` bits | `KL`: raise `s[7]` by 1 and append `00`. |
| R14 | Coded literals | "and the bits after the last code are 0" | coded and a bit of stream `j` after its last code (in its last byte) is 1 | `K`: set a padding bit of a stream whose codes do not end on a byte boundary. |
| R15 | Coded literals | "A stream without literals has `s[j] = 0`. Else the block is invalid." | coded, `j ≥ n`, and `s[j] != 0` | `K` with `n ≤ 7`: set `s[7]` to 1 and insert `00` at `start[7]` (before `B`, so `B` is unchanged). Can't be broken alone: it is R13 with 0 bits of codes. |
| R16 | Tokens | "`if t >= 3072: the block is invalid`" | the 12 bits after an escape code are ≥ 3072, i.e. their first two bits are `11` | `E`: `b[4]` `23` → `2f` (the escaped number becomes `0xff2` = 4082). |
| R17 | Offsets; Decoding | "For a sequence with a match, an offset of 0 ... makes the block invalid." / "`if off == 0 ...: invalid`" | a token's offset bits give 0 (class 1 bit 0; class 2, 3, 4, 5 bits all 0) | `E`: `b[6]` `ff` → `7f` (class 1 bit 0, `off = 0`). |
| R18 | Offsets; Decoding | "or one larger than the bytes written so far, makes the block invalid" / "`off > \|out\|`" | `off > \|out\|`, `\|out\|` taken after `copy(ll)` | `E`: `b[5]` `f2` → `f1`: `ll = 1`, `\|out\| = 1`, `off = 2`. |
| R19 | Decoding | "`if ll > n - used or ...: invalid`" | a token's `ll` (with `value(LL)`) is more than the literals left | `E`: `b[5]` `f2` → `f3`: `ll = 3 > 2`. |
| R20 | Decoding | "`... or ll > BLOCK - \|out\|: invalid`" | a token's `ll` is more than the bytes the block still needs | `03 00 61 62 63 23 f2 ff fb 64 00 20`: `E` with a third literal and a second token, escaped token 1 (`ll = 1`, `mlf = 0`, class 0); after the first match `\|out\| = 4096`, `n − used = 1`, `ll = 1 > 0`. Every block that breaks R20 also breaks I5: R20 adds no invalid block (see "Rules that add nothing"). |
| R21 | Decoding | "`if ... ml > BLOCK - \|out\|: invalid`" | a match is longer than the bytes the block still needs | `E`: `b[8]` `60` → `80` (`read(11)` = 2012, `ml = 4095 > 4094`). Also breaks I5, see below. |
| R22 | The bitstream of the sequences | "A valid block ends where its bits end: after the last bit it needs, fewer than 8 bits of `B` are left" | the decoder needs bits past the end of `B` (`B` cut too short) | `E`: delete `b[8]`: `read(11)` needs 3 bits past the end. |
| R23 | The bitstream of the sequences | "fewer than 8 bits of `B` are left, all in its last byte" | after the last sequence, 8 or more bits of `B` are left | `E`: append `00`. |
| R24 | The bitstream of the sequences | "and they are 0" / "`at the stop: fewer than 8 bits of B left, all 0, else invalid`" | a bit of `B` after the last bit used is 1 | `E`: `b[8]` `60` → `61`. |

## Implied rules (written nowhere as one rule)

| ID | follows from | sentences | condition (invalid if) | smallest change |
| --- | --- | --- | --- | --- |
| I1 | R18, R19, Decoding loop | "a valid block has `n >= 1`, because the first match needs a byte before it" (stated as following from the rules); "`if n - used == BLOCK - \|out\|`" | `n = 0` (either layout) | `E`: `h` → `00 00` and delete `61 62` (so `B` is unchanged): breaks R19 (`ll = 2 > 0`). A block with `n = 0` always breaks R18 or R19; no own change. |
| I2 | R18 | "the first match's offset is at most that sequence's `ll`" | first match's `off > ll` of the first sequence | `E`: `b[5]` `f2` → `f1` (as R18). |
| I3 | R17 + Offsets table | "`read(1) * 2`", "an offset of 0 ... makes the block invalid" | the offset bit of class 1 is 0; the offset bits of class 2 to 5 are all 0 | `E`: `b[6]` `ff` → `7f` (as R17). Class 1 has 1 bit and only one valid value. |
| I4 | R16 + Tokens | "followed by the token's number in 12 bits", "`if t >= 3072`" | the first 2 of the 12 escaped bits are both 1 | as R16. |
| I5 | Decoding loop, R20, R21, R22 | "`if n - used == BLOCK - \|out\|: the last sequence`"; "Every match is at least 4 bytes"; "once the literals left are as many as the bytes the block still needs, no match can follow" | the matches of the block do not add up to exactly `BLOCK − n` bytes. Each sequence lowers `(BLOCK − \|out\|) − (n − used)` by `ml ≥ 4`; the loop ends only when it is 0, so it must hit 0 exactly | `E`: `b[8]` `60` → `40` (`ml = 4093`): after it `n − used = 0 ≠ 1`, the decoder reads a token from the padding and fails by R22 (or earlier). |
| I6 | Decoding loop + R22 to R24 | "the last sequence has no token"; "at the stop: fewer than 8 bits of B left, all 0"; (How this file was checked: "a last sequence with a token, which is invalid") | after the point where `n − used = BLOCK − \|out\|`, `B` holds more than padding: no token may describe the last sequence | `L`: append `80` (a "token" after a block of literals only): breaks R23 / R24. Exception, see V13: zero bits that would make a token are padding. |
| I7 | R22, R23 | "fewer than 8 bits of `B` are left" | `\|B\| != ceil(bits used / 8)` | `E`: append `00` or delete `b[8]`. |
| I8 | I7 + Layout | "`2 + n` ... up to `len`", "a block of literals only has `n = BLOCK` and no bits in `B`" | `len` is not exactly header + literals (or table + streams) + `ceil(bits/8)`. In particular a raw block of literals only has `len = 4098` | `L`: append any byte. |
| I9 | R12, R13 | "they take at most `8 * s[j]` bits and fewer than 8 less" | `s[j] != ceil(bits of stream j / 8)`: the sizes are fixed by the literals and the table | as R12 / R13. |
| I10 | R12 + "longest code 10 bits" (literal tables) + own table lengths 1 to 10 + "Stream `j` holds literal `j`, `j + 8`, ..." | "a stream holds at most every 8th literal, of at most 10 bits" is a *Why:*, but follows from the rules | `s[j] > ceil(10 * ceil((n − j) / 8) / 8)`, in particular any `s[j] > 640`, so the values 641 to 4095 that `w = 10, 11, 12` can hold are never valid | Can't be broken alone: breaks R13. |
| I11 | R8, R9, list end rule | "The list ends as soon as the codes fill the code space"; "a byte value with a code is above 255" | the description of the own table does not reach a Kraft sum of exactly 1 by byte value 255. So: it has codes for at least 2 byte values; its last symbol is a length symbol; symbol 18 (`k = 9`, ≥ 256 values) never occurs in a valid block | as R9 (or R11, if the bits run out first). |
| I12 | R19, I5 | as above | the `ll` of the sequences with a token add up to more than `n` | as R19. |
| I13 | "Every sequence before the last writes at least 4 bytes" + R21 | | more than `BLOCK / 4` sequences with a token | Can't be broken alone; breaks R21 (or R20, R22). |
| I14 | Layout + R4 | "`3 + e` \| `w` \| `s[0]` to `s[7]`" | with `e = 1`, `len < 9 + ((t >> 4) & 7)` (the `dt` byte counts) | `K` with `e = 1`: cut it after `dt` and fewer than `w` size bytes. |

## Rules that add nothing to the set of invalid blocks

The decoder needs them, so that it does not read or write outside its buffers, but a block they reject
is also rejected by another rule:

- R1 (by R3 / R4), R4 and R11 (by R7), R15 (by R13), I10 (by R13).
- R20 and R21: a block that breaks them has `(BLOCK − |out|) − (n − used) < 0` at that point (R20) or
  will have it (R21); that difference only falls, so the stop condition is never met and the block
  breaks I5 (in the end R20, R21 or R22) anyway. R19, R17 and R18 do add invalid blocks: without them
  `copy` and the match would read bytes that don't exist.

## Valid, although one might expect it not to be

| ID | section | what | why valid |
| --- | --- | --- | --- |
| V1 | Tokens | A token sent escaped although it has a code of its own. | "Any token can be sent escaped, also one that has a code of its own; it means the same." |
| V2 | Offsets | An offset sent with a class other than the cheapest (e.g. 2 with class 3, 16 with class 2 or 5), or the last offset again sent explicitly instead of with class 0. | No rule asks for the cheapest; only "What the compressor writes" does, and it is not part of the format. |
| V3 | Offsets | Class 0 in the first sequence: offset 1. | "The last offset starts as 1 for each block." Valid if `ll ≥ 1`. |
| V4 | Layout | `w` larger than the largest `s[j]` needs (e.g. `w = 12` for streams of a few bytes). | Only "the smallest `w`" in the compressor section; the format only reads `w` bits. |
| V5 | Layout | Coded literals that take more bytes than the raw literals would; a block's own table for few literals; `code = 8` with `n < 512`. | The thresholds are in "What the compressor writes", not in the rules. |
| V6 | Literals as deltas | `e = 1` where no literal is at `p ≥ x` (e.g. all literals before byte 4, or `dt = 3` with a large last offset), so no delta is applied; `e = 1` with `n = BLOCK`. | "if `p < x`, there is no such byte, and the literal is stored as it is." |
| V7 | Literals as deltas | `out[p − x]` lies in an earlier sequence, not in "the match before or a literal of the same sequence" (e.g. `dt = 2` after a 4-byte match, or `dt = 3` with `last` = 4000). | The rule is "If `p >= x`"; nothing limits `p − x` further. See QUESTIONS Q11. |
| V8 | A block's own literal table | The description starts with a skip, has two skips in a row (e.g. two skips of 1 for 2 values), gives codes to byte values that never occur, or ends before byte value 255. | Only the sum, byte value ≤ 255 and the padding are checked; "Byte values after the last one with a code have none." |
| V9 | Length values | `ll = 15` as token `ll = 15` plus `value(LL) = 0`; `ml = 35` as `mlf = 31` plus `value(ML) = 0`. | `ll = 15 + value(LL)`; each length has exactly one form, but these may look like waste. |
| V10 | Decoding | A last sequence with no literals: the last match reaches the end of the block. | "A block whose last match reaches the end of the block ends with no literals left." |
| V11 | Layout; Decoding | Coded literals with `n = BLOCK` (literals only), and coded literals with `n < 8` (some streams empty). | `n = BLOCK` gives no tokens; empty streams have `s[j] = 0`. |
| V12 | Decoding | A match with `off < ml` repeating its own bytes; a match with class 0 right after a match. | "so it can repeat bytes it writes itself". |
| V13 | The bitstream of the sequences | Padding zero bits that would read as a token. By the example, `TOK` has a 3- or 4-bit code of only zeros (CLAIMS C57), so `E`'s 5 padding bits `00000` start with a token code. | The decoder stops at `n − used = BLOCK − \|out\|` and never reads them; the rule asks only that fewer than 8 bits be left and that they are 0. |
| V14 | Layout | In a raw block, `b[2]` is the first literal and may have any value, also one whose low 4 bits are 9 to 15. | R6 and R5 apply to coded literals only. See CLAIMS C69. |
| V15 | Decoding | The content of the decoded block is never checked; any literals and matches that pass the rules are valid. | zram stores no checksum (*Why:* of R22 to R24). |
| V16 | Coded literals | Fixed literal tables never make a block invalid by their codes: every bit string decodes. | "Every table is complete ... a decoder never meets bits that are no code." Only the stream end (R12 to R15) can fail. |
| V17 | Layout | `e = 1` together with `code = 8`, or with any of `code` 0 to 7. | Nothing forbids it. |
