# src/seqlz_lit_sets_4k.inc

The 8 literal tables for 4 KiB pages, and the code `OWN`, as code lengths: one byte per byte value,
16 per line, so line `n` of a table is the bytes `16 * n` to `16 * n + 15`. Generated from the same
counts as the token table; nobody edits it ([how they are trained](seqlz_lit_sets.c.html#b1)).

A page with coded literals names one of the 8 tables in the low bits of its byte 2; the compressor picks the one that
codes the page's literals in the fewest bits. Each table is complete, codes 2 to 10 bits, and every
byte value has a code in every table.

## Eight tables, one per kind of page

@@ 1-9
Each table is a group of training pages that k-means found, and the length-limited Huffman code of
their literals. The generator adds 1 to every count, so a byte that never occurred in a group still
gets a code, of 10 bits: the encoder needs a code for any byte zram hands it.

@@ 10-27
Table 0, text. The space, `0x20`, and `e`, `0x65`, have the shortest codes, 4 bits; `\n` (line 11, the 11th
number) has 5. Line 17 is `` ` `` and `a` to `o`, 4 to 8 bits except `` ` `` and `j`. The bytes from `0x80` on,
lines 19 to 26, are all 10 bits: rare in this group.
@more {#ch-codes--table-0-line-17} Line 17 of table 0, byte by byte
Line {{ch08.litinc.line60}} is the bytes `0x60` to `0x6f`, the backtick and `a` to `o`, with their
code lengths in table 0:

| byte | `` ` `` | `a` | `b` | `c` | `d` | `e` | `f` | `g` | `h` | `i` | `j` | `k` | `l` | `m` | `n` | `o` |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| bits | {{lit.0.len.96}} | {{lit.0.len.97}} | {{lit.0.len.98}} | {{lit.0.len.99}} | {{lit.0.len.100}} | {{lit.0.len.101}} | {{lit.0.len.102}} | {{lit.0.len.103}} | {{lit.0.len.104}} | {{lit.0.len.105}} | {{lit.0.len.106}} | {{lit.0.len.107}} | {{lit.0.len.108}} | {{lit.0.len.109}} | {{lit.0.len.110}} | {{lit.0.len.111}} |
@assert ch08.litinc.line60 <= 17 and min(lit[0].len[0x61], lit[0].len[0x62], lit[0].len[0x63], lit[0].len[0x64], lit[0].len[0x65], lit[0].len[0x66], lit[0].len[0x67], lit[0].len[0x68], lit[0].len[0x69], lit[0].len[0x6b], lit[0].len[0x6c], lit[0].len[0x6d], lit[0].len[0x6e], lit[0].len[0x6f]) == 4 and max(lit[0].len[0x61], lit[0].len[0x62], lit[0].len[0x63], lit[0].len[0x64], lit[0].len[0x65], lit[0].len[0x66], lit[0].len[0x67], lit[0].len[0x68], lit[0].len[0x69], lit[0].len[0x6b], lit[0].len[0x6c], lit[0].len[0x6d], lit[0].len[0x6e], lit[0].len[0x6f]) == 8 and lit[0].len[0x60] > 8 and lit[0].len[0x6a] > 8 and lit[1].len[0] == lit[4].len[0] == 2 and lit[7].len[0xff] == 2 and ch08.lit[0].max == ch08.lit[1].max == ch08.lit[2].max == ch08.lit[3].max == ch08.lit[4].max == ch08.lit[5].max == ch08.lit[6].max == ch08.lit[7].max == 10

The other 7 tables differ: tables 1 and 4 give `0x00` 2 bits, table 7 gives `0xff`
{{lit.7.len.255}} bits. Every table has codes of 10 bits.
@end

@@ 28-45
Table 1, binary data: `0x00` has 2 bits, a quarter of the code space, then `0x01` at 5 bits and
small numbers and powers of 2 at 6.

@@ 46-81
Table 2: upper-case letters, `_` and digits at 5 and 6 bits. Table 3: text again, `e` at 4 bits, with
`0x00` at 5 bits among the letters.

@@ 82-117
Table 4: `0x00` at 2 bits, then lower-case letters. Table 5: tab and the digits at 4 bits, numbers
written as text.

@@ 118-135
Table 6: `0x00` at 3 bits, `0xff` at 4, then `0x48`, `0x89` and `0x8b` at 5: bytes that are common in x86-64
machine code, where `0x48` is the prefix of a 64-bit operation and `0x89` and `0x8b` are `mov`.

@@ 136-155
Table 7: `0xff` at 2 bits, the last number of the table, and everything else 7 to 10 bits:
pages with many `0xff` bytes. [The measurement](book:#ch-codes--lit-sets-file) has why 8 tables and
not 16.

## The code OWN

@@ 156-159
The 19 code lengths of `OWN`, the code that a level 3 page writes its own literal table in. Symbols 0
to 9 are the lengths 1 to 10, so the first line's 2, 2 at positions 8 and 9 say that lengths 9 and 10
are the most frequent in such tables: most bytes are rare. Symbols 10 to 18 are runs of bytes without
a code ([format](book:#ch-format--own)).
