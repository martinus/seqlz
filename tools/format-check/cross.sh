#!/bin/bash
# SPDX-License-Identifier: MIT OR GPL-2.0-only
#
# cross.sh <out dir> <pages>...: step 11 of docs/format-check.md, seqlz-all-decoders on 32-bit x86 and on
# big-endian s390x, the other CPUs the kernel's seqlz runs on that differ most from the 64-bit little
# endian ones. Builds it with plain gcc, without CMake, as CI's same-bytes job does: with -m32 on the
# host, and in podman's gcc:14 image for s390x under qemu, its tables generated there. Prints each
# one's summary; exits 1 on a difference. The pages are copied into <out dir>, which the container
# mounts: pages from memory hold keys and passwords, so <out dir> stays on this machine. With DOCTEST_SRC,
# doctest's source dir (e.g. _deps/doctest-src of a build), also step 6's rule pairs and
# test/seqlz_ref_test.cpp, which hold every decoder against the blocks of the tests, on both.
set -euo pipefail
[[ $# -ge 2 ]] || { sed -n '4,12s/^# \?//p' "$0" >&2; exit 2; }
repo=$(cd "$(dirname "$0")/../.." && pwd)
out=$(realpath -m "$1")
shift
mkdir -p "$out/pages"
pages=()
for p in "$@"; do
    cp "$p" "$out/pages/"
    pages+=("pages/$(basename "$p")")
done

# the agents' D and R each an object of their own, cr_decode() renamed, see fuzz/decoders.h
# build <name> <compiler flags>...: $OUT/all-decoders-<name>, a script for bash -c here and in podman
# shellcheck disable=SC2016
build='set -e
name=$1
shift
tools/gen-tables/gen-tables.sh "$OUT/tables-$name.c" >/dev/null
gcc "$@" -std=c11 -O2 -Dcr_decode=cr_decode_d -c tools/format-check/cleanroom/D/src/cr_decode.c -o "$OUT/d-$name.o"
gcc "$@" -std=c11 -O2 -Dcr_decode=cr_decode_r -c tools/format-check/cleanroom/R/seqlz.c -o "$OUT/r-$name.o"
gcc "$@" -std=gnu11 -O2 -Isrc -Ifuzz -Itools/seqlz-ref -o "$OUT/all-decoders-$name" tools/format-check/all_decoders.c \
    tools/seqlz-ref/seqlz_ref.c src/seqlz_*.c "$OUT/tables-$name.c" "$OUT/d-$name.o" "$OUT/r-$name.o"'
# tests <name> <compiler flags>...: $OUT/tests-<name>, the two test files of seqlz_test that hold the decoders
# against each other, with what build made; $DOCTEST is doctest's source dir
# shellcheck disable=SC2016
tests='set -e
name=$1
shift
for f in src/seqlz_*.c tools/seqlz-ref/seqlz_ref.c "$OUT/tables-$name.c"; do
    gcc "$@" -std=gnu11 -O2 -Isrc -c "$f" -o "$OUT/t-$name-$(basename "$f" .c).o"
done
g++ "$@" -std=c++20 -O2 -Isrc -Itools/seqlz-ref -I"$DOCTEST" -o "$OUT/tests-$name" test/main.cpp \
    test/seqlz_rules_test.cpp test/seqlz_ref_test.cpp "$OUT"/t-"$name"-*.o "$OUT/d-$name.o" "$OUT/r-$name.o"'
doctest=()
[[ -z ${DOCTEST_SRC:-} ]] || doctest=(-v "$(realpath "$DOCTEST_SRC"):/doctest:ro" -e DOCTEST=/doctest)

status=0
echo "== 32-bit x86"
(cd "$repo" && OUT=$out HOSTCC=gcc bash -c "$build" build m32 -m32)
(cd "$out" && ./all-decoders-m32 "${pages[@]}") | tail -1 || status=1
if [[ -n ${DOCTEST_SRC:-} ]]; then
    (cd "$repo" && OUT=$out DOCTEST=$(realpath "$DOCTEST_SRC") bash -c "$tests" tests m32 -m32)
    "$out/tests-m32" | grep -E '^\[doctest\] (test cases|assertions)' || status=1
fi
echo "== s390x, big endian"
podman run --rm --platform linux/s390x -v "$repo:/w:ro" -v "$out:/out" -w /w -e OUT=/out -e HOSTCC=gcc \
    docker.io/library/gcc:14 bash -c "$build" build s390x
podman run --rm --platform linux/s390x -v "$out:/out" -w /out docker.io/library/gcc:14 \
    ./all-decoders-s390x "${pages[@]}" | tail -1 || status=1
if [[ -n ${DOCTEST_SRC:-} ]]; then
    podman run --rm --platform linux/s390x -v "$repo:/w:ro" -v "$out:/out" "${doctest[@]}" -w /w -e OUT=/out \
        docker.io/library/gcc:14 bash -c "$tests" tests s390x
    podman run --rm --platform linux/s390x -v "$out:/out" -w /out docker.io/library/gcc:14 \
        ./tests-s390x | grep -E '^\[doctest\] (test cases|assertions)' || status=1
fi
exit $status
