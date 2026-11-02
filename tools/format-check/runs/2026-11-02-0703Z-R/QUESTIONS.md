# Questions and choices while writing the seqlz decoder

Each entry gives the section of `seqlz.rst`, the sentence at issue, the two
readings, the one I took and why, and whether other text in `seqlz.rst`
decided it. Entries marked **[slow]** are places that are right as written but
took me a while to understand; they were not genuine ambiguities.

## 1. The `dt` check when `e = 0`

- **Section:** "Layout of a compressed block".
- **Sentence:** a block is invalid with coded literals if "... `dt` is not 1,
  2 or 3, `code > 8` ...".
- **Readings:** (a) the `dt` check applies only when `e = 1`, because the
  layout table puts the `dt` byte at offset 3 only "with `e = 1`"; when `e = 0`
  there is no `dt` byte to test. (b) `dt` is always tested, so `e = 0` would
  always be invalid because the absent byte is not 1..3.
- **Taken:** (a). Reading (b) would make every non-delta coded block invalid,
  which contradicts the whole point of `e` being a flag and of level-2 coding
  without deltas.
- **Decided by:** the layout table, which lists the `dt` byte only for `e = 1`
  ("with `e = 1` the byte `dt` ... else nothing").

## 2. End of the sequence bitstream: three conditions

- **Section:** "The bitstream of the sequences".
- **Sentence:** "after the last bit it needs, fewer than 8 bits of `B` are
  left, all in its last byte, and they are 0. A block that needs a bit past
  the end of `B` is invalid."
- **Readings:** whether "no bit past the end", "fewer than 8 bits left", and
  "they are 0" are one test or three. If a decoder reads zeros past the end
  (allowed), does a block that consumed exactly all of `B` plus padding still
  need the "fewer than 8 left" test?
- **Taken:** all three as conjunctive requirements for validity: (i) no needed
  bit lay past `|B|` (I track an overrun flag), (ii) the leftover bit count is
  below 8, (iii) every leftover bit is 0.
- **Decided by:** the "Why" paragraph, which says reading zeros past the end
  and checking at the end "that it did not use them" gives the same result as
  checking at every read. So the overrun flag replaces per-read checks, and the
  leftover-bits test is still needed. The example ends with "00000" (5 zero
  bits left), which confirms the leftover-must-be-0 rule.

## 3. `off > |out|` is tested after the literals are copied

- **Section:** "Decoding" (and "Offsets").
- **Sentence:** "an offset ... one larger than `|out|` after this sequence's
  literals are copied, makes the block invalid"; the pseudocode puts
  `copy(ll)` before the line `if off == 0 or off > |out| or ...`.
- **Readings:** compare `off` to `|out|` before or after `copy(ll)`.
- **Taken:** after, i.e. against the output length that already includes this
  sequence's `ll` literals.
- **Decided by:** both the explicit sentence and the order of the pseudocode.
  This is why the two consequences hold: a valid block has `n >= 1` and the
  first match's offset is at most that sequence's `ll`. **[slow]** — the
  interaction took a while to see.

## 4. `dt = 3`: which `last`, and the value of `x`

- **Section:** "Literals as deltas".
- **Sentence:** "`x` the smallest multiple of `last` that is 4 or more, where
  `last` is the offset of the sequence before ... `copy(ll)` comes before
  `last = off`, so a sequence's literals use the offset of the sequence before
  ... and the last sequence's literals the offset of the last match."
- **Readings:** does a sequence's delta use its own just-decoded offset or the
  previous sequence's offset?
- **Taken:** the previous sequence's offset (the current value of `last` at the
  moment `copy(ll)` runs, since `last` is updated only after the copy). For the
  final literal-only sequence, `last` holds the last match's offset.
- **Decided by:** the quoted sentence states it outright. I compute
  `x = 4` for `last` 1 or 2, `6` for `last = 3`, else `last`. **[slow]** —
  the "smallest multiple of `last` that is 4 or more" needed working through:
  it gives 4, 4, 6, 4, 5, 6, ... for `last` = 1, 2, 3, 4, 5, 6.

## 5. Block's own table: a run that reaches exactly byte value 256

- **Section:** "A block's own literal table".
- **Sentence:** "a run of byte values without a code goes past 255 ... A run
  past 255 makes the block invalid at once." and "Symbol 18 (`k = 9`) ... is in
  no valid block: a complete table has at least two byte values with a code."
- **Readings:** a run covering byte values up to exactly 255 (`bv + run =
  256`) is "past 255" or not. Symbol 18 at `bv = 0` with `read(8) = 0` gives a
  run of exactly 256, reaching byte value 255 but not beyond.
- **Taken:** `bv + run > 256` is invalid at once; `bv + run == 256` is allowed
  but leaves the table incomplete (the sum of `2^-l` is unchanged by a run), so
  the next symbol fails (a code at `bv = 256 > 255`, or a further run), making
  the block invalid. Either way symbol 18 can never appear in a valid block.
