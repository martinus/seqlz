/* SPDX-License-Identifier: GPL-2.0-only OR MIT */
#ifndef SEQLZ_COMPAT_LINUX_MATH64_H
#define SEQLZ_COMPAT_LINUX_MATH64_H

#include <linux/types.h>

/*
 * A u64 divided by a u32: a function of its own in the kernel, because 32-bit
 * CPUs have no such division and the kernel has no library call for it
 */
static inline u64 div_u64(u64 dividend, u32 divisor)
{
	return dividend / divisor;
}

#endif
