# The code on one page {#map}

Part II is the code itself: every file of `src/` as it is, with a short note next to each block.
Under a note, buttons open what goes deeper, below the block: why it is written this way, a worked
example with real numbers, what it was measured at, an animation. Open them where a note leaves you
with a question, skip them where it does not. Read the files in this order, each one links to the
next:

@code

This page is the other order: the path of one page through the code, written and read back, with a
link to the code of each step.

<figure class="diagram">
<div class="scroll">
<svg viewBox="0 0 720 452" role="img" aria-label="The map of the code: zram calls seqlz_compress() and seqlz_decompress(); writing a page goes from the matcher through the encoder and the literal coder to the compressed page, reading goes from the literal decoder through the sequence loop back to 4096 bytes; the format constants and tables are under both.">
<defs><marker id="map-arrow" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0 0 L10 5 L0 10 z" fill="currentColor"/></marker></defs>
<a href="#ch-zram-glue"><rect class="fill-paper" x="10" y="8" width="700" height="34" rx="4" stroke="currentColor" stroke-width="1"/>
<text x="20" y="30" font-size="12.5" text-anchor="start" font-weight="700">zram, through backend_seqlz.c</text>
<g class="acc"><text x="268" y="30" font-size="11">zram_seqlz.c</text></g>
<text x="700" y="30" font-size="11.5" text-anchor="end">write: seqlz_compress()   read: seqlz_decompress()</text>
</a>
<text x="10" y="64" font-size="11.5" text-anchor="start">write a page</text>
<rect x="10" y="74" width="80" height="92" rx="4" fill="none" stroke="currentColor" stroke-width="1"/>
<text x="50.0" y="117.0" font-size="11.5" text-anchor="middle">4096</text>
<text x="50.0" y="131.0" font-size="11.5" text-anchor="middle">bytes</text>
<a href="#ch-matcher"><rect class="fill-sheet" x="112" y="74" width="190" height="92" rx="4" stroke="currentColor" stroke-width="1"/>
<text x="207.0" y="92" font-size="12.5" text-anchor="middle" font-weight="700">matcher</text>
<g class="acc"><text x="207.0" y="107" font-size="11" text-anchor="middle">seqlz_match.h</text></g>
<text x="207.0" y="124" font-size="11" text-anchor="middle">seqlz_match_block()</text>
<text x="207.0" y="138" font-size="11" text-anchor="middle">sequences: ll, ml, offset</text>
</a>
<a href="#ch-encoder"><rect class="fill-sheet" x="324" y="74" width="190" height="92" rx="4" stroke="currentColor" stroke-width="1"/>
<text x="419.0" y="92" font-size="12.5" text-anchor="middle" font-weight="700">encoder</text>
<g class="acc"><text x="419.0" y="107" font-size="11" text-anchor="middle">seqlz_compress.c</text></g>
<text x="419.0" y="124" font-size="11" text-anchor="middle">encode_seq()</text>
<text x="419.0" y="138" font-size="11" text-anchor="middle">literals front, bits back</text>
</a>
<a href="#ch-literals-enc"><rect class="fill-sheet" x="536" y="74" width="174" height="92" rx="4" stroke="currentColor" stroke-width="1"/>
<text x="623.0" y="92" font-size="12.5" text-anchor="middle" font-weight="700">literal coder</text>
<g class="acc"><text x="623.0" y="107" font-size="11" text-anchor="middle">seqlz_compress_literals.c</text></g>
<text x="623.0" y="124" font-size="11" text-anchor="middle">code_literals()</text>
<text x="623.0" y="138" font-size="11" text-anchor="middle">levels 2 and 3</text>
</a>
<path d="M90 120 L110 120" fill="none" stroke="currentColor" stroke-width="1.3" marker-end="url(#map-arrow)"/>
<path d="M302 120 L322 120" fill="none" stroke="currentColor" stroke-width="1.3" marker-end="url(#map-arrow)"/>
<path d="M514 120 L534 120" fill="none" stroke="currentColor" stroke-width="1.3" marker-end="url(#map-arrow)"/>
<a href="#ch-format"><rect class="fill-paper" x="550" y="196" width="160" height="62" rx="4" stroke="currentColor" stroke-width="1.5"/>
<text x="630" y="216" font-size="12.5" text-anchor="middle" font-weight="700">compressed page</text>
<g class="acc"><text x="630" y="231" font-size="11" text-anchor="middle">chapter 4</text></g>
<text x="630" y="248" font-size="11" text-anchor="middle">u16, literals, bits</text>
</a>
<path d="M635 166 L635 194" fill="none" stroke="currentColor" stroke-width="1.3" marker-end="url(#map-arrow)"/>
<path d="M440 166 C 450 200, 500 222, 548 224" fill="none" stroke="currentColor" stroke-width="1.3" stroke-dasharray="4 3" marker-end="url(#map-arrow)"/>
<text x="486" y="196" font-size="11" text-anchor="middle">level 1</text>
<text x="10" y="288" font-size="11.5" text-anchor="start">read a page</text>
<a href="#ch-literals-dec"><rect class="fill-sheet" x="500" y="300" width="210" height="92" rx="4" stroke="currentColor" stroke-width="1"/>
<text x="605.0" y="318" font-size="12.5" text-anchor="middle" font-weight="700">literal decoder</text>
<g class="acc"><text x="605.0" y="333" font-size="11" text-anchor="middle">seqlz_decompress_literals.c</text></g>
<text x="605.0" y="350" font-size="11" text-anchor="middle">seqlz_decode_literals()</text>
<text x="605.0" y="364" font-size="11" text-anchor="middle">levels 2 and 3, 8 streams</text>
</a>
<a href="#ch-decode"><rect class="fill-sheet" x="112" y="300" width="330" height="92" rx="4" stroke="currentColor" stroke-width="1"/>
<text x="277.0" y="318" font-size="12.5" text-anchor="middle" font-weight="700">sequence loop</text>
<g class="acc"><text x="277.0" y="333" font-size="11" text-anchor="middle">seqlz_decompress.c</text></g>
<text x="277.0" y="350" font-size="11" text-anchor="middle">decode_block(): tokens, offsets, copies</text>
</a>
<a href="#ch-dec-basics"><g class="acc"><text x="277" y="378" font-size="11" text-anchor="middle">with the copies and the bit reader at its top</text></g></a>
<rect x="10" y="300" width="80" height="92" rx="4" fill="none" stroke="currentColor" stroke-width="1"/>
<text x="50.0" y="343.0" font-size="11.5" text-anchor="middle">4096</text>
<text x="50.0" y="357.0" font-size="11.5" text-anchor="middle">bytes</text>
<path d="M635 258 L635 298" fill="none" stroke="currentColor" stroke-width="1.3" marker-end="url(#map-arrow)"/>
<path d="M498 346 L444 346" fill="none" stroke="currentColor" stroke-width="1.3" marker-end="url(#map-arrow)"/>
<path d="M110 346 L92 346" fill="none" stroke="currentColor" stroke-width="1.3" marker-end="url(#map-arrow)"/>
<path d="M562 250 C 520 270, 470 280, 444 298" fill="none" stroke="currentColor" stroke-width="1.3" stroke-dasharray="4 3" marker-end="url(#map-arrow)"/>
<text x="496" y="266" font-size="11" text-anchor="middle">level 1</text>
<text x="10" y="414" font-size="11.5" text-anchor="start">under both</text>
<a href="#ch-linux"><rect class="fill-sheet" x="100" y="400" width="145" height="44" rx="4" stroke="currentColor" stroke-width="1"/>
<text x="172.5" y="418" font-size="12" text-anchor="middle" font-weight="700">kernel names</text>
<g class="acc"><text x="172.5" y="434" font-size="11" text-anchor="middle">src/linux/</text></g></a>
<a href="#ch-header"><rect class="fill-sheet" x="255" y="400" width="145" height="44" rx="4" stroke="currentColor" stroke-width="1"/>
<text x="327.5" y="418" font-size="12" text-anchor="middle" font-weight="700">format constants</text>
<g class="acc"><text x="327.5" y="434" font-size="11" text-anchor="middle">seqlz_format.h</text></g></a>
<a href="#ch-internal"><rect class="fill-sheet" x="410" y="400" width="145" height="44" rx="4" stroke="currentColor" stroke-width="1"/>
<text x="482.5" y="418" font-size="12" text-anchor="middle" font-weight="700">table layout</text>
<g class="acc"><text x="482.5" y="434" font-size="11" text-anchor="middle">seqlz_tables.h</text></g></a>
<a href="#ch-codes"><rect class="fill-sheet" x="565" y="400" width="145" height="44" rx="4" stroke="currentColor" stroke-width="1"/>
<text x="637.5" y="418" font-size="12" text-anchor="middle" font-weight="700">tables built</text>
<g class="acc"><text x="637.5" y="434" font-size="11" text-anchor="middle">seqlz_codes.c</text></g></a>
</svg>
</div>
<figcaption>The code by the path of a page: writing left to right, reading right to left, and what both use below. Each box links to its chapter.</figcaption>
</figure>

