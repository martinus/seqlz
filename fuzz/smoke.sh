#!/bin/bash
# smoke.sh [seconds] [build dir] [cmake args]: the five fuzz targets at the same time with libFuzzer, ASan
# and UBSan for the given time, 60 s by default, from make_seeds.c's seeds. Inputs that fail end up in
# <build dir>/crashes. For CI; the long runs are afl.sh. Needs clang. With -DSEQLZ_SANITIZE=OFF
# -DSEQLZ_MSAN=ON as cmake args, MSan instead of ASan and UBSan. LEVELS=1 or 2 fuzzes a seqlz built with
# that many levels, as the kernel series' first patches have it: decode and roundtrip only, as diff and
# gen compare with decoders of all levels and encode writes blocks of all levels.
set -euo pipefail
SECONDS_PER_TARGET=${1:-60}
LEVELS=${LEVELS:-3}
B=$(realpath -m "${2:-build-fuzz}")
REPO=$(cd "$(dirname "$0")/.." && pwd)

cmake -S "$REPO" -B "$B" -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DBUILD_TESTING=OFF -DSEQLZ_FUZZ=ON -DSEQLZ_SANITIZE=ON -DSEQLZ_WERROR=ON -DSEQLZ_FUZZ_LEVELS="$LEVELS" "${@:3}"
targets=(decode roundtrip diff encode gen)
[[ $LEVELS == 3 ]] || targets=(decode roundtrip)
cmake --build "$B" --target $(printf "seqlz_%s_fuzz " "${targets[@]}") seqlz-fuzz-seeds
mkdir -p "$B"/seeds-{decode,roundtrip} "$B/crashes"
"$B/seqlz-fuzz-seeds" "$B/seeds-decode" "$B/seeds-roundtrip"
# diff takes compressed pages, as decode does
ln -sfn seeds-decode "$B/seeds-diff"
# encode starts from nothing: its input is a list of sequences, and runs of zeros are bad ones; so does
# gen, whose input are the choices of a writer, any bytes a valid block
mkdir -p "$B/seeds-encode" "$B/seeds-gen"

pids=()
for t in "${targets[@]}"; do
    mkdir -p "$B/corpus-$t"
    # a hang is a failure too: the decoder has to return for any input
    "$B/seqlz_${t}_fuzz" -max_total_time="$SECONDS_PER_TARGET" -timeout=5 -print_final_stats=1 \
        -artifact_prefix="$B/crashes/$t-" "$B/corpus-$t" "$B/seeds-$t" > "$B/$t.log" 2>&1 &
    pids+=($!)
done
status=0
for pid in "${pids[@]}"; do
    wait "$pid" || status=1
done
for t in "${targets[@]}"; do
    echo "== $t"
    tail -n 15 "$B/$t.log"
done
exit $status
