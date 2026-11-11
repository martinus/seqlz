# src/seqlz.h

The interface of the codec for this repository's own programs: the tests, the benchmark harness, the
fuzzers and the tools, on top of the kernel's interface, [`linux/seqlz.h`](linux-seqlz.h.html), which
they get through `seqlz_internal.h`. The kernel does not get this file. The `/**` comments are
kernel-doc, the kernel's format for documenting a function.

A reading order:

1. [`seqlz_compress_block()`](#b95): the compressor with the state and the level as the code has them,
   which the kernel's `seqlz_compress()` calls.
2. For tests and tools: [`seqlz_find()`](#b49) and [`seqlz_encode()`](#b64), the compressor cut in
   two halves, in [`seqlz_harness.c`](seqlz_harness.c.html), and [`seqlz_tables_build()`](#b19).

The kernel-doc of the functions describes all levels, as the functions take every level. What it says
about `SEQLZ_LEVEL_OWN` is level 3's ([Part IV](book:#part-level-3)).
@intro 2
Level 2's one line here: the declaration of its 8 fixed literal tables.
@intro 3
Level 3's one line here: the declaration of the fixed code in which a page's own literal table is
written. The level 3 parts of the functions' comments are in level 1's blocks, at
[`seqlz_encode()`](#b64) and [`seqlz_compress_block()`](#b95).

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
The codes for the encoder and the decode tables are {{size.tables|grp}} bytes, one
global `const` struct, `seqlz_tables` ([its layout](#ch-internal--tables)), which every CPU reads. So
they are no part of the work memory per CPU. A program for the build machine writes the struct as C
when seqlz is built, in userspace as in the kernel, so a program that compresses calls nothing first.
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

@@ 35-38
The format's tables as code lengths, one byte per symbol: the sequences' tables, the tokens' and the
two length values'. These arrays are the format; everything else is computed from them
([tables as code lengths](book:#ch-header--lengths)).

@@ 39-41
The 8 fixed literal tables of level 2 as code lengths, one byte for each of the 256 byte values. They
are defined in [`seqlz_lengths.h`](seqlz_lengths.h.html), and [`build_lit_sets()`](seqlz_codes.c.html#b129)
makes the codes from them.

@@ 42-45
The fixed code in which a level 3 page writes its own literal table, as code lengths, one byte for each
of its {{own.symbols}} symbols ([the code](seqlz_codes.c.html#b148)).

## Compressing

@@ 46-48
Every sequence with a match covers at least 4 bytes, so a 4 KiB block has at most 1024 of them, plus
the last one, which only has literals: 1025.
`seqlz_find()` needs room for that many and `seqlz_encode()` rejects more, and the decoder's work per
page is bounded by the same count.

@@ 49-63
The matcher alone, without the encoder, in [`seqlz_harness.c`](seqlz_harness.c.html#b39). The table
trainer counts the symbols of exactly these sequences. The tests and a fuzzer check that
`seqlz_encode()` of them gives the same bytes as `seqlz_compress_block()`. It does not copy the
literals; a tool takes them from the block. Level 3 has a matcher of its own, so there the sequences
differ ([Part IV](book:#part-level-3)).

@@ 64-94
The encoder alone, also in [`seqlz_harness.c`](seqlz_harness.c.html#b109), for sequences from
outside: the tests, a fuzzer, and `tools/seqlz-worst/`, which writes valid pages the matcher would
never produce, to find the slowest ones to decode. At level 1, `st` and `block` may be `NULL`; they
are for level 3's deltas and its own table ([Part IV](book:#part-level-3)).

It checks the rules of lines 81 to 84 before it writes anything. 0 can mean failure because every page
has at least its 2-byte header.
@more Why it checks the sequences, and what it does not check
It did not always check them: a match shorter than 4 bytes made `ml - 4` wrap to a huge number, and
1025 such sequences wrote past the two pages of `dst`. Commit `256768e` fixed it, with
`fuzz/seqlz_encode_fuzz.c`, a fuzzer that feeds the encoder raw sequences. Nothing checks that `block`
matches the sequences: a wrong one gives a page that decodes to something else.
@end

@@ 95-122
The compressor itself: what the harness's [zram glue](zram_seqlz.c.html#b40) calls, and the
kernel's [`seqlz_compress()`](seqlz_compress.c.html#b104). In the kernel's copy it is `static`;
`seqlz_compress()` returns `-EINVAL` for a level that is not built, and `-E2BIG` where this returns 0.

A `dst` smaller than two blocks also works, e.g. the single page of zswap, the kernel's other
compressed swap, a cache in RAM in front of a swap device. But a page that does not fit fails up to 32
bytes early: before each sequence the encoder checks for its literals plus `ENC_ROOM`, 32 bytes, not
for the exact bytes. The rest of the comment is about levels 2 and 3, which write the literals raw
first and code them afterwards ([Part III](book:#part-level-2), [Part IV](book:#part-level-3)).
@more What levels 2 and 3 keep in `dst` and `st`
Why raw first: the encoder writes each sequence's literals the moment the matcher finds it, but which
literal table codes them best, and whether coding saves enough at all, follows only from the literals
of the whole page. So the page is written once with raw literals, and the coder makes a second pass
over them. The price is the one the comment states: the page has to fit raw.

The coded literals end up where the raw ones were, so one of the two has to wait somewhere else, and
that is `st`, free once the matcher is through. Level 2 writes its coded streams there and copies them
over the raw literals at the end; level 3 the other way round, it copies the raw literals or their
deltas there and writes the coded ones in place ([the coder](#ch-literals-enc--overview)). Level 3's
records, 6 bytes per sequence with literals ([records](#ch-encoder--records)), take room from the
bitstream in `dst`; a page that does not fit with them is matched again without them.
@end
@anim ends The buffer dst during a real page, from both ends, then turned around
