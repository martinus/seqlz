#!/usr/bin/env python3
# SPDX-License-Identifier: MIT OR GPL-2.0-only
"""retrain-tables.py: seqlz's tables trained again, everything that follows from the counts.

    tools/retrain-tables.py --train <build> --corpus <base> [--name <corpus name>]
    tools/retrain-tables.py --counts <counts file>

--train runs <build>/seqlz-train --corpus <base> --counts --lit-sets (a Release build: 11 min
for the corpus of 13th October), and the counts' head names the corpus as --name, default the
base's file name. --counts takes counts trained before. Then, from the repository's root:

- the 8 lit lines in the order that fits the committed literal tables best, by the sum of the
  differences of their code lengths, so that an unchanged table keeps its number (the order means
  nothing to the format);
- bench/seqlz_counts.txt, and src/seqlz_lengths.h by tools/kernel-port/gen-seqlz-lengths.py;
- the SHA-256 of each table in docs/format.md.

The counts' own line comes from the pages level 3 gives a table of their own with the tables <build> has
compiled in, the old ones: after a retrain, count it again with a build of the new tables,
seqlz-train --corpus <base> --own-counts <counts file>, and run this with --counts.

Prints which tables changed. Not done here: docs/format.md's example and Status, the example bytes in
test/seqlz_ref_test.cpp (both from tools/seqlz-compress/compress.c), the book's facts
(docs/book/build.py --check), the entry in docs/explored-designs.md."""

import argparse
import hashlib
import itertools
import re
import subprocess
import sys
import tempfile
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
GEN = "tools/kernel-port/gen-seqlz-lengths.py"
COUNTS = "bench/seqlz_counts.txt"
LENGTHS = REPO / "src" / "seqlz_lengths.h"
ORDER = (
    "# The 8 lit lines are in the order that matches the literal tables trained before them best, by the\n"
    "# sum of the differences of their code lengths: the order means nothing to the format.\n"
)


def gen(*args):
    return subprocess.run([sys.executable, GEN, *args], cwd=REPO, check=True, capture_output=True, text=True).stdout


def numbers(text):
    return [int(x) for x in re.findall(r"\d+", text)]


def lit_lengths(text):
    """the 8 literal tables' code lengths of a seqlz_lengths.h"""
    end = text.index("seqlz_own_lengths[SEQLZ_OWN_SYMBOLS] =")
    v = numbers(text[text.index("seqlz_lit_sets[SEQLZ_LIT_SETS][256] =") : end].split("=", 1)[1])
    return [v[256 * k : 256 * (k + 1)] for k in range(8)]


def own_lengths(text):
    """the code lengths of OWN, the code of a page's own literal table, of a seqlz_lengths.h"""
    return numbers(text[text.index("seqlz_own_lengths[SEQLZ_OWN_SYMBOLS] =") :].split("=", 1)[1])


def ordered(counts, old_text):
    """counts with the lit lines in the order that fits the committed literal tables best"""
    with tempfile.NamedTemporaryFile("w", suffix=".txt") as f:
        f.write(counts)
        f.flush()
        new = lit_lengths(gen("--extern", f.name))
    old = lit_lengths(old_text)
    cost = [[sum(abs(a - b) for a, b in zip(old[i], new[j])) for j in range(8)] for i in range(8)]
    perm = min(itertools.permutations(range(8)), key=lambda p: sum(cost[i][p[i]] for i in range(8)))
    lines = counts.split("\n")
    at = [k for k, line in enumerate(lines) if line.startswith("lit ")]
    if len(at) != 8:
        raise SystemExit(f"{len(at)} lit lines in the counts, 8 expected")
    lit = [lines[k] for k in at]
    for i, k in enumerate(at):
        lines[k] = lit[perm[i]]
    counts = "\n".join(lines)
    if list(perm) != list(range(8)) and ORDER not in counts:
        head = "# occurred as a literal on them.\n"
        if head not in counts:
            raise SystemExit("the counts' head has no line 'occurred as a literal on them.'")
        counts = counts.replace(head, head + ORDER, 1)
    return counts, perm


def table_hashes():
    """SHA-256 of each table's code lengths, as tools/seqlz-ref/seqlz_ref.py computes them"""
    src = LENGTHS.read_text()
    field = lambda n: numbers(re.search(r"\." + n + r"\s*=\s*\{([^}]*)\}", src).group(1))  # noqa: E731
    lit = sum(lit_lengths(src), [])
    tables = {"TOK": field("token"), "LL": field("ll"), "ML": field("ml"), "LIT": lit, "OWN": own_lengths(src)}
    return {k: hashlib.sha256(bytes(v)).hexdigest() for k, v in tables.items()}


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--train", metavar="BUILD", help="a build dir with seqlz-train")
    ap.add_argument("--corpus", help="the corpus base for --train")
    ap.add_argument("--name", help="the corpus' name in the counts' head, default the base's file name")
    ap.add_argument("--counts", help="counts trained before, instead of --train")
    args = ap.parse_args()
    if bool(args.train) == bool(args.counts) or bool(args.train) != bool(args.corpus):
        ap.error("--train and --corpus, or --counts")
    if args.train:
        with tempfile.TemporaryDirectory() as d:
            out = Path(d) / "counts.txt"
            train = Path(args.train) / "seqlz-train"
            r = subprocess.run([str(train), "--corpus", args.corpus, "--counts", str(out), "--lit-sets"],
                               capture_output=True, text=True)
            if r.returncode:
                raise SystemExit(f"{train} failed:\n{r.stdout}{r.stderr}")
            counts = out.read_text()
        counts = counts.replace(f"--corpus {args.corpus} ", f"--corpus {args.name or Path(args.corpus).name} ")
    else:
        counts = Path(args.counts).read_text()
    if not re.search(r"^token \d+ ", counts, re.M):
        raise SystemExit("no token line in the counts")

    before = table_hashes()
    counts, perm = ordered(counts, LENGTHS.read_text())
    (REPO / COUNTS).write_text(counts)
    LENGTHS.write_text(gen("--extern", COUNTS))
    after = table_hashes()

    spec = REPO / "docs" / "format.md"
    text = spec.read_text()
    for key, h in after.items():
        text, n = re.subn(rf"^(\| `{key}` \| `)[0-9a-f]{{64}}(` \|)$", rf"\g<1>{h}\g<2>", text, flags=re.M)
        if n != 1:
            raise SystemExit(f"docs/format.md has {n} rows for {key}, 1 expected")
    spec.write_text(text)

    print(f"lit lines in the order {list(perm)} of the trainer's")
    for key in after:
        print(f"{key}: {'changed' if after[key] != before[key] else 'the same'}")
    if after != before:
        print("next: docs/format.md's example and Status, test/seqlz_ref_test.cpp's example bytes "
              "(tools/seqlz-compress/compress.c), docs/book/build.py --check, docs/explored-designs.md")


if __name__ == "__main__":
    main()
