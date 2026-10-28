# src/seqlz_decompress.c

The decoder of one page. A compressed page is a 2-byte header (the number of literals and a flag for
coded literals), the literals, and a bitstream with one token per sequence: a sequence is "copy `nl`
literals, then copy `len` bytes from `off` bytes back" ([the layout](book:#ch-format--layout)). The
coded literals are decoded in another file, `seqlz_decompress_literals.c`
([chapter 13](book:#ch-literals-dec)).

C wants a helper before its caller, so the file reads bottom up. A reading order that follows the
call:

1. [`seqlz_decode()`](#b588), the only function zram calls: it picks one of two copies of the loop.
2. [`decode_block()`](#b392): the header, the literals, and the three limits of the fast path.
3. The loop: [escape](#b472), [offset](#b488), [fast path](#b500), [slow path](#b534), [end checks](#b568).
4. The helpers, as the loop reaches them: [`next_token_fast()`](#b360),
   [`copy_match_fast()`](#b303), [`copy_literals()`](#b31), [`copy_match()`](#b89), the
   [bit reader](#b148).

The step-through under [the loop](#b472) runs the real `decode_block()` on two pages, one statement
at a time.

## Copies

@@ 1-6
The whole file has one function that is not `static`, `seqlz_decode()` at the bottom. Everything else
is inlined into it.

@@ 7-30
`__builtin_memcpy()` of a constant size is one unaligned load and one store. It is the builtin because
the kernel's `memcpy()` can become a call with `CONFIG_FORTIFY_SOURCE`.

The two differ in the order of loads and stores. `copy8()` loads, then stores, so a second `copy8()`
may read the bytes the first just wrote: that is what a match with an offset of 8 to 15 needs.
`copy16()` loads both halves first, so it is only for literals, whose source never overlaps the page.

@@ 31-61
A wild copy: 16 bytes at a time, also past `nl`. The bytes past `nl` are garbage for a moment, the
match after the literals writes over them.

The first `copy16()` has no loop, because most sequences have fewer than 16 literals and a loop that
runs 0 or 1 times mispredicts. `lit_bound` is the end of what may be read, not the end of the literals:
the end of the input for raw literals, the end of the scratch for coded ones. Only within 15 bytes of
either end does it copy byte by byte.

@@ 62-67
For an offset of 1 to 7, how far on the same 8 bytes of a repeated pattern fit again: the largest
multiple of `off` up to 8. Offset 3 repeats `abc`, and `abcabcab` fits again 6 bytes on. Entry 0 is a
placeholder, offset 0 is rejected before.

@@ 68-88
The 8 bytes that a byte-by-byte copy with offset `off` would write, built in one register. Load 8 bytes
at `d - off`, keep the low `off` bytes, multiply by a constant with a 1 in every byte at a multiple of
`off`. The copies of the pattern do not overlap, so no carry crosses a byte: `abc` times
`0x0001000001000001` is `abcabcab`.

The load is little endian on every CPU because the multiply needs the pattern's first byte in the
lowest byte.
@anim overlap What a match with offset 1, 2 or 3 writes

@@ 89-124
The match copy of the slow path: matches with a length value (up to a page long) and matches near the
end of the page.

Below offset 8 the bytes overlap the bytes being written. So the pattern is built once and stored every
`step` bytes, stores only, no load waits for a store. `off & 7U` changes nothing here, `off` is below 8
already; it is left over from older code. With fewer than 8 bytes left in the page everything goes to
the byte loop at the bottom.

The first decoder copied a match up to the end of the page byte by byte, and its slowest 1% of pages
took more than 10 times as long as `lz4`'s. That is the last sentence of the comment.

@@ 125-147
From offset 8 on, `copy8()` is right: each load ends before the bytes its store writes. Two copies
without a loop first, since most matches are at most 16 bytes. 32 bytes first made the slowest pages
slower: they have many short matches, and each copied 32 bytes.

The byte loop at the end is the format's own definition of a match. It runs only in the last 7 bytes
of the page.

## The bit reader

@@ 148-163
`bits` is left-aligned: the next bit of the stream is bit 63, so the next `n` bits are
`bits >> (64 - n)`, and a code indexes its table with the top bits directly. `count` says how many of
the top bits are real.

`count` is an `int` on purpose. Reading past the end gives zeros and makes `count` negative, and one
check at [the end of the page](#b568) catches that, instead of a check on every read. Wrong bits can
make wrong lengths and offsets, never a wrong memory access: each one is checked where it is used.

@@ 164-175
One unaligned big-endian load of 8 bytes, shifted down below the `count` real bits and ORed in. `p`
moves on by the bytes that went in whole, `(63 - count) >> 3`.

`count | 56` is `56 + count % 8`, so after a refill `count` is 56 to 63 and the decoder can rely on 56
bits. The partial byte at the bottom is not counted; the next refill loads it again to the same place,
so the OR needs no mask.
@anim refill One refill, one operation per step

@@ 176-187
With fewer than 8 bytes left, byte by byte: zram hands the decoder a buffer of exactly the compressed
size, and the byte after it can belong to another allocation. The loop also stops at a negative
`count`, where `56 - count` would be a shift by too much.

@@ 188-194
`(int)n` keeps the subtraction signed, so `count` can go negative. The largest drop is a token's code
with its offset bits, 11 + 14 bits, never 64.

@@ 195-215
A length value: one lookup with the top 8 bits gives the code's length `n`, the number of extra bits
`x` and the base, packed into one `u32`. The value is the base plus the `x` bits after the code.

`(bits << n) >> (64 - x)` would be a shift by 64 for `x = 0`, which is undefined. `>> 1` and then
`>> (63 - x)` gives 0 for `x = 0` and the same otherwise, without a branch.

## Deltas, level 3 only

@@ 216-236
Adds 4 or 8 bytes to the bytes `dist` before them, each byte on its own, modulo 256, in one word.
The low 7 bits of each byte add without a carry into the next byte (at most `0x7f + 0x7f`); bit 7
is then fixed with an XOR of both top bits. A plain word addition would carry from one byte into the
next.

`add8()` only for a distance of 8 or more: with distance 4 its two loads overlap the bytes it writes.

@@ 237-248
How far back the byte is that a literal is a delta to: 4, 8, or derived from the offset of the
sequence before, at least 4 ([chapter 4](book:#ch-format--deltas)).

@@ 249-277
Level 3 stores some literals as the difference to an earlier byte of the page. They can only be turned
back in the page, right after the copy and before the match after them, because that match may copy
from them.

`k` starts at `dist - at` near the page's start: literals before byte `dist` have nothing to be a delta
to. Then whole words forward, each reading `dist` bytes back, which are final by then. The index `k`
and not a pointer, because `d + dist` could be past the page, and C does not allow forming that
pointer.

@@ 278-302
The same for the fast path, which copied 16 bytes whatever `nl` is: all 16 bytes, no loop, no branch
on `nl`. The bytes past `nl` become garbage, which they were already; the match writes over them.
Near the page's start it falls back to `undelta_literals()`.

## Helpers of the fast path

@@ 303-326
A fast match has at most 34 bytes, and the caller guarantees 40 bytes of room, so the copy needs no
loop: 16, 32 or 40 bytes in `copy8()`s. With `8 <= off < 16` a load reads what the store before
wrote, as the overlap needs.

`m = d - off` once, because clang computed `d + 8 - off` again for every load. Always copying 40
bytes, without the branches on `len`, was slower: the extra loads read bytes the stores just before
them wrote.

@@ 327-344
Below offset 8: the pattern register, stored 5 times without a loop. The smallest step is 5, so 5
stores cover at least 28 bytes, with step 8 they cover 40, every fast match. The loop runs only for a
longer match with a step of 5, 6 or 7. No `& 7U` here: the caller has rejected offset 0.

@@ 345-359
The token: the top 11 bits index a table of 2048 entries. Every index is valid because the token code
is complete. The entry has the lengths and the offset's class, so the offset's bits follow without a
second lookup.

It refills only when fewer than 11 + 12 bits are left (a code and the longest offset), so most lookups
use bits that are already in the register and do not wait for a load. Used for the first sequence and
after a slow one.

@@ 360-382
Two tricks, both for a page the CPU decodes once (a swap-in):

- The next token is looked up before this sequence's copies. No length value follows on the fast path,
  so the next code starts right after the offset bits. An in-order core such as the Cortex-A55 copies
  while the lookup is on its way ([in-order cores](book:#ch-cpu--in-order)).
- The refill comes every second fast sequence, without checking the bits left. Whether the bits run
  short depends on the codes before, a branch the predictor cannot learn on a page it has not seen; a
  branch that alternates it gets right. Two fast sequences take at most 2 × (11 + 14) = 50 bits, and a
  refill leaves 56.

@@ 383-391
The three limits of the fast path. In the page: it writes at most 14 literal bytes and 40 bytes of
match, rounded up to 64. In the literals: one `copy16()`. In the input: one refill of 8 bytes. With
these, the fast path checks nothing else.

## decode_block(): the loop

@@ 392-400
`deltas` is a constant at both calls in `seqlz_decode()`, so the compiler makes two copies of the loop,
and a page without deltas runs none of their code. A check of `deltas` for every sequence made levels
1 and 2 slower to read. The cost is code size.

@@ 401-410
`last = 1` is the format's starting repeat offset ([offsets](book:#ch-format--offsets)). `skip = 0`
has the first fast sequence refill. `lit_bound` is where reading has to stop, behind `lit_end`.

@@ 411-423
The tables are prefetched before the header is read, so that their cache misses overlap with each
other and with the work on the header and the literals. An in-order core stops at every miss it did not
see coming.

The header's low 15 bits are the number of literals; more than a page is invalid, raw or coded.
@anim misses A model of 8 cache misses, one after another or overlapped

@@ 424-447
Bit 15 of the header picks the layout. Coded literals are decoded into the scratch first
([chapter 13](book:#ch-literals-dec)), which returns where the bitstream starts. It also gets `d`: a page
with a literal table of its own has that table built in the output page, before the loop overwrites
it ([own table](book:#ch-literals-dec--own)).

Raw literals are right after the header, with the bitstream behind them. Reading the bitstream as
literals is allowed, it is inside the input, so `lit_bound` is the end of the input.
@anim abbits The 9-byte example page, one field per step

@@ 448-460
The fast path copies without checking the room, so it is taken only where there is enough: three
compares against these limits. They are numbers and not pointers, because for a short input
`s_end - 8` would point before the buffer, which C does not allow. As numbers they are then below `lit`
and `br.p`, and the fast path is not taken.

@@ 461-471
The format has no count of sequences, and the last one has no token: the page ends when the literals
left fill the bytes left. `d_end - d == lit_end - lit` is the same as `d - lit == d_end - lit_end`,
whose right side never changes. So the loop compares two pointers it moves anyway against `end_gap`
([the end](book:#ch-format--end)).

A page of only literals has no token at all and jumps to the end.

@@ 472-487
Rare tokens have no code of their own: the escape's code, then the token's number in 12 bits. The
escape's entry has bit 31 set. After the refill and the 12 bits, `token_entry(idx, 0)` builds the entry
the table would have had, with a code length of 0 because the bits are dropped already. From here on an
escaped token is like any other ([token entry](book:#ch-internal--token-entry)).
@stepper decode The real decode_block() on two pages, statement by statement

@@ 488-499
The offset without a branch, from the token's entry: `<< TOK_N` shifts the code out, `>> back` brings
the offset bits down, `<< TOK_SHIFT` scales them (by 2 or 8 for some classes). Class 0 is the repeat
offset: it has no bits, `raw` is meaningless, and the select takes `last`. gcc makes the select a
`cmov`.

`TOK_LL` and `TOK_ML` are the lengths as they are, the match length already with its minimum of 4.

@@ 500-533
Most sequences go here. The condition: no length value follows (one bit of the entry), and the three
limits. The body: the next token first, 16 bytes of literals, the offset check, the match.

The offset check `off - 1U >= d - dst` is one unsigned compare for `1 <= off <= d - dst`: `off = 0`
wraps to the largest `unsigned int`.

`nl` is not checked against the literals left. On a damaged page `lit` can run past `lit_end`, but it
stays inside `lit_bound`, and the slow path, where every page ends, rejects it. The page's last match
never takes the fast path: fewer than 8 bits are left after it, and the fast path wants 8 bytes.

@@ 534-545
The slow path, with every check. `nl == 15` means a length value follows.

The order of the checks matters: `lit > lit_end` first, else `lit_end - lit` wraps and any `nl`
passes. The first fast path in September 2026 missed exactly this, and a test builds such a page since
then.

@@ 546-567
A match length of 35 in the token means a length value follows. With it `len` can be longer than a
page, which the check of room catches. `last` is set before the checks; when they fail it no longer
matters.

When the literals left fill the bytes left, no match can follow (a match has at least 4 bytes): the
loop ends. Otherwise the next token comes through the checked `next_token()`, and `skip = 0` restarts
the fast path's refill schedule.

@@ 568-587
The last sequence: the literals left, exactly the room left. Then the page has to end where its bits
end: fewer than 8 bits left (a negative `count` wraps to large as `unsigned int`, one compare for
both), every byte read, and the padding bits zero.

This strictness is the only damage check there is: zram stores no checksum
([why so strict](book:#ch-format--end)). Why no input can make the decoder read or write out of bounds
is [one list in the book](book:#ch-decode--safety).

## seqlz_decode()

@@ 588-599
The entry point. Deltas exist only with coded literals, flagged in bit 7 of byte 2; with raw literals
byte 2 is a literal. Each call passes a constant to `decode_block()`, which gives the two copies of
the loop.
