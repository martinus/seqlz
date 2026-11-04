# Level 2's bytes: coded literals {#ch-format2}

Level 1's page keeps its literals as they are, between the header and the bitstream
([the layouts](#ch-format--layout)). Level 2 codes them where that saves enough, and the page then
says so with the top bit of `h`. This chapter is the part of [the format](#ch-format) that level 2
adds; the code that reads and writes it follows.

## Coded literals: byte 2, the sizes, 8 streams {#ch-format--coded}

With the top bit of `h` set, the literals are Huffman coded, with one of 8 fixed literal
tables or with a table that the page brings, and more fields come first. Byte 2, `t`:

<figure class="diagram">
<div class="scroll">
<svg viewBox="0 0 720 134" role="img" aria-label="Byte 2 of a page with coded literals, {{ch04.text.t}}: the delta bit, w minus 5 in bits 4 to 6, the literals' code in bits 0 to 3" font-size="12">
  <text x="222.0" y="30" text-anchor="middle" class="mut">bit 7</text>
  <rect x="200" y="40" width="44" height="30" class="fill-sheet" stroke="currentColor"/>
  <text x="222.0" y="60" text-anchor="middle" font-weight="bold">{{ch04.text.t_bit.7}}</text>
  <text x="266.0" y="30" text-anchor="middle" class="mut">bit 6</text>
  <rect x="244" y="40" width="44" height="30" class="fill-mark" stroke="currentColor" fill-opacity="0.3"/>
  <text x="266.0" y="60" text-anchor="middle" font-weight="bold">{{ch04.text.t_bit.6}}</text>
  <text x="310.0" y="30" text-anchor="middle" class="mut">bit 5</text>
  <rect x="288" y="40" width="44" height="30" class="fill-mark" stroke="currentColor" fill-opacity="0.3"/>
  <text x="310.0" y="60" text-anchor="middle" font-weight="bold">{{ch04.text.t_bit.5}}</text>
  <text x="354.0" y="30" text-anchor="middle" class="mut">bit 4</text>
  <rect x="332" y="40" width="44" height="30" class="fill-mark" stroke="currentColor" fill-opacity="0.3"/>
  <text x="354.0" y="60" text-anchor="middle" font-weight="bold">{{ch04.text.t_bit.4}}</text>
  <text x="398.0" y="30" text-anchor="middle" class="mut">bit 3</text>
  <rect x="376" y="40" width="44" height="30" class="fill-acc" stroke="currentColor" fill-opacity="0.3"/>
  <text x="398.0" y="60" text-anchor="middle" font-weight="bold">{{ch04.text.t_bit.3}}</text>
  <text x="442.0" y="30" text-anchor="middle" class="mut">bit 2</text>
  <rect x="420" y="40" width="44" height="30" class="fill-acc" stroke="currentColor" fill-opacity="0.3"/>
  <text x="442.0" y="60" text-anchor="middle" font-weight="bold">{{ch04.text.t_bit.2}}</text>
  <text x="486.0" y="30" text-anchor="middle" class="mut">bit 1</text>
  <rect x="464" y="40" width="44" height="30" class="fill-acc" stroke="currentColor" fill-opacity="0.3"/>
  <text x="486.0" y="60" text-anchor="middle" font-weight="bold">{{ch04.text.t_bit.1}}</text>
  <text x="530.0" y="30" text-anchor="middle" class="mut">bit 0</text>
  <rect x="508" y="40" width="44" height="30" class="fill-acc" stroke="currentColor" fill-opacity="0.3"/>
  <text x="530.0" y="60" text-anchor="middle" font-weight="bold">{{ch04.text.t_bit.0}}</text>
  <path d="M203 78 V84 H241 V78" fill="none" stroke="currentColor"/>
  <text x="222.0" y="102" text-anchor="middle" font-weight="bold">e = {{ch04.text.t_bit.7}}</text>
  <text x="222.0" y="119" text-anchor="middle">t &gt;&gt; 7</text>
  <path d="M247 78 V84 H373 V78" fill="none" stroke="currentColor"/>
  <text x="310.0" y="102" text-anchor="middle" font-weight="bold">w - 5 = {{ch04.text.w5}}</text>
  <text x="310.0" y="119" text-anchor="middle">(t &gt;&gt; 4) &amp; 7</text>
  <path d="M379 78 V84 H549 V78" fill="none" stroke="currentColor"/>
  <text x="464.0" y="102" text-anchor="middle" font-weight="bold">code = {{ch04.text.set}}</text>
  <text x="464.0" y="119" text-anchor="middle">t &amp; 15</text>
  <text x="150" y="60" text-anchor="end">t = {{ch04.text.t}}</text>
</svg>
</div>
<figcaption>Byte 2 of the made-up text page below: <code>{{ch04.text.t}}</code> is literal table {{ch04.text.set}} and stream sizes of {{ch04.text.w}} bits.</figcaption>
</figure>

- `code = t & 15` says how the literals are coded: 0 to 7 is one of the 8 fixed literal tables, 8
  (`SEQLZ_LIT_OWN`) a table of the page's own ([Part IV](#ch-format--own)), and 9 to 15 make the page
  invalid.
- `w = 5 + ((t >> 4) & 7)` is the number of bits of each stream size, 5 to 12.
- `e = t >> 7` says whether the literals are stored as deltas to earlier bytes of the page
  ([Part IV](#ch-format--deltas)), which only `seqlz-3` writes. The codes 9 to 15 are free for a later format
  ([no version in the page](#ch-format--version)).

With `e = 1` byte 3 is `dt`, which says which earlier byte each delta is to. The next `w` bytes, from
byte $3 + e$ on, are one little-endian number `S` of `8 * w` bits with the 8 stream sizes, `w` bits
each, the lowest first: `s[j] = (S >> (j * w)) & ((1 << w) - 1)`. With code 8 the page's own table follows,
`d` bytes; with the codes 0 to 7, `d` is 0. Then come the 8 streams, `s[j]`
bytes each, and after the last one `B`. The literals are dealt out to the streams like cards: stream
$j$ holds literals $j$, $j + 8$, $j + 16$ and so on, each as a code of the table that `code` names,
read most significant bit first like `B`. A stream's codes must end in its last byte, and the bits
after the last code are 0; a stream without literals has `s[j] = 0`. All 8 streams use the same table:
that there are 8 streams and 8 fixed tables is a coincidence.

A page made up for this book: 4096 bytes of "words" of 2 to 9 letters, picked at random from the 12
letters `etaoinshrdlu` by the C library's classic random number generator from seed 1, each word
followed by a space. `seqlz-1` stores it in {{ch04.text.raw}} bytes, `seqlz-2` in
{{ch04.text.len}}. The first {{ch04.text.streams_at}} bytes of the coded page are

```text
{{ch04.text.hdr}}  {{ch04.text.t_hex}}  {{ch04.text.sizes_hex}}
```

- `h = {{ch04.text.h}}`: coded literals, and `n = {{ch04.text.n_hex}} = {{ch04.text.n}}`,
  {{ch04.text.per_stream}} per stream.
- `t = {{ch04.text.t}} = 0b{{ch04.text.t_bin}}`: code `{{ch04.text.t}} & 15 = {{ch04.text.set}}`,
  the fixed table that codes this page's literals in the fewest bits, and
  `w = 5 + (({{ch04.text.t}} >> 4) & 7) = 5 + {{ch04.text.w5}} = {{ch04.text.w}}`.
- The {{ch04.text.w}} bytes of sizes are `S = {{ch04.text.S}}`. Its lowest {{ch04.text.w}} bits,
  `{{ch04.text.s0_hex}} = {{ch04.text.s.0}}`, are the size of stream 0; the 8 sizes are
  {{ch04.text.s.0}}, {{ch04.text.s.1}}, {{ch04.text.s.2}}, {{ch04.text.s.3}}, {{ch04.text.s.4}},
  {{ch04.text.s.5}}, {{ch04.text.s.6}} and {{ch04.text.s.7}} bytes, {{ch04.text.sum}} together.
- No deltas and no own table, `e = 0` and `d = 0`: the streams start at byte
  $3 + w = {{ch04.text.streams_at}}$, `B` at
  ${{ch04.text.streams_at}} + {{ch04.text.sum}} = {{ch04.text.b_at}}$, and takes the last
  {{ch04.text.b_bytes}} bytes of the page.
@assert ch04.text.best == ch04.text.set and ch04.text.set < 8 and ch04.text.t_bit[7] == 0

{{ch04.text.per_stream}} literals in about {{ch04.text.avg}} bytes is {{ch04.text.bits_per_lit}} bits
per literal, where the raw page spends 8. `seqlz-3` stores the same page in {{own.text.len}} bytes, with a
table of its own, [Part IV](#ch-format--own).

::: why Why the stream sizes take `w` bits and not 2 bytes each
A stream holds every 8th literal, and a literal code has at most 10 bits: at most 640 bytes, 10 bits.
The largest stream of most pages is below 128
bytes, 7 bits. With 2 bytes per size, as until 5th October 2026, the header was 19 bytes, and pages
were 5.3 bytes larger on average
([explored-designs.md](https://github.com/martinus/seqlz/blob/main/docs/explored-designs.md#stream-sizes-in-as-many-bits-as-the-largest-needs-kept)).
Widths of 3 and 4 bits are left out: few pages would use them.
:::

Why the literals are in 8 streams is a question of the decoder's speed
([chains of dependent steps](#ch-cpu--chains)). How the encoder picks the table, and when it codes the
literals at all, is in [`code_sampled()`](#ch-literals-enc), at level 3 in
[`seqlz_code_own()`](#ch-literals-enc--code-own-entry).
