/* SPDX-License-Identifier: GPL-2.0-only OR MIT */
/*
 * The cache line of the codec's prefetch_lines(), for the codec's files, not
 * glue.c. Older arm64 kernels, the Mi 9T's 4.14 among them, have 128 bytes in
 * L1_CACHE_BYTES. Its cores have 64, as mainline says, and with 128
 * prefetch_lines() would skip every second line.
 *
 * L1_CACHE_BYTES also sets the alignment of kernel structs, e.g. through
 * ARCH_DMA_MINALIGN and ____cacheline_aligned: build.sh's seqlz_4_14_codec.h
 * includes every kernel header the codec includes before this, with the
 * kernel's 128, so that only the codec's own code sees 64.
 */
#ifndef SEQLZ_4_14_CACHE_H
#define SEQLZ_4_14_CACHE_H

#if defined(__aarch64__) && L1_CACHE_BYTES == 128
#undef L1_CACHE_BYTES
#define L1_CACHE_BYTES 64
#endif

#endif
