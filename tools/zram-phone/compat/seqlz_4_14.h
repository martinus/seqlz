/* SPDX-License-Identifier: GPL-2.0-only OR MIT */
/*
 * What src/ takes from the kernel's headers and Linux 4.14 does not have yet,
 * included before each file of the module by build.sh's Kbuild. compat/linux/
 * has the headers that came later.
 */
#ifndef SEQLZ_4_14_H
#define SEQLZ_4_14_H

#include <linux/build_bug.h>
#include <linux/export.h>
#include <linux/stddef.h>

/* static_assert() came with 5.1 */
#ifndef static_assert
#define static_assert(expr, ...) __static_assert(expr, ##__VA_ARGS__, #expr)
#define __static_assert(expr, msg, ...) _Static_assert(expr, msg)
#endif

/* sizeof_field() came with 5.5 */
#ifndef sizeof_field
#define sizeof_field(TYPE, MEMBER) sizeof((((TYPE *)0)->MEMBER))
#endif

/*
 * Several builds of seqlz are loaded at the same time, and a symbol may be
 * exported by one module only: nothing is exported
 */
#undef EXPORT_SYMBOL_GPL
#define EXPORT_SYMBOL_GPL(sym) extern typeof(sym) sym

#endif
