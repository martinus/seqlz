# src/seqlz_match.h

The LZ part of seqlz: the matcher walks through a page and at each position asks whether the next bytes
appeared earlier in the same page. What it finds are sequences, `ll` literals and a match of `ml`
bytes from `offset` bytes back ([the format](book:#ch-format)), and it hands each one to a function
it is given, in the compressor the encoder of `seqlz_compress.c`. All of it is inline, so matcher and
encoder become one loop.

This loop alone took about as long as all of `lz4` takes to compress a page, 17 300 to 17 900 cycles
against `lz4`'s 17 203 on x86-64 when it was last measured alone
([explored-designs.md](https://github.com/martinus/seqlz/blob/main/docs/explored-designs.md#the-compressor-second-round-fewer-instructions)),
which is why most lines here have a measurement behind them.

A reading order that follows the call:

1. [`seqlz_match_block()`](#b120), the matcher, and [its state](#b149).
2. [One position](#b178): two candidates, one branch. The step-through there runs the real function
   on two pages.
3. [A hit](#b204): [`seqlz_match_len()`](#b24) counts the match, at level 3
   [`seqlz_lazy()`](#b75) may take a longer one, then [emit and insert](#b222).
4. [`seqlz_hash5()`](#b13), the hash of the table.

## The hash

@@ 1-12
@anchor ch-matcher--file
A header of inline functions with one includer, `seqlz_compress.c`. The kernel copy, made by
[`port.py`](book:#ch-zram-glue), pastes the matcher right into that file, before its one caller.

@@ 13-23
5 bytes into a slot of 4096 (12 bits). `v << 24` drops 3 of the 8 bytes loaded and puts the 5 at the
top, the multiply mixes them, and the top 12 bits are the slot: the top bits of a product depend on
all input bits, the low ones only on the low input bits.

5 bytes, not 4 as in `lz4`: fewer and longer matches, 5% fewer cycles per page when it came, and with today's tables
also smaller pages. A match of 4 bytes at a new offset takes about as many bits as its 4 coded
literals. Same constant and formula as `zstd`'s 5-byte hash.
@anim hash The three steps of a hash-table matcher, as a model
@more {#ch-matcher--hash} A pointer's 5 bytes into a slot, step by step
The table holds the last position where 5 bytes were seen, at the slot of their hash. Two different
byte strings can get the same hash, a **collision**, so the matcher compares the bytes before it uses
a candidate. The hash is computed at about 4000 positions per page, so it has to be cheap.

`v` is 8 bytes loaded little endian, the byte at `p` in bits 0 to 7. `v << 24` drops the 3 bytes at
`p + 5` to `p + 7` and leaves the 5 bytes at `p` to `p + 4` in bits 24 to 63. The multiply mixes them,
modulo $2^{64}$, and `>> (64U - SEQLZ_HASH_BITS)` keeps the top 12 bits, a slot from 0 to 4095. For the
bytes `10 00 56 34 12`, the start of a pointer `0x7f1234560010` as x86-64 and arm64 store it:

<figure class="diagram">
<div class="scroll">
<svg viewBox="0 0 720 262" role="img" aria-label="The 8 bytes at p loaded as a number, shifted left by 24 so that only the 5 lowest bytes remain, multiplied by zstd's prime, and the top 12 bits of the product taken as the slot {{ch09.h1.slot}}.">
  <g font-size="11">
    <text x="276" y="30" text-anchor="middle">p+0</text><text x="328" y="30" text-anchor="middle">p+1</text><text x="380" y="30" text-anchor="middle">p+2</text><text x="432" y="30" text-anchor="middle">p+3</text><text x="484" y="30" text-anchor="middle">p+4</text><text x="536" y="30" text-anchor="middle">p+5</text><text x="588" y="30" text-anchor="middle">p+6</text><text x="640" y="30" text-anchor="middle">p+7</text>
  </g>
  <g font-size="12">
    <text x="10" y="58">bytes at p, in memory</text>
    <text x="10" y="108">v = get_unaligned_le64(p)</text>
    <text x="10" y="122" font-size="11">bit 63 left, bit 0 right</text>
    <text x="10" y="158">v &lt;&lt; 24</text>
    <text x="10" y="208">(v &lt;&lt; 24) * 889523592379</text>
    <text x="10" y="222" font-size="11">modulo 2^64</text>
  </g>
  <g fill="none" stroke="currentColor" stroke-width="1">
    <rect x="510" y="38" width="52" height="28"/><rect x="562" y="38" width="52" height="28"/><rect x="614" y="38" width="52" height="28"/>
    <rect x="250" y="88" width="52" height="28"/><rect x="302" y="88" width="52" height="28"/><rect x="354" y="88" width="52" height="28"/>
    <rect x="510" y="138" width="52" height="28"/><rect x="562" y="138" width="52" height="28"/><rect x="614" y="138" width="52" height="28"/>
    <rect x="250" y="188" width="52" height="28"/><rect x="302" y="188" width="52" height="28"/><rect x="354" y="188" width="52" height="28"/><rect x="406" y="188" width="52" height="28"/><rect x="458" y="188" width="52" height="28"/><rect x="510" y="188" width="52" height="28"/><rect x="562" y="188" width="52" height="28"/><rect x="614" y="188" width="52" height="28"/>
  </g>
  <g class="acc" stroke="currentColor" stroke-width="2">
    <rect class="fill-sheet" x="250" y="38" width="260" height="28"/>
    <rect class="fill-sheet" x="406" y="88" width="260" height="28"/>
    <rect class="fill-sheet" x="250" y="138" width="260" height="28"/>
    <line x1="302" y1="38" x2="302" y2="66"/><line x1="354" y1="38" x2="354" y2="66"/><line x1="406" y1="38" x2="406" y2="66"/><line x1="458" y1="38" x2="458" y2="66"/>
    <line x1="458" y1="88" x2="458" y2="116"/><line x1="510" y1="88" x2="510" y2="116"/><line x1="562" y1="88" x2="562" y2="116"/><line x1="614" y1="88" x2="614" y2="116"/>
    <line x1="302" y1="138" x2="302" y2="166"/><line x1="354" y1="138" x2="354" y2="166"/><line x1="406" y1="138" x2="406" y2="166"/><line x1="458" y1="138" x2="458" y2="166"/>
    <rect class="fill-sheet" x="250" y="188" width="78" height="28"/>
  </g>
  <g font-size="13" text-anchor="middle">
    <text x="276" y="57">10</text><text x="328" y="57">00</text><text x="380" y="57">56</text><text x="432" y="57">34</text><text x="484" y="57">12</text><text x="536" y="57">7f</text><text x="588" y="57">00</text><text x="640" y="57">00</text>
    <text x="276" y="107">00</text><text x="328" y="107">00</text><text x="380" y="107">7f</text><text x="432" y="107">12</text><text x="484" y="107">34</text><text x="536" y="107">56</text><text x="588" y="107">00</text><text x="640" y="107">10</text>
    <text x="276" y="157">12</text><text x="328" y="157">34</text><text x="380" y="157">56</text><text x="432" y="157">00</text><text x="484" y="157">10</text><text x="536" y="157">00</text><text x="588" y="157">00</text><text x="640" y="157">00</text>
    <text x="276" y="207">{{ch09.h1.prod.0}}</text><text x="315" y="207">{{ch09.h1.prod.1hi}}</text><text x="341" y="207">{{ch09.h1.prod.1lo}}</text><text x="380" y="207">{{ch09.h1.prod.2}}</text><text x="432" y="207">{{ch09.h1.prod.3}}</text><text x="484" y="207">{{ch09.h1.prod.4}}</text><text x="536" y="207">{{ch09.h1.prod.5}}</text><text x="588" y="207">{{ch09.h1.prod.6}}</text><text x="640" y="207">{{ch09.h1.prod.7}}</text>
  </g>
  <g class="acc" fill="none" stroke="currentColor" stroke-width="1.5">
    <path d="M 250 224 L 250 230 L 328 230 L 328 224"/>
  </g>
  <g class="acc" font-size="12"><text x="250" y="248">top 12 bits: {{ch09.h1.slot_hex}} = slot {{ch09.h1.slot}}</text></g>
</svg>
</div>
<figcaption>The 5 bytes 10 00 56 34 12 become slot {{ch09.h1.slot}}; the 3 bytes after them do not count.</figcaption>
</figure>

The bytes `20 00 56 34 12`, the start of the next pointer, give the product `{{ch09.h2.prod}}` and
slot {{ch09.h2.slot}}: one byte of difference moves the slot far, so the table is used evenly.

The top bits, because bit `k` of a product depends only on bits 0 to `k` of the factors: a carry moves
up, never down. The input is in bits 24 to 63 of `v << 24`, so bits 0 to 23 of the product are always
0, and only the top bits depend on all 40 input bits. Putting the 5 bytes at the top makes every one of
them reach the 12 bits that are kept. The constant is odd, so the multiplication modulo $2^{64}$ can be
undone and no two inputs collide before the shift. `zstd` uses the same constant, `prime5bytes`,
`0xcf1bbcdcbb`, and the same formula for its 5-byte hash.
@end
@more {#ch-matcher--five-bytes} Why 5 bytes and not 4, measured
`lz4` and `lzo` hash 4 bytes, the shortest match. seqlz hashes 5, so the table finds a candidate only
where 5 bytes repeat; matches of exactly 4 bytes still come from the repeat offset, which needs no
table, and from the table where two different runs of 5 bytes have the same hash and share their
first 4.

When it came, on 24th September 2026, the 5-byte hash took 5% fewer cycles to compress a page
(20 425 against 21 440, x86-64, userspace) and cost 0.6 points of memory (27.2% against 26.6% of the
page). In the kernel it took `seqlz-1`'s write p99 from 11 650 to 10 480 ns, twice what the cycles
say: the slowest pages have many short matches, and the 5-byte hash finds fewer of them
([explored-designs.md](https://github.com/martinus/seqlz/blob/main/docs/explored-designs.md#zram-prefetch-the-compressed-data-before-decompression),
"A hash of 5 bytes: C3 in the kernel").

With today's tables and coded literals the 4-byte hash is not even smaller: `seqlz-2` gets 24 bytes per
page larger with it. Most likely a match of 4 bytes at a new offset takes more bits than the 4 coded
literals it replaces, and the greedy matcher then misses the longer match behind it
([item 5](https://github.com/martinus/seqlz/blob/main/docs/explored-designs.md#5-the-matchers-hash-5-bytes-into-4096-slots-stays)).
A hash of 6 bytes went further the same way, fewer cycles for more memory, and was dropped.
@end

## seqlz_match_len()

@@ 24-52
How many bytes agree. The XOR of two little-endian words is 0 where they agree, and its lowest set bit
is in the first byte that differs, so `ctz(x) >> 3` is the number of equal bytes, one instruction.

The first 16 bytes with one branch: whether a match ends in the first 8 bytes is hard to predict, and
the select between the two halves becomes a conditional move. `| 1ULL << 63` tells the compiler `x` is
not 0, for which `ctz` is undefined.
@more {#ch-matcher--match-len} `L` against `x` in byte 11, step by step
`__builtin_ctzll()` is one `tzcnt` on x86-64, `rbit` and `clz` on arm64. Say `p` holds
`ABCDEFGHIJKLMNOP` and `q` holds `ABCDEFGHIJKxMNOP`:

| | value |
| --- | --- |
| `x1`, bytes 0 to 7 | 0, all equal |
| `x2`, bytes 8 to 15 | `{{ch09.mlen.x2}}`: byte 3 of it is `'L' ^ 'x'` = `0x34` |
| `x = x1 ? x1 : x2` | `x2` |
| <code>__builtin_ctzll(x &#124; 1ULL &lt;&lt; 63)</code> | {{ch09.mlen.ctz}}: bit 2 of byte 3, $3 \times 8 + 2$ |
| result | `8 + ({{ch09.mlen.ctz}} >> 3)` = {{ch09.mlen.len}} |

The selects become conditional moves, so a match that ends anywhere in the first 16 bytes costs one
branch, `x | x2`, which goes the same way most of the time. Whether a match ends in the first 8 bytes
is the hard question, most do and many do not, and a branch that goes one way mostly and the other way
at random is the worst case for a branch predictor ([chapter 3](#ch-cpu--branches)).

Inside the `if`, `x` is never 0, but the compiler does not have to see that through the select. With
bit 63 set the call is defined for any `x`, and for an `x` that is not 0 the count stays the same. The
loop after it tests `x` right before the count, and needs no OR.
@end
@more {#ch-matcher--sixteen} The first 16 bytes without a branch, measured
Comparing the first 16 bytes without a branch between the halves took the compressor from 22 848 to
21 916 cycles per page, and the mispredictions from 293 to 280 per page, x86-64 at a fixed 4.5 GHz, on
20 000 pages of a desktop dump. Of the mispredictions before, 29% were the loop of this function: 76%
of the matches were shorter than 12 bytes and 13% were 20 bytes or longer
([explored-designs.md](https://github.com/martinus/seqlz/blob/main/docs/explored-designs.md#the-compressor-second-round-fewer-instructions)).

One `ctz` of the first differing word, instead of a `ctz` of each word combined, came on 3rd October
2026 (commit `4186196`): 3% fewer cycles per page on x86-64. A loop that compares 16 bytes per round
instead of 8 was 1% slower on the Mi 9T's little core
([explored-designs.md](https://github.com/martinus/seqlz/blob/main/docs/explored-designs.md#the-matcher-on-an-in-order-core-34-fewer-compress-cycles-on-the-a55-same-output-kept)).
@end

@@ 53-68
Then 8 bytes per round, and bytes only within the last 7 of the page. The `>= 16` and `>= 8` keep
every load inside the page, and `q` is before `p`, so it is inside whenever `p` is. A match may overlap
its source, e.g. at offset 2 in a page of `ab` repeated: the matcher only reads, and
[the decoder](book:#ch-decode) has to care.

## Level 3's lazy step

@@ 69-74
Matches of 20 bytes or more are taken as they are. A lazy step up to that length kept most of what a
step at every match saves.

@@ 75-112
Is there a longer match one position later? It tries the same two candidates as the loop, the repeat
offset and the table's entry, with the 8 bytes and hash the loop already loaded one position ahead.
Both are in the page before `pos + 1`, for the reasons the loop's are ([the state](#b149)). Only a
strictly longer match wins, at the same length the match at `pos` has one literal fewer. The winner
moves `pos` on by one and stores it in the table, as the loop would have.

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
@more {#ch-matcher--emit} Who calls the matcher, with which receiver
A call through `emit_fn` once per sequence would be an indirect call, and the matcher would have to
keep its state in memory across it. The callers in `src/seqlz_compress.c`, `seqlz_find()`,
`compress_block()` and `compress_own()`, pass a constant function, `find_emit()`, `encode_emit()` and
`encode_emit_rec()`, all `__always_inline` too. After inlining, the compiler sees which function
`emit` is and inlines it as well: `gcc -O2` for x86-64 makes `seqlz_compress.o` without a single
indirect call. So the type is for the source: one matcher with several receivers.

The `bool lazy` of level 3 goes the same way. Every call passes a constant: `seqlz_find()` has one call
with `true` and one with `false`, `compress_block()` passes `false` and `compress_own()`, level 3's,
`true`. So each inlined copy of the loop sees a constant, and the one for levels 1 and 2 has no code of
level 3 at all ([`compress_block()`](seqlz_compress.c.html#b409)).

`seqlz_find()` collects the sequences into an array for tests and tools such as the table trainer,
which trains the tables on exactly the sequences this matcher finds. The kernel's copy has only the
encoder: `port.py` makes the matcher call `encode_seq()` by name, with `lazy` as its last argument,
so the lazy copy, level 3's, is also the one that writes the records for the deltas
([the records](#ch-encoder--records)).

Two passes, the matcher into an array and the encoder over it, took 24 257 instead of 22 848 cycles
per page, and the mispredictions went from 293 to 319 per page: the encoder's branches lost the
history of the matcher's branches, which predicted them
([explored-designs.md](https://github.com/martinus/seqlz/blob/main/docs/explored-designs.md#the-compressor-second-round-fewer-instructions),
x86-64, 20 000 pages of a desktop dump).
@end

@@ 120-148
@anchor ch-matcher
The plan: greedy like `lz4`'s fast mode, two candidates (the repeat offset, which costs 0 offset bits,
and the last position with the same hash), and every position, with no skipping after a stretch
without a match. Skipping needed state the loop has no register for; without it writes got faster,
pages that do not compress slower.

`lazy` is a constant at every call, so levels 1 and 2 have no trace of level 3's code.
@more {#ch-matcher--plan} Why greedy, and why two candidates
**Greedy.** At levels 1 and 2 the matcher takes the first match it finds, counts how long it is and
jumps over it. A **lazy** matcher checks whether a match one byte later is longer, a **hash chain**
keeps every earlier position of a hash and searches them all. `lz4`'s fast mode is greedy the same
way. Level 3 adds one lazy step for short matches and extends each match backwards, the paragraph of
the comment that begins "With lazy" ([level 3](#b211)).

One lazy step at every match, taking the next position's candidate if it is longer by 2 bytes or
more, took 24 808 instead of 20 400 compress cycles per page, 21% more, and `seqlz-1` stayed at 27.0%
of the page
([explored-designs.md](https://github.com/martinus/seqlz/blob/main/docs/explored-designs.md#zram-prefetch-the-compressed-data-before-decompression),
"A hash of 5 bytes: C3 in the kernel", September 2026). An earlier level 3 and 4, a hash chain priced
by the tables' code lengths with one or two lazy steps, stored pages 3.3% smaller than level 2 for 1.8
times the time to write, and were not kept
([explored-designs.md](https://github.com/martinus/seqlz/blob/main/docs/explored-designs.md#levels-3-and-4-a-hash-chain-priced-by-the-tables-as-small-as-zstd-3-on-two-of-four-dumps-and-faster-to-write-and-read-not-kept)).
The level 3 of 15th October 2026 is cheaper: no chain, the lazy step only for matches below 20 bytes,
and it came together with a literal table of the page's own
([explored-designs.md](https://github.com/martinus/seqlz/blob/main/docs/explored-designs.md#level-3-a-lazy-matcher-and-a-literal-table-of-the-pages-own-zstd-3s-memory-in-less-than-half-its-time-on-the-phone-kept)).

**Two candidates.** One is the **repeat offset**, which needs no lookup and costs 0 offset bits, e.g.
the same field of an array of structs every 16 or 24 bytes. With today's matcher about 22% of the
matches on desktop pages use it, and 32% on phone pages (20 000 pages of each dump, `seqlz_find()`,
same-filled pages left out, measured on 14th October 2026). The other is the last position whose 5
bytes had the same hash.

**What it misses** is older places, also at level 3. A slot keeps only the newest position, so a
longer match at an older place is not found. More positions per slot and a hash chain were measured,
and each cost about as much time as it saved bytes
([explored-designs.md, "seqlz-fast-lit: one of 8 literal tables per page"](https://github.com/martinus/seqlz/blob/main/docs/explored-designs.md#seqlz-fast-lit-one-of-8-literal-tables-per-page)).
`zstd` 3 searches harder, and that is part of why it stores pages smaller than level 2, at about twice
the time to write one.
@end
@more {#ch-matcher--step} Without `lz4`'s step: writes 3% faster, incompressible pages slower
`lz4` and `lzo` skip ahead faster and faster when they find no match. seqlz tries every position.

The step was `1 + (pos - anchor) >> 6`, as in `lz4`. Without it, on 30th September 2026, in the kernel
in a VM (Ryzen 9 7950X, CPU fixed at 4.5 GHz, 40 000 zram pages of a desktop), `seqlz-2` wrote
a page 0.23 µs faster, 3%, at 0.4 bytes more per page, and the write p99 went from 12 300 to 11 400 ns.
Pages that zram stores raw, because they do not compress, take 2.3 µs longer each: the whole page is
searched. It is not faster because it visits other positions: a step that starts only after 1024
literals visits the same positions on almost every page, and it was slower than the old step. The step
needs `anchor` in the loop for the positions without a match, and the loop has no register to spare
([explored-designs.md](https://github.com/martinus/seqlz/blob/main/docs/explored-designs.md#the-matcher-without-its-step-writes-1-to-3-faster-kept),
commit `055e7b5`). The 3% are from boots with only `lzo-rle` next to it; in boots with all four codecs
the writes gained 0.05 µs.
@end

@@ 149-177
Positions, not pointers: the limit is a constant, and the table stores positions in 2 bytes. `back` is
the repeat offset as a negative index, so the load is one addressing mode.

The table is cleared for every page, so every entry is a position of this page before `pos`, and a
candidate's bytes load without a check. A slot never written says "position 0", a real candidate. The
next position's bytes, hash, slot and candidate bytes are loaded one position ahead
([one position](#b178)).
@more {#ch-matcher--state} A cache of 8 KiB, cleared for every page
`limit` is {{ch09.limit}}, a constant the compiler puts into the compare, and positions instead of
pointers took 21 916 to 21 786 cycles per page
([second round](https://github.com/martinus/seqlz/blob/main/docs/explored-designs.md#the-compressor-second-round-fewer-instructions)).
`anchor` is where the next sequence's literals start. `last` starts at 1, as the format's repeat
offset does ([the format](#ch-format--offsets)).

The table is a cache, not a map. Each slot holds the last position stored there; a collision loses the
older one. That costs a match now and then, never a wrong result, because every candidate is compared
first.

- **2 bytes per slot.** A position needs 12 bits, also 14 for 16 KiB pages. 4096 pointers would take
  32 KiB per CPU. `lz4` in the kernel also stores positions of 2 bytes for inputs this small, 8192 of
  them.
- **4096 slots.** On the Mi 9T, 2048 slots made pages 939.6 instead of 936.9 bytes and were no faster;
  8192 slots made them 937.1 bytes, cost the in-order A55 1.6 µs more per write, and would take 16 KiB
  per CPU, all of `lz4`'s budget
  ([item 5](https://github.com/martinus/seqlz/blob/main/docs/explored-designs.md#5-the-matchers-hash-5-bytes-into-4096-slots-stays)).
  With 16 KiB pages `SEQLZ_HASH_BITS` is 13: 8192 slots, 16 KiB.
- **Cleared for each page.** A `memset()` of {{ch09.table_bytes}} bytes per page took the compressor
  from 25 064 to 23 327 cycles per page, 7% less, against a check at every position whether the entry
  is a position of this page
  ([second round](https://github.com/martinus/seqlz/blob/main/docs/explored-designs.md#the-compressor-second-round-fewer-instructions)).
  `lz4` in the kernel clears its 16 KiB for each page too.

The clearing is also why a candidate's 4 bytes load without a check: every entry is a position of this
page before `pos`, 0 if nothing was stored. A slot never written is not empty, it says "position 0",
and a match there is real: the page of `ab` repeated finds its only match that way, at position 2,
against the 4 bytes at 0. The repeat offset is safe the same way: `last` and `pos` start at 1, after a
match `last` is at most the match's start, and `pos` only grows.
@end

@@ 178-203
The path taken most: no match. Both candidates are read and compared, and `rep_hit | cand_hit`, a
bitwise OR, decides with one branch. With a branch per candidate it mispredicted almost twice as often
as `lz4`.

The next position's values were started one position ahead, so on an in-order core the compares use
loads done during the position before: 3.4% fewer cycles on the Cortex-A55, the same output. The store
into the table comes before `cand = table[h]` of the next position, so the matches stay the same.
@stepper match seqlz_match_block() on two pages, statement by statement
@more {#ch-matcher--position} One branch for two candidates, measured
This is the path taken most: on the phone's pages a run without a match is 4 positions long on average
([explored-designs.md](https://github.com/martinus/seqlz/blob/main/docs/explored-designs.md#the-a55-again-the-write-as-on-4-october-the-decoders-code-prefetched-for-cold-reads-not-kept)).
The last position tried, {{ch09.limit_last}}, reads up to byte 4095, so the last 8 bytes of a page
never start a match. `cur` is the first 4 of the 8 bytes in `v`, one load less per position.

`rep_hit | cand_hit` is the bitwise OR, not `||`, so both compares are computed and one branch decides;
with `||` compilers often make a second branch. With three branches per position, the first `seqlz-1`
mispredicted 539 times per page. Reading both candidates and deciding with one branch took that to
329, and the p99 of the cold compress loop from 15.4 to 12.6 µs. With the page in the cache, the extra
reads cost 7%
([explored-designs.md](https://github.com/martinus/seqlz/blob/main/docs/explored-designs.md#the-compressor-seqlz-fast),
x86-64, userspace, September 2026, with an older matcher).

The store into the table comes before the branch, so it happens with or without a match, and before
`cand = table[h]` for the next position: if that has the same hash, it must find `pos`, as it did
before the loads moved ahead.
@end
@more {#ch-matcher--ahead} One position ahead, on an in-order core
On an in-order core such as the Cortex-A55, which waits at every load whose value is not there yet
([chapter 3](#ch-cpu--in-order)), a position without a match is one chain: load 8 bytes, multiply,
load the slot, load 4 bytes at it, compare, branch. Before commit `996248b` each position ran that
chain from its top, at 0.91 instructions per cycle on the A55. Now the loop starts the next position's
loads and multiplication before the compares of this one, which is software pipelining: the compares
use operands loaded during the position before.

On the Mi 9T's little core (Cortex-A55) the compress loop took 42 349 instead of 43 841 cycles per page,
and 39 762 instead of 40 965 instructions; on x86-64 16 148 instead of 16 489 user cycles. The 4 bytes
taken from the 8 of the hash and the negative index for the repeat offset made it 15 instead of 17
instructions per position. Every page's compressed length stayed the same, on 455 239 pages of a
desktop dump and 81 709 of a phone's
([explored-designs.md](https://github.com/martinus/seqlz/blob/main/docs/explored-designs.md#the-matcher-on-an-in-order-core-34-fewer-compress-cycles-on-the-a55-same-output-kept),
commit `996248b`, 3rd October 2026). Reading the next table entry before the store and choosing the
right one with a compare was 9% slower: more instructions.
@end
@more {#ch-matcher--back} The page of 512 pointers, position by position
The step-through under the note runs `seqlz_match_block()` on two pages, one statement at a time: this
page of 512 pointers, and the made-up page of [chapter 16](#ch-trace). It is the function of this file,
recorded while it searched the pages when the book was built, so every position, slot and match is
the real one. Step with the buttons or the arrow keys, and with shift from one sequence to the next.

The tables below come from `seqlz_find()` on the same page, and from a copy of `seqlz_match.h` with a
`printf()` after the two compares. The page: 512 pointers of 8 bytes, `0x7f1234560010 + 16 * i`, in
memory `10 00 56 34 12 7f 00 00`, `20 00 56 34 12 7f 00 00`, ...

| `pos` | 5 bytes at `pos` | `h` | `cand` | repeat offset | `rep_hit` | `cand_hit` |
| --- | --- | --- | --- | --- | --- | --- |
| 1 | `{{ch09.ptr.1.bytes}}` | {{ch09.ptr.1.h}} | {{ch09.ptr.1.cand}} | {{ch09.ptr.1.last}} | {{ch09.ptr.1.rep_hit}} | {{ch09.ptr.1.cand_hit}} |
| 2 to 7 | ... | ... | 0 | 1 | 0 | 0 |
| 8 | `{{ch09.ptr.8.bytes}}` | {{ch09.ptr.8.h}} | {{ch09.ptr.8.cand}} | {{ch09.ptr.8.last}} | {{ch09.ptr.8.rep_hit}} | {{ch09.ptr.8.cand_hit}} |
| 9 | `{{ch09.ptr.9.bytes}}` | {{ch09.ptr.9.h}} | **{{ch09.ptr.9.cand}}** | {{ch09.ptr.9.last}} | {{ch09.ptr.9.rep_hit}} | **{{ch09.ptr.9.cand_hit}}** |
| 16 | `{{ch09.ptr.16.bytes}}` | {{ch09.ptr.16.h}} | {{ch09.ptr.16.cand}} | {{ch09.ptr.16.last}} | {{ch09.ptr.16.rep_hit}} | {{ch09.ptr.16.cand_hit}} |
| 17 | `{{ch09.ptr.17.bytes}}` | {{ch09.ptr.17.h}} | {{ch09.ptr.17.cand}} | **{{ch09.ptr.17.last}}** | **{{ch09.ptr.17.rep_hit}}** | {{ch09.ptr.17.cand_hit}} |
@assert ch09.ptr.miss3to7 == 0 and ch09.ptr[2].cand == 0 and ch09.ptr[2].cand_hit == 0 and ch09.ptr[2].rep_hit == 0 and ch09.ptr[9].cand_hit == 1 and ch09.ptr[17].rep_hit == 1 and ch09.h2.slot == ch09.ptr[8].h

Up to position 8 every slot is still 0, and the 4 bytes at 0 match nothing. Position 8 puts
`20 00 56 34 12` into slot {{ch09.ptr.8.h}}, the second example of [the hash](#ch-matcher--hash). At
position 9 the 5 bytes `00 56 34 12 7f` come again, slot {{ch09.ptr.9.h}} holds 1, and the match runs
{{ch09.ptr.seq.0.ml}} bytes, to `30` against `20`, at offset {{ch09.ptr.seq.0.off}}. From then on
position 16, the low byte of the third pointer, misses and position 17 hits at the repeat offset 8:

| sequence | starts at | `ll` | `ml` | offset |
| --- | --- | --- | --- | --- |
| 0 | {{ch09.ptr.seq.0.at}} | {{ch09.ptr.seq.0.ll}} | {{ch09.ptr.seq.0.ml}} | {{ch09.ptr.seq.0.off}} |
| 1 to 13 | {{ch09.ptr.seq.1.at}}, 24, ..., {{ch09.ptr.seq.13.at}} | {{ch09.ptr.seq.1.ll}} | {{ch09.ptr.seq.1.ml}} | {{ch09.ptr.seq.1.off}} |
| 14 | {{ch09.ptr.seq.14.at}} | {{ch09.ptr.seq.14.ll}} | {{ch09.ptr.seq.14.ml}} | {{ch09.ptr.seq.14.off}} |
| ... | | | | |
| {{ch09.ptr.before_last}} | {{ch09.ptr.seq.509.at}} | {{ch09.ptr.seq.509.ll}} | {{ch09.ptr.seq.509.ml}} | {{ch09.ptr.seq.509.off}} |
| {{ch09.ptr.last}} | {{ch09.ptr.seq.510.at}} | {{ch09.ptr.seq.510.ll}} | {{ch09.ptr.seq.510.ml}} | {{ch09.ptr.seq.510.off}} |
@assert ch09.ptr.odd == 0 and ch09.ptr.seqs == 511 == pointers.seqs

{{ch09.ptr.seqs}} sequences. Every 16th pointer carries into its second byte, `0x...00f0 + 0x10`, and
sequence 14 has 2 literals, `00 01`, and a match of 6. The last sequence shows `limit` at work: the
loop stops at {{ch09.ptr.seq.510.at}}, and the last pointer's 8 bytes become literals, where one
literal and a match of 7 would have done.

::: try The sequences of the page of pointers
In the repository, save this as `find.c` and build it with `gcc -O2 -Isrc -o find find.c
src/seqlz_compress.c src/seqlz_compress_literals.c src/seqlz_codes.c src/seqlz_default_tables.c src/seqlz_lit_sets.c`:

```c
#include "seqlz.h"
#include <stdio.h>

static struct seqlz_state st;
static u8 page[SEQLZ_BLOCK_SIZE];
static struct seqlz_sequence seq[SEQLZ_MAX_SEQUENCES];

int main(void)
{
	unsigned int i, n, pos = 0;

	for (i = 0; i < SEQLZ_BLOCK_SIZE / 8; i++)
		put_unaligned_le64(0x7f1234560010ULL + 16 * i, page + 8 * i);
	n = seqlz_find(&st, page, seq, 2);
	printf("%u sequences\n", n);
	for (i = 0; i < n; i++) {
		printf("at %4u: ll %u, ml %u, offset %u\n", pos,
		       seq[i].literals, seq[i].match, seq[i].offset);
		pos += seq[i].literals + seq[i].match;
	}
	return 0;
}
```
@program ch09-find

It prints `{{ch09.ptr.seqs}} sequences` and the sequences of the table above, these among them:

@output ch09-find 1-4,15-17,510-512

Fill `page` with your own bytes to see what the matcher makes of them.
:::
@end

@@ 204-210
When both hit, the repeat offset wins, as its offset costs no bits. The match is counted from `pos`,
4 bytes are known to agree.

Levels 1 and 2 do not extend matches backwards: it stood between choosing the candidate and counting,
and taking it out made writes 1% to 7% faster for pages up to 0.4% larger.
@more {#ch-matcher--hit} The repeat offset first, and no step back, measured
Which of the two candidates gives the longer match is not checked. Measured offline on 40 000 zram
pages of a desktop, always taking the longer of the two made pages about 1 byte larger, and taking the
table's only when it is longer by more than 2 bytes saved 0.5 bytes per page, for a second call of
`seqlz_match_len()`
([explored-designs.md](https://github.com/martinus/seqlz/blob/main/docs/explored-designs.md#the-matcher-without-its-step-writes-1-to-3-faster-kept),
"The longer of the two candidates").

A match can start before the position where it is found, because the positions inside a match are not
in the table ([the insert](#b222)) and slots are overwritten: when a later place repeats the inside of
an earlier match, the matcher finds it only from the one position inserted near that match's end.
[A copy found 2 bytes late](#ch-matcher--late) shows it.

Until October 2026 the matcher stepped back while the byte before `pos` was the byte before the
candidate, down to `anchor`. That made 2.4% of the matches longer, by 1 or 2 bytes, 0.03 bytes per
match. It also stood between choosing the candidate and counting the match, so every match waited
for two more loads and a compare. On the Mi 9T, zram in its own kernel, 19 775 pages of a phone,
`seqlz-2` wrote a page in 11.93 instead of 12.77 µs on the Cortex-A76 and in 33.09 instead
of 34.10 µs on the A55, reads the same
([explored-designs.md](https://github.com/martinus/seqlz/blob/main/docs/explored-designs.md#the-matcher-without-its-step-back-writes-66-faster-on-the-a76-and-1-to-2-in-the-vm-kept),
15th October 2026). In the kernel VM it wrote 1.0% to 2.3% faster on four dumps, at 1.6 bytes less to
3.7 bytes more per page. `seqlz-1` loses more, up to 15 bytes per page: a byte outside a match costs
it a whole byte.
@end

@@ 211-221
Level 3 only: the lazy step for short matches, then the step back, as long as the byte before the
match equals the byte before its source. `pos` and `m` move together, so the offset stays. Here it
comes after the count, where it costs less. `pos > anchor` keeps it out of the sequence before, whose
match ended at `anchor`, and `m > 0` keeps `src[m - 1]` in the page.
@more {#ch-matcher--lazy} Two sequences become one, on an example
A page of random bytes, as the page of [a copy found 2 bytes late](#ch-matcher--late) without its
copies, where bytes 100 to 104 are `0x5a` and the 4 bytes at 21, and byte 199 is `0x5a` again,
followed by a copy of bytes 21 to 51. At 199 the table finds the 5 bytes at 100. Level 2 takes them,
then finds the rest of the copy right behind:

| level | sequence | literals | match at | `ml` | offset |
| --- | --- | --- | --- | --- | --- |
| 2 | 0 | {{ch09.lazy.l2.0.ll}} | {{ch09.lazy.l2.0.at}} | {{ch09.lazy.l2.0.ml}} | {{ch09.lazy.l2.0.off}} |
| 2 | 1 | {{ch09.lazy.l2.1.ll}} | {{ch09.lazy.l2.1.at}} | {{ch09.lazy.l2.1.ml}} | {{ch09.lazy.l2.1.off}} |
| 3 | 0 | {{ch09.lazy.l3.0.ll}} | {{ch09.lazy.l3.0.at}} | {{ch09.lazy.l3.0.ml}} | {{ch09.lazy.l3.0.off}} |
@assert ch09.lazy.l2.seqs == 3 and ch09.lazy.l3.seqs == 2 and ch09.lazy.l2[0].ml < ch09.lazy.l3[0].ml and ch09.lazy.l3[0].at == ch09.lazy.l2[0].at + 1

At level 3 the 5 bytes at 199 are below {{ch09.lazy_len}}, so [`seqlz_lazy()`](#b75) looks at 200,
where the table has position 21 for the same 5 bytes, and the match there is {{ch09.lazy.l3.0.ml}}
bytes long. Byte 199 becomes a literal, and one sequence with one token and one offset does what two
did. The step back finds `0x5a` before 200 but not before 21, and stops.

The extension backwards is the step back that [the measurement](#ch-matcher--hit) took out of levels 1
and 2. It cost there because it came between choosing the candidate and counting the match. At level
3 it comes after the count, and after the lazy step, which can move a match one position on and leave
the byte it passed over to be taken back with the new offset. On the page of
[a copy found 2 bytes late](#ch-matcher--late) it takes the 2 literals before the match.

The animation under the note of [`seqlz_lazy()`](#b75) has a made-up page where both take bytes: the
table finds a short match at `pos`, `seqlz_lazy()` a longer one at `pos + 1`, and the step back moves
that one back past `pos`, into the literals before it. `anim/lazy.c` builds that page and checks each
match against what `seqlz_find()` returns at levels 2 and 3.
@end
@more {#ch-matcher--lazy-measured} The lazy step from 20 bytes on, the step back kept, measured
On 20 000 pages each of a phone dump and two desktop samples, a lazy step for matches up to
4 + n bytes saved more bytes the larger n was, and n = 16, the 20 of `SEQLZ_LAZY_LEN`, kept most of
what a step at every match saves. Without the step back level 3 was 0.69% larger on ten dumps, and in
userspace on the Mi 9T's Cortex-A76 it wrote 2.1% faster and read 3.5% slower. In the kernel VM the
lazy step alone was on no hull; with the literal table of the page's own it was. Level 3 as a whole
wrote a page in about 16 µs against 12 µs at level 2 on the A76, in the phone's own kernel, on 15th
October 2026
([explored-designs.md](https://github.com/martinus/seqlz/blob/main/docs/explored-designs.md#level-3-a-lazy-matcher-and-a-literal-table-of-the-pages-own-zstd-3s-memory-in-less-than-half-its-time-on-the-phone-kept)).
@end

@@ 222-244
The new repeat offset, then the sequence goes to the encoder. Of the skipped positions inside the
match, one goes into the table, 2 bytes before its end, as `lz4`'s fast mode does: data that repeats
once tends to repeat near the end of the last match. Every position would be a hash, a load and a
store per byte.

After the loop the bytes from `anchor` on are the last sequence, literals only, none when a match ran
to the end. The last 8 bytes never
start a match, every try reads 8 bytes. The format has no count of sequences
([the end](book:#ch-format--end)). Every other sequence covers at least 4 bytes, so a page has at most
{{const.MAX_SEQUENCES}}, `SEQLZ_MAX_SEQUENCES`, which `seqlz_find()`'s array and
[the encoder's bound](book:#ch-encoder--two-pages) rely on.
@more {#ch-matcher--insert} One position of a match, not all, and not the start
The insert comes before the load of `table[h]` for the new `pos`, so a new position with the same hash
as `pos - 2` finds it.

Every position of a match would be a hash, a load and a store per byte, where the matcher now does
nothing at all. Storing the start of each match as well, as `zstd`'s fast mode does, cost 550 cycles
more per page for 2.1 bytes per page less, and was not kept
([explored-designs.md](https://github.com/martinus/seqlz/blob/main/docs/explored-designs.md#the-matcher-without-its-step-writes-1-to-3-faster-kept)).
@end
@more {#ch-matcher--late} A copy found 2 bytes late
A page of random bytes `R`, bits 16 to 23 of the C library's classic linear congruential generator
from seed 12345, with bytes 0 to 63 copied to 64 to 127, and the 14 bytes at 124 to 137 copied to 398
to 411. The matcher finds the copy at 64 at once, a match of {{ch09.rnd.first.ml}} bytes at offset
{{ch09.rnd.first.off}}, and inserts position {{ch09.rnd.first.inserted}} ($= 128 - 2$). At the second
copy:

| `pos` | 5 bytes at `pos` | `h` | `cand` | `cand_hit` |
| --- | --- | --- | --- | --- |
| 398 | `{{ch09.rnd.398.bytes}}` | {{ch09.rnd.398.h}} | {{ch09.rnd.398.cand}} | {{ch09.rnd.398.cand_hit}} |
| 399 | `{{ch09.rnd.399.bytes}}` | {{ch09.rnd.399.h}} | {{ch09.rnd.399.cand}} | {{ch09.rnd.399.cand_hit}} |
| 400 | `{{ch09.rnd.400.bytes}}` | {{ch09.rnd.400.h}} | {{ch09.rnd.400.cand}} | {{ch09.rnd.400.cand_hit}} |
@assert ch09.rnd.first.at == 64 and ch09.rnd[398].cand_hit == ch09.rnd[399].cand_hit == 0 and ch09.rnd[400].cand == ch09.rnd.first.inserted and ch09.rnd[399].cand not in (0, 125)

398 and 399 miss: their 5 bytes were last at 124 and 125, inside the match, and slot
{{ch09.rnd.399.h}} even holds {{ch09.rnd.399.cand}}, a collision. Position 400 has the bytes of 126,
the one position inserted, and hits. `seqlz_find()` returns {{ch09.rnd.seq.ll}} literals, 398 and
399 among them, and a match of {{ch09.rnd.seq.ml}} at offset {{ch09.rnd.seq.off}}, starting at
{{ch09.rnd.seq.at}}. The copy started 2 bytes earlier, and those 2 bytes are literals: at levels 1 and 2
the matcher does not step back, [the note on a hit](#b204) says why. At level 3 it does, and
`seqlz_find(&st, page, seq, 3)` returns {{ch09.rnd3.seq.ll}} literals and a match of
{{ch09.rnd3.seq.ml}} at {{ch09.rnd3.seq.at}}, the whole copy.
@assert ch09.rnd.seq.at == 400 and ch09.rnd.seq.ml == 12 and ch09.rnd3.seq.at == 398 and ch09.rnd3.seq.ml == 14
@end
