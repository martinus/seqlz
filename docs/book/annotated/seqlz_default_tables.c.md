# src/seqlz_default_tables.c

The code lengths of the token table and the two tables of length values, compiled in. The numbers
themselves are in a generated `.inc` file, one per page size; this file only picks it
([where they come from](book:#ch-codes--lengths)).

## The lengths of the format

@@ 1-13
These lengths are the format: a page decodes only with the tables it was compressed with, and the page
carries no version ([no version](book:#ch-format--version)). So they are generated, never edited:
`gen-seqlz-lengths.py` turns counts that are checked into the repository into lengths, and CI makes the
`.inc` files again and fails if one differs by a byte.

The training pages are not pages the benchmarks measure, so no benchmark measures the tables on the
pages they were trained on.

@@ 14-18
seqlz is built for one page size, `SEQLZ_PAGE_BITS` 12 or 14, and each has its own tables: on 16 KiB
pages the offsets have more bits and the length values two more symbols. The literal tables are picked
the same way, in `seqlz_lit_sets.c`.
