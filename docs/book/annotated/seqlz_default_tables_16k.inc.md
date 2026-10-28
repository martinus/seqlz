# src/seqlz_default_tables_16k.inc

The token and length value tables for 16 KiB pages, built only with `SEQLZ_PAGE_BITS=14`. Same
generator, same layout as [the 4 KiB file](seqlz_default_tables_4k.inc.html): one row per match length
and offset class, `ll` 0 to 15 across ([the 16 KiB tables](book:#ch-codes--fixed)).

## How it differs from 4 KiB

@@ 1-10
The counts are `bench/seqlz_counts_16k.txt`, from 26 933 pages of a zram dump of the Android 17
emulator with 16 KiB pages, not from a real phone. The format for 16 KiB pages is not fixed yet.

@@ 11-202
3072 tokens as on 4 KiB pages, but 512 with a code instead of 640. Class 2, an offset below 256, has 72
codes instead of 139. Class 3 offsets have 14 bits here.

@@ 203-213
The escape has 4 bits again, below this page size's limit of 31 - 14 - 12 = 5. `ll` and `ml` have 26
symbols, two more than on 4 KiB pages: length values go up to 16 383.
