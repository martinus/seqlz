# src/linux/seqlz.h

The kernel's interface of seqlz, `include/linux/seqlz.h` in the kernel's copy: two functions, the
sizes of their work memory, and the levels. zram's backend calls nothing else, and the kernel exports
nothing else. It has the shape of `include/linux/lz4.h`: work memory sizes as constants, functions
that take the work memory as a `void *`, and no tables. The tests and tools have more, in
[`seqlz.h`](seqlz.h.html), which includes this one.

## Blocks, bounds and work memory

@@ 1-12
@anchor ch-zram-glue--interface
A compressed block is only for the kernel that wrote it: the tables are part of the format, and the
format may change from one kernel to the next ([no version](book:#ch-format--version)). zram keeps
its pages in memory, never on disk, so nothing ever reads a block written by another kernel. The
comment names `Documentation/staging/seqlz.rst`, which `port.py` writes from `docs/format.md`.

@@ 13-22
The interface speaks of blocks of `SEQLZ_BLOCK_SIZE` bytes, not of `PAGE_SIZE`: one memory page with
4 KiB pages, a quarter of one with 16 KiB pages. The kernel's copy has `#define SEQLZ_BLOCK_SIZE 4096U`
here: it compresses blocks of 4 KiB whatever the page size, and `port.py` takes out the 16 KiB of
`SEQLZ_PAGE_BITS`, which only the userspace build has. `SEQLZ_COMPRESS_BOUND` is the two pages zram
hands the compressor ([why two pages always fit](book:#ch-encoder--two-pages)).

@@ 23-31
The work memory, as constants so that a caller can allocate it once per CPU. For compression the
matcher's state, the hash table, which also holds the raw literals at level 2 and the work of a block's
own table at level 3 ([what lies where](book:#ch-header--state)); 8192 bytes, 16 384 for 16 KiB blocks
in userspace, which the kernel's copy cuts. For decompression the buffer of decoded literals, 16 bytes
more than a block. `seqlz_compress.c` and `seqlz_decompress.c` check both against the codec's own
sizes with a `static_assert()`.

@@ 32-42
Three levels, and the one zram uses when nothing is set: level 2, the fixed literal tables
([the levels](book:#ch-internal)).

## The two functions

@@ 43-64
@anchor ch-zram-glue--kerneldoc
The comment is **kernel-doc**, the kernel's format for documenting functions, which
`scripts/kernel-doc` parses and checks.

- `Context: Any context` promises that the function may run where sleeping is not allowed. zram calls
  it under a mutex and does not need it; it holds because the codec never sleeps or allocates.
- The `Return:` has a subtlety of [the room check](book:#ch-encoder--room-check): the encoder stops as
  soon as fewer than {{ch15.enc_room}} bytes (`ENC_ROOM`) plus the next literals are free, so a block
  can be refused although its size with coded literals would have fit. With zram's 2 pages it never
  happens.

[The function](seqlz_compress.c.html#b98) is at the end of `seqlz_compress.c`.

@@ 65-85
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
