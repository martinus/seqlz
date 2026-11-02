#!/bin/bash
# SPDX-License-Identifier: MIT OR GPL-2.0-only
#
# check.sh [dir]: docs/format.md against the code, for CI. seqlz_ref.py checks the SHA-256 of every
# table in src/ against the one docs/format.md gives, and then decodes
# pages that src/ compressed, with raw and with coded literals: each must give the page back.
# A retrain of the tables without new hashes in docs/format.md fails here, and so does a format change
# that the spec does not describe. The pages are made up, never from a dump: tools/seqlz-worst/pages/,
# and six written here, three of them with deltas, one for each. First, the tables in src/ must be what tools/kernel-port/gen-seqlz-lengths.py
# makes from the counts in bench/, byte for byte, and the longest code of each table what docs/format.md's
# table of tables says. Last, the claims of docs/format.md that tools/format-check/claims.py checks. Needs
# cc and python3.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/../.." && pwd)
out=${1:-$(mktemp -d)}
mkdir -p "$out"

if ! (cd "$repo" && tools/kernel-port/gen-seqlz-lengths.py --extern bench/seqlz_counts.txt | cmp -s - src/seqlz_lengths.h); then
    echo "src/seqlz_lengths.h is not what tools/kernel-port/gen-seqlz-lengths.py --extern makes" >&2
    exit 1
fi
# and it refuses lengths that seqlz_tables_build() refuses, so the kernel's tables built from what it writes
# cannot fail
python3 - "$repo/tools/kernel-port/gen-seqlz-lengths.py" <<'EOF'
import importlib.util
import sys

spec = importlib.util.spec_from_file_location("gen", sys.argv[1])
gen = importlib.util.module_from_spec(spec)
spec.loader.exec_module(gen)
gen.check("complete", [1, 2, 3, 3], 3, True)
gen.check("an escaped symbol", [1, 2, 3, 3, 0], 3, False)
for lengths, every in [([1, 2, 2, 2], True), ([1, 2, 3], True), ([1, 2, 4, 4, 4, 4], True), ([1, 2, 3, 3, 0], True)]:
    try:
        gen.check(str(lengths), lengths, 3, every)
    except SystemExit:
        continue
    sys.exit(f"gen-seqlz-lengths.py takes the lengths {lengths}, which seqlz_tables_build() refuses")
EOF

"$repo/tools/gen-tables/gen-tables.sh" "$out/seqlz_tables.c"
${CC:-cc} -O2 -I"$repo/src" -o "$out/compress" "$repo/tools/seqlz-compress/compress.c" \
    "$repo"/src/seqlz_*.c "$out/seqlz_tables.c"
cp "$repo"/tools/seqlz-worst/pages/*.page "$out/"
python3 - "$out" "$repo/docs/format.md" <<'EOF'
import random
import struct
import sys

out, text = sys.argv[1], open(sys.argv[2], "rb").read()
open(f"{out}/ab.page", "wb").write(b"ab" * 2048)
open(f"{out}/pointers.page", "wb").write(b"".join(struct.pack("<Q", 0x7F1234560010 + 16 * i) for i in range(512)))
open(f"{out}/text.page", "wb").write(text[:4096])
# pages whose literals level 3 codes as deltas, one for each of the three: fields of 4 bytes whose 3 high
# bytes every second field repeats, fields of 8 bytes whose 3 high bytes repeat, and runs of a 3-byte
# pattern, each followed by 8 bytes a little above the byte 6 before them (the last offset 3 counts as 6)
rng = random.Random(229)
p = bytearray()
while len(p) < 4096:
    high = bytes(rng.randrange(256) for _ in range(3))
    p += bytes([rng.randrange(256)]) + high + bytes([rng.randrange(256)]) + high
open(f"{out}/delta4.page", "wb").write(p)
high = bytes(rng.randrange(256) for _ in range(3))
p = b"".join(bytes(rng.randrange(256) for _ in range(5)) + high for _ in range(512))
open(f"{out}/delta8.page", "wb").write(p)
p = bytearray()
while len(p) < 4096:
    p += bytes(rng.randrange(256) for _ in range(3)) * 4
    for _ in range(8):
        p.append((p[-6] + 1 + rng.randrange(3)) & 255)
open(f"{out}/delta-last3.page", "wb").write(p[:4096])
EOF
"$out/compress" "$out"/*.page >/dev/null

# the column "longest code" of docs/format.md's table of tables, both copies, against the tables: a
# retrain changes the tables' hashes there, and this keeps it from leaving the column behind
python3 - "$here" "$repo/docs/format.md" <<'EOF'
import re
import sys

sys.path.insert(0, sys.argv[1])
import seqlz_ref

longest = {name: max(lengths) for name, lengths in seqlz_ref.load_lengths().items()}
spec = open(sys.argv[2]).read()
rows = re.findall(r"^\| (?:[^|]*`(\w+)`|literal tables 0 to 7) \| [^|]* \| (\d+) bits \|", spec, re.M)
seen = [(name or "LIT", int(bits)) for name, bits in rows]
if sorted({n for n, _ in seen}) != sorted(longest) or len(seen) != 2 * len(longest):
    sys.exit(f"docs/format.md's table of tables: rows {seen}, expected each of {sorted(longest)} twice")
for name, bits in seen:
    if bits != longest[name]:
        sys.exit(f"docs/format.md says the longest code of {name} is {bits} bits, the table's is {longest[name]}")
EOF

pages=0
wrong=0
for p in "$out"/*.page; do
    want=$(sha256sum "$p" | cut -d' ' -f1)
    for c in "$p.1" "$p.2" "$p.3"; do
        got=$(python3 "$here/seqlz_ref.py" "$c" | cut -d' ' -f2-)
        if [[ "$got" != "valid $want" ]]; then
            echo "$(basename "$c"): $got, the page is $want" >&2
            wrong=$((wrong + 1))
        fi
        pages=$((pages + 1))
    done
done
echo "the tables' hashes match docs/format.md, $wrong of $pages compressed pages decode wrong with seqlz_ref.py"
[[ $wrong -eq 0 ]]
python3 "$repo/tools/format-check/claims.py" | tail -1
