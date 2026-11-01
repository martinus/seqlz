# src/seqlz.h

The interface of the codec for this repository's own programs: the tests, the benchmark harness, the
fuzzers and the tools, on top of the kernel's interface, [`linux/seqlz.h`](linux-seqlz.h.html), which
they get through `seqlz_internal.h`. The kernel does not get this file. The `/**` comments are
kernel-doc, the kernel's format for documenting a function.

A reading order:

1. [`seqlz_compress_block()`](#b91): the compressor with the state and the level as the code has them,
   which the kernel's `seqlz_compress()` calls.
2. For tests and tools: [`seqlz_find()`](#b45) and [`seqlz_encode()`](#b60), the compressor cut in
   two halves, in [`seqlz_harness.c`](seqlz_harness.c.html), and [`seqlz_tables_build()`](#b19).

## Types

@@ 1-11
It includes `seqlz_internal.h`, so a test that includes `seqlz.h` reaches every constant and every
table of the codec, besides these functions.

@@ 12-18
A sequence for code outside the compressor: `seqlz_find()` fills an array of them, `seqlz_encode()`
reads one. The compressor itself never builds this array. Its matcher hands each sequence to the
encoder as three arguments the moment it is found, which saves a pass over the page.

## The tables

@@ 19-34
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

@@ 35-41
The format's tables as code lengths, one byte per symbol: the sequences' tables, the 8 fixed literal
tables of 256 bytes each, and the fixed code in which a page's own literal table is written. These
arrays are the format; everything else is computed from them ([tables as code lengths](book:#ch-header--lengths)).

## Compressing

@@ 42-44
Every sequence with a match covers at least 4 bytes, so a 4 KiB block has at most 1024 of them, plus
the last one, which only has literals: 1025, and {{k16.const.MAX_SEQUENCES}} on 16 KiB pages.
`seqlz_find()` needs room for that many and `seqlz_encode()` rejects more, and the decoder's work per
page is bounded by the same count.

@@ 45-59
The matcher alone, without the encoder, in [`seqlz_harness.c`](seqlz_harness.c.html#b39). The table trainer counts the symbols of exactly these
sequences, and the tests and a fuzzer check that `seqlz_encode()` of them gives the same bytes as
`seqlz_compress_block()`. At level 3 the matcher is another one, so the sequences differ. It does not copy
the literals; a tool takes them from the block.

@@ 60-90
The encoder alone, also in [`seqlz_harness.c`](seqlz_harness.c.html#b108), for sequences from outside: the tests, a fuzzer, and `tools/seqlz-worst/`, which
writes valid pages the matcher would never produce, to find the slowest ones to decode.

It checks the rules of lines 77 to 80 before it writes anything. It did not always: a match shorter
than 4 bytes made `ml - 4` wrap to a huge number, and 1025 such sequences wrote past the two pages of
`dst` (fixed in commit `256768e`, with `fuzz/seqlz_encode_fuzz.c`, a fuzzer that feeds the encoder
raw sequences). Nothing checks that `block` matches the sequences: a wrong one gives
a page that decodes to something else. 0 can mean failure because every page has at least its 2-byte
header.

@@ 91-118
What the harness's zram glue calls, and the kernel's `seqlz_compress()`. A `dst` smaller than two blocks also works, e.g. the single page of
zswap, the kernel's other compressed swap, a cache in RAM in front of a swap device. But a page that
does not fit fails up to 32 bytes early: before each sequence the encoder checks for its literals plus
`ENC_ROOM`, 32 bytes, not for the exact bytes.

The literals are written raw first, so the page must fit raw even if coding would shrink it; the coded
ones then go over them, and meanwhile `st` holds the coded streams below level 3, the raw literals or
their deltas at level 3 ([the coder](book:#ch-literals-enc--overview)). Level 3
also needs a 6-byte record per sequence with literals, for its deltas
([records](book:#ch-encoder--records)); if they do not fit, it matches the page again without them.
In the kernel copy it is `static`: the kernel's [`seqlz_compress()`](seqlz_compress.c.html#b98)
calls it, returns `-EINVAL` for a level outside 1 to 3, and `-E2BIG` instead of 0.
@anim ends The buffer dst during a real page, from both ends, then turned around
