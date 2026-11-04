# src/seqlz_harness.c

What only the tests and tools use of the compressor, and the kernel does not have:
[`seqlz_find()`](#b39), the sequences the matcher finds, and [`seqlz_encode()`](#b131), a page written
from given sequences with the compressor's encoder. The table trainer counts the symbols of
`seqlz_find()`'s sequences, and a fuzzer checks that `seqlz_encode()` of them gives the bytes
`seqlz_compress_block()` gives.

## seqlz_find()

@@ 1-16
The encoder of `seqlz_encoder.h` for `seqlz_encode()`. The matcher comes below, once more, with
another receiver.

@@ 17-38
The simplest receiver of the matcher's sequences: it stores the three numbers into an array and
ignores the literals. The matcher of `seqlz_match.h` calls `encode_seq()` on a `struct seqlz_encoder`,
as the compressor needs it; the two `#define`s around the `#include` make it call `find_seq()` on a
`struct find_recorder` here. A second copy of the matcher, inlined into `seqlz_find()`; the
compressor's copy calls the encoder by name, as the kernel's does. The macros only rename what comes
after them, so `seqlz_encoder.h`, included above, keeps its names, and after the `#undef`s
`encode_raw()` below gets the real encoder again.

@@ 39-46
@anchor ch-encoder--find
`seqlz_find()` is not for zram: the table trainer counts the symbols of exactly these sequences, and a
fuzzer checks that `seqlz_encode()` of them gives the same bytes as `seqlz_compress_block()`. The kernel copy
does not have it. A level outside 1 to `SEQLZ_LEVEL_MAX` gives no sequences, as the other two entry
points give no page for it.

@@ 47-48
One call, with `lazy` computed from the level: speed does not matter here, so this copy of the
matcher tests `lazy` at run time. The two copies in `seqlz_compress.c` get a constant each.

@@ 49-53
Below level 3 the matcher is never lazy, and `f.n` is the number of sequences it found.

## encode_raw() and seqlz_encode()

@@ 54-68
The same loop as the matcher's, over the given sequences, and the last sequence gets `ml` 0 whatever its
`match` says. `rec` is the constant `true`, and `e.rec` decides, as speed does not matter here.

@@ 69-102
The encoder for sequences from outside, for tests and tools. The sequences are checked by the rules a
decoder checks before a byte is written.

A match below 4 bytes is invalid, and worse: `ml - 4` wraps, a huge length value follows,
and the room proof no longer holds. Before commit `256768e` 1025 such sequences wrote past the two
pages of `dst` ([why](book:#ch-encoder--raw)).
@more {#ch-encoder--raw} The checks before a byte is written, and the bug they stop
A `dst` below {{ch10.dst_min}} bytes would start `p` below `lit + 32`, or below `lit`, where the room
check's cast would turn a negative room into a huge one. The sequences are checked by the rules
[the decoder](#ch-decode) checks.

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

`src_end` is the end of the caller's literals.

@@ 103-106
Level 3 with a page writes the records for the deltas, as `compress_own()` does.

@@ 107-107
All sequences into the encoder.

@@ 108-119
At level 3, once more without the records if the page does not fit with them, as `compress_own()` does.
`seqlz_delta_literals()` then writes the literals' deltas to `deltas`, and `*delta` is the delta it
picked, or 0.

@@ 120-122
The page with raw literals, or 0.

@@ 123-130
For level 2 without a state, the slots of the literal coder in a static buffer of a state's size: one
thread at a time, which the tests and tools are.

@@ 131-142
The public entry for sequences from outside. The level is checked before anything is written, so a level
outside 1 to `SEQLZ_LEVEL_MAX` returns 0 as [the header](seqlz.h.html#b64) says. The kernel copy does
not have this function.

@@ 143-151
Level 3 needs the state, and returns 0 without it: `encode_raw()` with the page for the records, then
the deltas and maybe a table of the page's own in `seqlz_code_own()`.

@@ 152-153
Levels 1 and 2: the page with raw literals.

@@ 154-159
At level 2 `encode_raw()` has left the raw literals in `dst`, as `seqlz_compress_block()` has them, and
`seqlz_code_literals()` codes them with the slots in `st`, or in `encode_slots`. Until 1st November 2026
it called `seqlz_code_raw_literals()`, which read them from `literals`, and until 23rd October 2026 it
copied the literals into a block on its stack, a frame of 4240 bytes where `src/` is built into a
kernel (commit `d8937df`).

@@ 160-161
Level 1's page as it is.
