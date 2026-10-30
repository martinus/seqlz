/* SPDX-License-Identifier: GPL-2.0-only OR MIT */
#ifndef SEQLZ_COMPAT_LINUX_TYPES_H
#define SEQLZ_COMPAT_LINUX_TYPES_H

/* #include_next is a GCC extension, which -Wpedantic would warn about */
#pragma GCC system_header

/* the UAPI header, for libc's headers that include it */
#if __has_include_next(<linux/types.h>)
#include_next <linux/types.h>
#endif

/*
 * Nothing here defines bool, true, false or NULL itself, so that the codec's
 * headers stay usable from C++
 */
#include <stdbool.h>
#include <stddef.h>

/* as the kernel has them on every architecture, u64 too */
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long long u64;

#endif
