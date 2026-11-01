# Claims in format.md that follow from the rules or the tables and could be wrong

Written from `format.md` only. No code was written and no table file was opened, so nothing that
needs the tables was checked. "Checked by hand" means worked out on paper from format.md's own rules.

## Found by hand: claims that are wrong or contradict each other

- **C6**: `read(3)` of the bits 0, 1, 1 is 3, not 6, by the definition of `read()`.
- **C22 / C26 / C61**: the `ML` table can't both have a longest code of 8 bits and give symbol 23 the 7-bit
  code `1111111`, as the example says. In a complete canonical code, a code of only ones has the
  longest length: 8-bit codes would start at `first[8] = (127 + 1) * 2 = 256`, and that needs 9 bits.
- **C49**: `out[p − x]` is not always "of the match before or a literal of the same sequence".
- **C69**: "the values of byte 2 with a code above 8 ... are invalid" holds for coded literals only.
- **C5**: "a block of 4 KiB is too small to carry tables of its own" doesn't fit level 3, where a block
  brings its own literal table.

## The claims

| ID | section | sentence quoted | how it could be checked |
| --- | --- | --- | --- |
| C1 | intro | "level 2 codes them with one of 8 fixed tables ... A decoder for one decodes all of them." | `code` 0 to 7 are 8 tables (✓ by hand). Decode the blocks of all 3 levels with one decoder. |
| C2 | Status | "There is no format for blocks of 16 KiB." | Look for anything left over: the Example still says "with the 4 KiB tables", and "How this file was checked" still talks about 16 KiB blocks. Check that no rule depends on another block size. |
| C3 | Status | "This is the format of 21st October 2026" / "Until 1st November 2026 this file also had blocks of 16 KiB" | Compare with the history of format.md and `src/`: did the format of 4 KiB blocks change after 21 October (the deltas were tested on 22 October)? |
| C4 | The idea | "The last sequence has literals only, the ones left, and no bits: they fill the rest of the block, and that is how a decoder knows it is the last." | ✓ by hand against the loop (`n − used == BLOCK − \|out\|`). |
| C5 | The idea | "*Why:* a block of 4 KiB is too small to carry tables of its own." | Compare with "A block's own literal table" (level 3, `code = 8`): true for `TOK`, `LL`, `ML`, not for the literals. |
| C6 | Notation | "`read(3)` of the bits 0, 1, 1 is the number 6; the same bits as a code are the bit string `011`." | By hand: `read()` reads the most significant bit first, so 0, 1, 1 gives `0*4 + 1*2 + 1 = 3`. **Wrong as written.** 6 would be the bits 1, 1, 0. See QUESTIONS Q1. |
| C7 | Block size | "`P` 12, `BLOCK` 4096, `LEN_SYMBOLS` 24, `12 + P`" | ✓ by hand: 2^12 = 4096; 16 symbols of their own + symbols 16 to `11 + P` (P − 4 = 8) = 24 = 12 + P. |
| C8 | Layout | "Its highest bit, `h & 0x8000` (bit 7 of byte 1)" | ✓ by hand (little endian). |
| C9 | Layout | "15 bits could count up to 32767" | ✓ 2^15 − 1. |
| C10 | Layout | "`w = 5 + ((t >> 4) & 7)`" / "`w` is 5 to 12" | ✓ 3 bits, 0 to 7. |
| C11 | Layout | "8 sizes of `w` bits fill exactly `w` bytes" | ✓ 8w bits = w bytes. |
| C12 | Layout | the offsets in the coded table: `3 + e`, `3 + w + e`, `3 + w + e + d`, ...; "`start[0] = 3 + w + e + d`"; the own table "starts at byte `3 + w + e`, right after `dt` and the sizes" | ✓ by hand: sizes from `3 + e` for `w` bytes end at `3 + e + w`. |
| C13 | Layout | "*Why:* a stream holds at most every 8th literal, of at most 10 bits, so at most `BLOCK * 10 / 64` bytes, 640 in a 4 KiB block" | ✓ arithmetic: 512 literals × 10 bits = 5120 bits = 640 bytes. Needs every literal code ≤ 10 bits: the longest code of the 8 literal tables (table: 10) and the own table's lengths 1 to 10 (✓). |
| C14 | Layout | "*Why:* the largest stream of most blocks is below 128 bytes, so 7 bits each do" | 7 bits hold up to 127 (✓). "Most blocks": measure the largest `s[j]` of the compressor's coded blocks. |
| C15 | Layout | "*Why:* ... and 2 bytes each made blocks about 5 bytes larger on average." | 8 × 2 = 16 bytes against `w` bytes (5 to 12) is 4 to 11 bytes more per coded block; "about 5 on average" over all blocks needs the share of coded blocks and their `w`. Measure. |
| C16 | Prefix codes | "the bits `0 11 10` are A C B" | ✓ by hand. |
| C17 | Prefix codes | "as in deflate (RFC 1951, section 3.2.2)" | Compare steps 1 to 4 with RFC 1951's `next_code` algorithm (`code = (code + bl_count[bits-1]) << 1`, `bl_count[0] = 0`). |
| C18 | Prefix codes | "the lengths A 1, B 2, C 2 give A the number 0, B 2 and C 3" | ✓ by hand: `first[2] = (0 + 1) * 2 = 2`. |
| C19 | Prefix codes | "Every table is **complete** ... `2^-l` summed over all codes is exactly 1. Then every long enough string of bits starts with exactly one code, and a decoder never meets bits that are no code." | Sum `2^-l` over each of `TOK`, `LL`, `ML`, the 8 literal tables and `OWN`: each must be exactly 1. The second sentence follows (✓). |
| C20 | The tables | "`TOK` \| 3073 \| 11 bits" | Length of the table = 3073 (= 3072 tokens + escape, ✓); largest length = 11. |
| C21 | The tables | "`LL` \| `LEN_SYMBOLS` \| 8 bits" | 24 entries, largest 8. |
| C22 | The tables | "`ML` \| `LEN_SYMBOLS` \| 8 bits" | 24 entries, largest 8. **Contradicts the example**, see C61. |
| C23 | The tables | "literal tables 0 to 7 \| 256 each \| 10 bits" | 8 × 256 = 2048 entries; largest 10 (each table, or over all? QUESTIONS Q14). |
| C24 | The tables | "`OWN` \| 19 \| 8 bits" | 19 entries (✓ by hand: symbols 0 to 9 for lengths, 10 to 18 for skips); largest 8. |
| C25 | The tables | the table in the kernel's copy and the one outside it | ✓ by hand: same rows, only the header path differs. |
| C26 | The tables | "`ML` ... 8 bits" together with the example's "`1111111` \| `symbol(ML)` = 23" | By hand: they can't both hold (see top). Look up `ML[23]` and the largest `ML` length. |
| C27 | Where the tables come from | "each literal table's over 10 590 pages or more" / "The tables are counted on 1 091 983 pages" | Against `bench/seqlz_counts.txt`: the page counts per literal table and their sum. |
| C28 | Where the tables come from | "`tools/seqlz-ref/check.sh` checks in CI that they are its output, byte for byte" / "the test `seqlz_train` checks that it gives the same tables" | Run the generator on the counts and compare with the tables. |
| C29 | The tables | the five SHA-256 values | `sha256sum` of each table's lengths as bytes; for `LIT` the 8 tables in the order 0 to 7, 2048 bytes. |
| C30 | Bitstream | "bit `i` of `B` is bit `7 - (i mod 8)` of byte `i div 8`, as in the literal streams" | ✓ same formula in "Coded literals" and in the own table. |
| C31 | Bitstream | "*Why:* ... in a table indexed by them each code's entries are one range ...; read the other way round, the entries of a code are spread over the whole table." | By hand: MSB first, a code `c` of length `l` in a table of `2^L` entries has `[c << (L−l), (c+1) << (L−l))`; LSB first, its entries are every `2^l`-th. ✓ |
| C32 | Bitstream | "A decoder may read zeros past the end ... and check at the end that it did not use them; ... the result is the same as checking at every read." | By hand: a block that needs a bit past `len` is invalid either way, and the sequence loop ends (C45). Also check that a decoder reading zeros always ends in the own table (QUESTIONS Q9). Fuzz both ways. |
| C33 | Length values | "A length value is a number from 0 up to `2^P - 1`." | ✓ by hand: symbol 23, `b = 11`, `2^11 + 2^11 − 1 = 4095`. |
| C34 | Length values | "The symbols 16 to `11 + P` stand for the ranges `[2^b, 2^(b+1))` with `b = s - 12`, i.e. 16 to 31, 32 to 63" | ✓ by hand. |
| C35 | Length values | "100 is in `[64, 128)`, so `b = 6`, the symbol is 18, and `read(6)` gives `100 - 64 = 36`" | ✓ by hand. |
| C36 | Length values | "*Why `2^P - 1`:* the largest value a block needs is `ll - 15` of `BLOCK - 4` literals before a block's only match, `BLOCK - 19`" | ✓ by hand: `ll ≤ BLOCK − 4` (I5, the match takes ≥ 4), `4092 − 15 = 4077`. The largest `ML` value is smaller: `ml ≤ BLOCK − 1`, `4095 − 35 = 4060`. |
| C37 | Tokens | "That is 16 * 32 * 6 = 3072 tokens." / "`c = token >> 9` ... 0 to 5" | ✓ 3072; 3071 >> 9 = 5. |
| C38 | Tokens | "*Why:* ... shorter matches cost more bits than the literals they replace." | With the tables: the cheapest 3-byte match (shortest token code + offset bits, class 0 has none) against 3 raw literals (24 bits) or 3 coded ones. |
| C39 | Tokens | "*Why:* there are only 2048 codes of 11 bits, fewer than the 3072 tokens, and most tokens are rare, so only the frequent ones have a code." | 2^11 = 2048 ✓. With 11 bits at most, a complete table has at most 2048 codes, so at least 1025 of the 3073 symbols have length 0; count them in `TOK`. |
| C40 | Offsets | the column "offsets it can send": 2; 1 to 255; 1 to `BLOCK − 1`; 8 to 248; 8 to 2040 | ✓ by hand, without 0 (R17). |
| C41 | Offsets | "Class 1 stores the offset divided by 2, classes 4 and 5 divided by 8." | ✓ |
| C42 | Offsets | "*Why:* ... Offset 2 ... is the most frequent of all" | Against the counts / class 1's token counts. |
| C43 | Decoding | "with `off = 1` and `ml = 10` it repeats the last byte 10 times" | ✓ |
| C44 | Decoding | "*Why:* ... a token for the last sequence would only say what the decoder knows already, in about 1.2 bytes per block." | Measure: the bits a token (and its values) for the last sequence would take, on average. |
| C45 | Decoding | "Every sequence before the last writes at least 4 bytes, so a decoder stops after at most `BLOCK / 4 + 1` sequences, also on blocks that are not valid." | ✓ by hand: after 1024 full sequences `\|out\| = 4096`, and the next one stops or fails R21. |
| C46 | Decoding | "a valid block has `n >= 1` ... and the first match's offset is at most that sequence's `ll`" | ✓ by hand (I1, I2). |
| C47 | Coded literals | "A stream without literals has `s[j] = 0`." | ✓ follows from R13. |
| C48 | Literals as deltas | "`x = 4` for `last` 1 or 2, 6 for `last = 3`, else `last`" | ✓ smallest multiple ≥ 4. |
| C49 | Literals as deltas | "`out[p - x]` is a byte already written, of the match before or a literal of the same sequence" | By hand: **not always**. With `dt = 2` after a 4-byte match, the first literal is at `p`, so `p − 8` is before that match. With `dt = 3` and `last = 4000`, `p − x` can be anywhere. "Already written" holds (`x ≥ 1`). See QUESTIONS Q11. |
| C50 | Literals as deltas | "*Why:* ... With `x` at least 4, the bytes a word of 4 adds are all written before it" | ✓ `p − x ≤ p − 4`. |
| C51 | Own table | "symbol `l - 1`, for `l` from 1 to 10 ...; symbol `9 + k`, for `k` from 1 to 9" | ✓ symbols 0 to 18, 19 = `OWN`'s size. A skip covers `2^(k−1)` to `2^k − 1` values, 1 to 511 in all. |
| C52 | Own table | "`k = 1` is one byte value, `read(0)` is 0." | ✓ |
| C53 | Own table | "*Why:* ... the long lengths, which most bytes of a block's own table have, get the shortest codes in `OWN`." | Compare the lengths of `OWN` symbols 6 to 9 (lengths 7 to 10) with the rest. |
| C54 | Own table | "*Why:* A table takes 0.4 to 0.5 bytes per byte value with a code, 40 to 100 bytes on most blocks that have one." | That means about 80 to 250 byte values with a code. Bounds from `OWN`: between 1 and 8 bits per symbol. Measure on level 3's blocks. |
| C55 | Example | "The 9 bytes `02 00 61 62 23 f2 ff fb 60` are a 4 KiB block of `ab` 2048 times" | ✓ by hand, provided C57 and C61 hold: 2 + 4094 = 4096 bytes, offset 2 repeats `ab`. |
| C56 | Example | "`02 00`: `h = 2`, raw literals, `n = 2`; then the literals `61 62`, `a` and `b`." | ✓ |
| C57 | Example | "`0010` \| `symbol(TOK)` = 3072" | `TOK[3072] = 4` and its canonical code is `0010`. By hand, this means: since 3072 is the highest symbol, it is the last 4-bit code. So `TOK` has no codes of 1 or 2 bits, and either one 3-bit code `000` and the escape as its only 4-bit code, or no 3-bit code and exactly two 4-bit token codes `0000`, `0001` before the escape. In both cases a token code of only zeros, 3 or 4 bits (RULES V13). |
| C58 | Example | "the escape: the token has no code of its own" | `TOK[1010] = 0`. |
| C59 | Example | "`001111110010` \| `read(12)` = 1010 \| `ll = 2`, `mlf = 31`, class 1" | ✓ by hand: 0x3f2 = 1010 = 512 + 31 × 16 + 2. |
| C60 | Example | "`1` \| `read(1)` = 1 \| `off = 1 * 2 = 2`" | ✓ |
| C61 | Example | "`1111111` \| `symbol(ML)` = 23, `b = 11`" | `ML[23] = 7` and its code is all ones. By hand, that makes 7 the longest `ML` length, against C22. |
| C62 | Example | "`11111011011` \| `read(11)` = 2011 \| `ml = 35 + 2^11 + 2011 = 4094`" | ✓ by hand: 2011 in 11 bits is `11111011011`; 35 + 2048 + 2011 = 4094. |
| C63 | Example | "`B` is `23 f2 ff fb 60`, 40 bits" / "0 literals left for 0 bytes of the block" / "`00000` the rest of the last byte" | ✓ by hand: 4 + 12 + 1 + 7 + 11 = 35 bits used, 5 zero bits left; `n − used = 0 = BLOCK − \|out\|`. |
| C64 | What the compressor writes | "each offset with class 0 when it is the last one, else the class with the fewest offset bits that holds it" | ✓ by hand, there's never a tie: 2 → class 1; multiples of 8 up to 248 → class 4; other offsets up to 255 → class 2; multiples of 8 from 256 to 2040 → class 5; the rest → class 3. |
| C65 | What the compressor writes | "the smallest `w` that holds every `s[j]`, at least 5" | ✓ `w` can't be below 5 anyway. |
| C66 | What the compressor writes | "`m` is not the size of the header" | ✓ the header is `3 + w + e` = 8 to 16 bytes, not 35 or 19. |
| C67 | What the compressor writes | "Its code lengths are close to a Huffman code's, at most 10 bits" | ✓ fits the own table's lengths 1 to 10. |
| C68 | What the compressor writes | "zram gives it two pages, which every block fits" | Bound by hand: raw at most 2 + 4096 bytes plus `B`; literals cost ≤ 10 bits coded; a sequence with `ll < 15`, `mlf < 31` costs ≤ 16 (escape) + 12 (offset) bits per ≥ 4 bytes; so about ≤ 16 + 260 (own table) + 5120 bytes, below 8192. Confirm with the actual `TOK` longest code and escape length. |
| C69 | No version in the block | "The values of byte 2 with a code above 8, and the values of `dt` other than 1 to 3, are invalid now" | By hand: holds for coded literals only. In a raw block byte 2 is a literal and may be anything (RULES V14). 7 × 16 = 112 values of byte 2, 253 of `dt`. |
| C70 | How this file was checked | "for those, the CI's seeds include blocks with `dt` 1 and 3" | `dt = 2` is not named. Check the seeds; the 22 October run covered "every delta byte", so it covers `dt = 2`. |
| C71 | How this file was checked | "a block of literals only, a last sequence with a token, which is invalid" | ✓ fits I6 and the stop rule (but see RULES V13: a "token" of only zero bits that fits in the padding is valid). |

## Numbers that don't follow from the rules or the tables

These are measurements or history, and can only be checked against the repository, the counts or
the runs: 1 091 983 pages; 10 590 pages; 200 000 pages; three sessions of a Xiaomi Mi 9T; the dates
21 October, 28 September, 6, 9 and 22 October and 1 November 2026; 10 and 12 AFL++ instances, 2 hours,
1.7 and 1.94 billion inputs; commit `5342a9b`; 15 000 and 2088 pages, 5.8 million blocks; and the
compressor's thresholds `m` = 35 / 19, `n div 16`, every 7th / 11th literal, 512 literals, 32 bytes,
20 bytes.
