# src/seqlz.h

The interface of the codec for this repository's own programs: the tests, the benchmark harness, the
fuzzers and the tools. The kernel does not get this file; zram calls the two functions of
`include/linux/seqlz.h`, written from `tools/kernel-port/seqlz.h`
([chapter 15](book:#ch-zram-glue--interface)). The `/**` comments are kernel-doc, the kernel's format
for documenting a function.

A reading order:

1. [`seqlz_compress()`](#b114) and [`seqlz_decode()`](#b49): what zram's glue calls, with the two
   promises the rest of the codec keeps.
2. [`SEQLZ_COMPRESS_BOUND`](#b12), the size of `dst` that always works.
3. For tests and tools: [`seqlz_find()`](#b68) and [`seqlz_encode()`](#b83), the compressor cut in
   two halves, and [`seqlz_tables_build()`](#b25).

## Types and the bound

@@ 1-11
It includes `seqlz_internal.h`, so a test that includes `seqlz.h` reaches every constant and every
table of the codec, not just these functions.

@@ 12-17
Two blocks, zram's buffer of two pages. With this much room `seqlz_compress()` never fails, even on
random data, and writes no byte past it however large `dst` is. The proof is in the encoder
([why two pages fit](book:#ch-encoder--layout)).

@@ 18-24
A sequence for code outside the compressor: `seqlz_find()` fills an array of them, `seqlz_encode()`
reads one. The compressor itself never builds this array. Its matcher hands each sequence to the
encoder as three arguments the moment it is found, which saves a pass over the page.

## The tables

@@ 25-40
The decoder only needs a complete prefix code: then every pattern of bits starts with some code, and
no lookup can land on an empty entry. The encoder needs more: a code for every symbol it may have to
write, as it cannot refuse a page for one rare token. The last sentence of the `Return:` checks that,
so a bad table fails when the tables are generated at build time, and the encoder needs no check per
page.

No function takes the tables as a parameter. They are one global `const` struct, `seqlz_tables`,
generated as C during the build ([at build time](book:#ch-codes--kernel)). Tests that build other tables
pass a struct of their own.
@more {#ch-header--tables} One global table for every CPU
The codes for the encoder and the decode tables are {{size.tables|grp}} bytes on both page sizes, one
global `const` struct, `seqlz_tables` ([its layout](#ch-internal--tables)), which every CPU reads. So
they are no part of the work memory per CPU. A program for the build machine writes the struct as C
when seqlz is built, in userspace as in the kernel, so a program that compresses calls nothing first.
@assert size.tables == k16.size.tables
@end
@more {#ch-header--refused} The ways `seqlz_tables_build()` refuses lengths
Four failures are the ways lengths are no complete prefix code
([complete codes](#ch-compression--complete)): a code longer than its table, e.g. a token code of 12
bits; a sum of $2^{-l}$ above 1, where codes would share entries; below 1, a gap; or no code at all.
The fifth is the encoder's, a symbol it may have to write without a code: for a token that means
neither a code nor the escape, for a length value or a literal no code.

`-EINVAL` is the kernel's convention, a negative error number, 0 for success. The tests pass made-up
lengths to check that bad ones are refused, into a struct of their own: the global is `const`,
nothing can replace it.
@end

@@ 41-48
The format's tables as code lengths, one byte per symbol: the sequences' tables, the 8 fixed literal
tables of 256 bytes each, and the fixed code in which a page's own literal table is written. These
arrays are the format; everything else is computed from them ([tables as code lengths](book:#ch-header--lengths)).

## Decoding

@@ 49-64
@anchor ch-header--encode-decode
Lines 56 to 58 are the most important promise of the codec. zram stores no checksum, so a page damaged
in memory reaches the decoder as it is. A decoder that wrote outside its buffers would turn a damaged
page into a damaged kernel ([how the decoder keeps it](book:#ch-decode--safety)).

`scratch` may be `NULL` for a device at level 1, whose compressor never codes literals; that saves
the scratch per CPU ([chapter 15](book:#ch-zram-glue--backend)). There is no partial result: either
`dst` holds the page, or `-EINVAL`. The page does not say which tables wrote it.

## Compressing

@@ 65-67
Every sequence with a match covers at least 4 bytes, so a 4 KiB block has at most 1024 of them, plus
the last one, which only has literals: 1025, and {{k16.const.MAX_SEQUENCES}} on 16 KiB pages.
`seqlz_find()` needs room for that many and `seqlz_encode()` rejects more, and the decoder's work per
page is bounded by the same count.

@@ 68-82
The matcher alone, without the encoder. The table trainer counts the symbols of exactly these
sequences, and the tests and a fuzzer check that `seqlz_encode()` of them gives the same bytes as
`seqlz_compress()`. At level 3 the matcher is another one, so the sequences differ. It does not copy
the literals; a tool takes them from the block.

@@ 83-113
The encoder alone, for sequences from outside: the tests, a fuzzer, and `tools/seqlz-worst/`, which
writes valid pages the matcher would never produce, to find the slowest ones to decode.

It checks the rules of lines 100 to 103 before it writes anything. It did not always: a match shorter
than 4 bytes made `ml - 4` wrap to a huge number, and 1025 such sequences wrote past the two pages of
`dst` (fixed in commit `256768e`, with `fuzz/seqlz_encode_fuzz.c`, a fuzzer that feeds the encoder
raw sequences). Nothing checks that `block` matches the sequences: a wrong one gives
a page that decodes to something else. 0 can mean failure because every page has at least its 2-byte
header.

@@ 114-139
What zram calls, through the glue. A `dst` smaller than two blocks also works, e.g. the single page of
zswap, the kernel's other compressed swap, a cache in RAM in front of a swap device. But a page that
does not fit fails up to 32 bytes early: before each sequence the encoder checks for its literals plus
`ENC_ROOM`, 32 bytes, not for the exact bytes.

The literals are written raw first, so the page must fit raw even if coding would shrink it. Level 3
also needs a 6-byte record per sequence with literals, for its deltas
([records](book:#ch-encoder--records)); if they do not fit, it matches the page again without them.
In the kernel copy this is the `static` `seqlz_compress_block()`; the public `seqlz_compress()` of
`include/linux/seqlz.h` calls it, returns `-EINVAL` for a level outside 1 to 3, and `-E2BIG` instead
of 0 ([the interface](book:#ch-zram-glue--interface)).
@anim ends The buffer dst during a real page, from both ends, then turned around
