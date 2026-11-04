# src/seqlz_compress.c

The compressor: the encoder of `seqlz_encoder.h` and the matcher of `seqlz_match.h` put together. The
matcher finds a page's sequences, and the encoder writes each one the moment it is found, as one loop
per level. In the kernel's copy both headers are in this file, in that order, right before
`compress_block()`.

A reading order that follows the call:

1. [`seqlz_compress()`](#b109), the kernel's interface, which zram calls, and
   [`seqlz_compress_block()`](#b77), which it calls: levels 1 and 2 go to
   [`compress_block()`](#b16), level 3 to [`compress_own()`](#b35).
2. The encoder, in [`seqlz_encoder.h`](seqlz_encoder.h.html), and the matcher, in
   [`seqlz_match.h`](seqlz_match.h.html).

## The two halves
@@ 1-15
The kernel headers the file uses, including those of the two headers, which the kernel's copy has in
this file. Then the encoder and the matcher, both inlined into the functions below.

@@ 16-34
Levels 1 and 2: the greedy matcher and the encoder, the whole compression loop once inlined.

The check of `dst_cap` is here and in `compress_own()`, not once in `seqlz_compress_block()`: there it took
1% more instructions at levels 1 and 2, on 2000 pages of a desktop dump with gcc 16.2 on x86-64, 23rd
October 2026 (commit `75756b7`).

@@ 35-76
Level 3: the lazy matcher with records. A page that does not fit with its records is matched once
more without them, as `encoder_init()` clears `e.rec`; a loop and not two calls, so the inlined matcher
is in the function once. Then `seqlz_delta_literals()` picks the delta, moves the literals down over
the records and writes their deltas to `st->literals`, and `encoder_finish()` finds `dst` as it would
without records.

`noinline` on purpose: inlined next to `compress_block()`, levels 1 and 2 wrote their pages slower.

@more {#ch-encoder--wrappers} Level 3's records in a copy of the loop of their own, measured
The first version, with the XOR that came before the deltas, checked `e->rec` for every sequence in
the loop all levels share, and the decoder its XOR the same way. Levels 1 and 2 then wrote pages about
6% slower on the Mi 9T's Cortex-A76 and read them slower too, userspace, 21st October 2026
([explored-designs.md](https://github.com/martinus/seqlz/blob/main/docs/explored-designs.md#level-3-xors-its-literals-with-an-earlier-byte-of-the-page-38-fewer-bytes-than-seqlz-3-of-15-october-kept)).
With the records only in the lazy matcher's copy, and the decoder's loop copied for pages with deltas
([the decoder's two copies](#ch-decode--deltas)), levels 1 and 2 write and read as fast as before, within the
run's spread.
@end

@@ 77-81
What zram calls, through the interface's `seqlz_compress()` below.

@@ 82-85
Level 3's delta, from `compress_own()` to `seqlz_code_own()`.

@@ 86-93
`dst_cap` is cut to `SEQLZ_COMPRESS_BOUND`, since the bitstream starts at the end of `dst`: with a
larger `dst` it would land far from the page, in memory nothing else of it touches.

@@ 94-99
Level 3: [`compress_own()`](#b35), then `seqlz_code_own()` with the delta it picked.

@@ 100-100
Levels 1 and 2: the greedy matcher and the encoder, the page with raw literals.

@@ 101-104
Level 2 codes the literals with one of 8 fixed tables where that saves enough, using the matcher's
8 KiB hash table for the coded streams, so it needs no memory of its own.
@more {#ch-encoder--entry} Three levels on two pages, and a `dst` larger than two pages
`seqlz_code_literals()` codes the literals with one of the 8 fixed literal tables if that saves
enough, and returns the new length or `len` unchanged; `seqlz_code_own()` does the same at level 3,
also with a table of the page's own. Both use `st->literals`, the matcher's 8 KiB table as bytes
through the union in `struct seqlz_state` ([what lies where](#ch-header--state)): level 2 writes the
coded streams into slots there and copies them over the raw literals once all are read
([the slots](seqlz_compress_literals.c.html#b309)), level 3 mostly reads the literals from a copy there
while it writes the coded ones over the raw ones in `dst` ([in place](#ch-literals-enc--in-place)).
`seqlz_code_own()` also uses
[`own_work(st)`](seqlz_compress_literals.c.html#b921), the upper half of the same 8 KiB.

The text page of [Part IV](#ch-format--own) shows what the levels do to one page: {{words.len}}
bytes at level 1, {{words.coded.len}} at level 2 and {{words.l3.len}} at level 3, without deltas; the
page of pointers {{pointers.len}} bytes at level 1, {{delta.ptr.l2.len}} at level 2 and
{{delta.ptr.l3.len}} at level 3, with them.
@assert words.l3.delta == 0 and pointers.l3.delta == 1
@assert delta.ptr.l2.len < pointers.len

Every page fits into `SEQLZ_COMPRESS_BOUND` ([two pages](#ch-encoder--two-pages)), and until 24th
October 2026 a larger `dst` was not cut: with a `dst` of 64 KiB the bitstream went 56 KiB past the
page, in memory the page has nothing else in (commit `8fed16a`). zram passes exactly two pages, so its
pages did not change.

In the kernel's copy `seqlz_compress_block()` is `static` and has no test of the level: the
interface's `seqlz_compress()` [below](#b109) is its one caller and tests it.
@end

@@ 105-108
A build of level 1 alone stops here: the page keeps its raw literals.

## The kernel's interface

@@ 109-128
@anchor ch-zram-glue--api
The interface's `seqlz_compress()`, which zram's backend calls, declared in
[`linux/seqlz.h`](linux-seqlz.h.html). The compressor's 0 becomes `-E2BIG`, "output too small", and a
level outside 1 to `SEQLZ_LEVEL_MAX` is `-EINVAL`. The `static_assert`s tie the interface's numbers to
the codec's: the work memory is the matcher's state, and the interface's levels start at 1 as the
codec's do, so `seqlz_compress()` passes `level` on unchanged; the levels above, 2 and 3, are checked
where a build has them ([`seqlz_internal.h`](seqlz_internal.h.html#b103)). The last one ties the 32 bytes that `seqlz_compress()`'s
kernel-doc names ([`linux/seqlz.h`](linux-seqlz.h.html#b59)) to `ENC_ROOM`, so the documentation
cannot drift from the encoder.

::: kernel EXPORT_SYMBOL_GPL
Modules are linked at load time against a table of exported symbols, and a function is in it only if
its file says `EXPORT_SYMBOL()` or `EXPORT_SYMBOL_GPL()`. The `_GPL` variant serves only modules whose
`MODULE_LICENSE()` is GPL-compatible. zram's backend can be in a zram module, so it needs the export.
Outside the kernel, [`linux/export.h`](book:#ch-linux) makes it a declaration that does nothing.
:::
