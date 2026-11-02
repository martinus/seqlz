#!/usr/bin/env python3
# SPDX-License-Identifier: MIT OR GPL-2.0-only
"""writer_items.py <dir>...: step 5 of docs/format-check.md, how often the blocks <k>.z in the dirs, which
W's cr-gen writes, have each item of step 5's list. Each block is parsed as tools/seqlz-ref/seqlz_ref.py
parses it, with what an item needs counted on the way. Prints each item with its count and marks those
below 100; for the tokens, how many of the 3072 were sent with their own code and escaped. Exits 1 if
an item is below 100 or a token is missing."""

import collections
import multiprocessing
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(REPO / "tools" / "seqlz-ref"))

import seqlz_ref
from seqlz_ref import Bits, P

TABLES = seqlz_ref.load_tables()
TOK_LENGTHS = seqlz_ref.load_lengths()["TOK"]
BLOCK = 1 << P
OFFSET_BITS = [0, 1, 8, P, 5, 8]
OFFSET_SCALE = [1, 2, 1, 1, 8, 8]
# the smallest and largest offset of each class with bits that a block allows: offset 0 is invalid, and
# a match of 4 bytes needs them after it
OFFSET_RANGE = {1: (2, 2), 2: (1, 255), 3: (1, BLOCK - 4), 4: (8, 248), 5: (8, 2040)}
# the largest length values a block allows: ll before a match of 4, ml after one byte
LL_MAX, ML_MAX = BLOCK - 4 - 15, BLOCK - 1 - 35


def edges(largest):
    """the smallest and largest value of each range of a length value, symbol 16 on"""
    return {v for b in range(4, 12) for v in (1 << b, min((1 << (b + 1)) - 1, largest)) if v <= largest}


def items(block):
    tok_codes, ll_codes, ml_codes, lit_codes, own_codes = TABLES
    c = collections.Counter()
    tokens = (set(), set())
    h = block[0] | block[1] << 8
    n, dt = h & 0x7FFF, 0
    if n in (1, 7, 8, 9, BLOCK - 4, BLOCK):
        c[f"layout: n = {n}"] += 1
    if h & 0x8000:
        t = block[2]
        w, code, e = 5 + (t >> 4 & 7), t & 15, t >> 7
        c["layout: coded literals"] += 1
        c[f"layout: code {code}"] += 1
        c[f"layout: w {w}"] += 1
        c[f"layout: e {e}"] += 1
        if e:
            dt = block[3]
            c[f"layout: dt {dt}"] += 1
        d = 0
        if code == 8:
            at = 3 + w + e
            r = Bits(block, at)
            value, space = 0, 0
            while space < 1024:
                sym = r.symbol(own_codes)
                if sym <= 9:
                    c[f"own table: code length {sym + 1}"] += 1
                    space += 1024 >> (sym + 1)
                    value += 1
                else:
                    k = sym - 9
                    x = (1 << (k - 1)) + r.read(k - 1)
                    if x == 1 << (k - 1):
                        c[f"own table: run k {k}, smallest x"] += 1
                    # a run of 255 leaves one byte value, and a complete table has two with a code
                    if x == min((1 << k) - 1, 254):
                        c[f"own table: run k {k}, largest x"] += 1
                    value += x
            codes, d = seqlz_ref.own_table(block, at, own_codes)
            if any(s == 255 for s in codes.values()):
                c["own table: last code byte value 255"] += 1
            if len(codes) == 2:
                c["own table: two byte values, 1 bit each"] += 1
            c[f"own table: ends at bit {(r.pos - 8 * at - 1) % 8 + 1} of its last byte"] += 1
        else:
            codes = lit_codes[code]
        sizes = int.from_bytes(block[3 + e : 3 + e + w], "little")
        s = [sizes >> (j * w) & ((1 << w) - 1) for j in range(8)]
        if w > max(5, max(s).bit_length()):
            c["layout: w larger than the sizes need"] += 1
        if n < 8:
            c["layout: streams with no literal"] += 1
        start = [3 + w + e + d]
        for j in range(8):
            start.append(start[-1] + s[j])
        lits = [0] * n
        for j in range(8):
            r = Bits(block, start[j])
            for k in range(j, n, 8):
                lits[k] = r.symbol(codes)
            if j < n:
                c[f"layout: a stream ends at bit {(r.pos - 8 * start[j] - 1) % 8 + 1} of its last byte"] += 1
        bits = Bits(block[start[8] :])
    else:
        c["layout: raw literals"] += 1
        lits = list(block[2 : 2 + n])
        bits = Bits(block[2 + n :])

    def value(vcodes):
        sym = bits.symbol(vcodes)
        if sym < 16:
            return sym
        return (1 << (sym - 12)) + bits.read(sym - 12)

    out, from_match, used, last, first = bytearray(), [], 0, 1, True

    def copy(k, last_sequence):
        nonlocal used
        for i in range(k):
            b, p = lits[used + i], len(out)
            x = {1: 4, 2: 8, 3: last}.get(dt, 0)
            while 0 < x < 4:
                x += last
            if dt and p < x:
                c["deltas: a literal before x, as it is"] += 1
            elif dt:
                if from_match[p - x]:
                    c["deltas: out[p - x] a byte of a match"] += 1
                if last_sequence:
                    c["deltas: in the last sequence"] += 1
                if dt == 3:
                    c[f"deltas: dt 3 with last {last if last <= 8 else 'above 8'}"] += 1
                b = (b + out[p - x]) & 255
            out.append(b)
            from_match.append(False)
        used += k

    while n - used != BLOCK - len(out):
        t = bits.symbol(tok_codes)
        escaped = t == 3072
        if escaped:
            t = bits.read(12)
        tokens[escaped].add(t)
        ll, mlf, cl = t & 15, (t >> 4) & 31, t >> 9
        raw = bits.read(OFFSET_BITS[cl])
        off = last if cl == 0 else raw * OFFSET_SCALE[cl]
        if cl == 0 and first:
            c["offsets: class 0 in the first sequence"] += 1
        if ll == 15:
            v = value(ll_codes)
            ll = 15 + v
            if v in edges(LL_MAX):
                c[f"ll: an edge of a range, {ll}"] += 1
        if ll in (0, 14, 15, 16):
            c[f"ll: {ll}"] += 1
        copy(ll, False)
        ml = mlf + 4
        if mlf == 31:
            v = value(ml_codes)
            ml = 35 + v
            if v in edges(ML_MAX):
                c[f"ml: an edge of a range, {ml}"] += 1
        if ml in (4, 34, 35, 36):
            c[f"ml: {ml}"] += 1
        if cl:
            lo, hi = OFFSET_RANGE[cl]
            if off == lo:
                c[f"offsets: class {cl}, smallest offset"] += 1
            if off == hi:
                c[f"offsets: class {cl}, largest offset"] += 1
        if cl == 3 and (off == 2 or off <= 255 or off % 8 == 0 and off <= 2040):
            c["offsets: class 3 for an offset of fewer bits"] += 1
        if off == len(out):
            c["offsets: offset |out|"] += 1
        if off < ml and off in (1, 2, 3, 8):
            c[f"offsets: off {off} below ml"] += 1
        last, first = off, False
        for _ in range(ml):
            out.append(out[-off])
            from_match.append(True)
        if len(out) == BLOCK:
            c["sequences: a match that ends at the end of the block"] += 1
    if used == 0 and n:
        c["sequences: the last sequence with all literals"] += 1
    if n - used == 0:
        c["sequences: the last sequence with no literals"] += 1
    copy(n - used, True)
    if n == BLOCK:
        c["sequences: literals only, no bits in B"] += len(bits.data) == 0
    if bits.data:
        c[f"bits: B ends at bit {(bits.pos - 1) % 8 + 1} of its last byte"] += 1
    else:
        c["bits: B empty"] += 1
    return c, tokens


