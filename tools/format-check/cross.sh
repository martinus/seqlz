#!/bin/bash
# SPDX-License-Identifier: MIT OR GPL-2.0-only
#
# cross.sh <out dir> <pages>...: step 11 of docs/format-check.md, seqlz-all-decoders on 32-bit x86 and on
# big-endian s390x, the other CPUs the kernel's seqlz runs on that differ most from the 64-bit little
# endian ones. Builds it with plain gcc, without CMake, as CI's same-bytes job does: with -m32 on the
# host, and in podman's gcc:14 image for s390x under qemu, its tables generated there. Prints each
# one's summary; exits 1 on a difference. The pages are copied into <out dir>, which the container
# mounts: pages from memory hold keys and passwords, so <out dir> stays on this machine.
set -euo pipefail
[[ $# -ge 2 ]] || { sed -n '4,10s/^# \?//p' "$0" >&2; exit 2; }
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

status=0
echo "== 32-bit x86"
(cd "$repo" && OUT=$out HOSTCC=gcc bash -c "$build" build m32 -m32)
(cd "$out" && ./all-decoders-m32 "${pages[@]}") | tail -1 || status=1
echo "== s390x, big endian"
podman run --rm --platform linux/s390x -v "$repo:/w:ro" -v "$out:/out" -w /w -e OUT=/out -e HOSTCC=gcc \
    docker.io/library/gcc:14 bash -c "$build" build s390x
podman run --rm --platform linux/s390x -v "$out:/out" -w /out docker.io/library/gcc:14 \
    ./all-decoders-s390x "${pages[@]}" | tail -1 || status=1
exit $status
