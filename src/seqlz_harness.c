// SPDX-License-Identifier: GPL-2.0-only OR MIT
/*
 * What only the tests and tools use of the compressor: seqlz_find(), the
 * sequences the matcher finds, and seqlz_encode(), a block written from given
 * sequences. The kernel has neither.
 */
#include <linux/minmax.h>

#include "seqlz.h"
#include "seqlz_encoder.h"

/*
 * The matcher of seqlz_match.h once more, with a recorder of the sequences in
 * place of the encoder: here its struct seqlz_encoder is struct find_recorder,
 * and its encode_seq() is find_seq().
 */
struct find_recorder {
	struct seqlz_sequence *seq;
	unsigned int n;
};

static __always_inline void find_seq(struct find_recorder *f, const u8 *in,
				     unsigned int ll, unsigned int ml,
				     unsigned int off, bool rec)
{
	(void)in;
	(void)rec;
	f->seq[f->n].literals = (u16)ll;
	f->seq[f->n].match = (u16)ml;
	f->seq[f->n].offset = (u16)off;
	f->n++;
}

#define seqlz_encoder find_recorder
#define encode_seq find_seq
#include "seqlz_match.h"
#undef encode_seq
#undef seqlz_encoder

unsigned int seqlz_find(struct seqlz_state *st, const void *src,
			struct seqlz_sequence *seq, unsigned int level)
{
	struct find_recorder f = { .seq = seq };

	if (level < 1 || level > SEQLZ_LEVELS)
		return 0;
	seqlz_match_block(st->table, src, level >= SEQLZ_LEVEL_OWN, &f);
	return f.n;
}

/* seqlz_encode() up to the coded literals, with the compressor's encoder */
static unsigned int encode_raw(const struct seqlz_sequence *seq, unsigned int n,
			       const u8 *literals, unsigned int n_literals,
			       const u8 *block, u8 *deltas, unsigned int *delta,
			       void *dst, unsigned int dst_cap)
{
	const u8 *in = literals;
	struct seqlz_encoder e;
	unsigned int i, pos = 0, lits = 0;

	if (dst_cap < SEQLZ_HEADER + ENC_ROOM || n == 0 ||
	    n > SEQLZ_MAX_SEQUENCES || n_literals > SEQLZ_BLOCK_SIZE)
		return 0;
	/*
	 * Only sequences that make a block, checked as the decoder checks them,
	 * before anything is written. dst has room only because every match
	 * covers at least 4 bytes: a shorter one makes ml - 4 wrap, and its
	 * length value takes more bits than the block could save.
	 */
	for (i = 0; i < n; i++) {
		lits += seq[i].literals;
		pos += seq[i].literals;
		if (pos > SEQLZ_BLOCK_SIZE)
			return 0;
		if (i + 1 == n)
			break;
		if (seq[i].match < SEQLZ_MIN_MATCH || seq[i].offset == 0 ||
		    seq[i].offset > pos)
			return 0;
		pos += seq[i].match;
	}
	if (pos != SEQLZ_BLOCK_SIZE || lits != n_literals)
		return 0;
	encoder_init(&e, dst, dst_cap, literals + n_literals);
	/* with a block, the records for the deltas, as compress_own() */
	e.rec = block != NULL;
	for (;;) {
		for (i = 0; i < n; i++) {
			unsigned int ll = seq[i].literals;

			encode_seq(&e, in, ll, i + 1 == n ? 0U : seq[i].match,
				   seq[i].offset, true);
			in += ll;
		}
		/*
		 * encoder_init() clears rec: the pass without records is the
		 * last
		 */
		if (e.p != e.lit || !e.rec)
			break;
		encoder_init(&e, dst, dst_cap, literals + n_literals);
		in = literals;
	}
	*delta = e.rec ? seqlz_delta_literals(dst, &e.lit, block, deltas) : 0;
	return encoder_finish(&e, dst, dst_cap);
}

unsigned int seqlz_encode(struct seqlz_state *st,
			  const struct seqlz_sequence *seq, unsigned int n,
			  const u8 *literals, unsigned int n_literals,
			  const void *block, void *dst, unsigned int dst_cap,
			  unsigned int level)
{
	unsigned int len, delta;

	if (level < 1 || level > SEQLZ_LEVELS ||
	    (level >= SEQLZ_LEVEL_OWN && !st))
		return 0;
	/* as seqlz_compress_block() */
	dst_cap = min(dst_cap, SEQLZ_COMPRESS_BOUND);
	len = encode_raw(seq, n, literals, n_literals,
			 level >= SEQLZ_LEVEL_OWN ? block : NULL,
			 st ? st->literals : NULL, &delta, dst, dst_cap);
	if (len == 0 || level < SEQLZ_LEVEL_CODED)
		return len;
	if (level < SEQLZ_LEVEL_OWN)
		return seqlz_code_raw_literals(dst, len, literals);
	return seqlz_code_own(dst, len, st, delta);
}
