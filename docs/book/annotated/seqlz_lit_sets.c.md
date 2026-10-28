# src/seqlz_lit_sets.c

The 8 literal tables that a page with coded literals picks one of, compiled in. Like
`seqlz_default_tables.c`, it only includes the generated `.inc` file of the page size
([the literal tables](book:#ch-codes--lit-sets-file)).

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

@@ 16-20
Also in the `.inc` file: `seqlz_own_lengths`, the code `OWN` that a level 3 page writes its own
literal table in. Picked by page size like the token tables in `seqlz_default_tables.c`.
