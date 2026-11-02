#!/usr/bin/env python3
# SPDX-License-Identifier: MIT OR GPL-2.0-only
"""mutants.py <work dir> <pages>... [--only TEXT] [--fuzz SECONDS]: step 9 of docs/format-check.md.
Plants one difference at a time in seqlz_decompress(), each a change of one rule, in a copy of this
checkout's HEAD in <work dir>, and runs the checks of steps 4 and 6 on it: the rule pairs and the reference
tests of seqlz_test, and seqlz-all-decoders on the pages given. With --fuzz, a mutant they do not find gets
the fuzz targets diff and gen with libFuzzer, ASan and UBSan for that many seconds, which see a read past a
buffer that changes no verdict. A mutant counts as found when one of them fails, and only once it built: a
mutant that does not build is reported as such, never as found or survived. Prints a line per mutant;
exits 1 if one survived. The checkout itself is never changed."""

import argparse
import os
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent.parent
D = "src/seqlz_decompress.c"
L = "src/seqlz_decompress_literals.c"
T = "src/seqlz_tables.h"

# (name, the rule it breaks, file, the text, its replacement); the text must be in the file exactly once
MUTANTS = [
    ("len < 2 accepted as 1", "R1", D,
     "\tif (src_len < SEQLZ_HEADER)\n\t\treturn -EINVAL;", "\tif (src_len + 1U < SEQLZ_HEADER)\n\t\treturn -EINVAL;"),
    ("n = BLOCK + 1 accepted", "R2", D,
     "\tif (n_lit > SEQLZ_BLOCK_SIZE)\n\t\treturn -EINVAL;",
     "\tif (n_lit > SEQLZ_BLOCK_SIZE + 1U)\n\t\treturn -EINVAL;"),
    ("raw literals one byte past len", "R3", D,
     "if ((u64)SEQLZ_HEADER + n_lit > src_len)", "if ((u64)SEQLZ_HEADER + n_lit > (u64)src_len + 1U)"),
    ("an escaped token of 3072", "R13", D,
     "if (idx >= SEQLZ_TOKEN_SYMBOLS)", "if (idx > SEQLZ_TOKEN_SYMBOLS)"),
    ("fast path: an offset of |out| + 1", "R15", D,
     "\t\t\tif (off - 1U >= (unsigned int)(d - (u8 *)dst))\n\t\t\t\treturn -EINVAL;",
     "\t\t\tif (off - 1U > (unsigned int)(d - (u8 *)dst))\n\t\t\t\treturn -EINVAL;"),
    ("one literal more than are left", "R17", D,
     "nl > (unsigned int)(lit_end - lit) ||", "nl > (unsigned int)(lit_end - lit) + 1U ||"),
    ("literals one past the block's end", "R18", D,
     "\t\t    nl > (unsigned int)(d_end - d))", "\t\t    nl > (unsigned int)(d_end - d) + 1U)"),
    ("slow path: an offset of |out| + 1", "R15", D,
     "\t\tif (off - 1U >= (unsigned int)(d - (u8 *)dst) ||", "\t\tif (off - 1U > (unsigned int)(d - (u8 *)dst) ||"),
    ("a match one past the block's end", "R16", D,
     "\t\t    len > (unsigned int)(d_end - d))", "\t\t    len > (unsigned int)(d_end - d) + 1U)"),
    ("8 to 15 bits left at the end", "R12", D,
     "if ((unsigned int)br.count >= 8U || br.p != br.end ||",
     "if ((br.count >= 16 || br.count < 0) || br.p != br.end ||"),
    ("bits needed past the end of B", "R11", D,
     "if ((unsigned int)br.count >= 8U || br.p != br.end ||",
     "if ((br.count >= 8 && br.count < 64) || br.p != br.end ||"),
    ("the padding of B not checked", "R12b", D,
     "(br.end[-1] & ((1U << br.count) - 1U)))", "(br.end[-1] & ((1U << br.count) - 1U) & 0U))"),
    ("the last offset starts at 0", "Offsets", D, "n_lit, last = 1,", "n_lit, last = 0,"),
    ("the last offset starts at 2", "Offsets", D, "n_lit, last = 1,", "n_lit, last = 2,"),
    ("a delta added where p < x", "deltas", D,
     "unsigned int k = at >= dist ? 0 : dist - at;", "unsigned int k = at >= dist ? 0 : dist - at - 1U;"),
    ("dt = 3 with last = 3 as x = 3", "deltas", T,
     "return last < 4U ? (last == 3U ? 6U : 4U) : last;", "return last < 4U ? (last == 3U ? 3U : 4U) : last;"),
    ("a run to 257 accepted", "R8c", L,
     "\t\t\tif (b > 256U)\n\t\t\t\treturn NULL;", "\t\t\tif (b > 257U)\n\t\t\t\treturn NULL;"),
    ("the own table's code space overfull", "R8a", L,
     "if (space != full || b > 256U ||", "if (space < full || b > 256U ||"),
    ("a code for byte value 256", "R8b", L,
     "if (space != full || b > 256U ||", "if (space != full || b > 257U ||"),
    ("the own table one byte past len", "R8e", L, "(used + 7U) / 8U > len ||", "(used + 7U) / 8U > len + 1U ||"),
    ("the own table's padding not checked", "R8d", L,
     "((used & 7U) && bits >> (64U - (8U - (used & 7U)))))", "((used & 7U) && 0))"),
    ("a run one byte value longer", "R8c", L,
     "\t\t\tb += (1U << (nb - 1U)) +\n", "\t\t\tb += (1U << (nb - 1U)) + 1U +\n"),
    ("the header one byte short", "R4, R5", L,
     "if (src_len < SEQLZ_LIT_HEADER(*width) + deltas)", "if (src_len + 1U < SEQLZ_LIT_HEADER(*width) + deltas)"),
    ("w = 4 + ...", "Layout", L, "\t*width = SEQLZ_SIZE_BITS_MIN;", "\t*width = SEQLZ_SIZE_BITS_MIN - 1U;"),
    ("code 9 taken as 8", "R7", L,
     "\tcode = s[2] & SEQLZ_LIT_CODE_MASK;\n",
     "\tcode = s[2] & SEQLZ_LIT_CODE_MASK;\n\tif (code == SEQLZ_LIT_OWN + 1U)\n\t\tcode = SEQLZ_LIT_OWN;\n"),
    ("dt = 4 accepted", "R6", L, "*delta - 1U >= SEQLZ_DELTA_LAST", "*delta - 1U > SEQLZ_DELTA_LAST"),
    ("the streams one byte past len", "R9", L,
     "if ((u64)(q - s) + total > src_len)", "if ((u64)(q - s) + total > (u64)src_len + 1U)"),
    ("literal j read from stream j + 1", "Coded literals", L,
     "\t\tip[i] = start[i];", "\t\tip[i] = start[(i + 1U) & 7U];"),
    ("a stream's codes 8 bits short of its end", "R19, R21", L,
     "return left >= 0 && left <= 7 &&", "return left >= 0 && left <= 8 &&"),
    ("a stream's padding not checked", "R20", L,
     "(left == 0 || !(start[sz - 1U] & ((1U << left) - 1U)));", "(left == 0 || 1);"),
    ("n > BLOCK not checked again in the literals", "R2, equivalent", L,
     "\tif (n_lit > SEQLZ_BLOCK_SIZE)\n\t\treturn NULL;\n\tsizes", "\tsizes"),
]


