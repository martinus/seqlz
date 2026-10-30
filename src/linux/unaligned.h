/* SPDX-License-Identifier: GPL-2.0-only OR MIT */
#ifndef SEQLZ_COMPAT_LINUX_UNALIGNED_H
#define SEQLZ_COMPAT_LINUX_UNALIGNED_H

#include <linux/compiler_types.h>
#include <linux/string.h>
#include <linux/types.h>

/*
 * Little and big endian numbers at any address, whatever the CPU. On x86-64 and
 * arm64 a plain load or store, and a byte swap for big endian.
 */
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define SEQLZ_LE16(x) __builtin_bswap16(x)
#define SEQLZ_LE32(x) __builtin_bswap32(x)
#define SEQLZ_LE64(x) __builtin_bswap64(x)
#define SEQLZ_BE64(x) (x)
#else
#define SEQLZ_LE16(x) (x)
#define SEQLZ_LE32(x) (x)
#define SEQLZ_LE64(x) (x)
#define SEQLZ_BE64(x) __builtin_bswap64(x)
#endif

static __always_inline u16 get_unaligned_le16(const void *p)
{
	u16 v;

	memcpy(&v, p, sizeof(v));
	return SEQLZ_LE16(v);
}

static __always_inline void put_unaligned_le16(u16 val, void *p)
{
	val = SEQLZ_LE16(val);
	memcpy(p, &val, sizeof(val));
}

static __always_inline u32 get_unaligned_le32(const void *p)
{
	u32 v;

	memcpy(&v, p, sizeof(v));
	return SEQLZ_LE32(v);
}

static __always_inline void put_unaligned_le32(u32 val, void *p)
{
	val = SEQLZ_LE32(val);
	memcpy(p, &val, sizeof(val));
}

static __always_inline u64 get_unaligned_le64(const void *p)
{
	u64 v;

	memcpy(&v, p, sizeof(v));
	return SEQLZ_LE64(v);
}

static __always_inline u64 get_unaligned_be64(const void *p)
{
	u64 v;

	memcpy(&v, p, sizeof(v));
	return SEQLZ_BE64(v);
}

static __always_inline void put_unaligned_le64(u64 val, void *p)
{
	val = SEQLZ_LE64(val);
	memcpy(p, &val, sizeof(val));
}

static __always_inline void put_unaligned_be64(u64 val, void *p)
{
	val = SEQLZ_BE64(val);
	memcpy(p, &val, sizeof(val));
}

#endif
