// SPDX-License-Identifier: GPL-2.0-only OR MIT
/*
 * The compressor: the matcher, which finds a block's sequences, and the
 * encoder, which writes them as it finds them.
 */
#include <linux/build_bug.h>
#include <linux/errno.h>
#include <linux/export.h>
#include <linux/string.h>
#include <linux/unaligned.h>

#include "seqlz.h"
#include "seqlz_encoder.h"
#include "seqlz_match.h"

static unsigned int compress_block(struct seqlz_state *st, const u8 *src,
				   void *dst, unsigned int dst_cap)
{
	struct seqlz_encoder e;

	/*
	 * No block fits into less, and below SEQLZ_HEADER bytes p - lit in
	 * encode_seq() would start below 0. Checked here and in compress_own(),
	 * not once in seqlz_compress_block(): there it made the matcher's code
	 * slower.
	 */
	if (dst_cap < SEQLZ_HEADER + ENC_ROOM)
		return 0;
	encoder_init(&e, dst, dst_cap, src + SEQLZ_BLOCK_SIZE);
	seqlz_match_block(st->table, src, false, &e);
	return encoder_finish(&e, dst, dst_cap);
}

/*
 * compress_block() for SEQLZ_LEVEL_OWN: the lazy matcher, with the records for
 * the deltas, and without them if the block does not fit with them; *delta
 * gets the delta. A function of its own, so that the other levels' code stays
 * as it is without it: inlined next to it, their blocks were slower to write.
 */
static noinline unsigned int compress_own(struct seqlz_state *st, const u8 *src,
					  void *dst, unsigned int dst_cap,
					  unsigned int *delta)
{
	struct seqlz_encoder e;

	*delta = 0;
	if (dst_cap < SEQLZ_HEADER + ENC_ROOM)
		return 0;
	encoder_init(&e, dst, dst_cap, src + SEQLZ_BLOCK_SIZE);
	e.rec = true;
	/*
	 * Once with the records; a block that does not fit with them once more
	 * without, as encoder_init() clears e.rec. A loop and not two calls,
	 * so that the inlined matcher is there once.
	 */
	for (;;) {
		seqlz_match_block(st->table, src, true, &e);
		if (e.p != e.lit || !e.rec)
			break;
		encoder_init(&e, dst, dst_cap, src + SEQLZ_BLOCK_SIZE);
	}
	if (e.rec) {
		/*
		 * a copy of e.lit: with its address taken, e could not be kept
		 * in registers
		 */
		u8 *lit = e.lit;

		*delta = seqlz_delta_literals(dst, &lit, src, st->literals);
		e.lit = lit;
	}
	return encoder_finish(&e, dst, dst_cap);
}

unsigned int seqlz_compress_block(struct seqlz_state *st, const void *src,
				  void *dst, unsigned int dst_cap,
				  unsigned int level)
{
	unsigned int len, delta;

	if (level < 1 || level > SEQLZ_LEVELS)
		return 0;
	/*
	 * Not more room than every block fits into: the bitstream is written
	 * from the end of dst down, and with a larger dst it would land far
	 * from the block, in memory nothing else of it touches
	 */
	dst_cap = min(dst_cap, SEQLZ_COMPRESS_BOUND);
	if (level >= SEQLZ_LEVEL_OWN) {
		len = compress_own(st, src, dst, dst_cap, &delta);
		return len ? seqlz_code_own(dst, len, st, delta) : 0;
	}
	len = compress_block(st, src, dst, dst_cap);
	if (len == 0 || level < SEQLZ_LEVEL_CODED)
		return len;
	return seqlz_code_literals(dst, len, st);
}

/* the interface's work memory is the matcher's state */
static_assert(sizeof(struct seqlz_state) == SEQLZ_MEM_COMPRESS);
/* its levels are those of seqlz_compress_block() */
static_assert(SEQLZ_LEVEL_MIN == 1 && SEQLZ_LEVEL_CODED == 2 &&
	      SEQLZ_LEVEL_OWN == SEQLZ_LEVEL_MAX);
/* the room the interface's kernel-doc names, see ENC_ROOM */
static_assert(ENC_ROOM == 32);

int seqlz_compress(const void *src, void *dst, unsigned int dst_len,
		   void *wrkmem, int level)
{
	unsigned int len;

	if (level < SEQLZ_LEVEL_MIN || level > SEQLZ_LEVEL_MAX)
		return -EINVAL;
	len = seqlz_compress_block(wrkmem, src, dst, dst_len,
				   (unsigned int)level);
	return len ? (int)len : -E2BIG;
}
EXPORT_SYMBOL_GPL(seqlz_compress);
