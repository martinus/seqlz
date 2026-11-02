#!/usr/bin/env python3
# SPDX-License-Identifier: MIT OR GPL-2.0-only
"""measure_claims.py [--pages N] <compress> <pages>...: step 10 of docs/format-check.md, the numbers of
docs/format.md's *Why:* sentences that only pages can give. The first N pages (default 2000) of each
file are compressed at levels 1 to 3 by <compress>, a build of tools/seqlz-compress/compress.c, and
parsed as tools/seqlz-ref/seqlz_ref.py parses them, the bits of each field counted. Prints counts only,
never page contents: pages from memory hold keys and passwords, so this runs locally, not in CI."""

import argparse
import collections
import multiprocessing
import subprocess
import sys
import tempfile
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(REPO / "tools" / "seqlz-ref"))

import seqlz_ref
from seqlz_ref import Bits, P

TABLES = seqlz_ref.load_tables()
OFFSET_BITS = [0, 1, 8, P, 5, 8]
OFFSET_SCALE = [1, 2, 1, 1, 8, 8]


def count(block):
    """one valid block's counts"""
    tok_codes, ll_codes, ml_codes, lit_codes, own_codes = TABLES
    c = collections.Counter(blocks=1)
    size = 1 << P
    h = block[0] | block[1] << 8
    n, dt = h & 0x7FFF, 0
    if h & 0x8000:
        t = block[2]
        w, code, e = 5 + (t >> 4 & 7), t & 15, t >> 7
        c["coded"] += 1
        c["coded_w"] += w
        if e:
            dt = block[3]
        d = 0
        if code == 8:
            codes, d = seqlz_ref.own_table(block, 3 + w + e, own_codes)
            c["own"] += 1
            c["own_bytes"] += d
            c["own_values"] += len(codes)
            c["own_long"] += sum(1 for length, _ in codes if length >= 7)
            c["own_40_100"] += 40 <= d <= 100
        else:
            codes = lit_codes[code]
        sizes = int.from_bytes(block[3 + e : 3 + e + w], "little")
        s = [sizes >> (j * w) & ((1 << w) - 1) for j in range(8)]
        c["largest_below_128"] += max(s) < 128
        start = [3 + w + e + d]
        for j in range(8):
            start.append(start[-1] + s[j])
        lits, lit_bits = [0] * n, [0] * n
        for j in range(8):
            r = Bits(block, start[j])
            for k in range(j, n, 8):
                at = r.pos
                lits[k] = r.symbol(codes)
                lit_bits[k] = r.pos - at
        bits = Bits(block[start[8] :])
    else:
        lits, lit_bits = list(block[2 : 2 + n]), [8] * n
        bits = Bits(block[2 + n :])

    def value(vcodes):
        sym = bits.symbol(vcodes)
        if sym < 16:
            return sym
        return (1 << (sym - 12)) + bits.read(sym - 12)

    out, used, last = bytearray(), 0, 1

    def copy(k):
        nonlocal used
        for i in range(k):
            b, p = lits[used + i], len(out)
            x = {1: 4, 2: 8, 3: last}.get(dt, 0)
            while 0 < x < 4:
                x += last
            if dt and p >= x:
                c["delta_literals"] += 1
                c["delta_zero"] += b == 0
                b = (b + out[p - x]) & 255
                c["delta_zero_without"] += b == 0
            out.append(b)
            c["literals"] += 1
            c["literal_bits"] += lit_bits[used + i]
        used += k

    while n - used != size - len(out):
        at = bits.pos
        t = bits.symbol(tok_codes)
        if t == 3072:
            t = bits.read(12)
        ll, mlf, cl = t & 15, (t >> 4) & 31, t >> 9
        off = bits.read(OFFSET_BITS[cl]) * OFFSET_SCALE[cl] if cl else last
        match_bits = bits.pos - at
        if ll == 15:
            ll = 15 + value(ll_codes)
        copy(ll)
        at = bits.pos
        ml = mlf + 4 if mlf < 31 else 35 + value(ml_codes)
        match_bits += bits.pos - at
        last = off
        c["matches"] += 1
        c[f"class {cl}"] += 1
        if cl:
            c[f"offset {off}"] += 1
        if ml == 4:
            c["match_4"] += 1
            c["match_4_bits"] += match_bits
        for _ in range(ml):
            out.append(out[-off])
    copy(n - used)
    return c


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--pages", type=int, default=2000)
    ap.add_argument("compress")
    ap.add_argument("files", nargs="+")
    args = ap.parse_args()
    total = {level: collections.Counter() for level in (1, 2, 3)}
    with tempfile.TemporaryDirectory() as tmp, multiprocessing.get_context("fork").Pool() as pool:
        for k, f in enumerate(args.files):
            names = []
            with open(f, "rb") as pages:
                for i in range(args.pages):
                    page = pages.read(4096)
                    if len(page) < 4096:
                        break
                    names.append(f"{tmp}/{k}-{i}")
                    Path(names[-1]).write_bytes(page)
            for i in range(0, len(names), 500):
                subprocess.run([args.compress, *names[i : i + 500]], check=True, stdout=subprocess.DEVNULL)
            for level in (1, 2, 3):
                blocks = (Path(f"{name}.{level}").read_bytes() for name in names)
                for c in pool.imap_unordered(count, blocks, chunksize=64):
                    total[level].update(c)
            for name in names:
                for suffix in ("", ".1", ".2", ".3"):
                    Path(name + suffix).unlink()
    for level, t in total.items():
        print(f"level {level}: {t['blocks']} blocks, {t['coded']} with coded literals, {t['own']} with their own table")
        if t["coded"]:
            print(f"  C11: the largest stream below 128 bytes in {t['largest_below_128'] / t['coded']:.1%} of the coded blocks")
            print(f"  C13: w is {t['coded_w'] / t['coded']:.2f} on average, 2 bytes per size {16 - t['coded_w'] / t['coded']:.1f} "
                  f"bytes more per coded block, {(16 * t['coded'] - t['coded_w']) / t['blocks']:.2f} per block")
        print(f"  C41: a match of 4 bytes {t['match_4_bits'] / t['match_4']:.1f} bits of token, offset and length, "
              f"a literal {t['literal_bits'] / t['literals']:.2f} bits")
        explicit = t["matches"] - t["class 0"]
        offsets = sorted(((v, int(key[7:])) for key, v in t.items() if key.startswith("offset ")), reverse=True)
        print(f"  C45: the repeat offset {t['class 0'] / t['matches']:.1%} of the matches; of the others "
              + ", ".join(f"{o} {v / explicit:.1%}" for v, o in offsets[:4]))
        if t["delta_literals"]:
            print(f"  C56: {t['delta_zero'] / t['delta_literals']:.1%} of the literals as deltas are 0, "
                  f"{t['delta_zero_without'] / t['delta_literals']:.1%} would be without")
        if t["own"]:
            print(f"  C62: {t['own_long'] / t['own_values']:.1%} of the byte values of own tables have a code of 7 to 10 bits")
            print(f"  C63: {t['own_bytes'] / t['own_values']:.2f} bytes per byte value with a code, {t['own_bytes'] / t['own']:.1f} "
                  f"per table, {t['own_40_100'] / t['own']:.1%} of the tables 40 to 100 bytes")


if __name__ == "__main__":
    main()
