# src/seqlz_lit_sets.c

The 8 literal tables that a page with coded literals picks one of, compiled in. Like
`seqlz_default_tables.c`, it only includes the generated `.inc` file of the page size, for 4 KiB
pages [`seqlz_lit_sets_4k.inc`](seqlz_lit_sets_4k.inc.html).

## Eight fixed literal tables

@@ 1-15
k-means, in plain words: give each training page to the table that codes its literals in the fewest
bits, make each table the Huffman code of the literals of its pages, and repeat until the groups stop
changing. Where it ends depends on the starting tables, so the trainer runs it from 32 starts and keeps
the best. The result is 8 tables for 8 kinds of pages: in the 4 KiB tables e.g. one for text, two that give
the byte `0x00` a 2-bit code, one that gives `0xff` a 2-bit code.

"The most used table comes first" is out of date for the 4 KiB tables. `bench/seqlz_counts_4k.txt`
says they are in the order that matches the tables trained before them, so that table 0 is still the
one for text, and table 1 has more pages than table 0. The order means nothing to the format.
@more {#ch-codes--lit-sets-file} Why 8 literal tables, not 1, 16 or 64
::: measured 8 literal tables, not 1, 16 or 64
k-means trained on the resident pages of a desktop and priced on its zram pages, as the share of
`seqlz-1`'s output saved: 1 table 6.8%, 4 tables 9.2%, 8 tables of 10 bits 11.4%, 16 tables
11.9%, 64 tables 12.9%
([explored-designs.md](https://github.com/martinus/seqlz/blob/main/docs/explored-designs.md#seqlz-fast-lit-one-of-8-literal-tables-per-page)).
16 tables were built and measured in the kernel VM: 5 bytes per page less, and twice the decode
tables made cold reads 0.27 µs slower, so they were dropped
([explored-designs.md](https://github.com/martinus/seqlz/blob/main/docs/explored-designs.md#seqlz-fast-lit-by-the-score-no-budget-offsets-in-steps-of-8)).
:::
@end

@@ 16-20
Also in the `.inc` file: `seqlz_own_lengths`, the code `OWN` that a level 3 page writes its own
literal table in, made by the same generator from the line `own` of the counts
([where the tables come from](book:#ch-format--tables)). Picked by page size like the token tables in
`seqlz_default_tables.c`.
