# src/seqlz_format.h

The format as constants, plus two small functions that turn a sequence's numbers into what the format
stores: `seqlz_len_symbol()` and `seqlz_off_class()`. Every file of the codec includes it. The long
comment at the top is the format short enough to keep in your head; [chapter 4](book:#ch-format) has
it in words and `docs/format.md` is the specification. The notes here say why each number has its
value.

A reading order:

1. [The comment at the top](#b7): sequences, the token, offset classes, length values, the block's
   layout.
2. [The table sizes](#b72) and [the token's fields](#b84): what the longest codes cost a cold read.
3. [The escape](#b100), for the tokens without a code.
4. [`seqlz_off_class()`](#b195), the class of an offset without a branch, and
   [the nibble constants](#b175) it reads.
5. Coded literals: [tables and streams](#b218), [the header](#b234), [deltas](#b256),
   [a page's own table](#b273).

## The format in one comment

@@ 1-6
In the kernel's copy this `#include` becomes `linux/types.h` and nothing else: a program that runs on
the build machine and writes the tables includes this file too, and may only use the few headers the
kernel has for such programs ([chapter 8](book:#ch-codes--kernel)).

@@ 7-27
A sequence is "copy `ll` literals, then copy `ml` bytes from `offset` bytes back". The token packs
all three into one Huffman symbol so that one table lookup in the decoder gives all three. E.g. 1
literal, a match of 7 at the repeat offset (class 0) is token `1 + 16 * 3 + 0 = 49`, the typical
sequence of an array of pointers.

"1 to the bytes before it": a match can reach back to the block's first byte and no further, so 4095
is the largest offset in a 4 KiB block ([the token](book:#ch-format--token)).

@@ 28-39
Class 0 is the repeat offset: the same offset as the sequence before, sent with no bits at all. Class 1
spends a bit that is always 1, which looks wasteful. Without it the decoder would need a special case;
with it every class's offset comes out of the same shift and mask
([token entry](book:#ch-internal--token-entry)).

Classes 4 and 5 exist because memory pages are full of 8-byte fields: offset 72 is sent as `72 / 8 =
9` in 5 bits, instead of 8 bits in class 2 ([offset classes](book:#ch-format--offsets)).

@@ 40-51
A length value of 16 or more is sent as "where its highest bit is", as a symbol, then the bits below
that bit. E.g. 100 is `0b1100100`: the highest bit is bit 6, so symbol 18, then the 6 bits `100100`.
The highest bit itself is not sent, the symbol says where it is.

A block does not store how many sequences it has. The decoder stops when the literals left are exactly
the bytes left to fill: a match has at least 4 bytes, so no match can follow
([how a page ends](book:#ch-format--end)).

@@ 52-61
The `u16` is what lets the decoder read two places at once: the literals start at byte 2, the
bitstream at byte `2 + n` for `n` raw literals ([the layout](book:#ch-format--layout)). A 16-byte copy
of the last literals may read into the bitstream, which is harmless: it is still inside the input.

In the kernel's copy the last line points at `Documentation/staging/seqlz.rst`, which `port.py` writes
from `docs/format.md`: the kernel's copy may not name the repository.
@anim abbits The 9-byte example page, one field per step

## The block size and the token

@@ 62-71
`SEQLZ_PAGE_BITS` is the codec's one build setting: 12 for 4 KiB pages, 14 for 16 KiB, passed as
`-DSEQLZ_PAGE_BITS=14`. The kernel's copy fixes `SEQLZ_BLOCK_BITS` at `12U` whatever the kernel's page
size, because the format for 16 KiB blocks is not fixed yet.

@@ 72-83
The longest code sets the size of the decode table: the decoder looks up the next `L` bits in a table
of `2^L` entries ([table decoding](book:#ch-compression--table-decoding)). 11 bits are 2048 entries of
4 bytes, 8 KiB, 128 cache lines of 64 bytes. A **cold** read, of a page swapped out a while ago, finds
none of them in the cache and pays for each line it touches.

With 10 bits fewer tokens get a code of their own and more take the escape, so blocks get larger. The
smaller table paid off only on the Mi 9T's in-order Cortex-A55; on its A76 and in the kernel VM on
x86-64 it did not, so 11 stays ([the token table](book:#ch-header--token)).

@@ 84-99
The token holds `ll` in bits 0 to 3, `ml - 4` in bits 4 to 8, the class in bits 9 to 11:
`SEQLZ_TOKEN_SYMBOLS` is `6 << 9`, 3072. Larger values need a length value after the token, which is
a second lookup in another table.

The two widths cannot grow: the decoder's token entry stores `ml` as it is, the cap plus 4, in 6 bits.
`31 + 4 = 35` fits 6 bits; with 6 bits for `ml - 4` it would be `63 + 4 = 67`, which does not.

## The escape and the caps

@@ 100-123
A code of `l` bits uses `1 / 2^l` of the code space, and the sum may not exceed 1
([complete codes](book:#ch-compression--complete)). With at most 11 bits that is room for 2048 codes,
for 3072 tokens. So rare tokens are sent as the escape, symbol 3072, then their number in 12 plain
bits. The values 3072 to 4095 are no token, and the decoder rejects them.

`SEQLZ_MAX_ESCAPE_LEN` turns a promise of the encoder into a limit on the tables. The encoder's proof
that two pages always fit assumes a token and its offset take at most 31 bits. The longest pair is an
escaped token with a class 3 offset, so the escape's code may have `31 - 12 - 12 = 7` bits on 4 KiB
blocks. The table generator checks it ([all symbols](book:#ch-codes--all-symbols)).

The caps double as flags: `ll` 15 in a token means "15 or more", and `ll - 15` follows.

## Length values and the u16

@@ 124-144
`SEQLZ_LEN_LOG_BASE` is 12 so that the two kinds of symbols meet without a gap: symbol 16 is 16 to 31
(highest bit 4), 17 is 32 to 63, and the last on 4 KiB blocks, 23, is 2048 to 4095. That is 24
symbols.

The literal count can borrow the top bit of the `u16` because it never reaches it: at most 16384
literals, 15 bits. The decoder reads the count as the `u16` masked with `SEQLZ_LIT_CODED - 1U`.

## The tables as code lengths

@@ 145-156
A canonical Huffman code follows from the code lengths alone, so one byte per symbol is the whole
table. The decoder and encoder tables are computed from these at build time. Because the compiled-in
lengths are part of the format, `docs/format.md` lists their SHA-256, so that a decoder written from
the spec can check it has the right ones.
@anim table From code lengths to a decode table and back

@@ 157-174
`31 - __builtin_clz(v)` is the position of the highest set bit. The builtin is undefined for 0, but
`v` is at least 16 on that path. For `v = 100` it gives 6: symbol `12 + 6 = 18`, with 6 extra bits.

gcc compiles it with one branch, for `v < 16`. That is fine here: the encoder calls it only for the
few sequences that have a length value.

## Offset classes without a branch

@@ 175-194
Each class's number of offset bits is one nibble of one constant, read with a shift and a mask instead
of a `switch` or an array. For class 3, `0x85C810 >> 12` is `0x85C`, and `& 15` leaves `0xC`, 12.
When class 3's bits were computed from the block's bits in code, gcc branched on the class; a shift
and a mask cannot become a branch. `SEQLZ_SHIFT_NIBBLES` is the same trick for the shift, `0x330010`.

Class 5 stays at 8 bits, offsets below 2048, also on 16 KiB pages: 10 bits there made pages no smaller
on average ([offset classes](book:#ch-header--offsets)).

@@ 195-217
Read the sum from 3: minus 1 below 256, minus 1 more for offset 2, plus 2 for a multiple of 8 below
2048 (`aligned`). E.g. offset 72 is `3 - 1 - 0 + 2 = 4`, offset 2 is `3 - 1 - 1 + 0 = 1`, offset 399
is 3. Offset 2048 is a multiple of 8 but not below 2048, so class 3.

`is_new` is a mask: `(off == last) - 1U` is 0 for the repeat offset and all ones otherwise, so `&
is_new` makes the class 0 without an `if`. `&` instead of `&&` for `aligned`: `&&` may skip its right
side, and compilers often keep that as a branch. gcc compiles the function for x86-64 without a jump.

## Coded literals

@@ 218-233
A literal table codes the 256 byte values in codes of at most 10 bits, so its decode table has 1024
entries of 2 bytes, 2 KiB. 9 bits would halve it; the comment says how that went.

Why 8 streams: the decoder knows where literal `k + 1` starts only after it has looked up literal `k`,
so one stream is one chain of lookups, each waiting for the one before. 8 streams are 8 independent
chains, which a CPU works on side by side ([chains](book:#ch-cpu--chains)). That there are also 8
tables is a coincidence: all streams of a block use its one table.
@anim streams Literals dealt out to 8 streams, and decoded a round at a time

@@ 234-255
8 sizes of `w` bits each are exactly `w` bytes, hence `3 + w`. E.g. a block whose largest stream has
100 bytes needs `w = 7` and a header of 10 bytes.

The code field has 4 bits for 9 values: 0 to 7 the fixed tables, 8 the block's own table. 9 to 15 are
invalid on purpose, so that a future format can tell itself apart
([no version](book:#ch-format--version)). The `static_assert`s that these fields fit together are in
`seqlz_tables.h` ([byte 2](book:#ch-internal--byte2)).

@@ 256-272
A delta turns a slowly growing value into one small number that repeats. E.g. a page of pointers, each
16 larger than the one before: the low byte `0x30` minus the low byte of the pointer 8 bytes back,
`0x20`, is `0x10`, and so is the next one, `0x40 - 0x30`. A subtraction and not a XOR: `0x40` XOR
`0x30` is `0x70`, so a XOR gives one value per pattern of carries ([deltas](book:#ch-format--deltas)).

Why "4 or more" for `SEQLZ_DELTA_LAST`: the decoder adds 4 or 8 bytes at a time, which only works if
every byte it adds is at least 4 bytes back. After a match at offset 3 the bytes repeat every 3, so the
byte 6 back equals the byte 3 back. 0 is not a valid value: bit 7 already says whether there are deltas.

@@ 273-298
A run's symbol says how many bits its number has. E.g. a run of 32 is `0b100000`, 6 bits: symbol
`SEQLZ_OWN_RUN + 6 = 15`, then the 5 bits below the top one, `00000`. Runs of 1 to 9 bits make 9
symbols; with the 10 lengths, 19 in all, `SEQLZ_OWN_SYMBOLS`.

The last paragraph is why a block's own table costs the decoder no memory. Its decode table needs
2 KiB, and work memory per CPU is what the project counts. The output page is still free while the
literals are decoded into the scratch, so the table is built there
([own table](book:#ch-literals-dec--own)).
@anim owndesc How a page's own table is described, one symbol per step
