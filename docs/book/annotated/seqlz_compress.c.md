# src/seqlz_compress.c

The compressor: the encoder of `seqlz_encoder.h` and the matcher of `seqlz_match.h` put together. The
matcher finds a page's sequences, and the encoder writes each one the moment it is found, as one loop
per level. In the kernel's copy both headers are in this file, in that order, right before
`compress_block()`.

A reading order that follows the call:

1. [`seqlz_compress()`](#b98), the kernel's interface, which zram calls, and
   [`seqlz_compress_block()`](#b75), which it calls: levels 1 and 2 go to
   [`compress_block()`](#b16), level 3 to [`compress_own()`](#b34).
2. The encoder, in [`seqlz_encoder.h`](seqlz_encoder.h.html), and the matcher, in
   [`seqlz_match.h`](seqlz_match.h.html).

## The two halves
@@ 1-15
The kernel headers the file uses, including those of the two headers, which the kernel's copy has in
this file. Then the encoder and the matcher, both inlined into the functions below.

@@ 16-33
Levels 1 and 2: the greedy matcher and the encoder, the whole compression loop once inlined.

The check of `dst_cap` is here and in `compress_own()`, not once in `seqlz_compress_block()`: there it took
1% more instructions at levels 1 and 2, on 2000 pages of a desktop dump with gcc 16.2 on x86-64, 23rd
October 2026 (commit `75756b7`).

@@ 34-74
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

@@ 75-97
What zram calls. `dst_cap` is cut to `SEQLZ_COMPRESS_BOUND`, since the bitstream starts at the end of
`dst`: with a larger `dst` it would land far from the page, in memory nothing else of it touches.

Level 1 stops after `compress_block()`. Level 2 codes the literals with one of 8 fixed tables, using
the matcher's 8 KiB hash table as the copy of the raw literals, so it needs no memory of its own.
Level 3 hands the delta to `seqlz_code_own()`.
@more {#ch-encoder--entry} Three levels on two pages, and a `dst` larger than two pages
`seqlz_code_literals()` codes the literals with one of the 8 fixed literal tables if that saves
enough, and returns the new length or `len` unchanged; `seqlz_code_own()` does the same at level 3,
also with a table of the page's own. Both read the literals from a copy while they write the coded
ones over the raw ones in `dst` ([in place](#ch-literals-enc--in-place)). Level 2's copy is
`st->literals`, the matcher's 8 KiB table as bytes through the union in `struct seqlz_state`
([what lies where](#ch-header--state)); `seqlz_code_own()` also uses
[`own_work(st)`](seqlz_compress_literals.c.html#b987), the upper half of the same 8 KiB.

The text page of [chapter 4](#ch-format--own) shows what the levels do to one page: {{words.len}}
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
interface's `seqlz_compress()` [below](#b98) is its one caller and tests it.
@end

## The kernel's interface

@@ 98-118
@anchor ch-zram-glue--api
The interface's `seqlz_compress()`, which zram's backend calls, declared in
[`linux/seqlz.h`](linux-seqlz.h.html). The compressor's 0 becomes `-E2BIG`, "output too small", and a
level outside 1 to 3 is `-EINVAL`. The `static_assert`s tie the interface's numbers to the codec's:
the work memory is the matcher's state, and the interface's levels are the codec's, so
`seqlz_compress()` passes `level` on unchanged. The last one ties the 32 bytes that `seqlz_compress()`'s
kernel-doc names ([`linux/seqlz.h`](linux-seqlz.h.html#b43)) to `ENC_ROOM`, so the documentation
cannot drift from the encoder.

::: kernel EXPORT_SYMBOL_GPL
Modules are linked at load time against a table of exported symbols, and a function is in it only if
its file says `EXPORT_SYMBOL()` or `EXPORT_SYMBOL_GPL()`. The `_GPL` variant serves only modules whose
`MODULE_LICENSE()` is GPL-compatible. zram's backend can be in a zram module, so it needs the export.
Outside the kernel, [`linux/export.h`](book:#ch-linux) makes it a declaration that does nothing.
:::
