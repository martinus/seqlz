# Level 3's bytes: a table of the page's own, deltas {#ch-format3}

Level 3 adds two things to [level 2's coded literals](#ch-format2): code 8 in byte 2, a literal
table that the page brings, and bit 7, literals stored as deltas to earlier bytes of the page. Both
sit in the header of the coded literals; the sequences are level 1's. This chapter is the part of
[the format](#ch-format) that level 3 adds; the code follows.

## A page's own literal table, in byte order {#ch-format--own}

With code 8 the page brings its own literal table, right after `dt` and the stream sizes, at
byte $3 + w + e$. It is
read most significant bit first, like `B`. It is not a list of 256 code lengths: it is a list of
symbols of one more fixed table, `OWN`, which goes through the byte values from 0 up and says for each
one whether it has a code, and how long it is:

- symbol `l - 1`, for `l` from 1 to 10: the next byte value has a code of `l` bits;
- symbol `9 + k`, for `k` from 1 to 9, then `x = read(k - 1)`: the next $2^{k-1} + x$ byte values have
  no code, a **run**. Symbol 10 is a run of 1 with no bits after it, symbol 15 with the 5 bits `00000`
  a run of 32.

A run is a count of byte values in a row that have no code. Small runs are common, a table skips
one or two byte values here and there, and long ones are rare, e.g. the 32 control characters below
the space in a text page. So the code for a run has to be short for small counts and still work for
large ones. `OWN` writes a run in two parts: first how many bits the count has in binary, its
**width** `k`, as the symbol `9 + k`; then the bits of the count below its first 1:

| run | in binary | width `k` | symbol `9 + k` | bits after the symbol |
| ---: | --- | ---: | ---: | --- |
| 1 | `1` | 1 | 10 | none |
| 2 | `10` | 2 | 11 | `0` |
| 3 | `11` | 2 | 11 | `1` |
| 4 | `100` | 3 | 12 | `00` |
| 5 | `101` | 3 | 12 | `01` |
| 7 | `111` | 3 | 12 | `11` |
| 32 | `100000` | 6 | 15 | `00000` |
| 100 | `1100100` | 7 | 16 | `100100` |

The first bit of a number in binary is always a 1, else the number would have one bit less. So once the
decoder knows the width, it knows that bit too, and only the `k - 1` bits below it are in the
bitstream. To decode, it puts the 1 back in front: symbol 12 means width 3, `read(2)` gives `01`, and a
1 in front of `01` is `101`, a run of 5. The rule above, $2^{k-1} + x$, says the same with numbers
instead of bits: $2^{k-1}$ is the 1 in front, 4 for `k = 3`, and `x` is the number the bits after the
symbol make, 1 for `01`.

Writing a number as its width and then its bits is an old idea. Its textbook form is the
[Elias gamma code](https://en.wikipedia.org/wiki/Elias_gamma_coding): `k - 1` zeros, then all `k`
bits of the number, the 1 in front included. The decoder counts zeros until it reads a 1; that 1 is
the number's first bit, and as many bits follow as it counted zeros. A run of 5 is `00` and `101`,
`00101`, and a run of 32 is `00000100000`, 11 bits. The
[Exp-Golomb code](https://en.wikipedia.org/wiki/Exponential-Golomb_coding) of order 0 is the same code
for numbers that start at 0: it writes `n` as Elias gamma writes `n + 1`, so a run as `run - 1`
gives the same bits. [Chapter 6](#ch-decisions--level3) has its other orders.

`OWN` does not write the zeros: the width is one of its symbols, in the same prefix code as the
lengths. This does two things. One symbol says that a run comes and how wide it is; with Elias gamma
each symbol would need one more bit in front, to tell a run from a length. And like every
[Huffman code](#ch-compression--huffman), `OWN` gives a symbol the shorter a code the more often it came
on the pages `OWN` was counted on, lengths and runs together. On those pages the lengths came more
often than the runs, so the lengths of 9 and 10 bits got codes of {{own.len.8}} bits, 7 and 8 bits
codes of {{own.len.6}} bits, and a run costs more than in Elias gamma (the whole code is in the table
below):

| run | bits in `OWN`: the symbol and the bits after it | bits in Elias gamma, and 1 for run or length |
| --- | ---: | ---: |
| 1 | {{own.len.10}} | 1 + 1 |
| 2 or 3 | {{own.len.11}} + 1 | 3 + 1 |
| 4 to 7 | {{own.len.12}} + 2 | 5 + 1 |
| 32 to 63 | {{own.len.15}} + 5 | 11 + 1 |
@assert own.len[10] > 2 and own.len[11] + 1 > 4 and own.len[12] + 2 > 6 and own.len[15] + 5 > 12 and own.len[8] == own.len[9] == 2 and own.len[6] == own.len[7] == 3

The bits go where a page's table spends them: every byte value with a code costs one length, and a
run comes only between them. In the example below, {{own.text.with_code}} of the {{own.text.rows}}
symbols are lengths.

The list ends as soon as the codes fill the code space, when $\sum 2^{-l}$ over the lengths so far
is exactly 1: the code is then [complete](#ch-compression--complete), and the byte values after the
last one have no code. The bits after the last symbol, up to the end of its byte, are 0, and `d` is
the number of bytes the table takes. The codes follow from the lengths as for every table, the
[canonical code](#ch-compression--canonical-codes), with the byte value as the symbol. `OWN` has
{{own.symbols}} symbols and codes of at most {{own.code_bits}} bits, by length:

| symbols | mean | codes |
| --- | --- | --- |
| 8, 9 | lengths 9 and 10 | `{{own.code.8}}`, `{{own.code.9}}` |
| 6, 7 | lengths 7 and 8 | `{{own.code.6}}`, `{{own.code.7}}` |
| 5, 10 | length 6, a run of 1 | `{{own.code.5}}`, `{{own.code.10}}` |
| 4, 11 | length 5, a run of 2 or 3 | `{{own.code.4}}`, `{{own.code.11}}` |
| 12 | a run of 4 to 7 | `{{own.code.12}}` |
| 2, 3 | lengths 3 and 4 | `{{own.code.2}}`, `{{own.code.3}}` |
| 0, 1, 13 to 18 | lengths 1 and 2, runs of 8 to 15, ..., 256 to 511 | `{{own.code.0}}` to `{{own.code.18}}` |
@assert own.len[8] == own.len[9] == 2 and own.len[6] == own.len[7] == 3 and own.len[5] == own.len[10] == 4 and own.len[4] == own.len[11] == 5 and own.len[12] == 6 and own.len[2] == own.len[3] == 7 and own.len[0] == own.len[1] == own.len[13] == own.len[18] == 8 and own.symbols == 19

The lengths that pages' own tables have most, 7 to 10 bits, get the shortest codes. The
description is in the order of the byte values, so a byte value costs no bits of its own, only its
length does; `docs/format.md` gives about 0.45 bytes per byte value with a code, 60 to 120 bytes on
most pages that have a table of their own
([format.md](https://github.com/martinus/seqlz/blob/main/docs/format.md#a-blocks-own-literal-table)).
A run of 256 to 511 can never be valid, as a table has only 256 byte values; it is there so that `OWN`
is complete too. Sent in byte order, with runs, the descriptions made pages 0.5% smaller than the ones
of 9th October, which listed the bytes by code length, each as its distance to the one before
([explored-designs.md](https://github.com/martinus/seqlz/blob/main/docs/explored-designs.md#a-pages-own-literal-table-in-the-order-of-the-bytes-decoded-two-lengths-at-a-time-05-smaller-than-by-code-length-reads-29-slower-on-the-a76-kept)). Where its counts come from is in [chapter 4](#ch-format--tables).

The text page of [level 2's bytes](#ch-format--coded), at level 3, is {{own.text.len}} bytes. Its byte 2 is `{{own.text.t}}`,
`0b{{own.text.t_bin}}`: code {{own.text.lcode}} and `w = 5 + {{own.text.w5}} = {{own.text.w}}`. The
{{own.text.dbytes}} bytes from byte {{own.text.desc_at}} on are its table:

```text
{{own.text.desc}}
```

| bits | symbol | means | $\sum 2^{-l}$, in 1/1024 |
| --- | --- | --- | --- |
| `{{own.text.d.0.bits}}` | {{own.text.d.0.sym}} | {{own.text.d.0.what}} | {{own.text.d.0.space}} |
| `{{own.text.d.1.bits}}` | {{own.text.d.1.sym}} | {{own.text.d.1.what}} | {{own.text.d.1.space}} |
| `{{own.text.d.2.bits}}` | {{own.text.d.2.sym}} | {{own.text.d.2.what}} | {{own.text.d.2.space}} |
| `{{own.text.d.3.bits}}` | {{own.text.d.3.sym}} | {{own.text.d.3.what}} | {{own.text.d.3.space}} |
| `{{own.text.d.4.bits}}` | {{own.text.d.4.sym}} | {{own.text.d.4.what}} | {{own.text.d.4.space}} |
| `{{own.text.d.5.bits}}` | {{own.text.d.5.sym}} | {{own.text.d.5.what}} | {{own.text.d.5.space}} |
| `{{own.text.d.6.bits}}` | {{own.text.d.6.sym}} | {{own.text.d.6.what}} | {{own.text.d.6.space}} |
| `{{own.text.d.7.bits}}` | {{own.text.d.7.sym}} | {{own.text.d.7.what}} | {{own.text.d.7.space}} |
| ... | | | |
| `{{own.text.d.18.bits}}` | {{own.text.d.18.sym}} | {{own.text.d.18.what}} | {{own.text.d.18.space}} |
| `{{own.text.d.19.bits}}` | {{own.text.d.19.sym}} | {{own.text.d.19.what}} | {{own.text.d.19.space}} |
@assert own.text.rows == 20 and own.text.last_row == 19 and own.text.d[19].space == 1024 and own.text.d[18].space < 1024

@anim owndesc wide The same {{own.text.dbytes}} bytes, one symbol of `OWN` per step: the byte values get their lengths or a run skips them, and the lengths fill the code space until the code is complete; then the canonical codes. From the real compressor at level 3.

The sum reaches 1 with `u`, after {{own.text.desc_bits}} bits, and the last byte has
{{own.text.desc_pad}} zero bits after them. {{own.text.with_code}} byte values have a code: the space,
`l` and `u` 3 bits, the other 10 letters 4 bits, $3 \cdot \frac{1}{8} + 10 \cdot \frac{1}{16} = 1$. As
canonical codes, by length and then by byte value, the space is `{{own.text.code.20}}`, `l`
`{{own.text.code.6c}}`, `u` `{{own.text.code.75}}`, then `a` `{{own.text.code.61}}`, `d`
`{{own.text.code.64}}` and so on up to `t` `{{own.text.code.74}}`. The streams start at
${{own.text.desc_at}} + {{own.text.dbytes}} = {{own.text.streams_at}}$ and take {{own.text.sum}} bytes,
{{own.enc.bits_per_lit}} bits per literal where table {{ch04.text.set}} took {{own.enc.set_bits_per_lit}}, and
the bitstream is the same {{own.text.b_bytes}} bytes as at level 2. The two runs before the space
and `a` take {{own.len.15}} + 5 and {{own.len.16}} + 6 bits: on a page of text most of the table's
bits are the lengths.
@assert own.text.clen["20"] == own.text.clen["6c"] == own.text.clen["75"] == 3 and own.text.with_code == 13 and own.text.b_bytes == ch04.text.b_bytes

A page is invalid if the sum goes above 1, if a byte value above 255 would get a code, if a bit after
the last symbol in its byte is not 0, or if $3 + w + e + d$ is beyond `len`. `seqlz_decompress()` reads zeros
past the end of the page, and a code of zeros, `{{own.code.8}}`, is a length, so a table that never
fills the code space still ends there, and the last of those checks rejects it. How the encoder chooses the lengths and which pages get a
table is in [`seqlz_code_own()`](#ch-literals-enc--own); how the decoder builds a decode table from them
without any memory of its own, in [`read_own_table()`](#ch-literals-dec--own).

## Literals as deltas {#ch-format--deltas}

With `e = 1` each literal was stored, before it was coded, as its **delta** to the byte `x` before it
in the page: the literal minus that byte, modulo 256. The decoder adds the same byte back as it appends
the literal, modulo 256 again, which gives the literal back. The byte `dt`, byte 3, right after `t` and
before the stream sizes, says which `x`. One rule holds for all three. Take a literal that the decoder
writes to byte `p` of the page, where `out` is the page as far as it is written, so `out[p - x]` is
the byte `x` before it. If `p >= x`, the page stores the literal as `(literal - out[p - x]) mod 256`,
and the decoder writes `(stored + out[p - x]) mod 256`; if `p < x`, there is no such byte, and the
literal is stored as it is. `x` is

| `dt` | `x` |
| --- | --- |
| 1 | 4 |
| 2 | 8 |
| 3 | the smallest multiple of `last` that is 4 or more |

`last` is the offset of the sequence before, 1 for the first, so for `dt = 3` the byte is 4 back for
`last` 1 or 2, 6 back for `last` 3, and `last` back from 4 on. Only the first `x` bytes of the page have no byte `x` before them; every other literal is a delta,
the first literals of a sequence too. `out[p - x]` is a byte the decoder has already written, as it is
in the page: a byte of the match before, of an earlier sequence, or an earlier literal of the same
sequence; the last sequence is no different. Any other `dt` makes the page invalid. Memory pages are
full of 4- and 8-byte fields, whose bytes are often those of the field before or differ from them by a
little, as counters, sizes and addresses of objects in a row do, and the byte after a match often
differs only a little from the byte the match would have copied next. As deltas, such literals become a
few small values, which a literal table codes in fewer bits
([`docs/format.md`](https://github.com/martinus/seqlz/blob/main/docs/format.md#literals-as-deltas)).
A delta, not a XOR: a field that grows by 1 from `0x0f` to `0x10` has a delta of 1, but `0x0f` XOR
`0x10` is `0x1f`, every bit the carry flipped. The first form of `e`, on 21st October 2026, XORed the
literals; the subtraction replaced it the same day, before it was merged ([chapter 6](#ch-decisions--deltas)).

[The page of 512 pointers](#ch-matcher--back) at level 3 begins with
`{{delta.ptr.l3.head}}`: `h` says {{delta.ptr.l3.n}} coded literals, `t = {{delta.ptr.l3.t}}` is code
{{delta.ptr.l3.code}} with `w = {{delta.ptr.l3.w}}` and `e = 1`, byte {{delta.ptr.l3.dt_at}} is
`dt = {{delta.ptr.l3.dt}}`, so `x = 8`, and 5 bytes of sizes follow. Its first literals,
`{{delta.ptr.plain}}`, are stored as `{{delta.ptr.deltas}}`: the first sequence is
{{delta.ptr.rec.0.ll}} literals from byte 0, of which the first 8 have no byte 8 before them and stay as
they are, and its last, `0x20` at byte 8, is stored minus `out[0] = 0x10`. The second sequence is 1
literal at byte {{delta.ptr.rec.1.pos}}, the first of its sequence, and the decoded delta `0x10` is
added to `out[8] = 0x20`, the low byte of the pointer before, which gives `0x30`.
@assert delta.ptr.l3.w == 5 and delta.ptr.l3.dt == ch06.DELTA_8 and delta.ptr.rec[0].pos == 0 and delta.ptr.rec[1].ll == 1 and delta.ptr.rec[1].pos == 16 and delta.ptr.plain[:2] == "10" and delta.ptr.plain[24:26] == "20" and delta.ptr.deltas[24:26] == "10" and delta.ptr.plain[:23] == delta.ptr.deltas[:23] and delta.ptr.fast.before[:2] == "10" and delta.ptr.fast.ref[:2] == "20" and delta.ptr.fast.after[:2] == "30"
