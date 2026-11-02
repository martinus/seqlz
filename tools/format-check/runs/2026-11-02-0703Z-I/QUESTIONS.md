# Places in format.md that can be read two ways

Each entry quotes format.md, then gives the readings. "Matters for validity" says whether the two readings
accept different blocks.

## Q1. Is `dt` checked when `e = 0`?
Layout: "with coded literals `len < 3` …, `len < 3 + w + e`, `dt` is not 1, 2 or 3, `code > 8`, …"

- A: `dt` exists only with `e = 1` (layout table: "with `e = 1` the byte `dt` …, else nothing"), so the rule applies only then.
- B: the rule is listed for every coded block, so byte 3 must be 1–3 even with `e = 0`. In that case it is the first stream-size byte.

A is meant, almost surely. The list should say "with `e = 1`, `dt` is not 1, 2 or 3". **Matters for validity.**

## Q2. Does a run "go past 255" when it ends exactly at 255?
Own table: "a run of byte values without a code goes past 255" / "A run past 255 makes the block invalid at once".

- A: past 255 means it covers byte value 256 or more (`v + 2^(k-1) + r > 256`).
- B: a run that reaches 255 is already "past" the last byte value that can have a code.

Validity is the same either way: after a run that ends at 255 the sum is still below 1, and every next symbol is invalid (RULES I12). Only the point where the decoder stops differs ("at once" or one symbol later). That changes nothing unless an error code or position is reported. **Does not matter for validity.**

## Q3. How far does a *Why:* reach: one sentence or the rest of the paragraph?
Intro: "Sentences marked *Why:* explain a choice and are not part of the format". Several *Why:* paragraphs go on with sentences that read like rules or claims:

- Layout: "*Why:* a stream holds … 5 bytes larger on average. The codes 9 to 15 are kept for later."
- Bitstream: "*Why:* zram stores no checksum, … and the compressor writes nothing else. A decoder may read zeros past the end instead of checking at every read, …; `seqlz_decompress()` does that, and the result is the same as checking at every read."
- Decoding: "*Why:* a decoder knows `n` and `BLOCK`, … in about 1.2 bytes per block. Every sequence before the last writes at least 4 bytes, so a decoder stops after at most `BLOCK / 4 + 1` sequences, also on blocks that are not valid."
- Literals as deltas: "*Why:* … which the literal tables code in fewer bits. With `x` at least 4, … a decoder can add 4 bytes at a time."
- Own table: "*Why:* … A table takes 0.4 to 0.5 bytes per byte value with a code, …"

Readings: A, only the first sentence after the marker is not normative. B, the whole paragraph after the marker is not. Under B, the permission to read zeros past the end, and the bound of sequences, are not part of the format. **Does not matter for validity** (they are permissions and consequences), but it matters for what a decoder may do.

## Q4. Is the order of the pseudocode in Decoding normative for the order of the bits?
Decoding: "`token: ll, mlf, c` / `x = read(offset bits of class c)` / … / `if ll == 15: ll = 15 + value(LL)` / … / `if mlf == 31: ml = 35 + value(ML)`". Tokens: "Each sequence but the last starts with a **token**".

- A: the bits are in the order of the pseudocode: token, offset bits, LL value, ML value.
- B: the pseudocode only says what is computed, and the bits could be token, LL value, offset bits, ML value (lz4-like), or any order.

The prose never says the order. The example fixes "offset bits before the ML value", but it has no LL value, so the place of the LL value relative to the offset bits rests on the pseudocode alone. A is meant. **Matters for validity and for the decoded content.**

## Q5. "the offset of the sequence before" for `dt = 3` and class 0
Offsets: "class 0 … the offset of the sequence before". Literals as deltas: "`last` is the offset of the sequence before, 1 for the first".

- A: it is the variable `last`: the offset the sequence before used, also when that came from class 0 (so it is the last offset sent), and 1 before any match.
- B: "the sequence before" is the one just before in the block. A reader could take "the first" in "1 for the first" as the first *literal* of the block, or ask what it is for the last sequence of a block without matches.

The Decoding pseudocode settles it as A (`last = off` after every match, `last` starts at 1), and "the last sequence's literals the offset of the last match". For `n = BLOCK`, `last = 1` gives `x = 4`. **A is settled by the pseudocode.**

## Q6. Is "byte `p` of the block" a position in the output or in the literals?
Literals as deltas: "Take a literal that `copy()` writes to byte `p` of the block, where `out` is the block as far as it is written, so `out[p - x]` is the byte `x` before it."

- A: `p` is the position in `out`, from 0 (the literal is `out[p]`).
- B: `p` is the literal's index in `lits`, and `p - x` is an earlier literal (a delta to the literal `x` literals before).

The text and the *Why:* ("a literal after a match often differs only a little from the byte the match would have copied") mean A. Positions from 0 come from "bytes 0 and 1". B is a plausible misreading because of "each literal was stored as its delta to the byte `x` before it". **Matters for decoded content, not for validity** (I16).

