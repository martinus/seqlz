#!/usr/bin/env python3
# SPDX-License-Identifier: MIT OR GPL-2.0-only
"""make_room.py <role> <room>: fills the directory a clean-room agent of docs/format-check.md works in,
and nothing else. Role D, W or I: format.md; R: the kernel's copy, seqlz.rst, as port.py writes it.
Each gets the tables as tables/<name>.bin, one per row of format.md's table of SHA-256, and BRIEF.md,
the role's brief from docs/format-check.md, "Step 2: the briefs". A role that is a file instead is a
trial: the room gets only that file, as BRIEF.md."""

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
    """{name: the code lengths as bytes}, checked against format.md's SHA-256 by seqlz_ref.py"""
    return {name: bytes(lengths) for name, lengths in seqlz_ref.load_lengths().items()}


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
