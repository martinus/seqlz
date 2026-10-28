# src/seqlz_default_tables_4k.inc

The code lengths of the token table and the two tables of length values for 4 KiB pages: one byte per
symbol, 0 for a symbol without a code. `seqlz_codes.c` turns them into codes and decode tables
([chapter 8](book:#ch-codes--lengths)). A generator writes this file from symbol counts; nobody edits
it.

How to read the token rows: a token is `ll + 16 * (ml - 4) + 512 * class`, so one row of 16 numbers
is one match length and one offset class, with `ll` 0 to 15 from left to right
([the token](book:#ch-format--token)). Each offset class takes 32 rows, `ml` 4 to 35 from top to
bottom. [Class 0](#b11), [class 1](#b43), [class 2](#b75), [class 3](#b107), [class 4](#b139),
[class 5](#b171), then [the escape](#b203) and the [`ll`](#b205) and [`ml`](#b209) tables.

## Where the numbers come from

@@ 1-10
`gen-seqlz-lengths.py` reads `bench/seqlz_counts_4k.txt`: how often each symbol occurred on about 1.1
million training pages from a phone and a desktop. It turns the counts into an optimal prefix code
whose longest code has 11 bits, by package-merge, which is Huffman's idea with a length limit
([training](book:#ch-codes--training-counts-then-lengths)). A frequent symbol gets a short code.

Only the 640 most frequent of the 3072 tokens get a code; all others go through the escape. The
SPDX line is in the kernel's order, as `port.py` writes it too.
@anim huffman Building a Huffman code from counts, by hand

## The token table, by offset class

@@ 11-42
Class 0, the repeat offset: the offset of the match before, in 0 bits. Line 14 has tokens 48 to 63,
`ml` 7; its second number is token 49, 1 literal and 7 bytes, with 4 bits one of the two shortest token
codes. That is the sequence of an array of pointers whose lowest byte differs.

The first column is all 0: with no literals, a match at the same offset would have been part of the
match before, so such tokens hardly occur, and none got a code. Line 42 is `ml` 35 or more, a length value follows.

@@ 43-74
Class 1, the single offset 2 in 1 bit: runs of 16-bit values. Every token here with a code has no
literals, `ll` 0. Line 47, token 576, is 8 bytes of a repeated 16-bit value, 6 bits.

@@ 75-106
Class 2, an offset of 1 to 255 in 8 bits. Its first column has a code in every row, and the class's
shortest, 7 bits for token 1040: a match right after a match, at a new near offset. The last column,
`ll` 15, has codes in 14 of the 32 rows: it stands for every literal run of 15 or more.

@@ 107-138
Class 3, any offset, 12 bits on 4 KiB pages. The same picture as class 2, codes from 7 bits on, so a
sequence here takes at least 19 bits before its literals and length values.

@@ 139-170
Class 4, multiples of 8 below 256 in 5 bits: the pointers and 8-byte fields of memory. Token 2097,
line 142, has the other 4-bit code: 1 literal and 7 bytes from a new offset, the same shape as token 49.

@@ 171-202
Class 5, multiples of 8 up to 2040 in 8 bits. The same shapes as class 4, with codes 1 or 2 bits longer:
tokens 2594, 2609 and 2624, where `ll + ml` is 8, have 6 bits.

## The escape and the length values

@@ 203-204
Symbol 3072, the escape, has 4 bits, as short as the most frequent token: the 2432 tokens without a
code are each rare but frequent together. An escaped token costs 4 + 12 bits. The format allows at most
7 bits here on 4 KiB pages ([the escape](book:#ch-header--escape)).

@@ 205-208
`ll`'s length values, for literal runs of 15 or more: symbols 0 to 15 are the values themselves, 16 to
23 the values 16 to 4095, one power of 2 each, with plain bits after the code. The shortest codes, 3
bits, are the values 0 and 1 and the ranges 16 to 31 and 32 to 63. Every symbol has a code, also one
that never occurred: the generator adds 1 to every count.

@@ 209-213
`ml`'s, for matches of 35 bytes or more, `ml - 35`. Symbol 16, the values 16 to 31, has 2 bits:
matches of 51 to 66 bytes are the most frequent range of long ones. Codes of length values are at most 8 bits, a
decode table of 256 entries.
