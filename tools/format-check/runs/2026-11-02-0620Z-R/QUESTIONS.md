# Questions and interpretation choices

Each entry gives the seqlz.rst section, the sentence(s) in question, the
readings I saw, the one I took and why, and whether other text (often the
example) settled it. Entries marked **[hard]** are points that are
unambiguous but took a while to understand.

## 1. `dt` is only present/checked when `e = 1`

- Section: *Layout of a compressed block*.
- Quote: "with coded literals ``len < 3`` ... or ``len < 3 + w + e``, ``dt``
  is not 1, 2 or 3, ``code > 8`` ...". And the layout row: "``3`` / ``e`` /
  with ``e = 1`` the byte ``dt`` ... else nothing".
- Readings: (a) `dt` is always validated; (b) `dt` exists and is validated
  only when `e = 1`, since the layout gives it size `e` (0 bytes when
  `e = 0`).
- Taken: (b). There is no `dt` byte when `e = 0`, so the "dt is not 1,2,3"
  invalid condition can only apply when `e = 1`.
- Decided by: the layout table, which makes the `dt` byte conditional on `e`.

## 2. `S` can be up to 96 bits; extract each `s[j]` as a bit field **[hard]**

- Section: *Layout of a compressed block*.
- Quote: "The ``w`` bytes from byte ``3 + e`` on are one number ``S`` of
  ``8 * w`` bits, little endian, and ``s[j] = (S >> (j * w)) & ((1 << w) -
  1)``".
- With `w = 12`, `S` is 96 bits and `j * w` reaches 84, so `S` does not fit
  in a 64-bit integer. I do not form `S`; I read each `s[j]` directly as the
  `w` bits at little-endian bit offset `j * w` within the `w` bytes.
- No other-reading ambiguity; this is just an implementation subtlety.

## 3. Offset bits are read before the `ll` extension, `ml` value after the copy

- Section: *Decoding*.
- Quote (pseudocode): "``x = read(offset bits of class c)`` ... ``if ll ==
  15: ll = 15 + value(LL)`` ... ``copy(ll)`` ... ``if mlf == 31: ml = 35 +
  value(ML)``".
- Readings: the three variable-length reads (offset bits, LL value, ML value)
  could be ordered differently in the bitstream.
- Taken: exactly the pseudocode order: offset bits, then LL value (if any),
  then ML value (if any), all consecutively in `B`; `copy()` consumes no bits.
- Decided by: the worked *Example*, which reads the class-1 offset bit right
  after the 12-bit escaped token and the ML value last.

## 4. End-of-`B` test: not past the end, fewer than 8 bits left, all zero

- Section: *The bitstream of the sequences*.
- Quote: "after the last bit it needs, fewer than 8 bits of ``B`` are left,
  all in its last byte, and they are 0" and "A decoder may read zeros past
  the end ... and check at the end that it did not use them".
- Readings: how to combine "did not use zeros past the end" with "fewer than
  8 bits left".
- Taken: with `consumed` = bits taken from `B` and `total = |B| * 8`: invalid
  if `consumed > total` (used padding), invalid if `total - consumed >= 8` (a
  whole unused byte), and the remaining `< 8` bits must all be 0.
- Decided by: the *Example* ends with five `0` bits left over, which is valid.

## 5. Coded stream size bounds and empty streams

- Section: *Coded literals*.
- Quote: "their codes take ``b`` bits with ``8 * s[j] - 8 < b <= 8 * s[j]``
  ... the bits after the last code in that byte are 0. A stream without
  literals ... has ``s[j] = 0``. Else the block is invalid."
- Taken: decode exactly the stream's literal count, then require
  `8*s[j]-8 < b <= 8*s[j]` and that the trailing `8*s[j]-b` (< 8) bits are 0;
  a stream with no literals must have `s[j] = 0`. Over-read into following
  bytes during decode is allowed and then rejected by `b <= 8*s[j]`.
- No competing reading; the sentence is explicit.

## 6. A block's own table: when the list ends, and `d` **[hard]**

- Section: *A block's own literal table*.
- Quote: "The list ends as soon as the codes fill the code space: when
  ``2^-l``, summed over the lengths so far, is exactly 1." and "The table's
  last byte is the one with its last bit, the bits after that one in it are
  0, and ``d`` is its number of bytes."
- Taken: track the Kraft sum in units of 1/1024; stop reading symbols the
  moment it equals 1 (1024). `d = ceil(bits_used / 8)`; the trailing bits of
  the last byte must be 0; invalid if `3 + w + e + d > len`.
- No competing reading; understanding the stopping rule and `d` took care.

## 7. Own table: three distinct invalidity causes

