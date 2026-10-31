/* SPDX-License-Identifier: GPL-2.0-only OR MIT */
#ifndef SEQLZ_ENCODER_H
#define SEQLZ_ENCODER_H

/*
 * The encoder of seqlz_compress.c, which the tests' seqlz_encode() in
 * seqlz_harness.c uses too. All of it is static, so that it ends up inside the
 * loops of seqlz_compress.c; the kernel's seqlz_compress.c has it in place.
 */

#include <linux/build_bug.h>
#include <linux/string.h>
#include <linux/unaligned.h>

#include "seqlz_internal.h"

/*
 * The encoder writes a block in one pass, as the matcher finds the sequences.
 * The block starts with the literals and the bitstream follows, but how many
 * literals there are is known only at the end. So the literals go to the front
 * of dst, after the u16, and the bitstream goes to the back of dst, backwards:
 * its first byte is the last byte of dst, and it grows down toward the
 * literals. At the end it is copied, the right way round, to right after the
 * literals.
 *
 * So dst can have any size, and a block that does not fit is an error, as for
 * the other compressors. Before each sequence the encoder checks that its
 * literals, its record (see LIT_REC) and ENC_ROOM bytes more are free between
 * the two: a sequence's copies write up to 16 bytes past its literals, and its
 * flushes write at most 15 bytes below where the bitstream was before it. A
 * sequence has at most two flushes: the first moves the bitstream down by at
 * most 7 bytes, as it has at most 60 bits (see below), and the second writes 8
 * bytes below that. 16 + 15 bytes fit into ENC_ROOM. A block that does not fit
 * stops the writes there, and seqlz_compress_block() returns 0. It can happen
 * up to ENC_ROOM bytes before dst is full.
 *
 * With the two pages zram gives, it never happens without records: a sequence
 * with a match covers at least 4 bytes of the block, and its token and offset
 * take at most SEQLZ_SEQ_BITS, 31 bits, also with an escaped token. One with
 * length values covers 19 bytes or more, for ll >= 15 or ml >= 35, and 4 * 31
 * bits are more than its token, offset and two length values take. The last
 * sequence has no bits. So the bitstream has at most ENC_BITSTREAM_MAX bytes,
 * 3968 for blocks of 4 KiB, and with a block of literals that leaves more than
 * ENC_ROOM bytes free; for blocks of 16 KiB 15 872 bytes. The records take 6
 * bytes more per sequence, and compress_own() writes a block without them
 * where they do not fit.
 *
 * Bits are collected in a 64-bit accumulator and written 8 bytes at a time;
 * then the write position moves down by the whole bytes, and the at most 7 bits
 * left over are written again with the next ones. After a write the accumulator
 * holds at most 7 bits, so a sequence's token, offset and match length value
 * fit before the next one: 7 + 31 + 20 = 58 bits, 60 for 16 KiB blocks. Only a
 * literal length value, 20 or 22 bits more, needs a write of its own.
 */
#define ENC_ROOM 32U
/* the most bits of a length value: its code and the bits below its top bit */
#define ENC_VALUE_BITS (SEQLZ_MAX_BITS + SEQLZ_BLOCK_BITS)
static_assert(7U + SEQLZ_SEQ_BITS + ENC_VALUE_BITS < 64U,
	      "a token, its offset and a length value fit the accumulator");
static_assert(16U + (7U + SEQLZ_SEQ_BITS + ENC_VALUE_BITS) / 8U + 8U <=
		      ENC_ROOM,
	      "a sequence's copies and flushes fit into ENC_ROOM");
/* the largest bitstream: SEQLZ_SEQ_BITS for every 4 bytes of the block */
#define ENC_BITSTREAM_MAX ((SEQLZ_BLOCK_SIZE / 4U * SEQLZ_SEQ_BITS + 7U) / 8U)
static_assert(SEQLZ_HEADER + SEQLZ_BLOCK_SIZE + ENC_BITSTREAM_MAX + ENC_ROOM <=
		      SEQLZ_COMPRESS_BOUND,
	      "two blocks are always room enough");

/*
 * SEQLZ_OFF_SHIFT(), but 15 for class 0: an offset is below 1 << 15, so its
 * shifted bits are 0 there, as class 0 sends none, without a test of the class
 */
#define ENC_SHIFT(cls) (((SEQLZ_SHIFT_NIBBLES | 15U) >> (4U * (cls))) & 15U)
static_assert(SEQLZ_BLOCK_SIZE <= 1U << 15 && SEQLZ_OFF_SHIFT(0) == 0U,
	      "class 0's offset shifted by 15 is 0");
/*
 * Per offset class its SEQLZ_RAW_BITS() in the low nibble and its ENC_SHIFT()
 * in the high one: one load. From the two constants of SEQLZ_RAW_NIBBLES and
 * SEQLZ_SHIFT_NIBBLES the compiler built both for every sequence.
 */
