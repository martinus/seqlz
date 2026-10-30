# src/seqlz_lengths_16k.h

Every table for 16 KiB pages, built only with `SEQLZ_PAGE_BITS=14`. Same generator, same layout as
[the 4 KiB file](seqlz_lengths.h.html) ([the 16 KiB tables](book:#ch-codes--fixed)). The kernel's copy
has no such file: it compresses blocks of 4 KiB whatever the page size.

## How it differs from 4 KiB

@@ 1-22
The counts are `bench/seqlz_counts_16k.txt`, from 26 933 pages of a zram dump of the Android 17
emulator with 16 KiB pages, not from a real phone. The format for 16 KiB pages is not fixed yet.

@@ 23-214
3072 tokens as on 4 KiB pages, but 512 with a code instead of 640. Class 2, an offset below 256, has 72
codes instead of 139. Class 3 offsets have 14 bits here.

@@ 215-225
The escape has 4 bits again, below this page size's limit of 31 - 14 - 12 = 5. `ll` and `ml` have 26
symbols, two more than on 4 KiB pages: length values go up to 16 383.

@@ 226-227
The 8 literal tables, laid out as in the 4 KiB file.

@@ 228-373
The groups are different: every table here gives `0x00` the shortest code, 2 to 4 bits, and none is a
table for text without zeros, as the 4 KiB table 0 is.

@@ 374-379
`OWN` trained on the 16 KiB pages that level 3 gave a table of their own: again lengths 8 to 10 are
the most frequent.