- **Decided by:** "goes past 255" means strictly beyond 255, i.e. covering byte
  value 256, which is `bv + run > 256`. The completeness rule and the "at least
  two byte values with a code" sentence then rule out the exactly-256 case.

## 6. Block's own table: number of bytes `d` and trailing bits

- **Section:** "A block's own literal table".
- **Sentence:** "The table's last byte is the one with its last bit, the bits
  after that one in it are 0, and `d` is its number of bytes." and "Bits past
  `len` are read as 0, and `3 + w + e + d > len` then makes the block invalid."
- **Readings:** how to compute `d` and when a table that completes only by
  consuming zero bits past `len` is invalid.
- **Taken:** after the description completes (sum of `2^-l` is exactly 1), `d =
  ceil(bits_consumed / 8)`. If `3 + w + e + d > len` the block is invalid; this
  also catches any completion that used zero bits past `len`, since then the
  last byte index is at or past `len`. Within range, the bits from the last
  symbol's end to the byte boundary must all be 0.
- **Decided by:** the quoted sentences. I check `3 + w + e + d > len` before
  reading the trailing bits, so the trailing-bit read stays inside `src`.

## 7. Literal stream: "codes end in the stream's last byte"

- **Section:** "Coded literals".
- **Sentence:** "has literals whose codes take `bits` bits, and not
  `8 * s[j] - 8 < bits <= 8 * s[j]`" is invalid.
- **Readings:** the boundary cases. If `s[j] = 0` and the stream has literals,
  `8*s[j]-8 = -8 < bits` holds but `bits <= 0` fails, so invalid. If the codes
  need more than `8*s[j]` bits, invalid.
- **Taken:** decode each stream from its own `s[j]` bytes (reading zeros past
  that, with an overrun flag), then require `bits <= 8*s[j]`, `bits > 8*s[j]-8`,
  and that all bits after the last code in the last byte are 0. An empty stream
  (streams `n`..7 when `n < 8`) must have `s[j] = 0`.
- **Decided by:** the three bullet points of the section, plus "A decoder may
  read past the end of a stream ... if it checks these rules; the result is the
  same," which licenses the read-zeros-and-check approach. **[slow]** — the
  inequality and its boundary cases took a careful read.

## 8. The last-sequence test and why the loop terminates

- **Section:** "Decoding".
- **Sentence:** "`if n - used == BLOCK - |out|:` the last sequence".
- **Readings:** none, really; but I had to convince myself the loop always
  halts and the equality is reached for valid blocks.
- **Taken:** each full (non-last) sequence writes `ll + ml` bytes and consumes
  `ll` literals, so `D = (BLOCK - |out|) - (n - used)` falls by `ml >= 4` each
  step; it starts at `BLOCK - n >= 0`. A valid block reaches `D = 0` exactly;
  otherwise some check (offset, match length, literal count) fails. I also cap
  the loop at `BLOCK + 1` iterations as a backstop.
- **Decided by:** "Every sequence before the last writes at least 4 bytes, so a
  decoder stops after at most `BLOCK / 4 + 1` sequences, also on blocks that
  are not valid." **[slow]**

## 9. The offset class is always 0..5 after a valid token

- **Section:** "Tokens" and "Offsets".
- **Observation:** `c = token >> 9`. For `token` in 0..3071 the maximum is
  `3071 >> 9 = 5`; `token = 3072` is the escape, and an escaped `token >= 3072`
  is invalid, so a decoded token is always 0..3071 and `c` is always 0..5.
- **Taken:** no separate range check on `c` is needed. **[slow]** — I verified
  this rather than adding a redundant guard.

## 10. Where the SHA-256 check belongs

- **Section:** "Where the tables come from".
- **Sentence:** "The SHA-256 of each table's lengths ... so that a decoder can
  check that it has the right ones." / "The KUnit tests of seqlz check them."
- **Readings:** verify the SHA-256 at every `cr_decode` call, once at startup,
  or at the time the embedded tables are generated.
- **Taken:** the generator script `gen_tables.py` verifies each table against
  its SHA-256 before embedding it, so a wrong table can never reach the C code;
  `cr_decode` itself does no hashing (it would add cost to every call for no
  gain once the tables are fixed in the source).
- **Decided by:** "The KUnit tests ... check them" places the check outside the
  hot decode path. This is a build-time choice, not a format rule.

## 11. `value(table)` symbol range

- **Section:** "Length values".
- **Observation:** for `LL`/`ML` the only symbols with codes are 0..23, and
  `value()` with `s >= 16` uses `b = s - 12` (4..11), giving values up to
  `2^11 + (2^11 - 1) = 4095 = 2^P - 1`. No symbol can exceed the table, because
  `symbol()` only ever returns a symbol that has a code.
- **Taken:** no extra bound check on the value; the format's own numbers keep
  it within `[0, 2^P)`.
- **Decided by:** the "*Why `2^P - 1`*" paragraph.
