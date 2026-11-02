# Questions and choices while writing the decoder

Each entry gives the section of `seqlz.rst`, the sentence, the two readings, the
one taken and why, and whether other text in `seqlz.rst` decided it.

## 1. The `dt` validity check when there is no delta byte

- **Section:** *Layout of a compressed block* ("A block is invalid if").
- **Sentence:** "with coded literals `len < 3 + w + e`, `dt` is not 1, 2 or 3,
  `code > 8`, the block's own table is invalid, or ...".
- **Readings:** (a) `dt` must be 1..3 for every coded block; (b) `dt` only
  exists when `e = 1` (the layout puts `dt` in the `e` bytes at offset 3), so the
  check applies only then.
- **Taken:** (b). When `e = 0` there is no `dt` byte to read, so the check cannot
  apply. I test `dt` only when `e = 1`.
- **Decided by:** the layout table, which lists `dt` as the content of the `e`
  bytes "with `e = 1` the byte `dt` ... else nothing".

## 2. Reading `t` needs `len >= 3`, but the invalid rule is `len < 3 + w + e`

- **Section:** *Layout of a compressed block*.
- **Sentence:** "with coded literals `len < 3 + w + e` ... is invalid".
- **Readings:** `w` and `e` come from `t` at byte 2, which can only be read when
  `len >= 3`; (a) special-case `len < 3` as invalid before reading `t`; (b) read
  `t` unconditionally.
- **Taken:** (a). A coded block is at least `3 + 5 + 0 = 8` bytes, so any
  `len < 3` is invalid; I return -1 before touching byte 2, which also avoids a
  read outside `src`.
- **Decided by:** `w = 5 + ...` gives a minimum `3 + w + e` of 8, so `len < 3`
  is always invalid; consistent, not contradicted.

## 3. Does the last sequence's `copy` also undo deltas?

- **Section:** *Literals as deltas*.
- **Sentence:** "`out[p - x]` is a byte already written ... The last sequence is
  no different."
- **Readings:** (a) deltas are undone for the last literal run too; (b) the last
  run is copied verbatim.
- **Taken:** (a). `copy()` applies deltas with `e = 1`, and the last sequence
  calls `copy(n - used)` like any other.
- **Decided by:** the explicit sentence "The last sequence is no different" plus
  the *Decoding* definition of `copy`.

## 4. Which `last` value `dt = 3` and class 0 use inside a sequence

- **Section:** *Decoding* / *Literals as deltas* / *Offsets*.
- **Sentence (deltas):** "`last` is the offset of the sequence before, 1 for the
  first". **Sentence (offsets):** class 0 is "the offset of the sequence before".
- **Readings:** (a) use the offset of the previous sequence while decoding the
  current one; (b) use the current sequence's own offset.
- **Taken:** (a). In the loop, `copy(ll)` (which needs `last` for `dt = 3`) runs
  before `last = off`, so `last` still holds the previous offset; class 0 reads
  `last` before it is overwritten too.
- **Decided by:** the *Decoding* pseudocode order: `copy(ll)` precedes
  `last = off`, and `off` for class 0 is taken before any update.

## 5. "Codes must end in its last byte" for a literal stream

- **Section:** *Coded literals*.
- **Sentence:** "they take at most `8 * s[j]` bits and fewer than 8 less, and the
  bits after the last code are 0. A stream without literals has `s[j] = 0`."
- **Readings:** (a) the stream's codes occupy exactly `s[j]` bytes, i.e.
  `ceil(bits_used / 8) == s[j]`, with the leftover bits of the last byte zero,
  and `s[j] = 0` exactly when the stream has no literals; (b) only an upper bound
  on bits.
- **Taken:** (a). "At most `8*s[j]` bits and fewer than 8 less" means
  `8*s[j] - 8 < bits_used <= 8*s[j]`, which is `ceil(bits_used/8) == s[j]`. A
  stream with literals but `s[j] = 0` fails this; an empty stream needs
  `s[j] = 0`.
- **Decided by:** the two sentences together; nothing else was needed.

## 6. When the block's own table description ends, and what makes it invalid

