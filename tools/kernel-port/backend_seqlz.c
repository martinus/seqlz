// SPDX-License-Identifier: GPL-2.0-only OR MIT
/*
 * port.py keeps the ZCOMP_RW_SPLIT parts for a tree whose zram has separate
 * compression and decompression contexts, struct zcomp_cstrm in zcomp.h, and
 * the others for a tree without; the copy in the tree has no #ifdef left.
 */

#define pr_fmt(fmt) "seqlz: " fmt

#include <linux/kernel.h>
#include <linux/minmax.h>
#include <linux/seqlz.h>
#include <linux/slab.h>

#include "backend_seqlz.h"

/* a page is one block of seqlz; Kconfig keeps the backend to 4 KiB pages */
static_assert(PAGE_SIZE == SEQLZ_BLOCK_SIZE);
/* kmalloc() from its caches, larger would take pages of a higher order */
static_assert(SEQLZ_MEM_COMPRESS <= KMALLOC_MAX_CACHE_SIZE);
static_assert(SEQLZ_MEM_DECOMPRESS <= KMALLOC_MAX_CACHE_SIZE);

static void seqlz_release_params(struct zcomp_params *params)
{
}

static int seqlz_setup_params(struct zcomp_params *params)
{
	if (params->dict_sz) {
		pr_err("dictionary is not supported\n");
		return -EOPNOTSUPP;
	}
	if (params->level == ZCOMP_PARAM_NOT_SET)
		params->level = SEQLZ_LEVEL_DEFAULT;
	if (params->level < SEQLZ_LEVEL_MIN ||
	    params->level > SEQLZ_LEVEL_MAX) {
		pr_err("invalid compression level %d\n", params->level);
		return -EINVAL;
	}
	return 0;
}

#ifdef ZCOMP_RW_SPLIT
static int seqlz_create_cctx(struct zcomp_params *params,
			     struct zcomp_ctx *ctx)
{
	ctx->context = kzalloc(SEQLZ_MEM_COMPRESS, GFP_KERNEL);
	return ctx->context ? 0 : -ENOMEM;
}

/* level 1 decompresses without work memory, its pages have no coded literals */
static int seqlz_create_dctx(struct zcomp_params *params,
			     struct zcomp_ctx *ctx)
{
	if (params->level == SEQLZ_LEVEL_MIN)
		return 0;
	ctx->context = kzalloc(SEQLZ_MEM_DECOMPRESS, GFP_KERNEL);
	return ctx->context ? 0 : -ENOMEM;
}
#else
/*
 * The work memory of compression and of decompression, per CPU. zram holds
 * the stream's mutex for either, and neither keeps anything in it from one
 * page to the next, so they share it: kmalloc() gives 8 KiB for the larger,
 * 16 KiB for both apart.
 */
static int seqlz_create(struct zcomp_params *params, struct zcomp_ctx *ctx)
{
	ctx->context = kzalloc(max(SEQLZ_MEM_COMPRESS, SEQLZ_MEM_DECOMPRESS),
			       GFP_KERNEL);
	return ctx->context ? 0 : -ENOMEM;
}
#endif

static void seqlz_destroy(struct zcomp_ctx *ctx)
{
	kfree(ctx->context);
}

static int seqlz_zcomp_compress(struct zcomp_params *params,
				struct zcomp_ctx *ctx, struct zcomp_req *req)
{
	int ret;

	/* seqlz compresses one block of a page, see the static_assert above */
	if (req->src_len != PAGE_SIZE)
		return -EINVAL;
	ret = seqlz_compress(req->src, req->dst, req->dst_len, ctx->context,
			     params->level);
	if (ret < 0)
		return ret;
	req->dst_len = ret;
	return 0;
}

static int seqlz_zcomp_decompress(struct zcomp_params *params,
				  struct zcomp_ctx *ctx, struct zcomp_req *req)
{
	/* seqlz writes a whole block */
	if (req->dst_len < PAGE_SIZE)
		return -EINVAL;
	return seqlz_decompress(req->src, req->src_len, req->dst, ctx->context);
}

const struct zcomp_ops backend_seqlz = {
	.compress	= seqlz_zcomp_compress,
	.decompress	= seqlz_zcomp_decompress,
#ifdef ZCOMP_RW_SPLIT
	.create_cctx	= seqlz_create_cctx,
	.destroy_cctx	= seqlz_destroy,
	.create_dctx	= seqlz_create_dctx,
	.destroy_dctx	= seqlz_destroy,
#else
	.create_ctx	= seqlz_create,
	.destroy_ctx	= seqlz_destroy,
#endif
	.setup_params	= seqlz_setup_params,
	.release_params	= seqlz_release_params,
	.name		= "seqlz",
};
