/* SPDX-License-Identifier: GPL-2.0-only OR MIT */
#ifndef SEQLZ_COMPAT_LINUX_COMPILER_TYPES_H
#define SEQLZ_COMPAT_LINUX_COMPILER_TYPES_H

/*
 * The attributes the kernel's compiler_types.h defines, which kbuild includes
 * in every file. Here the stand-ins that need them include it, prefetch.h among
 * them, which seqlz_internal.h includes; none that libc's headers include too.
 */

/* glibc's sys/cdefs.h has its own */
#ifndef __always_inline
#define __always_inline inline __attribute__((__always_inline__))
#endif
/* only for the codec's C: as macros they would break C++'s [[gnu::noinline]] */
#ifndef __cplusplus
#define noinline __attribute__((__noinline__))
#define __cold __attribute__((__cold__))
#endif

#endif
