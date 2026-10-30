/* SPDX-License-Identifier: GPL-2.0-only OR MIT */
#ifndef SEQLZ_COMPAT_LINUX_MODULE_H
#define SEQLZ_COMPAT_LINUX_MODULE_H

/* #include_next is a GCC extension, which -Wpedantic would warn about */
#pragma GCC system_header

/* the UAPI header of the same name */
#if defined(__has_include_next)
#if __has_include_next(<linux/module.h>)
#include_next <linux/module.h>
#endif
#endif

#include <linux/build_bug.h>

/* no module outside the kernel: a declaration, for the semicolon */
#define MODULE_LICENSE(s) static_assert(1, s)
#define MODULE_DESCRIPTION(s) static_assert(1, s)

#endif
