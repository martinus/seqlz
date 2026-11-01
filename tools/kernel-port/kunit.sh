#!/usr/bin/env bash
# SPDX-License-Identifier: MIT OR GPL-2.0-only
#
# kunit.sh <linux tree> [kunit.py args]: port.py into a copy of the tree's HEAD, then the KUnit tests of
# seqlz with the kernel's tools/testing/kunit/kunit.py, by default in UML. kunit.py args go to
# `kunit.py run`, e.g. --kconfig_add CONFIG_KASAN=y, or --arch=arm64 --cross_compile=... for qemu, or
# --make_options "CC=ccache gcc" for ccache, as CI does.
set -euo pipefail
[[ $# -ge 1 ]] || { echo "usage: tools/kernel-port/kunit.sh <linux tree> [kunit.py args]" >&2; exit 2; }
tree=$1
shift
here=$(cd "$(dirname "$0")" && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

git -C "$tree" archive HEAD | tar -x -C "$work" --one-top-level=src
python3 "$here/port.py" "$work/src" >/dev/null
cd "$work/src"
# for a ccache in CC: paths below $work relative and the build directory not hashed, so that the next run,
# in another mktemp -d, hits the cache
export CCACHE_BASEDIR="$work" CCACHE_NOHASHDIR=1
tools/testing/kunit/kunit.py run --kunitconfig=lib/seqlz --build_dir="$work/build" --jobs="$(nproc)" "$@"
