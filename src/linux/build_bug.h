/* SPDX-License-Identifier: GPL-2.0-only OR MIT */
#ifndef SEQLZ_COMPAT_LINUX_BUILD_BUG_H
#define SEQLZ_COMPAT_LINUX_BUILD_BUG_H

/*
 * static_assert() as the kernel has it: the message is optional, before C23
 * too. Without one, the expression is the message. A keyword in C23 and C++.
 */
#if !defined(__cplusplus) && \
	(!defined(__STDC_VERSION__) || __STDC_VERSION__ < 202311L)
#undef static_assert
#define static_assert(...) __static_assert(__VA_ARGS__, #__VA_ARGS__, "")
#define __static_assert(expr, msg, ...) _Static_assert(expr, msg)
#endif

#endif
