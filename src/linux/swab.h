/* SPDX-License-Identifier: GPL-2.0-only OR MIT */
#ifndef SEQLZ_COMPAT_LINUX_SWAB_H
#define SEQLZ_COMPAT_LINUX_SWAB_H

/* #include_next is a GCC extension, which -Wpedantic would warn about */
#pragma GCC system_header

/* the UAPI header of the same name */
#if defined(__has_include_next)
#if __has_include_next(<linux/swab.h>)
#include_next <linux/swab.h>
#endif
#endif

#include <linux/types.h>

/* the 8 bytes of x in the other order */
static __always_inline u64 swab64(u64 x)
{
	return __builtin_bswap64(x);
}

#endif
