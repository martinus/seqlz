# src/seqlz_compress.c

The compressor's entry points. The matcher of `seqlz_match.h` finds a page's sequences, and the encoder
of `seqlz_encoder.h` writes each one the moment it is found; both are inlined here, so the two become
one loop. At level 1 that loop is all there is: the page comes out with its literals as they are, raw.
In the kernel's copy both headers are in this file, the encoder first, right before `compress_block()`.

A reading order that follows the call:

1. [`seqlz_compress()`](#b109), the kernel's interface, which zram calls, and
   [`seqlz_compress_block()`](#b77), which it calls and which calls [`compress_block()`](#b16).
2. The encoder, in [`seqlz_encoder.h`](seqlz_encoder.h.html), and the matcher, in
   [`seqlz_match.h`](seqlz_match.h.html).

@intro 2
Level 2 adds one step after the loop: [`seqlz_code_literals()`](#b101) codes the page's raw literals
with one of 8 fixed literal tables, where that saves enough. The page itself comes from
`compress_block()`, as at level 1.

@intro 3
Level 3 has a loop of its own, [`compress_own()`](#b35): the lazy matcher, and an encoder that writes a
record before each sequence's literals, which the deltas need. Then `seqlz_delta_literals()` picks a
delta for the literals, or none, and [`seqlz_code_own()`](#b94) codes them, with a fixed table or a table of the
page's own.

## The two halves
@@ 1-15
The kernel headers the file uses, also those the two headers need, as the kernel's copy has them in
this file. Then the encoder and the matcher, both inlined into the functions below.

@@ 16-34
`compress_block()` writes a page with raw literals. `encoder_init()` sets up `dst`, the matcher finds
the sequences and hands each one to the encoder, and `encoder_finish()` returns the page's length, or 0
if it did not fit. With both inlined, this is the whole compression loop. The `false` tells the matcher
not to be lazy, which only level 3 is ([`compress_own()`](#b35)). Level 2 uses this function too.

A `dst` below {{ch10.dst_min}} bytes, the u16 and `ENC_ROOM`, gives 0 at once: no page fits into less,
and the encoder's room check needs that much to work ([why](seqlz_encoder.h.html#b176)).
@more The check of `dst_cap` here, not in the caller, measured
The check is here and in `compress_own()`, not once in `seqlz_compress_block()`: there it took 1% more
instructions at levels 1 and 2, on 2000 pages of a desktop dump with gcc 16.2 on x86-64, 23rd October
2026 (commit `75756b7`).
@end

@@ 35-76
Level 3's version of `compress_block()`. The matcher is lazy (`true`), and with `e.rec` the encoder
writes a record before each sequence's literals, which the deltas need: where the sequence starts,
how many literals it has, and the offset before it ([the records](book:#ch-encoder--records)).

The records take room. A page that does not fit with them ends with `e.p == e.lit`, the encoder's sign
for "did not fit", and is matched once more without them: `encoder_init()` clears `e.rec`, so the
second round is the last. A loop and not two calls, so that the inlined matcher is in the function once.

With records, `seqlz_delta_literals()` picks the delta, moves the literals down over the records and
writes their deltas to `st->literals`. After it `encoder_finish()` finds `dst` as it would without
records. `*delta` stays 0 for a page without records.

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
The codec's entry point: `src` is one page, `dst` has room for `dst_cap` bytes, and the result is the
length of the compressed page, or 0 if it does not fit or the level is not one. zram reaches it through
the interface's [`seqlz_compress()`](#b109).

@@ 82-85
Level 3's delta, from `compress_own()` to `seqlz_code_own()`.

@@ 86-93
`dst_cap` is cut to `SEQLZ_COMPRESS_BOUND`, two pages, which every page fits into. The bitstream starts
at the end of `dst`, so with a larger `dst` it would land far from the page, in memory nothing else of
it touches.
@more A `dst` larger than two pages, and the kernel's copy
Every page fits into `SEQLZ_COMPRESS_BOUND` ([two pages](#ch-encoder--two-pages)). Until 24th October
2026 a larger `dst` was not cut: with a `dst` of 64 KiB the bitstream went 56 KiB past the page, in
memory the page has nothing else in (commit `8fed16a`). zram passes exactly two pages, so its pages did
not change.

In the kernel's copy `seqlz_compress_block()` is `static` and has no test of the level: the
interface's `seqlz_compress()` [below](#b109) is its one caller and tests it.
@end

@@ 94-99
Level 3: [`compress_own()`](#b35) writes the page, then `seqlz_code_own()` codes its literals, with the
delta it picked, and with a fixed table or a table of the page's own.
@more Level 3's work memory, and what it does to two pages
`seqlz_code_own()` uses `st->literals`, the matcher's 8 KiB table as bytes
([what lies where](#ch-header--state)): it mostly reads the literals from a copy there while it writes
the coded ones over the raw ones in `dst` ([in place](#ch-literals-enc--in-place)). It also uses
[`own_work(st)`](seqlz_compress_literals.c.html#b921), the upper half of the same 8 KiB.

The made-up page of words of [level 2's panel](#ch-encoder--entry) is {{words.l3.len}} bytes at level
3, without deltas, against {{words.coded.len}} at level 2; the page of pointers
{{delta.ptr.l3.len}} bytes, with them, against {{delta.ptr.l2.len}} at level 2.
@assert words.l3.delta == 0 and pointers.l3.delta == 1
@end

@@ 100-100
`compress_block()` writes the page with raw literals, at levels 1 and 2.

@@ 101-104
Level 2 codes the literals of the page `compress_block()` wrote. `seqlz_code_literals()` picks one of
the 8 fixed literal tables and codes the literals with it if that saves enough; else it returns `len`
unchanged. It uses the matcher's 8 KiB hash table for the coded streams, so it needs no memory of its
own. A page that did not fit, and level 1, return `len` as it is.
@more {#ch-encoder--entry} Two levels on two pages
`seqlz_code_literals()` uses `st->literals`, the matcher's 8 KiB table as bytes through the union in
`struct seqlz_state` ([what lies where](#ch-header--state)). It writes the coded streams into slots
there and copies them over the raw literals once all are read
([the slots](seqlz_compress_literals.c.html#b309)). The page is written with raw literals first, so it
has to fit that way, also where its coded form would be smaller.

A made-up page of words, of the letters `etaoinshrdlu` and spaces, is {{words.len}} bytes at level 1
and {{words.coded.len}} at level 2; [the page of pointers](#ch-matcher--back) {{pointers.len}} bytes at
level 1 and {{delta.ptr.l2.len}} at level 2.
@assert delta.ptr.l2.len < pointers.len
@end

@@ 105-108
A build with level 1 only returns the page as `compress_block()` wrote it, with raw literals.

## The kernel's interface

@@ 109-128
@anchor ch-zram-glue--api
The interface's `seqlz_compress()`, which zram's backend calls, declared in
[`linux/seqlz.h`](linux-seqlz.h.html). The compressor's 0 becomes `-E2BIG`, "output too small", and a
level outside 1 to `SEQLZ_LEVEL_MAX` is `-EINVAL`.

The `static_assert`s tie the interface's numbers to the codec's. The work memory is the matcher's
state. The interface's levels start at 1 as the codec's do, so `seqlz_compress()` passes `level` on
unchanged; the levels above are checked where a build has them
([`seqlz_internal.h`](seqlz_internal.h.html#b103)). The 32 bytes that `seqlz_compress()`'s kernel-doc
names ([`linux/seqlz.h`](linux-seqlz.h.html#b59)) are `ENC_ROOM`, so the documentation cannot drift
from the encoder.

::: kernel EXPORT_SYMBOL_GPL
Modules are linked at load time against a table of exported symbols, and a function is in it only if
its file says `EXPORT_SYMBOL()` or `EXPORT_SYMBOL_GPL()`. The `_GPL` variant serves only modules whose
`MODULE_LICENSE()` is GPL-compatible. zram's backend can be in a zram module, so it needs the export.
Outside the kernel, [`linux/export.h`](book:#ch-linux) makes it a declaration that does nothing.
:::
