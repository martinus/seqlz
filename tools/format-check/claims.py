#!/usr/bin/env python3
# SPDX-License-Identifier: MIT OR GPL-2.0-only
"""claims.py: step 10 of docs/format-check.md, the claims of the clean-room agent I's list,
tools/format-check/cleanroom/I/CLAIMS.md, checked against the tables, docs/format.md and
bench/seqlz_counts.txt. Prints one line per claim: "holds" where this program checks it, "argued" where
a sentence of reasoning or another step settles it, "measured" with the number and where it comes from
(tools/format-check/measure_claims.py on pages, which CI does not have, or explored-designs.md), "open"
with what it would need. Exits 1 if a claim it checks does not hold."""

import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(REPO / "tools" / "seqlz-ref"))

import seqlz_ref

P = 12
BLOCK = 1 << P
T = seqlz_ref.load_lengths()  # also checks their SHA-256 against format.md, C26
SPEC = (REPO / "docs" / "format.md").read_text()
LIT = [T["LIT"][256 * k : 256 * (k + 1)] for k in range(8)]
results = []


def claim(cid, ok, detail):
    """ok: True, False, or "argued" / "measured" / "open" """
    results.append((cid, ok, detail))


def complete(lengths):
    """the Kraft sum of a table's codes is exactly 1"""
    m = max(lengths)
    return sum(1 << (m - n) for n in lengths if n) == 1 << m


def code_of(lengths, sym):
    """a symbol's canonical code as a bit string, format.md's "Prefix codes" """
    codes = seqlz_ref.canonical(lengths, max(lengths))
    for (n, c), s in codes.items():
        if s == sym:
            return format(c, f"0{n}b")
    return None


def first_symbol_of_zeros(lengths):
    """the symbol whose code is all zeros: the first one of the shortest length"""
    shortest = min(n for n in lengths if n)
    return lengths.index(shortest), shortest


