/* SPDX-License-Identifier: GPL-2.0-only OR MIT */
#ifndef SEQLZ_COMPAT_LINUX_STDDEF_H
#define SEQLZ_COMPAT_LINUX_STDDEF_H

/* #include_next is a GCC extension, which -Wpedantic would warn about */
#pragma GCC system_header

/* the UAPI header, for libc's headers that include it */
#if __has_include_next(<linux/stddef.h>)
#include_next <linux/stddef.h>
#endif

#include <stddef.h>

#define sizeof_field(TYPE, MEMBER) sizeof((((TYPE *)0)->MEMBER))

#endif
