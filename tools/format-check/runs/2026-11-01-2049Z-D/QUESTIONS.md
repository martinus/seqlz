# Questions while writing the seqlz decoder from format.md

Each entry has the section, the sentence, the possible readings, the one I took and why, and whether
other text in format.md decided it. Entries marked **[took long]** are not ambiguous; they are right
but took me a while to understand.

## 1. Layout of a compressed block — coded literals with `len` 2

> with coded literals `len < 3 + w + e`, ...

- Problem: `w` and `e` come from byte 2, `t`, which does not exist when `len == 2`.
- Readings: (a) the block is invalid; (b) not covered.
- Taken: (a). `w >= 5`, so `len < 3` gives `len < 3 + w + e` for every value `t` could have. The
  decoder rejects `len < 3` before it reads `t`. No other text needed. **[took long]**

## 2. Coded literals — what "Else" covers

> The codes of stream `j`'s literals must end in its last byte: they take at most `8 * s[j]` bits and
> fewer than 8 less, and the bits after the last code are 0. A stream without literals has `s[j] = 0`.
> Else the block is invalid.

- Readings: (a) "Else" covers all three conditions, so a stream with no literals and `s[j] > 0` is
  invalid; (b) "A stream without literals has `s[j] = 0`" only follows from the first rule, and
  "Else" covers only the stream-end rule.
- Taken: (a). "Else" comes after both sentences, and the first rule alone does not settle a stream
  with no codes: 0 bits is "at most `8 * s[j]`", and "fewer than 8 less" fails for `s[j] >= 1`, so (b)
  also gives invalid for `s[j] >= 1`. Both readings agree, so it does not matter. Nothing else in the
  text decides it.

## 3. A block's own literal table — a skip past byte value 255

> The block is invalid if the sum goes above 1, a byte value with a code is above 255, ...

- Problem: a skip symbol (`9 + k`) can move the next byte value past 255 without giving any of those
  values a code. Only a *code* above 255 is named as invalid.
- Readings: (a) the skip alone is invalid; (b) only a later length symbol makes it invalid.
- Taken: both give the same result. The list ends only "as soon as the codes fill the code space",
  which only a length symbol can do. After such a skip, the next length symbol is for a byte value
  above 255, which is invalid, and skips alone run past `len`, which is also invalid
  (`3 + w + e + d > len`). So the decoder rejects such a skip at once. **[took long]**

## 4. A block's own literal table — the end check in bits and in bytes

> `3 + w + e + d > len`

- The table is read from byte `3 + w + e` with zeros past `len`. `d = ceil(bits / 8)`. If the bits ran
  past `len`, then `3 + w + e + d > len`, so the zeros read past the end never make a block valid.
  **[took long]**

## 5. Tokens — an escaped token that has its own code

> Any token can be sent escaped, also one that has a code of its own; it means the same.

- "What the compressor writes" says the compressor escapes only tokens without a code, but that
  section is "Not part of the format". Taken: an escaped token that has its own code is valid. The
  format text decides this.

## 6. Decoding — which `last` the deltas of `dt = 3` use

> `x` the smallest multiple of `last` that is 4 or more, where `last` is the offset of the sequence
> before, 1 for the first

- In the pseudo-code, `copy(ll)` comes before `last = off`, so the literals of a sequence use the
  previous sequence's offset, and the last sequence uses the offset of the last match. That matches
  "the offset of the sequence before". The pseudo-code and the prose agree. **[took long]**

## 7. Decoding — the order of the reads in `B`

> token, `x = read(offset bits)`, then `value(LL)`, then `value(ML)`

- The offset bits come before the `LL` value, not after the literals. The pseudo-code says so
  explicitly. The example confirms that the offset bits come before the `ML` value (its `ll` is 2,
  so it has no `LL` value). **[took long]**

## 8. Decoding — offset 0 from classes 1, 2, 3, 4, 5