static const u8 enc_class[8] = {
	SEQLZ_RAW_BITS(0) | ENC_SHIFT(0) << 4,
	SEQLZ_RAW_BITS(1) | ENC_SHIFT(1) << 4,
	SEQLZ_RAW_BITS(2) | ENC_SHIFT(2) << 4,
	SEQLZ_RAW_BITS(3) | ENC_SHIFT(3) << 4,
	SEQLZ_RAW_BITS(4) | ENC_SHIFT(4) << 4,
	SEQLZ_RAW_BITS(5) | ENC_SHIFT(5) << 4,
};

struct seqlz_encoder {
	u64 acc; /* the bits not yet written, the newest in the low bits */
	unsigned int cnt; /* how many bits of acc count */
	u8 *p; /* the next byte of the bitstream goes right below p */
	u8 *lit; /* where the next literal goes */
	const u8 *src_end; /* copies of 16 literals may read up to here */
	unsigned int last; /* the offset of the sequence before, class 0 */
	/*
	 * at SEQLZ_LEVEL_OWN: write a record before the literals, see LIT_REC
	 */
	bool rec;
	unsigned int pos; /* with rec: where the next sequence starts */
};

/* appends the n bits of v, which has no higher bits set; n < 64 - cnt */
static __always_inline void enc_put(struct seqlz_encoder *e, u64 v,
				    unsigned int n)
{
	e->acc = e->acc << (n & 63U) | v;
	e->cnt += n;
}

/*
 * appends a code from an enc[] entry, code | length << 16, and after it the
 * n_extra low bits of extra
 */
static __always_inline void enc_put_code(struct seqlz_encoder *e, u32 entry,
					 unsigned int extra,
					 unsigned int n_extra)
{
	unsigned int len = entry >> 16 & 15U;

	n_extra &= 31U;
	enc_put(e,
		(u64)(entry & 0xffffU) << n_extra |
			(extra & ((1U << n_extra) - 1U)),
		len + n_extra);
}

/*
 * Writes the whole bytes of the accumulator below p, the first bit as the top
 * bit of the byte right below p, the next byte below that. The shift puts the
 * cnt bits that count on top, older bits above them fall out, and a little
 * endian store of the word puts its top byte last, at p - 1. The last byte is
 * written even when it is not full, and again by the next write.
 */
static __always_inline void enc_flush(struct seqlz_encoder *e)
{
	put_unaligned_le64(e->acc << ((64U - e->cnt) & 63U), e->p - 8);
	e->p -= e->cnt >> 3;
	e->cnt &= 7U;
}

/* a length value: its symbol's code and its extra bits */
static __always_inline void put_len_value(struct seqlz_encoder *e,
					  const struct value_table *t,
					  unsigned int v)
{
	unsigned int n_extra,
		s = seqlz_len_symbol(v, &n_extra) & (ENC_LEN_SYMBOLS - 1U);

	enc_put_code(e, t->enc[s], v, n_extra);
}

/*
 * The code of a token without a code of its own, whose enc[] entry is 0: the
 * escape's code followed by the token in SEQLZ_ESCAPE_BITS bits, and its length
 */
static __always_inline u32 token_escape(unsigned int tok, unsigned int *len)
{
	u32 ee = seqlz_tables.token.enc[SEQLZ_ESCAPE];

	*len = (ee >> TOK_ENC_LEN_AT) + SEQLZ_ESCAPE_BITS;
	return (ee & TOK_ENC_CODE_MASK) << SEQLZ_ESCAPE_BITS | tok;
}

/*
 * Writes one sequence: its literals from in, then its token, offset and length
 * values. ml is 0 for the last sequence, which has only its literals. With rec,
 * a constant, and e->rec, the record before the literals, see LIT_REC: the
 * callers that write records pass rec true, the matcher of the levels below
 * SEQLZ_LEVEL_OWN false, so its code has none of it.
 */
