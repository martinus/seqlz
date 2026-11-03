# format.md, checked by decoders that never saw the code

Two decoders written from [format.md](format.md) alone, in a clean room without `src/`, decode every
block the same as `seqlz_decompress()`, and so do the blocks of a writer from the same clean room, which
makes every choice format.md allows: 25 billion inputs, real pages and damaged ones, on x86-64, 32-bit
x86 and big-endian s390x, without a single difference. Along the way the check changed format.md in 26
places, and none of them is a change of the format. This page is the result of
[format-check.md](format-check.md), the procedure, in one place; the procedure has every command, and its
[Runs](format-check.md#runs) every number with the commit it was measured on.

## Contents

- [The question](#the-question)
- [The result in numbers](#the-result-in-numbers)
- [What the clean rooms found: 23 places of format.md](#what-the-clean-rooms-found-23-places-of-formatmd)
- [No decoder differs from `seqlz_decompress()`](#no-decoder-differs-from-seqlz_decompress)
- [Every rule of `src/` is a rule of format.md, and the other way round](#every-rule-of-src-is-a-rule-of-formatmd-and-the-other-way-round)
- [The check finds a difference when there is one](#the-check-finds-a-difference-when-there-is-one)
- [The numbers in format.md's *Why:* sentences](#the-numbers-in-formatmds-why-sentences)
- [What this does not show](#what-this-does-not-show)

## The question

A specification that is only checked against the code it describes tends to describe that code. The
reference decoders of `tools/seqlz-ref/` were written from format.md, but each change of the format
changed them in the same commit as `src/`, by someone who had `src/` open. So they show that format.md and
`src/` agree, not that format.md alone is enough. The question here is the second one: can someone who
only has format.md and the tables write a decoder that accepts exactly the blocks `seqlz_decompress()`
accepts, and decodes each to the same page?

Four agents got a room each, a podman container with format.md, the tables and a brief, and nothing
else: no `src/`, no git history, no web search or fetch
([`tools/format-check/cleanroom.sh`](../tools/format-check/cleanroom.sh)). Every tool call they made is in
an audit, and none reached outside its room.

| agent | wrote | from |
| --- | --- | --- |
| D | a decoder | format.md |
| R | a decoder | the kernel's copy, `Documentation/staging/seqlz.rst` |
| W | a writer: any bytes become a valid block, by every choice format.md leaves open | format.md |
| I | every rule that makes a block invalid, and every number format.md claims | format.md |

Their code is in [`tools/format-check/cleanroom/`](../tools/format-check/cleanroom), as they wrote it.

## The result in numbers

| what | how much | difference |
| --- | --- | --- |
| real pages, 10 samples of phones and desktops, at levels 1 to 3, then cut, bit flipped and damaged | 600 000 blocks, 102.8 million decodes | 0 |
| AFL++, 24 instances for 9 h 4 min, 4 targets | 24.5 billion inputs | 0 |
| libFuzzer with MSan, 1 hour | 453 million inputs | 0 |
| W's blocks from random choices, each decoded to W's page | 2 million blocks | 0 |
| the blocks of D's, R's and the repository's tests | 295 000 blocks | 0 |
| `seqlz_ref.py`, the slow decoder in Python, on distinct blocks of all the above | 98 333 blocks, 68 830 valid | 0 |
| a valid and an invalid block for each rule of I's list | 712 assertions | 0 |
| 32-bit x86 and s390x, the pages and the rule pairs | 2 million decodes each | 0 |

All on a Ryzen 9 7950X, s390x under qemu, on 2nd November 2026. "Difference" means one decoder calls a
block valid and another does not, or two decode it to different pages. Every input goes through all
four C decoders: `seqlz_decompress()`, `seqlz_ref.c`, D's and R's.

## What the clean rooms found: 23 places of format.md

Each agent wrote down every question it had to answer itself, and every question got a kind: A, format.md
answers it; B, format.md is silent or can be read two ways; C, format.md and `src/` differ; D, the agent
misread. Three rounds, each in new rooms on the format.md the round before had fixed:

| round | questions | kind B | kind C or D |
| --- | --- | --- | --- |
| 1 | 57 | 10 | 0 |
| 2 | 64 | 7 | 0 |
| 3 | 53 | 6 | 0 |

The rounds stopped by the rule of step 3: a round in which no question of kind B has two readings that
give a block different verdicts, no agent took a reading other than `src/`'s, and no question is of kind
C or D. All 23 were text: a wrong example of `read()`, the longest code of `ML` given as 8 bits while
the table's is 7, the end of a block's own table after a run past 255, a
rule that was only implied, that a block needing a bit past its end is invalid, names with two
meanings, ... Not one was a change of the format, and not one made a decoder of the repository wrong.
The questions of each round are in [`tools/format-check/runs/`](../tools/format-check/runs).

## No decoder differs from `seqlz_decompress()`

Damage is how most of the blocks a decoder must reject get made, and fuzzing finds the rest. Real
pages, compressed at all three levels, then every cut of one block in 10, every flipped bit of one in 100
and a random damage of every block, give 102.8 million decodes. AFL++ ran four targets: the decoder
alone, the roundtrip, all four decoders on any input, and W's blocks, damaged after W wrote them. It
stopped 162 minutes after the last new edge:

| target | inputs | edges | queue |
| --- | --- | --- | --- |
| `decode` | 12.64 billion | 645 of 753 | 846 |
| `diff` | 9.72 billion | 1700 of 2121 | 1244 |
| `roundtrip` | 1.64 billion | 1535 of 1977 | 1947 |
| `gen` | 0.49 billion | 2097 of 2609 | 1636 |

W matters because `seqlz_compress()` makes the same choices every time: it never escapes a token that
has a code, never sends offset 2 in class 3, never makes `w` larger than it needs to be. A decoder that
rejected such a block would pass every test that starts from the compressor. W makes all of them: of the
157 items of step 5's list, 154 are in at least 100 of 2 million blocks, every one of the 3072 tokens is
there with its code and escaped, and the three corners random choices hardly reach have a block made
by hand in the tests, e.g. the largest offset a block can have, 4092, after 4092 literals.

## Every rule of `src/` is a rule of format.md, and the other way round

[`tools/format-check/reject-sites.md`](../tools/format-check/reject-sites.md) maps each of the 20 places
where `seqlz_decompress()` rejects a block to the rules of I's list, and each rule to its place. One
place has no rule, the call without scratch memory, which is about the call, not the block. One rule has
no place, R10, which I's list has as no condition of its own. So `src/` checks nothing that format.md
leaves out, and format.md asks for nothing that `src/` does not check.

Then line and branch coverage over all of the blocks above:

| file | lines | branches |
| --- | --- | --- |
| `src/seqlz_decompress.c` | 301 of 301 | 130 of 130 |
| `src/seqlz_decompress_literals.c` | 206 of 207 | 94 of 96 |
| `tools/seqlz-ref/seqlz_ref.c` | 220 of 223 | 149 of 156 |
| D | 231 of 231 | 138 of 138 |
| R | 378 of 393 | 199 of 218 |

The one line of `src/` missed checks `n > BLOCK` a second time, after the caller did. Every other line
that rejects a block ran, so every rule has met a block that breaks it.

## The check finds a difference when there is one

A check that finds nothing is only worth something if it would have found something.
[`tools/format-check/mutants.py`](../tools/format-check/mutants.py) planted 31 differences into
`seqlz_decompress()`, one at a time, each a rule checked wrong or not at all. 20 were found by the rule
pairs or the real pages, 7 more by 5 minutes of fuzzing, and the 4 left are equivalent: each breaks a
check that a later one catches anyway, e.g. the second `n > BLOCK`. One of the 7 showed a gap: an escaped
token of exactly 3072, which a block of 104 bytes made valid for `seqlz_decompress()` alone, was found by
the fuzzer but not by the rule pairs, whose pair of R13 has that token now.

## The numbers in format.md's *Why:* sentences

Agent I listed 80 claims of format.md, every number and every statement that should follow from the
rules. [`tools/format-check/claims.py`](../tools/format-check/claims.py) checks them in CI: 44 hold by a
program, 25 by an argument or another step, 11 by a measurement, the corpus ones by
[`measure_claims.py`](../tools/format-check/measure_claims.py) on 20 000 pages. None is wrong now, but
three were, all in *Why:* sentences, not in rules:

- "2 bytes each made blocks about 5 bytes larger": that was zsmalloc's cost a month earlier; now it is 9
  bytes per block with coded literals.
- "shorter matches cost more bits than the literals they replace": a match of 4 bytes takes 11.6 to 11.9
  bits, 3 literals 19 to 24. The reason for a minimum of 4 is time, 8% more sequences for matches of 3.
- offset 2 "the most frequent of all": it is the most frequent other than the last offset, and at level 3
  only second to 8.

These three changed after the clean rooms had read them. Each gives a reason, not a rule, so no block's
verdict depends on it, and the plan fixes them without a new round.

## What this does not show

The decoders agree, which shows that D and R read format.md the way `src/` does it. It does not show
more than that:

- The same family of models wrote format.md, `src/`, `seqlz_ref.c` and the clean-room code. They may all
  read a sentence wrong the same way. A clean room on another vendor's model, or a person who writes a
  decoder, would show more.
- A guess that came out right shows only if the agent noticed it guessed.
- 25 billion inputs without a difference is that, not a proof that there is none.
- What the compressor writes is not part of the format and not checked, only that its blocks decode.

The procedure is meant to be run again after each change of format.md;
[After a change of format.md](format-check.md#after-a-change-of-formatmd) says how much of it.
