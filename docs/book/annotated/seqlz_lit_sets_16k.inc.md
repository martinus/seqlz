# src/seqlz_lit_sets_16k.inc

The 8 literal tables and `OWN` for 16 KiB pages, built only with `SEQLZ_PAGE_BITS=14`. Same generator,
same layout as [the 4 KiB file](seqlz_lit_sets_4k.inc.html) ([the 16 KiB tables](book:#ch-codes--fixed)).

## How it differs from 4 KiB

@@ 1-9
Counted on the Android 17 emulator's pages, 26 933 of them, not on a phone and a desktop.

@@ 10-155
The groups are different: every table here gives `0x00` the shortest code, 2 to 4 bits, and none is a
table for text without zeros, as the 4 KiB table 0 is.

@@ 156-159
`OWN` trained on the 16 KiB pages that level 3 gave a table of their own: again lengths 8 to 10 are
the most frequent.
