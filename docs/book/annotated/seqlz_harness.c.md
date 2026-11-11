# src/seqlz_harness.c

What only the tests and tools use of the compressor; the kernel does not have this file.
[`seqlz_find()`](#b39) returns the sequences the matcher finds, and [`seqlz_encode()`](#b133) writes a
page from given sequences, with the compressor's encoder. The table trainer counts the symbols of
`seqlz_find()`'s sequences, and a fuzzer checks that `seqlz_encode()` of them gives the bytes
`seqlz_compress_block()` gives.

@intro 2
At level 2, `seqlz_encode()` codes the literals of the page as `seqlz_compress_block()` does, and needs
[a buffer of its own](#b125) when the caller passes no state.

@intro 3
At level 3, `seqlz_find()` runs the lazy matcher, and `seqlz_encode()` writes the page as
`compress_own()` does: with records, once more without them if it does not fit, then the deltas and
`seqlz_code_own()`.

## seqlz_find()

@@ 1-16
The encoder of `seqlz_encoder.h`, for `seqlz_encode()`. The comment is about the next block: the
matcher, a second time, with another receiver of its sequences.

@@ 17-38
`seqlz_find()` needs the matcher's sequences in an array, not written as a page. The matcher of
`seqlz_match.h` calls `encode_seq()` on a `struct seqlz_encoder`; the two `#define`s around the
`#include` rename both, so this copy of the matcher calls `find_seq()` on a `struct find_recorder`.
`find_seq()` stores the three numbers and ignores the literals, and `rec`, which is for level 3's
records.

The macros rename only what comes after them: `seqlz_encoder.h`, included above, keeps its names, and
after the `#undef`s `encode_raw()` below gets the real encoder again. The compressor's copy of the
matcher calls the encoder by name, as the kernel's does.

@@ 39-46
@anchor ch-encoder--find
`seqlz_find()` runs the matcher on a page with the recorder, and returns how many sequences it found.
A level outside 1 to `SEQLZ_LEVEL_MAX` gives no sequences, as the other two entry points give no page
for it.

@@ 47-48
At level 3 the matcher is lazy. Here `lazy` is computed from the level at run time, as speed does not
matter in tests and tools. The two calls in [`seqlz_compress.c`](seqlz_compress.c.html#b35) pass a
constant each, so each gets a copy of the matcher of its own.

@@ 49-53
Without level 3 the matcher gets `false`, as in [`compress_block()`](seqlz_compress.c.html#b16).
`f.n` is the number of sequences it found.

## encode_raw() and seqlz_encode()

@@ 54-68
`encode_all()` calls `encode_seq()` for each given sequence, as the matcher does for the ones it finds.
The last sequence gets `ml` 0 whatever its `match` says: it has only literals. `rec` is `true`, so
whether records are written is up to `e.rec`, which only level 3 sets ([level 3](#b103)).

@@ 69-102
`encode_raw()` is the part of `seqlz_encode()` that every level shares: the page with raw literals.
The sequences come from a tool or a fuzzer, so the loop walks them once before the encoder sees them,
with `pos` as the decoder's place in the page. E.g. 3 literals, then a match of 5 at offset 4, is
refused: at that point `pos` is 3, and the match would start 1 byte before the page.

`literals + n_literals` is the encoder's `src_end`, where its 16-byte literal copies have to stop
reading. In `compress_block()` that is the page's end, as the literals lie in the page; here they are
a separate array, which may end right after the last literal. `block`, `deltas` and `delta` are for
level 3's deltas.

The comment's last sentence describes the bug that brought these checks: before commit `256768e` a
match of 3 bytes went through, and the encoder wrote past `dst` ([the bug](book:#ch-encoder--raw)).
@more {#ch-encoder--raw} The checks before a byte is written, and the bug they stop
A `dst` below {{ch10.dst_min}} bytes gets 0 at once, as in [`compress_block()`](seqlz_compress.c.html#b16).
Until 10th November 2026 the encoder's room check compared unsigned, and there a `p` below `lit`
would have made a negative room a huge one; since then it compares signed
([why](#ch-encoder--emit)). The sequences are checked by the rules [the decoder](#ch-decode) checks.

`encode_seq()` computes `ml - SEQLZ_MIN_MATCH` unsigned. For `ml` 3 that wraps to
{{ch10.wrap.ml4|grp}}, so a length value follows with {{ch10.wrap.extra}} extra bits, which breaks the
accumulator's budget and the 31 bits per 4 bytes of the room proof. Before commit `256768e` the
encoder checked only that the literals were there, and 1025 such sequences wrote past the two pages of
`dst`; AddressSanitizer reported it in `enc_flush()`
([#121](https://github.com/martinus/seqlz/issues/121)). None of the fuzzers had found it, because
each reached `seqlz_encode()` with valid sequences only. `fuzz/seqlz_encode_fuzz.c` gives it raw
sequences now, and failed within seconds against the old encoder. `seqlz_compress_block()` was never
affected: its matcher makes only valid sequences.
@end

@@ 103-107
Deltas need records: {{delta.rec}} bytes per sequence with literals, where they start, how many, and
the offset before ([the records](book:#ch-encoder--records)). Without `block` there is no page to take
deltas in, so no records either. The encoder writes them into `dst` from `ENC_REC_AT`,
{{delta.rec_at|grp}} bytes in, so a `dst` that ends before that and `ENC_ROOM` gets none, and the page
no deltas. [`compress_own()`](seqlz_compress.c.html#b35) has the same test.

@@ 108-108
All sequences into the encoder.

@@ 109-121
A page that does not fit with its records is written once more without them, as in `compress_own()`.
With records, `seqlz_delta_literals()` picks the delta from them and writes the literals' deltas to
`deltas`; `*delta` is the delta, or 0. Unlike `compress_own()`, the second pass is a second call of
`encode_all()`, not a loop: the harness is not on a hot path, and an `if` with its own `#if` block
leaves a build of fewer levels without a loop that runs once.

@@ 122-124
`encoder_finish()`: the page with raw literals, or 0 if it did not fit.

@@ 125-132
The literal coder of level 2 writes its coded streams into slots, which `seqlz_compress_block()` takes
from the state. `seqlz_encode()` may get no state below level 3 ([the header](seqlz.h.html#b64)), and
then uses this static buffer of a state's size. One buffer for every caller is fine for the tests and
tools, which call it from one thread.

@@ 133-144
The public entry for sequences from outside; the kernel's copy does not have it. The `min()` keeps its
pages equal to the compressor's whatever `dst` a tool passes: both run with the same room, and at level
3 a larger `dst` could have room for records where two pages have none
([the cut in the compressor](seqlz_compress.c.html#b81)). The test `seqlz: compress and encode use no
more of dst than SEQLZ_COMPRESS_BOUND` encodes into 64 KiB and checks that the bytes behind the bound
stay as they were.

@@ 145-153
Level 3 needs the state, for the deltas and the table of the page's own, and returns 0 without it.
`encode_raw()` gets the page for the records, then `seqlz_code_own()` codes the literals, as in
`seqlz_compress_block()`.

@@ 154-155
The page with raw literals. `block` and `deltas` are `NULL`: only level 3 uses them.

@@ 156-161
At level 2, `encode_raw()` has left the raw literals in `dst`, as `compress_block()` does, and
`seqlz_code_literals()` codes them, with the slots in `st` or in [`encode_slots`](#b125).
@more How `seqlz_encode()` got there
Until 1st November 2026 it called `seqlz_code_raw_literals()`, which read the literals from
`literals`, and until 23rd October 2026 it copied the literals into a block on its stack, a frame of
4240 bytes where `src/` is built into a kernel (commit `d8937df`).
@end

@@ 162-163
Level 1's page as `encode_raw()` wrote it, or 0.