static __always_inline void encode_seq(struct seqlz_encoder *e, const u8 *in,
				       unsigned int ll, unsigned int ml,
				       unsigned int off, bool rec)
{
	unsigned int k = 0, tlen, raw_bits, cls, tok, te, cinfo;
	unsigned int n_rec = rec && e->rec && ll ? LIT_REC : 0;
	u32 code;

	/*
	 * The block does not fit: no more writes. With p at lit, every later
	 * sequence ends here too, and encoder_finish() sees it.
	 */
	if ((size_t)(e->p - e->lit) < ll + n_rec + ENC_ROOM) {
		e->p = e->lit;
		return;
	}
	if (n_rec) {
		put_unaligned_le16((u16)e->pos, e->lit);
		put_unaligned_le16((u16)ll, e->lit + 2);
		put_unaligned_le16((u16)e->last, e->lit + 4);
		e->lit += LIT_REC;
	}
	if (rec)
		e->pos += ll + ml;
	/*
	 * The literals, 16 bytes at a time; dst has room for what goes past
	 * ll. Where the block has 16 bytes more after them, which is almost
	 * always, one test, and the first 16 bytes before it, which could
	 * mispredict as a loop. Near the block's end only whole chunks of 16
	 * inside the literals, then the rest byte by byte.
	 */
	if ((unsigned int)(e->src_end - in) >= ll + 16U) {
		do {
			__builtin_memcpy(e->lit + k, in + k, 16);
			k += 16;
		} while (k < ll);
	} else {
		for (; k + 16U <= ll; k += 16)
			__builtin_memcpy(e->lit + k, in + k, 16);
		for (; k < ll; k++)
			e->lit[k] = in[k];
	}
	e->lit += ll;

	/*
	 * The last sequence: no token, the decoder knows it from the literals
	 * left, which fill the rest of the block.
	 */
	if (ml == 0)
		return;
	/* token and offset in one put, the length values after them */
	cls = seqlz_off_class(off, e->last, &raw_bits);
	cinfo = enc_class[cls & 7U];
	/* the same bits, from enc_class[]: its constant is then not built */
	raw_bits = cinfo & 15U;
	tok = seqlz_token(ll, ml, cls);
	te = seqlz_tables.token.enc[tok];
	/* gcc keeps this a branch, which predicts: 6% of the tokens escape */
	if (te) {
		code = te & TOK_ENC_CODE_MASK;
		tlen = te >> TOK_ENC_LEN_AT;
	} else {
		code = token_escape(tok, &tlen);
	}
	/*
	 * The offset's bits right after the token's code: classes 1 to 5 send
	 * off >> SEQLZ_OFF_SHIFT(cls), which is below 1 << raw_bits, in
	 * raw_bits bits. Class 0 sends none, ENC_SHIFT() shifts its offset to
	 * 0.
	 */
	enc_put(e, (u64)code << raw_bits | off >> (cinfo >> 4),
		tlen + raw_bits);
	if (ll >= SEQLZ_LL_CAP) {
		put_len_value(e, &seqlz_tables.ll, ll - SEQLZ_LL_CAP);
		enc_flush(e);
	}
	if (ml - SEQLZ_MIN_MATCH >= SEQLZ_ML_CAP)
		put_len_value(e, &seqlz_tables.ml,
			      ml - SEQLZ_MIN_MATCH - SEQLZ_ML_CAP);
	enc_flush(e);
	e->last = off;
}

static __always_inline void encoder_init(struct seqlz_encoder *e, u8 *d,
					 unsigned int dst_cap,
					 const u8 *src_end)
{
	*e = (struct seqlz_encoder){ .p = d + dst_cap,
				     .lit = d + SEQLZ_HEADER,
				     .src_end = src_end,
				     .last = 1 };
}

/*
 * Writes the u16, and copies the bitstream from the back of dst to right after
 * the literals, the right way round. Returns the length of the block, or 0 if
 * it did not fit into the dst_cap bytes at d.
 */
static unsigned int encoder_finish(struct seqlz_encoder *e, u8 *d,
				   unsigned int dst_cap)
{
	u8 *end = d + dst_cap, *out = e->lit;
	const u8 *in;
	unsigned int n_lit = (unsigned int)(e->lit - (d + SEQLZ_HEADER));
	unsigned int k = 0;
	unsigned int bytes;

	if (e->p == e->lit)
		return 0;
	/*
	 * Every sequence with a match ends with enc_flush(), which already
	 * wrote the bits left over on top of the byte below p, zeros below
	 * them: that byte is the bitstream's last.
	 */
	if (e->cnt > 0)
		e->p--;
	in = e->p;
	bytes = (unsigned int)(end - in);
	put_unaligned_le16((u16)n_lit, d);
	/*
	 * Byte k of the bitstream is at end - 1 - k. 8 bytes at a time while
	 * the copy does not overlap them, else one at a time, in place: only a
	 * block that nearly fills dst gets there.
	 */
	if ((size_t)(in - out) >= bytes) {
		for (; k + 8U <= bytes; k += 8)
			put_unaligned_be64(get_unaligned_le64(end - 8 - k),
					   out + k);
		for (; k < bytes; k++)
			out[k] = *(end - 1 - k);
	} else {
		u8 *lo = e->p, *hi = end - 1;

		for (; lo < hi; lo++, hi--) {
			u8 b = *lo;

			*lo = *hi;
			*hi = b;
		}
		memmove(out, in, bytes);
	}
	return SEQLZ_HEADER + n_lit + bytes;
}

#endif
