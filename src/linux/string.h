/* SPDX-License-Identifier: GPL-2.0-only OR MIT */
#ifndef SEQLZ_COMPAT_LINUX_STRING_H
#define SEQLZ_COMPAT_LINUX_STRING_H

#include <stddef.h>

/*
 * With libc, its declarations. The benchmarks also build the codec with the
 * kernel's flags and without libc headers, as the kernel is built
 * (cmake/kernel_codecs.cmake); then only these prototypes.
 */
#if __has_include(<string.h>)
#include <string.h>
#else
void *memcpy(void *dst, const void *src, size_t n);
void *memmove(void *dst, const void *src, size_t n);
void *memset(void *dst, int c, size_t n);
#endif

#endif