## Q7. What counts as "the last bit it needs" and "a bit read past the end"?
Bitstream: "after the last bit it needs, fewer than 8 bits of `B` are left" / Decoding: "at the stop: no bit read past the end of B".

- A: bits *used* by the decoding (as with a decoder that reads one bit at a time).
- B: bits *fetched*, so a decoder that loads its bit buffer ahead would reject a valid block near the end.

The next sentences ("A decoder may read zeros past the end … check at the end that it did not use them") settle it as A, but the pseudocode's "no bit read past the end" says "read". **Settled as A by the next sentence.** The pseudocode wording is weaker.

## Q8. Are bits after the last sequence "a token"?
How this file was checked: "a token after the last sequence whose bits are not 0, which is invalid".

- A: after the last sequence there are only padding bits. They must be fewer than 8 and 0 (R12, R12b), and nothing is parsed as a token.
- B: a token may follow the last sequence as long as its bits are 0. This could mean a zero-bit `TOK` code is tolerated even if it spans 8 or more bits.

The Bitstream section ("fewer than 8 bits of `B` are left") rules out B for 8 or more bits. The test description reads as if zero bits are a token. **A; the sentence should say "bits after the last sequence that are not 0".**

## Q9. Is the bound of `n` part of "both layouts" or only of the list?
Layout: "`n > BLOCK` is invalid in both layouts" and the list "`len < 2`, or `n > BLOCK`".

There is no real second reading. But the text gives no hint that `n` from 4093 to 4095, and `n = 0`, are also always invalid (RULES I1). A reader could take "`n > BLOCK` is invalid" as the complete list of invalid `n`. **No conflict; the narrower range is implied, not said.**

## Q10. "`len < 3 + w + e`": does it apply when the own table or the streams are missing too?
Layout: "`len < 3 + w + e`, …, the block's own table is invalid, or `3 + w + e + d + s[0] + ... + s[7] > len`."

- A: these are separate checks in this order, each assuming the earlier ones passed.
- B: they are one set of conditions, any of which makes the block invalid.

**Does not matter for validity.** R5 and R8e are implied by R9 (RULES R5, R8e). The order matters only for which error a decoder reports, and for what it reads before rejecting (it must not read `S` beyond `len`).

## Q11. What is `len` relative to the own table: "Bits past `len` are read as 0"?
Own table: "Bits past `len` are read as 0, and `3 + w + e + d > len` then makes the block invalid."

- A: the own table's bits come from bytes `3 + w + e` onwards up to `len`, and then zeros. A table running into the streams' bytes is fine while it is being read, because the streams start after `d`.
- B: the table must not read past its own region. That cannot be known before `d` is known.

A is the only consistent one. The table's region is defined by where it ends. **Settled.**

## Q12. The class-1 "offset bits"
Offsets: "| 1 | 1 | `read(1) * 2` | 2 |".

- A: class 1 reads one bit, which must be 1 (a 0 gives offset 0, invalid).
- B: class 1 is offset 2 with no information, and the bit is a check bit or an encoding of 0 vs 2.

The table and R14 are clear: A. The "offsets it can send: 2" column and the *Why:* "gets a class of its own" may make a reader expect 0 bits. **Settled as A**, but worth one sentence in format.md.

## Q13. "`w` the bits of each stream size" and the table row "`3 + e` | `w` |"
Layout: "`w = 5 + ((t >> 4) & 7)` the bits of each stream size"; the table gives `w` as the *size in bytes* of the sizes field.

- A: `w` is both, as the next paragraph says ("so `w` counts both").
- B: a reader of the table alone might think the field is `w` bits.

Settled by the next paragraph. **Settled; mentioned because the table row reads as bytes ("Sizes are in bytes unless they say bits").**

## Q14. "fewer than 8 bits of `B` are left, all in its last byte" for an empty `B`
Bitstream.

- A: with no bit used and `|B| = 0`, nothing is left: valid (a block of literals only).
- B: "its last byte" requires `B` to have a byte, so an empty `B` is outside the rule.

Decoding ("a block of literals only has `n = BLOCK` and no bits in `B`") settles it as A. **Settled.**

## Q15. "The text between the comments … is left out of the kernel's copy"
Intro, and the comment blocks.

- A: the kernel copy is one format, and only the tool and repository text differs.
- B: the kernel copy is a separate text that has to be checked on its own. For example, its "Where the tables come from" says "occurred in the blocks' own tables", while this file says "in the descriptions of the pages that level 3 gives a table of their own". These are the same counts said two ways, but a reader could take the two copies as describing different counts.

**Does not matter for validity.**

## Q16. "Status: the format of 22nd October 2026" vs the removal of 16 KiB on 1st November
Status.

- A: the 4 KiB format is unchanged since 22nd October; only the 16 KiB variant was dropped.
- B: the file has changed after 22nd October, so the date names an older state.

**Does not matter for validity**, but CLAIMS C80 should be checked.
