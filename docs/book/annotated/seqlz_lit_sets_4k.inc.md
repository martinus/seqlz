# src/seqlz_lit_sets_4k.inc

The 8 literal tables for 4 KiB pages, and the code `OWN`, as code lengths: one byte per byte value,
16 per line, so line `n` of a table is the bytes `16 * n` to `16 * n + 15`. Generated from the same
counts as the token table; nobody edits it ([the literal tables](book:#ch-codes--lit-sets-file)).

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
pages with many `0xff` bytes. The chapter's
[measurement](book:#ch-codes--lit-sets-file) has why 8 tables and not 16.

## The code OWN

@@ 156-159
The 19 code lengths of `OWN`, the code that a level 3 page writes its own literal table in. Symbols 0
to 9 are the lengths 1 to 10, so the first line's 2, 2 at positions 8 and 9 say that lengths 9 and 10
are the most frequent in such tables: most bytes are rare. Symbols 10 to 18 are runs of bytes without
a code ([format](book:#ch-format--own)).
