// SPDX-License-Identifier: GPL-2.0-only OR MIT
/*
 * The code lengths compiled in, of the tokens and length values and of the
 * literal tables. They are part of the format: a block decodes only with the
 * tables it was compressed with (docs/format.md). For blocks of 4 KiB they are
 * in seqlz_lengths.h, the kernel's but without static, for 16 KiB blocks in
 * seqlz_lengths_16k.h.
 * tools/kernel-port/gen-seqlz-lengths.py makes them from the counts in
 * bench/seqlz_counts_4k.txt and _16k.txt, those of 4 KiB counted with
 *   seqlz-train --corpus train-mix-1012 --counts
 * on pages that are not in the dumps the benchmarks measure. docs/format.md
 * says which pages, in "Where the tables come from".
 *
 * The literal tables are made by k-means over the literal histograms of the
 * training pages, with the compressor's matcher: each page goes to the table
 * that codes its literals in the fewest bits, then each table becomes the
 * Huffman code of its pages' literals. Of 32 runs from other starting tables,
 * the one with the fewest bits is kept. The most used table comes first.
 *
 * The kernel includes its seqlz_lengths.h, with static tables, in
 * gen_seqlz_tables, the program that builds the tables, and in its KUnit test;
 * here the tests and tools use them through seqlz.h, so they are defined once,
 * in this file.
 */
#include "seqlz.h"

#if SEQLZ_PAGE_BITS != 12
#include "seqlz_lengths_16k.h"
#else
#include "seqlz_lengths.h"
#endif
