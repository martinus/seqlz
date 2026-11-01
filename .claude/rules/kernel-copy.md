---
paths:
  - "src/**"
  - "tools/kernel-port/**"
---

# The kernel copy

Reviewers read the copy `tools/kernel-port/port.py` writes into a kernel tree, not `src/`. New code
follows these rules; open gaps in existing code: issues #151 to #157 (review of 10 Oct 2026).

- Generated, never edited: fix `src/` or `tools/kernel-port/`. To read it: `git archive` the kernel
  tree's `drivers/block/zram lib/Kconfig lib/Kconfig.debug lib/Makefile MAINTAINERS
  Documentation/admin-guide/blockdev/zram.rst Documentation/staging/index.rst
  arch/x86/include/asm/processor.h` into a dir, run `port.py <dir>` (it fails without the last two).
- Kernel helpers, not own ones: `get_unaligned_le16()`, `sizeof_field()`, `prefetch()`,
  `L1_CACHE_BYTES`, `BIT()`. No inline assembly, not even per architecture: fix a kernel gap with its
  own kernel patch, carried in `tools/kernel-port/` and applied by `port.py` (`x86-prefetcht0.patch`).
  That file is `git format-patch -1 --stdout` of branch `x86-prefetcht0-tip` (`gra cd moonnova`, on
  tip/master): change code or changelog there with `git commit --amend`, then write the file again.
  `src/linux/*.h` (not `seqlz.h`, the interface) = kernel headers outside the kernel; may do what such
  a patch does. `src/` includes exactly the kernel headers the kernel copy includes; Linux 4.14's gaps
  are in `tools/zram-phone/compat/`.
- Nothing in `lib/` depends on the CPU model; `port.py` fails on `read_cpuid` or `MIDR_` there (#152:
  one decode loop for all cores).
- `static` unless zram calls it. Grep-able names (not `count`, `refill`). Every number named or
  commented with its origin.
- Comments: what the code does, plus one line of why. Measurement numbers (phone, compiler, dumps) go
  to `docs/explored-designs.md` and the commit message, not `src/` (#156). The reason in plain words,
  never a heading of explored-designs.md or another repo doc; the only doc `src/` names is
  `docs/format.md` (the kernel gets it as `Documentation/staging/seqlz.rst`). When changing code, check
  the comments and kernel-doc above it (the 10 Oct review found 5 wrong comments, 6 dead file names).
- Style: `src/.clang-format` (kernel: tabs, 80 cols, `u8 *p`). clang-format does not reflow comments:
  wrap them by hand to 80 cols, kernel block form.
- Every kernel path runs in KUnit; every decoder path is reached by a fuzz target. Code change on an
  error path: update the KUnit error-path tests too (`seqlz_test_damaged` in
  `tools/kernel-port/seqlz_kunit.c`).
- Memory = what `kmalloc()`/`vmalloc()` hands out, not what is asked: 8192 + 4112 bytes in two
  allocations = 16 KiB, as one union = 8 KiB (#154).
- `tools/kernel-port/check.sh`: W=1 warning-free on x86-64, arm64, arm, s390; no new checkpatch
  `--strict` finding; stack frames < 1024 bytes (older kernels' 32-bit `FRAME_WARN`; now 1280, 2048 on
  64-bit). It prints the three deepest and fails at 1024; deepest are `seqlz_decode_literals()` and
  `seqlz_code_literals()`, near 1000 with KASAN+UBSan as `stress.sh` sets them; `read_own_table()` is `noinline`
  to keep its locals out of `seqlz_decode_literals()`.
- `check.sh` builds only what `port.py` keeps; the VM's and the phone's backends build all of `src/`,
  the harness-only functions (`HARNESS_ONLY`) too, with the kernel's frame limit. Keep their locals
  small: a 3.6 KB struct on the stack of the harness's level 2 coder passed `check.sh` and failed the VM
  build (31 Oct).
- The copy never names this repo, our machines, phones or dumps. `port.py` fails on names
  (`FORBIDDEN`), not on wording like "on pages of a phone"; also on a `REWRITES` entry that matches no
  comment any more (change or remove it with its comment).
- Once the codec is merged upstream, a decoder bug is a security bug: kernel security process, no
  disclosure to anyone (employer included) before the fix is public, fix upstream fast, `Fixes:` +
  `Cc: stable@vger.kernel.org`.
