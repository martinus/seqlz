#!/usr/bin/env python3
# SPDX-License-Identifier: MIT OR GPL-2.0-only
"""make_room.py <role> <room>: fills the directory a clean-room agent of docs/format-check.md works in,
and nothing else. Role D, W or I: format.md; R: the kernel's copy, seqlz.rst, as port.py writes it.
Each gets the tables as tables/<name>.bin, one per row of format.md's table of SHA-256, and BRIEF.md,
the role's brief from docs/format-check.md, "Step 2: the briefs". A role that is a file instead is a
trial: the room gets only that file, as BRIEF.md."""

import hashlib
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(REPO / "tools" / "seqlz-ref"))
sys.path.insert(0, str(REPO / "tools" / "kernel-port"))

import format_rst
import port
import seqlz_ref


def tables():
    """{name: the code lengths as bytes}, from src/seqlz_lengths.h, checked against format.md's SHA-256"""
    src = (REPO / "src" / "seqlz_lengths.h").read_text()

    def field(name):
        return seqlz_ref.numbers(re.search(r"\." + name + r"\s*=\s*\{([^}]*)\}", src).group(1))

    own_at = src.index("seqlz_own_lengths[SEQLZ_OWN_SYMBOLS] =")
    lit_at = src.index("seqlz_lit_sets[SEQLZ_LIT_SETS][256] =")
    t = {
        "TOK": field("token"),
        "LL": field("ll"),
        "ML": field("ml"),
        "LIT": seqlz_ref.numbers(src[lit_at:own_at].split("=", 1)[1]),
        "OWN": seqlz_ref.numbers(src[own_at:].split("=", 1)[1]),
    }
    hashes = seqlz_ref.spec_hashes()
    if sorted(hashes) != sorted(t):
        sys.exit(f"format.md names the tables {sorted(hashes)}, src/seqlz_lengths.h has {sorted(t)}")
    out = {}
    for name, lengths in t.items():
        out[name] = bytes(lengths)
        if hashlib.sha256(out[name]).hexdigest() != hashes[name]:
            sys.exit(f"{name}: src/seqlz_lengths.h is not the table docs/format.md names")
    return out


def brief(role):
    """the text of brief D, W or I of docs/format-check.md; R is D's for seqlz.rst"""
    doc = (REPO / "docs" / "format-check.md").read_text()
    head = r"^### Brief " + ("D" if role == "R" else role) + r":.*?^```text\n(.*?)^```$"
    m = re.search(head, doc, re.MULTILINE | re.DOTALL)
    if not m:
        sys.exit(f"no brief {role} in docs/format-check.md")
    return m.group(1).replace("format.md", "seqlz.rst") if role == "R" else m.group(1)


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    role, room = sys.argv[1], Path(sys.argv[2])
    room.mkdir(parents=True)
    if role not in ("D", "R", "W", "I"):
        (room / "BRIEF.md").write_text(Path(role).read_text())
        return
    md = (REPO / "docs" / "format.md").read_text()
    if role == "R":
        (room / "seqlz.rst").write_text(format_rst.convert(md) + port.SPEC_API)
    else:
        (room / "format.md").write_text(md)
    (room / "tables").mkdir()
    for name, data in tables().items():
        (room / "tables" / f"{name}.bin").write_bytes(data)
    (room / "BRIEF.md").write_text(brief(role))


if __name__ == "__main__":
    main()