## Writing a page {#map--write}

zram calls `seqlz_compress()` with the page, a buffer for the result and the level, through the
[backend](#ch-zram-glue--backend). At levels 1 and 2 three steps follow:

1. **The matcher** ([`seqlz_match.h`](#ch-matcher)), `seqlz_match_block()`, walks the page once and cuts it
   into sequences: `ll` literals, bytes it found no match for, then a match of `ml` bytes copied from
   `offset` bytes back. It finds a candidate with a hash of the next 5 bytes in a table of positions
   ([the hash](#ch-matcher--hash)), and tries the repeat offset at every position too.
2. **The encoder** ([`seqlz_encoder.h`](#ch-encoder)) gets each sequence the moment it is found: the
   matcher calls `encode_seq()`, which the compiler inlines into its loop ([one loop](#ch-matcher--emit)),
   all of it in `seqlz_compress.c`. `encode_seq()` copies the literals to the front of the
   output and puts the sequence's token code, offset bits and length values into a 64-bit accumulator,
   which goes to the back of the output in whole bytes ([both ends](#ch-encoder--layout)).
   `encoder_finish()` moves the bits to right after the literals, and that is a level 1 page.
3. **The literal coder** ([`seqlz_compress_literals.c`](#ch-literals-enc)), at levels 2 and 3 only, prices the 8 fixed
   literal tables with one add per literal, takes the one with the fewest bits, and writes the literals
   as 8 streams of Huffman codes over the raw ones, where that saves enough
   ([the rule](#ch-literals-enc--rule)).

Level 3 does more in each step: the matcher looks one position further and extends a match backwards
([level 3](#ch-matcher--lazy)), the encoder puts a record in front of each sequence's literals
([records](#ch-encoder--records)), and the literal coder may build a literal table of the page's own
and store the literals as deltas to an earlier byte ([own table](#ch-literals-enc--own),
[deltas](#ch-literals-enc--deltas)). What comes out of all levels is the one format of
[chapter 4](#ch-format): a header, the literals raw or as 8 streams, then the bitstream of the
sequences.

## Reading a page {#map--read}

zram calls `seqlz_decompress()` with the compressed page, the 4096 bytes to fill, and a scratch buffer.

1. **The literal decoder** ([`seqlz_decompress_literals.c`](#ch-literals-dec)), only for a page with coded literals:
   `seqlz_decode_literals()` reads the header, takes one of the 8 fixed tables or builds the page's own,
   and decodes the 8 streams side by side into the scratch ([from the header to the
   bitstream](#ch-literals-dec)).
2. **The sequence loop** ([`seqlz_decompress.c`](#ch-decode)), `decode_block()`: one lookup in the token table
   gives a sequence's `ll`, `ml` and how to read its offset, then the literals are copied from the
   scratch or the input and the match from the page itself. Most sequences take the fast path, with
   copies of a fixed size and no checks of room ([fast path](#ch-decode--fast)), the others the slow
   path with every check ([slow path](#ch-decode--slow)). Level 3's deltas are added back right after
   each copy of literals ([in page order](#ch-literals-dec--undelta)). The copies and the bit reader
   are at [the top of `seqlz_decompress.c`](#ch-dec-basics).
3. A page is valid only if its bits end where the input ends ([the end](#ch-decode--end)). Any other
   input gets an error, and no input makes the decoder read or write outside its buffers
   ([why](#ch-decode--safety)).

## Under both {#map--shared}

- [`src/linux/`](#ch-linux): the kernel's headers outside the kernel, so that the same code
  builds there.
- [`seqlz_format.h`](#ch-header) and [`seqlz.h`](#f-seqlz-h): the format as constants, the token, the
  offset classes and the caps, and the functions for userspace.
- [`seqlz_tables.h`](#ch-internal): the layout of the tables, shaped so that the decoder
  needs one lookup per token.
- [`seqlz_codes.c`](#ch-codes): the tables built from the trained code lengths, once, when seqlz is
  built ([at build time](#ch-codes--kernel)).

The encoders take their codes from these tables and the decoders their decode tables; neither path
changes them.

## Questions, and where the answers are {#map--questions}

| question | where |
| --- | --- |
| What does zram need from a codec, and how is a codec scored? | [chapter 1](#ch-zram), [the score](#ch-zram--score) |
| How many bits does one sequence take? | [the token](#ch-format--token), [`encode_seq()`](#ch-encoder--emit) |
| How does the matcher find a match, and why so few candidates? | [two candidates](#ch-matcher--plan) |
| How can the encoder write one buffer from both ends? | [both ends](#ch-encoder--layout) |
| How does a page pick its literal table? | [pricing](#ch-literals-enc--pricing), [the choice](#ch-literals-enc--choice) |
| When does a page get a literal table of its own? | [own table](#ch-literals-enc--own) |
| What are the deltas, and where are they undone? | [format](#ch-format--deltas), [encoder](#ch-literals-enc--deltas), [decoder](#ch-literals-dec--undelta) |
| Why 8 streams of literals? | [chains](#ch-cpu--chains), [8 side by side](#ch-literals-dec--cost) |
| What makes the decoder fast? | [fast path](#ch-decode--fast), [next token](#ch-decode--next-token), [prefetch](#ch-dec-basics--prefetch) |
| Why can no input make the decoder write out of bounds? | [the loop](#ch-decode--safety), [the literals](#ch-literals-dec--safety) |
| Where do the tables come from? | [training](#ch-codes--lengths), [fixed tables](#ch-codes--fixed) |
| How much memory does a CPU need? | [work memory](#ch-zram--work-memory), [the backend](#ch-zram-glue--backend) |
| How does `src/` become kernel code? | [port.py](#ch-zram-glue--port) |
| What does one real page look like at each step? | [chapter 5](#ch-trace) |
| Why is it built this way and not another? | [chapter 6](#ch-decisions) |
