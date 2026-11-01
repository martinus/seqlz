# src/seqlz_compress_literals.c

The compressor's literals. After `seqlz_compress.c` has written a page with raw literals, level 2
prices every {{ch11.sample.step}}th of them in all 8 fixed literal tables, takes the cheapest, writes
the 8 streams into slots in the matcher's hash table, and copies them over the raw literals where that
saves enough. Level 3 can also store the literals as deltas to earlier bytes first, and can bring a
table of the page's own ([the format](book:#ch-format--coded)); where it may do either, it prices every
literal and writes the streams over the raw literals in place, from a copy in the matcher's hash table.
Either way the coded literals cost no work memory. The sequences' bitstream does not change; it only
moves down behind the coded literals. The decoder's side is
[`seqlz_decompress_literals.c`](seqlz_decompress_literals.c.html).

A reading order that follows the call:

1. The entry points: [`seqlz_code_literals()`](#b1008) at level 2, [`seqlz_code_own()`](#b993) at
   level 3. Both are [`code_literals()`](#b778), compiled for their arguments.
2. Level 2 prices [a sample](#b825) and writes [the streams into slots](#b859). Where a page may get
   deltas or a table of its own, [`lit_sums()`](#b281) prices every literal in all 8 tables with one add
   per literal, then [the cheapest table](#b889), and [a table of the page's own](#b912).
3. [The rule](#b935) decides whether coding pays, then [the streams are written](#b937) by
   [`lit_streams()`](#b320), and [the header](#b950).
4. Level 3's deltas come earlier, right after matching: [`seqlz_delta_literals()`](#b153), which
   prices them in [`pick_delta()`](#b88).

The four functions without `static`, `seqlz_delta_literals()` and the three entry points, are what
`seqlz_compress.c` calls.
@more {#ch-literals-enc--example} The page of letters, before and after
The page this file's panels take apart: {{ch11.letters}} letters, each drawn from `etaoinshrdlu` and a
space by the C library's classic linear congruential generator, `seed = seed * 1103515245 + 12345`
from `seed = 1`, taking `(seed >> 16) % 13`, then {{ch11.zeros}} zero bytes. The first 24 letters are
`{{ch11.first24}}`. The matcher finds {{letters.seqs}} sequences: {{ch11.n}} literals, the letters and
the first zero, and a match of {{ch11.ml}} bytes at offset {{ch11.off}} that copies the zero byte to
the end of the page, then the last sequence with no literals. At level 1 the page has
{{letters.len}} bytes, at level 2 {{letters.coded.len}}, and at level 3 the same: {{ch11.n}} literals
are too few for a table of their own.
@assert ch11.n == ch11.letters + 1 and ch11.off == 1

<figure class="diagram">
<div class="scroll">
<svg viewBox="0 0 720 280" font-size="12" role="img" aria-label="dst before and after code_literals() at level 2: the raw literals are coded into 8 slots in the matcher's table, the streams are copied over the raw literals' old place, and the bitstream moves down behind them">
  <text x="20" y="22">dst after compress_block(): {{letters.len}} bytes, raw literals</text>
  <rect x="20" y="30" width="60" height="30" class="fill-sheet" stroke="currentColor"/>
  <text x="50" y="50" text-anchor="middle">{{ch11.raw.u16}}</text>
  <rect x="80" y="30" width="440" height="30" fill="none" stroke="currentColor"/>
  <text x="300" y="50" text-anchor="middle">{{ch11.n}} raw literals</text>
  <g class="acc">
    <rect x="520" y="30" width="80" height="30" class="fill-acc" stroke="currentColor" fill-opacity="0.25"/>
    <text x="560" y="50" text-anchor="middle">bitstream</text>
  </g>
  <text x="20" y="74" text-anchor="middle">0</text>
  <text x="80" y="74" text-anchor="middle">2</text>
  <text x="520" y="74" text-anchor="middle">{{ch11.raw.bs}}</text>
  <text x="600" y="74" text-anchor="middle">{{letters.len}}</text>

  <line x1="300" y1="78" x2="300" y2="106" stroke="currentColor"/>
  <path d="M296 100 L300 108 L304 100 Z" fill="currentColor"/>
  <text x="308" y="96">lit_streams()</text>
  <text x="20" y="104">spare = st-&gt;literals</text>
  <rect x="170" y="110" width="270" height="30" class="fill-sheet" stroke="currentColor" stroke-dasharray="4 3"/>
  <text x="305" y="130" text-anchor="middle">8 slots, one stream each</text>

  <line x1="300" y1="140" x2="300" y2="196" stroke="currentColor"/>
  <path d="M296 190 L300 198 L304 190 Z" fill="currentColor"/>
  <text x="308" y="172">8-byte copies</text>

  <g class="acc">
    <line x1="560" y1="60" x2="490" y2="196" stroke="currentColor"/>
    <path d="M484 190 L489 199 L492 189 Z" fill="currentColor"/>
    <text x="536" y="130">memmove()</text>
  </g>

  <text x="20" y="194">dst after code_literals(): {{letters.coded.len}} bytes</text>
  <rect x="20" y="200" width="60" height="30" class="fill-sheet" stroke="currentColor"/>
  <text x="50" y="220" text-anchor="middle">{{ch11.u16}}</text>
  <rect x="80" y="200" width="30" height="30" class="fill-sheet" stroke="currentColor"/>
  <text x="95" y="220" text-anchor="middle">{{ch11.byte2}}</text>
  <rect x="110" y="200" width="60" height="30" class="fill-sheet" stroke="currentColor"/>
  <text x="140" y="220" text-anchor="middle">sizes</text>
  <rect x="170" y="200" width="35" height="30" fill="none" stroke="currentColor"/>
  <rect x="205" y="200" width="35" height="30" fill="none" stroke="currentColor"/>
  <rect x="240" y="200" width="35" height="30" fill="none" stroke="currentColor"/>
  <rect x="275" y="200" width="35" height="30" fill="none" stroke="currentColor"/>
  <rect x="310" y="200" width="35" height="30" fill="none" stroke="currentColor"/>
  <rect x="345" y="200" width="35" height="30" fill="none" stroke="currentColor"/>
  <rect x="380" y="200" width="35" height="30" fill="none" stroke="currentColor"/>
  <rect x="415" y="200" width="35" height="30" fill="none" stroke="currentColor"/>
  <text x="187" y="220" text-anchor="middle">s0</text>
  <text x="222" y="220" text-anchor="middle">s1</text>
  <text x="257" y="220" text-anchor="middle">s2</text>
  <text x="292" y="220" text-anchor="middle">s3</text>
  <text x="327" y="220" text-anchor="middle">s4</text>
  <text x="362" y="220" text-anchor="middle">s5</text>
  <text x="397" y="220" text-anchor="middle">s6</text>
  <text x="432" y="220" text-anchor="middle">s7</text>
  <g class="acc">
    <rect x="450" y="200" width="80" height="30" class="fill-acc" stroke="currentColor" fill-opacity="0.25"/>
    <text x="490" y="220" text-anchor="middle">bitstream</text>
  </g>
  <rect x="530" y="200" width="70" height="30" fill="none" stroke="currentColor" stroke-dasharray="2 3"/>
  <text x="565" y="220" text-anchor="middle">unused</text>
  <text x="20" y="244" text-anchor="middle">0</text>
  <text x="80" y="244" text-anchor="middle">2</text>
  <text x="110" y="244" text-anchor="middle">3</text>
  <text x="170" y="244" text-anchor="middle">{{ch11.q.0}}</text>
  <text x="205" y="244" text-anchor="middle">{{ch11.q.1}}</text>
  <text x="240" y="244" text-anchor="middle">{{ch11.q.2}}</text>
  <text x="275" y="244" text-anchor="middle">{{ch11.q.3}}</text>
  <text x="310" y="244" text-anchor="middle">{{ch11.q.4}}</text>
  <text x="345" y="244" text-anchor="middle">{{ch11.q.5}}</text>
  <text x="380" y="244" text-anchor="middle">{{ch11.q.6}}</text>
  <text x="415" y="244" text-anchor="middle">{{ch11.q.7}}</text>
  <text x="450" y="244" text-anchor="middle">{{ch11.q.8}}</text>
  <text x="530" y="244" text-anchor="middle">{{letters.coded.len}}</text>
  <text x="170" y="262" text-anchor="middle">q[0]</text>
  <text x="450" y="262" text-anchor="middle">q[8]</text>
</svg>
</div>
<figcaption>One page in dst before and after code_literals() at level 2, not to scale: the 8 streams are written into slots, copied over the raw literals' old place, and the bitstream moves down.</figcaption>
</figure>

The offsets under the second row are `q[0]` to `q[8]` for this page, from `d`. Bytes
{{letters.coded.len}} to {{ch11.raw.last}} keep what they held, but are no longer part of the page.
The panels of the blocks follow it step by step: [the sums of stream 0](#ch-literals-enc--pricing),
[the width](#ch-literals-enc--width), [stream 0's codes](#ch-literals-enc--lit_put-one-code-into-an-accumulator),
[its first flush](#ch-literals-enc--lit_flush-whole-bytes-to-memory-big-endian),
[the tail](#ch-literals-enc--the-tail-at-most-4-literals-per-stream-one-switch),
[the sizes](#ch-literals-enc--sizes) and [the header](#ch-literals-enc--header).
@end
@more {#ch-literals-enc--bytes} The page of letters, byte by byte
The {{letters.coded.len}} bytes of [the page of letters](#ch-literals-enc--example) at level 2:

```text
{{ch11.dump.0}}
{{ch11.dump.1}}
{{ch11.dump.2}}
{{ch11.dump.3}}
{{ch11.dump.4}}
{{ch11.dump.5}}
{{ch11.dump.6}}
{{ch11.dump.7}}
{{ch11.dump.8}}
{{ch11.dump.9}}
```
@assert ch11.dump_lines == 10

- `{{ch11.u16}}` is `{{ch11.u16hex}}`: coded literals, `{{ch11.u16hex}} & 0x7fff = {{ch11.n}}` of
  them.
- `{{ch11.byte2}}` is code `0x{{ch11.byte2}} & 15 = {{ch11.set}}`, table {{ch11.set}}, and width
  `5 + (0x{{ch11.byte2}} >> 4 & 7) = {{ch11.width}}`. Table {{ch11.set}} is one of the two tables for
  text: `e` {{lit.0.len.101}} bits, the space {{lit.0.len.32}}.
@assert ch11.set == 0 and lit[3].len[0x65] == 4 and lit[0].len[0x65] == 4 and letters.l3.len == letters.coded.len and ch11.n < own.enc.min_lits
- `{{ch11.sizes.hex}}` are the 8 sizes, {{ch11.sizes_comma}}, {{ch11.coded}} bytes in all. The
  streams start at byte {{ch11.q.0}} and end at {{ch11.q.8}}, `{{ch11.q8hex}}`.
- Stream 0 begins `{{ch11.s0.head4}}`, `{{ch11.s0.head4bin}}`, which table {{ch11.set}}'s codes cut
  into {{ch11.s0.codes5}}: literals 0, 8, 16, 24 and 32. It has {{ch11.s0.bits}} bits, so its last
  byte, `{{ch11.s0.lasthex}}`, holds `{{ch11.s0.lastbits}}` and {{ch11.s0.zeros}} zero bits.
- `{{letters.b.hex}}` from byte {{ch11.q.8}} on is the sequences' bitstream, the same {{ch11.bs}}
  bytes that followed the raw literals.

The pricing, on every {{ch11.sample.step}}th literal, {{ch11.sample.n}} of the {{ch11.n}}, one sum per
table ([the sample](#b825)):

| table | bits of the sample | times {{ch11.sample.step}}, in bytes |
| --- | --- | --- |
| {{ch11.rank.0.table}} | {{ch11.rank.0.bits}} | {{ch11.rank.0.est}} |
| {{ch11.rank.1.table}} | {{ch11.rank.1.bits}} | {{ch11.rank.1.est}} |
| {{ch11.rank.2.table}} | {{ch11.rank.2.bits}} | {{ch11.rank.2.est}} |
| others | {{ch11.others.min}} to {{ch11.others.max}} | |

Tables {{ch11.rank.0.table}} and {{ch11.rank.1.table}} tie, and the tie keeps the lower number. Priced
on every literal, as level 3 prices a page that may get a table of its own, table {{ch11.full.set}}
would have won, {{ch11.full.bits}} bits against {{ch11.full.set_bits}}: that is what the sample costs
here. With the margin, `{{ch11.rank.0.est}} + {{ch11.coded_min}} = {{ch11.sample.lhs}}` is below
`{{ch11.n}} - {{ch11.n}} / 16 + {{ch11.sample.margin}} = {{ch11.sample.rhs}}`, so the streams are
written. On their real sizes, `{{ch11.coded}} + {{ch11.coded_min}} = {{ch11.rule.lhs}}` is less than
`{{ch11.n}} - {{ch11.n}} / 16 = {{ch11.rule.rhs}}`, by {{ch11.rule.gap}} bytes ([the rule](#b935)). It
saves {{ch11.saves}} bytes, {{ch11.saves_pct}}% of the {{letters.len}} bytes with raw literals.
@assert F("ch11.rank.0.bits") == F("ch11.rank.1.bits") and F("ch11.rank.0.table") == ch11.set and ch11.full.set != ch11.set and ch11.full.bits < ch11.full.set_bits and ch11.sample.lhs < ch11.sample.rhs and ch11.rule.lhs + ch11.rule.gap == ch11.rule.rhs

::: try Compress a page of your own
In a scratch directory, with the repository's path in `$R` (`R=/path/to/seqlz`):

```c
#include <stdio.h>
#include "seqlz.h"

static struct seqlz_state st;
static u8 page[SEQLZ_BLOCK_SIZE], dst[2 * SEQLZ_BLOCK_SIZE];

int main(void)
{
	unsigned int k, len, seed = 1;

	for (k = 0; k < 200; k++) {
		seed = seed * 1103515245u + 12345u;
		page[k] = "etaoinshrdlu "[(seed >> 16) % 13];
	}
	len = seqlz_compress_block(&st, page, dst, sizeof(dst), 2);
	for (k = 0; k < len; k++)
		printf("%02x%c", dst[k], k % 16 == 15 ? '\n' : ' ');
	printf("\n%u bytes\n", len);
	return 0;
}
```
@program ch11-letters

Build it with `gcc -O2 -I$R/src ex.c $R/src/seqlz_*.c seqlz_tables.c`, the tables from `$R/tools/gen-tables/gen-tables.sh seqlz_tables.c`.
It prints the page above, 16 bytes to a line, and ends with:

@output ch11-letters 10-11

With {{ch11.lraw}} letters instead of 200 the literals stay raw, with one more they are coded.
@assert ch11.lraw < 200 and ch11.lraw_coded == 1
:::
@end
@more {#ch-literals-enc--cost} What coding the literals costs, measured
The pricing is cheap, writing the streams is not, and a page that keeps its raw literals has paid for
the pricing too. On the Cortex-A55, `code_literals()` took about 5400 cycles, 3 µs, per page in the
hot loop of the userspace benchmark in October 2026: `seqlz-2` compressed a page in 47 640 cycles,
`seqlz-1` in 42 241. Most pages paid part of that even when they stayed raw: in that sample 14 582 of
19 387 pages kept their raw literals, about 404 each, all priced in all 8 tables first. Pricing only
every 8th literal first, to send hopeless pages back early, made compression slower, because the
literals of most raw pages come close to paying, and the coded ones still needed the pass over every
literal for their sizes
([item 8 of "Six choices"](https://github.com/martinus/seqlz/blob/main/docs/explored-designs.md#8-coded-literals-on-the-a55-78-instructions-per-literal-at-one-per-cycle)).
Since 31st October 2026 level 2 has no such pass: the table comes from every
{{ch11.sample.step}}th literal and the sizes from the streams themselves, written into slots; measured
with every 5th literal, level 2 wrote a page 1.4% to 1.8% faster on the Mi 9T for 0.5 bytes per page
([the sample](#ch-literals-enc--sample)).
This is where `seqlz-1`, which never calls `code_literals()`, gains on the phone: in an earlier
measurement on the Mi 9T's little core, `seqlz-2` stored 55 bytes per page less and took
4.2 µs more per page written, with 0.34 reads per write
([explored-designs.md](https://github.com/martinus/seqlz/blob/main/docs/explored-designs.md#a-second-phone-dump-after-12-hours-of-use-seqlz-fast-reads-faster-than-lzo-rle-at-p99)).
Which of the two a phone should use depends on how it values a byte against a µs, and that is not
settled.

Level 3 adds a third choice. On 15th October 2026, in the Mi 9T's own kernel on the Cortex-A76 and
19 775 pages of a phone session, it stored a page in 952.0 bytes of zsmalloc memory, against 997.0 at
level 2 and 951.4 for `zstd` 3, and wrote one in about 16 µs, against 12 µs at level 2 and 36 µs for
`zstd` 3
([explored-designs.md](https://github.com/martinus/seqlz/blob/main/docs/explored-designs.md#level-3-a-lazy-matcher-and-a-literal-table-of-the-pages-own-zstd-3s-memory-in-less-than-half-its-time-on-the-phone-kept)).
Those 4 µs are the lazy step, the extension backwards and the table together. The deltas, on 21st
October 2026, in userspace on the Mi 9T with the kernel's compile flags, added about 4 µs per page
written at level 3 on the A76 and 8 µs on the A55, for 4.6% fewer bytes on ten samples of 20 000
pages, 873.6 instead of 915.6 per page: the XOR before them cost 3.6 µs and 7.4 µs, the subtraction
0.1 to 0.3 µs more on the A76
([explored-designs.md](https://github.com/martinus/seqlz/blob/main/docs/explored-designs.md#level-3-stores-its-literals-as-deltas-not-xors-084-fewer-bytes-than-the-xor-kept)).
On 22nd October 2026 one rule for all three deltas, every literal at least `x` bytes into the page
([the deltas](#ch-format--deltas)), took that to 869.7 bytes per page, 5.0% fewer than without
deltas, and writes about 0.2 µs faster on the Mi 9T, reads within the noise
([explored-designs.md](https://github.com/martinus/seqlz/blob/main/docs/explored-designs.md#one-rule-for-which-literals-are-deltas-045-fewer-bytes-a-simpler-spec-kept)).
@end

## Deltas, level 3

@@ 1-16
Three ways to store the literals: raw, coded with a fixed table, or coded with a table of the page's
own; at level 3 optionally as deltas first.

@@ 17-27
The deltas are priced on every {{delta.step}}th literal only, a prime, so that it is not the same byte
of each field of 2, 3, 4 or 8 bytes. Every 11th instead of every 5th made pages 0.08% larger on ten
samples of 20 000 pages, and level 3's writes on the Mi 9T 2.5% faster on the A55 and 3.1% on the A76,
on 31st October 2026 ([explored-designs.md](https://github.com/martinus/seqlz/blob/main/docs/explored-designs.md#the-deltas-priced-on-every-11th-literal-instead-of-every-5th-008-more-bytes-level-3-writes-25-and-31-faster-on-the-mi-9t-kept)).
Every 4th had made pages 1.0 byte larger, because in fields of 4 or 8 bytes it sees the same byte of
each field every time, and every literal almost no smaller, with the XOR that came before the deltas,
on 21st October 2026
([explored-designs.md](https://github.com/martinus/seqlz/blob/main/docs/explored-designs.md#level-3-xors-its-literals-with-an-earlier-byte-of-the-page-38-fewer-bytes-than-seqlz-3-of-15-october-kept)).
`DELTA_HDR_BITS` is the delta's byte in the header, 8 bits scaled as the sums are, $8 / 11$, which is 0
in integer division, so the macro takes {{delta.penalty}}.

@@ 28-39
Reads one sum out of its 16-bit lane: `even` has tables 0, 2, 4 and 6, `odd` tables 1, 3, 5 and 7,
4 lanes each. `k / 2` is the lane.
@more {#ch-literals-enc--lit-bits} Table 5's sum of stream 0, read out
`even` and `odd` have one word per sum `j`: a stream in `code_literals()`, a delta in
`seqlz_delta_literals()`. Table 5 is odd, so its sum is in `odd`, in lane `5 / 2 = 2`, bits 32 to 47.
For stream 0 of [the 4 literals `e`, space, `e` and a zero](#ch-literals-enc--pricing), `odd[0]`
shifted right by 32 is `{{ch11.odd32}}`, and `& 0xffff` gives `0x{{ch11.sum.5.hex}} = {{ch11.sum.5.dec}}`,
table 5's sum.
@end

@@ 40-63
The sums are counted in 8-bit lanes, 8 tables in one `u64`, and moved into 16-bit lanes before an 8-bit
lane can overflow. `x & lanes` keeps the even bytes in place, `x >> 8 & lanes` moves the odd ones down
by a byte.

`delta_spill()` does that for one sum per choice instead of one per stream: its words are indexed by
the delta, 0 for none and 1 to 3 for `SEQLZ_DELTA_4`, `SEQLZ_DELTA_8` and `SEQLZ_DELTA_LAST`, so
`lit_bits()` reads them as it reads a stream's.

@@ 64-74
8 byte subtractions in one word, without a borrow from one byte into the next. Setting bit 7 of every
byte of `a` and clearing it in `b` stops every borrow at bit 7 of its own byte; the XOR then puts the
right bit 7 back. The decoder's `add8()` is the same trick for an addition.
@more {#ch-literals-enc--sub-bytes} Why bit 7 comes out right, on `00 01` minus `10 00`
The deltas are written 8 at a time, and a plain subtraction of two `u64` is not 8 subtractions of
bytes: where a byte of the first word is smaller than the byte of the second, the subtraction borrows
1 from the byte above, and that byte comes out 1 too small. The format wants each byte on its own,
the same as a byte loop writes.

`a | high` sets the top bit of every byte of `a`, and `b & ~high` clears it in every byte of `b`. Now
each byte of the first word is at least `0x80` and each byte of the second at most `0x7f`, so no byte
can borrow from the one above, and the low 7 bits of each byte of the difference are those of the
byte's own difference: a borrow out of bit 6 only ever reaches bit 7 of the same byte. Bit 7 is wrong,
though. In a subtraction bit 7 of the result is bit 7 of `a`, XOR bit 7 of `b`, XOR the borrow out of
bit 6; the code used 1 and 0 in place of `a`'s and `b`'s bit, so it has 1 XOR the borrow. XOR with 1,
`a`'s bit and `b`'s bit fixes that, and `(a ^ ~b) & high` is exactly those three in bit 7 of each
byte: `a ^ ~b` is `a ^ b ^ 1` bit by bit. Two words whose low byte borrows, the bytes `00 01` minus
`10 00`:

```text
a                     {{delta.sub.a}}
b                     {{delta.sub.b}}
a | high              {{delta.sub.a_hi}}
b & ~high             {{delta.sub.b_lo}}
difference            {{delta.sub.diff}}
(a ^ ~b) & high       {{delta.sub.fix}}
sub_bytes(a, b)       {{delta.sub.r}}      bytes f0 01: 0x00 - 0x10 = 0xf0, 0x01 - 0x00 = 0x01
a - b                 {{delta.sub.plain}}      bytes f0 00: the borrow took byte 1's 1
```

@assert delta.sub.r == "0x00000000000001f0" and delta.sub.plain == "0x00000000000000f0"

Compiled for arm64 by clang with `-O2`, `sub_bytes()` is 7 instructions where the XOR was 1, the
masks as immediates of `orr`, `and` and `eor`. The decoder's `add4()` and `add8()` are the same trick
for an addition ([the decoder's deltas](#ch-literals-dec--undelta)).
@end
@more {#ch-literals-enc--sub-bytes-word} A word of the page of counters, byte by byte
The figure shows all 8 bytes of a word lane by lane, on a word that `seqlz_delta_literals()`
subtracts with `SEQLZ_DELTA_4` on [the page of counters](#ch-literals-dec--counters): the 8 bytes at byte
{{delta.lane.at}} minus the 8 bytes {{delta.lane.dist}} before.
Only byte 0 borrows. The borrow clears bit 7 of byte 0 of the difference, which `a | high` had set,
and stops there; in the other bytes bit 7 stays 1, and `(a ^ ~b) & high` turns it into their true 0.
The plain `a - b` takes the borrow from byte 1.
@assert delta.lane.borrow == "0" and delta.lane.wrong == "1" and delta.lane.ahigh == "" and delta.lane.bhigh == "0" and delta.lane.diff[0] < "80" and sorted(delta.lane.diff.values())[1] >= "80" and delta.lane.fix[0] == "00" and sorted(delta.lane.fix.values()) == ["00"] + ["80"] * 7 and max(delta.lane.r.values()) < "80"

<figure class="diagram">
<div class="scroll">
<svg viewBox="0 0 720 310" font-size="12" role="img" aria-label="sub_bytes() on a word of the page of counters, byte by byte: the 8 bytes of a and b, a with bit 7 set and b with it cleared, their difference, the fix of bit 7, the result, and a plain subtraction, where the borrow of byte 0 makes byte 1 wrong">
  <text x="675" y="18" text-anchor="middle">byte 0</text>
  <text x="613" y="18" text-anchor="middle">byte 1</text>
  <text x="551" y="18" text-anchor="middle">byte 2</text>
  <text x="489" y="18" text-anchor="middle">byte 3</text>
  <text x="427" y="18" text-anchor="middle">byte 4</text>
  <text x="365" y="18" text-anchor="middle">byte 5</text>
  <text x="303" y="18" text-anchor="middle">byte 6</text>
  <text x="241" y="18" text-anchor="middle">byte 7</text>
  <line x1="20" y1="88" x2="710" y2="88" stroke="currentColor" stroke-opacity="0.35"/>
  <line x1="20" y1="154" x2="710" y2="154" stroke="currentColor" stroke-opacity="0.35"/>
  <line x1="20" y1="218" x2="710" y2="218" stroke="currentColor"/>
  <text x="202" y="45" text-anchor="end">a</text>
  <rect x="648" y="30" width="54" height="22" rx="3" class="fill-sheet" stroke="currentColor"/><text x="675" y="45" text-anchor="middle">{{delta.lane.a.0}}</text>
  <rect x="586" y="30" width="54" height="22" rx="3" class="fill-sheet" stroke="currentColor"/><text x="613" y="45" text-anchor="middle">{{delta.lane.a.1}}</text>
  <rect x="524" y="30" width="54" height="22" rx="3" class="fill-sheet" stroke="currentColor"/><text x="551" y="45" text-anchor="middle">{{delta.lane.a.2}}</text>
  <rect x="462" y="30" width="54" height="22" rx="3" class="fill-sheet" stroke="currentColor"/><text x="489" y="45" text-anchor="middle">{{delta.lane.a.3}}</text>
  <rect x="400" y="30" width="54" height="22" rx="3" class="fill-sheet" stroke="currentColor"/><text x="427" y="45" text-anchor="middle">{{delta.lane.a.4}}</text>
  <rect x="338" y="30" width="54" height="22" rx="3" class="fill-sheet" stroke="currentColor"/><text x="365" y="45" text-anchor="middle">{{delta.lane.a.5}}</text>
  <rect x="276" y="30" width="54" height="22" rx="3" class="fill-sheet" stroke="currentColor"/><text x="303" y="45" text-anchor="middle">{{delta.lane.a.6}}</text>
  <rect x="214" y="30" width="54" height="22" rx="3" class="fill-sheet" stroke="currentColor"/><text x="241" y="45" text-anchor="middle">{{delta.lane.a.7}}</text>
  <text x="202" y="73" text-anchor="end">b</text>
  <rect x="648" y="58" width="54" height="22" rx="3" class="fill-sheet" stroke="currentColor"/><text x="675" y="73" text-anchor="middle">{{delta.lane.b.0}}</text>
  <rect x="586" y="58" width="54" height="22" rx="3" class="fill-sheet" stroke="currentColor"/><text x="613" y="73" text-anchor="middle">{{delta.lane.b.1}}</text>
  <rect x="524" y="58" width="54" height="22" rx="3" class="fill-sheet" stroke="currentColor"/><text x="551" y="73" text-anchor="middle">{{delta.lane.b.2}}</text>
  <rect x="462" y="58" width="54" height="22" rx="3" class="fill-sheet" stroke="currentColor"/><text x="489" y="73" text-anchor="middle">{{delta.lane.b.3}}</text>
  <rect x="400" y="58" width="54" height="22" rx="3" class="fill-sheet" stroke="currentColor"/><text x="427" y="73" text-anchor="middle">{{delta.lane.b.4}}</text>
  <rect x="338" y="58" width="54" height="22" rx="3" class="fill-sheet" stroke="currentColor"/><text x="365" y="73" text-anchor="middle">{{delta.lane.b.5}}</text>
  <rect x="276" y="58" width="54" height="22" rx="3" class="fill-sheet" stroke="currentColor"/><text x="303" y="73" text-anchor="middle">{{delta.lane.b.6}}</text>
  <rect x="214" y="58" width="54" height="22" rx="3" class="fill-sheet" stroke="currentColor"/><text x="241" y="73" text-anchor="middle">{{delta.lane.b.7}}</text>
  <text x="202" y="111" text-anchor="end">a | high</text>
  <rect x="648" y="96" width="54" height="22" rx="3" class="fill-sheet" stroke="currentColor"/><text x="675" y="111" text-anchor="middle">{{delta.lane.ahi.0}}</text>
  <rect x="586" y="96" width="54" height="22" rx="3" class="fill-sheet" stroke="currentColor"/><text x="613" y="111" text-anchor="middle">{{delta.lane.ahi.1}}</text>
  <rect x="524" y="96" width="54" height="22" rx="3" class="fill-sheet" stroke="currentColor"/><text x="551" y="111" text-anchor="middle">{{delta.lane.ahi.2}}</text>
  <rect x="462" y="96" width="54" height="22" rx="3" class="fill-sheet" stroke="currentColor"/><text x="489" y="111" text-anchor="middle">{{delta.lane.ahi.3}}</text>
  <rect x="400" y="96" width="54" height="22" rx="3" class="fill-sheet" stroke="currentColor"/><text x="427" y="111" text-anchor="middle">{{delta.lane.ahi.4}}</text>
  <rect x="338" y="96" width="54" height="22" rx="3" class="fill-sheet" stroke="currentColor"/><text x="365" y="111" text-anchor="middle">{{delta.lane.ahi.5}}</text>
  <rect x="276" y="96" width="54" height="22" rx="3" class="fill-sheet" stroke="currentColor"/><text x="303" y="111" text-anchor="middle">{{delta.lane.ahi.6}}</text>
  <rect x="214" y="96" width="54" height="22" rx="3" class="fill-sheet" stroke="currentColor"/><text x="241" y="111" text-anchor="middle">{{delta.lane.ahi.7}}</text>
  <text x="202" y="139" text-anchor="end">b &amp; ~high</text>
  <rect x="648" y="124" width="54" height="22" rx="3" class="fill-sheet" stroke="currentColor"/><text x="675" y="139" text-anchor="middle">{{delta.lane.blo.0}}</text>
  <rect x="586" y="124" width="54" height="22" rx="3" class="fill-sheet" stroke="currentColor"/><text x="613" y="139" text-anchor="middle">{{delta.lane.blo.1}}</text>
  <rect x="524" y="124" width="54" height="22" rx="3" class="fill-sheet" stroke="currentColor"/><text x="551" y="139" text-anchor="middle">{{delta.lane.blo.2}}</text>
  <rect x="462" y="124" width="54" height="22" rx="3" class="fill-sheet" stroke="currentColor"/><text x="489" y="139" text-anchor="middle">{{delta.lane.blo.3}}</text>
  <rect x="400" y="124" width="54" height="22" rx="3" class="fill-sheet" stroke="currentColor"/><text x="427" y="139" text-anchor="middle">{{delta.lane.blo.4}}</text>
  <rect x="338" y="124" width="54" height="22" rx="3" class="fill-sheet" stroke="currentColor"/><text x="365" y="139" text-anchor="middle">{{delta.lane.blo.5}}</text>
  <rect x="276" y="124" width="54" height="22" rx="3" class="fill-sheet" stroke="currentColor"/><text x="303" y="139" text-anchor="middle">{{delta.lane.blo.6}}</text>
  <rect x="214" y="124" width="54" height="22" rx="3" class="fill-sheet" stroke="currentColor"/><text x="241" y="139" text-anchor="middle">{{delta.lane.blo.7}}</text>
  <text x="202" y="177" text-anchor="end">(a | high) - (b &amp; ~high)</text>
  <g class="acc"><rect x="648" y="162" width="54" height="22" rx="3" class="fill-sheet" stroke="currentColor" stroke-width="2"/><text x="675" y="177" text-anchor="middle">{{delta.lane.diff.0}}</text></g>
  <rect x="586" y="162" width="54" height="22" rx="3" class="fill-sheet" stroke="currentColor"/><text x="613" y="177" text-anchor="middle">{{delta.lane.diff.1}}</text>
  <rect x="524" y="162" width="54" height="22" rx="3" class="fill-sheet" stroke="currentColor"/><text x="551" y="177" text-anchor="middle">{{delta.lane.diff.2}}</text>
  <rect x="462" y="162" width="54" height="22" rx="3" class="fill-sheet" stroke="currentColor"/><text x="489" y="177" text-anchor="middle">{{delta.lane.diff.3}}</text>
  <rect x="400" y="162" width="54" height="22" rx="3" class="fill-sheet" stroke="currentColor"/><text x="427" y="177" text-anchor="middle">{{delta.lane.diff.4}}</text>
  <rect x="338" y="162" width="54" height="22" rx="3" class="fill-sheet" stroke="currentColor"/><text x="365" y="177" text-anchor="middle">{{delta.lane.diff.5}}</text>
  <rect x="276" y="162" width="54" height="22" rx="3" class="fill-sheet" stroke="currentColor"/><text x="303" y="177" text-anchor="middle">{{delta.lane.diff.6}}</text>
  <rect x="214" y="162" width="54" height="22" rx="3" class="fill-sheet" stroke="currentColor"/><text x="241" y="177" text-anchor="middle">{{delta.lane.diff.7}}</text>
  <text x="202" y="205" text-anchor="end">(a ^ ~b) &amp; high</text>
  <rect x="648" y="190" width="54" height="22" rx="3" class="fill-sheet" stroke="currentColor"/><text x="675" y="205" text-anchor="middle">{{delta.lane.fix.0}}</text>
  <rect x="586" y="190" width="54" height="22" rx="3" class="fill-sheet" stroke="currentColor"/><text x="613" y="205" text-anchor="middle">{{delta.lane.fix.1}}</text>
  <rect x="524" y="190" width="54" height="22" rx="3" class="fill-sheet" stroke="currentColor"/><text x="551" y="205" text-anchor="middle">{{delta.lane.fix.2}}</text>
  <rect x="462" y="190" width="54" height="22" rx="3" class="fill-sheet" stroke="currentColor"/><text x="489" y="205" text-anchor="middle">{{delta.lane.fix.3}}</text>
  <rect x="400" y="190" width="54" height="22" rx="3" class="fill-sheet" stroke="currentColor"/><text x="427" y="205" text-anchor="middle">{{delta.lane.fix.4}}</text>
  <rect x="338" y="190" width="54" height="22" rx="3" class="fill-sheet" stroke="currentColor"/><text x="365" y="205" text-anchor="middle">{{delta.lane.fix.5}}</text>
  <rect x="276" y="190" width="54" height="22" rx="3" class="fill-sheet" stroke="currentColor"/><text x="303" y="205" text-anchor="middle">{{delta.lane.fix.6}}</text>
  <rect x="214" y="190" width="54" height="22" rx="3" class="fill-sheet" stroke="currentColor"/><text x="241" y="205" text-anchor="middle">{{delta.lane.fix.7}}</text>
  <text x="202" y="241" text-anchor="end">sub_bytes(a, b)</text>
  <g class="acc"><rect x="648" y="226" width="54" height="22" rx="3" class="fill-sheet" stroke="currentColor" stroke-width="1.5"/><text x="675" y="241" text-anchor="middle">{{delta.lane.r.0}}</text></g>
  <g class="acc"><rect x="586" y="226" width="54" height="22" rx="3" class="fill-sheet" stroke="currentColor" stroke-width="1.5"/><text x="613" y="241" text-anchor="middle">{{delta.lane.r.1}}</text></g>
  <g class="acc"><rect x="524" y="226" width="54" height="22" rx="3" class="fill-sheet" stroke="currentColor" stroke-width="1.5"/><text x="551" y="241" text-anchor="middle">{{delta.lane.r.2}}</text></g>
  <g class="acc"><rect x="462" y="226" width="54" height="22" rx="3" class="fill-sheet" stroke="currentColor" stroke-width="1.5"/><text x="489" y="241" text-anchor="middle">{{delta.lane.r.3}}</text></g>
  <g class="acc"><rect x="400" y="226" width="54" height="22" rx="3" class="fill-sheet" stroke="currentColor" stroke-width="1.5"/><text x="427" y="241" text-anchor="middle">{{delta.lane.r.4}}</text></g>
  <g class="acc"><rect x="338" y="226" width="54" height="22" rx="3" class="fill-sheet" stroke="currentColor" stroke-width="1.5"/><text x="365" y="241" text-anchor="middle">{{delta.lane.r.5}}</text></g>
  <g class="acc"><rect x="276" y="226" width="54" height="22" rx="3" class="fill-sheet" stroke="currentColor" stroke-width="1.5"/><text x="303" y="241" text-anchor="middle">{{delta.lane.r.6}}</text></g>
  <g class="acc"><rect x="214" y="226" width="54" height="22" rx="3" class="fill-sheet" stroke="currentColor" stroke-width="1.5"/><text x="241" y="241" text-anchor="middle">{{delta.lane.r.7}}</text></g>
  <text x="202" y="285" text-anchor="end">a - b</text>
  <rect x="648" y="270" width="54" height="22" rx="3" class="fill-sheet" stroke="currentColor"/><text x="675" y="285" text-anchor="middle">{{delta.lane.plain.0}}</text>
  <g class="mark"><rect x="586" y="270" width="54" height="22" rx="3" class="fill-sheet" stroke="currentColor" stroke-width="2"/><text x="613" y="285" text-anchor="middle">{{delta.lane.plain.1}}</text></g>
  <rect x="524" y="270" width="54" height="22" rx="3" class="fill-sheet" stroke="currentColor"/><text x="551" y="285" text-anchor="middle">{{delta.lane.plain.2}}</text>
  <rect x="462" y="270" width="54" height="22" rx="3" class="fill-sheet" stroke="currentColor"/><text x="489" y="285" text-anchor="middle">{{delta.lane.plain.3}}</text>
  <rect x="400" y="270" width="54" height="22" rx="3" class="fill-sheet" stroke="currentColor"/><text x="427" y="285" text-anchor="middle">{{delta.lane.plain.4}}</text>
  <rect x="338" y="270" width="54" height="22" rx="3" class="fill-sheet" stroke="currentColor"/><text x="365" y="285" text-anchor="middle">{{delta.lane.plain.5}}</text>
  <rect x="276" y="270" width="54" height="22" rx="3" class="fill-sheet" stroke="currentColor"/><text x="303" y="285" text-anchor="middle">{{delta.lane.plain.6}}</text>
  <rect x="214" y="270" width="54" height="22" rx="3" class="fill-sheet" stroke="currentColor"/><text x="241" y="285" text-anchor="middle">{{delta.lane.plain.7}}</text>
  <g class="mark"><path d="M675 294 C675 310 613 310 613 296" fill="none" stroke="currentColor" stroke-width="1.5"/><path d="M609 302 L613 294 L617 302 Z" fill="currentColor"/></g>
  <text class="mark" x="574" y="308" text-anchor="end">the borrow of byte 0</text>
</svg>
</div>
<figcaption><code>sub_bytes()</code> byte by byte on the 8 bytes at byte {{delta.lane.at}} of the page of counters minus the 8 bytes {{delta.lane.dist}} before, byte 0 on the right. Byte 0 borrows: in <code>sub_bytes()</code> the borrow stays inside byte 0, in the plain <code>a - b</code> byte 1 comes out 1 too small.</figcaption>
</figure>
@end

@@ 75-87
A record, 3 `u16` that `encode_seq()` wrote before each sequence's literals: where they start in the
page, how many there are, and the offset before them, which gives the distance of `SEQLZ_DELTA_LAST`.

@@ 88-109
Which delta to use, priced like the tables: the literals as they are and with each of the 3 deltas,
in all 8 fixed tables at once, 4 sums. A delta pays for its header byte, `DELTA_HDR_BITS`, the 8 bits
scaled to the 1 in {{delta.step}} literals priced, at least 1.

The comment's last sentence is an assumption: the delta is chosen by the fixed tables, also for a page
that then gets a table of its own.

@@ 110-138
For each priced literal: the byte, and the bytes 4, 8 and `last` back where the literal is that far
into the page (else 0, the format's one rule for all three). The cast to `u8` is the difference modulo
256, and keeps the index inside `lit_cost`.

`j` carries the step over to the next sequence, so the priced literals are every {{delta.step}}th of
the page, not of each sequence.
@more {#ch-literals-enc--delta-loop} The loop on the page of pointers, step by step
The loop walks the records: `p` is a record, `p + LIT_REC` its literals, and the next record follows
them. For each priced literal at position `at` it loads the byte, and the byte each delta would be
to: 4, 8 or `last` back, where the literal is at least that far into the page, else 0, which leaves
the byte as it is. That is the one rule of the format, `p >= x` for all three, so the price is of the
literals the decoder would get; the first literals of a sequence are deltas too, to bytes of the match
or the sequences before.

`b - b4` is an `unsigned int` and wraps to near $2^{32}$ when `b4` is the larger byte; the cast to
`u8` keeps its low 8 bits, which is the difference modulo 256, e.g. `0x00 - 0x56` is `0xaa`. The price
is as cheap as the XOR's that came before: a subtraction where there was a XOR. Then 4 adds, one per
choice, each an add of all 8 tables in one `u64`. `n` counts the literals in the 8-bit lanes: after
`LIT_COST_RUN`, 25, of at most 10 bits a lane could overflow, so they move to the 16-bit lanes first.

`j` makes the priced literals every {{delta.step}}th of the page's literals; per sequence, a sequence
of 1 literal would be priced every time. [The page of pointers](#ch-matcher--back), whose
{{delta.ptr.n_lit}} literals at level 3 are mostly the low byte of a pointer right after a match at
offset 8, has {{delta.ptr.sampled}} priced literals.
@assert delta.ptr.sampled == (delta.ptr.n_lit + delta.step - 1) // delta.step
@end

@@ 139-152
The cheapest of 4 × 8 sums wins. A tie keeps the lower number, so no delta wins over a delta that saves
nothing.
@more {#ch-literals-enc--delta-prices} The four prices of the page of pointers
Each of the 4 times 8 sums is read with `lit_bits()`, choice `x` in table `k`. A delta costs its byte
in the header, 8 bits, and the sums count every {{delta.step}}th literal only, so the byte is scaled
as the sums are, `DELTA_HDR_BITS`: $8 / 11$ is 0 in integer division, and the macro takes
{{delta.penalty}}, the "at least 1" of its comment. For [the page of pointers](#ch-matcher--back), in
bits of the priced literals, each with its best fixed table:

| choice | none | `SEQLZ_DELTA_4` | `SEQLZ_DELTA_8` | `SEQLZ_DELTA_LAST` |
| --- | --- | --- | --- | --- |
| bits, with {{delta.penalty}} for the delta's byte | {{delta.ptr.price.0}} | {{delta.ptr.price.1}} | {{delta.ptr.price.2}} | {{delta.ptr.price.3}} |
@assert delta.ptr.pick == ch06.DELTA_8 and delta.ptr.price[2] == delta.ptr.price[3] < min(delta.ptr.price[0], delta.ptr.price[1]) and delta.ptr.recs_last8 == delta.ptr.recs - 1 and delta.ptr.rec[0].dist == 4

`SEQLZ_DELTA_8` and `SEQLZ_DELTA_LAST` cost the same, and the tie keeps the lower number,
`SEQLZ_DELTA_8`. From the second sequence on both take the byte 8 back, as every offset before is 8;
they differ only in the first sequence, which `SEQLZ_DELTA_LAST` takes 4 back, as its `last` is 1. The
fixed tables underrate the deltas: the deltas are
{{delta.ptr.distinct.deltas}} values, {{delta.ptr.h.10}} of them `0x10`, which no fixed table codes in
few bits, and the page's own table then takes the page from {{delta.ptr.l3plain.len}} bytes without
deltas to {{delta.ptr.l3.len}} with them ([the page of pointers](#ch-header--deltas)).
@end

@@ 153-168
Two passes over the records: `pick_delta()` prices, this function moves the literals down over the
records and writes their deltas into `deltas`, the matcher's state.
@more {#ch-literals-enc--deltas} What the deltas are, and when they are chosen
The format lets a page store each literal as its delta to an earlier byte, the literal minus that
byte modulo 256, one of three ways for the whole page ([the deltas](#ch-header--deltas)). Which way
codes a page's literals in the fewest bits is a question of the same kind as which fixed table does,
and `seqlz_delta_literals()` answers it the same way, with the sums of `lit_cost` of
[the pricing](#ch-literals-enc--pricing): four sums, the literals as they are and with each delta, in
all 8 fixed tables at once. It runs once the page is matched, on the records that `encode_seq()` wrote
in front of each sequence's literals ([the records](#ch-encoder--records)), and before
`code_literals()`, which then codes the deltas ([`seqlz_code_own()`](#b993)).

It does two things: it prices the deltas in [`pick_delta()`](#b88), and it moves the literals down over
the records, which the page needs whatever it picks. Both walk the records, and [`read_rec()`](#b75)
reads one. `out` is where the next literal goes in `dst`, `to` where the next delta goes in `deltas`.
`pick_delta()` was the first half of `seqlz_delta_literals()` until 24th October 2026, when it moved
out to keep the functions of the kernel copy short (commit `8993922`).
@end

@@ 169-199
The literals move down by 6 bytes per record so far, 8 bytes at a time, also up to 7 bytes past a
sequence's last literal: the next sequence's literals go over those. `memcpy()` was a call per
sequence.

The next record is read before the copy, since only the first sequence's copy reaches into it. A load
before the store makes the overlapping forward copy safe.
@more {#ch-literals-enc--delta-copy} Why the word copy never overwrites what it still reads
The word copies write up to 7 bytes past a sequence's last literal, to `out + n_lit + 6` at most:
those bytes land where the next sequence's literals go next. A sequence's literals start
{{delta.rec}} bytes above `out` for every record so far, its own included, and the next record right
after them, at `in + n_lit`. For the first sequence that is `out + n_lit + {{delta.rec}}`, so its copy
reaches the next record's first byte; from the second sequence on, `out` is at least 2 records below
`in`, and the copy ends below the next record. So the loop reads the next record, `pos`, `ll` and
`last`, before it copies, while `in`, `pin`, `n_lit`, `at` and `dist` keep the current one. Past the
last sequence the bytes written reach at most the first byte past the old end of the literals, inside
the room the encoder's check left before `p` ([the room check](#ch-encoder--room-check)).

As `out` is at least {{delta.rec}} bytes below `in`, each load reads its 8 bytes before the store can
change them, and the next load starts above what the store wrote, so a forward copy of words is safe
where it overlaps; `memcpy()` is not. Without deltas the work ends with this copy.
@end

@@ 200-226
The deltas, 8 at a time with `sub_bytes()`. Literals closer to the page's start than the distance stay
as they are. The encoder subtracts bytes of the page as it is, so a distance of 4 is no problem for a
word of 8; the decoder, which has only what it restored so far, needs words of 4 there.
@more {#ch-literals-enc--delta-write} The first 12 literals of the page of pointers, as deltas
`x` is the distance of the delta, 4, 8 or the `dist` of the record, and `j` the number of literals at
the start of the sequence that stay as they are: those whose position is below `x`, which only a
sequence near the start of the page has; from position `x` on, `j` is 0. The same two lines serve all
three deltas, as the format has one rule for them. The rest become deltas 8 bytes at a time with
[`sub_bytes()`](#b64) if the 8 bytes past the sequence's last literal are still in the page, else
byte by byte, with the cast to `u8` for the modulo 256. The test is on the page only, and the comment
says why it holds for `deltas` too: `to - deltas` is the number of literals of the sequences before,
which lie in the page before this sequence's position, so it is at most `at`, and the stores up to
`to + n_lit + 6` stay below `deltas + SEQLZ_BLOCK_SIZE`, the size of the matcher's state.

The first 12 literals of [the page of pointers](#ch-matcher--back), as they are and as deltas with
`SEQLZ_DELTA_8`:

```text
as they are   {{delta.ptr.plain}}
deltas        {{delta.ptr.deltas}}
```

The first sequence's 9 literals start at byte 0, so `j` is 8: the first 8 stay, and the 9th, `20` at
byte 8, becomes `20 - 10 = 10`. From the second sequence on, `j` is 0 and each literal, the first of
its sequence, is the low byte of a pointer minus the one 8 bytes before: `30 - 20 = 10`, `40 - 30 = 10`,
`50 - 40 = 10`. The low bytes go up by `0x10`, so the delta of every low byte is `0x10`, also from
`0xf0` to `0x00`, where `0x00 - 0xf0` is `0x10` modulo 256. A XOR of the same bytes was `0x10` only
every second time, `0x30` every fourth, `0x70` every eighth: the bits that change when a counter in
the top 4 bits counts up by one. Of the page's {{delta.ptr.n_lit}} literals, {{delta.ptr.xor.h.10}}
XORed were `0x10` and {{delta.ptr.h.10}} deltas are.
@assert delta.ptr.plain == "10 00 56 34 12 7f 00 00 20 30 40 50" and delta.ptr.deltas == "10 00 56 34 12 7f 00 00 10 10 10 10" and delta.ptr.pick == ch06.DELTA_8 and delta.ptr.rec[0].ll == 9 and F("delta.ptr.xor.h.10") > F("delta.ptr.xor.h.30") > F("delta.ptr.xor.h.70") and F("delta.ptr.h.10") > F("delta.ptr.xor.h.10")
@end

## Codes into streams

@@ 227-242
One literal's code into a stream's accumulator. `*sum += e` adds the whole entry, code and length: the
low byte of the sum is then the sum of the lengths, exact below 256, which 4 codes of at most 10 bits
are. One mask per flush instead of one per literal.
@more {#ch-literals-enc--lit_put-one-code-into-an-accumulator} Stream 0 of the page of letters, code by code
`e & 63U` is the code's length: the low byte holds at most 10, and the mask lets the compiler see a
shift below 64, for UBSan ([the masks](#ch-internal--masks)). In `*sum += e` the codes add only into
bits 8 and up, and a carry never goes down, so the low byte of the sum stays the sum of the lengths;
between two flushes it is at most $4 \times 10 = 40$, and `lit_flush()` reads `*sum & 255U`. Stream 0
of [the page of letters](#ch-literals-enc--example), in table {{ch11.set}}, gets literals 0, 8, 16 and
24:

| literal | `enc[b]` | code | `acc` after | `sum` after | low byte |
| --- | --- | --- | --- | --- | --- |
| `{{ch11.put.0.ch}}` | `{{ch11.put.0.enc}}` | `{{ch11.put.0.code}}`, {{ch11.put.0.len}} bits | `{{ch11.put.0.acc}}` | `{{ch11.put.0.sum}}` | {{ch11.put.0.low}} |
| `{{ch11.put.1.ch}}` | `{{ch11.put.1.enc}}` | `{{ch11.put.1.code}}`, {{ch11.put.1.len}} bits | `{{ch11.put.1.acc}}` | `{{ch11.put.1.sum}}` | {{ch11.put.1.low}} |
| `{{ch11.put.2.ch}}` | `{{ch11.put.2.enc}}` | `{{ch11.put.2.code}}`, {{ch11.put.2.len}} bits | `{{ch11.put.2.acc}}` | `{{ch11.put.2.sum}}` | {{ch11.put.2.low}} |
| `{{ch11.put.3.ch}}` | `{{ch11.put.3.enc}}` | `{{ch11.put.3.code}}`, {{ch11.put.3.len}} bits | `{{ch11.put.3.acc}}` | `{{ch11.put.3.sum}}` | {{ch11.put.3.low}} |
@end

@@ 243-256
The last bytes of a stream, where an 8-byte store would write into the next stream. It runs once or
twice per stream, so a loop of up to 7 stores is fine; `gcc -O2` for x86-64 inlines it anyway. In
place, `sizes[j]` came from the exact bits of the stream, so its last flush ends exactly at `e`, and
the bits after the last code are the zeros of the shift, as [the format](book:#ch-format--end)
requires. In a slot, `e` is the next slot, at least 8 bytes past the stream's last byte
([`LIT_SLOT()`](#b417)), so every flush there is a store of 8; the copy into place
[ends the last stream](#b859) with `store_tail()`.

@@ 257-280
The streams' version of `enc_flush()`, but forwards: the bits on top, a byte swap and a
little-endian store make a big-endian store, so the stream's first bit is the top bit of its first
byte. One swap before the branch: in each branch, the compiler did not merge them.
@more {#ch-literals-enc--lit_flush-whole-bytes-to-memory-big-endian} The first flush of stream 0, step by step
Like [`enc_flush()`](seqlz_encoder.h.html#b116), but forwards: `n` bits are not in memory yet, the
shift puts them on top, and `swab64()` followed by a little-endian store is a big-endian store, the
order the decoder reads. The mask matters only for `n = 0`, at a stream's last flush when its bits
ended on a byte boundary; `p` is then at `end`, and `store_tail()` writes 0 bytes. With 8 bytes of
room the flush writes all 8, the zeros past the bits landing in the stream's own room for later
flushes to overwrite; near the end [`store_tail()`](#b243) writes only the `(n + 7) >> 3` bytes that
carry bits, so it never touches the next stream. A partial last byte is written again by the next
flush.

For stream 0 of [the page of letters](#ch-literals-enc--example), the first flush:

```text
n        = 0 + {{ch11.flush.n}} = {{ch11.flush.n}}
acc<<{{ch11.flush.shift}}  = {{ch11.flush.sh}}        the {{ch11.flush.n}} bits on top, zeros below
w        = {{ch11.flush.w}}        bswap64
stored   = {{ch11.flush.stored}}   at q[0], byte {{ch11.q.0}} of the page
p       += {{ch11.flush.n}} >> 3 = {{ch11.flush.moved}}               cnt = {{ch11.flush.n}} & 7 = {{ch11.flush.cnt}}
```

The bytes `{{ch11.flush.final}}` are final; `{{ch11.flush.part}}` holds {{ch11.flush.cnt}} bits of
stream 0, `{{ch11.flush.partbits}}`, and the next flush writes it again with its last 3 bits.
@assert ch11.flush.cnt == 5 and ch11.flush.moved == 2
@end
@more {#ch-literals-enc--one-swap} Why one byte swap before the branch
The obvious form is `put_unaligned_be64()` in the first branch and a big-endian byte loop in the
second. On arm64, swapping the bytes inside both branches gave the coder other machine code, so the
code swaps once, in a register, before the branch, and both branches store the same `w` (commit
`9d64639`). On a big-endian machine `put_unaligned_le64()` swaps the bytes itself, so the bytes in
memory are the same on every machine; CI checks that on s390x.
@end

## Pricing all 8 tables

@@ 281-319
The pricing: `lit_cost[b]` has byte `b`'s code length in table `j` in byte lane `j`, so one 64-bit add
sums all 8 tables, SWAR. The kernel may not use vector registers here.

25 literals per stream go into the 8-bit lanes (25 × 10 bits ≤ 255), then into the 16-bit lanes. The 8
sums per table are also the 8 streams' sizes, so no second pass. The same pass counts the bytes for a
table of the page's own, even and odd literals in two histograms, so that a run of one byte does not
wait for its own count. Level 2 does not call it: it prices [a sample](#b825) and takes the sizes from
the streams; level 3 does, on the pages that may get deltas or a table of their own.
@anim pricing The sums of stream 0, one add per literal, and their move into the 16-bit lanes
@more {#ch-literals-enc--pricing} `e`, space, `e`, zero: 4 adds instead of 32
For every stream and every literal table, `lit_sums()` sums how many bits the stream's literals take.
The obvious loops are 8 loads and 8 adds per literal; the first version in the project, a histogram
followed by 8 times 256 products, cost 1400 ns on pages with few literals
([explored-designs.md](https://github.com/martinus/seqlz/blob/main/docs/explored-designs.md#seqlz-fast-lit-one-of-8-literal-tables-per-page)).
With `lit_cost[b]` one add sums all 8 tables, [SWAR](#ch-cpu--swar).

The adds go into the 8-bit lanes of `x`, 25 literals per stream, `LIT_COST_RUN`: 25 codes of at most
10 bits sum to at most 250, a 26th could overflow into the next lane. Then
[`lanes_spill()`](#b40) widens each stream's 8 byte lanes into the 16-bit lanes of `even`, tables 0,
2, 4 and 6, and `odd`, tables 1, 3, 5 and 7. The chunks are 200 literals, a multiple of 8, so `k` is a
multiple of 8 in the first inner loop and literal `k + j` is stream `j`; only the last chunk takes a
rest one literal at a time. A 16-bit lane holds the {{ch11.stream_bits}} bits of a stream at most,
{{k16.ch11.stream_bits}} on 16 KiB pages.
@assert F("k16.ch11.stream_bits") <= 65535

The 4 literals `65 20 65 00`, `e`, a space, `e` and a zero byte, have these `lit_cost` entries, table
7 in the top byte:

```text
{{ch11.cost.65}}
{{ch11.cost.20}}
{{ch11.cost.00}}
```

As stream 0, `x[0]` after the 4 adds, and the split:

```text
x                = {{ch11.x}}
x & lanes        = {{ch11.even}}    even: tables 6, 4, 2, 0 = {{ch11.sum.6.dec}}, {{ch11.sum.4.dec}}, {{ch11.sum.2.dec}}, {{ch11.sum.0.dec}}
x >> 8 & lanes   = {{ch11.odd}}    odd:  tables 7, 5, 3, 1 = {{ch11.sum.7.dec}}, {{ch11.sum.5.dec}}, {{ch11.sum.3.dec}}, {{ch11.sum.1.dec}}
```

4 adds instead of 32. The animation under the note does the same on stream 0 of
[chapter 5](#ch-trace)'s page, {{ch11.slab.s0}} literals in chunks of 25; as a choice, the same adds
in 8-bit lanes only. Level 2 prices the page of letters from a sample now
([its pricing](#ch-literals-enc--bytes)), with the same adds, one sum for all sampled literals.
@end
@more {#ch-literals-enc--histograms} Two histograms, even and odd literals, measured
`hist` is `NULL` where the page tries no table of its own, and the two `if (hist)` are then the same
branch for the whole page, which the CPU predicts; otherwise the same pass counts the bytes for the
page's own table. Literal
`k + j` goes to `hist[j & 1]`, and with `k` a multiple of 8, `j & 1` is the literal's own parity: even
literals into one histogram, odd ones into the other. Text repeats bytes, `ee` or a run of spaces,
and two increments of the same counter in a row are a chain: the second load waits for the first
store. With two histograms, two neighbours never share one; [`own_code()`](#b625) adds them up later.
The counts are `u16`: a page has at most as many literals as bytes, 16 384 on 16 KiB pages. Counting
in this pass, instead of one of its own, made the own table cost writes 20.2% instead of 23.9% in
userspace on the Mi 9T's Cortex-A76, on 15th October 2026
([explored-designs.md](https://github.com/martinus/seqlz/blob/main/docs/explored-designs.md#level-3-a-lazy-matcher-and-a-literal-table-of-the-pages-own-zstd-3s-memory-in-less-than-half-its-time-on-the-phone-kept)).
@end
@more {#ch-literals-enc--pricing-cost} Why 8 sums per table, and what the pricing costs, measured
One sum per page would do to choose the table. The 8 sums per table are for after the choice: each
stream is rounded up to whole bytes on its own, and writing in place needs all 8 sizes before the
first stream, since stream 1 starts where stream 0 ends. So the sizes fall out of the pricing, and no
second pass over the literals is needed. Level 2 does without them: its streams go into slots of
their own, and their sizes are where they end ([the slots](#b859)).

::: measured What the pricing costs
In the kernel, a build that prices the literals and then stores the page with raw literals wrote a
page in 6920 ns at p50 and 10 529 ns at p99, against 6890 and 10 490 ns for `seqlz-1`, which does
not price: 30 to 40 ns per write, about 0.5%, September 2026. In userspace loops over 2000 pages the
pricing was at most 400 cycles, 1.5% of a write. Choosing the table from every 4th literal would save
little, as the sizes still need a pass over all literals. Writing the streams is what costs, about
0.6 ns per coded literal in the kernel
([explored-designs.md](https://github.com/martinus/seqlz/blob/main/docs/explored-designs.md#seqlz-fast-lit-one-of-8-literal-tables-per-page)).
On 31st October 2026 the pass was 34% of `code_literals()`' instructions at level 2 on x86-64, and
level 2 left it out: the sizes come from the streams in [the slots](#b859)
([the sample](#ch-literals-enc--sample)).
:::
@end

## Four streams at a time

@@ 320-345
One stream is one chain: each code waits for the shift before. 4 accumulators run 4 chains side by
side; one stream after the other cost about 3.5 cycles per literal. Why 4 and not 8 is not written
down: 8 streams of 5 live values each would be 40, far more than 16 registers. `ends` gets where each
stream stopped, which in a slot is the stream's size.
@more {#ch-literals-enc--streams} Why 4 streams at a time, measured
Appending a code is `acc = acc << len | code`, and the next code of the same stream needs the result:
one stream is one chain where every step waits for the one before. Written one stream after the
other, that cost about 3.5 cycles per literal, measured with commit `34080eb`
([explored-designs.md](https://github.com/martinus/seqlz/blob/main/docs/explored-designs.md#numbers-that-were-only-in-the-codecs-comments-until-10th-october-2026)).
With 4 accumulators, 4 chains run side by side on an out-of-order CPU
([dependency chains](#ch-cpu--chains)). The outer loop runs twice, streams 0 to 3 and 4 to 7. `e0` to
`e3` are the ends, each stream ending where the next starts. Each stream has 5 live values, `p`, `e`,
`a`, `c` and `s`.

The 40 live values of 8 streams are a guess at the reason, not measured: x86-64 has 16 general
registers. In the kernel, "8 streams, 4 in one loop, twice" wrote a page in 7590 ns at p50 and
12 030 ns at p99 on a desktop's zram pages, against 7530 and 12 030 ns with the 4 streams of the
format before in one loop
([explored-designs.md](https://github.com/martinus/seqlz/blob/main/docs/explored-designs.md#seqlz-fast-lit-one-of-8-literal-tables-per-page)).
@end

@@ 346-362
A run: 4 literals per stream of this half, then a flush. 4 codes of 10 bits and 7 bits left over fit
the accumulator. The loop stops early enough that at most 4 literals per stream are left for below.
@more {#ch-literals-enc--runs-of-32-literals} Which literals a run takes, and why 4 per stream
A run is `run`, 32 literals of the page, 4 for each stream of this half: for `half = 0` and `k = 0`,
stream 0 gets literals 0, 8, 16 and 24, stream 1 gets 1, 9, 17 and 25; for `half = 4`, `k` starts at
4. 4 codes of at most 10 bits and 7 bits left over are 47 bits, which fit; 5 would fit too,
$7 + 50 = 57$, and the repository does not say why 4. The highest literal of a run is `k + 27`, and the
condition `k + run - 4 < n_literals` asks for one more, so after the loop each stream of this half has
at most 4 literals left, which fit without a flush.
@end

@@ 363-398
The rest, at most 4 literals per stream: one loop with a switch, skipping the other half's literals
(`k += 4` after each group of 4), then the last flushes. A stream's end counts its last byte if it
holds bits, `c != 0`: the flush left them there without moving `p` past it.
@more {#ch-literals-enc--the-tail-at-most-4-literals-per-stream-one-switch} The tail of the page of letters
Cases 0 and 4 both go to `a0`, which is stream 0 for `half = 0` and stream 4 for `half = 4`, so one
switch serves both halves. For `half = 0` and the `n = {{ch11.n}}` of
[the page of letters](#ch-literals-enc--example), the runs end at `k = {{ch11.tail0}}`, and the tail
takes:

```text
{{ch11.tr.0}}
{{ch11.tr.1}}
{{ch11.tr.2}}
{{ch11.tr.3}}
{{ch11.tr.4}}
{{ch11.tr.5}}
```
@assert ch11.tr.lines == 6

For `half = 4` the tail starts at `k = {{ch11.tail4}}`, stream 4, and after {{ch11.tr4.last}} the
skip makes `k` {{ch11.tr4.jump}}, past the end, where the loop condition stops it.
@assert ch11.tail4 % 8 == 4 and ch11.tr4.jump > ch11.n
@end

@@ 399-416
The 8 stream sizes in `width` bits each, lowest bit first, exactly `width` bytes.
@more {#ch-literals-enc--sizes} The 8 sizes of the page of letters in 5 bytes
The 8 sizes are one number of `8 * width` bits, little endian, size 0 in the lowest `width` bits
([format.md](https://github.com/martinus/seqlz/blob/main/docs/format.md#layout-of-a-compressed-block)).
`acc` holds at most $7 + 12 = 19$ bits, and after 8 sizes `8 * width` bits, whole bytes, have gone out,
exactly `width` bytes. The sizes of [the page of letters](#ch-literals-enc--example), {{ch11.sizes}}
with `width = {{ch11.width}}`, become the {{ch11.width}} bytes `{{ch11.sizes.hex}}`: in binary, little
end first, `10010` (18), `10001` (17), and so on.
@assert ch11.width == 5 and F("ch11.sizes").startswith("18, 17,")
@end

## A table of the page's own

@@ 417-479
The work memory of a page's own table, at most `OWN_WORK`, 4 KiB, lives in the matcher's 8 KiB state
behind the copy of the literals, so it costs nothing extra. Only pages with 512 literals or more try
it, and it has to save {{own.enc.saves_min}} bytes against the best fixed table.

The description takes at most 256 bytes; its room is 512, a power of 2, so `own_put()` can mask its
index and the compiler sees the bound.

Below level 3, and at level 3 on a page without deltas and with fewer than {{own.enc.min_lits}} literals, the table
comes from every {{ch11.sample.step}}th literal, and the streams go into 8 slots of `LIT_SLOT()` bytes
in the matcher's state first: room for the longest stream a page's literals can have, every literal
with a code of 10 bits, plus 9 bytes, 1 for the rounding down of `/ 8U` and 8 for `lit_flush()`'s
8-byte stores. On 4 KiB pages that is {{ch11.slot}} bytes a slot, {{ch11.slots_bytes|grp}} for all 8,
in the {{ch11.state_bytes|grp}} bytes of the state; on 16 KiB pages they do not fit, `LIT_SLOTS` is 0,
and every literal is priced, as on level 3's other pages.
@more {#ch-literals-enc--the-work-memory} Which pages may try, measured, and why 256 bytes of description at most
A table of the page's own needs counts, lengths, codes and room for the description,
{{own.enc.work_size|grp}} bytes in `struct own_work`. The compressor's work memory is the 8 KiB of the
matcher's table, and nothing else is free per CPU, so [`own_work()`](#b987) puts the struct at byte
{{own.enc.work_at}} of it; below that byte goes the copy of the raw literals. A page with more
literals than that would overwrite it, so `OWN_LITERALS_MAX` is the limit, 4096 on 4 KiB pages, where
no page has more. On 16 KiB pages it is 12 288 of 16 384.

Pages with fewer than 512 literals, `OWN_LITERALS_MIN`, gained 1.15 bytes per page together on one
phone dump, counted offline, and in the kernel VM 512 was on the hull, where a table from 2048
literals on was too. With it, about a fifth of the pages got a table of their own, and its
description took about 0.45 bytes per byte with a code
([explored-designs.md](https://github.com/martinus/seqlz/blob/main/docs/explored-designs.md#level-3-a-lazy-matcher-and-a-literal-table-of-the-pages-own-zstd-3s-memory-in-less-than-half-its-time-on-the-phone-kept)).
`OWN_SAVES_MIN` was 16 bytes until 31st October 2026: 32 stores 1.07 bytes per page more on the ten
samples and makes level 3's writes 0.19 µs faster on the Mi 9T's Cortex-A76 and 0.47 µs on its A55
([explored-designs.md](https://github.com/martinus/seqlz/blob/main/docs/explored-designs.md#the-literal-coding-thresholds-per-level-35-bytes-at-level-2-19-at-level-3-own-tables-from-32-33-and-49-bytes-per-page-less-kept)).

The description takes at most 256 bytes, 8 bits for each of the 256 byte values, as the comment
counts: a byte with a code is a symbol of at most 8 bits, and a run of $r$ bytes before it takes
$8 + \lfloor \log_2 r \rfloor$ bits, at most 8 for each of the $r + 1$ bytes. Its room is 512 bytes, a
power of 2 above that, and 8 more for the last 8-byte store of [`own_put()`](#b569).
@assert own.enc.work == 4096 and own.enc.work_size <= own.enc.work and own.enc.max_lits == 4096 and own.enc.min_lits == 512 and own.enc.desc_room == 512
@end

@@ 480-522
Code lengths close to Huffman without sorting. Each byte first gets the shortest `l` with
`f * 2^l >= n`, which keeps the code within its space except where lengths are cut to 10 bits.

`f * 2^l / n`, 1 to 2, is how much a byte saves per code space if its code were one bit shorter, the
"gain". A counting sort into 16 buckets by gain gives the order for the repair below, with a multiply
by `inv` instead of a division.
@anim ownlens own_lengths() on two real pages, bucket by bucket
@more {#ch-literals-enc--own_lengths-close-to-huffman-without-sorting} The first lengths and the buckets of the text page
A Huffman code gets the code lengths right, but it needs the bytes sorted by count, and a tree. The
comment's way is cheaper: start with lengths that are about right, then repair them in an order that
a counting sort gives.

A byte that is a fraction $p = f / n$ of the literals would ideally get $-\log_2 p$ bits. The first
loop gives it the shortest `l` with $f \cdot 2^l \ge n$, which is $\lceil -\log_2 p \rceil$: the count
of leading zeros gives `l` within one, `(f << l) < n_literals` corrects it, `l += !l` would keep a
byte that is all the literals from 0 bits, which `own_code()`'s test for 2 bytes or more rules out
before, and lengths above 10 bits are cut to 10. Since each $2^{-l} \le p$, the lengths sum to at most
1, and only the cut can overfill the code.

$f \cdot 2^l / n$ is then between 1 and 2, and it is what the code calls the gain: shortening the byte
to `l - 1` bits saves `f` bits and takes $2^{-l}$ more of the code space, so $f \cdot 2^l$ is the bits
saved per code space. `inv` turns the division into a multiply, computed once with `div_u64()`
([`div_u64()`](#ch-compat--min)), and `q` is 16 times the ratio, 16 to 31, so `q - 16` is a
bucket from 0 to 15. Bytes cut to 10 bits get bucket 0. Then a counting sort, `at[]`, lists the bytes
by bucket in `by_gain[]`, within a bucket by byte value. For [the text page](#ch-literals-enc--own):

| byte | count | `l` | $f \cdot 2^l$ | $f \cdot 2^l / n$ | bucket | final length |
| --- | --- | --- | --- | --- | --- | --- |
| space | {{own.enc.b.20.freq}} | {{own.enc.b.20.l0}} | {{own.enc.b.20.f2l}} | {{own.enc.b.20.ratio}} | {{own.enc.b.20.gain}} | {{own.enc.b.20.len}} |
| `l` | {{own.enc.b.6c.freq}} | {{own.enc.b.6c.l0}} | {{own.enc.b.6c.f2l}} | {{own.enc.b.6c.ratio}} | {{own.enc.b.6c.gain}} | {{own.enc.b.6c.len}} |
| `n` | {{own.enc.b.6e.freq}} | {{own.enc.b.6e.l0}} | {{own.enc.b.6e.f2l}} | {{own.enc.b.6e.ratio}} | {{own.enc.b.6e.gain}} | {{own.enc.b.6e.len}} |
| `u` | {{own.enc.b.75.freq}} | {{own.enc.b.75.l0}} | {{own.enc.b.75.f2l}} | {{own.enc.b.75.ratio}} | {{own.enc.b.75.gain}} | {{own.enc.b.75.len}} |
| `e` | {{own.enc.b.65.freq}} | {{own.enc.b.65.l0}} | {{own.enc.b.65.f2l}} | {{own.enc.b.65.ratio}} | {{own.enc.b.65.gain}} | {{own.enc.b.65.len}} |
| `a` | {{own.enc.b.61.freq}} | {{own.enc.b.61.l0}} | {{own.enc.b.61.f2l}} | {{own.enc.b.61.ratio}} | {{own.enc.b.61.gain}} | {{own.enc.b.61.len}} |
| `d` | {{own.enc.b.64.freq}} | {{own.enc.b.64.l0}} | {{own.enc.b.64.f2l}} | {{own.enc.b.64.ratio}} | {{own.enc.b.64.gain}} | {{own.enc.b.64.len}} |
@assert own.enc.b["20"].l0 == 3 and own.enc.b["6c"].l0 == 4 and own.enc.b["6c"].len == 3 and own.enc.b["75"].len == 3 and own.enc.b["6e"].len == 4 and own.enc.b["6e"].gain == own.enc.b["75"].gain and own.enc.changed == 2

For the space, `clz({{own.enc.b.20.freq}}) - clz({{own.enc.n_lit}})` is ${{own.enc.clz_space}} - {{own.enc.clz_n}} = 2$,
and ${{own.enc.b.20.freq}} \cdot 4 < {{own.enc.n_lit}}$ makes it {{own.enc.b.20.l0}}. The 13 first
lengths, one of 3 bits and 12 of 4, fill {{own.enc.space0}} of the 1024 units of a 10-bit code space:
{{own.enc.room0}} are left, which [the repair](#ch-literals-enc--repair) hands out.

The animation under the note puts each byte's share of the literals, $f / n$, next to the share of the
code space its code takes, $2^{-l}$: the first loop keeps the second at or below the first, and the
gain is how far below. As a choice it runs on the base64 page that
[`read_own_table()`'s animation](#f-seqlz_decompress_literals-c-b199) reads too, where the second loop has more to do.
@end

@@ 523-547
The repair: an overfull code lengthens the bytes of the smallest gain first, then room left goes to
the bytes of the largest gain. The result can be a few bits worse than a real Huffman code; the
buckets save the sort and the tree.
@more {#ch-literals-enc--repair} The repair on the text page: `l` and `u` get 3 bits
An overfull code gets longer codes from bucket 0 up: lengthening a byte frees the least room where it
costs the fewest bits per room. Room left is handed out from the top bucket down, while a byte's
shorter code still fits. On [the text page](#ch-literals-enc--own_lengths-close-to-huffman-without-sorting)
the code is not overfull, and the second loop runs once through `by_gain[]` from its end: `l`, the
last byte of bucket 3, gets 3 bits and takes 64 of the 128 units, the space has 3 already and would
need 128 for 2, and `u`, the last byte of bucket 2, takes the other 64. That fills the code. `n`
occurs {{own.enc.nu_diff}} times more than `u` and stays at 4 bits: both are in bucket 2, and within a
bucket the loop goes by byte value, from the top. A Huffman code would not leave those
{{own.enc.nu_diff}} bits; the buckets save the sort. The comment above the second loop is about a byte
that just got shorter and is then among the longest: the room left is always a multiple of the
longest code's, so another round of the `while` fills it.
@end

@@ 548-562
`log2()` in 1/256 bits, to about 1/50 bit, with a table of 32 bytes and no division: the top bit's
position is the integer part, the next 5 bits index the fraction.
@more {#ch-literals-enc--log2} `log2_256(608)`, step by step
For $f \ge 1$, `e` is the position of the top bit, the integer part of $\log_2 f$. `f << (31 - e)` moves
the top bit to bit 31, and the 5 bits below it, `>> 26 & 31`, say where $f / 2^e$ lies between 1 and
2, in 32 steps; `log2_frac[]` has $\log_2$ of the middle of each step, in 1/256. For the space's
{{own.enc.b.20.freq}} on [the text page](#ch-literals-enc--own): `e = {{own.enc.log.e}}`,
`f << 22 = {{own.enc.log.shifted}}`, the 5 bits are {{own.enc.log.idx}},
`log2_frac[{{own.enc.log.idx}}] = {{own.enc.log.frac}}`, so `log2_256()` gives
${{own.enc.log.e}} \cdot 256 + {{own.enc.log.frac}} = {{own.enc.b.20.log}}$, where $256 \log_2 608$ is
{{own.enc.b.20.log_exact}}.
@assert own.enc.b["20"].freq == 608 and own.enc.log.e == 9
@end

@@ 563-568
4 counts as the 16-bit lanes of one `u64`, for the presence test below.

@@ 569-584
Bits into the description, forwards: once 32 bits wait, 8 bytes go out big-endian and the position
moves on by 4; the other 4 bytes are written again by the next store.

As [`enc_put()`](seqlz_encoder.h.html#b91) does for the sequences, but forwards. A put adds at most
16 bits to fewer than 32, so 48 bits fit. `& 63` is a no-op for `cnt` from 32 on, and
`& (OWN_DESC_ROOM - 1)` keeps the store inside `desc[]` for any input, with 8 bytes of room behind it.

@@ 585-624
The description: for each byte with a code, in order of value, the run of bytes without a code before
it, then its code length, as symbols of the fixed code `OWN` ([the format](book:#ch-format--own)).
`prev` starts at `~0U`, so the first run is from byte 0.
@more {#ch-literals-enc--canonical-codes-and-the-description} The description of the text page, run by run
The symbols are those of `OWN` from `seqlz_tables.own.enc`. For the first byte `b - prev - 1` wraps to
`b`, the run from byte 0. A run of `run` is `nb = 32 - clz(run)` bits wide, symbol
`SEQLZ_OWN_RUN + nb`, and its code goes out together with the `nb - 1` bits below the run's top bit,
one put: for the run of 32 before the space, `nb = 6`, the code `{{own.code.15}}` and `00000`. At the
end the bits left in `acc` go out as whole bytes, the last one filled with zeros. On
[the text page](#ch-literals-enc--own) that is {{own.enc.d}} bytes, the ones
[the format](#ch-format--own) reads symbol by symbol. With these bytes and `bits`, the last lines of
`own_code()` make [the first check](#ch-literals-enc--own), which the text page passes with
{{own.enc.first_lhs}} bytes against {{own.enc.limit}}.
@assert own.enc.d == own.text.dbytes

`own_describe()` was part of `own_code()` until 24th October 2026, when it moved out to keep the
functions of the kernel copy short (commit `8993922`).
@end

@@ 625-668
Adds the even and odd counts, 4 at a time, and marks which bytes occur, a bit per byte value, without
a branch: the top bit of each 16-bit lane is set where the lane is not 0, and a multiply gathers the 4
top bits into 4 adjacent bits.
@more {#ch-literals-enc--own_code-which-bytes-and-what-the-entropy-promises} The presence test on bytes `0x60` to `0x63`, step by step
`w->freq[0]` and `w->freq[1]` are the even and the odd literals' counts, and this loop adds them, 4
bytes at a time: [`own_lanes()`](#b563) loads 4 counts as the 16-bit lanes of a `u64`, one add sums 4
pairs, and the sums go back into `freq[0]`. In the same step it marks which of the 4 bytes occur, in
`present[4]`, 256 bits. The comment's trick, for the bytes `0x60` to `0x63`, the backtick and `a` to
`c`, of [the text page](#ch-literals-enc--own), where only `a` occurs:

```text
c                    = {{own.enc.pr.c}}   the 4 sums, a in lane 1
(c & low) + low      = {{own.enc.pr.plus}}   bit 15 of a lane set where its low 15 bits are not 0
(c | ...) & ~low >> 15 = {{own.enc.pr.tops}}   one bit per lane that is not 0, at 0, 16, 32 or 48
* 0x0001000200040008 = {{own.enc.pr.mul}}
>> 48 & 15           = {{own.enc.pr.nibble}}               bit 1: byte 0x61, a
```

The multiplier is $2^{48} + 2^{33} + 2^{18} + 2^3$, and it moves the bit of lane $j$, at $16j$, to
$48 + j$: the 4 products that land in bits 48 to 51 are the 4 lane bits, and the other 12 products
land at other bits, never two on one, so no carry reaches bits 48 to 51. The `c |` covers a lane whose
bit 15 is set by the count itself, which a page of at most 16 384 literals never has.
@end

@@ 669-685
The bytes that occur, in order of value (lowest set bit, then clear it), and on the way the sum of
`f * log2(f)` for the entropy.

@@ 686-700
The cheap test before any code is built: `n log2 n - sum f log2 f` is the entropy, the bits no prefix
code can beat, plus about 0.45 bytes of description per byte. If that cannot save
{{own.enc.saves_min}} bytes, or the page would stay raw anyway with `coded_min`, level 3's
{{ch11.coded_min_own}}, most pages leave here.
@more {#ch-literals-enc--entropy} The estimate on the text page
With $n$ literals and $f_b$ of byte $b$, $n \log_2 n - \sum_b f_b \log_2 f_b$ is $n$ times the
entropy, the bits per literal that no prefix code can beat ([entropy](#ch-compression--entropy)).
`h / 2048` turns 1/256 bits into bytes, and `n * 115 / 256` adds 0.45 bytes of description for each
byte with a code. If that estimate, with the smallest header and `OWN_SAVES_MIN`, is not below
`limit`, or the page would stay raw anyway ([the rule](#b950)), no code is built: on many pages the
fixed table is close enough, and this is where they leave. On [the text page](#ch-literals-enc--own),
`h` is {{own.enc.h}}, {{own.enc.h_bits}} bits per literal, {{own.enc.h_bytes}} bytes, plus
{{own.enc.desc_est}} for {{own.enc.n}} bytes with a code: `est` = {{own.enc.est}}, and
${{own.enc.est}} + 8 + {{own.enc.saves_min}} = {{own.enc.est_lhs}}$ is far below `limit` =
{{own.enc.limit}}.
@assert own.enc.est_lhs < own.enc.limit and own.enc.n == 13
@end

@@ 701-713
Canonical codes from the lengths, as for the fixed tables, and on the way the exact bits of all
literals. `enc[]` has the layout of the fixed tables' `enc[]`, so `lit_streams()` takes either.

The `& 15U` and `& 255U` masks change no valid value; they keep every index inside its array whatever
the value, at the cost of an `and`.
@more {#ch-literals-enc--own-bits} The text page's bits, against the entropy
The [canonical codes](#ch-compression--canonical-codes) are made as
[`first_codes()`](#ch-codes--first-codes) makes them for the fixed tables. `bits` on
[the text page](#ch-literals-enc--own) is {{own.enc.bits}}, {{own.enc.bits_per_lit}} bits per literal:
close to the entropy's {{own.enc.h_bits}}. With the 8 streams rounded up to whole bytes they take
{{own.enc.own_coded}} bytes with a width of {{own.enc.own_width}} bits ([`own_sizes()`](#b723)),
against {{own.enc.set_coded}} with table {{own.enc.set}}.
@end

@@ 714-722
The second test, with the real description and bits: only a page that passes it pays for
`own_sizes()`, one more pass over the literals.

@@ 723-764
The bytes of each stream with the page's own code. Eight named sums, not `sizes[k % 8]`: each is a
chain of its own, and the compiler keeps them in registers.

@@ 765-777
The width of the stream sizes from one count of leading zeros of their OR. Setting bit 4 makes the
width at least 5 and keeps the OR from being 0, for which `__builtin_clz()` is undefined.
@more {#ch-literals-enc--width} Two widths, worked, and why not 2 bytes per size
`size_width()` is called twice at level 3, for the fixed table's sizes and for the page's own. `all`,
the OR of the sizes, has the highest set bit of the largest one, which is all the width needs, without
a compare per stream. `32 - clz` is the bit width:

| largest size | `all`, the sizes ORed, with bit 4 set | `clz` | `width` | header bytes |
| --- | --- | --- | --- | --- |
| {{ch11.s0.bytes}} bytes, [the page of letters](#ch-literals-enc--example) | `{{ch11.max.bin}}` = {{ch11.max.all}} | {{ch11.max.clz}} | {{ch11.width}} | {{ch11.header}} |
| {{ch11.lit_stream_max}} bytes, the largest a 4 KiB page can have | `0b1010010000` | 22 | 10 | 13 |
@assert ch11.lit_stream_max == 640 and ch11.max.all >= ch11.s0.bytes and 0b1010010000 == 640 | 16

Until 5th October 2026 every size had 2 bytes, a header of 19 bytes ([the format](#ch-format--coded)).
On a desktop's zram pages the largest stream of a page with coded literals was below 128 bytes on 77%
of the pages, the mean of two dumps, so the header is now mostly 9 to 11 bytes
([explored-designs.md](https://github.com/martinus/seqlz/blob/main/docs/explored-designs.md#stream-sizes-in-as-many-bits-as-the-largest-needs-kept)).
@end

## code_literals()

@@ 778-824
@anchor ch-literals-enc
The coder. It is `__always_inline`, so each entry point gets its own copy: level 2's has `own` the
constant `NULL` and no code for a page's own table at all, 2% fewer instructions per page, and each
copy has its level's `coded_min` as a constant.

The coded literals go where the raw ones were, so `dst` needs no room beyond `len`: level 2 writes its
streams into slots first, level 3 reads the raw literals from a copy where it prices every literal.
@more {#ch-literals-enc--overview} Where each entry point's raw literals come from
The coded literals go where the raw ones were, so the streams are written somewhere else first, or
the raw literals are read from somewhere else, and each of the three entry points at the end of the
file brings that place in its own way. [`seqlz_code_literals()`](#b1008), which
`seqlz_compress_block()` calls at level 2, passes `raw = NULL` and `spare = st->literals`, the per-CPU
matcher's hash table, which is garbage once the matcher is done: the slots of the streams.
[`seqlz_code_raw_literals()`](#b1018), for `seqlz_encode()`, whose caller has the literals, passes them
as `raw` and a static buffer of the state's size as `spare`. [`seqlz_code_own()`](#b993) passes
`spare = st->literals` without deltas, which then is the copy of the raw literals, or the slots on a
page that tries no table of its own. With deltas it passes as `raw` the deltas that
[`seqlz_delta_literals()`](#b153) wrote into the matcher's state, which delta in `delta`, and no
`spare`; those are the literals that get priced and coded, so `literals` is `raw` where there is one,
while `d` still holds them as they are. So `d` needs no room beyond `len`.
@end
@more {#ch-literals-enc--inline} `__always_inline`, measured
At level 2 `own` is the constant `NULL`, and the code for a table of the page's own is not there, also
not in the loop over the literals that counts them. Until 23rd October 2026 the comment said so but
the compilers did not do it: gcc and clang built one copy of `code_literals()`, and level 2, zram's
default, ran level 3's code with `own == NULL` checked at run time. Inlined, level 2 took 2.0% fewer
instructions to compress a page, 2000 pages of a desktop dump with gcc 16.2 on x86-64, and the kernel
module got about 10 KiB more code (commit `c18ccf0`).
@end
@more {#ch-literals-enc--start} `delta_byte`, `bits = ~0U`, and when `own` becomes `NULL`
`delta_byte` is 1 with a delta and 0 without: it counts the delta's byte, which comes before the
sizes, wherever the header's length matters, and it is bit 7 of byte 2. `n_literals` is the plain
count, as the page has raw literals here, and `bits = ~0U` makes the first table's sum smaller. `own`
is `NULL` below level 3, and becomes `NULL` for a page with fewer than {{own.enc.min_lits}} literals or
more than `OWN_LITERALS_MAX`, {{own.enc.max_lits}} on 4 KiB pages, so on all of them; from then on
`own` alone says whether to try [a table of the page's own](#b912). A page without `own` and with a
`spare` takes [the sample](#b825) and the slots, where they fit the state: level 2, and level 3's pages
without deltas and with fewer than {{own.enc.min_lits}} literals. The others go through
[`lit_sums()`](#b889), which for a page that tries its own table also counts how often each byte
occurs, into `own->freq`, which has to start at 0.
@end

@@ 825-858
The table from a sample: every {{ch11.sample.step}}th literal adds its `lit_cost` entry, all 8 tables
in one add, in the same 8-bit lanes as [`lit_sums()`](#b281) and spilled the same way, but one sum per
table instead of one per stream and table. The cheapest wins, a tie keeps the lower number. Times
{{ch11.sample.step}}, the sum estimates the coded bytes; where that is
{{ch11.sample.margin}} bytes or more past the rule, the page stays raw without its streams written.
@more {#ch-literals-enc--sample} Why a sample of every {{ch11.sample.step}}th literal, and the margin, measured
Until 31st October 2026 level 2 priced every literal with `lit_sums()`, one `u64` add per literal,
and that pass was 34% of `code_literals()`' instructions at level 2 on x86-64; the pass was needed
for the header, which wants each stream's size before the first stream is written in place. A sample
before the pass did not help: the pass still ran on every coded page. A sample alone picks the table
well, 0.06 to 0.14 bytes per page worse than every literal, so level 2 now takes the table from the
sample and the sizes from the streams, which it writes into [slots](#b859) first. With every 5th
literal that saved 1.6% of level 2's compress instructions for 0.51 bytes per page on ten samples of
20 000 pages; on the Mi 9T level 2 then wrote a page 0.46 µs faster on the A55, 1.4%, and 0.21 µs on
the A76, 1.8%, where two copies of the same build differed by 0.08 and 0.06 µs. The step is a prime,
so that it is not the same byte of each 4- or 8-byte field: every 4th cost 5.3 bytes per page. 7
saves 2.0% of the instructions for 0.67 bytes; 11 and 13 cost more bytes per instruction saved
([explored-designs.md](https://github.com/martinus/seqlz/blob/main/docs/explored-designs.md#the-literal-table-from-a-sample-the-streams-into-slots-level-2-writes-14-to-18-faster-on-the-mi-9t-for-05-bytes-per-page-kept-the-sample-before-the-pass-level-3-from-the-counts-and-four-counts-not-kept)).

Most pages that stay raw are far from the rule, so the sample sends them back before any stream is
written. Without that exit, the streams of two thirds of the pages were written for nothing, 6.8%
more instructions than with it; a margin of 16 bytes instead of {{ch11.sample.margin}} saved 0.6% of
the instructions instead of 1.6%, for 0.30 bytes per page instead of 0.51. A page the sample misjudges
by more than the margin stays raw although it would have paid; that is part of the 0.51 bytes.
@end

@@ 859-888
The streams of the chosen table go into the slots, `q[j]` at `j` slots into `spare`, and a stream's
size is where it ended. On those sizes [the rule](#b935) decides. A page that gets coded has its
streams moved over the raw literals, which are all read by now, 8 bytes at a time: a store up to 7
bytes past a stream's end lands where the next stream goes next. The last stream ends exactly, with
`store_tail()`, as the bitstream may start right behind it; the loads past a stream's end stay inside
its slot.

@@ 889-911
@anchor ch-literals-enc--choice
Every literal priced, on the pages that may get deltas or a table of their own, and on 16 KiB pages:
[`lit_sums()`](#b281), then the table with the fewest bits; a tie keeps the lower number. Each
stream's size is rounded up to whole bytes on its own, and the header counts the delta's byte if
there is one.

The choice compares bits, not bytes: after the rounding, a table with fewer bits can take a few bytes
more. The code does not look for that case, and the decoder takes whatever table byte 2 names.

@@ 912-934
Level 3: a page's own table, if it and its description take at least {{own.enc.saves_min}} bytes less
than the best fixed table. Then the page is written as for a fixed table, with the description in the
header.
@more {#ch-literals-enc--own} The text page: both checks, and why the delta's byte cancels
`enc` holds the fixed table's codes so far. At level 3 a table of the page's own may take its place:
Huffman-like code lengths for exactly the bytes this page has, sent in front of the streams
([the format](#ch-format--own)). The panels of this file follow it on the text page of the format
chapter, {{own.enc.n_lit}} literals of 12 letters and the space, whose best fixed table is table
{{own.enc.set}}, at {{own.enc.set_bits_per_lit}} bits per literal.
@assert own.enc.set == ch04.text.set and own.enc.n_lit == ch04.text.n

[`own_code()`](#b625) gets `header + coded`, what the fixed table costs, as `limit`, and returns the
bytes of the table's description, or 0 where it does not pay. Two checks decide, the first one cheap
and at the end of `own_code()`. Its `bits` is the sum of the code lengths over all literals, counted
on the way; divided by 8 it is at most the sum of the 8 streams' bytes, each rounded up, and the
smallest header has 8 bytes. Only a page that passes with these lower bounds pays for
[`own_sizes()`](#b723), one more pass over the literals, and the exact check here. Both want
`OWN_SAVES_MIN` bytes less than the fixed table:

| | own table | fixed table {{own.enc.set}} |
| --- | --- | --- |
| first check | $8 + {{own.enc.d}} + {{own.enc.bits_bytes}} + {{own.enc.saves_min}} = {{own.enc.first_lhs}}$ | $\le {{own.enc.limit}}$ |
| second check | ${{own.enc.own_header}} + {{own.enc.d}} + {{own.enc.own_coded}} + {{own.enc.saves_min}} = {{own.enc.second_lhs}}$ | $\le {{own.enc.limit}}$ |
@assert own.enc.first_lhs <= own.enc.second_lhs <= own.enc.limit and own.enc.saves_min == 32

So the page takes its own table: header, description and streams are {{own.enc.total}} bytes instead
of {{own.enc.fixed_total}}. `desc`, `width`, `header`, `coded`, `sizes` and `enc` change over to it,
and the rest of `code_literals()` writes the page as for a fixed table, with the description in the
header, behind the sizes, which come behind the delta's byte if there is one. With deltas, `header` on
the right side of the second check counts that byte, so the left side adds `delta_byte` too: both
tables need it, it cancels out, and the page's own table has to save the same {{own.enc.saves_min}}
bytes as without deltas.
@end

@@ 935-936
The rule: coded only where that saves 1/16 of the literals plus `coded_min` bytes,
{{ch11.coded_min}} at level 2 and {{ch11.coded_min_own}} at level 3. zsmalloc's size classes are 16
bytes apart, so a few bytes less often save nothing, while every read of a page with coded literals
pays a fixed time for the literal table and the decode.
@more {#ch-literals-enc--rule} What the rule saves, and why 1/16 of the literals
The page with raw literals has `2 + n` bytes before the bitstream, the coded one `3 + w + e + desc +
coded`, with `e` 1 with deltas, else 0, and `desc` 0 for a fixed table. If the rule holds, the page
shrinks by more than {{ch11.save_min}} bytes at level 2 and {{ch11.save_min_own}} at level 3, plus
$\frac{1}{16}$ of the literals, with the largest `w` of 12 and no deltas, and by one byte less with
deltas. For `n = 0` the right side is 0 and the page stays raw.

zsmalloc stores a page in a size class, and classes are at least 16 bytes apart
([zram](#ch-zram)). A page a few bytes smaller often lands in the same class and saves nothing, while
every read of it pays for decoding the literals. Coding the literals wherever they save anything at
all gave less than 0.1 points more on a desktop's zram pages of September 2026, in userspace
([explored-designs.md](https://github.com/martinus/seqlz/blob/main/docs/explored-designs.md#seqlz-fast-lit-one-of-8-literal-tables-per-page)).
@end
@more {#ch-literals-enc--51} Why {{ch11.coded_min}} and {{ch11.coded_min_own}} bytes, measured on the Mi 9T and the PC
A page with coded literals costs a fixed time to read, whatever the number of literals: the decoder
brings the page's 2 KiB literal table into the cache and decodes the literals into a buffer first
([the decoder](#ch-literals-dec)). On a phone's little core, which has few cache misses in flight,
that is several µs.

::: measured 51 bytes instead of 19, on the Mi 9T and on the PC, 5th October 2026
Before 5th October 2026 the minimum was 19 bytes. In the Mi 9T's own kernel, 20 000 of the phone's
zram pages, clocks fixed, a minimum of 51 coded 24.9% instead of 38.8% of the pages. Pages
got 7.9 bytes larger on average, and the time per page written ($\text{write} + 0.34 \times \text{cold read}$, the reads per write of then) got 2.7 µs
shorter on the Cortex-A55 and 1.1 µs shorter on the Cortex-A76. $\frac{2}{16}$ of the literals instead of $\frac{1}{16}$
coded about as few pages, for 12.3 bytes and less time saved. On desktop pages 51 costs 9 bytes per
page, for 0.1 µs in the kernel VM on a Ryzen 9 7950X, within what two boots of the same build
differ. 51 is the best of the three minimums measured, 19, 51 and 115, not a measured optimum.
Measured again with the prefetches of items 4 and 7 of "Six choices", 51 saved 0.8 µs on the A55 and
0.4 µs on the A76, both within the noise there; it stayed. Sources:
[explored-designs.md, "Coded literals only where they save 51
bytes"](https://github.com/martinus/seqlz/blob/main/docs/explored-designs.md#coded-literals-only-where-they-save-51-bytes-27-µs-per-page-written-less-on-the-a55-kept)
and [item 9 of "Six
choices"](https://github.com/martinus/seqlz/blob/main/docs/explored-designs.md#9-the-51-bytes-of-coded-literals-only-where-they-save-51-bytes-again-with-item-4s-and-7s-prefetches).
:::

51 was chosen for level 2's reads, and level 3 took it as it was. On 31st October 2026 each level got
its own: 35 at level 2 stores 3.27 bytes per page less on ten samples of 20 000 pages, -0.33%, for the
same time on the Mi 9T's A76 and writes 0.12 µs slower on its A55; 19 at level 3, together with the 32
bytes of `OWN_SAVES_MIN`, 4.92 bytes less, -0.55%, for writes 0.07 to 0.14 µs faster and warm reads
0.13 µs slower on the A76 and 0.25 µs on the A55, two copies of each build against two of the one
before ([explored-designs.md](https://github.com/martinus/seqlz/blob/main/docs/explored-designs.md#the-literal-coding-thresholds-per-level-35-bytes-at-level-2-19-at-level-3-own-tables-from-32-33-and-49-bytes-per-page-less-kept)).
@assert ch11.coded_min == 35 and ch11.coded_min_own == 19
@end
@more {#ch-literals-enc--no-header} Why the rule does not count the header, measured
The rule does not count the header of `3 + w` bytes, nor the delta's byte, on purpose. When the sizes
shrank from 2 bytes to `w` bits, counting the smaller header would have coded 64.7% instead of 56.3% of
a desktop's pages, and on the phone's pages saved 2 bytes per page for 251 ns more per page written on
the A55, with 0.34 reads per write
([explored-designs.md](https://github.com/martinus/seqlz/blob/main/docs/explored-designs.md#stream-sizes-in-as-many-bits-as-the-largest-needs-kept)).
So the rule kept the old 19 bytes as its constant, which 51 replaced on 5th October 2026, and
{{ch11.coded_min}} and {{ch11.coded_min_own}} per level on 31st October.
@end

@@ 937-949
Only now, as the page gets coded, the raw literals are copied into `spare`. The streams start right
after the header, each where the one before ends.

The streams end before the bitstream starts: the rule guarantees that, together with the
`static_assert` below.

The test is on `spare`, not on `raw`, as the copy depends on `spare` alone: with deltas `spare` is
`NULL` and `raw` is the copy. When `seqlz_code_raw_literals()` still passed `spare = NULL` too, a
test on `raw` left a `memcpy()` to `NULL` in the code that gcc saw, although it never ran (commit
`c18ccf0`).
@anim inplace dst while the streams are written over the raw literals
@more {#ch-literals-enc--in-place} Why writing over the raw literals is safe
`memcpy()` returns its first argument, so with a `spare` the line copies the raw literals into it and
points `literals` there; pages that stay raw are never copied. With deltas, `spare` is `NULL` and
nothing is copied: `raw` is the copy of the deltas in the matcher's state. `q[0]` is right after the
header, and each stream starts where the one before ends. The first [`static_assert`](#b971) after
the function makes the comment's argument a compile-time fact:

```text
streams end at     d + header + coded,  header = 3 + w + e + desc
3 + w + e        <= {{ch11.header_max}} + 1 <= 2 + {{ch11.coded_min_own}} <= 2 + coded_min      (the static_assert)
desc + coded + coded_min < n - n/16 <= n                  (the rule, since the page is coded)
so header + coded < 2 + coded_min + n - coded_min = 2 + n,  and d + 2 + n is where the bitstream starts
```

So no stream byte lands on the bitstream. The same holds for level 2's copy out of the slots, whose
sizes passed the same rule: on [the page of letters](#ch-literals-enc--example) the streams end at
byte {{ch11.q.8}} and the bitstream starts at byte {{ch11.raw.bs}}. Every store of `lit_streams()`
stays between `q[0]` and `q[8]` ([`lit_flush()`](#b257)), and the u16, byte 2 and the sizes are
written after it, over bytes that were raw literals.

The copy is what makes the overwrite safe: the reader of the raw literals is not in `dst` at all. The
animation under the note runs `lit_streams()` in place from the copy on [chapter 5](#ch-trace)'s
page, with the table level 2 takes for it, a run of literals per step, with the copy drawn under the
bytes of `dst` the literals came from; that is how level 3 writes the pages it prices in full, and how
level 2 wrote every page until 31st October 2026. From the first run on, the later streams store over
raw literals that are read later, from the copy. The bitstream is the one thing no store may reach,
and none does.
@end
@more {#ch-literals-enc--why-spare} Why the matcher's table, and not a second buffer or the end of `dst`, measured
The first version moved the raw literals and the bitstream to the end of `dst` and wrote the coded
literals from its front, so all three had to fit at once. zram gives the compressor two pages, which
is plenty; zswap gives exactly one page, and there pages with coded literals failed to fit more
often, which cost 2.0% more memory. With the raw literals in the matcher's table it costs 0.23%, and
in the kernel VM writes got 0.12 µs faster. In zram's two pages the output is the same byte for byte,
checked on 80 000 desktop and phone pages
([explored-designs.md](https://github.com/martinus/seqlz/blob/main/docs/explored-designs.md#coded-literals-from-the-matchers-table-a-buffer-of-one-page-costs-01-to-05-instead-of-16-to-29-kept)).
@end

@@ 950-970
The u16 with the coded flag, byte 2 (table, width, delta flag), the delta's byte, the sizes and the
description; then the bitstream moves down to right after the coded literals.
@more {#ch-literals-enc--header} The u16 and byte 2 of three pages
The u16 gets `SEQLZ_LIT_CODED`: the {{ch11.n}} literals of [the page of letters](#ch-literals-enc--example)
are `{{ch11.u16hex}}`, the bytes `{{ch11.u16}}`. Byte 2 is the literals' code in bits 0 to 3,
`width - 5` in bits 4 to 6, and in bit 7 whether they are deltas, e.g. table 3 with width 7 is
`3 | 2 << 4 = 0x23`, the page's own table with width {{own.text.w}}
`8 | {{own.text.w5}} << 4 = {{own.text.t}}`, and the page of pointers, own table, width
{{delta.ptr.l3.w}} and deltas, `8 | 0 << 4 | 1 << 7 = {{delta.ptr.l3.t}}`. The delta's byte goes right
behind byte 2, at `SEQLZ_LIT_HEADER(0)`, byte 3, and the sizes behind it, one byte later with a delta
than without. The description comes after the sizes, at `SEQLZ_LIT_HEADER(width)` plus the delta's
byte, and `q[0]` already left room for all of them, since `header` counts them.
@assert delta.ptr.l3.w == 5
@end
@more {#ch-literals-enc--memmove} Why `memmove()`, and why the bitstream is copied twice, measured
`memmove()`, as the bitstream moves down by more than {{ch11.save_min}} bytes and is often longer than
that, so source and destination overlap. On [the page of letters](#ch-literals-enc--example),
{{ch11.bs}} bytes move from byte {{ch11.raw.bs}} to byte {{ch11.q.8}}, and the page goes from
{{letters.len}} to {{letters.coded.len}} bytes. The encoder copied the bitstream once already, from
the back of `dst`. Copying it only once, to behind the coded literals, made writes 0.2 µs slower in the
kernel VM and 0.8 µs on the A55, `seqlz-1` too, because gcc laid out the matcher's loop differently
([explored-designs.md](https://github.com/martinus/seqlz/blob/main/docs/explored-designs.md#the-bitstream-copied-once-behind-the-literals-raw-or-coded-writes-up-to-03-µs-slower-in-the-vm-and-08-µs-on-the-a55-not-kept)).
The copy of 400 to 600 bytes on a third of the pages stayed.
@end

@@ 971-986
The arguments of this file, checked by the compiler: the coded literals end before the bitstream at
either level's minimum, the raw literals' copy fits the matcher's table, and the sums fit their lanes.
@more {#ch-literals-enc--asserts} What the asserts rely on, in numbers
The first assert is the step of [why the streams never reach the bitstream](#ch-literals-enc--in-place)
that the compiler can check. The second keeps the copy free: `struct seqlz_state` is a union of the
matcher's table and `u8 literals[SEQLZ_BLOCK_SIZE]`, and with the table at least a page,
{{ch11.hash_bytes}} bytes on 4 KiB pages and {{k16.ch11.hash_bytes|grp}} on 16 KiB pages, the
literals cost no byte of work memory.

The other four are the bounds the sums and the counts rely on. A stream has at most an eighth of a
page of literals, of at most 10 bits each, {{ch11.stream_bits}} bits, and the 16-bit lanes of
[`lit_sums()`](#ch-literals-enc--pricing) hold that; `seqlz_delta_literals()` sums one choice over
every {{delta.step}}th literal of the page, and [the sample](#b825) one table over every
{{ch11.sample.step}}th, which the lanes also hold. [`own_code()`](#b625) takes bit 15 of a count for
its presence test, so no count may reach it: a page with a table of its own has at most
`OWN_LITERALS_MAX` literals.
@end

## Entry points

@@ 987-992
`struct own_work` lives in the matcher's state behind the copy of the literals.
@more {#ch-encoder--own-work} Where `struct own_work` lies in the matcher's state
For a table of the page's own, the coder needs `struct own_work`, {{own.enc.work_size|grp}} bytes of
counts, lengths, codes and the table's description ([own table](#ch-literals-enc--own)), and
`own_work()` puts it into the matcher's state at byte {{own.enc.work_at}}: `st->work` counts in `u64`,
so `OWN_LITERALS_MAX / 8U` of them are that byte. Below it is the copy of the raw literals or their
deltas ([what lies where](#ch-header--state)).
@end

@@ 993-1007
Level 3, with its minimum `SEQLZ_LIT_CODED_MIN_OWN`: with a delta, the deltas in the matcher's state
are coded; without, the literals in `dst`. If the deltas stay raw, the literals as they are are not tried again: that coded 1.4% of its tries for
0.71 bytes per page, and cost 0.39 µs per write on the Mi 9T's Cortex-A76.

If the deltas stay raw, `dst` still has the literals as they are, and they stay raw too. So a page
never stores deltas as raw literals; the format could not say so, as only coded literals have a byte 2.
@more {#ch-literals-enc--code-own} The second try of the plain literals, and why it went, measured
Until 24th October 2026 the literals as they are got a second try where the deltas stayed raw, with
the fixed tables and a table of the page's own. As the comment says, it coded few pages and saved
little: on the ten samples it ran on 26% of the pages and coded 1.4% of its tries, the means. Without
it level 3 stores 0.71 bytes per page more in zsmalloc memory, 0.079%, the mean of the ten samples,
and on the Mi 9T, zram in the phone's own kernel with 19 775 pages of a phone session, writes a page
0.39 µs faster on the A76 and 0.93 µs on the A55. That is about 2 bytes per µs on the A76, where the
deltas themselves gave about 22
([explored-designs.md](https://github.com/martinus/seqlz/blob/main/docs/explored-designs.md#level-3-without-its-second-try-of-the-plain-literals-071-bytes-per-page-more-writes-18-and-20-faster-on-the-mi-9t-kept),
commit `2fc645d`).
@end

@@ 1008-1017
Levels below 3: fixed tables only, `SEQLZ_LIT_CODED_MIN`, the matcher's table for the slots. Until
22nd October 2026 the coder was in `seqlz_compress.c`, a `static` function next to its callers.

@@ 1018-1029
The same for `seqlz_encode()`, whose caller has the raw literals. Its `st` may be `NULL`, so the slots
are a static buffer of the state's size: one thread at a time, which the tests and tools are. Not in the kernel copy.
