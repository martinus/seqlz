/* SPDX-License-Identifier: GPL-2.0-only OR MIT */
#ifndef SEQLZ_COMPAT_LINUX_MODULE_H
#define SEQLZ_COMPAT_LINUX_MODULE_H

#include <linux/build_bug.h>

/* no module outside the kernel: a declaration, for the semicolon */
#define MODULE_LICENSE(s) static_assert(1, s)
#define MODULE_DESCRIPTION(s) static_assert(1, s)

#endif
