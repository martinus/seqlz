/* SPDX-License-Identifier: GPL-2.0-only OR MIT */
#ifndef SEQLZ_COMPAT_LINUX_ERRNO_H
#define SEQLZ_COMPAT_LINUX_ERRNO_H

/* #include_next is a GCC extension, which -Wpedantic would warn about */
#pragma GCC system_header

/* the UAPI header, which libc's errno.h includes */
#if __has_include_next(<linux/errno.h>)
#include_next <linux/errno.h>
#endif

/* without libc's headers, as the benchmarks build the codec with the kernel's flags */
#ifndef EINVAL
#define E2BIG 7
#define EINVAL 22
#endif

#endif
