# src/seqlz_default_tables.c

The code lengths of the token table and the two tables of length values, compiled in. The numbers
themselves are in a generated `.inc` file, one per page size; this file only picks it.
[`seqlz_codes.c`](seqlz_codes.c.html) turns them into codes and decode tables when seqlz is built.

## The lengths of the format

@@ 1-13
@anchor ch-codes--lengths
These lengths are the format: a page decodes only with the tables it was compressed with, and the page
carries no version ([no version](book:#ch-format--version)). So they are generated, never edited:
`gen-seqlz-lengths.py` turns counts that are checked into the repository into lengths, and CI makes the
`.inc` files again and fails if one differs by a byte
([how](seqlz_default_tables_4k.inc.html#b1)).

The training pages are not pages the benchmarks measure, so no benchmark measures the tables on the
pages they were trained on.
@more {#ch-codes--training-pages} The training pages of 13th October 2026, and what they changed
The 4 KiB counts of 13th October 2026 are from {{ch08.counts4k.pages|grp}} pages: three sessions of a
Xiaomi Mi 9T under memory pressure, the resident pages of a desktop and 200 000 pages of its zram dump
of 28th September ([where the tables come from](#ch-format--tables)). On two test sessions of the
phone, with web sites none of the training sessions loaded, `seqlz-2` stores up to 1.7% less than
with the tables before, and the phone dump of 3rd October, which the tables before were trained on, 1.8% more
([explored-designs.md](https://github.com/martinus/seqlz/blob/main/docs/explored-designs.md#the-tables-trained-on-phone-sessions-up-to-17-smaller-on-them-an-old-phone-dump-18-larger)).
Nearly all of it is in the literal tables. The counts cannot give a page back: the token and length
counts are sums over all pages, each literal table's over at least
{{ch08.counts4k.lit_min_pages|grp}} pages, and a count says nothing about which symbol came after which.
@end

@@ 14-18
seqlz is built for one page size, `SEQLZ_PAGE_BITS` 12 or 14, and each has its own tables: on 16 KiB
pages the offsets have more bits and the length values two more symbols. The literal tables are picked
the same way, in `seqlz_lit_sets.c`.
@more {#ch-codes--fixed} Fixed tables, and the 16 KiB ones
All of the tables are made once, before the first page, and no zram device has tables of its own. A
page carries no token or length table; at level 3 it may carry a literal table, written in the fixed
code `OWN`, which the decoder builds per page ([the page's own table](#ch-literals-dec--own)).
[Where the tables come from](#ch-format--tables) has the measurements of both, and
[no version](#ch-format--version) why a page needs no version of its tables. That is also why the
tables could be trained again on 13th and 14th October 2026: nobody uses the format yet, and new
tables only need new hashes in `format.md`.

The 16 KiB tables, [`seqlz_default_tables_16k.inc`](seqlz_default_tables_16k.inc.html) and
[`seqlz_lit_sets_16k.inc`](seqlz_lit_sets_16k.inc.html), are the same construction with counts from
{{ch08.counts16k.pages|grp}} pages of a zram dump of the Android 17 emulator with 16 KiB pages. They
give {{k16.ch08.tok.coded}} tokens a code, with a {{k16.tok.len.3072}}-bit escape, below that page
size's limit of $31 - 14 - 12 = 5$ bits. On pages of the emulator that were not in the training, they
made `seqlz-2` 2.6% smaller than the 16 KiB tables before them, which were trained on desktop pages
([explored-designs.md](https://github.com/martinus/seqlz/blob/main/docs/explored-designs.md#android-17-in-the-emulator-the-4-kib-tables-fit-the-16-kib-ones-trained-again-26-smaller)).
That is one emulator and no real phone, and the format for 16 KiB pages is not fixed yet
([format.md](https://github.com/martinus/seqlz/blob/main/docs/format.md#status)).
@end
