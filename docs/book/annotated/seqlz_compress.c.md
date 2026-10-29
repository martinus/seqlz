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
@more {#ch-encoder--example} The page of `ab`, bit by bit
[The format chapter](#ch-format--example) takes the {{ab.len}} bytes `{{ab.hex}}` of a page of `ab`
2048 times apart; here they come into being. The matcher finds {{ab.seqs}} sequences: at position 2
the slot of `ababa` was never written and holds 0, and the 4 bytes at 0 match, so
{{ch10.ab.s1.ll}} literals and a match at offset {{ch10.ab.s1.off}} to the end of the page,
{{ch10.ab.s1.ml}} bytes; the last sequence has 0 literals.
@assert ab.seqs == 2 and ab.lits == ch10.ab.s1.ll == 2 and ch10.ab.s1.ml == 4094

| step | what happens | `acc` after | `cnt` |
| --- | --- | --- | --- |
| literals | 16 bytes `abab...` copied to `d + 2`, `lit` moves on by 2 | | 0 |
| token and offset | token {{ch10.ab.s1.tok}} has no code: the escape's code `{{ch10.ab.s1.esccode}}` and the token's 12 bits `{{ch10.ab.s1.tokbits}}`, then the offset `{{ch10.ab.s1.offbits}}` | `{{ch10.ab.s1.acc}}` | {{ch10.ab.s1.cnt}} |
| `ml` value | code `{{ch10.ab.s1.mlv.code}}`, extra `{{ch10.ab.s1.mlv.xbits}}` | `{{ch10.ab.s1.mlv.acc}}` | {{ch10.ab.s1.mlv.cnt}} |
| flush | `{{ch10.ab.f1.word}}` stored at `end - 8`: `{{ch10.ab.f1.stored}}`; `p` = `end - {{ch10.ab.f1.p}}` | | {{ch10.ab.f1.cnt1}} |
| last sequence | 0 literals, no bits | | {{ch10.ab.f1.cnt1}} |
| finish | the byte below `p` is the last of the bitstream; {{ab.b.bytes}} bytes of bitstream; u16 = {{ab.lits}} | | |

`encoder_finish()` copies the {{ab.b.bytes}} bytes one at a time from `end - 1` down, `{{ab.b.hex}}`,
right after `61 62`: {{ab.len}} bytes. The 14 more bytes of `abab` the literal copy wrote are
overwritten by the bitstream or lie beyond the {{ab.len}} bytes returned. In the order the decoder
reads them, the {{ab.b.bits}} bits are `{{ch10.ab.s1.esccode}}` (the escape) `{{ch10.ab.s1.tokbits}}`
(token {{ch10.ab.s1.tok}}) `{{ch10.ab.s1.offbits}}` (offset {{ch10.ab.s1.off}}, sent as 1)
`{{ch10.ab.s1.mlv.code}}` (symbol {{ch10.ab.s1.mlv.sym}} of the `ml` table)
`{{ch10.ab.s1.mlv.xbits}}` ({{ch10.ab.s1.mlv.x}}). The steps one by one are in the panels of
[`enc_put_code()`](#ch-encoder--put), [`encode_seq()`](#ch-encoder--one-put) and
[`encoder_finish()`](#ch-encoder--finish).
@assert ab.b.bytes == ch10.ab.f1.moved + 1 and ab.b.pad == 8 - ch10.ab.f1.cnt1
@assert ch10.ab.s1.putbits + ch10.ab.s1.mlv.code + ch10.ab.s1.mlv.xbits == ab.b.bin

::: try Compress a page yourself
In the repository, save this as `ab.c` and build it with `gcc -O2 -Isrc -o ab ab.c src/seqlz_compress.c
src/seqlz_compress_literals.c src/seqlz_codes.c src/seqlz_default_tables.c src/seqlz_lit_sets.c`:

```c
#include "seqlz.h"
#include <stdio.h>
#include <stdlib.h>

static struct seqlz_state st;
static u8 page[SEQLZ_BLOCK_SIZE], dst[2 * SEQLZ_BLOCK_SIZE];

int main(void)
{
	unsigned int i, len;

	for (i = 0; i < SEQLZ_BLOCK_SIZE; i++)
		page[i] = i & 1 ? 'b' : 'a';
	len = seqlz_compress(&st, page, dst, sizeof(dst), 1);
	for (i = 0; i < len; i++)
		printf("%02x ", dst[i]);
	printf("\n");
	return 0;
}
```
@program ch10-ab

It prints:

@output ch10-ab

With level 2 or 3 instead of 1 the result is the same: {{ab.lits}} literals are too few to save the
{{ch11.coded_min}} bytes coded literals must save.
@assert ab.coded.len == ab.len == ab.l3.len
:::
@end

## The matcher's receiver

@@ 1-8
The two halves of a compressor in one file, and the matcher as a header, so that it is inlined into
the encoder's functions.

@@ 9-28
The simplest receiver of the matcher's sequences: it stores the three numbers into an array and
ignores the literals. Inlined into the matcher's loop like the encoder's receiver.

@@ 29-42
@anchor ch-encoder--find
`seqlz_find()` is not for zram: the table trainer counts the symbols of exactly these sequences, and a
fuzzer checks that `seqlz_encode()` of them gives the same bytes as `seqlz_compress()`. The kernel copy
does not have it. A level outside 1 to `SEQLZ_LEVELS` gives no sequences, as the other two entry
points give no page for it.

Two calls that differ only in `true` and `false` on purpose: with a constant in each, the compiler
inlines two copies of the matcher, and the one for levels 1 and 2 has no trace of level 3's lazy
matching, not even a test of `lazy` per match.

## Both ends of dst

@@ 43-82
@anchor ch-encoder
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
@more {#ch-encoder--layout} Both ends of `dst`, drawn, and why one pass
<figure class="diagram">
<div class="scroll">
<svg viewBox="0 0 720 222" role="img" aria-label="While encoding, the literals grow from the front of dst after 2 bytes left for the u16, and the bitstream grows backwards from the end of dst; at the end the bitstream is copied, turned around, to right after the literals.">
  <text x="20" y="26" font-size="12">while encoding</text>
  <rect x="20" y="34" width="40" height="36" fill="none" stroke="currentColor" stroke-width="1" stroke-dasharray="4 3"/>
  <rect class="fill-sheet" x="60" y="34" width="240" height="36" stroke="currentColor" stroke-width="1"/>
  <rect x="300" y="34" width="170" height="36" fill="none" stroke="currentColor" stroke-width="1"/>
  <g class="mark"><rect class="fill-sheet" x="470" y="34" width="230" height="36" stroke="currentColor" stroke-width="1.5"/></g>
  <g font-size="12" text-anchor="middle">
    <text x="40" y="56">u16</text>
    <text x="180" y="56">literals, in order →</text>
    <text x="385" y="56">free, ≥ ll + 32</text>
  </g>
  <g class="mark" font-size="12" text-anchor="middle"><text x="585" y="56">← bitstream, byte 0 last</text></g>
  <g font-size="11">
    <text x="20" y="86">d</text>
    <text x="60" y="86" text-anchor="middle">d+2</text>
    <text x="300" y="86" text-anchor="middle">lit</text>
    <text x="470" y="86" text-anchor="middle">p</text>
    <text x="700" y="86" text-anchor="end">d+dst_cap</text>
  </g>
  <text x="20" y="136" font-size="12">after encoder_finish()</text>
  <g class="acc"><rect class="fill-sheet" x="20" y="144" width="40" height="36" stroke="currentColor" stroke-width="1.5"/></g>
  <rect class="fill-sheet" x="60" y="144" width="240" height="36" stroke="currentColor" stroke-width="1"/>
  <g class="mark"><rect class="fill-sheet" x="300" y="144" width="230" height="36" stroke="currentColor" stroke-width="1.5"/></g>
  <rect x="530" y="144" width="170" height="36" fill="none" stroke="currentColor" stroke-width="1" stroke-dasharray="4 3"/>
  <g font-size="12" text-anchor="middle">
    <g class="acc"><text x="40" y="166" font-size="10.5">n_lit</text></g>
    <text x="180" y="166">literals</text>
    <text x="615" y="166">not part of the page</text>
  </g>
  <g class="mark" font-size="12" text-anchor="middle"><text x="415" y="166">bitstream, byte 0 first →</text></g>
  <g font-size="11">
    <text x="20" y="196">d</text>
    <text x="300" y="196" text-anchor="middle">d+2+n_lit</text>
    <text x="530" y="196" text-anchor="middle">d+len</text>
  </g>
  <g class="mark" fill="none" stroke="currentColor" stroke-width="1.5">
    <path d="M 585 72 C 585 112, 430 104, 418 140"/>
    <polygon class="fill-mark" points="417,144 413,134 422,136"/>
  </g>
  <g class="mark" font-size="11"><text x="716" y="106" text-anchor="end">copied, byte order</text><text x="716" y="120" text-anchor="end">turned around</text></g>
</svg>
</div>
<figcaption>The buffer while encoding, and after encoder_finish(): the bitstream moves from the back to right after the literals.</figcaption>
</figure>

Byte 0 of the bitstream, the first the decoder reads, is the last byte of `dst`, and the two ends grow
toward each other. The animation under the note does the same with a real page, the slab of task
records from [chapter 5](#ch-trace), one sequence per step; its buttons give `dst` the two pages of
zram, the smallest size the page fits into, and 1 byte less.

::: why One pass, any buffer
Two passes, the matcher into an array and then the encoder, were 6% slower
([measured](#ch-matcher--emit)). A second buffer for the bitstream needs more work memory per CPU.
Until 8th October 2026 the encoder wrote the bitstream forwards, behind room for a page of literals at
`d + 2 + 4096`, and moved it down at the end, which needed a `dst` of two pages. zram gives two pages,
zswap, the kernel's compressed cache in front of another swap device, gives one, and `lz4`, `lzo` and
`zstd` take any buffer and fail when the page does not fit
([#122](https://github.com/martinus/seqlz/issues/122)). The layout from both ends takes any `dst`,
writes the same bytes, and made writes in the kernel VM 0.20 µs faster per page; on the Mi 9T they
stayed within 0.22 µs
([explored-designs.md](https://github.com/martinus/seqlz/blob/main/docs/explored-designs.md#the-compressor-into-a-buffer-of-any-size-the-bitstream-from-the-back-the-same-bytes-in-zram-writes-2-faster-in-the-vm-kept),
commit `66aab11`).
:::

What this design gives up is the buffer of one page. The page is written with raw literals first, so a
page whose raw form does not fit fails, also where its coded form would. With a `dst` of exactly one
page, as zswap gives, that costs 0.2% more memory on desktop and phone pages
([explored-designs.md](https://github.com/martinus/seqlz/blob/main/docs/explored-designs.md#coded-literals-from-the-matchers-table-a-buffer-of-one-page-costs-01-to-05-instead-of-16-to-29-kept)).
zram gives two pages, and there it costs nothing.
@end
@more {#ch-encoder--room-check} The room check: 32 bytes, and a page that fails with room to spare
Before each sequence the literals, at level 3 also their [record](#ch-encoder--records), and
`ENC_ROOM` bytes more must fit between `lit` and `p`. The literal copies write up to 16 bytes past the
last literal, so the literals reach at most `lit + ll + 16`. A sequence flushes at most twice; the
first moves `p` down by at most 7 bytes, and the second stores 8 bytes below that, so the bits reach
no lower than `p - 15`. With `p - lit >= ll + 32` the two never meet, with 1 byte spare. Until 11th
October 2026 the comment counted this as 40 bytes, not 32
([#180](https://github.com/martinus/seqlz/issues/180)).

As the check is not exact, a page can fail although it would have fit with up to 31 bytes to spare:
[the page of pointers](#ch-matcher--back) compresses to {{pointers.len}} bytes and needs a `dst` of
{{ch10.ptr.min_dst}}.
@assert ch10.ptr.min_dst - pointers.len < ch10.enc_room and ch10.slab.min_dst - slab.len < ch10.enc_room
@end
@more {#ch-encoder--two-pages} Why two pages are always enough, in numbers
Every sequence but the last covers at least 4 bytes, and its token and offset take at most
`SEQLZ_SEQ_BITS`, 31 bits: 11 bits of code and 12 of offset, or an escaped token, whose escape code
`SEQLZ_MAX_ESCAPE_LEN` holds to 7 bits ([the escape](#ch-header--escape)); with today's
{{tok.len.3072}}-bit escape the real maximum is {{ch10.max_tok_off}}. Length values come only with long
sequences: an `ll` value, at most $8 + {{ch10.len_extra_max}} = {{ch10.len_bits_max}}$ bits, only with
15 literals or more, which alone are worth $15 / 4 \times 31 = 116$ bits of the budget, and an `ml`
value only with a match of 35 bytes or more, worth 271. The last sequence has no bits. So:

| | 4 KiB pages | 16 KiB pages |
| --- | --- | --- |
| bits at most, `SEQLZ_BLOCK_SIZE / 4 * 31` | ${{ch10.bound.seqs.tex}} \times 31 = {{ch10.bound.bits.tex}}$ | ${{k16.ch10.bound.seqs.tex}} \times 31 = {{k16.ch10.bound.bits.tex}}$ |
| bytes of bitstream at most | {{ch10.bound.bytes|grp}} | {{k16.ch10.bound.bytes|grp}} |
| the u16 + every byte a literal + that bitstream | $2 + {{ch10.bound.page.tex}} + {{ch10.bound.bytes.tex}} = {{ch10.bound.total.tex}}$ | $2 + {{k16.ch10.bound.page.tex}} + {{k16.ch10.bound.bytes.tex}} = {{k16.ch10.bound.total.tex}}$ |
| two pages | {{ch10.bound.two|grp}} | {{k16.ch10.bound.two|grp}} |
| left over, must be ≥ `ENC_ROOM` | {{ch10.bound.left}} | {{k16.ch10.bound.left}} |
@assert min(F("ch10.bound.left"), F("k16.ch10.bound.left")) >= ch10.enc_room

The bound adds the most literals and the most bits, which no page has at once. It holds without
records: at level 3 they take {{delta.rec}} bytes more per sequence, and a page that does not fit with
them is matched again without them ([`compress_own()`](#b427)).
@end

@@ 83-96
The comment above, checked by the compiler. A change of a table or a constant that breaks the
argument now stops the build; until 23rd October 2026 it was only in the comment, which had been wrong
once (it counted 40 bytes instead of 32), commit `75756b7`.

`ENC_BITSTREAM_MAX` is 31 bits for every 4 bytes of the block, and the u16, a block of literals, that
bitstream and `ENC_ROOM` fit `SEQLZ_COMPRESS_BOUND`, two pages.
@more {#ch-encoder--asserts} The accumulator's 58 bits and the 8098 bytes the `static_assert`s check
After a flush at most 7 bits are left, so token, offset and a match length value fit before the next
one: $7 + 31 + 20 = 58$ bits, 60 on 16 KiB pages, where the comment counts 22 per value. The 20 has a
bit to spare: a value is at most 8 bits of code and {{ch10.len_extra_max}} extra bits,
{{ch10.len_bits_max}}, {{k16.ch10.len_bits_max}} on 16 KiB pages; the tables had one more symbol with
one more extra bit, which no page needs, until 11th October 2026
([#182](https://github.com/martinus/seqlz/issues/182)). A literal length value would add as many bits
again, so it gets a flush of its own ([when the flushes come](#ch-encoder--flushes)).

`ENC_VALUE_BITS` is generous, a code of 8 bits and `SEQLZ_BLOCK_BITS` more, the 20 bits of the
accumulator's 58 on 4 KiB pages. The copies and the flushes then take
$16 + \lfloor 58 / 8 \rfloor + 8 = 31 \le 32$ bytes, and the u16, a page of literals, the largest
bitstream and `ENC_ROOM` take $2 + 4096 + {{ch10.bound.bytes.tex}} + 32 = 8098$ bytes, which fit
`SEQLZ_COMPRESS_BOUND`.
@assert F("ch10.bound.total") + ch10.enc_room == 8098 and F("ch10.bound.two") == 8192 and ch10.enc_room == 32
@end

@@ 97-110
@anchor ch-encoder--state
`acc` holds the bits not yet written, `cnt` of them count; bits above them may be old ones already
written, and are shifted out before they matter. `p` is where the bitstream ends at the bottom, `lit`
where the next literal goes. `src_end` is how far the 16-byte literal copies may read: the end of the
page in the compressor, the end of the caller's literals in `seqlz_encode()`. `last` is the repeat
offset, starting at 1 as in the matcher and the decoder.

The matcher leaves the encoder no registers, so this struct lives on the stack
([explored-designs.md](https://github.com/martinus/seqlz/blob/main/docs/explored-designs.md#the-compressor-second-round-fewer-instructions)).
`rec` and `pos` are level 3's, for the records before the literals; `pos` is where the next sequence
starts in the page.

## Bits

@@ 111-118
Appends `n` bits at the bottom of the accumulator. `v` has no bits above `n`, and `n < 64 - cnt`
keeps the bits that count from falling out of the top. The `& 63U` changes no value; it lets the
compiler drop the check that `CONFIG_UBSAN_SHIFT` would add before a shift it cannot prove in range
([the masks](book:#ch-internal--masks)).

@@ 119-135
A code from an `enc[]` entry (`code | length << 16`) and its extra bits in one put, the code in the
higher bits, since the decoder reads the most significant bit first. `& 31U` is for UBSan again.
@more {#ch-encoder--put} The `ml` value of the page of `ab`, through these lines
The page of `ab` repeated has a match of {{ch10.ab.s1.ml}} bytes, so `ml - 4` is {{ch10.ab.s1.ml4}} and
the length value ${{ch10.ab.s1.ml4}} - 31 = {{ch10.ab.s1.mlv.v}}$: symbol {{ch10.ab.s1.mlv.sym}} with
{{ch10.ab.s1.mlv.extra}} extra bits, whose {{ch10.ab.s1.mlv.len}}-bit code in the `ml` table is
`{{ch10.ab.s1.mlv.code}}`, entry `{{ch10.ab.s1.mlv.entry}}`:
@assert ch10.ab.s1.mlv.v == 4059 and ch10.ab.s1.mlv.code == ml.code[23]

| | value |
| --- | --- |
| `extra & ((1U << 11) - 1U)` | {{ch10.ab.s1.mlv.v}} & 2047 = {{ch10.ab.s1.mlv.x}} = `{{ch10.ab.s1.mlv.xbits}}` |
| `(u64){{ch10.ab.s1.mlv.codehex}} << 11` | `{{ch10.ab.s1.mlv.shifted}}` |
| value put | <code>{{ch10.ab.s1.mlv.shifted}} &#124; {{ch10.ab.s1.mlv.x}}</code> = `{{ch10.ab.s1.mlv.put}}`, {{ch10.ab.s1.mlv.n}} bits: `{{ch10.ab.s1.mlv.bits}}` |

The top bit of 4059, the 2048, is not sent: the symbol says it.
@end

@@ 136-149
Whole bytes of the accumulator go below `p`. The shift puts the `cnt` valid bits on top of the word,
and a little-endian store writes the top byte last, at `p - 1`, so read from the end of `dst`
backwards the bits come in the order the decoder wants.

`p` moves down by the whole bytes; the at most 7 bits left over stay in `acc` and are written again,
with more bits, by the next flush. For `cnt` 0 the shift would be by 64, which the `& 63U` turns into
a shift by 0: the old word is written once more, and the next flush overwrites it.
@anim accum The accumulator, put by put and flush by flush, for a real page
@more {#ch-encoder--flush} The first two sequences of the page of pointers, flush by flush
On [the page of pointers](#ch-matcher--back):

- Sequence 1, {{ch10.ptr.s1.ll}} literals and {{ch10.ptr.s1.ml}} bytes at offset
  {{ch10.ptr.s1.off}}, is token {{ch10.ptr.s1.tok}}, code `{{ch10.ptr.s1.code}}`, and the offset
  `{{ch10.ptr.s1.offbits}}` in {{ch10.ptr.s1.raw}} bits: `cnt = {{ch10.ptr.s1.cnt}}`. The flush
  stores `acc << {{ch10.ptr.f1.shift}}` = `{{ch10.ptr.f1.word}}` at `p - 8`, which puts
  `{{ch10.ptr.f1.e1}}`, the first 8 bits, at `p - 1` and `{{ch10.ptr.f1.e2}}`, the 6 bits left and two
  0s, at `p - 2`. `p` moves down by {{ch10.ptr.f1.moved}}, `cnt` is {{ch10.ptr.f1.cnt1}}.
- Sequence 2, {{ch10.ptr.s2.ll}} literal and {{ch10.ptr.s2.ml}} bytes at the repeat offset, is token
  {{ch10.ptr.s2.tok}}, code `{{ch10.ptr.s2.code}}`, no offset bits. The flush writes
  `{{ch10.ptr.f2.e2}}` at the new `p - 1`, the same byte as before, now with all its bits, and
  `{{ch10.ptr.f2.e3}}` below it. `p` moves down by {{ch10.ptr.f2.moved}}, `cnt` is
  {{ch10.ptr.f2.cnt1}}.
@assert ch10.ptr.s1.code + ch10.ptr.s1.offbits == "10101011" + "000001" and ch10.ptr.f2.e3 == "00" and ch10.ptr.s2.code == tok.code[49] and ch10.ptr.f1.cnt1 == 6 and ch10.ptr.f2.cnt1 == 2

The animation under the note does the same, put by put and flush by flush, for the first 4 sequences
of [chapter 5](#ch-trace)'s page; its values come from the encoder's own helpers, through
`anim/accum.c`, which checks them against `encode_emit()`.

A flush with `cnt` 0 does happen: a sequence with an `ll` value and no `ml` value flushes twice, the
second time possibly with no bits. Before commit `66aab11` the bitstream went forwards and the flush
swapped the bytes of each word; now the swap is in [`encoder_finish()`](#b304), once per page.
@end

@@ 150-160
A length value as its symbol's code plus extra bits. The symbol is masked to the 32 entries of `enc[]`
for UBSan; `v` goes in whole, `enc_put_code()` keeps only its extra bits.

@@ 161-177
A token's code. An entry of 0 means "no code of its own", since every code has a length of at least 1
in the top bits. Then the token is escaped: the escape's code, then the token's number in 12 bits,
together one code ([escape](book:#ch-format--token)).
@more {#ch-encoder--codes} Token 10, escaped, step by step
{{ch10.tok_codes}} of the {{const.TOKEN_SYMBOLS}} tokens have a code of their own
([an entry of 0](#ch-internal--token-table)); token 10, `ll` 10 and a match of 4 bytes at the repeat
offset, has none:
@assert ch10.esc10.enc == 0 and tok.len[10] == 0

| | value |
| --- | --- |
| `ee = seqlz_tables.token.enc[3072]` | `{{ch10.esc.entry}}`: length {{ch10.esc.len}}, code `{{ch10.esc.codehex}}` |
| `*len` | {{ch10.esc.len}} + 12 = {{ch10.esc10.len}} |
| returned | <code>{{ch10.esc.codehex}} &lt;&lt; 12 &#124; 10</code> = `{{ch10.esc10.ret}}`: `{{ch10.esc10.bits}}` |
@end

## encode_seq(), one sequence

@@ 178-200
The room check before anything is written. A failed check sets `p` to `lit`, and that sticks: every
later sequence fails it too, and `encoder_finish()` sees `p == lit` and returns 0. The matcher runs on
to the end of the page anyway, which costs nothing with zram's two pages, where it never happens.

`rec` is a constant at every call, so the copy of the loop for levels 1 and 2 has none of level 3's
record code.
@more {#ch-encoder--emit} When a record is written, and why the room cannot go negative
`n_rec` is the record's size, {{delta.rec}} bytes, or 0. A record needs three things: `rec`, `true`
from the callers that write records, [`encode_emit_rec()`](#b253) and [`encode_raw()`](#b362), and
`false` from `encode_emit()`; `e->rec`, which is false in the second pass without records; and `ll`
not 0, because a sequence without literals has no deltas. The room check counts the record too.

The cast to `size_t` makes the compare unsigned, so a negative room would look huge. `p` never goes
below `lit`, because both callers reject a `dst_cap` below {{ch10.dst_min}}, and a failed check stops
at `p == lit`.
@end

@@ 201-208
Level 3 only: a record of 6 bytes before the literals, with where the sequence starts in the page, how
many literals it has, and the offset before it. Level 3's deltas need those, and they are not in the
literals; `seqlz_delta_literals()` reads the records once the page is matched
([records](book:#ch-encoder--records)).
@more {#ch-encoder--record-bytes} The first two records of the page of pointers
The record is the position of the sequence's first literal, `e->pos`, the number of literals, and
`e->last`, the offset of the sequence before, which the end of `encode_seq()` sets to `off` only after
the record is written. Then `lit` moves on, so the literals come right after their record. `pos` moves
on by the whole sequence, literals and match, whenever `rec` is the constant `true`, also without
records, where nobody reads it. The first two records of [the page of pointers](#ch-matcher--back), as
the bytes in `dst`:

| record | bytes | `pos` | `ll` | `last` |
| --- | --- | --- | --- | --- |
| 0 | `{{delta.ptr.rec.0.hex}}` | {{delta.ptr.rec.0.pos}} | {{delta.ptr.rec.0.ll}} | {{delta.ptr.rec.0.last}} |
| 1 | `{{delta.ptr.rec.1.hex}}` | {{delta.ptr.rec.1.pos}} | {{delta.ptr.rec.1.ll}} | {{delta.ptr.rec.1.last}} |

The first sequence takes the first pointer and the low byte of the second as literals, and copies 7
bytes from 8 back, so the second starts at {{delta.ptr.rec.1.pos}} with `last` 8.
@assert delta.ptr.rec[0].pos == 0 and delta.ptr.rec[0].last == 1 and delta.ptr.rec[1].pos == delta.ptr.rec[0].ll + 7 and delta.ptr.rec[1].last == 8
@end

@@ 209-229
The literals: one 16-byte copy without a loop, since most sequences have few literals (1 in the page
of pointers) and a loop's exit branch could mispredict, then more 16-byte copies, and bytes only near
the end of the input. The bytes past `ll` land in `ENC_ROOM`, and the next sequence's literals
overwrite them, the wild copy again. `__builtin_memcpy()` with a constant 16 is always inlined, also
with `CONFIG_FORTIFY_SOURCE` ([why the builtin](seqlz_internal.h.html#b12)).

The last sequence (`ml == 0`) has only its literals and no bits. Until 11th October 2026 it had a
token and maybe an `ll` value, about 9 bits per page ([how a page ends](book:#ch-format--end)).

@@ 230-252
Token and offset in one put: the offset bits right after the token's code. `0U - (cls != 0)` is all
ones for classes 1 to 5 and 0 for class 0 (the repeat offset), which sends no bits.

An `ll` value gets a flush of its own, and every sequence ends with one, so the accumulator never
overflows: at most 7 + 31 + 20 = 58 bits before a flush. `last` changes only after the record above
used the old one.
@more {#ch-encoder--seq176} Sequence 176 of the page of task records, line by line
The figure takes sequence 176 of [chapter 5](#ch-trace)'s page through these lines: a new offset of
class {{ch10.slab.s176.cls}}, a token with a code of its own, and an `ll` value. The animation of
`dst` under the note on [both ends](#b43) counts the same {{ch10.slab.s176.nbits}} bits for it;
`facts/ch10_12.c` reads them back from `dst` and checks them against the codes put.
@assert ch10.slab.s176.tlen == 8 and ch10.slab.s176.raw == 12 and ch10.slab.s176.llv.len == 3 and ch10.slab.s176.llv.extra == 4 and ch10.slab.s176.escaped == 0 and ch10.slab.s176.ll >= 15 and ch10.slab.s176.mlf < 31 and ch10.slab.s176.seqbits == ch10.slab.s176.code + ch10.slab.s176.offbits + ch10.slab.s176.llv.code + ch10.slab.s176.llv.xbits

<figure class="diagram">
<div class="scroll">
<svg viewBox="0 0 720 340" role="img" aria-label="Sequence 176 of chapter {{chapter:ch-trace}}'s page through encode_seq(): its offset class, its token, the token's code, the offset bits, the ll value, and the 27 bits they make" font-size="12">
  <text x="10" y="16" font-weight="bold">sequence 176 of chapter {{chapter:ch-trace}}'s page: ll = {{ch10.slab.s176.ll}}, ml = {{ch10.slab.s176.ml}}, off = {{ch10.slab.s176.off}}, e-&gt;last = {{ch10.slab.s176.last}}</text>
  <text x="10" y="48" class="mut" font-size="11">1</text>
  <text x="28" y="48">seqlz_off_class({{ch10.slab.s176.off}}, {{ch10.slab.s176.last}}, &amp;raw_bits)</text>
  <text x="408" y="48"><tspan class="acc" font-weight="bold">class {{ch10.slab.s176.cls}}</tspan>, raw_bits = <tspan class="mark" font-weight="bold">{{ch10.slab.s176.raw}}</tspan></text>
  <text x="10" y="74" class="mut" font-size="11">2</text>
  <text x="28" y="74">seqlz_token({{ch10.slab.s176.ll}}, {{ch10.slab.s176.ml}}, {{ch10.slab.s176.cls}})</text>
  <text x="408" y="74">{{ch10.slab.s176.llf}} + 16 * {{ch10.slab.s176.mlf}} + 512 * {{ch10.slab.s176.cls}} = <tspan font-weight="bold">{{ch10.slab.s176.tok}}</tspan></text>
  <text x="10" y="100" class="mut" font-size="11">3</text>
  <text x="28" y="100">token_code({{ch10.slab.s176.tok}}, &amp;tlen)</text>
  <text x="408" y="100">token.enc[{{ch10.slab.s176.tok}}]: code <tspan class="acc" font-weight="bold">{{ch10.slab.s176.code}}</tspan>, tlen = {{ch10.slab.s176.tlen}}</text>
  <text x="10" y="126" class="mut" font-size="11">4</text>
  <text x="28" y="126">enc_put(e, code &lt;&lt; {{ch10.slab.s176.raw}} | ({{ch10.slab.s176.off}} &gt;&gt; {{ch10.slab.s176.shift}}), {{ch10.slab.s176.tlen}} + {{ch10.slab.s176.raw}})</text>
  <text x="408" y="126">{{ch10.slab.s176.putn}} bits: offset bits <tspan class="mark" font-weight="bold">{{ch10.slab.s176.offbits}}</tspan></text>
  <text x="10" y="152" class="mut" font-size="11">5</text>
  <text x="28" y="152">ll &gt;= 15: put_len_value(e, &amp;seqlz_tables.ll, {{ch10.slab.s176.llv.v}})</text>
  <text x="408" y="152">symbol {{ch10.slab.s176.llv.sym}}: code <tspan font-weight="bold">{{ch10.slab.s176.llv.code}}</tspan>, extra <tspan class="mut" font-weight="bold">{{ch10.slab.s176.llv.xbits}}</tspan></text>
  <text x="10" y="178" class="mut" font-size="11">6</text>
  <text x="28" y="178">enc_flush(e)</text>
  <text x="408" y="178">the whole bytes of acc go below p</text>
  <text x="10" y="204" class="mut" font-size="11">7</text>
  <text x="28" y="204">ml - 4 = {{ch10.slab.s176.ml4}}, below 31: no ml value</text>
  <text x="408" y="204">enc_flush(e), then e-&gt;last = {{ch10.slab.s176.off}}</text>
  <line x1="396" y1="32" x2="396" y2="214" stroke="currentColor" stroke-width="0.6" class="mut"/>
  <text x="10" y="244" font-weight="bold">the {{ch10.slab.s176.nbits}} bits of the sequence, in the order the decoder reads them</text>
  <g class="acc"><rect x="28" y="258" width="176" height="28" class="fill-acc" fill-opacity="0.3" stroke="currentColor"/></g>
  <text x="35" y="277" textLength="162" lengthAdjust="spacing" font-weight="bold" font-size="13">{{ch10.slab.s176.code}}</text>
  <text x="116.0" y="304" text-anchor="middle" font-size="11">token {{ch10.slab.s176.tok}}</text>
  <g class="mark"><rect x="204" y="258" width="264" height="28" class="fill-mark" fill-opacity="0.3" stroke="currentColor"/></g>
  <text x="211" y="277" textLength="250" lengthAdjust="spacing" font-weight="bold" font-size="13">{{ch10.slab.s176.offbits}}</text>
  <text x="336.0" y="304" text-anchor="middle" font-size="11">offset {{ch10.slab.s176.off}}</text>
  <g><rect x="468" y="258" width="66" height="28" class="fill-sheet" stroke="currentColor"/></g>
  <text x="475" y="277" textLength="52" lengthAdjust="spacing" font-weight="bold" font-size="13">{{ch10.slab.s176.llv.code}}</text>
  <text x="501.0" y="304" text-anchor="middle" font-size="11">symbol {{ch10.slab.s176.llv.sym}}</text>
  <g class="mut"><rect x="534" y="258" width="88" height="28" class="fill-paper" stroke="currentColor"/></g>
  <text x="541" y="277" textLength="74" lengthAdjust="spacing" font-weight="bold" font-size="13">{{ch10.slab.s176.llv.xbits}}</text>
  <text x="578.0" y="304" text-anchor="middle" font-size="11">low {{ch10.slab.s176.llv.extra}} of {{ch10.slab.s176.llv.v}}</text>
  <text x="116.0" y="320" text-anchor="middle" font-size="11" class="mut">{{ch10.slab.s176.tlen}} bits</text>
  <text x="336.0" y="320" text-anchor="middle" font-size="11" class="mut">{{ch10.slab.s176.raw}} bits</text>
  <text x="545.0" y="320" text-anchor="middle" font-size="11" class="mut">the ll value, {{ch10.slab.s176.llv.n}} bits</text>
</svg>
</div>
<figcaption>Sequence 176 of chapter {{chapter:ch-trace}}'s page through <code>encode_seq()</code>, from its <code>ll</code>, <code>ml</code> and offset to the bits that go into <code>acc</code>, from the real encoder's helpers.</figcaption>
</figure>
@end
@more {#ch-encoder--one-put} Token and offset in one put, measured
The first sequence of [the page of `ab`](#ch-encoder--example) is {{ch10.ab.s1.ll}} literals and a
match at offset {{ch10.ab.s1.off}}, with `last` 1: class {{ch10.ab.s1.cls}}, {{ch10.ab.s1.raw}} bit,
token $2 + 16 \times 31 + 512 \times 1 = {{ch10.ab.s1.tok}}$.
@assert ch10.ab.s1.ll == 2 and ch10.ab.s1.cls == 1 and ch10.ab.s1.raw == 1 and ch10.ab.s1.tok == 2 + 16 * 31 + 512

The offset is shifted right by `SEQLZ_OFF_SHIFT(cls)` and ANDed with `0U - (cls != 0)`. Without the
mask, class 0 would OR the offset into the low bits of the token's code. The other classes need no
mask: `seqlz_off_class()` picks a class whose bits hold the offset. Masking only class 0, instead of
every offset to `raw_bits` bits, came with commit `996248b`, with no number of its own. One put instead
of two took 21 786 to 21 233 cycles per page on x86-64
([explored-designs.md](https://github.com/martinus/seqlz/blob/main/docs/explored-designs.md#the-compressor-second-round-fewer-instructions)).
For the page of `ab`: `{{ch10.ab.s1.codehex}} << {{ch10.ab.s1.raw}} | ({{ch10.ab.s1.off}} >> 1) = {{ch10.ab.s1.put}}`,
{{ch10.ab.s1.putn}} bits.
@assert ch10.ab.s1.off >> 1 == int(ch10.ab.s1.offbits, 2)
@end
@more {#ch-encoder--flushes} When the flushes come
An `ll` value gets a flush of its own, and every sequence ends with one, so the budget holds at every
put:

| put | `cnt` before at most | bits at most | `cnt` after at most |
| --- | --- | --- | --- |
| token and offset | 7 | 31 | 38 |
| `ll` value | 38 | 20 | 58, then a flush leaves 7 |
| `ml` value | 38 | 20 | 58 |

Without the flush between them, an `ll` and an `ml` value after a long escaped token could reach
$7 + 31 + 20 + 20 = 78$ bits.
@end

## Receivers, start and finish

@@ 253-267
The matcher calls a receiver with a `void *`, the signature of [`emit_fn`](book:#ch-matcher--emit).
`rec` has to be a constant at the call, so there are two receivers, one per constant: levels 1 and 2
get `encode_emit()`, level 3 `encode_emit_rec()`. With the records in a test per sequence instead,
levels 1 and 2 wrote pages about 6% slower on the Mi 9T's Cortex-A76.
@more {#ch-encoder--wrappers} Level 3's records in a copy of the loop of their own, measured
The first version, with the XOR that came before the deltas, checked `e->rec` for every sequence in
the loop all levels share, and the decoder its XOR the same way. Levels 1 and 2 then wrote pages about
6% slower on the Mi 9T's Cortex-A76 and read them slower too, userspace, 21st October 2026
([explored-designs.md](https://github.com/martinus/seqlz/blob/main/docs/explored-designs.md#level-3-xors-its-literals-with-an-earlier-byte-of-the-page-38-fewer-bytes-than-seqlz-3-of-15-october-kept)).
With the records only in the lazy matcher's copy, and the decoder's loop copied for pages with deltas
([the decoder's two copies](#ch-decode--deltas)), levels 1 and 2 write and read as fast as before, within the
run's spread.
@end

@@ 268-277
@anchor ch-encoder--init
The compound literal zeroes every field it does not name: `acc`, `cnt`, `rec` and `pos` start at 0.
`p` starts at the end of `dst`, `lit` behind the 2 bytes of the u16, which is written last. Level 3
sets `rec` after the call; a second `encoder_init()`, for a page that did not fit with its records,
clears it again.

@@ 278-303
`p == lit` only after a failed room check. The bits left over are in memory already: the last flush
wrote them on top of the byte below `p`, with zeros below them, exactly the zero padding the format
wants. So `p` only moves down by one to count that byte. Then the u16, the number of raw literals.
@more {#ch-encoder--finish} The last byte, written once, and the u16 of the page of pointers
`p == lit` only after a failed room check, since a successful one leaves at least 16 bytes between
them. The zeros below the last bits are exactly the end [a valid page](#ch-format--validity) has.
Until 11th October 2026 a store here wrote that byte a second time; without it the bytes were the
same on the 600 pages of `test/seqlz_endian.c` and on both desktop dumps (commit `c9db4e5`,
[#180](https://github.com/martinus/seqlz/issues/180)).

The u16 for [the page of pointers](#ch-matcher--back) is {{pointers.lits}} literals,
`{{ch10.ptr.u16}}`, with `SEQLZ_LIT_CODED` 0; `seqlz_code_literals()` rewrites it if the literals get
coded.
@end

@@ 304-328
Byte `k` of the bitstream is at `end - 1 - k`. A little-endian load and a big-endian store reverse 8
bytes at once, a load, a byte swap and a store on x86-64.

Only a page that nearly fills `dst` has the two places overlap; then the bitstream is reversed in place
and moved down with `memmove()`.
@more {#ch-encoder--reverse} The last 8 bytes of the page of pointers, turned around
A little-endian load of the 8 bytes at `end - 8 - k` puts the byte at the highest address on top, and
a big-endian store writes the top byte to the lowest address, so the 8 bytes come out reversed: with
`gcc -O2` for x86-64 a load, a `bswap` and a store, with `clang -O2` for arm64 a `rev`. For
[the page of pointers](#ch-matcher--back) the last 8 bytes of `dst` are `{{ch10.ptr.last8}}`, read as
`{{ch10.ptr.le64}}` and written as `{{ch10.ptr.head8}}`, the bitstream's first 8 bytes in order.

The animation of `dst` under the note on [both ends](#b43) does all of this for
[chapter 5](#ch-trace)'s page, whose smallest `dst` takes the path that overlaps.
@assert ch10.slab.min_dst - slab.len < slab.b.bytes
@end

## encode_raw() and seqlz_encode()

@@ 329-361
The encoder for sequences from outside, for tests and tools. The sequences are checked by the rules a
decoder checks before a byte is written.

A match below 4 bytes is not only invalid but dangerous: `ml - 4` wraps, a huge length value follows,
and the room proof no longer holds. Before commit `256768e` 1025 such sequences wrote past the two
pages of `dst` ([why](book:#ch-encoder--raw)).
@more {#ch-encoder--raw} The checks before a byte is written, and the bug they stop
A `dst` below {{ch10.dst_min}} bytes would start `p` below `lit + 32`, or below `lit`, where the room
check's cast would turn a negative room into a huge one. The sequences are checked by the rules
[the decoder](#ch-decode) checks.

`encode_seq()` computes `ml - SEQLZ_MIN_MATCH` unsigned. For `ml` 3 that wraps to
{{ch10.wrap.ml4|grp}}, so a length value follows with {{ch10.wrap.extra}} extra bits, which breaks the
accumulator's budget and the 31 bits per 4 bytes of the room proof. Before commit `256768e` the
encoder checked only that the literals were there, and 1025 such sequences wrote past the two pages of
`dst`; AddressSanitizer reported it in `enc_flush()`
([#121](https://github.com/martinus/seqlz/issues/121)). None of the fuzzers had found it, because
each reached `seqlz_encode()` with valid sequences only. `fuzz/seqlz_encode_fuzz.c` gives it raw
sequences now, and failed within seconds against the old encoder. `seqlz_compress()` was never
affected: its matcher makes only valid sequences.
@end

@@ 362-385
The same loop as the matcher's, over the given sequences; `src_end` is the end of the caller's
literals, and the last sequence gets `ml` 0 whatever its `match` says. At level 3 with the records, and
once more without them if the page does not fit, as `compress_own()` does: `rec` is the constant
`true` and `e.rec` decides, as speed does not matter here. `seqlz_delta_literals()` then writes the
literals' deltas to `deltas`, and `*delta` is the delta it picked, or 0.

@@ 386-408
The public entry for sequences from outside. The level is checked before anything is written, so a
level outside 1 to `SEQLZ_LEVELS`, or level 3 without the state, returns 0 as
[the header](seqlz.h.html#b83) says. At level 2 it codes the literals where the caller has them, since
its state may be `NULL`; until 23rd October 2026 it copied them into a block on its stack, a frame of
4240 bytes where `src/` is built into a kernel (commit `d8937df`). At level 3 with the deltas and
maybe a table of the page's own. The kernel copy does not have this function.

## The compressor

@@ 409-426
Levels 1 and 2: the greedy matcher with `encode_emit`, the whole compression loop once inlined.

The check of `dst_cap` is here and in `compress_own()`, not once in `seqlz_compress()`: there it took
1% more instructions at levels 1 and 2, on 2000 pages of a desktop dump with gcc 16.2 on x86-64, 23rd
October 2026 (commit `75756b7`).

@@ 427-467
Level 3: the lazy matcher with records. A page that does not fit with its records is matched once
more without them, as `encoder_init()` clears `e.rec`; a loop and not two calls, so the inlined matcher
is in the function once. Then `seqlz_delta_literals()` picks the delta, moves the literals down over
the records and writes their deltas to `st->literals`, and `encoder_finish()` finds `dst` as it would
without records.

`noinline` on purpose: inlined next to `compress_block()`, levels 1 and 2 wrote their pages slower.

@@ 468-489
What zram calls. `dst_cap` is cut to `SEQLZ_COMPRESS_BOUND`, since the bitstream starts at the end of
`dst`: with a larger `dst` it would land far from the page, in memory nothing else of it touches.

Level 1 stops after `compress_block()`. Level 2 codes the literals with one of 8 fixed tables, using
the matcher's 8 KiB hash table as the copy of the raw literals, so it needs no memory of its own.
Level 3 hands the delta to `seqlz_code_own()`.
@more {#ch-encoder--entry} Three levels on two pages, and a `dst` larger than two pages
`seqlz_code_literals()` codes the literals with one of the 8 fixed literal tables if that saves
enough, and returns the new length or `len` unchanged; `seqlz_code_own()` does the same at level 3,
also with a table of the page's own. Both read the literals from a copy while they write the coded
ones over the raw ones in `dst` ([in place](#ch-literals-enc--in-place)). Level 2's copy is
`st->literals`, the matcher's 8 KiB table as bytes through the union in `struct seqlz_state`
([what lies where](#ch-header--state)); `seqlz_code_own()` also uses
[`own_work(st)`](seqlz_compress_literals.c.html#b869), the upper half of the same 8 KiB.

The text page of [chapter 4](#ch-format--own) shows what the levels do to one page: {{words.len}}
bytes at level 1, {{words.coded.len}} at level 2 and {{words.l3.len}} at level 3, without deltas; the
page of pointers {{pointers.len}} bytes at level 1, {{delta.ptr.l2.len}} at level 2 and
{{delta.ptr.l3.len}} at level 3, with them.
@assert words.l3.delta == 0 and pointers.l3.delta == 1
@assert pointers.len == delta.ptr.l2.len

Every page fits into `SEQLZ_COMPRESS_BOUND` ([two pages](#ch-encoder--two-pages)), and until 24th
October 2026 a larger `dst` was not cut: with a `dst` of 64 KiB the bitstream went 56 KiB past the
page, in memory the page has nothing else in (commit `8fed16a`). zram passes exactly two pages, so its
pages did not change.

The kernel's copy keeps `compress_block()`, `compress_own()` and `seqlz_compress()`, renamed to
`seqlz_compress_block()`, as the kernel's interface function is called `seqlz_compress()`, which tests
the level instead ([the interface](#ch-zram-glue--api)).
@end