> For a sequence with a match, an offset of 0, or one larger than the bytes written so far, makes the
> block invalid.

- `read(1) * 2`, `read(8)`, `read(P)`, ... can all give 0. The table's column "offsets it can send"
  (e.g. "2" for class 1) does not list 0. Taken: the bits are allowed to be read, and the block is
  invalid because `off == 0`. The pseudo-code decides this (`if off == 0 ...: invalid`).

## 9. The bitstream of the sequences — the end rule with `|B| = 0`

> after the last bit it needs, fewer than 8 bits of `B` are left, all in its last byte, and they are 0.

- When `|B| = 0` there is no "last byte". Taken: 0 bits are left, which is fewer than 8, so the block
  is valid (a block of literals only must have `|B| = 0`, as "Decoding" says). This is also why a raw
  block with `n = BLOCK` must have `len == 2 + BLOCK` exactly.

## 10. The bitstream of the sequences — bits read past the end

> A decoder may read zeros past the end instead of checking at every read, and check at the end that
> it did not use them

- Taken: read zeros past the end of `B`, of each literal stream and of the own table. At the end,
  check (a) bits used `<= 8 * size` and (b) `8 * size - used < 8`, and that the remaining bits are 0.
  For the literal streams the bits past a stream's end belong to the next stream, but the zeros give
  the same result, because any read past the end is invalid anyway.

## 11. Literals as deltas — raw literals

> with raw literals and with `e = 0` as they are

- Raw literals have no `t`, so they have no `e`. They are never deltas. This is clear; I list it only
  because the layout table for raw literals does not mention `e`.

## 12. The tables — the comment blocks for the kernel's copy

- The text inside `<!-- in the kernel's copy: ... -->` repeats the table list with other file names
  and gives the same table sizes. I used the visible text. The two copies do not conflict.

## 13. Layout — the sizes `s[j]` as one little endian number

> The `w` bytes from byte `3 + e` on are one number `S` of `8 * w` bits, little endian, and
> `s[j] = (S >> (j * w)) & ((1 << w) - 1)`

- So `s[0]` is the low `w` bits of the first bytes. A size can cross a byte boundary. **[took long]**
  (It is easy to misread the size column "w" in the layout table as bits; it is bytes.)

## 14. The tables — "longest code" of `ML`

> | match length values, `ML` | `LEN_SYMBOLS` | 8 bits | ...

- `tables/ML.bin` (whose SHA-256 matches) has no code longer than 7 bits. Readings: (a) the column is
  a limit, not the exact longest length; (b) the file or the column is wrong.
- Taken: (a). The SHA-256 decides that the file is right. The decoder builds the `ML` decode table
  for 8 bits, which works for either. The other tables reach their limit exactly (`TOK` 11, `LL` 8,
  literal tables 10, `OWN` 8).

## 15. Decoding — "Two things follow from the rules"

> a valid block has `n >= 1`, ... and the first match's offset is at most that sequence's `ll`.

- Readings: (a) these are consequences, and need no check of their own; (b) they are extra rules.
- Taken: (a), as "follow from the rules" says. The decoder has no separate `n >= 1` check; the
  `off > |out|` check rejects such blocks. The tests check that `n = 0` blocks are rejected.

## 16. What the compressor writes — not rules of validity

> Not part of the format: any block that decodes is fine

- The section lists choices that look like rules: `w` is the smallest that holds every `s[j]`, the
  escape only for tokens without a code, class 0 when the offset repeats, coded literals only when
  they save enough, deltas only with coded literals. Taken: none of them is checked. A larger `w`, an
  escaped token with its own code, or class 3 for offset 2 is valid. The section's first sentence
  decides this.

## 17. The tables — tokens without a code

- `TOK` gives a code to only 641 of its 3073 symbols (the escape, 3072, has 4 bits). The table is still
  complete, so every bit pattern decodes to a symbol that has a code. **[took long]** At first I
  expected all 3072 tokens to need a code.