claim("C1", len(T["LIT"]) == 8 * 256, "LIT holds 8 tables of 256 lengths")
claim("C2", "argued", "one layout for all levels; step 4 decodes every level's blocks with every decoder")
claim("C3", BLOCK == 4096, "2^12 = 4096")
claim("C4", len(T["LL"]) == len(T["ML"]) == 12 + P == 24, "LL and ML have 12 + P = 24 symbols")
claim("C5", (0x8000 >> 8) == 1 << 7, "bit 15 of a little endian u16 is bit 7 of byte 1")
claim("C6", (1 << 15) - 1 == 32767, "2^15 - 1")
claim("C7", "argued", "the offsets add up the sizes of the rows before them")
claim("C8", all(8 * w // 8 == w for w in range(5, 13)), "8 sizes of w bits are w bytes")
claim("C9", (5 + 0, 5 + 7, 8 * 5, 8 * 12) == (5, 12, 40, 96), "w = 5 + 0..7, S 40 to 96 bits")
claim("C10", max(max(t) for t in LIT) <= 10 and BLOCK // 8 * 10 // 8 == BLOCK * 10 // 64 == 640,
      "every literal table's codes at most 10 bits; 512 literals of 10 bits are 640 bytes")
claim("C11", "measured", "the largest stream below 128 bytes in 68.8% of the coded blocks of level 2, 79.8% of "
      "level 3; measure_claims.py, 20 000 pages of the 10 samples")
claim("C12", (1 << 7) - 1 == 127, "7 bits hold sizes below 128")
claim("C13", "measured", "w is 7.0 on average at level 2, 6.65 at level 3, so 2 bytes per size are 9.0 and 9.3 "
      "bytes more per coded block; measure_claims.py, 20 000 pages of the 10 samples")
claim("C14", "argued", "start[0] is the formula with an empty sum")
claim("C15", "measured", "one stream 11 901 decode cycles per page, four 8146 (explored-designs.md, seqlz-hc-lit); four "
      "7627 against eight's 7418 on the PC (explored-designs.md, 4 literal streams instead of 8)")
claim("C16", "argued", "byte 2 exists only for len >= 3")
claim("C17", all(complete(t) for t in LIT), "each 256 lengths of LIT are a complete code")
claim("C18", "argued", "0 | 11 | 10 is A C B with the codes 0, 10, 11")
claim("C19", code_of([1, 2, 2], 0) == "0" and code_of([1, 2, 2], 1) == "10" and code_of([1, 2, 2], 2) == "11",
      "the lengths 1, 2, 2 give the codes 0, 10, 11")
claim("C20", all(complete(T[n]) for n in ("TOK", "LL", "ML", "OWN")) and all(complete(t) for t in LIT),
      "TOK, LL, ML, OWN and the 8 literal tables are complete")
claim("C21", "argued", "a complete prefix code of 2 or more codes: every long enough bit string starts with one")
claim("C22", "argued", "no field of the layout names a set of tables")
claim("C23", len(T["TOK"]) == 3073 and max(T["TOK"]) == 11, "TOK: 3073 symbols, longest code 11 bits")
claim("C24", (len(T["LL"]), max(T["LL"]), len(T["ML"]), max(T["ML"]), max(max(t) for t in LIT), len(T["OWN"]),
              max(T["OWN"])) == (24, 8, 24, 7, 10, 19, 8),
      "LL 24/8, ML 24/7, literal tables 256/10, OWN 19/8; tools/seqlz-ref/check.sh holds the column too")
claim("C25", "argued", "tools/seqlz-ref/check.sh holds both copies of the table against the tables")
claim("C26", True, "load_lengths() checked every table's SHA-256 against format.md")

counts = (REPO / "bench" / "seqlz_counts.txt").read_text()
pages = int(re.search(r"(\d+) pages, \d+ of them with more than", counts).group(1))
lit_pages = [int(line.split()[2]) for line in counts.splitlines() if line.startswith("lit ")]


def spaced(v):
    """a number as format.md writes it, 1 091 983"""
    return f"{v:,}".replace(",", " ")


# every mention in format.md, the kernel's copy and this file's, of the pages and of the fewest per table
said_pages = re.findall(r"(\d{1,3}(?: \d{3})+) (?:memory )?pages(?: of a desktop| under memory|: three)", SPEC)
said_fewest = re.findall(r"over (\d{1,3}(?: \d{3})*) pages or more", SPEC)
claim("C27", len(said_pages) == 2 and set(said_pages) == {spaced(pages)}
      and set(said_fewest) == {spaced(min(lit_pages))},
      f"bench/seqlz_counts.txt: {pages} pages, the literal tables' fewest {min(lit_pages)}, as format.md says "
      f"{len(said_pages)} and {len(said_fewest)} times")
claim("C28", "argued", "tools/seqlz-ref/check.sh and the test seqlz_train hold the tables against the counts")
claim("C29", "argued", "B, the literal streams and the own table are all read with bit 7 - (i mod 8) first")
claim("C30", "measured", "the ranges follow from MSB first; 42.5 instead of 55.3 lines of the token table per "
      "page (explored-designs.md, Prefetching the tables)")
claim("C31", "argued", "the sentence says what the end check is for, not that it finds all damage: steps 4 and 7"
      " decode damaged blocks, some of which are valid")
claim("C32", "argued", "seqlz_decompress() reads ahead, seqlz_ref.c bit by bit; steps 4 and 7 find no difference")
claim("C33", (1 << 11) + (1 << 11) - 1 == BLOCK - 1, "symbol 23: 2^11 + 2^11 - 1 = 4095 = 2^P - 1")
claim("C34", [(1 << (s - 12), (1 << (s - 11)) - 1) for s in (16, 17, 23)] == [(16, 31), (32, 63), (2048, 4095)],
      "symbols 16, 17, 23 are [16, 32), [32, 64), [2048, 4096)")
claim("C35", (100).bit_length() - 1 == 6 and 6 + 12 == 18 and 100 - 64 == 36, "100: b = 6, symbol 18, read(6) = 36")
claim("C36", BLOCK - 4 - 15 == BLOCK - 19 < BLOCK and BLOCK - 1 - 35 < BLOCK,
      "the largest ll value, BLOCK - 19 = 4077, and ml value, 4060, are below 2^P")
claim("C37", all(t & 15 <= 15 and (t >> 4) & 31 <= 31 and t >> 9 <= 5 for t in range(3072)),
      "every token below 3072 has a class 0 to 5")
claim("C38", 16 * 32 * 6 == 3072, "16 * 32 * 6")
claim("C39", (1 << 12) >= 3072, "12 bits hold 3072 tokens, 3072 to 4095 are invalid")
with_code = sum(1 for n in T["TOK"][:3072] if n)
claim("C40", with_code <= 1 << 11 and with_code < 3072,
      f"{with_code} tokens have a code, at most 2048 can; that most are rare is the counts'")
claim("C41", "measured", "matches of 3 at the last offset: about 10 bits each, 1.5% of the page, 8% more "
      "sequences (explored-designs.md, Ratio from the parse and the tables on phone pages); a 4-byte match takes 11.6 to "
      "11.9 bits, 3 literals 19 to 24, so it is not the bits; measure_claims.py, 20 000 pages of the 10 samples")
claim("C42", (2 * 1, 255, BLOCK - 1, 31 * 8, 255 * 8) == (2, 255, 4095, 248, 2040), "the largest offset of each class")
claim("C43", "argued", "the table's formulas")
claim("C44", all(x * f == 0 for x, f in ((0, 2), (0, 1), (0, 1), (0, 8), (0, 8))), "offset bits of 0 give 0")
claim("C45", "measured", "the bits per class agree; of the offsets other than the last one, 2 is 7.2% and 8 "
      "6.2% at levels 1 and 2, 8 5.9% and 2 5.0% at level 3; measure_claims.py, 20 000 pages of the 10 samples")
claim("C46", "argued", "a copy byte by byte repeats what it writes")
claim("C47", BLOCK // 4 + 1 == 1025, "1024 sequences of 4 bytes or more, then one more")
claim("C48", "argued", "RULES.md I1 and I2")
claim("C49", "argued", "the stop at once with n = BLOCK, and no byte left after it")
claim("C50", "measured", "1.22 to 1.28 bytes per page on three dumps (explored-designs.md, The last sequence without a "
      "token)")
claim("C51", "argued", "|out| = ll before the first match, off >= 1; test/seqlz_rules_test.cpp, I1")
claim("C52", "argued", "stream j holds literal j first, so it is empty for j >= n")
claim("C53", "argued", "D and R read streams differently from seqlz_decompress(); steps 4 and 7 find no difference")
x_of_last = [next(m for m in range(last, 9 * last, last) if m >= 4) for last in (1, 2, 3, 4, 5, 8)]
claim("C54", x_of_last == [4, 4, 6, 4, 5, 8],
      "the smallest multiple of last that is 4 or more: 4, 4, 6, then last")
claim("C55", "argued", "x >= 4: the 4 bytes x back are written before a word of 4 literals")
claim("C56", "measured", "31.5% of the literals as deltas are 0, 13.5% would be without; measure_claims.py, 20 000 pages of the 10 samples")
claim("C57", len(T["OWN"]) == 10 + 9, "OWN: 10 lengths and 9 runs")
claim("C58", (1 << 0) + 0 == 1, "k = 1: 2^0 + read(0) = 1")
claim("C59", "argued", "each symbol is for one byte value or more, 256 of them")
zero_sym, zero_len = first_symbol_of_zeros(T["OWN"])
zeros_end = zero_sym < 10 and 256 * 2 ** -(zero_sym + 1) < 1 or zero_sym >= 10
claim("C60", zeros_end, f"zeros read as OWN are symbol {zero_sym}, code of {zero_len} bits: the list ends invalid")
claim("C61", 2 ** -1 < 1, "one code fills half the code space at most, a run of 256 leaves no byte value")
claim("C62", "measured", "the lengths of OWN agree; 85.6% of the byte values of own tables have a code of 7 to "
      "10 bits; measure_claims.py, 20 000 pages of the 10 samples")
claim("C63", "measured", "0.50 bytes per byte value with a code, 56.1 per table, 74.4% of the tables 40 to 100 "
      "bytes; measure_claims.py, 20 000 pages of the 10 samples")

example = bytes.fromhex("0200616223f2fffb60")
page = seqlz_ref.decode(example, seqlz_ref.load_tables())
claim("C64", page == b"ab" * 2048, "seqlz_ref.py decodes the example to ab 2048 times")
claim("C65", example[:4] == b"\x02\x00ab", "h = 2, raw, the literals a and b")
claim("C66", len(example) - 4 == 5, "B is 5 bytes")
claim("C67", T["TOK"][3072] == 4 and code_of(T["TOK"], 3072) == "0010", "the escape's code is 0010")
bits = "".join(format(b, "08b") for b in example[4:])
fields = (1010 & 15, (1010 >> 4) & 31, 1010 >> 9)
claim("C68", int(bits[4:16], 2) == 1010 and T["TOK"][1010] == 0 and fields == (2, 31, 1),
      "read(12) = 1010: ll 2, mlf 31, class 1, no code of its own")
claim("C69", bits[16] == "1", "the offset bit of class 1 is 1, offset 2")
claim("C70", code_of(T["ML"], 23) == "1111111" and bits[17:24] == "1111111", "ML symbol 23's code is 1111111")
claim("C71", int(bits[24:35], 2) == 2011 and 35 + 2048 + 2011 == 4094, "read(11) = 2011, ml = 4094")
claim("C72", bits[35:] == "00000", "the 5 bits after bit 35 are 0")
claim("C73", "argued", "the header is 3 + w + e bytes, m is 35 or 19")
claim("C74", "argued", "s[j] <= 640 takes 10 bits at most")
claim("C75", "argued", "the own table's lengths are 1 to 10")
claim("C76", max(8 // 11, 1) == 1, "8 / 11 rounded down, at least 1")
cr_h = (REPO / "tools" / "format-check" / "cleanroom" / "W" / "cr.h").read_text()
w_max = int(re.search(r"#define CR_MAX_BLOCK (\d+)", cr_h).group(1))
claim("C77", w_max <= 2 * BLOCK, f"the clean-room writer W bounds every block it can write at {w_max} bytes")
claim("C78", sum(1 for t in range(256) if t & 15 > 8) == 112 and 256 - 3 == 253, "112 values of byte 2, 253 of dt")
claim("C79", "open", "the logs of those runs")
claim("C80", "argued", "the decoder's rules have not changed since 689dad5 of 22nd October; 1st November only took "
      "the 16 KiB blocks out")

wrong = 0
for cid, ok, detail in results:
    status = "holds" if ok is True else ok if isinstance(ok, str) else "WRONG"
    wrong += ok is False
    print(f"{cid:4} {status:6} {detail}")
held = sum(1 for _, ok, _ in results if ok is True)
argued = sum(1 for _, ok, _ in results if ok == "argued")
measured = sum(1 for _, ok, _ in results if ok == "measured")
still_open = sum(1 for _, ok, _ in results if ok == "open")
print(f"{len(results)} claims: {held} hold, {argued} argued, {measured} measured, {still_open} open, {wrong} wrong")
sys.exit(1 if wrong else 0)
