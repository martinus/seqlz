# src/seqlz_harness.c

What only the tests and tools use of the compressor, and the kernel does not have:
[`seqlz_find()`](#b39), the sequences the matcher finds, and [`seqlz_encode()`](#b108), a page written
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

@@ 39-50
@anchor ch-encoder--find
`seqlz_find()` is not for zram: the table trainer counts the symbols of exactly these sequences, and a
fuzzer checks that `seqlz_encode()` of them gives the same bytes as `seqlz_compress_block()`. The kernel copy
does not have it. A level outside 1 to `SEQLZ_LEVELS` gives no sequences, as the other two entry
points give no page for it.

One call, with `lazy` computed from the level: speed does not matter here, so this copy of the
matcher tests `lazy` at run time. The two copies in `seqlz_compress.c` get a constant each.

## encode_raw() and seqlz_encode()

@@ 51-83
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

@@ 84-107
The same loop as the matcher's, over the given sequences; `src_end` is the end of the caller's
literals, and the last sequence gets `ml` 0 whatever its `match` says. At level 3 with the records, and
once more without them if the page does not fit, as `compress_own()` does: `rec` is the constant
`true` and `e.rec` decides, as speed does not matter here. `seqlz_delta_literals()` then writes the
literals' deltas to `deltas`, and `*delta` is the delta it picked, or 0.

@@ 108-129
The public entry for sequences from outside. The level is checked before anything is written, so a
level outside 1 to `SEQLZ_LEVELS`, or level 3 without the state, returns 0 as
[the header](seqlz.h.html#b60) says. At level 2 it codes the literals where the caller has them, since
its state may be `NULL`; until 23rd October 2026 it copied them into a block on its stack, a frame of
4240 bytes where `src/` is built into a kernel (commit `d8937df`). At level 3 with the deltas and
maybe a table of the page's own. The kernel copy does not have this function.
