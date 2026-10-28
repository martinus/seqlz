# src/seqlz_match.h

The LZ part of seqlz: the matcher walks through a page and at each position asks whether the next bytes
appeared earlier in the same page. What it finds are sequences, `ll` literals and a match of `ml`
bytes from `offset` bytes back ([the format](book:#ch-format)), and it hands each one to a function
it is given, in the compressor the encoder of `seqlz_compress.c`. All of it is inline, so matcher and
encoder become one loop.

This loop alone takes about as long as all of `lz4` takes to compress a page, which is why most lines
here have a measurement behind them.

A reading order that follows the call:

1. [`seqlz_match_block()`](#b120), the matcher, and [its state](#b149).
2. [One position](#b178): two candidates, one branch. The step-through there runs the real function
   on two pages.
3. [A hit](#b204): [`seqlz_match_len()`](#b24) counts the match, at level 3
   [`seqlz_lazy()`](#b75) may take a longer one, then [emit and insert](#b222).
4. [`seqlz_hash5()`](#b13), the hash of the table.

## The hash

@@ 1-12
A header of inline functions with one includer, `seqlz_compress.c`. The kernel copy pastes the matcher
right into that file, before its one caller.

@@ 13-23
5 bytes into a slot of 4096 (12 bits). `v << 24` drops 3 of the 8 bytes loaded and puts the 5 at the
top, the multiply mixes them, and the top 12 bits are the slot: the top bits of a product depend on
all input bits, the low ones only on the low input bits.

5 bytes, not 4 as in `lz4`: fewer and longer matches, 5% fewer cycles per page when it came, and with today's tables
also smaller pages. A match of 4 bytes at a new offset takes about as many bits as its 4 coded
literals. Same constant and formula as `zstd`'s 5-byte hash.
@anim hash The three steps of a hash-table matcher, as a model

## seqlz_match_len()

@@ 24-52
How many bytes agree. The XOR of two little-endian words is 0 where they agree, and its lowest set bit
is in the first byte that differs, so `ctz(x) >> 3` is the number of equal bytes, one instruction.

The first 16 bytes with one branch: whether a match ends in the first 8 bytes is hard to predict, and
the select between the two halves becomes a conditional move. `| 1ULL << 63` tells the compiler `x` is
not 0, for which `ctz` is undefined.

@@ 53-68
Then 8 bytes per round, and bytes only within the last 7 of the page. The `>= 16` and `>= 8` keep
every load inside the page, and `q` is before `p`, so it is inside whenever `p` is.

## Level 3's lazy step

@@ 69-74
Matches of 20 bytes or more are taken as they are. A lazy step up to that length kept most of what a
step at every match saves.

@@ 75-112
Is there a longer match one position later? It tries the same two candidates as the loop, the repeat
offset and the table's entry, with the 8 bytes and hash the loop already loaded one position ahead.
Only a strictly longer match wins; it moves `pos` on by one and stores it in the table, as the loop
would have.

The repeat offset is skipped where the match at `pos` has it already: one position later, that is
the same match one byte shorter. A branch per candidate is fine here, it runs once per short match.
@anim lazy Level 3 on a page with three copies in it

## The matcher

@@ 113-119
The receiver of the sequences. It costs no call: the matcher is `__always_inline`, every caller passes
a constant `__always_inline` function, and after inlining the compiler sees which one it is.
`seqlz_compress.o` has no indirect call.

Matching and encoding as one loop: two passes were 6% slower, the encoder's branches lost the history
of the matcher's that predicted them.

@@ 120-148
The plan: greedy like `lz4`'s fast mode, two candidates (the repeat offset, which costs 0 offset bits,
and the last position with the same hash), and every position, with no skipping after a stretch
without a match. Skipping needed state the loop has no register for; without it writes got faster,
pages that do not compress slower.

`lazy` is a constant at every call, so levels 1 and 2 have no trace of level 3's code.

@@ 149-177
Positions, not pointers: the limit is a constant, and the table stores positions in 2 bytes. `back` is
the repeat offset as a negative index, so the load is one addressing mode.

The table is cleared for every page, so every entry is a position of this page before `pos`, and a
candidate's bytes load without a check. A slot never written says "position 0", a real candidate. The
next position's bytes, hash, slot and candidate bytes are loaded one position ahead
([the state](book:#ch-matcher--state)).

@@ 178-203
The path taken most: no match. Both candidates are read and compared, and `rep_hit | cand_hit`, a
bitwise OR, decides with one branch. With a branch per candidate it mispredicted almost twice as often
as `lz4`.

The next position's values were started one position ahead, so on an in-order core the compares use
loads done during the position before: 3.4% fewer cycles on the Cortex-A55, the same output. The store
into the table comes before `cand = table[h]` of the next position, so the matches stay the same.
@stepper match seqlz_match_block() on two pages, statement by statement

@@ 204-210
When both hit, the repeat offset wins, as its offset costs no bits. The match is counted from `pos`,
4 bytes are known to agree.

Levels 1 and 2 do not extend matches backwards: it stood between choosing the candidate and counting,
and taking it out made writes 1% to 7% faster for pages up to 0.4% larger.

@@ 211-221
Level 3 only: the lazy step for short matches, then the step back, as long as the byte before the
match equals the byte before its source. `pos` and `m` move together, so the offset stays. Here it
comes after the count, where it costs less.

@@ 222-244
The new repeat offset, then the sequence goes to the encoder. Of the skipped positions inside the
match, one goes into the table, 2 bytes before its end, as `lz4`'s fast mode does: data that repeats
once tends to repeat near the end of the last match. Every position would be a hash, a load and a
store per byte.

After the loop the bytes from `anchor` on are the last sequence, literals only. The last 8 bytes never
start a match, every try reads 8 bytes.