def main():
    total = collections.Counter()
    tokens = (set(), set())
    files = sorted(f for d in sys.argv[1:] for f in Path(d).glob("*.z"))
    with multiprocessing.get_context("fork").Pool() as pool:
        for c, t in pool.imap_unordered(items, (f.read_bytes() for f in files), chunksize=64):
            total.update(c)
            tokens[0].update(t[0])
            tokens[1].update(t[1])
    want = (
        [f"layout: n = {n}" for n in (1, 7, 8, 9, BLOCK - 4, BLOCK)]
        + ["layout: raw literals", "layout: coded literals", "layout: w larger than the sizes need",
           "layout: e 0", "layout: e 1", "layout: streams with no literal"]
        + [f"layout: code {k}" for k in range(9)]
        + [f"layout: w {w}" for w in range(5, 13)]
        + [f"layout: dt {k}" for k in (1, 2, 3)]
        + [f"layout: a stream ends at bit {k} of its last byte" for k in range(1, 9)]
        + [f"ll: {v}" for v in (0, 14, 15, 16)]
        + [f"ll: an edge of a range, {15 + v}" for v in sorted(edges(LL_MAX))]
        + [f"ml: {v}" for v in (4, 34, 35, 36)]
        + [f"ml: an edge of a range, {35 + v}" for v in sorted(edges(ML_MAX))]
        + ["sequences: a match that ends at the end of the block", "sequences: the last sequence with no literals",
           "sequences: the last sequence with all literals", "sequences: literals only, no bits in B"]
        + [f"offsets: class {k}, {e} offset" for k in range(1, 6) for e in ("smallest", "largest")]
        + ["offsets: class 0 in the first sequence", "offsets: class 3 for an offset of fewer bits",
           "offsets: offset |out|"]
        + [f"offsets: off {k} below ml" for k in (1, 2, 3, 8)]
        + [f"deltas: dt 3 with last {k}" for k in (1, 2, 3, 4, 5, 7, 8, "above 8")]
        + ["deltas: a literal before x, as it is", "deltas: out[p - x] a byte of a match", "deltas: in the last sequence"]
        + [f"own table: code length {k}" for k in range(1, 11)]
        + [f"own table: run k {k}, {e} x" for k in range(1, 9) for e in ("smallest", "largest")]
        + ["own table: last code byte value 255", "own table: two byte values, 1 bit each"]
        + [f"own table: ends at bit {k} of its last byte" for k in range(1, 9)]
        + [f"bits: B ends at bit {k} of its last byte" for k in range(1, 9)]
        + ["bits: B empty"]
    )
    below = 0
    for item in want:
        mark = "  BELOW 100" if total[item] < 100 else ""
        below += total[item] < 100
        print(f"{total[item]:10} {item}{mark}")
    with_code = {t for t in range(3072) if TOK_LENGTHS[t]}
    missing = (len(with_code - tokens[0]), len(with_code - tokens[1]), len(set(range(3072)) - with_code - tokens[1]))
    print(f"tokens: {len(with_code)} have a code; not sent with it {missing[0]}, not escaped {missing[1]}; "
          f"of the {3072 - len(with_code)} without a code not escaped {missing[2]}")
    print(f"{len(files)} blocks, {len(want)} items, {below} below 100")
    sys.exit(1 if below or any(missing) else 0)


if __name__ == "__main__":
    main()
