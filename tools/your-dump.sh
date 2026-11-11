#!/usr/bin/env bash
# SPDX-License-Identifier: MIT OR GPL-2.0-only
#
# The table of docs/why.md on pages of your own: lz4, lzo-rle, zstd 3 and seqlz's three levels as zram
# swap in a kernel VM, the whole page fault of a swap-out and a swap-in, and the score of docs/plan.md
# §1.1 with the codecs that have the lowest score for some exchange rate of bytes against µs.
set -euo pipefail

usage() {
    cat >&2 <<'EOF'
usage: tools/your-dump.sh <build dir> <linux tree> <zram dump | corpus base> <out dir>

<build dir> is a CMake build of this repository (seqlz-import-raw, seqlz-sample-corpus, seqlz-score).
<linux tree> is a git checkout that tools/zram-vm/run.sh builds a kernel from, e.g. 986c24e0fe44, the
commit CI uses: .github/fetch-kernel.sh <dir> 986c24e0fe44f844b44d365b71ce831947f50298.
The pages are either a zram dump, the raw bytes of a zram device (docs/measuring.md, "Pages to measure
on"), or a corpus of this repository, <base>.pages and <base>.tsv. A random sample of PAGES (default
20000) of them is measured, BOOTS (default 3) boots, the codecs of ALGOS (default
lz4,lzo-rle,zstd,seqlz-1,seqlz-2,seqlz-3, at most 8). x86-64 with KVM and qemu-system-x86_64.
Memory is what zram holds per page, so zram's own fixed memory is in it: with 500 pages a codec took
more than 4096 bytes per page, use thousands.

<out dir> gets the sample, the VM's log swap.log and the score score.txt. The pages are whatever was
in memory, keys and passwords too: <out dir> is mode 700, delete it after use.

Fix the clock first, sudo tools/pc-bench.sh set: with boost on two identical runs differed by up to
520 ns per read. The VM is pinned to CPU 2. About 5 minutes for the 3 boots of the defaults, plus building the kernel.
EOF
    exit 2
}

[[ $# -eq 4 ]] || usage
build=$1
tree=$2
in=$3
out=$4
here=$(cd "$(dirname "$0")" && pwd)

for t in seqlz-import-raw seqlz-sample-corpus seqlz-score; do
    [[ -x $build/$t ]] || { echo "no $build/$t: cmake --build $build --target $t" >&2; exit 1; }
done
mkdir -p "$out"
chmod 700 "$out"

if [[ -f $in.pages ]]; then
    corpus=$in
elif [[ -f $in ]]; then
    "$build/seqlz-import-raw" --in "$in" --out "$out/dump" >&2
    corpus=$out/dump
else
    echo "no $in.pages and no file $in" >&2
    exit 1
fi
"$build/seqlz-sample-corpus" --corpus "$corpus" --out "$out/sample" --pages "${PAGES:-20000}" >&2

# the clock: a number to compare with docs/why.md needs min = max and no boost
"$here/pc-bench.sh" show 2 | tee "$out/clock.txt" >&2
if ! grep -q '^boost: 0' "$out/clock.txt" || ! awk '$1 == "cpu2:" { exit !($3 == $5) }' "$out/clock.txt"; then
    echo "warning: CPU 2's clock is not fixed, the times will not compare: sudo tools/pc-bench.sh set" >&2
fi

ALGOS=${ALGOS:-lz4,lzo-rle,zstd,seqlz-1,seqlz-2,seqlz-3} BOOTS=${BOOTS:-3} MODE=swap \
    KARGS=zram.zram_prefetch=8 "$here/zram-vm/run.sh" "$tree" "$out/sample" >"$out/swap.log"
"$build/seqlz-score" "$out/swap.log" | tee "$out/score.txt"
echo "$out has your pages: rm -rf $out when you are done" >&2
