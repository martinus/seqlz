# src/zram_seqlz.c

seqlz as zram calls a codec, for the project's benchmark harness: three codecs, `seqlz-1` to
`seqlz-3`, one per level of `seqlz_compress_block()`, all read by the same decoder. The harness has a copy
of zram's interface, `struct bench_codec`, in `bench/kernel_codecs/zram_codec.h`
([the harness's copy](#ch-zram-glue--harness)). The kernel does not get this file; its
backend, `backend_seqlz.c`, goes into the tree with the copy of the codec that `port.py` writes
([the kernel's copy](#ch-zram-glue--port)).

zram's calls in short ([what zram calls](book:#ch-zram--zcomp)): once per device `setup_params()`;
once per CPU `create_ctx()`, which allocates the work memory; then per page `compress()` or
`decompress()`. A series of October 2026, not merged yet, splits the context in two, `create_cctx()`
for writing and `create_dctx()` for reading; the harness has that form. A reading order:

1. [`compress()`](#b40), the one compress function, and [`decompress()`](#b114).
2. The work memory: [the hash table](#b29) for writing, [the scratch](#b89) for reading.
3. The three [codec objects](#b79) that tie the functions together.

The panels under this intro are about other files: the harness's interface, then the kernel's side,
from `port.py` through the interface and the tables to the backend, and last the checks and tests
that keep the kernel's copy and `src/` right.
@more {#ch-zram-glue--harness} The harness's copy of zram's interface
zram's interface to a backend, `struct zcomp_ops`, and its streams, one per CPU and locked by a mutex,
are in [chapter 1](#ch-zram--zcomp). The harness has a copy of that interface in
`bench/kernel_codecs/zram_codec.h`, shortened:

```c
struct bench_params {  /* struct zcomp_params: one per device */
	const void *dict;
	size_t dict_size;
	int level;
	unsigned page_size;   /* the harness runs 4 KiB and 16 KiB pages */
	void *drv_data;
	size_t allocated;     /* what the codec allocated, as the allocator hands it out */
};

struct bench_stream {  /* struct zcomp_ctx: one per CPU and direction */
	void *context;
	size_t allocated;
};

struct bench_codec {   /* struct zcomp_ops */
	const char *name;
	int (*setup_params)(struct bench_params *p);
	void (*release_params)(struct bench_params *p);
	int (*create_cctx)(struct bench_params *p, struct bench_stream *s);
	void (*destroy_cctx)(struct bench_stream *s);
	int (*create_dctx)(struct bench_params *p, struct bench_stream *s);
	void (*destroy_dctx)(struct bench_stream *s);
	int (*compress)(struct bench_params *p, struct bench_stream *s,
			const void *src, unsigned int src_len, void *dst,
			unsigned int *dst_len);
	int (*decompress)(struct bench_params *p, struct bench_stream *s,
			  const void *src, unsigned int src_len, void *dst,
			  unsigned int *dst_len);
};
```

The two context functions are those of Sergey Senozhatsky's series of October 2026
([explored-designs.md](https://github.com/martinus/seqlz/blob/main/docs/explored-designs.md#zrams-contexts-split-into-compression-and-decompression-the-same-time-in-the-vm-the-same-memory-for-seqlz)).
A codec without `create_dctx` gets the compression context in `decompress()`, as every backend did before
the split; the harness's `lz4`, `lzo` and `zstd` are copies of the kernel's glue from before it.
@end
@more {#ch-zram-glue--port} How `port.py` writes the kernel's copy from `src/`
A patch for `lib/` needs exports, `Kconfig` and `Makefile` entries, the zram backend, documentation,
KUnit tests, and none of the harness's functions. A second copy by hand would drift, so
`port.py <linux tree>` writes it, and its docstring opens with the rule: "src/ stays the one source of
the codec." A fix goes into `src/` or `tools/kernel-port/`, never into the copy. Since 30th October
2026 the files of `src/` have the kernel's includes, guards, interface and exports themselves, so for
the codec the copy is nearly a copy.

| file in the tree | from |
| --- | --- |
| `include/linux/seqlz.h` | `src/linux/seqlz.h`, for blocks of 4 KiB only |
| `lib/seqlz/seqlz_format.h`, `seqlz_tables.h`, `seqlz_internal.h` | `src/`, without what only the harness uses |
| `lib/seqlz/seqlz_compress.c` | `src/seqlz_compress.c`, with `seqlz_encoder.h` and `seqlz_match.h` in place |
| `lib/seqlz/seqlz_compress_literals.c`, `seqlz_decompress.c`, `seqlz_decompress_literals.c` | `src/` |
| `lib/seqlz/gen_seqlz_tables.c` | `src/seqlz_codes.c` and `tools/gen-tables/gen_seqlz_tables.c`: a program for the build machine, which writes the tables, `seqlz_tables.c`, when the kernel is built |
| `lib/seqlz/seqlz_lengths.h` | the code lengths, written in the tree by its `scripts/gen-seqlz-lengths.py` from `lib/seqlz/seqlz_counts.txt`; `src/seqlz_lengths.h` is the same file without `static` |
| `scripts/gen-seqlz-lengths.py`, `lib/seqlz/seqlz_counts.txt` | `tools/kernel-port/gen-seqlz-lengths.py` and `bench/seqlz_counts_4k.txt` |
| `lib/seqlz/tests/seqlz_kunit.c` | `tools/kernel-port/seqlz_kunit.c`, with values from `docs/format.md` filled in |
| `lib/Kconfig`, `lib/Kconfig.debug`, `lib/Makefile`, `lib/seqlz/Makefile` | `CONFIG_SEQLZ`, and `CONFIG_SEQLZ_KUNIT_TEST` with the other KUnit tests of `lib/`; `-O3` as `lib/lz4` |
| `drivers/block/zram/backend_seqlz.[ch]`, `Kconfig`, `Makefile`, `zcomp.c` | the backend, `CONFIG_ZRAM_BACKEND_SEQLZ` |
| `Documentation/staging/seqlz.rst` | `docs/format.md`, by `format_rst.py` |
| `MAINTAINERS`, `Documentation/admin-guide/blockdev/zram.rst`, `Documentation/staging/index.rst` | an entry each |
| `arch/x86/include/asm/processor.h` | `x86-prefetcht0.patch`, unless the tree has it |
@end
@more {#ch-zram-glue--what-portpy-changes-in-the-code} What `port.py` changes in the code
The whole diff between `src/seqlz_decompress.c` and its copy:

```text
- * number of extra bits, which follow the code. Takes up to SEQLZ_MAX_BITS +
- * SEQLZ_BLOCK_BITS bits, 20 or 22; the caller refills before.
+ * number of extra bits, which follow the code. Takes up to
+ * SEQLZ_MAX_BITS + SEQLZ_BLOCK_BITS bits, 20; the caller refills before.
- * 50, 11 for each token and up to 14 for each offset; after a slow one it
+ * 46, 11 for each token and up to 12 for each offset; after a slow one it
```

- **Blocks of 4 KiB only.** `#define SEQLZ_BLOCK_BITS 12U` in place of `SEQLZ_PAGE_BITS`, and
  `SEQLZ_BLOCK_SIZE` of the interface `4096U`, whatever the kernel's `PAGE_SIZE` is: the library
  compresses blocks of 4 KiB and builds with 16 KiB and 64 KiB pages too. The values for 16 KiB blocks
  are cut, e.g. `SEQLZ_HASH_BITS` becomes `12U` and `SEQLZ_MEM_COMPRESS` `8192`, and so are the numbers
  for them in the comments, as in the diff above. The format for 16 KiB blocks is not fixed yet, and
  with them `seqlz-2` asks for twice `lz4`'s work memory per CPU
  ([format.md](https://github.com/martinus/seqlz/blob/main/docs/format.md#status)). A library in
  `lib/` that built only with 4 KiB pages would fail the first build bot with 64 KiB pages on arm64.
- **The encoder and the matcher in place.** What follows the includes of `seqlz_encoder.h` and
  `seqlz_match.h` goes into `seqlz_compress.c`, right before `compress_block()`, and its includes of
  them and of `seqlz.h` become one of `seqlz_internal.h`: in the kernel they have one user, and a
  reviewer reads one file.
- **`seqlz_compress_block()` `static`**, without its test of the level, which the interface does; a
  one-line comment says that it is for the interface below, which checked the level.
- **`seqlz_codes.c` as a program for the build machine**, `gen_seqlz_tables.c`
  ([at build time](#ch-codes--kernel)), the one that writes the tables of `src/` too, with
  `seqlz_tables_build()` `static`.
- **What only the harness uses is cut**: all of `src/seqlz.h` and `src/seqlz_harness.c`,
  `seqlz_code_raw_literals()` and its declaration, and the cast in `seqlz_internal.h` that the C++ of the
  harness needs. The `raw` parameter of `code_literals()` stays: at level 3 it carries the deltas
  ([`seqlz_code_own()`](#ch-literals-enc--code-own)).

Every textual replacement goes through `must()` or `cut()`, which assert that the text occurs exactly as
often as expected, so a change in `src/` that moves such a text stops `port.py` instead of writing a copy
that silently missed a step.
@end
@more {#ch-zram-glue--what-portpy-refuses} What `port.py` refuses to write
Before writing anything to disk, `port.py` checks every line it produced:

```python
{{ch15.port.harness_only}}
{{ch15.port.cpu_model}}
{{ch15.port.forbidden}}
```

`CPU_MODEL` applies to `lib/` only. The decoder once had a loop of its own for in-order cores, chosen by
the arm64 MIDR register; the review of the kernel copy
([#152](https://github.com/martinus/seqlz/issues/152)) replaced the two loops with one, and this check
keeps it that way. `FORBIDDEN` catches what a kernel reader cannot follow: the repository's documents
and tools, issue numbers, the project's names, and reasons that hold only for the project's machines,
such as the Mi 9T's 4.14 kernel. Since commit `6bff3de` the comments of `src/` give their reasons in plain
words, so few need a change. Those that do are rewritten through `REWRITES`, pairs of old and new text,
e.g. `"docs/format.md is the specification."` becomes
`"Documentation/staging/seqlz.rst specifies the format."`; a changed comment is wrapped again to 80
columns. A rewrite that matched nothing fails the run: its comment is gone, and the rewrite has to go
with it.
@end
@more {#ch-zram-glue--init} The kernel's tables, generated when the kernel is built
`port.py` writes the code lengths, `lib/seqlz/seqlz_lengths.h`, with the tree's own
`scripts/gen-seqlz-lengths.py` from `lib/seqlz/seqlz_counts.txt`, so anybody can make them again from
the counts. When the kernel is built, `gen_seqlz_tables`, a program compiled for the build machine,
writes `seqlz_tables.c` into the build directory from those lengths
([at build time](#ch-codes--kernel)). So the patch carries counts, lengths and two generators, not
{{size.tables|grp}} bytes of tables. The result is `const struct seqlz_tables seqlz_tables`, the
kernel's one set, shared by every device and CPU: read-only data costs nothing to share, each core
caches its own copy of the lines, and a stray write faults instead of corrupting every page compressed
later. There is nothing to build at boot, so the module has no init and no exit function.

`CONFIG_SEQLZ` is a tristate, but the bool `CONFIG_ZRAM_BACKEND_SEQLZ` selects it, so it is built in
whenever the backend is enabled, also with `ZRAM=m`, as `ZRAM_BACKEND_LZ4` selects `LZ4_COMPRESS`.
The KUnit tests include `seqlz_lengths.h`, whose arrays are `static`, to check the code lengths
against the specification, so the kernel exports the two functions only.
@end
@more {#ch-zram-glue--backend} The kernel's backend, `backend_seqlz.c`, and its 8 KiB per CPU
The backend, `tools/kernel-port/backend_seqlz.c`, is modelled on zram's `backend_lz4.c` and is a
template for zram trees with and without the October 2026 series.

The figure shows where it sits when zram writes or reads a page: zram takes this CPU's stream, zcomp
passes its buffers and the backend's context to the backend, and the backend hands the context to the
library as its work memory. The contexts are the form with the split; without it, one union serves
both directions. The names of zram and zcomp are those of a tree without the series, which may name
the streams of the split differently.

<figure class="diagram">
<div class="scroll">
<svg viewBox="0 0 720 420" role="img" aria-label="The call path of a page written and a page read: zram, zcomp, backend_seqlz.c and lib/seqlz, and the two contexts per CPU that the backend allocates, with the bytes kmalloc() hands out for each.">
  <defs><marker id="ch-zram-glue-arrow" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0 0 L10 5 L0 10 z" fill="currentColor"/></marker></defs>
  <g font-size="12" font-weight="600">
    <text x="263" y="18" text-anchor="middle">write a page</text>
    <text x="565" y="18" text-anchor="middle">read a page</text>
  </g>
  <g class="mut" font-size="11">
    <text x="6" y="54">zram_drv.c</text>
    <text x="6" y="125">zcomp.c</text>
    <text x="6" y="212">backend_seqlz.c</text>
    <text x="6" y="270">lib/seqlz/</text>
    <text x="6" y="336">per CPU</text>
  </g>
  <g fill="none" stroke="currentColor">
    <rect x="118" y="30" width="290" height="40" rx="3" class="fill-sheet"/>
    <rect x="420" y="30" width="290" height="40" rx="3" class="fill-sheet"/>
    <rect x="118" y="88" width="592" height="24" rx="3" class="fill-paper" stroke-dasharray="4 3"/>
    <rect x="118" y="130" width="290" height="40" rx="3" class="fill-sheet"/>
    <rect x="420" y="130" width="290" height="40" rx="3" class="fill-sheet"/>
    <rect x="118" y="188" width="290" height="40" rx="3" class="fill-sheet"/>
    <rect x="420" y="188" width="290" height="40" rx="3" class="fill-sheet"/>
    <rect x="118" y="246" width="290" height="40" rx="3" class="fill-sheet"/>
    <rect x="420" y="246" width="290" height="40" rx="3" class="fill-sheet"/>
  </g>
  <g fill="none" stroke="currentColor" marker-end="url(#ch-zram-glue-arrow)">
    <line x1="263" y1="70" x2="263" y2="87"/><line x1="565" y1="70" x2="565" y2="87"/>
    <line x1="263" y1="112" x2="263" y2="129"/><line x1="565" y1="112" x2="565" y2="129"/>
    <line x1="263" y1="170" x2="263" y2="187"/><line x1="565" y1="170" x2="565" y2="187"/>
    <line x1="263" y1="228" x2="263" y2="245"/><line x1="565" y1="228" x2="565" y2="245"/>
  </g>
  <g font-size="12" text-anchor="middle">
    <text x="263" y="47">zram_write_page()</text>
    <text x="565" y="47">zram_read_page()</text>
    <text x="414" y="104" font-size="11">zcomp_stream_get(): this CPU's stream, its mutex held for the call</text>
    <text x="263" y="147">zcomp_compress()</text>
    <text x="565" y="147">zcomp_decompress()</text>
    <text x="263" y="205">seqlz_zcomp_compress()</text>
    <text x="565" y="205">seqlz_zcomp_decompress()</text>
    <text x="263" y="263">seqlz_compress()</text>
    <text x="565" y="263">seqlz_decompress()</text>
  </g>
  <g class="mut" font-size="11" text-anchor="middle">
    <text x="263" y="63">a page not filled with one repeated word</text>
    <text x="565" y="63">read_compressed_page()</text>
    <text x="263" y="163">dst: the stream's buffer of 2 pages</text>
    <text x="565" y="163">src: in zsmalloc, or local_copy</text>
    <text x="263" y="221">wrkmem = ctx->context, params->level</text>
    <text x="565" y="221">wrkmem = ctx->context</text>
    <text x="263" y="279">its static seqlz_compress_block()</text>
    <text x="565" y="279">the decoder itself</text>
  </g>
  <g class="acc">
    <g fill="none" stroke="currentColor" marker-end="url(#ch-zram-glue-arrow)">
      <line x1="263" y1="286" x2="263" y2="309"/><line x1="565" y1="286" x2="565" y2="309"/>
    </g>
    <rect x="118" y="310" width="290" height="56" rx="3" class="fill-acc" fill-opacity="0.12" stroke="currentColor"/>
    <rect x="420" y="310" width="290" height="56" rx="3" class="fill-acc" fill-opacity="0.12" stroke="currentColor"/>
    <g font-size="12" text-anchor="middle">
      <text x="263" y="327">compression context</text>
      <text x="565" y="327">decompression context</text>
    </g>
  </g>
  <g font-size="11" text-anchor="middle">
    <text x="263" y="342">the matcher's hash table, {{size.state|grp}} bytes</text>
    <text x="263" y="357">kmalloc-8k: {{ch15.mem.cctx_gets|grp}} bytes, every level</text>
    <text x="565" y="342">the scratch, {{const.SCRATCH|grp}} bytes</text>
    <text x="565" y="357">kmalloc-8k: {{ch15.mem.dctx_gets|grp}} bytes, levels 2 and 3</text>
  </g>
  <g font-size="12">
    <text x="118" y="390" class="acc">seqlz-1: {{ch01.cctx_kib}} KiB per CPU, seqlz-2 and seqlz-3: {{ch01.split_kib}} KiB per CPU</text>
    <text x="118" y="410" class="mut" font-size="11">without the split: one union of the two, {{ch01.union_kib}} KiB per CPU at every level</text>
  </g>
</svg>
</div>
<figcaption>A page written and a page read, from zram to the library, and the work memory per CPU that the backend allocates when a CPU comes up, as <code>kmalloc()</code> hands it out; level 1 has no decompression context.</figcaption>
</figure>

The contexts without the split, and `seqlz_destroy()`, which both forms share:

@listing tools/kernel-port/backend_seqlz.c {{ch15.backend.create.a}}-{{ch15.backend.create.b}}

What counts per CPU is what the allocator hands out ([chapter 1](#ch-zram--work-memory)). A struct of the
hash table and the scratch side by side was {{size.state}} + {{const.SCRATCH}} =
{{ch15.state_plus_scratch|grp}} bytes, above `KMALLOC_MAX_CACHE_SIZE`, and `kvzalloc()` took 16 KiB for it.
As a union it is {{size.state}} bytes from `kmalloc-8k`. `lz4` asks `vmalloc()` for 16 416 bytes, which is
5 pages, 20 480
([plan.md](https://github.com/martinus/seqlz/blob/main/docs/plan.md#33-per-cpu-workspace-is-real-memory-on-a-phone)):
12 KiB less per CPU, on the Mi 9T's 8 cores 96 KiB, resident whether the phone swaps or not.
`kzalloc_obj(*c)` is a newer typed spelling of `kzalloc(sizeof(*c), GFP_KERNEL)`, as zram's other
backends allocate their contexts; `GFP_KERNEL` may sleep, which is fine when a CPU comes up. Two
`static_assert`s at the top of the file ([the setup](#ch-zram-glue--backend-params)) keep
`SEQLZ_MEM_COMPRESS` and `SEQLZ_MEM_DECOMPRESS` at most `KMALLOC_MAX_CACHE_SIZE`: larger, `kzalloc()`
would take pages of a higher order straight from the page allocator, which can fail under
fragmentation when a CPU comes up.
@assert const.SCRATCH <= size.state <= 8192 < ch15.state_plus_scratch <= 16384

::: why The union is safe because of zram's mutex
Commit `6063362` ([#154](https://github.com/martinus/seqlz/issues/154)) gives two reasons. zram holds
the stream's mutex for either operation, so they never run at the same time on one context. And neither
keeps anything from one page to the next: the matcher clears its table for every page, the decoder
writes its scratch before it reads it. The commit ran the kernel copy under KASAN, lockdep and UBSan: the corpus written and read back at the
two levels of then, 0 of 20 000 pages different; 10 minutes of swap with 4 workers per boot, 16.5
million pages out at level 2 and 16.9 million at level 1, 0 different.
:::

With the split, two functions and no union:

@listing tools/kernel-port/backend_seqlz.c {{ch15.backend.split.a}}-{{ch15.backend.split.b}}

Both contexts come from `kmalloc-8k`: 16 KiB per CPU at levels 2 and 3, 8 KiB at level 1, which has no
decompression context, and `kfree(NULL)` does nothing. The plan's score counts both, as work memory
per CPU.
@end
@more {#ch-zram-glue--backend-params} The backend's setup: 4 KiB pages, no dictionary, levels 1 to 3
@listing tools/kernel-port/backend_seqlz.c {{ch15.backend.head.a}}-{{ch15.backend.head.b}}

`pr_fmt` comes before the first include: `pr_err()` and its relatives wrap their format string in it.
The `static_assert` says what zram's `Kconfig` makes sure of, `depends on PAGE_SIZE_4KB`: zram hands the
backend whole pages, and seqlz takes blocks of 4 KiB.
`ZCOMP_RW_SPLIT` is no kernel macro but a marker: `port.py` keeps the `#ifdef` parts if the tree's
`zcomp.h` has `struct zcomp_cstrm`, else the `#ifndef` parts, and removes the marker's lines.
`seqlz_release_params()` is empty but must exist, since zram calls it without a check for `NULL`. A
dictionary is refused with `-EOPNOTSUPP`, zram's "not set" level becomes level 2, and any other level
than 1 to 3 is `-EINVAL`, as `lz4` refuses invalid levels. The level is set per device:

```text
echo "algo=seqlz level=1" > /sys/block/zram0/algorithm_params
```
@end
@more {#ch-zram-glue--backend-calls} The backend's compress and decompress
@listing tools/kernel-port/backend_seqlz.c {{ch15.backend.pages.a}}-{{ch15.backend.pages.b}}

The harness's functions in the kernel's types: zram passes `2 * PAGE_SIZE` as `dst_len` to compress and
`PAGE_SIZE` to decompress, and the level comes from the params. In the union form `->cmem` and `->dmem`
are the same address; the member says which use is meant. Neither function prefetches: the VM's
measurements of [the prefetch](#ch-zram-glue--prefetch) used experimental code,
`tools/zram-vm/zram-prefetch.patch` and the VM's own backend. `port.py` adds `&backend_seqlz` to
`zcomp.c`'s list, the entries to zram's `Kconfig` and `Makefile`, and a paragraph to zram's
documentation.
@end
@more {#ch-zram-glue--prefetch-patch} `prefetch()` on x86-64: a patch to the kernel itself
On x86-64 the kernel's `prefetch()` was `__builtin_prefetch()`, which clang drops under `-mno-sse`, so a
kernel built with clang, as Android's are, had no prefetches in the decoder
([the story and the measurement](#ch-compat--prefetch)). No inline assembly goes into
`lib/`, so the fix is a patch to the kernel, `tools/kernel-port/x86-prefetcht0.patch`, which defines
`ARCH_HAS_PREFETCH` for 64-bit in `arch/x86/include/asm/processor.h`:

```c
#ifdef CONFIG_X86_64
/*
 * Every x86-64 CPU has prefetcht0. Not __builtin_prefetch(): the kernel
 * builds with -mno-sse, and clang then drops the builtin without a warning.
 */
static inline void prefetch(const void *x)
{
	asm("prefetcht0 %0" : : "m" (*(const char *)x));
}
#else
```

In the measurement of the patch's commit message, `lz4`, which does not prefetch, swapped in the same
with and without it. 32-bit x86 patches in `prefetchnta`, a non-temporal prefetch, on every CPU with SSE,
which would change what gcc builds get, so the patch does not use that. `port.py` applies it unless the
tree has it, and [`src/linux/prefetch.h`](book:#ch-compat--prefetch) does the same outside the kernel.
@end
@more {#ch-zram-glue--check} What `check.sh` checks before a submission
`tools/kernel-port/check.sh <linux tree> [make args]` takes the tree's `HEAD` with `git archive`, so the
tree is never touched, commits the x86 patch on that base so that checkpatch sees seqlz's part alone,
runs `port.py`, and then:

1. configures `defconfig` with zram and the backend built in, seqlz and its KUnit tests as modules;
2. builds `lib/seqlz/` and `drivers/block/zram/` with `W=1`, the warnings the kernel leaves off by
   default because older code triggers them, and fails on any warning;
3. on x86-64, fails if `seqlz_decompress.o` has no `prefetcht0`, the check that the patch works with
   clang;
4. runs `checkpatch.pl --strict`, the style checker reviewers run before they read a patch, on the diff
   and on the x86 patch;
5. runs `scripts/kernel-doc -none -Wall -Werror` on the codec's files and the headers;
6. parses `Documentation/staging/seqlz.rst` with docutils, if Python has it;
7. compares the `seqlz_tables.c` that the build generated with the one `gen-tables.sh` writes in
   userspace, and fails if a 64-bit division calls a library function, which the kernel
   does not have on 32-bit CPUs;
8. on arm64, builds the library and its tests again with 16 KiB and 64 KiB pages, with zram's backend
   off, as zram's `Kconfig` keeps it to 4 KiB pages.

CI runs it for x86-64 with gcc, and with clang for arm64, the phones' architecture, 32-bit arm, whose
32-bit `long` and pointers show a computation that assumes 64 bits, and s390, which is big-endian. The
32-bit kernel warns about stack frames above 1280 bytes, against 2048 on 64-bit. With gcc 16.2.1, x86-64
`defconfig` and the Makefile's `-O3`, the deepest frames were `seqlz_decode_literals()`, 640 bytes, and
`code_literals()`, 600, on 22nd October 2026, near 1000 with KASAN and UBSan. Since 23rd October
`code_literals()` is inlined into its callers, and on 24th October `seqlz_code_own()`, which has it
inside, had the 600 bytes (commit `8993922`). Since 11th October 2026 `check.sh` prints the
three deepest frames of each build and fails at 1024 bytes, the limit of older kernels' 32-bit
`FRAME_WARN`.
@end
@more {#ch-zram-glue--kunit-tests-in-uml} KUnit tests in UML, and the specification as reST
**KUnit** is the kernel's unit test framework: test functions take a `struct kunit *`, check with macros
like `KUNIT_EXPECT_EQ()`, and run inside a kernel with its allocator and headers. **UML**, User Mode
Linux, is the kernel built as an ordinary program, `ARCH=um`, which boots in a second without a virtual
machine. `port.py` writes `lib/seqlz/tests/seqlz_kunit.c`, {{ch15.kunit.cases}} cases:

- `seqlz_test_tables`: the SHA-256 of each table's code lengths against `docs/format.md`. The template's
  placeholders, like `"@TOK_4k@"`, are filled from the specification's table, so the kernel's tests check
  against the specification, not a copy.
- `seqlz_test_example`: the specification's {{ab.len}} bytes that decode to a page of `ab`; the same bytes
  without the last one, and with a zero byte more, must be `-EINVAL`.
- `seqlz_test_round_trip`: generated pages of {{ch15.kunit.kinds}} kinds at every level.
- `seqlz_test_damaged`: damaged pages are rejected or decode, never out of bounds.
- `seqlz_test_one_sequence`: pages built by hand, each valid or invalid by one rule of the specification.
- `seqlz_test_coded_literals`: pages whose literals get coded.
- `seqlz_test_own_table`: pages with a table of their own, whose runs the decoder takes in one lookup
  of `quad[]` or alone, and tables that are invalid by one rule each.

The test's `decompress()` copies the input into an allocation of exactly its length, "so that KASAN sees
a read behind them": KASAN, the kernel's address sanitizer, reports a read past an allocation only where
the allocation ends. `tools/kernel-port/kunit.sh` runs `kunit.py run --kunitconfig=lib/seqlz` on such a
copy of the tree; CI runs it on x86-64 with KASAN and UBSan, and on arm64 with 64 KiB pages in qemu,
where a block is a sixteenth of a page. `tools/kernel-port/stress.sh`, run by hand,
boots the kernel copy in a VM with KASAN, lockdep, `DEBUG_ATOMIC_SLEEP` and UBSan, writes a corpus to zram
with `lz4` and seqlz at every level, swaps under memory pressure with every page compared, and runs the
kernel's zram selftests: the gate of the plan's Phase 5, and the source of the page counts of
[the union](#ch-zram-glue--backend).

The specification goes along: `tools/kernel-port/format_rst.py` writes `docs/format.md` as
`Documentation/staging/seqlz.rst`. It handles only what `format.md` uses and fails on anything else, "so
that a new construct in format.md is noticed instead of turned into wrong reST". HTML comments mark text
only for the repository and text only for the kernel.
@end
@more {#ch-zram-glue--tests} The two doctests of this file
`test/` has the doctest unit tests, one binary, `seqlz_test`. Two of them check this file.
`seqlz takes no dictionary, its tables are part of the format` gives the codecs a dictionary of the size
of `struct seqlz_lengths`, which once replaced the code lengths, and checks that a page compresses to the
same bytes as without. `seqlz compresses with the hash table, seqlz-2 decompresses with the
scratch` checks the context sizes, and that `seqlz-1` decodes with a stream without a context.
@end
@more {#ch-zram-glue--fuzzing} The fuzzers, and a second decoder from the specification
`fuzz/` has four targets for libFuzzer, which AFL++ runs too:

- `seqlz_decode_fuzz.c`: any input through `seqlz_decompress()`, with and without the scratch, each buffer an
  allocation of exactly its size, so that AddressSanitizer sees one byte too far. A page that decodes
  without the scratch must decode the same with it.
- `seqlz_roundtrip_fuzz.c`: any page through `seqlz_compress_block()` and `seqlz_decompress()`, at every level. It must come back, fit into 2 pages, and be the bytes `seqlz_encode()` writes for
  `seqlz_find()`'s sequences at that level.
- `seqlz_encode_fuzz.c`: any list of sequences through `seqlz_encode()`, also lists that make no page,
  added after an encoder overflow ([#121](https://github.com/martinus/seqlz/issues/121)) that the other
  targets could not reach, since the matcher's sequences are always valid.
- `seqlz_diff_fuzz.c`: the decoder against `tools/seqlz-ref/seqlz_ref.c`, a second decoder written from
  `docs/format.md` alone, "so that a misreading in src/ is not shared". It reads every bit on its own and
  checks every rule where the specification states it. Any disagreement on validity or on the page is a
  bug in one of them, or an unclear sentence of the specification.

`fuzz/smoke.sh` runs all four for 60 seconds each under AddressSanitizer and UndefinedBehaviorSanitizer;
CI runs it for 4 KiB and 16 KiB pages and once with MemorySanitizer, which reports a read of a byte the
codec did not write for this input. On the diff target, `docs/format.md` records two long AFL++ runs: on
6th October 2026, 10 instances for 2 hours, 1.7 billion inputs, and on 9th October, with the code of commit
`5342a9b`, 12 instances for 2 hours, 1.94 billion, no difference found in either
([format.md](https://github.com/martinus/seqlz/blob/main/docs/format.md#how-this-file-was-checked)).
ClusterFuzzLite runs the targets daily, an hour each with AddressSanitizer, UndefinedBehaviorSanitizer and
MemorySanitizer.
@end
@more {#ch-zram-glue--the-same-bytes-on-a-big-endian-cpu} The same bytes on a big-endian CPU
The format's multi-byte values are little endian, and the code reads and writes them with
`get_unaligned_le16()` and its relatives. Until commit `91fa53e` on 5th October 2026 it did not, and on
the big-endian s390x every page failed to round trip ([how](#ch-compat--endian)).
`test/seqlz_endian.c` keeps it fixed: 600 generated pages of six kinds, each compressed at every level
and decoded again, all compressed bytes hashed with FNV-1a. It is plain C, so it builds in
seconds under qemu. Its output on x86-64, and the same with `-m32`:

```text
{{ch15.endian.line}}
```

`coded {{ch15.endian.coded}}` counts the compressions, of {{ch15.endian.runs|grp}}, that came out with
coded literals, `own table {{ch15.endian.own}}` those among them with a table of the page's own. CI's
`same-bytes` job builds it for x86-64, 32-bit x86 and s390x in qemu and `diff`s the three lines.
@end
@more {#ch-zram-glue--ci} What CI runs, and what it does not cover
| job | what it runs |
| --- | --- |
| `format` | `clang-format` 21.1.8 on every `.c`, `.h` and `.cpp` |
| `test` | `seqlz_test` built 4 ways: gcc debug with sanitizers, clang release, gcc release on arm64, gcc release with sanitizers for 16 KiB pages |
| `fuzz` | `fuzz/smoke.sh 60` for 4 KiB, 16 KiB and MSan |
| `same-bytes` | `test/seqlz_endian.c` on x86-64, 32-bit x86 and s390x, outputs diffed |
| `docs` | `tools/seqlz-ref/check.sh` and `tools/check-links.py` |
| `book` | this book, `docs/book/build.py --check` and `lint.py`; `pages` publishes it from `main` |
| `kernel-port` | `check.sh` for x86-64 (gcc), arm64, arm and s390 (clang), and `kunit.sh` with KASAN and UBSan, and on arm64 with 64 KiB pages |
| `ci-ok` | the one required check |

The `test` job greps doctest's summary for a non-zero number of passed assertions, because a binary that
ran no test also exits 0. `ci-ok` `needs` all the others and runs with `if: always()`, then checks that
each reported `success`: without `always()` a failed dependency would make it skipped, and GitHub counts a
skipped required check as passed. With one required job, the others can be renamed or split without
touching the branch settings.

::: warn What none of this covers
All these checks run on the CPUs and compilers CI has. The decode times and the memory come from separate
measurements, in the kernel VM and on the Mi 9T, which CI does not repeat. And this file is not what
the kernel runs: `backend_seqlz.c` is exercised by the KUnit tests and `stress.sh`, and reached by the
harness's benchmarks only with `PORT=1` in the VM.
:::
@end

## Setting up

@@ 1-9
@anchor ch-zram-glue
zram can give a device a dictionary, e.g. for `zstd`. seqlz's tables are fixed and part of the
format, so there is nothing a dictionary could change, and this file ignores it; the kernel's backend
refuses one ([its setup](book:#ch-zram-glue--backend-params)).
@more {#ch-zram-glue--head} The three levels, and why the tables are no dictionary
`seqlz-2` lets `seqlz_code_literals()` ([the literal coder](#ch-literals-enc)) code the literals where
that saves $\frac{1}{16}$ of them and {{ch15.coded_min}} bytes, `seqlz-3` adds the lazy matcher, a
literal table of the page's own and the literals as deltas to earlier bytes of the page, and codes
them from {{ch11.coded_min_own}} bytes on
([the levels](#ch-header--compressor)). In the kernel they are levels 1 to 3 of one backend, `seqlz`,
with 2 the default; the harness names them as three codecs to compare them side by side.

A dictionary could have carried other tables, e.g. tables trained on one phone's pages, and measured,
those saved too little ([fixed tables](#ch-codes--fixed)). So since commit `87a0ba4` the tables are
part of the format, and a doctest checks that a dictionary changes no byte
([the doctests](#ch-zram-glue--tests)). The file includes [`seqlz.h`](#f-seqlz-h), the userspace
interface, and `zram_codec.h`, the harness's copy of zram's interface.
@end

@@ 10-17
`setup_params()`, once per device. seqlz is compiled for one page size, so a 4 KiB build given a
corpus of 16 KiB pages fails here instead of reading past every page. No tables are built: they are
`const` data, generated when seqlz is built, shared by every device and CPU.
@more {#ch-zram-glue--setup} Tables per device until October 2026, and why not
Lengths from a bad training stop the build that generates the tables
([at build time](#ch-codes--kernel)), not a benchmark. The kernel generates them the same way
([the kernel's tables](#ch-zram-glue--init)).

Building {{ch15.tables_kb}} KB of tables in `setup_params()` for every zram device and priority, from
constant data, was what the kernel copy did until commit `aaa381e`; the review asked for the global.
Until 22nd October 2026 the harness still allocated a set per device and passed it to every call, and
every function of the codec took it as a parameter; later that day it built the global in every
`setup()`, and then nowhere.
@end

@@ 18-28
Nothing per device to release. `destroy()` takes only the stream, so it serves to free both kinds of
context.

## Work memory and compress

@@ 29-39
The compression context, one per CPU: `struct seqlz_state`, the matcher's hash table of 8192 bytes,
for all three levels. `bench_zalloc()` stands for the kernel's `kzalloc()` and counts what the
allocator would hand out, so the harness can report memory per CPU
([work memory](book:#ch-zram--work-memory)). The zeroing is not needed, the matcher clears its table
for every page.
@more {#ch-zram-glue--cctx} What the hash table's memory holds
`struct seqlz_state` is a union of the matcher's hash table and a page of bytes
([what lies where](#ch-header--state)): {{ch17.hash_slots}} entries of 2 bytes, {{size.state}} bytes, and
`seqlz_code_literals()` writes the coded streams into the same memory once the matcher is done, and
level 3 mostly a copy of the raw literals
([in place](#ch-literals-enc--in-place)). `kzalloc()` zeroes in the kernel, and the harness does the same. The kernel builds without the warning about unused
parameters below `W=3`, so its backend needs no `(void)p`.
@assert ch17.hash_slots * 2 == size.state and size.state >= 4096
@end

@@ 40-54
@anchor ch-zram-glue--fast
`*dst_len` goes in as the size of `dst` and comes out as the compressed length: zram's convention.
zram gives a compressor 2 pages of output, and a page always fits into 2 pages
([two pages](book:#ch-encoder--two-pages)), so `seqlz_compress_block()` returns 0, "did not fit", only for a
smaller buffer, e.g. the one page zswap gives.

The level is a parameter that zram's `compress()` does not have, so each codec gets a wrapper of one
line.

@@ 55-61
The wrapper of `seqlz-1`: raw literals, no literal table.

@@ 62-78
`seqlz-1` has no decompression context, so `s` here is the compression context, and the decoder does
not use it: pages with raw literals need no work memory. `NULL` as the scratch makes a page with coded
literals invalid, which `seqlz-1` never writes. In zram with the split, a codec without `create_dctx`
gets no context at all here, which [a doctest](book:#ch-zram-glue--tests) checks on every test page.

`bench_prefetch_page()` asks the CPU to start loading the compressed bytes and every line of the
output page before the decoder needs them ([prefetch](book:#ch-cpu--prefetch)). The kernel's backend
prefetches neither, so the harness's decode times include prefetches the kernel does not do; the
kernel VM's prefetch measurements used an experimental zram patch. The `*dst_len` check comes first, so a too small `dst` is never written.
@more {#ch-zram-glue--prefetch} What `bench_prefetch_page()` prefetches, and what it was measured at
The prefetch is in the harness's header:

@listing bench/kernel_codecs/zram_codec.h {{ch15.prefetch_page.a}}-{{ch15.prefetch_page.b}}

It asks for the compressed page from its second cache line on, and for every line of the destination
with the second argument `1`, "for writing". The CPU's own prefetcher follows a pointer walking forward,
but the decoder reads a page in several places at once: the literals, the bitstream behind them, and up
to 8 streams. The code does not say why it starts at `src + 64`; the first line is likely loaded by the
decoder's read of the header at about the same moment.

::: measured Prefetch the compressed data, not the destination
In the kernel VM, with the compressed data flushed from the cache, the prefetch of it took `lz4`'s p99
read latency from 5600 to 3891 ns, and seqlz's from 5450 to 4410 ns; the destination's prefetch helped
nothing there
([explored-designs.md](https://github.com/martinus/seqlz/blob/main/docs/explored-designs.md#zram-prefetch-the-compressed-data-before-decompression)).
On the Mi 9T, the prefetch of the compressed data made the A55's warm reads 2.3 µs faster, and the
destination's as well cost the A76 2 µs on cold reads
([explored-designs.md](https://github.com/martinus/seqlz/blob/main/docs/explored-designs.md#7-the-phones-module-did-not-prefetch-the-compressed-data-23-µs-faster-warm-reads-on-the-a55)).
The harness still prefetches the destination for every codec that calls it, `lz4-prefetch` and both
codecs of this file. For decode times the project trusts the kernel's numbers, from the VM and the
phone.
:::
@end

@@ 79-88
The fields not named are null. No `create_dctx` tells the harness, as it tells zram after the split,
that this codec decompresses without a context of its own. Not `static`: `bench/bench_main.cpp`
names it.

## Levels 2 and 3

@@ 89-99
The scratch that coded literals are decoded into before the sequences are copied
([`seqlz_decode_literals()`](book:#ch-literals-dec)): a page plus 16 bytes, 4112 on 4 KiB pages, because the
decoder's 16-byte copies read up to 15 bytes past the last literal. With the hash table that is
12 304 bytes per CPU, which `kmalloc()` hands out as 16 384, below `lz4`'s 20 480.
@more {#ch-zram-glue--fast-lit} The 16 spare bytes, and what the split cost in memory
The 16 spare bytes cover two things: the reads of the decoder's 16-byte copies, and the last step
of `seqlz_decode_literals()`, which writes up to 8 bytes past the last literal. So `seqlz-2` has
{{size.state}} bytes per CPU for compression and {{const.SCRATCH}} for decompression,
{{ch15.state_plus_scratch|grp}} bytes, which `kmalloc()` hands out as {{ch15.mem.split_gets|grp}}. The
harness's one context before the split was a struct of both, also {{ch15.mem.struct_gets|grp}} bytes
handed out, so the split cost seqlz no memory. The kernel's backend without the split shares one
allocation between the two ([the backend](#ch-zram-glue--backend)).
@end

@@ 100-113
Level 3 needs no more work memory than level 2: its own literal table is worked out in the hash
table's memory, and decoded into the output page and the scratch.
@more {#ch-zram-glue--level-3} Where level 3 puts its table, its records and its deltas
`seqlz_compress_block()` writes the raw literals first and codes them over the raw ones in the same buffer
where that pays, so the same 2 pages of output suffice. Level 3's table of the page's own is worked
out in the compression context, behind the copy of the literals
([the work memory](#ch-literals-enc--the-work-memory)), and decoded into the output page and the
scratch ([own table](#ch-literals-dec--own)); the records for the deltas go into the output buffer,
and the deltas into the compression context ([the records](#ch-encoder--records)).
@end

@@ 114-125
The decompress of `seqlz-2` and `seqlz-3`, with the scratch as `s->context`. One function for both:
the page itself says, in byte 2, which table it uses and whether its literals are deltas. Pages with
raw literals leave the scratch alone, so a page of any level decodes here.

@@ 126-148
The same codec twice with a different level. Both have `create_dctx`, so zram gives them a second
context, the scratch, for reading.
