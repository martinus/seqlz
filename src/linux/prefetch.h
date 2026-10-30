/* SPDX-License-Identifier: GPL-2.0-only OR MIT */
#ifndef SEQLZ_COMPAT_LINUX_PREFETCH_H
#define SEQLZ_COMPAT_LINUX_PREFETCH_H

#include <linux/compiler_types.h>

/*
 * Asks the CPU to load the cache line at p, without waiting for it. The kernel
 * builds x86-64 without SSE, and clang then drops __builtin_prefetch().
 * tools/kernel-port/x86-prefetcht0.patch makes the kernel's prefetch() the
 * instruction itself on x86-64; this is the same, for the benchmarks that
 * build the codec with the kernel's flags.
 */
static __always_inline void prefetch(const void *p)
{
#if defined(__x86_64__) && !defined(__SSE__)
	__asm__("prefetcht0 %0" : : "m"(*(const char *)p));
#else
	__builtin_prefetch(p);
#endif
}

#endif
