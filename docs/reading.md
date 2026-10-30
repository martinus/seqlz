# Reading and reviewing the code

The codec is about 3800 lines, and the decoder is the part that has to be safe for any input. So read
it in five passes, each one a piece that can be judged by itself, and the decoder before the
compressor. The book, [seqlz, line by line](https://martin.ankerl.com/seqlz/), explains every line
when the reason for one is not obvious; it describes the code of the commit it names, the code on
`main` may be a bit newer.

## Before the code

Read [format.md](format.md), about 30 minutes. The code implements that specification, so the rest is a
check against it. Keep it open next to the decoder.

## 1. The interface, about 220 lines

[`src/linux/seqlz.h`](../src/linux/seqlz.h), and `seqlz_compress()` and `seqlz_decompress()` at the ends of
[`seqlz_compress.c`](../src/seqlz_compress.c) and [`seqlz_decompress.c`](../src/seqlz_decompress.c): what a kernel
reviewer sees first. Check the contract: the block size, `SEQLZ_COMPRESS_BOUND`, the work memory
`SEQLZ_MEM_COMPRESS` and `SEQLZ_MEM_DECOMPRESS`, the return values, and that a `NULL` scratch is only valid for
blocks without coded literals.

## 2. The format as constants, about 480 lines

[`src/seqlz_format.h`](../src/seqlz_format.h), then [`src/seqlz_tables.h`](../src/seqlz_tables.h). Check every
constant against format.md, and that the
`static_assert`s really tie the numbers together: the bit widths, the 31 bits that a token and its
offset take at most, the sizes of the tables.

## 3. The decoder, about 930 lines

[`src/seqlz_decompress.c`](../src/seqlz_decompress.c), then
[`src/seqlz_decompress_literals.c`](../src/seqlz_decompress_literals.c). The one question for every
line: can any input make it read or write outside its buffers? The places to look at:

- [`decode_block()`](../src/seqlz_decompress.c) and its fast path, with its three rooms `FAST_BLOCK_ROOM`, `FAST_LIT_ROOM` and
  `FAST_IN_ROOM`;
- the bit reader past the end of the input, [`seqlz_br_refill()`](../src/seqlz_decompress.c): its `count` goes negative there, and is
  checked once at the end;
- [`copy_match()`](../src/seqlz_decompress.c) with offsets that overlap the bytes it writes;
- [`read_own_table()`](../src/seqlz_decompress_literals.c), which parses untrusted bytes into the scratch;
- the sizes of the 8 literal streams and how each must end,
  [`lit_stream_ends()`](../src/seqlz_decompress_literals.c).

The KUnit tests in [`tools/kernel-port/seqlz_kunit.c`](../tools/kernel-port/seqlz_kunit.c) show which rules are checked:
`seqlz_test_damaged()` flips bits and cuts blocks, `seqlz_test_one_sequence()` and
`seqlz_test_coded_literals()` build blocks by hand, one rule of the format at a time. The fuzz targets
in [`fuzz/`](../fuzz) reach every decoder path.

## 4. The compressor, about 1520 lines

[`src/seqlz_encoder.h`](../src/seqlz_encoder.h), the encoder, [`src/seqlz_match.h`](../src/seqlz_match.h), the
matcher, [`src/seqlz_compress.c`](../src/seqlz_compress.c), which has both in the kernel's copy, then
[`src/seqlz_compress_literals.c`](../src/seqlz_compress_literals.c): the coded literals, a block's own literal table and the deltas. A bug
here costs bytes or time, not memory safety. The one thing to check hard is that it never writes past
`dst_cap`: `ENC_ROOM`, [`encoder_finish()`](../src/seqlz_encoder.h), and the fallback to raw literals.

## 5. Tables and glue, about 480 lines

[`src/seqlz_codes.c`](../src/seqlz_codes.c) and
[`tools/gen-tables/gen_seqlz_tables.c`](../tools/gen-tables/gen_seqlz_tables.c) build the tables when
seqlz is built:
check that lengths which are no complete prefix code, or that leave the encoder without a code, are
refused ([`seqlz_tables_build()`](../src/seqlz_codes.c), [`has_all_symbols()`](../src/seqlz_codes.c)).
Then [`tools/kernel-port/backend_seqlz.c`](../tools/kernel-port/backend_seqlz.c), zram's backend: the allocation of the contexts, the levels, the page size.

## What to skip

[`src/seqlz.h`](../src/seqlz.h) is the interface for the tests and tools, [`src/seqlz_harness.c`](../src/seqlz_harness.c) has
the two functions of it the kernel does not, `seqlz_find()` and `seqlz_encode()`,
[`src/zram_seqlz.c`](../src/zram_seqlz.c) and the other headers of [`src/linux/`](../src/linux) are the
harness's glue, `seqlz_lengths.h` and `seqlz_lengths_16k.h` are generated tables.

## How to read it

- **As the kernel sees it.** `python3 tools/kernel-port/port.py <copy of a kernel tree>`
  ([`port.py`](../tools/kernel-port/port.py)) writes the kernel's copy, and `lib/seqlz/` there is what a reviewer reads: the kernel's headers, no harness code,
  the comments as they would be sent. [`tools/kernel-port/check.sh`](../tools/kernel-port/check.sh) builds and checks it the same way.
- **Notes, not edits.** For each finding the file and line, and whether it is a bug, a question or a
  matter of style. A list like that is easy to turn into fixes, one commit each.
- **Why it is like that.** Before calling a line odd: the book has its reason, and
  [explored-designs.md](explored-designs.md) has the measurement behind most choices, also the ones
  that were tried and not kept.
