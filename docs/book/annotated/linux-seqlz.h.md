# src/linux/seqlz.h

The kernel's interface of seqlz, `include/linux/seqlz.h` in the kernel's copy: two functions, the
sizes of their work memory, and the levels. zram's backend calls nothing else, and the kernel exports
nothing else. It has the shape of `include/linux/lz4.h`: work memory sizes as constants, functions
that take the work memory as a `void *`, and no tables. The tests and tools have more, in
[`seqlz.h`](seqlz.h.html), which gets this one through `seqlz_internal.h`.

The comments name all three levels, because the interface is the same for all of them. At level 1 the
compressor needs its work memory, the decompressor none.
@intro 2
Level 2's one line here: from level 2 on, the default level is 2. The rest of the file is level 1's
and holds as it is. Among it is `SEQLZ_MEM_DECOMPRESS`, the decompressor's work memory, which level 2
is the first level to need ([the block](#b18)).

## Blocks, bounds and work memory

@@ 1-13
@anchor ch-zram-glue--interface
A compressed block is only for the kernel that wrote it: the tables are part of the format, and the
format may change from one kernel to the next ([no version](book:#ch-format--version)). zram keeps
its pages in memory, never on disk, so nothing ever reads a block written by another kernel. The
comment names `Documentation/staging/seqlz.rst`, which `port.py` writes from `docs/format.md`.

@@ 14-17
The interface speaks of blocks of `SEQLZ_BLOCK_SIZE` bytes, not of `PAGE_SIZE`: a block is one memory
page with 4 KiB pages, a quarter of one with 16 KiB pages. `SEQLZ_COMPRESS_BOUND` is the two pages zram
hands the compressor ([why two pages always fit](book:#ch-encoder--two-pages)).
@more The 16 KiB blocks of the userspace build, until 1st November 2026
seqlz compresses blocks of 4 KiB whatever the page size. Until 1st November 2026 the userspace build
also had blocks of 16 KiB, with tables of their own, which the kernel's copy never had.
@end

@@ 18-30
The work memory, as constants, so that a caller can allocate it once per CPU. For compression it is
the matcher's hash table, 8192 bytes ([what lies where](book:#ch-header--state)); levels 2 and 3 reuse
that memory once the matcher is done. For decompression it is a buffer of a block and 16 bytes, which
only levels 2 and 3 need, for their coded literals ([Part III](book:#part-level-2)).
`seqlz_compress.c` checks the first against `sizeof(struct seqlz_state)` with a `static_assert()`,
level 2's part of `seqlz_internal.h` the second against `SEQLZ_SCRATCH`.

@@ 31-52
`SEQLZ_LEVELS` is the number of levels a build has: 3 unless the build says fewer, and anything but 1
to 3 fails the build. A build with fewer levels has no code and no tables of the levels above, and its
decoder takes their blocks for invalid. The kernel's patch series adds one level per patch, and
`port.py` writes the copy without the `#if` lines ([the levels](book:#ch-header--compressor)). Then the
three levels themselves, from the fastest to the smallest blocks.

@@ 53-54
With level 2 built, the default is level 2: zram uses it for a device that sets no level
([the backend's setup](book:#ch-zram-glue--backend-params)).

@@ 55-58
A build of level 1 alone has only level 1, which is then the default. The top level is the number of
levels built.

## The two functions

@@ 59-80
@anchor ch-zram-glue--kerneldoc
The comment is **kernel-doc**, the kernel's format for documenting functions, which
`scripts/kernel-doc` parses and checks. The work memory is `struct seqlz_state`. It needs 8-byte
alignment because level 3 keeps `u64`s in it; what `kmalloc()` returns has at least that.

- `Context: Any context` promises that the function may run where sleeping is not allowed. zram does
  not need that: it calls the codec under a mutex. It holds because the codec never sleeps or
  allocates.
- `-E2BIG` can come early: the encoder stops as soon as fewer than {{ch15.enc_room}} bytes
  (`ENC_ROOM`) plus the next literals are free ([the room check](book:#ch-encoder--room-check)). With
  zram's 2 pages it never happens.

[The function](seqlz_compress.c.html#b109) is at the end of `seqlz_compress.c`.

@@ 81-101
@anchor ch-header--encode-decode
"Safe for any input" is the codec's most important promise, constraint C6 of the plan and its one
hard limit ([chapter 1](book:#ch-zram--safety)). zram stores no checksum, so a page damaged in memory
reaches the decoder as it is. A decoder that wrote outside its buffers would turn a damaged page into a
damaged kernel ([how the decoder keeps it](book:#ch-decode--safety)). Its time is bounded too, by the
block size, not by `src_len`.

`wrkmem` may be `NULL` for a device at level 1: a level 1 page has raw literals, which need no work
memory, so the device saves that buffer per CPU ([the backend](book:#ch-zram-glue--backend)). There is
no partial result: either `dst` holds the block, or the call returns `-EINVAL`.

[The function](seqlz_decompress.c.html) is in `seqlz_decompress.c`, the decoder itself. Before
commit `aaa381e` the kernel's copy exported all of `seqlz.h`, 7 functions and the code lengths.
