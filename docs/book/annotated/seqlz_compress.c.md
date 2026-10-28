# src/seqlz_compress.c

The compressor: the matcher of `seqlz_match.h` finds a page's sequences, and the encoder in this file
writes each one the moment it is found, as one loop. The literals go out as they are, and per
sequence the token's code, the offset bits and the length values go into a bitstream
([the format](book:#ch-format)). Levels 2 and 3 then code the literals, in
`seqlz_compress_literals.c`.

The problem the file solves: the bitstream comes after all literals, but how many literals there are
is known only after the last sequence. So the encoder writes from both ends of `dst`, the literals from
the front and the bitstream backwards from the back, and turns the bitstream around at the end.

A reading order that follows the call:

1. [`seqlz_compress()`](#b468), what zram calls: level 1 and 2 go to [`compress_block()`](#b409),
   level 3 to [`compress_own()`](#b427).
2. The layout: [the comment on both ends](#b43) and [why two pages always fit](#b83).
3. The matcher calls [`encode_emit()`](#b253) for each sequence, which is
   [`encode_seq()`](#b178): the room check, the literals, then the bits through
   [`enc_put()`](#b111) and [`enc_flush()`](#b136).
4. [`encoder_finish()`](#b278) turns the bitstream around behind the literals.

## The matcher's receiver

@@ 1-8
The two halves of a compressor in one file, and the matcher as a header, so that it is inlined into
the encoder's functions.

@@ 9-28
The simplest receiver of the matcher's sequences: it stores the three numbers into an array and
ignores the literals. Inlined into the matcher's loop like the encoder's receiver.

@@ 29-42
`seqlz_find()` is not for zram: the table trainer counts the symbols of exactly these sequences, and a
fuzzer checks that `seqlz_encode()` of them gives the same bytes as `seqlz_compress()`. The kernel copy
does not have it.

Two calls that differ only in `true` and `false` on purpose: with a constant in each, the compiler
inlines two copies of the matcher, and the one for levels 1 and 2 has no trace of level 3's lazy
matching.

## Both ends of dst

@@ 43-82
The layout, and the proof that it fits. The literals grow up from the front of `dst`, the bitstream
grows down from its end, and before each sequence the encoder checks that its literals and `ENC_ROOM`
bytes more are free between them.

Why `ENC_ROOM` is 32: a sequence's copies write up to 16 bytes past its literals, and its at most two
flushes write at most 15 bytes below where the bitstream was. The third paragraph is why zram's two
pages always fit: a sequence with a match covers at least 4 bytes and takes at most 31 bits for its
token and offset.

One pass and any `dst` size: two passes, matcher into an array then encoder, were 6% slower
([one pass](book:#ch-encoder--layout)).
@anim ends The buffer dst during a real page, from both ends, then turned around

@@ 83-96
The comment above, checked by the compiler. A change of a table or a constant that breaks the
argument now stops the build; until 23rd October 2026 it was only in the comment, which had been wrong
once (it counted 40 bytes instead of 32).

`ENC_BITSTREAM_MAX` is 31 bits for every 4 bytes of the block, and the u16, a block of literals, that
bitstream and `ENC_ROOM` fit `SEQLZ_COMPRESS_BOUND`, two pages.

@@ 97-110
`acc` holds the bits not yet written, `cnt` of them count. `p` is where the bitstream ends at the
bottom, `lit` where the next literal goes. `last` is the repeat offset, starting at 1 as in the
matcher and the decoder.

The matcher leaves the encoder no registers, so this struct lives on the stack. `rec` and `pos` are
level 3's, for the records before the literals.

## Bits

@@ 111-118
Appends `n` bits at the bottom of the accumulator. The `& 63U` changes no value; it lets the compiler
drop the check that `CONFIG_UBSAN_SHIFT` would add before a shift it cannot prove in range.

@@ 119-135
A code from an `enc[]` entry (`code | length << 16`) and its extra bits in one put, the code in the
higher bits, since the decoder reads the most significant bit first. `& 31U` is for UBSan again.

@@ 136-149
Whole bytes of the accumulator go below `p`. The shift puts the `cnt` valid bits on top of the word,
and a little-endian store writes the top byte last, at `p - 1`, so read from the end of `dst`
backwards the bits come in the order the decoder wants.

`p` moves down by the whole bytes; the at most 7 bits left over stay in `acc` and are written again,
with more bits, by the next flush. For `cnt` 0 the shift would be by 64, which the `& 63U` turns into
a shift by 0: the old word is written once more, and the next flush overwrites it.
@anim accum The accumulator, put by put and flush by flush, for a real page

@@ 150-160
A length value as its symbol's code plus extra bits. The symbol is masked to the 32 entries of `enc[]`
for UBSan; `v` goes in whole, `enc_put_code()` keeps only its extra bits.

@@ 161-177
A token's code. An entry of 0 means "no code of its own", since every code has a length of at least 1
in the top bits. Then the token is escaped: the escape's code, then the token's number in 12 bits,
together one code ([escape](book:#ch-format--token)).

## encode_seq(), one sequence

@@ 178-200
The room check before anything is written. A failed check sets `p` to `lit`, and that sticks: every
later sequence fails it too, and `encoder_finish()` sees `p == lit` and returns 0. The matcher runs on
to the end of the page anyway, which costs nothing with zram's two pages, where it never happens.

`rec` is a constant at every call, so the copy of the loop for levels 1 and 2 has none of level 3's
record code.

@@ 201-208
Level 3 only: a record of 6 bytes before the literals, with where the sequence starts in the page, how
many literals it has, and the offset before it. Level 3's deltas need those, and they are not in the
literals; `seqlz_delta_literals()` reads the records once the page is matched
([records](book:#ch-encoder--records)).

@@ 209-229
The literals: one 16-byte copy without a loop, since most sequences have few literals, then more
16-byte copies, and bytes only near the end of the input. The bytes past `ll` land in `ENC_ROOM`, and
the next sequence's literals overwrite them, the wild copy again.

The last sequence (`ml == 0`) has only its literals and no bits.

@@ 230-252
Token and offset in one put: the offset bits right after the token's code. `0U - (cls != 0)` is all
ones for classes 1 to 5 and 0 for class 0 (the repeat offset), which sends no bits.

An `ll` value gets a flush of its own, and every sequence ends with one, so the accumulator never
overflows: at most 7 + 31 + 20 = 58 bits before a flush. `last` changes only after the record above
used the old one.

## Receivers, start and finish

@@ 253-267
The matcher calls a receiver with a `void *`. `rec` has to be a constant at the call, so there are two
receivers, one per constant: levels 1 and 2 get `encode_emit()`, level 3 `encode_emit_rec()`. With the
records in a test per sequence instead, levels 1 and 2 wrote pages about 6% slower on the Mi 9T's
Cortex-A76.

@@ 268-277
The compound literal zeroes every field it does not name: `acc`, `cnt`, `rec` and `pos` start at 0.
`p` starts at the end of `dst`, `lit` behind the 2 bytes of the u16, which is written last.

@@ 278-303
`p == lit` only after a failed room check. The bits left over are in memory already: the last flush
wrote them on top of the byte below `p`, with zeros below them, exactly the zero padding the format
wants. So `p` only moves down by one to count that byte. Then the u16, the number of raw literals.

@@ 304-328
Byte `k` of the bitstream is at `end - 1 - k`. A little-endian load and a big-endian store reverse 8
bytes at once, a load, a byte swap and a store on x86-64.

Only a page that nearly fills `dst` has the two places overlap; then the bitstream is reversed in place
and moved down with `memmove()`.

## encode_raw() and seqlz_encode()

@@ 329-361
The encoder for sequences from outside, for tests and tools. The sequences are checked by the rules a
decoder checks before a byte is written.

A match below 4 bytes is not only invalid but dangerous: `ml - 4` wraps, a huge length value follows,
and the room proof no longer holds. Before commit `256768e` 1025 such sequences wrote past the two
pages of `dst` ([why](book:#ch-encoder--raw)).

@@ 362-385
The same loop as the matcher's, over the given sequences; `src_end` is the end of the caller's
literals. At level 3 with the records, and once more without them if the page does not fit, as
`compress_own()` does.

@@ 386-408
The public entry for sequences from outside. The level is checked before anything is written. At
level 2 it codes the literals where the caller has them, at level 3 with the deltas and maybe a table
of the page's own. The kernel copy does not have this function.

## The compressor

@@ 409-426
Levels 1 and 2: the greedy matcher with `encode_emit`, the whole compression loop once inlined.

The check of `dst_cap` is here and in `compress_own()`, not once in `seqlz_compress()`: there it took
1% more instructions at levels 1 and 2, on 2000 pages of a desktop dump with gcc 16.2 on x86-64.

@@ 427-467
Level 3: the lazy matcher with records. A page that does not fit with its records is matched once
more without them, as `encoder_init()` clears `e.rec`; a loop and not two calls, so the inlined matcher
is in the function once. Then `seqlz_delta_literals()` picks the delta and moves the literals down over
the records.

`noinline` on purpose: inlined next to `compress_block()`, levels 1 and 2 wrote their pages slower.

@@ 468-489
What zram calls. `dst_cap` is cut to `SEQLZ_COMPRESS_BOUND`, since the bitstream starts at the end of
`dst`: with a larger `dst` it would land far from the page, in memory nothing else of it touches.

Level 1 stops after `compress_block()`. Level 2 codes the literals with one of 8 fixed tables, using
the matcher's 8 KiB hash table as the copy of the raw literals, so it needs no memory of its own.
Level 3 hands the delta to `seqlz_code_own()`.
