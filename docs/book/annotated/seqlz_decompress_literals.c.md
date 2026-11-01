# src/seqlz_decompress_literals.c

The decoder of a page's coded literals. They are Huffman coded in 8 streams, literal `i` in stream
`i % 8`, with one of 8 fixed literal tables or a table the page brings along
([the layout](book:#ch-format--coded)). `decode_block()` in `seqlz_decompress.c` calls
`seqlz_decode_literals()` once, before its loop over the sequences. It decodes all literals into the
scratch, and from then on the loop reads them as if the page had them raw. The encoder's side is
[`seqlz_compress_literals.c`](seqlz_compress_literals.c.html).

Decoding a Huffman code is a chain: look up the next 10 bits, write the byte, shift by the code's
length, and only then are the next code's bits known. 8 streams are 8 chains with nothing between them,
which an out-of-order CPU works on at the same time ([chains](book:#ch-cpu--chains)).

A reading order that follows the call:

1. [`seqlz_decode_literals()`](#b320): the checks, the stream sizes, and the two loops over the streams.
2. [`read_lit_header()`](#b256): byte 2 and the delta's byte, then a fixed table or the page's own.
3. [`read_own_table()`](#b162), only for a page with its own table, and [`own_fill()`](#b113).
4. The stream reader the loops use: [`lit_refill()`](#b37), [`lit_decode()`](#b54), and at the end
   [`lit_stream_ends()`](#b95).

The deltas of level 3 come out of here still as deltas; the loop over the sequences adds the bytes
back in the page ([the deltas](seqlz_decompress.c.html#b220)).
@more {#ch-literals-dec--safety} Why nothing reads or writes outside its buffer
`seqlz_decompress()` is safe for any input ([the decoder's safety](#ch-decode--safety)). For
`seqlz_decode_literals()`:

- **Reads stay in the input.** The header is read after `src_len` is checked, the sizes from a copy. A
  refill loads 8 bytes only when 8 are left before the end of the whole input, else `lit_load_tail()`
  reads up to it. Table indices are 10 bits, a fixed table's number 3 bits. A page's own table is read
  from the input only up to its end, and the steps past it read zeros
  ([the window](#b199)).
- **Reading past a stream is allowed, and caught.** A refill near the end of stream 3 loads bytes of
  stream 4 or of the bitstream, still inside the input. A stream that uses them as codes fails
  `lit_stream_ends()`. The format allows exactly this
  ([format.md](https://github.com/martinus/seqlz/blob/main/docs/format.md#coded-literals)). A damaged
  stream's `ip` can even move past `end`; its refills then read nothing, and the check rejects it.
- **Writes stay in the scratch.** Every write is `out[at]` with `at < n_lit + 8`, and `n_lit` is at most
  a page. `read_own_table()` writes its buckets at a masked index in the scratch, and its decode table only
  once the code is complete, exactly 1024 entries, into the page.
- **The deltas read and write only the page.** `undelta_literals()` writes the `nl` literals at `d`,
  which the loop over the sequences has checked against the room left, or for the last sequence are
  the room left ([the end](#ch-decode--end)), and up to 15 bytes past them only where
  `d_end - d >= nl + 15`; it reads at least `dist` bytes
  back, never before `dst`, because the first literal it restores is at least `dist` bytes into the
  page. `undelta_fast()` writes the 16 bytes the fast path copied, inside its room, and reads at most
  `dist` bytes back only where `d - dst` is at least `dist`; nearer the page's start it calls
  `undelta_literals()` ([both](seqlz_decompress.c.html#b270)).
- **The work is bounded.** No loop depends on the codes: `n_lit / 40` rounds and one tail. The loop of
  `read_own_table()` depends on them, and ends anyway: every step takes a code, and every 8 bits past the
  input are two lengths of 9 bits, which fill the code space after at most 512 of them.

The fuzz targets allocate the scratch and the page at their exact sizes, so AddressSanitizer reports a
read or write of one byte too far ([the fuzzers](#ch-zram-glue--fuzzing)).
@end
@more {#ch-literals-dec--cost-read} What a page with coded literals costs to read, measured
On the A55, an in-order core, a round is 312 instructions for 40 literals, in userspace with clang 21
from NDK r30: per literal a shift for the index, the load, a shift for the byte, the store and the shift
of the stream, plus 2.8 for the refills and the stream pointers on the stack. At about one instruction
per cycle, a page with coded literals takes about 8400 cycles more than with raw literals, 8 per
literal, and `seqlz_decode_literals()` has 14% of the decoder's samples, with no hot spot. Putting the 8 loads
of a step first saved 2.8% of the A55's decode cycles, and was not built into the kernel module
([explored-designs.md](https://github.com/martinus/seqlz/blob/main/docs/explored-designs.md#8-coded-literals-on-the-a55-78-instructions-per-literal-at-one-per-cycle)).

The fixed cost per page counts more there: the 2 KiB table comes from memory, and the literals go
through the scratch before the sequences copy them. That is why the encoder codes literals only where
they save {{ch11.coded_min}} bytes and $\frac{1}{16}$ of themselves at level 2, {{ch11.coded_min_own}}
bytes at level 3 ([the rule](#ch-literals-enc--51)). Decoding the
literals into the end of the output page instead, which saves the scratch, was built: on 4 KiB pages it
made cold reads 0.08 to 0.22 µs slower in the kernel VM, and it was not kept
([explored-designs.md](https://github.com/martinus/seqlz/blob/main/docs/explored-designs.md#16-kib-pages-tuned-c5-does-not-hold-the-literals-in-the-page-would-fix-it-not-kept)).
@end

## The stream reader

@@ 1-12
Two kinds of tables, one decoder: a page's own table is turned into the same kind of decode table a
fixed one has, so everything after the header does not know the difference.

@@ 13-22
The trick of this file. A stream is a pointer `ip` and 64 bits, with a 1 below the valid bits. Each
code shifts the bits left, and the 1 moves up with them, so `__builtin_ctzll(bits)` is the number of
bits used since `ip`.

So a stream needs no counter. 8 streams with a counter are 24 values, more than the 16 registers of
x86-64 can hold next to the table and the output; with the sentinel the 8 `bits` stay in registers
and only a refill touches the pointers. zstd keeps its 4 Huffman streams the same way.

The comment's last sentence is why `bits` is never 0, for which `__builtin_ctzll()` is undefined: a
refill leaves at least 56 bits above the 1, and the codes until the next refill take fewer.
@more {#ch-literals-dec--sentinel} Stream 0 of the page of letters, in three states
[The page of letters](#ch-literals-enc--example) has {{letters.lits}} literals, 200 random letters and
a zero byte, coded with literal table {{ch13.set}}. Stream 0 starts at byte {{ch13.s0.start}} of the
compressed page with the bytes `{{ch13.s0.bytes}}`.
@assert letters.lits == 201 and ch13.last_lit_byte == 0 and ch13.r.valid_before + ch13.r.new_bits == ch13.r.valid_after

<figure class="diagram">
<div class="scroll">
<svg viewBox="0 0 720 250" font-size="12" role="img" aria-label="the 64 bits of stream 0 after a refill, after 5 codes, and after the next refill, with the sentinel 1 moving up and down">
  <text x="100" y="30">after the first refill: {{ch13.s0.bits0}}, ctz = 0</text>
  <rect x="100" y="38" width="{{ch13.fig.w0}}" height="28" fill="none" stroke="currentColor"/>
  <text x="{{ch13.fig.c0}}" y="57" text-anchor="middle">63 bits of the stream, from ip = byte {{ch13.s0.start}}</text>
  <g class="acc">
    <rect x="667" y="38" width="9" height="28" class="fill-acc" stroke="currentColor"/>
  </g>
  <text x="688" y="57">1</text>
  <text x="100" y="80">bit 63</text>
  <text x="676" y="80" text-anchor="end">bit 0</text>

  <text x="100" y="110">after 5 codes of {{ch13.c.bits}} bits: {{ch13.c.after}}, ctz = {{ch13.r.used}}</text>
  <rect x="100" y="118" width="{{ch13.fig.w1}}" height="28" fill="none" stroke="currentColor"/>
  <text x="{{ch13.fig.c1}}" y="137" text-anchor="middle">{{ch13.r.valid_before}} bits not used yet</text>
  <g class="acc">
    <rect x="{{ch13.fig.s1}}" y="118" width="9" height="28" class="fill-acc" stroke="currentColor"/>
  </g>
  <rect x="{{ch13.fig.z1}}" y="118" width="{{ch13.fig.wz1}}" height="28" class="fill-sheet" stroke="currentColor"/>
  <text x="{{ch13.fig.cz1}}" y="137" text-anchor="middle">{{ch13.r.used}} zeros shifted in</text>
  <text x="{{ch13.fig.l1}}" y="162" text-anchor="middle">bit {{ch13.r.used}}</text>

  <text x="100" y="192">next refill: ip += {{ch13.r.used}} &gt;&gt; 3 = {{ch13.r.bytes}}, shift by {{ch13.r.used}} &amp; 7 = {{ch13.r.shift}}: {{ch13.r.bits}}</text>
  <rect x="100" y="200" width="{{ch13.fig.w2}}" height="28" fill="none" stroke="currentColor"/>
  <text x="{{ch13.fig.c1}}" y="219" text-anchor="middle">the same {{ch13.r.valid_before}} bits, then {{ch13.r.new_bits}} new ones</text>
  <g class="acc">
    <rect x="{{ch13.fig.s2}}" y="200" width="9" height="28" class="fill-acc" stroke="currentColor"/>
  </g>
  <rect x="{{ch13.fig.z2}}" y="200" width="{{ch13.fig.wz2}}" height="28" class="fill-sheet" stroke="currentColor"/>
  <text x="{{ch13.fig.l2}}" y="244" text-anchor="middle">bit {{ch13.r.shift}}</text>
  <line x1="{{ch13.fig.s1}}" y1="200" x2="{{ch13.fig.s1}}" y2="228" stroke="currentColor" stroke-dasharray="2 2"/>
</svg>
</div>
<figcaption>Stream 0's 64 bits: the sentinel 1 (accent) marks how many bits were used since ip; a refill moves ip by the whole bytes and shifts by the rest.</figcaption>
</figure>

The second refill, number by number, is [under `lit_refill()`](#ch-literals-dec--refill).
@end
@more What the sentinel costs, and where the pointers go
`__builtin_ctzll(bits)` is one instruction on x86-64, two on arm64. It replaces the `count` of the
sequences' bit reader ([the bit reader](seqlz_decompress.c.html#b152)). With the sentinel the 8 `bits`
stay in registers, and the 8 pointers, which only a refill needs, go to the stack
([explored-designs.md](https://github.com/martinus/seqlz/blob/main/docs/explored-designs.md#seqlz-fast-lit-one-of-8-literal-tables-per-page)).
zstd reads its 4 streams from the end backwards; seqlz reads forwards, as it reads the sequences.
@end

@@ 23-36
The 8 bytes at `ip`, with zeros after the end of the compressed page: an 8-byte load there could read
past the page, which may be the end of a mapping. Only the last refills of a page come here.

`noinline` keeps the byte loop out of the copies of `lit_refill()` in the two unrolled loops over the
streams, and `__cold` has the compiler place it away from the loop and lay out its call as the
unlikely branch.
@more {#ch-literals-dec--lit_load_tail-the-last-bytes-out-of-the-loop} When the page of letters needs it
It runs only where the sequences' bitstream after the streams is short. On the page of letters it has
{{ch13.bitstream}} bytes, and stream 7, which starts at byte {{ch13.s7.start}} of {{ch13.n}}, has its
last refill {{ch13.s7.before_end}} bytes before the end, through `lit_load_tail()`.
@assert ch13.s7.tail == 1 and ch13.bitstream == letters.b.bytes and ch13.n == letters.coded.len
@end

@@ 37-53
A refill without a counter: the sentinel's position is the bits used, `ip` moves on by the whole
bytes of them, and `<< (used & 7U)` drops the bits of the new first byte that were used already. That
shift also puts the new sentinel, the `| 1U`, right below the valid bits.

At least 56 bits are valid after a refill, `63 - (used & 7)`, enough for 5 codes of at most 10 bits
([why 56](seqlz_internal.h.html#b30)), so the decoder refills once per 5 literals of a stream. `end` is
the end of the whole page, not of the stream: a refill may load bytes of the next stream, and the check
at the end catches a stream that used them.
@more {#ch-literals-dec--refill} The second refill of stream 0, bit by bit
A stream starts with `bits = 1`, so its first refill loads from its start without a shift. On
[the page of letters](#ch-literals-dec--sentinel), stream 0's first 8 bytes end in an even byte, so
`| 1` sets bit 0, the sentinel, and the bits are `{{ch13.s0.bits0}}`. 5 codes,
`{{ch13.c.0.lit}}{{ch13.c.1.lit}}{{ch13.c.2.lit}}{{ch13.c.3.lit}}{{ch13.c.4.lit}}`, take
{{ch13.c.bits}} bits, and the next refill is:
@assert ch13.s0.odd == 0 and ch13.k.rounds == 5 and ch13.k.streams == 8 and ch13.k.lit_bits == 10

```text
used   = ctz({{ch13.c.after}}) = {{ch13.r.used}}
p      = ip + {{ch13.r.used}} >> 3 = ip + {{ch13.r.bytes}}                 byte {{ch13.r.at}} of the page
load   = {{ch13.r.load_hex}} -> {{ch13.r.load}}
| 1    = {{ch13.r.or1}}
<< {{ch13.r.shift}}   = {{ch13.r.bits}}                    {{ch13.r.used}} & 7 = {{ch13.r.shift}}, the sentinel at bit {{ch13.r.shift}}
```

The top {{ch13.r.valid_before}} bits are the same before and after, and {{ch13.r.new_bits}} new ones
follow. The shift drops `{{ch13.r.dropped}}`, the first {{ch13.r.shift}} bits of byte {{ch13.r.at}},
which were the end of the code of `{{ch13.c.4.lit}}`.
@assert ch13.c.last_split == 1 and ch13.c.last_prev_byte + 1 == ch13.r.at
@end
@more Why `| 1U` loses no bit, and the one branch
`| 1U` overwrites the last bit of the 8th byte. That bit is not lost: the next refill moves at most 7
bytes on and loads that byte again. The only branch is `end - p >= 8`, which is the same for every
refill but the last few of a page.
@end

@@ 54-69
One literal: the top 10 bits index the table (1024 entries, every index valid,
[table decoding](book:#ch-compression--table-decoding)), the entry has the byte and the code's length.

`mask` is 63 or 0. With 63 the stream moves on by the code's length, and the mask tells the compiler
that the shift is below 64. With 0 the literal is decoded and written but the stream stays where it
is: "decode, but do not move on", without a branch. The last step of [the rest](#b372) needs that.
@anchor ch-literals-dec--decode

## Sizes and ends

@@ 70-94
The 8 stream sizes, `width` bits each, packed behind the header. Each size is read with a 4-byte load
at its first byte, shifted and masked.

The sizes are copied into `h` first: for the last size, the 4-byte load can reach past the sizes and
past the end of a short page. `h`'s bytes behind them are zeros.
@more {#ch-literals-dec--sizes} The 8 sizes of the page of letters, load by load
From `p` on, the header has `width` bytes with the 8 stream sizes, `width` bits each, lowest bit first
([the format](#ch-format--coded)). `p` is byte 3, or byte 4 when byte 3 is the delta's byte
([`read_lit_header()`](#b292) returns it). A 4-byte load always holds a field: it starts at most 7 bits
into its first byte and has at most 12 bits. The furthest load, for `k = 7` with `width = 12`, is at
byte `84 / 8 = 10` and reads bytes 10 to 13, past the 12 bytes of sizes. [The page of
letters](#ch-literals-dec--sentinel) has `width = {{ch13.width}}` and the size bytes
`{{ch13.size_hex}}`:

| `k` | bit `k * {{ch13.width}}` | byte | shift | `le32` at that byte | size |
| --- | --- | --- | --- | --- | --- |
| 0 | {{ch13.sz.0.bit}} | {{ch13.sz.0.byte}} | {{ch13.sz.0.shift}} | `{{ch13.sz.0.le}}` | `{{ch13.sz.0.low}} & {{ch13.mask}} = {{ch13.sz.0.size}}` |
| 7 | {{ch13.sz.7.bit}} | {{ch13.sz.7.byte}} | {{ch13.sz.7.shift}} | `{{ch13.sz.7.le}}` | `{{ch13.sz.7.le_short}} >> {{ch13.sz.7.shift}} = {{ch13.sz.7.size}}` |
@assert ch13.sz[0].shift == 0 and ch13.sz[7].past == 1

The zeros at the top of the last load are `h`'s, not bytes of the page. The streams start at
{{ch13.starts}}, {{ch13.total}} bytes in all.
@end

@@ 95-112
Each stream has to end exactly in its last byte, with zero bits after its last code. `left` is the
bits of the stream's size minus the bits used; it has to be 0 to 7, and those last bits 0.

This catches a stream that read into the next one (negative), a stream with a whole byte too many,
and set bits after the codes. zram stores no checksum, so this and the end of the sequences'
bitstream are what tell a damaged page from a valid one.
@more {#ch-literals-dec--ends} Stream 0's end, and three damaged pages
The format wants a stream's codes to end in its last byte, with the bits after the last code 0
([format.md](https://github.com/martinus/seqlz/blob/main/docs/format.md#coded-literals)). The bits
used are the bytes `ip` moved past, times 8, plus the sentinel's position; `left` is a `long` so that a
stream that used more than its size gives a negative number. The trailing bits are read from the page,
not from `b`: if the stream's last byte was the 8th of its last load, the sentinel sits where its lowest
bit was.

Stream 0 of [the page of letters](#ch-literals-dec--sentinel) has {{ch13.e.size}} bytes,
{{ch13.e.size_bits}} bits, and used {{ch13.e.used}}: `left = {{ch13.e.left}}`, and its last byte,
`{{ch13.e.last_val}}`, has `{{ch13.e.last_val}} & {{ch13.e.mask}} = {{ch13.e.masked}}`. Three damaged
versions of the page, each rejected by `seqlz_decompress()` with `-EINVAL` when the book is built:

| change | what `lit_stream_ends()` sees |
| --- | --- |
| byte {{ch13.e.last}} is `{{ch13.e.bad_val}}` | `{{ch13.e.bad_val}} & {{ch13.e.mask}} = {{ch13.e.bad_masked}}`: a bit after the codes is set |
| stream 0's size is {{ch13.e.size1}} | `left = {{ch13.e.size1_bits}} - {{ch13.e.used}} = {{ch13.e.left1}}`: the codes ran into the next stream |
| stream 0's size is {{ch13.e.size2}} | `left = {{ch13.e.size2_bits}} - {{ch13.e.used}} = {{ch13.e.left2}}`: a whole byte after the codes |
@assert ch13.e.masked == 0 and ch13.e.bad_masked != 0 and ch13.e.left1 < 0 and ch13.e.left2 > 7
@end
@more {#ch-literals-dec--exact} Why the check is exact, and what it cost
Until 5th October 2026 the last round decoded 5 literals in every stream, also behind its last literal,
and the check allowed for those bits: a stream one byte too short was decoded with the next stream's
bits and accepted. Now the codes must fit the size exactly, a rule the format can state without the
bytes that follow. On the Mi 9T's A55 it cost 1.1% of the decode cycles per page, 16 357 to 16 540, and
nothing on the A76 (commit `acfd49b`).
@end

## A page's own table

@@ 113-141
Writes the decode table from the buckets: for each code length, the bytes with that length in order
of their value, which is the order of their canonical codes. So writing length 1, then 2, and so on
puts each byte exactly at the entries its code indexes.

A code of `l` bits has `1024 >> l` entries, all the same, the ones that start with the code
([table decoding](book:#ch-compression--table-decoding)). The multiply by `0x0001000100010001` copies
the 2-byte entry into 4 lanes, and one 8-byte store writes 4 entries. That works down to 8 bits, 4
entries.

`own_fill()` was the end of `read_own_table()` until 24th October 2026, when it moved out to keep the
functions of the kernel copy short (commit `8993922`). It is above its caller, as C wants a static
function declared before its call.

@@ 142-155
Codes of 9 bits have 2 entries, one 4-byte store; codes of 10 bits one. A loop per length without a
branch per byte: one loop for all lengths with that branch read level 3 slower on the Mi 9T's
Cortex-A76.
@more A loop per length, measured
With one loop over all lengths and a branch on how many entries a byte has, level 3 read 1.5% slower
warm than the experiment before it, in userspace on the Mi 9T's Cortex-A76; with a loop per length,
2.7% faster, on 15th October 2026
([explored-designs.md](https://github.com/martinus/seqlz/blob/main/docs/explored-designs.md#level-3-a-lazy-matcher-and-a-literal-table-of-the-pages-own-zstd-3s-memory-in-less-than-half-its-time-on-the-phone-kept)).
@end
@more {#ch-literals-dec--writing-the-entries-length-by-length} The entries of the text page
The table of [the text page](#ch-format--own), as `own_fill()` writes it:

| byte | code | entries | entry |
| --- | --- | --- | --- |
| the space | `{{own.text.code.20}}` | {{own.dt.20.first}} to {{own.dt.20.last}} | `{{own.dt.20.entry}}` |
| `l` | `{{own.text.code.6c}}` | {{own.dt.6c.first}} to {{own.dt.6c.last}} | `{{own.dt.6c.entry}}` |
| `u` | `{{own.text.code.75}}` | {{own.dt.75.first}} to {{own.dt.75.last}} | `{{own.dt.75.entry}}` |
| `a` | `{{own.text.code.61}}` | {{own.dt.61.first}} to {{own.dt.61.last}} | `{{own.dt.61.entry}}` |
| ... | | | |
| `t` | `{{own.text.code.74}}` | {{own.dt.74.first}} to {{own.dt.74.last}} | `{{own.dt.74.entry}}` |
@assert own.dt["20"].first == 0 and own.dt["74"].last == 1023

The table is built for one page and not kept: 2 KiB of stores for every read of such a page, where a
page with a fixed table loads its 2 KiB table, which `read_lit_header()` prefetches
([the prefetch](#b292)).
@end

@@ 156-161
`OWN_STEP_MAX`: one step takes at most a run's 8-bit code and 8 more bits.

The table needs 2 KiB, and the buckets 3 KiB on the way. The decode table goes into the output page,
which nothing writes before all literals are decoded; the buckets go into the scratch, which the
literals overwrite once the table is built. The `static_assert` checks that both fit, so a page's own
table costs no extra work memory.
@more {#ch-literals-dec--own} Where the table and its buckets go, in bytes
With code 8 in byte 2 the page brings its own literal table, as a list of code lengths in the fixed
code `OWN` ([the format](#ch-format--own)). `read_own_table()` reads that list and writes the same
kind of decode table a fixed literal table has, 1024 entries of `code length | byte << 8`, so the rest
of `seqlz_decode_literals()` does not know the difference.

The buckets are one row of 256 bytes for each code length 1 to 10 and one row for "none". The
`static_assert` checks both places: the table, {{own.dt.bytes|grp}} bytes, and one more for a page at
an odd address, in a page, and the buckets, {{own.dt.bucket_bytes|grp}} bytes, in the scratch.
`OWN_STEP_MAX` is 16: a run's code of up to 8 bits and its up to 8 bits more.
@assert own.dec.step_max == 16 and own.dt.bytes == 2048 and own.dt.bucket_bytes == 3072
@end

@@ 162-198
The description of the table is a list of code lengths, one per byte value in order, with runs for
bytes that do not appear, in the fixed code `OWN` ([the format](book:#ch-format--own)).

The comment's third paragraph is the bound: the loop ends for any input, after at most 1024 + 257
steps. The last paragraph says why it is `noinline`: inlined, its locals made the stack frame of
`seqlz_decode_literals()` larger than the kernel allows with KASAN, which puts a red zone around each
local ([the limit](book:#ch-zram-glue--check)).
@anim owndesc The description of a page's own table, one symbol per step
@more {#ch-literals-dec--own-cost} How long building a table takes, measured
The 3657 own tables of one phone dump, built alone in userspace, 20 times each: the first version, a
lookup per symbol, the bytes sorted by length, then the fill, took 2543 to 2677 ns per table on the Mi
9T's Cortex-A76 and about 7 µs on the A55. A bucket per length instead of the sort took that to about
2.1 µs on the A76, two lengths per lookup and the checks after the loop to 1689 to 1740 ns, 36% less
than the first version; reading the description was about 60% of the first version's time. A lookup of
10 bits instead of 8 was 5% to 6% faster with its table warm, and not kept: 4 KiB of table are 64 more
cache lines for a cold read, which was not measured. Against a description by code length, level 3
reads 2.9% slower on the A76 and 1.0% on the A55, for pages 0.5% smaller
([explored-designs.md](https://github.com/martinus/seqlz/blob/main/docs/explored-designs.md#a-pages-own-literal-table-in-the-order-of-the-bytes-decoded-two-lengths-at-a-time-05-smaller-than-by-code-length-reads-29-slower-on-the-a76-kept)).
@end

@@ 199-213
The description is read through a 64-bit window `bits` whose top `avail` bits are the next ones,
refilled with 8 bytes, big-endian, when fewer than 16 are left, as the
[sequences' bit reader](seqlz_decompress.c.html#b152) does it.

`n` is the whole bytes that fit, 7 when `bits` is empty, and the mask clears what is below them.
`avail + 8 * n` is at most 63, so no shift is by 64. Near the end of the input `lit_load_tail()` reads
zeros, and `min(at, len)` keeps the pointer inside the page even when `at` has run past its end.
@anim ownread The loop, one lookup per step, on two real pages

@@ 214-232
`quad[top]` says what the next 8 bits hold ([quad](book:#ch-internal--own)). Bit 31 means a run that
does not fit an entry: then `decode[top]` gives the run's symbol and code length, and its `nb - 1`
bits follow the code. Two shifts again, because `nb - 1` can be 0.

A run only moves `b` (the next byte value) on, not `space`, so a description of nothing but runs would
never fill the code space. That is why the run checks `b > 256` right here: a valid table names at
most 256 bytes. Until 23rd October 2026 that check came only after the loop, and such input kept the
loop going for as long as the input lasted (commit `3ae2e09`).
@more A run of 32, and why two shifts
A run of 32 does not fit an entry of `quad[]`: it is symbol 15's 8-bit code and 5 bits more. Then
`decode[top]` has the symbol and the code's length, and the run's `nb - 1` bits are read from right
after the code: `bits <<= d >> 8` drops the code, and `(bits >> 1) >> (64 - nb)` is the top `nb - 1`
bits. Two shifts, because for a run of 1, `nb` is 1, and one shift by 64 would be undefined; `bits >>
1` has a 0 on top, so a shift by 63 gives the 0 that `read(0)` is. Each run adds at least 1 to `b`, so
the check `b > 256` ends a description of runs alone after at most 257 of them.
@end

@@ 233-247
The usual step: up to two lengths, and the runs before them, from one lookup. Byte `b` goes into the
bucket of its length.

A complete code may end after the first length, and the zero padding after it would read as more
lengths. `two` is whether room is left; without it, the second length is `OWN_QUAD_NONE`, which goes
into a row nobody reads, fills no code space, and does not move `b`. So one length or two take the same
instructions, and the loop has one branch per step besides the refill, the one for a long run.
`& 255U` keeps a bucket index inside its row for any input.
@more {#ch-literals-dec--the-loop-up-to-two-lengths-per-lookup} The text page's table, step by step
A step with lengths takes the first one, `l1`, and the run before it. `k`, the bits the step takes, is
the end of the first length or of the second. For `OWN_QUAD_NONE` the second byte goes into the row
nobody reads, its `full >> 11` is 0 code space, and `b` moves on by `l2 != OWN_QUAD_NONE`, 0. The table
of [the text page](#ch-format--own):

| step | `top` | `quad[top]` | what the step does | `b` | `space` |
| --- | --- | --- | --- | --- | --- |
| 0 | `{{own.dec.0.top}}` | `{{own.dec.0.e}}` | a run alone, of {{own.dec.0.run}}, {{own.dec.0.used}} bits | | {{own.dec.0.space}} |
| 1 | `{{own.dec.1.top}}` | `{{own.dec.1.e}}` | length {{own.dec.1.l1}} for byte {{own.dec.1.b1}}, the space, {{own.dec.1.used}} bits | {{own.dec.1.b1}} | {{own.dec.1.space}} |
| 2 | `{{own.dec.2.top}}` | `{{own.dec.2.e}}` | a run alone, of {{own.dec.2.run}}, {{own.dec.2.used}} bits | | {{own.dec.2.space}} |
| 3 | `{{own.dec.3.top}}` | `{{own.dec.3.e}}` | length {{own.dec.3.l1}} for byte {{own.dec.3.b1}}, `a`, {{own.dec.3.used}} bits | {{own.dec.3.b1}} | {{own.dec.3.space}} |
| 4 | `{{own.dec.4.top}}` | `{{own.dec.4.e}}` | a run alone, of {{own.dec.4.run}}, {{own.dec.4.used}} bits | | {{own.dec.4.space}} |
| ... | | | | | |
| 19 | `{{own.dec.19.top}}` | `{{own.dec.19.e}}` | length {{own.dec.19.l1}} for byte {{own.dec.19.b1}}, `u`, {{own.dec.19.used}} bits | {{own.dec.19.b1}} | {{own.dec.19.space}} |
@assert own.dec.steps == 20 and own.dec["0"].kind == "alone" and own.dec["1"].kind == "one" and own.dec["4"].run == 2 and own.dec["19"].space == 1024 and own.dec.bits == own.text.desc_bits

The text page is a poor case for `quad[]`: its lengths of 3 and 4 bits have codes of 7 bits, so no
entry has two of them, and its runs of 2 have one bit after a 5-bit code that, with the next length,
does not fit 8 bits either. The pages `OWN` was made for have lengths of 7 to 10 bits, with codes of 2
and 3 bits, where an entry such as `quad[0xd1]`, a run of 1 and the lengths 9 and 10, does three
symbols in one lookup ([field by field](#ch-internal--own)).

The animation under the note of [the window](#b199) runs these steps, the checks and `own_fill()` on
the text page, and as a choice on a page of base64 text, lines of 64 characters as a key in PEM form
has them, where most lookups take two lengths.
@end

@@ 248-255
All checks at the end, as nothing reads the table before: the code space exactly full, at most 256
bytes named, the bits within the input, and zero bits after the last symbol in its byte. Then
`own_fill()`, and the description's end is where the streams start.
@more {#ch-literals-dec--the-checks-at-the-end} Why the loop ends, and where the text page's table ends
Besides the runs' `b`, the loop checks nothing about the bytes, and it ends for any input: a step
with a length adds at least 1 to `space`, at most 1024 of them fill it, and past the end of the input
every code is `{{own.code.8}}`, a length of 9 bits, 2 per 8 zero bits. Then `used`, the bits read,
decides. The bits after the last symbol in its byte are the top `8 - used % 8` bits of `bits`. On the
text page `used` is {{own.dec.bits}}, so {{own.text.desc_pad}} bits of the last byte must be 0, and the
table ends after {{own.text.dbytes}} bytes. These are the rules of [the format](#ch-format--own),
which `fuzz/seqlz_diff_fuzz.c` checks against the reference decoder.
@end

## The header

@@ 256-270
Reads the literal header: byte 2 (the table, the width of the stream sizes, whether the literals are
deltas), the delta's byte, and the page's own table if it has one. Inlined into its one caller; it was
part of `seqlz_decode_literals()` until 24th October 2026 (commit `8993922`).

It returns where the sizes are. `bucket` is the scratch, `out` of the caller, where
`read_own_table()` puts its buckets.
@anchor ch-literals-dec--the-header-in-read_lit_header

@@ 271-291
A page of 2 bytes has no byte 2; then the width stays at its minimum, and the length check rejects
the page before anything reads byte 2. The length check counts the delta's byte, so all header bytes
are in the page before they are read.

Codes 9 to 15 are reserved and rejected: a decoder that accepted them now could not tell old pages
from new ones later. `*delta - 1U >= SEQLZ_DELTA_LAST` checks both ends in one compare, 0 wraps to a
large number, so only 1 to 3 pass.
@more The header's checks, in numbers
`deltas` is bit 7 of byte 2, 0 or 1, the number of bytes that say which delta, and 0 for a page of 2
bytes too. Without `+ deltas` in the length check, a page cut right before its last size byte would
have that byte read past its end. In `*delta - 1U >= SEQLZ_DELTA_LAST`, a byte of 0 wraps to
$2^{32} - 1$, and everything from 4 on is at least 3. Without deltas, `*delta` is 0 and the page's
byte 3 is the first byte of the sizes. `*q`, where the streams start when the page has no table of its
own, is behind byte 2, the delta's byte and the sizes: $3 + w + e$ in the terms of
[the format](#ch-format--coded).
@end

@@ 292-319
Code 8: the page's own table, built into the output page from its first even address, since the
entries are `u16` and a test decodes into a page at an odd address. The streams then start behind the
table's description. Before the parse, the two tables it looks each step up in, `own.quad` and
`own.decode`, are asked for as the fixed tables are below: each step waits for the lookup before, so on
a cold read their misses came one after the other. With the prefetch level 3's cold reads got 0.55 µs
faster on the Mi 9T's A55 on 31st October 2026
([explored-designs.md](https://github.com/martinus/seqlz/blob/main/docs/explored-designs.md#the-own-literal-tables-lookup-tables-prefetched-before-its-parse-level-3s-cold-reads-055-µs-faster-on-the-a55-kept)).

Codes 0 to 7: a fixed table. The `& (SEQLZ_LIT_SETS - 1U)` changes none of them and keeps the index
inside the array for any value. The prefetch goes out before the sizes are read, so its cache misses
overlap with that work.
@more How many cache lines the prefetch covers
The 2 KiB table never starts a cache line, so it covers {{ch13.lt.lines}} lines, the last one asked
for by the [prefetch of `end - 1`](#ch-dec-basics--end-line). The sizes are at byte $3 + e$, behind
the delta's byte if there is one, which is the pointer the function returns.
@assert ch13.lt.mod8 != 0 and ch13.lt.lines == 33 and ch13.lt.prefetches == 33 and ch13.lt.loop_prefetches == 32
@end

## seqlz_decode_literals()

@@ 320-339
The only function here that `decode_block()` calls. `noinline` on purpose: inlined into the loop over
the sequences, the 8 streams took its registers, and pages with raw literals read slower too.
`*delta` gets which delta level 3's literals are, which the loop over the sequences adds back
([the deltas](seqlz_decompress.c.html#b220)).

The streams are arrays; after unrolling, the indices are constants and the compiler keeps each
element in a register like a plain variable.
@anchor ch-literals-dec
@more {#ch-literals-dec--main} Why `noinline`, measured
The function is in another file than its one caller, `decode_block()`; `noinline` still matters for a
kernel built with link-time optimization, which can inline across files. Inlined, the 8 streams
competed for registers with the loop over the sequences, and made pages with raw literals slower too,
12% when the comment still had a number on 7th October 2026, with a setup that is not recorded. Until
10th October the function was also aligned to 64 bytes; measured again, that made less than 0.02 µs
of difference in the kernel VM, and it was removed (commit `bad6224`).
@end
@more The streams as arrays, measured
Until 10th October 2026 `ip[]`, `start[]`, `sz[]` and `b[]` were 8 variables each, `i0` to `i7` and
`b0` to `b7`. The arrays made the function 105 lines shorter and read coded pages 0.1 µs faster on the
A55, within the noise of code placement in the kernel VM
([explored-designs.md](https://github.com/martinus/seqlz/blob/main/docs/explored-designs.md#five-simplifications-measured-in-the-vm-and-on-the-phone-the-literal-streams-as-arrays-kept-four-not)).
@end

@@ 340-356
`n_lit` was checked by the caller already. The check again here tells the compiler the bound, and the
loops below take fewer instructions: moving it out, together with one more change, took 7% more to
decode `seqlz-2` pages ([measured](#ch-literals-dec--the-sizes-checked-against-the-input)).

After the check against `src_len`, every stream ends inside the input. `total` is at most
$8 \times 4095$ and `q - s` at most a page, so that check cannot wrap. Whether the bitstream after the
streams is long enough is the sequence loop's business. A stream starts with `bits = 1`, so its first
refill loads from its start without a shift.
@more {#ch-literals-dec--the-sizes-checked-against-the-input} The second check of `n_lit`, measured
`n_lit` is at most a page, so the literals fit the scratch. `decode_block()` checks that already, for
raw and coded literals alike ([its check](seqlz_decompress.c.html#b451)). Moving the check here out,
together with a check of the header's length before anything else, took 7% more instructions to
decode `seqlz-2` pages, 2000 pages of a desktop dump with gcc 16.2 on x86-64, 23rd October 2026
(commit `3ae2e09`).
@end

@@ 357-371
The main loop: per round, 8 refills, then 5 literals from each of the 8 streams. Stream `i` in step
`j` holds literal `k + 8 * j + i`, so the literals land in page order, no shuffle. With constant
bounds the compiler unrolls it into 8 refills and 40 lookups, 8 independent chains.
@anim streams The 8 streams of a real page, a round at a time, against one stream
@anchor ch-literals-dec--full-rounds-8-streams-5-literals-each
@more {#ch-literals-dec--cost} 8 streams, not 4, measured
When `seqlz-2` got its 8 literal tables on 24th September 2026, the streams went from 4 to 8.
With 4, a literal took about 2.9 cycles. With 8, the decode loop over 2000 pages in the cache went from
9070 to 8676 cycles per page (median of 5, userspace; `seqlz-1` took 7316), and cold reads in the
kernel from 2940 to 2860 ns at p50 and from 4810 to 4531 ns at p99
([explored-designs.md](https://github.com/martinus/seqlz/blob/main/docs/explored-designs.md#seqlz-fast-lit-one-of-8-literal-tables-per-page)).
Measured again on 5th October 2026 in the Mi 9T's own kernel, 4 streams saved 2.7 bytes per page; the
A76 read 0.26 µs slower, the A55 1.1 µs faster, both within the spread, and the PC's hot loop decoded
2.8% slower
([explored-designs.md](https://github.com/martinus/seqlz/blob/main/docs/explored-designs.md#3-4-literal-streams-instead-of-8-27-bytes-no-faster-not-kept)).
On 10th October in the kernel VM, 4 streams read 1.8% slower with gcc 16.2.1 and 0.7% with clang
22.1.8, for 2.0 bytes per page less
([explored-designs.md](https://github.com/martinus/seqlz/blob/main/docs/explored-designs.md#five-simplifications-measured-in-the-vm-and-on-the-phone-the-literal-streams-as-arrays-kept-four-not)).
A format change for 2 to 3 bytes and no time is not worth it. That the format has 8 streams and 8
literal tables is a coincidence.
@end

@@ 372-399
Fewer than 40 literals are left. Every stream does the same steps; in the last one `(0U - (r > i)) &
63U` is 63 for the `r` streams that still have a literal and 0 for the others. Those decode one too,
write it behind the literals into the scratch's 16 spare bytes, and do not move on.

So each stream ends right after its own codes, which `lit_stream_ends()` then checks exactly. A loop
over only the streams with a literal left would run 0 to 7 times, different on every page, and
mispredict.
@more {#ch-literals-dec--the-rest-the-same-steps-for-all-8-streams} The last literal of the page of letters
After one more refill, at most `steps + 1 <= 5` codes follow. [The page of
letters](#ch-literals-dec--sentinel) has {{letters.lits}} literals: rounds at `k = {{ch13.rounds}}`,
then `rest = {{ch13.rest}}`, `steps = {{ch13.steps}}`, `r = {{ch13.rest_r}}`. Stream 0 decodes literal
{{ch13.last_lit}}, the zero byte, with mask 63, and streams 1 to 7 decode whatever their next 10 bits
say into `out[{{ch13.extra_first}}]` to `out[{{ch13.extra_last}}]`.
@assert ch13.rest_r == 1 and ch13.steps == 0 and ch13.last_lit_byte == 0

The last step writes at most `out[n_lit + 7]`, {{ch13.max_write}} on 4 KiB pages, inside the scratch
of a page plus 16 bytes, {{const.SCRATCH}}. The 16 spare bytes are there for the 16-byte copies of the
sequence loop, which may read past the last literal; a valid page never takes them as literals
([the decoder](#ch-decode)).
@end
@more The same steps for all streams, measured
The same steps for all streams cost at most 8 lookups and no branch on the page. Measured on the Mi 9T
on 5th October 2026 (commit `8dc0341`), median of 7 runs of 20 000 pages: the A55 decoded a page in
16 620 cycles this way against 16 531 with a loop per stream, the A76 in 7420 against 7480, all within
their spread. So the form is kept for its reason and for the exact size check, not for a measured gain.
@end
