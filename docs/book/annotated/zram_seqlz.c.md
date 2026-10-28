# src/zram_seqlz.c

seqlz as zram calls a codec, for the project's benchmark harness: three codecs, `seqlz-1` to
`seqlz-3`, one per level of `seqlz_compress()`, all read by the same decoder. The harness has a copy
of zram's interface, `struct bench_codec`, in `bench/kernel_codecs/zram_codec.h`
([the harness's picture](book:#ch-zram-glue--harness)). The kernel does not get this file; its
backend is written by `port.py` ([the kernel's copy](book:#ch-zram-glue--port)).

zram's calls in short ([what zram calls](book:#ch-zram--zcomp)): once per device `setup_params()`;
once per CPU `create_ctx()`, which allocates the work memory; then per page `compress()` or
`decompress()`. A series of October 2026, not merged yet, splits the context in two, `create_cctx()`
for writing and `create_dctx()` for reading; the harness has that form. A reading order:

1. [`compress()`](#b40), the one compress function, and [`decompress()`](#b114).
2. The work memory: [the hash table](#b29) for writing, [the scratch](#b89) for reading.
3. The three [codec objects](#b79) that tie the functions together.

## Setting up

@@ 1-9
zram can give a device a dictionary, e.g. for `zstd`. seqlz's tables are fixed and part of the
format, so there is nothing a dictionary could change, and this file ignores it; the kernel's backend
refuses one ([one format](book:#ch-zram-glue--head)).

@@ 10-17
`setup_params()`, once per device. seqlz is compiled for one page size, so a 4 KiB build given a
corpus of 16 KiB pages fails here instead of reading past every page. No tables are built: they are
`const` data, generated when seqlz is built, shared by every device and CPU.

@@ 18-28
Nothing per device to release. `destroy()` takes only the stream, so it serves to free both kinds of
context.

## Work memory and compress

@@ 29-39
The compression context, one per CPU: `struct seqlz_state`, the matcher's hash table of 8192 bytes,
for all three levels. `bench_zalloc()` stands for the kernel's `kzalloc()` and counts what the
allocator would hand out, so the harness can report memory per CPU
([work memory](book:#ch-zram--work-memory)). The zeroing is not needed, the matcher clears its table
for every page.

@@ 40-54
`*dst_len` goes in as the size of `dst` and comes out as the compressed length: zram's convention.
zram gives a compressor 2 pages of output, and a page always fits into 2 pages, so `seqlz_compress()`
returns 0, "did not fit", only for a smaller buffer, e.g. the one page zswap gives.

The level is a parameter that zram's `compress()` does not have, so each codec gets a wrapper of one
line.

@@ 55-61
The wrapper of `seqlz-1`: raw literals, no literal table.

@@ 62-78
`seqlz-1` has no decompression context, so `s` here is the compression context, and the decoder does
not use it: pages with raw literals need no work memory. `NULL` as the scratch makes a page with coded
literals invalid, which `seqlz-1` never writes.

`bench_prefetch_page()` asks the CPU to start loading the compressed bytes and every line of the
output page before the decoder needs them ([prefetch](book:#ch-cpu--prefetch)). The kernel's backend
prefetches neither, so the harness's decode times include prefetches the kernel does not do; the
kernel VM's prefetch measurements used an experimental zram patch. The `*dst_len` check comes first, so a too small `dst` is never written.

@@ 79-88
The fields not named are null. No `create_dctx` tells the harness, as it tells zram after the split,
that this codec decompresses without a context of its own. Not `static`: `bench/bench_main.cpp`
names it.

## Levels 2 and 3

@@ 89-99
The scratch that coded literals are decoded into before the sequences are copied
([`seqlz_decode_literals()`](book:#ch-literals-dec)): a page plus 16 bytes, 4112 on 4 KiB pages, because the
decoder's 16-byte copies read up to 15 bytes past the last literal. With the hash table that is
12 304 bytes per CPU, which `kmalloc()` hands out as 16 384, below `lz4`'s 20 480
([memory](book:#ch-zram-glue--fast-lit)).

@@ 100-113
Level 3 needs no more work memory than level 2: its own literal table is worked out in the hash
table's memory, and decoded into the output page and the scratch.

@@ 114-125
The decompress of `seqlz-2` and `seqlz-3`, with the scratch as `s->context`. One function for both:
the page itself says, in byte 2, which table it uses and whether its literals are deltas. Pages with
raw literals leave the scratch alone, so a page of any level decodes here.

@@ 126-148
The same codec twice with a different level. Both have `create_dctx`, so zram gives them a second
context, the scratch, for reading.
