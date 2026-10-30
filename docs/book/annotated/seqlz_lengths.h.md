# src/seqlz_lengths.h

The code lengths of every table of the format for 4 KiB pages: one byte per symbol, 0 for a symbol
without a code. [`seqlz_codes.c`](seqlz_codes.c.html) turns them into codes and decode tables. A generator
writes this file from symbol counts; nobody edits it. It is the kernel's `seqlz_lengths.h` but without
`static`, as [`seqlz_lengths.c`](seqlz_lengths.c.html) defines the tables for the tests and tools.
How to read the token rows: a token is `ll + 16 * (ml - 4) + 512 * class`, so one row of 16 numbers
is one match length and one offset class, with `ll` 0 to 15 from left to right
([the token](book:#ch-format--token)). Each offset class takes 32 rows, `ml` 4 to 35 from top to
bottom. [Class 0](#b23), [class 1](#b55), [class 2](#b87), [class 3](#b119), [class 4](#b151),
[class 5](#b183), then [the escape](#b215) and the [`ll`](#b217) and [`ml`](#b221) tables.
@more {#ch-codes--what-the-token-table-looks-like} The shortest token codes, and why `ll + ml` is 8
16 lengths per line, so token `s` is at line `{{ch08.inc.tok_line}} + s / 16`: line
{{ch08.inc.tok49_line}} has tokens 48 to 63, and its second number, token 49, is {{tok.len.49}}. Line
{{ch08.inc.esc_line}} is the escape, {{tok.len.3072}}.
@assert ch08.inc.tok_line + 3 == 26 and ch08.inc.esc_line == ch08.inc.tok_line + 192

Of the 3072 tokens, {{ch08.tok.coded}} have a code of their own, {{tok.with_code}} codes with the
escape. The shortest:

| token | `ll` | `ml` | class | code |
| --- | --- | --- | --- | --- |
| 49 | 1 | 7 | 0, the repeat offset | `{{tok.code.49}}` |
| 2097 | 1 | 7 | 4, a multiple of 8 below 256 | `{{tok.code.2097}}` |
| 34 | 2 | 6 | 0 | `{{tok.code.34}}` |
| 2082 | 2 | 6 | 4 | `{{tok.code.2082}}` |
| 2112 | 0 | 8 | 4 | `{{tok.code.2112}}` |
| 2624 | 0 | 8 | 5, a multiple of 8 from 256 to 2040 | `{{tok.code.2624}}` |
@assert tok.codes_of[4] == 3 and tok.codes_of[5] == 3 and tok.len[3072] == tok.len[49] == tok.len[2097] == 4 and tok.len[34] == tok.len[2082] == tok.len[2112] == 5 and tok.len[2624] == tok.len[576] == 6

In each, `ll + ml` is 8: one sequence per 8-byte value, a pointer or a field, whose low 1 or 2 bytes
differ from a value 8 or more bytes before, or none do. Among the codes of {{tok.len.576}} bits is also
token 576, no literals and 8 bytes from offset 2, class 1: a run of one 16-bit value.
@end

## Where the numbers come from

@@ 1-22
`gen-seqlz-lengths.py` reads `bench/seqlz_counts_4k.txt`: how often each symbol occurred on about 1.1
million training pages from a phone and a desktop. It turns the counts into an optimal prefix code
whose longest code has 11 bits, by package-merge, which is Huffman's idea with a length limit. A
frequent symbol gets a short code.

Only the 640 most frequent of the 3072 tokens get a code; all others go through the escape. The
SPDX line is in the kernel's order. The head comment names the command that wrote the file: in the
kernel's copy it is `scripts/gen-seqlz-lengths.py lib/seqlz/seqlz_counts.txt`, without `--extern`.
@anim huffman Building a Huffman code from counts, by hand
@more {#ch-codes--training-counts-then-lengths} From counts to lengths: what the generator does
`tools/kernel-port/gen-seqlz-lengths.py --extern` makes this file from `bench/seqlz_counts_4k.txt`, how
often each token, each `ll` and `ml` symbol and each byte as a literal occurred on the training pages,
and for each literal table how many pages it codes:

- Each table is an optimal **length-limited** prefix code, by package-merge (Larmore and Hirschberg):
  the code with the fewest bits for these counts among those whose longest code is 11, 8 or 10 bits. A
  plain Huffman code has no such limit.
- Every count gets 1 added, so every length value and every literal gets a code, even one that never
  occurred, which `has_all_symbols()` checks.
- Only the most frequent tokens get a code. The generator tries the 64, 128, 192, ... most frequent
  plus the escape, prices each choice on the training pages with the escaped tokens at the escape's
  code plus 12 bits, and keeps the cheapest whose escape code has at most 7 bits. For 4 KiB pages that
  is {{ch08.tok.coded}} tokens, {{ch08.tok.coded_64}} times 64.
- It checks the lengths as `seqlz_tables_build()` does, `has_all_symbols()` included, before it writes
  anything.
@assert ch08.tok.coded % 64 == 0

`tools/seqlz-ref/check.sh`, in CI, generates this file and `seqlz_lengths_16k.h` again and fails if one
differs by a byte. `seqlz_ref.py`, the decoder written from the spec, checks the SHA-256 of every table against
[`docs/format.md`](https://github.com/martinus/seqlz/blob/main/docs/format.md#where-the-tables-come-from),
e.g. `{{ch08.tok4k_sha}}...` for the 4 KiB token table, and the test `seqlz_train` checks that
`seqlz-train --from-counts` in C++ gives the same tables.

::: try Check a table's hash yourself
From the repository's root:
```python
import hashlib, re
t = open("src/seqlz_lengths.h").read()
tok = [int(x) for x in re.findall(r"\d+", re.search(r"\.token = \{(.*?)\}", t, re.S).group(1))]
print(len(tok), hashlib.sha256(bytes(tok)).hexdigest())
print(sum(1 for l in tok[:3072] if l), "tokens with a code, escape", tok[3072], "bits")
print(sum(2.0 ** -l for l in tok if l))
```
This prints {{tok.symbols}} and the `TOK_4k` hash of `format.md`, `{{ch08.tok.coded}} tokens with a code, escape {{tok.len.3072}} bits`, and
`1.0`, the Kraft sum of a complete code.
:::
@end

## The token table, by offset class

@@ 23-54
Class 0, the repeat offset: the offset of the match before, in 0 bits. Line 26 has tokens 48 to 63,
`ml` 7; its second number is token 49, 1 literal and 7 bytes, with 4 bits one of the two shortest token
codes. That is the sequence of an array of pointers whose lowest byte differs.

The first column is all 0: with no literals, a match at the same offset would have been part of the
match before, so such tokens hardly occur, and none got a code. Line 54 is `ml` 35 or more, a length value follows.

@@ 55-86
Class 1, the single offset 2 in 1 bit: runs of 16-bit values. Every token here with a code has no
literals, `ll` 0. Line 59, token 576, is 8 bytes of a repeated 16-bit value, 6 bits.

@@ 87-118
Class 2, an offset of 1 to 255 in 8 bits. Its first column has a code in every row, and the class's
shortest, 7 bits for token 1040: a match right after a match, at a new near offset. The last column,
`ll` 15, has codes in 14 of the 32 rows: it stands for every literal run of 15 or more.

@@ 119-150
Class 3, any offset, 12 bits on 4 KiB pages. The same picture as class 2, codes from 7 bits on, so a
sequence here takes at least 19 bits before its literals and length values.

@@ 151-182
Class 4, multiples of 8 below 256 in 5 bits: the pointers and 8-byte fields of memory. Token 2097,
line 154, has the other 4-bit code: 1 literal and 7 bytes from a new offset, the same shape as token 49.

@@ 183-214
Class 5, multiples of 8 up to 2040 in 8 bits. The same shapes as class 4, with codes 1 or 2 bits longer:
tokens 2594, 2609 and 2624, where `ll + ml` is 8, have 6 bits.

## The escape and the length values

@@ 215-216
Symbol 3072, the escape, has 4 bits, as short as the most frequent token: the 2432 tokens without a
code are each rare but frequent together: about 15 of about 205 sequences per page on desktop pages
and 10 of 170 on phone pages (20 000 pages of each dump, `seqlz_find()` and the tables of 14th October
2026, same-filled pages left out, measured on 14th October). An escaped token costs 4 + 12 bits. The
format allows at most 7 bits here on 4 KiB pages; [the escape](book:#ch-header--escape) has why the
rare tokens do not all get a code.

@@ 217-220
`ll`'s length values, for literal runs of 15 or more: symbols 0 to 15 are the values themselves, 16 to
23 the values 16 to 4095, one power of 2 each, with plain bits after the code. The shortest codes, 3
bits, are the values 0 and 1 and the ranges 16 to 31 and 32 to 63. Every symbol has a code, also one
that never occurred: the generator adds 1 to every count.

@@ 221-225
`ml`'s, for matches of 35 bytes or more, `ml - 35`. Symbol 16, the values 16 to 31, has 2 bits:
matches of 51 to 66 bytes are the most frequent range of long ones. Codes of length values are at most 8 bits, a
decode table of 256 entries.

## Eight tables, one per kind of page

@@ 226-227
The 8 literal tables, and the code `OWN`, as code lengths: one byte per byte value, 16 per line, so
line `n` of a table is the bytes `16 * n` to `16 * n + 15`. Generated from the same counts as the
token table ([how they are trained](seqlz_lengths.c.html#b13)).

A page with coded literals names one of the 8 tables in the low bits of its byte 2; the compressor picks the one that
codes the page's literals in the fewest bits. Each table is complete, codes 2 to 10 bits, and every
byte value has a code in every table.

Each table is a group of training pages that k-means found, and the length-limited Huffman code of
their literals. The generator adds 1 to every count, so a byte that never occurred in a group still
gets a code, of 10 bits: the encoder needs a code for any byte zram hands it.

@@ 228-245
Table 0, text. The space, `0x20`, and `e`, `0x65`, have the shortest codes, 4 bits; `\n` (line 229, the 11th
number) has 5. Line 235 is `` ` `` and `a` to `o`, 4 to 8 bits except `` ` `` and `j`. The bytes from `0x80` on,
lines 237 to 244, are all 10 bits: rare in this group.
@more {#ch-codes--table-0-line-17} The bytes `0x60` to `0x6f` of table 0, byte by byte
Line {{ch08.litinc.line60}} is the bytes `0x60` to `0x6f`, the backtick and `a` to `o`, with their
code lengths in table 0:

| byte | `` ` `` | `a` | `b` | `c` | `d` | `e` | `f` | `g` | `h` | `i` | `j` | `k` | `l` | `m` | `n` | `o` |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| bits | {{lit.0.len.96}} | {{lit.0.len.97}} | {{lit.0.len.98}} | {{lit.0.len.99}} | {{lit.0.len.100}} | {{lit.0.len.101}} | {{lit.0.len.102}} | {{lit.0.len.103}} | {{lit.0.len.104}} | {{lit.0.len.105}} | {{lit.0.len.106}} | {{lit.0.len.107}} | {{lit.0.len.108}} | {{lit.0.len.109}} | {{lit.0.len.110}} | {{lit.0.len.111}} |
@assert ch08.litinc.line60 == ch08.litinc.line + 6 and min(lit[0].len[0x61], lit[0].len[0x62], lit[0].len[0x63], lit[0].len[0x64], lit[0].len[0x65], lit[0].len[0x66], lit[0].len[0x67], lit[0].len[0x68], lit[0].len[0x69], lit[0].len[0x6b], lit[0].len[0x6c], lit[0].len[0x6d], lit[0].len[0x6e], lit[0].len[0x6f]) == 4 and max(lit[0].len[0x61], lit[0].len[0x62], lit[0].len[0x63], lit[0].len[0x64], lit[0].len[0x65], lit[0].len[0x66], lit[0].len[0x67], lit[0].len[0x68], lit[0].len[0x69], lit[0].len[0x6b], lit[0].len[0x6c], lit[0].len[0x6d], lit[0].len[0x6e], lit[0].len[0x6f]) == 8 and lit[0].len[0x60] > 8 and lit[0].len[0x6a] > 8 and lit[1].len[0] == lit[4].len[0] == 2 and lit[7].len[0xff] == 2 and ch08.lit[0].max == ch08.lit[1].max == ch08.lit[2].max == ch08.lit[3].max == ch08.lit[4].max == ch08.lit[5].max == ch08.lit[6].max == ch08.lit[7].max == 10

The other 7 tables differ: tables 1 and 4 give `0x00` 2 bits, table 7 gives `0xff`
{{lit.7.len.255}} bits. Every table has codes of 10 bits.
@end

@@ 246-263
Table 1, binary data: `0x00` has 2 bits, a quarter of the code space, then `0x01` at 5 bits and
small numbers and powers of 2 at 6.

@@ 264-299
Table 2: upper-case letters, `_` and digits at 5 and 6 bits. Table 3: text again, `e` at 4 bits, with
`0x00` at 5 bits among the letters.

@@ 300-335
Table 4: `0x00` at 2 bits, then lower-case letters. Table 5: tab and the digits at 4 bits, numbers
written as text.

@@ 336-353
Table 6: `0x00` at 3 bits, `0xff` at 4, then `0x48`, `0x89` and `0x8b` at 5: bytes that are common in x86-64
machine code, where `0x48` is the prefix of a 64-bit operation and `0x89` and `0x8b` are `mov`.

@@ 354-373
Table 7: `0xff` at 2 bits, the last number of the table, and everything else 7 to 10 bits:
pages with many `0xff` bytes. [The measurement](book:#ch-codes--lit-sets-file) has why 8 tables and
not 16.

## The code OWN

@@ 374-379
The 19 code lengths of `OWN`, the code that a level 3 page writes its own literal table in. Symbols 0
to 9 are the lengths 1 to 10, so the first line's 2, 2 at positions 8 and 9 say that lengths 9 and 10
are the most frequent in such tables: most bytes are rare. Symbols 10 to 18 are runs of bytes without
a code ([format](book:#ch-format--own)).