- **Section:** *A block's own literal table*.
- **Sentence:** "The list ends as soon as the codes fill the code space: when
  `2^-l` ... is exactly 1 ... The block is invalid if the sum goes above 1, a
  byte value with a code is above 255, a bit after the last symbol in its byte is
  not 0, or `3 + w + e + d > len`."
- **Readings:** how to stop and when to declare invalid, especially if a no-code
  run pushes the byte-value index past 255 while the sum is still below 1.
- **Taken:** stop right after the code symbol that makes the scaled sum reach
  exactly 1; invalid if a code symbol would land on byte value > 255, if the sum
  passes 1, or if the sum is still below 1 once the index has reached 256 (no
  byte value is left to complete the code). This also bounds the loop.
- **Decided by:** "a byte value with a code is above 255" and "the sum goes above
  1"; the "index reached 256 with sum < 1" case is the only way those leave the
  loop unbounded, and such a table can never complete, so it is invalid.

## 7. Offset classes 6 and 7 never occur

- **Section:** *Tokens* / *Offsets*.
- **Sentence:** "`c = token >> 9` the class of the offset, 0 to 5" and, for the
  escape, "if `t >= 3072`: the block is invalid".
- **Readings:** (a) class is always 0..5, so no class check is needed; (b) guard
  against class 6/7.
- **Taken:** (a). A plain token symbol is below 3072, and an escaped token with
  `t >= 3072` is invalid, so `t < 3072` and `c = t >> 9 <= 5` always.
- **Decided by:** the escape rule bounding `t`, and `3071 >> 9 = 5`.

## 8. A whole zero byte left at the end is invalid

- **Section:** *The bitstream of the sequences*.
- **Sentence:** "after the last bit it needs, fewer than 8 bits of `B` are left,
  all in its last byte, and they are 0."
- **Readings:** (a) any trailing content of 8 bits or more, even if all zero,
  makes the block invalid; (b) trailing zero bytes are allowed.
- **Taken:** (a). "Fewer than 8 bits left" means at most 7; a full zero byte is 8
  and fails. This also rejects a literals-only block padded with extra zero bytes.
- **Decided by:** "fewer than 8 bits of `B` are left"; the *Why* note ("the
  compressor writes nothing else") confirms the intent.

## 9. `n = 0` is not special-cased

- **Section:** *Decoding* ("Two things follow from the rules").
- **Sentence:** "a valid block has `n >= 1`, because the first match needs a byte
  before it".
- **Readings:** (a) reject `n = 0` with an explicit rule; (b) let the general
  rules reject it.
- **Taken:** (b). With `n = 0` the first sequence is not the last (unless
  `BLOCK = 0`), so a match is required, and any offset `>= 1` exceeds `|out| = 0`
  while offset 0 is invalid; the block is rejected without a dedicated check.
- **Decided by:** the quoted sentence states this is a consequence, not a separate
  rule, so I did not add one.

## Things that are right but took a while to understand

- **The packed stream sizes `S`** (*Layout of a compressed block*). The `w` bytes
  from byte `3 + e` are one little-endian number of `8*w` bits, and
  `s[j] = (S >> (j*w)) & ((1<<w)-1)`. With `w` up to 12, `S` does not fit in 64
  bits, so I extract each `s[j]`'s `w` bits directly, bit 0 being the least
  significant bit of byte `3 + e`.
- **The own-table description alphabet** (*A block's own literal table*). OWN
  symbols 0..9 mean "next byte value has a code of `symbol + 1` bits"; symbols
  10..18 mean "`2^(k-1) + read(k-1)` byte values have no code" with `k = symbol -
  9`. The run reads `k - 1` extra bits, and `k = 1` reads none.
- **Length values** (*Length values*). For symbol `s >= 16`, `b = s - 12` and the
  value is `2^b + read(b)`; the offset of 12 (not 16) is easy to misread.
- **Bit order differs by purpose.** The bitstream `B`, the literal streams, and
  the own table are read most significant bit first; only the packed sizes `S`
  are little-endian. I kept two separate bit readers to avoid mixing them.
