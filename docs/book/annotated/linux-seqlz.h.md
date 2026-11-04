# src/linux/seqlz.h

The kernel's interface of seqlz, `include/linux/seqlz.h` in the kernel's copy: two functions, the
sizes of their work memory, and the levels. zram's backend calls nothing else, and the kernel exports
nothing else. It has the shape of `include/linux/lz4.h`: work memory sizes as constants, functions
that take the work memory as a `void *`, and no tables. The tests and tools have more, in
[`seqlz.h`](seqlz.h.html), which gets this one through `seqlz_internal.h`.

## Blocks, bounds and work memory

@@ 1-13
@anchor ch-zram-glue--interface
A compressed block is only for the kernel that wrote it: the tables are part of the format, and the
format may change from one kernel to the next ([no version](book:#ch-format--version)). zram keeps
its pages in memory, never on disk, so nothing ever reads a block written by another kernel. The
comment names `Documentation/staging/seqlz.rst`, which `port.py` writes from `docs/format.md`.

@@ 14-17
The interface speaks of blocks of `SEQLZ_BLOCK_SIZE` bytes, not of `PAGE_SIZE`: one memory page with
4 KiB pages, a quarter of one with 16 KiB pages. seqlz compresses blocks of 4 KiB whatever the page
size; until 1st November 2026 the userspace build also had blocks of 16 KiB, with tables of their own,
which the kernel's copy never had. `SEQLZ_COMPRESS_BOUND` is the two pages zram hands the compressor
([why two pages always fit](book:#ch-encoder--two-pages)).

@@ 18-30
The work memory, as constants so that a caller can allocate it once per CPU. For compression the
matcher's state, the hash table, which also holds the raw literals at level 2 and the work of a block's
own table at level 3 ([what lies where](book:#ch-header--state)); 8192 bytes. For decompression the
buffer of decoded literals, 16 bytes more than a block, which holds the buckets of a page's own literal
table first, while the table is read ([own table](book:#ch-literals-dec--own)). `seqlz_compress.c` and
`seqlz_decompress.c` check both against the codec's own sizes with a `static_assert()`.

@@ 31-52
`SEQLZ_LEVELS`, the levels a build has: 3 unless the build says fewer, and 1 to 3, else the build
fails. A build with fewer levels has no code and no tables of the levels above, and its decoder takes
their blocks for invalid; the kernel series adds one level per patch, and `port.py` writes the copy
without the `#if` lines ([the levels](book:#ch-internal)). Then the three levels themselves, from the
fastest to the smallest blocks.

@@ 53-54
From level 2 on, zram's default is level 2, the fixed literal tables.

@@ 55-58
A build of level 1 alone has only level 1, which is then the default. The top level is the number of
levels built.

## The two functions

@@ 59-80
@anchor ch-zram-glue--kerneldoc
The comment is **kernel-doc**, the kernel's format for documenting functions, which
`scripts/kernel-doc` parses and checks. The work memory is `struct seqlz_state`, whose `work[]` is an
array of `u64`, so it needs 8-byte alignment; what `kmalloc()` returns has at least that.

- `Context: Any context` promises that the function may run where sleeping is not allowed. zram calls
  it under a mutex and does not need it; it holds because the codec never sleeps or allocates.
- The `Return:` has a subtlety of [the room check](book:#ch-encoder--room-check): the encoder stops as
  soon as fewer than {{ch15.enc_room}} bytes (`ENC_ROOM`) plus the next literals are free, so a block
  can be refused although its size with coded literals would have fit. With zram's 2 pages it never
  happens.

[The function](seqlz_compress.c.html#b109) is at the end of `seqlz_compress.c`.

@@ 81-101
@anchor ch-header--encode-decode
"Safe for any input" is the most important promise of the codec, constraint C6 of the plan, the one
hard limit ([chapter 1](book:#ch-zram--safety)). zram stores no checksum, so a page damaged in memory
reaches the decoder as it is. A decoder that wrote outside its buffers would turn a damaged page into a
damaged kernel ([how the decoder keeps it](book:#ch-decode--safety)), and its time is bounded by the
block size, not by `src_len`.

`wrkmem` may be `NULL` for a device at level 1, whose compressor never codes literals; that saves the
buffer per CPU ([the backend](book:#ch-zram-glue--backend)). There is no partial result: either `dst`
holds the block, or `-EINVAL`. The block does not say which tables wrote it.

[The function](seqlz_decompress.c.html) is in `seqlz_decompress.c`, the decoder itself. Before
commit `aaa381e` the kernel's copy exported all of `seqlz.h`, 7 functions and the code lengths.
