/* SPDX-License-Identifier: GPL-2.0-only OR MIT */
#ifndef SEQLZ_COMPAT_LINUX_EXPORT_H
#define SEQLZ_COMPAT_LINUX_EXPORT_H

/* nothing to export outside a module: a declaration, for the semicolon */
#define EXPORT_SYMBOL_GPL(sym) extern __typeof__(sym) sym

#endif