- Section: *A block's own literal table*.
- Quote: "invalid if the sum goes above 1, a byte value with a code is above
  255, a run of byte values without a code goes past 255 ... Symbol 18
  (``k = 9``) ... is in no valid block".
- Taken: a coded byte value at index > 255 is invalid; a no-code run with
  `byteval + run > 256` is invalid; the Kraft sum exceeding 1024 is invalid.
  Symbol 18 (run of >= 256) is rejected because it either overflows the run
  check or leaves the code space unfillable (loop then hits an impossible
  257th symbol). A 257th symbol is impossible since `byteval` is capped at 256.
- Decided by: the explicit list of causes.

## 8. Delta `x` for `dt = 3`, and which `last` it uses **[hard]**

- Section: *Literals as deltas*.
- Quote: "``dt = 3``: ``x`` the smallest multiple of ``last`` that is 4 or
  more ... ``x = 4`` for ``last`` 1 or 2, 6 for ``last = 3``, else ``last``.
  In `Decoding` ``copy(ll)`` comes before ``last = off``, so a sequence's
  literals use the offset of the sequence before ... and the last sequence's
  literals the offset of the last match."
- Taken: `x = 4` if `last <= 2`, `6` if `last == 3`, else `last`. The `last`
  used during a `copy()` is the value held before `last = off` runs, i.e. the
  previous sequence's offset (1 for the first sequence; the last match's
  offset for the final literal-only sequence).
- Decided by: the spec's own cross-reference to the *Decoding* ordering.

## 9. Delta when the reference byte does not exist (`p < x`)

- Section: *Literals as deltas*.
- Quote: "if ``p < x``, there is no such byte, and the literal is stored as it
  is."
- Taken: when the output position `p` is below `x`, write the stored byte
  unchanged (no delta added). Otherwise write `(stored + out[p - x]) mod 256`.

## 10. `n = BLOCK` is valid; only `n > BLOCK` is invalid

- Section: *Layout of a compressed block* / *Decoding*.
- Quote: "``n > BLOCK`` is invalid in both layouts" and "a block of literals
  only has ``n = BLOCK`` and no bits in ``B``".
- Taken: `n == 4096` is allowed (a literals-only block); the check is strictly
  `n > 4096`. Verified by the raw literals-only test.

## 11. Token class is always in range; escape is the only token error

- Section: *Tokens*.
- Quote: "``c = token >> 9`` the class of the offset, 0 to 5" and "if t ==
  3072: t = read(12); if t >= 3072: the block is invalid".
- Taken: for any `token` in `0..3071`, `token >> 9` is `0..5`, so no separate
  class check is needed; the only token-level invalidity is an escaped number
  `>= 3072`.

## 12. The last sequence is found by equality, and carries no token

- Section: *Decoding*.
- Quote: "if ``n - used == BLOCK - |out|``: the last sequence; ``copy(n -
  used)``; stop".
- Taken: at the top of each iteration, when the literals left exactly equal
  the bytes still needed, copy them (possibly zero) and stop without reading a
  token. This also handles the pure literals-only block (`n = BLOCK`,
  immediate stop).

## 13. Offset classes can encode 0, which is then invalid

- Section: *Offsets* / *Decoding*.
- Quote: "For a sequence with a match, an offset of 0, or one larger than the
  bytes written so far, makes the block invalid."
- Readings: whether `read(1)*2 = 0` (class 1) or `read(8)*8 = 0` (classes 4/5)
  or `read(P) = 0` (class 3) are rejected at decode.
- Taken: these are decoded to `off = 0` and then rejected by the general match
  rule, since every tokenled sequence has a match. The example's class-1
  offset is `read(1)=1 -> off=2`.

## 14. An escaped token that also has its own code is accepted

- Section: *Tokens*.
- Quote: "Any token can be sent escaped, also one that has a code of its own;
  it means the same."
- Taken: after an escape I use the 12-bit number as the token with no further
  check beyond `< 3072`; I never reject it for "should have used its code".

## 15. `ll`/`ml` after a length value: bound equivalently to the space checks

- Section: *Decoding* / *Length values*.
- Quote: "``if ll == 15: ll = 15 + value(LL)``" and the checks "``ll > n -
  used or ll > BLOCK - |out|``" / "``ml > BLOCK - |out|``".
- Taken: `value()` is at most `2^11 + (2^11 - 1) = 4095`, so `ll` and `ml`
  are at most ~4110. I reject `ll`/`ml > BLOCK` as a bound (prevents overflow)
  which is subsumed by the spec's `> BLOCK - |out|` checks, and then apply
  those checks. Right but subtle: the extra bound never rejects a block the
  spec would accept, because `BLOCK - |out| <= BLOCK`.
