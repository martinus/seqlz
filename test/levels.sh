#!/usr/bin/env bash
# SPDX-License-Identifier: MIT OR GPL-2.0-only
#
# levels.sh <dir> [cc args]: seqlz built with SEQLZ_LEVELS=1, 2 and 3, as the kernel series' patches add
# the levels one at a time, each with test/seqlz_endian.c and tables generated for it, into <dir>. Each
# build has to pass seqlz_endian, which also checks that it takes blocks of the levels it lacks for
# invalid, and has to write the same blocks at the levels it has as the build with all three.
# cc args go to every compile, e.g. -m32. The compiler is $CC, or gcc.
set -euo pipefail
[[ $# -ge 1 ]] || { echo "usage: test/levels.sh <dir> [cc args]" >&2; exit 2; }
out=$1
shift
root=$(cd "$(dirname "$0")/.." && pwd)
mkdir -p "$out"
for levels in 3 2 1; do
    "$root/tools/gen-tables/gen-tables.sh" "$out/tables$levels.c" -DSEQLZ_LEVELS=$levels
    # as the kernel's flags: -Wno-unused-parameter as its W=1, below level 3 some parameters are there for
    # it alone; -Wdeclaration-after-statement as all its builds, which a level's #if can break
    "${CC:-gcc}" -std=gnu11 -O2 -Wall -Wextra -Wno-unused-parameter -Wdeclaration-after-statement -Wundef -Werror \
        -DSEQLZ_LEVELS=$levels "$@" \
        -I"$root/src" -o "$out/endian$levels" "$root/test/seqlz_endian.c" "$root"/src/seqlz_*.c \
        "$out/tables$levels.c"
    "$out/endian$levels" >"$out/endian$levels.txt" || { cat "$out/endian$levels.txt"; exit 1; }
    echo "SEQLZ_LEVELS=$levels: $(head -n 1 "$out/endian$levels.txt")"
    # line 1 is the hash of all levels, then one line per level
    if ! diff <(sed -n "2,$((levels + 1))p" "$out/endian3.txt") <(tail -n +2 "$out/endian$levels.txt"); then
        echo "levels.sh: SEQLZ_LEVELS=$levels writes other blocks than the build with all levels" >&2
        exit 1
    fi
done
