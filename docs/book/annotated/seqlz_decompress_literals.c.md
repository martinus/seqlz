# src/seqlz_decompress_literals.c

The decoder of a page's coded literals. They are Huffman coded in 8 streams, literal `i` in stream
`i % 8`, with one of 8 fixed literal tables or a table the page brings along
([the layout](book:#ch-format--coded)). `decode_block()` in `seqlz_decompress.c` calls
`seqlz_decode_literals()` once, before its loop over the sequences. It decodes all literals into the
scratch, and from then on the loop reads them as if the page had them raw.

Decoding a Huffman code is a chain: look up the next 10 bits, write the byte, shift by the code's
length, and only then are the next code's bits known. 8 streams are 8 chains with nothing between them,
which an out-of-order CPU works on at the same time ([chains](book:#ch-cpu--chains)).

A reading order that follows the call:

1. [`seqlz_decode_literals()`](#b306): the checks, the stream sizes, and the two loops over the streams.
2. [`read_lit_header()`](#b251): byte 2 and the delta's byte, then a fixed table or the page's own.
3. [`read_own_table()`](#b157), only for a page with its own table, and [`own_fill()`](#b108).
4. The stream reader the loops use: [`lit_refill()`](#b32), [`lit_decode()`](#b49), and at the end
   [`lit_stream_ends()`](#b90).

## The stream reader

@@ 1-7
Two kinds of tables, one decoder: a page's own table is turned into the same kind of decode table a
fixed one has, so everything after the header does not know the difference.

@@ 8-17
The trick of this file. A stream is a pointer `ip` and 64 bits, with a 1 below the valid bits. Each
code shifts the bits left, and the 1 moves up with them, so `__builtin_ctzll(bits)` is the number of
bits used since `ip`.

So a stream needs no counter. 8 streams with a counter are 24 values, more than the 16 registers of
x86-64 can hold next to the table and the output; with the sentinel the 8 `bits` stay in registers
and only a refill touches the pointers. zstd keeps its 4 Huffman streams the same way.

@@ 18-31
The 8 bytes at `ip`, with zeros after the end of the compressed page: an 8-byte load there could read
past the page, which may be the end of a mapping. Only the last refills of a page come here.

`noinline` keeps the byte loop out of the many inlined copies of `lit_refill()`, and `__cold` has the
compiler place it away from the loop.

@@ 32-48
A refill without a counter: the sentinel's position is the bits used, `ip` moves on by the whole
bytes of them, and `<< (used & 7U)` drops the bits of the new first byte that were used already. That
shift also puts the new sentinel, the `| 1U`, right below the valid bits.

At least 56 bits are valid after a refill, enough for 5 codes of at most 10 bits, so the decoder
refills once per 5 literals of a stream. `end` is the end of the whole page, not of the stream: a
refill may load bytes of the next stream, and the check at the end catches a stream that used them.

@@ 49-64
One literal: the top 10 bits index the table (1024 entries, every index valid), the entry has the
byte and the code's length.

`mask` is 63 or 0. With 63 the stream moves on by the code's length. With 0 the literal is decoded and
written but the stream stays where it is: "decode, but do not move on", without a branch. The last
step of [the rest](#b358) needs that.

## Sizes and ends

@@ 65-89
The 8 stream sizes, `width` bits each, packed behind the header. Each size is read with a 4-byte load
at its first byte, shifted and masked.

The sizes are copied into `h` first: for the last size, the 4-byte load can reach past the sizes and
past the end of a short page. `h`'s bytes behind them are zeros.

@@ 90-107
Each stream has to end exactly in its last byte, with zero bits after its last code. `left` is the
bits of the stream's size minus the bits used; it has to be 0 to 7, and those last bits 0.

This catches a stream that read into the next one (negative), a stream with a whole byte too many,
and set bits after the codes. zram stores no checksum, so this and the end of the sequences'
bitstream are what tell a damaged page from a valid one ([the ends](book:#ch-literals-dec--ends)).

## A page's own table

@@ 108-136
Writes the decode table from the buckets: for each code length, the bytes with that length in order
of their value, which is the order of their canonical codes. So writing length 1, then 2, and so on
puts each byte exactly at the entries its code indexes.

A code of `l` bits has `1024 >> l` entries, all the same. The multiply by `0x0001000100010001` copies
the 2-byte entry into 4 lanes, and one 8-byte store writes 4 entries. That works down to 8 bits, 4
entries.

@@ 137-150
Codes of 9 bits have 2 entries, one 4-byte store; codes of 10 bits one. A loop per length without a
branch per byte: one loop for all lengths with that branch read level 3 slower on the Mi 9T's
Cortex-A76.

@@ 151-156
`OWN_STEP_MAX`: one step takes at most a run's 8-bit code and 8 more bits.

The table needs 2 KiB, and the buckets 3 KiB on the way. The decode table goes into the output page,
which nothing writes before all literals are decoded; the buckets go into the scratch, which the
literals overwrite once the table is built. The `static_assert` checks that both fit, so a page's own
table costs no extra work memory.

@@ 157-193
The description of the table is a list of code lengths, one per byte value in order, with runs for
bytes that do not appear, in the fixed code `OWN` ([the format](book:#ch-format--own)).

The comment's third paragraph is the bound: the loop ends for any input, after at most 1024 + 257
steps. The last paragraph says why it is `noinline`: inlined, its locals made the stack frame of
`seqlz_decode_literals()` larger than the kernel allows with KASAN.
@anim owndesc The description of a page's own table, one symbol per step

@@ 194-208
The description is read through a 64-bit window `bits` whose top `avail` bits are the next ones,
refilled with 8 bytes when fewer than 16 are left.

`n` is the whole bytes that fit, and the mask clears what is below them. `avail + 8 * n` is at most
63, so no shift is by 64. Near the end of the input `lit_load_tail()` reads zeros, and `min(at, len)`
keeps the pointer inside the page.
@anim ownread The loop, one lookup per step, on two real pages

@@ 209-227
`quad[top]` says what the next 8 bits hold ([quad](book:#ch-internal--own)). Bit 31 means a run that
does not fit an entry: then `decode[top]` gives the run's symbol and code length, and its `nb - 1`
bits follow the code. Two shifts again, because `nb - 1` can be 0.

A run only moves `b` (the next byte value) on, not `space`, so a description of nothing but runs would
never fill the code space. That is why the run checks `b > 256` right here: a valid table names at
most 256 bytes. Until 23rd October 2026 that check came only after the loop, and such input kept the
loop going for as long as the input lasted.

@@ 228-242
The usual step: up to two lengths, and the runs before them, from one lookup. Byte `b` goes into the
bucket of its length.

A complete code may end after the first length, and the zero padding after it would read as more
lengths. `two` is whether room is left; without it, the second length is `OWN_QUAD_NONE`, which goes
into a row nobody reads, fills no code space, and does not move `b`. So one length or two take the same
instructions, without a branch. `& 255U` keeps a bucket index inside its row for any input.

@@ 243-250
All checks at the end, as nothing reads the table before: the code space exactly full, at most 256
bytes named, the bits within the input, and zero bits after the last symbol in its byte. Then
`own_fill()`, and the description's end is where the streams start.

## The header

@@ 251-265
Reads the literal header: byte 2 (the table, the width of the stream sizes, whether the literals are
deltas), the delta's byte, and the page's own table if it has one. Inlined into its one caller.

@@ 266-286
A page of 2 bytes has no byte 2; then the width stays at its minimum, and the length check rejects
the page before anything reads byte 2. The length check counts the delta's byte, so all header bytes
are in the page before they are read.

Codes 9 to 15 are reserved and rejected: a decoder that accepted them now could not tell old pages
from new ones later. `*delta - 1U >= SEQLZ_DELTA_LAST` checks both ends in one compare, 0 wraps to a
large number, so only 1 to 3 pass.

@@ 287-305
Code 8: the page's own table, built into the output page from its first even address, since the
entries are `u16` and a test decodes into a page at an odd address. The streams then start behind the
table's description.

Codes 0 to 7: a fixed table. The `& (SEQLZ_LIT_SETS - 1U)` changes none of them and keeps the index
inside the array for any value. The prefetch goes out before the sizes are read, so its cache misses
overlap with that work.

## seqlz_decode_literals()

@@ 306-325
The only function here that `decode_block()` calls. `noinline` on purpose: inlined into the loop over
the sequences, the 8 streams took its registers, and pages with raw literals read slower too.

The streams are arrays; after unrolling, the indices are constants and the compiler keeps each
element in a register like a plain variable.

@@ 326-342
`n_lit` was checked by the caller already. The check again here tells the compiler the bound, and the
loops below take fewer instructions: moving it out, together with one more change, took 7% more to
decode `seqlz-2` pages ([why](book:#ch-literals-dec--main)).

After the check against `src_len`, every stream ends inside the input. A stream starts with `bits = 1`,
so its first refill loads from its start without a shift.

@@ 343-357
The main loop: per round, 8 refills, then 5 literals from each of the 8 streams. Stream `i` in step
`j` holds literal `k + 8 * j + i`, so the literals land in page order, no shuffle. With constant
bounds the compiler unrolls it into 8 refills and 40 lookups, 8 independent chains.
@anim streams The 8 streams of a real page, a round at a time, against one stream

@@ 358-385
Fewer than 40 literals are left. Every stream does the same steps; in the last one `(0U - (r > i)) &
63U` is 63 for the `r` streams that still have a literal and 0 for the others. Those decode one too,
write it behind the literals into the scratch's 16 spare bytes, and do not move on.

So each stream ends right after its own codes, which `lit_stream_ends()` then checks exactly. A loop
over only the streams with a literal left would run 0 to 7 times, different on every page, and
mispredict.
