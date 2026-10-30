/* SPDX-License-Identifier: GPL-2.0-only OR MIT */
#ifndef SEQLZ_COMPAT_LINUX_MINMAX_H
#define SEQLZ_COMPAT_LINUX_MINMAX_H

/* the kernel's min() is a macro, which would break std::min in C++ */
static inline unsigned int min(unsigned int a, unsigned int b)
{
	return a < b ? a : b;
}

#endif