def run(cmd, cwd, log):
    with open(log, "a") as f:
        return subprocess.run(cmd, cwd=cwd, stdout=f, stderr=subprocess.STDOUT, check=False).returncode


class Copy:
    """HEAD of this checkout in work/src, built in work/build: seqlz_test and seqlz-all-decoders; with
    fuzz, also the fuzz targets diff and gen with libFuzzer, ASan and UBSan in work/fuzz"""

    def __init__(self, work, pages, fuzz):
        self.src, self.build, self.fuzz, self.log = work / "src", work / "build", work / "fuzz", work / "log.txt"
        if self.src.exists():
            sys.exit(f"{self.src} exists")
        self.src.mkdir(parents=True)
        archive = subprocess.run(["git", "-C", str(REPO), "archive", "HEAD"], capture_output=True, check=True).stdout
        subprocess.run(["tar", "-x", "-C", str(self.src)], input=archive, check=True)
        # doctest from an earlier build, DOCTEST_SRC, else CMake fetches it
        doctest = os.environ.get("DOCTEST_SRC")
        extra = [f"-DFETCHCONTENT_SOURCE_DIR_DOCTEST={doctest}"] if doctest else []
        self.checks = [[str(self.build / "seqlz_test"), "-tc=format rules:*,seqlz_ref:*"],
                       [str(self.build / "seqlz-all-decoders"), *pages]]
        self.fuzz_seconds = fuzz
        if self.run(["cmake", "-S", str(self.src), "-B", str(self.build), "-G", "Ninja", "-DSEQLZ_WERROR=ON",
                     *extra]):
            sys.exit(f"the unchanged copy does not configure, see {self.log}")
        if fuzz and self.run(["cmake", "-S", str(self.src), "-B", str(self.fuzz), "-G", "Ninja",
                              "-DCMAKE_C_COMPILER=clang", "-DCMAKE_CXX_COMPILER=clang++",
                              "-DCMAKE_BUILD_TYPE=RelWithDebInfo", "-DBUILD_TESTING=OFF", "-DSEQLZ_FUZZ=ON",
                              "-DSEQLZ_SANITIZE=ON", "-DSEQLZ_WERROR=ON"]):
            sys.exit(f"the unchanged copy's fuzz build does not configure, see {self.log}")
        if not self.built():
            sys.exit(f"the unchanged copy does not build, see {self.log}")
        if self.failed():
            sys.exit(f"the unchanged copy fails, see {self.log}")
        if fuzz:
            self.seeds = self.fuzz / "seeds"
            self.seeds.mkdir()
            self.run([str(self.fuzz / "seqlz-fuzz-seeds"), str(self.seeds), str(self.fuzz / "seeds-roundtrip")])

    def run(self, cmd, cwd=None):
        return run(cmd, cwd or self.src, self.log)

    def built(self):
        ok = self.run(["cmake", "--build", str(self.build), "--target", "seqlz_test", "seqlz-all-decoders"]) == 0
        if ok and self.fuzz_seconds:
            ok = self.run(["cmake", "--build", str(self.fuzz), "--target", "seqlz_diff_fuzz", "seqlz_gen_fuzz",
                           "seqlz-fuzz-seeds"]) == 0
        return ok

    def failed(self):
        """the names of the checks that fail"""
        return [Path(cmd[0]).name for cmd in self.checks if self.run(cmd)]

    def fuzz_failed(self):
        """diff and gen side by side for fuzz_seconds, each from its own empty corpus, diff with the seeds"""
        procs = []
        for target in ("diff", "gen"):
            corpus = self.fuzz / f"corpus-{target}"
            subprocess.run(["rm", "-rf", str(corpus)], check=True)
            corpus.mkdir()
            cmd = [str(self.fuzz / f"seqlz_{target}_fuzz"), f"-max_total_time={self.fuzz_seconds}", "-timeout=5",
                   f"-artifact_prefix={self.fuzz}/{target}-", str(corpus)]
            if target == "diff":
                cmd.append(str(self.seeds))
            with open(self.log, "a") as f:
                procs.append((target, subprocess.Popen(cmd, cwd=self.fuzz, stdout=f, stderr=subprocess.STDOUT)))
        return [f"seqlz_{target}_fuzz" for target, proc in procs if proc.wait()]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("work")
    parser.add_argument("pages", nargs="+")
    parser.add_argument("--only", default="", help="the mutants whose name holds this text")
    parser.add_argument("--fuzz", type=int, default=0, metavar="SECONDS",
                        help="for each mutant the checks do not find, diff and gen with ASan and UBSan this long")
    args = parser.parse_args()
    copy = Copy(Path(args.work).resolve(), [str(Path(p).resolve()) for p in args.pages], args.fuzz)
    mutants = [m for m in MUTANTS if args.only in m[0]]
    survived = 0
    for name, rule, file, old, new in mutants:
        path = copy.src / file
        text = path.read_text()
        if text.count(old) != 1:
            sys.exit(f"{name}: the text to change is in {file} {text.count(old)} times, not once")
        path.write_text(text.replace(old, new))
        try:
            if not copy.built():
                verdict = "did not build"
            else:
                failed = copy.failed()
                if not failed and args.fuzz:
                    failed = copy.fuzz_failed()
                verdict = "found by " + ", ".join(failed) if failed else "SURVIVED"
                survived += not failed
        finally:
            path.write_text(text)
        print(f"{rule:16} {name:44} {verdict}", flush=True)
    copy.built()
    print(f"{len(mutants)} mutants, {survived} survived")
    sys.exit(1 if survived else 0)


if __name__ == "__main__":
    main()
