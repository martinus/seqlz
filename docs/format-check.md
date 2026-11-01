# Checking format.md

[format.md](format.md) says that a decoder can be written from it alone, and that it says which
compressed blocks are valid. This file is the procedure that checks both claims, written so that
anyone can run it again: who writes what, from which files, what runs against what, and when the check
has passed. It is a plan; nothing of it has run yet, and the tools it names under
`tools/format-check/` don't exist yet. Results go to [Runs](#runs) at the end.

## Contents

- [Why this is useful](#why-this-is-useful)
- [What has to hold](#what-has-to-hold)
- [When the check has passed](#when-the-check-has-passed)
- [Who does what](#who-does-what)
- [How to run it](#how-to-run-it)
- [Step 1: the clean room](#step-1-the-clean-room)
- [Step 2: the briefs](#step-2-the-briefs)
- [Step 3: the questions, sorted](#step-3-the-questions-sorted)
- [Step 4: every decoder on the same blocks](#step-4-every-decoder-on-the-same-blocks)
- [Step 5: valid blocks by construction](#step-5-valid-blocks-by-construction)
- [Step 6: one valid and one invalid block per rule](#step-6-one-valid-and-one-invalid-block-per-rule)
- [Step 7: coverage-guided fuzzing](#step-7-coverage-guided-fuzzing)
- [Step 8: every rule of src/ is a rule of format.md](#step-8-every-rule-of-src-is-a-rule-of-formatmd)
- [Step 9: planted differences, to see that the check finds them](#step-9-planted-differences-to-see-that-the-check-finds-them)
- [Step 10: the numbers of format.md](#step-10-the-numbers-of-formatmd)
- [Step 11: big-endian and 32-bit](#step-11-big-endian-and-32-bit)
- [What a difference means](#what-a-difference-means)
- [After a change of format.md](#after-a-change-of-formatmd)
- [What this does not show](#what-this-does-not-show)
- [Runs](#runs)

## Why this is useful

A specification that is only checked against the code it describes tends to describe that code. Its
author knows what the code does, and so does every reviewer who read the code, and they all fill a gap in
the text without noticing it. Only a reader who never saw the code meets the gap: a missing rule, a
sentence that can be read two ways, a bound that is off by one. Most of these don't show in tests,
because the code and the text agree on every block the compressor writes; they show in the blocks
nobody writes, which are the ones a decoder has to reject.

The ones who need format.md to be right don't read `src/`: someone who writes another decoder, for
another kernel, a userspace tool or a reader of zram dumps; a kernel reviewer who checks the decoder
against its description; and whoever makes `seqlz_decompress()` faster later, for whom format.md is
what says whether the faster decoder is still correct. For all of them a rule that is only in the code
is a rule that does not exist.

The reference decoders in `tools/seqlz-ref/` were written from format.md alone, and
`fuzz/seqlz_diff_fuzz.c` fuzzes `seqlz_decompress()` against `seqlz_ref.c`, see
[How this file was checked](format.md#how-this-file-was-checked). But each change of the format
changed them in the same commit as `src/`, by someone who had `src/` in front of them. So they show
that format.md and `src/` agree, and less and less that format.md alone is enough. A decoder written
from scratch, by someone who has never seen `src/`, shows that again, and this procedure is built
around one.

## What has to hold

| | property | checked in |
| --- | --- | --- |
| P1 | **Complete.** A decoder written from format.md and the tables alone, by someone who has not seen `src/`, its history or the book, decodes every block as `seqlz_decompress()` does. | steps 1 to 4, 7 |
| P2 | **Exact.** The blocks format.md calls valid are exactly the ones `seqlz_decompress()` accepts, both ways, and each decodes to the same block. Every rule of `src/` is written in format.md. | steps 4, 6, 7, 8 |
| P3 | **Writable.** Every choice format.md allows decodes, also the ones `seqlz_compress()` never makes: an escaped token that has a code, class 3 for offset 2, a `w` larger than needed, ... | step 5 |
| P4 | **The kernel's copy too.** `Documentation/staging/seqlz.rst`, which `tools/kernel-port/port.py` writes from format.md, is as complete as format.md. | steps 1 to 4, 7 |
| P5 | **Its numbers are right.** Every number and every claim that follows from the rules (e.g. "a decoder stops after at most `BLOCK / 4 + 1` sequences") holds. | step 10 |

P1 and P2 are what a decoder writer needs. P3 matters because the fuzzers and tests so far start from
what `seqlz_compress()` writes, and a rule that only blocks of another compressor reach is not tested by
them. P4 matters because the kernel's copy is what upstream reads.

## When the check has passed

All of these, on the same commit:

1. No open question of kind B (format.md is silent or can be read two ways), see step 3.
2. No difference between any two decoders in steps 4 to 7.
3. Every rule of the inventory (step 2, brief I) has a valid and an invalid block, and every decoder
   gives each the verdict the inventory says.
4. Every item of [the list in step 5](#step-5-valid-blocks-by-construction) was written at least 100
   times by the writer, and decoded by every decoder.
5. Every reject site in `src/seqlz_decompress.c` and `src/seqlz_decompress_literals.c` maps to a rule
   of the inventory, and every rule to a reject site; every line and branch of the decoders is reached
   by the blocks of steps 4 to 7, or the run says why not.
6. Every planted difference of step 9 is found.
7. The fuzzing of step 7 ran its full time and found nothing.
8. Every claim of step 10 is checked.

A fix of format.md during the run means a new clean room, see
[After a change of format.md](#after-a-change-of-formatmd): the check passes on the format.md the
clean-room agents read, not on an older one.

## Who does what

- **The runner**, a person or an agent with the whole repository, runs steps 1 and 3 to 11. The runner
  never gives a clean-room agent anything but what step 1 puts in its directory and the brief.
- **Four clean-room agents**, each a new session that has never seen this repository:
  - **D** writes a decoder from format.md.
  - **R** writes a decoder from the kernel's copy, `seqlz.rst`.
  - **W** writes a writer from format.md: from bytes that pick each choice, a valid block and the
    block it decodes to.
  - **I** writes the inventory of rules and the list of claims from format.md, and no code.

D, R, W and I don't see each other's work. A writer and a decoder of the same agent would share
its misreadings, and a block that both get wrong in the same way looks right. Where there is a choice of
models, D and R run on different ones. Each run records the model, the date and the commit of
format.md.

## How to run it

The tools come first. `tools/format-check/cleanroom.sh`, `all-decoders`, `claims.py`,
`fuzz/seqlz_gen_fuzz.c`, `test/seqlz_rules_test.cpp` and the decoders added to the `diff` target are
ordinary changes of the repository, made and reviewed as such, before any run. That change includes
one trial of `cleanroom.sh` with a brief that tells the agent to fetch a web page and to read a file
outside its directory: both have to fail, and the audit of step 1 has to name both.

Then the runner is an agent session, e.g. Claude Code, started in a fresh checkout of the commit under
test and told to follow this file, one phase at a time:

| phase | steps | what to tell the runner | it stops with |
| --- | --- | --- | --- |
| 1 | 1 to 3 | "Run steps 1 to 3 of docs/format-check.md, then stop and show me the questions, sorted." | the questions of kind B and C, for the maintainer |
| 2 | 4 to 6, 10 | "Run steps 4, 5, 6 and 10 of docs/format-check.md on the clean-room results in tools/format-check/runs/, then stop and show me every difference and every claim that does not hold." | the differences and the claims, for the maintainer |
| 3 | 7 | "Start step 7 of docs/format-check.md in the background and tell me how to see its state." | the fuzzers running |
| 4 | 7, 8, 9, 11 | "Step 7 has run its time. Collect its results, run steps 8, 9 and 11 of docs/format-check.md, and write the entry in Runs." | the entry in [Runs](#runs) |

- The runner never starts D, R, W or I as its own subagents. They would share its files, `src/`
  included, and their prompt would come from a session that knows `src/`. It only calls
  `cleanroom.sh`, which gives the agent the brief and nothing else, and it does not talk to an agent
  while it runs.
- The clean-room agent gets its credentials as an API key passed into the container, or as a login
  copied into the container's empty home directory; nothing else of the host's home.
- The maintainer decides each question of kind B and each difference where format.md is clear and
  `src/` does something else, see step 3 and [What a difference means](#what-a-difference-means). A
  change of format.md ends the phase: back to phase 1, with new clean rooms.
- A session can end between phases. What the next one needs is in `tools/format-check/runs/` and the
  run's notes, so it starts from there and not from the old session's context. Phase 3 takes hours
  and all the CPUs it is given; no benchmark runs on the machine meanwhile.

## Step 1: the clean room

`tools/format-check/cleanroom.sh <role> <dir>` (to be written) makes the directory an agent works in
and starts the agent there. The directory is outside any checkout of the repository, so that the
agent finds no `.git`, no instruction file of the project (`CLAUDE.md`, `.claude/`) and no memory of
earlier sessions from it.

What is in it, and nothing else:

| file | D, W, I | R |
| --- | --- | --- |
| the specification | `format.md`, as it is in the commit under test | `seqlz.rst`, from `tools/kernel-port/port.py` |
| the tables | `tables/TOK.bin`, `LL.bin`, `ML.bin`, `LIT.bin`, `OWN.bin` | the same |
| the brief | `BRIEF.md`, its role's text from [step 2](#step-2-the-briefs) | the same |

Each `.bin` holds one row of format.md's table of SHA-256: the code lengths as bytes in symbol order,
the 8 literal tables one after the other for `LIT`, so its SHA-256 is the one format.md gives.
*Why not `src/seqlz_lengths.h`*, which format.md names: it includes `seqlz_format.h` and uses the
names of `src/`, so it would bring code of `src/` into the room. The brief says where the tables are
instead.

How the agent runs:

- In a container (e.g. podman or docker) that mounts only this directory, with a compiler (gcc and
  clang with ASan, UBSan, MSan), make and python3, and no home directory of the host.
- The agent reads no instruction files, memory, plugins or hooks of the user or the project, and has
  no tools to search or fetch from the web, and no MCP servers. With Claude Code e.g. `claude --bare -p`
  with an API key, and `--disallowedTools WebFetch WebSearch`. The repository is public on GitHub and
  the book at <https://martin.ankerl.com/seqlz/> shows every line of `src/`, so any look outside the
  directory spoils the run.
- The whole transcript is kept next to the result (Claude Code: `--output-format stream-json`).
- The model is named explicitly, and recorded.

The container still has network access, the agent needs it for the API. So after the run the runner
reads the transcript for every command that reaches outside the directory: `curl`, `wget`, `git`,
`pip`, `npm`, `gh`, a path outside the directory, a package download. One of them, and the run does not
count; it runs again in a new directory. The audit, with the list of commands, goes to the run's notes.

What comes back is kept as written, in `tools/format-check/runs/<date>-<role>/`: the code, the agent's
`QUESTIONS.md`, `FEATURES.md` or `RULES.md` and `CLAIMS.md`, the audit, and the model's name. A bug fix
in a clean-room decoder later is its own commit, so that what the agent wrote stays visible in the
history.

## Step 2: the briefs

The runner gives each agent its brief as it is here, and answers no question during the run: an
answer from someone who knows `src/` would bring `src/` into the room. An agent that is stuck writes
its question down and goes on with one reading. The briefs don't name any place of format.md that is
known to be hard, so that they don't steer the reading.

### Brief D: the decoder

```text
This directory holds the specification of a compression format, format.md, and the code tables it
refers to, in tables/. Write a decoder for it, from these files only.

The tables: format.md says where they are in its own repository. Here, each row of its table of SHA-256
is a file in tables/, e.g. tables/TOK.bin: the code lengths as bytes in symbol order, the 8 literal
tables one after the other for LIT. Check each against the SHA-256 that format.md gives.

Do not look for any other description or implementation of this format, on the internet or anywhere
else, and do not use one you might remember. format.md links to files that are not here; they are not
needed. Sentences marked "Why:" explain a choice and are not part of the format.

What to write, in C11 with only the C standard library:

    int cr_decode(const unsigned char *src, size_t len, unsigned char *out);

It returns 0 if src[0..len) is a valid compressed block, out then holds the 4096 bytes of the block;
it returns -1 if the block is not valid, out is undefined then. It must return for every input, of any
length, and must not read outside src[0..len) or write outside out, also for invalid input. It will
run under AddressSanitizer, UndefinedBehaviorSanitizer, MemorySanitizer and a fuzzer, at least ten
thousand blocks per second. It may build its codes on the first call. The tables go into the sources,
made from tables/ by a script you write.

Return 0 for exactly the blocks format.md calls valid: no rule of your own to be safe, no rule left out
because it seems unimportant.

Write QUESTIONS.md while you work, not at the end: every place where you had to choose between two
readings, or where format.md does not say what to do. For each: the heading of the section, the
sentence quoted, the readings, the one you took and why, and whether other text in format.md, e.g.
the example, decided it. Also every place that is right but took you long to understand, marked as
such.

Write tests from format.md: its example, and blocks you make by hand for its rules, valid and
invalid. One command builds and runs them.

At the end, report: the files, how to build and test, the test result, the number of questions.
```

### Brief R: the decoder from the kernel's copy

The same as brief D, with `seqlz.rst` for `format.md`.

### Brief W: the writer

```text
This directory holds the specification of a compression format, format.md, and the code tables it
refers to, in tables/. Write a writer of compressed blocks for it, from these files only.

The tables: format.md says where they are in its own repository. Here, each row of its table of SHA-256
is a file in tables/, e.g. tables/TOK.bin: the code lengths as bytes in symbol order, the 8 literal
tables one after the other for LIT. Check each against the SHA-256 that format.md gives.

Do not look for any other description or implementation of this format, on the internet or anywhere
else, and do not use one you might remember. format.md links to files that are not here; they are not
needed. Sentences marked "Why:" explain a choice and are not part of the format, and so is the section
"What the compressor writes": the writer is not a compressor, and should make every block the format
allows, not the small ones.

What to write, in C11 with only the C standard library:

    size_t cr_write(const unsigned char *choices, size_t n, unsigned char *dst, size_t cap,
                    unsigned char *block);

It reads choices[0..n) in order, 0 after the end, and each byte it reads decides one choice the
format leaves open. It writes a valid compressed block to dst and the 4096 bytes it decodes to into
block, and returns the compressed length, or 0 if cap is too small; say in a comment how large cap has
to be. Every choice the format allows should be reachable
from some choice bytes, rare ones as easily as frequent ones: which literals, where matches go and how
long they are, how each length and offset is coded, which token codes, the layout and every value of
its fields, and so on. Any choice bytes must give a valid block.

Also write cr-gen <seed> <count> <dir>: writes count blocks from random choice bytes, each as
<k>.z and <k>.block, and prints how often each item of FEATURES.md occurred.

Write FEATURES.md: every choice the format allows, each with the section of format.md that allows it
and how the choice bytes pick it. Write QUESTIONS.md while you work, not at the end: every place where
you had to choose between two readings, or where format.md does not say whether a block is valid. For
each: the heading of the section, the sentence quoted, the readings, the one you took and why.

At the end, report: the files, how to build, the counts of cr-gen for 100000 blocks, the number
of questions.
```

### Brief I: the rules and the claims

```text
This directory holds the specification of a compression format, format.md. Read it and write two
lists, from format.md only. Write no code.

Do not look for any other description or implementation of this format, on the internet or anywhere
else, and do not use one you might remember. Sentences marked "Why:" are not part of the format.

RULES.md: every rule that makes a compressed block invalid. One row each: an ID (R1, R2, ...), the
section, the sentence quoted, the rule as a condition on the bytes, and the smallest change of a valid
block that breaks this rule and no other. Also the rules that follow from several sentences together
and are written nowhere as one rule, marked "implied", with the sentences they follow from. Also each
place where a block is valid although one might expect it not to be.

CLAIMS.md: every number and every statement in format.md that follows from the rules or the tables
and could be wrong, the "Why:" sentences included, e.g. a bound, a count, a size, a value in the
example. One row each: an ID (C1, ...), the section, the sentence quoted, and how it could be checked.

Write QUESTIONS.md as for the two lists: each place where format.md can be read two ways, quoted, with
the readings.
```

## Step 3: the questions, sorted

The runner reads every `QUESTIONS.md` and sorts each entry, with format.md and, now, `src/`:

| kind | what it is | what follows |
| --- | --- | --- |
| A | format.md answers it, in one place, and the agent found it | nothing; two agents asking the same is a hint to make it clearer anyway |
| B | format.md is silent, or can be read two ways, or needs two places together that don't point to each other | a change of format.md, even when the agent's guess was the one `src/` makes |
| C | format.md is clear and `src/` does something else | a difference, see [What a difference means](#what-a-difference-means) |
| D | the agent misread a clear sentence | a bug in that clean-room decoder; and a hint, if a second agent misread the same sentence |

Kind B is the main result of the whole procedure. A guess that came out the way `src/` does it is not
found by any fuzzer, only here. Each entry and its kind go into the run's notes, with the change of
format.md it led to.

## Step 4: every decoder on the same blocks

Every decoder gets the same blocks and must say the same: valid or not, and for a valid one the same
block. The decoders: `seqlz_decompress()`, `seqlz_ref.c`, D's, R's, and `seqlz_ref.py` on a sample,
as it is slow. `tools/format-check/all-decoders` (to be written) takes
files of blocks and prints one line per block per decoder, verdict and SHA-256 of the block.

The blocks:

- the blocks of `tools/seqlz-ref/check.sh`, the ones of `test/seqlz_ref_test.cpp`, and the example of
  format.md;
- the agents' own test blocks, D's and R's, through every decoder, not only their own;
- `seqlz_compress()` at levels 1 to 3 of the made-up pages of `fuzz/make_seeds.c` and
  `tools/seqlz-worst/pages/`;
- `seqlz_compress()` at levels 1 to 3 of real memory pages, collected as
  [measuring.md](measuring.md) describes: at least 200 000 pages from several machines, desktops
  and phones; this runs only locally, and only the verdicts and SHA-256 leave the machine, as the
  pages hold keys and passwords;
- of the compressed blocks of the samples above: every cut (every length from 0 to `len - 1`) of one
  block in 100, every single bit flipped of one block in 1000, and one random damage of 1 to 8 bytes
  of each.

With 200 000 pages that is about 2 * 10^7 blocks; the runner states the count.

## Step 5: valid blocks by construction

W's `cr_write()` makes a valid block and the block it decodes to from any bytes, which turns it into a
fuzz target: `fuzz/seqlz_gen_fuzz.c` (to be written) takes the input as choice bytes, has W write a
block, and checks that every decoder accepts it and gives W's block. Then it uses the rest of the
input to damage the block (a cut, bits flipped, bytes changed) and checks that the decoders agree on
the damaged one. It starts in step 4's run with `cr-gen`'s blocks, and runs in step 7 with the other
targets.

Where W and the decoders differ on an undamaged block, the decoder is not automatically right: W read
the same format.md, see [What a difference means](#what-a-difference-means).

W's `FEATURES.md` should have at least the items below; the runner adds what is missing and has W add
it in a second round, in the same clean room. A missing item counts as a question of kind B if W
could not tell from format.md that the choice is allowed.

- Layout: raw and coded literals; `n` = 1, 7, 8, 9, `BLOCK - 1`, `BLOCK`; coded literals with each
  `code` 0 to 8, each `w` 5 to 12, also larger than the largest `s[j]` needs,
  `e` 0 and 1, each `dt` 1 to 3, streams with no literal (`n < 8`), a stream that ends at each of the
  8 bit positions of its last byte.
- Sequences: every token 0 to 3071 at least once, each with a code of its own and escaped, and the
  tokens without a code escaped; `ll` 0, 14, 15, 16 and the edges of each range of a length value, up
  to the largest one a block allows; `ml` 4, 34, 35, 36 and the edges of each range, a match that ends
  exactly at the end of the block; the last sequence with no literals and with all of them; a block of
  literals only with no bits in `B`.
- Offsets: each class 0 to 5 with its smallest and largest offset; class 0 in the first sequence
  (`last` is 1); class 3 for an offset that a class with fewer bits holds; an offset of exactly `|out|`;
  `off < ml`, overlapping copies, with `off` 1, 2, 3 and 8.
- Deltas: `dt = 3` with `last` 1, 2, 3, 4, 5, 7, 8 and a large one; the first literals with `p < x`;
  a delta whose `out[p - x]` is a byte of a match; deltas in the last sequence.
- A block's own table: each code length 1 to 10, each run symbol `k` 1 to 9 with the smallest and
  largest `x`, a table whose last code is byte value 255, a table of only two byte values with codes
  of 1 bit, a table that ends at each bit position of its last byte.
- Bits: `B` that ends at each of the 8 bit positions of its last byte; a block whose `B` is empty.

## Step 6: one valid and one invalid block per rule

For each rule of I's `RULES.md`, the runner makes a pair of blocks with W's writer and a small edit:
one that is valid and as close to the edge as possible, and one that breaks this rule and no other.
The expected verdict comes from the inventory, not from a decoder; this is the only step whose
oracle is format.md itself and not the agreement of decoders. Every decoder must give the expected
verdict for both blocks.

The pairs go into `test/` as a doctest (`test/seqlz_rules_test.cpp`, to be written), one `SUBCASE`
per rule ID, so that CI keeps them. The rules that `test/seqlz_ref_test.cpp` already covers by hand
(the codes above 8, `dt` other than 1 to 3, a literal code outside its stream, a last sequence with a
token, own tables that overfill the code space, name a byte value above 255 or have bits after their
end) get their pair anyway, from the inventory's wording.

## Step 7: coverage-guided fuzzing

`fuzz/seqlz_diff_fuzz.c` gets D's and R's decoders next to `seqlz_ref.c`: any input, every decoder,
the same verdict and block. `fuzz/seqlz_gen_fuzz.c` from step 5 runs next to it. Seeds: the made-up
blocks of steps 4 to 6, never blocks from the samples, which would put page contents into AFL's queue.
A dictionary for AFL++ with the values of byte 2 (`t`) and of `dt`.

Budget, with `fuzz/afl.sh` (targets `diff` and `gen`):

- at least 12 instances for 8 hours, and then on until no instance found a new path for 2 hours
  (`afl-whatsup`, "last find");
- 1 hour of `fuzz/smoke.sh` with MSan (`-DSEQLZ_SANITIZE=OFF -DSEQLZ_MSAN=ON`), which AFL's builds
  don't have.

No benchmark runs on the same machine at the same time, it would spoil the benchmark's numbers. The
runner records the execs, paths and the last find of each target. After the run, every input of AFL's
queue goes through step 4's `all-decoders` once more, also through `seqlz_ref.py`.

## Step 8: every rule of src/ is a rule of format.md

The runner lists every place in `src/seqlz_decompress.c` and `src/seqlz_decompress_literals.c` where
`seqlz_decompress()` decides that a block is invalid, and maps each to the rule IDs of I's
`RULES.md`. Both ways:

- a reject site without a rule is a rule of `src/` that format.md does not have (kind B or C), or dead
  code;
- a rule without a reject site is a rule that `src/` does not check (kind C), or one that follows from
  other checks; then the run's notes say which.

Then line and branch coverage (`llvm-cov`, a clang build with `-fprofile-instr-generate
-fcoverage-mapping`) of the two files, of `seqlz_ref.c` and of D's and R's decoders, over all blocks
of steps 4 to 7. Every line and every branch reached, or the notes say why not, e.g. a check that a
`BUILD_BUG_ON` makes unreachable. A reject site that is never reached is a rule nobody tested.

## Step 9: planted differences, to see that the check finds them

A check that finds no difference only says something if it finds the differences that are there. So
the runner plants them: one change of `src/seqlz_decompress.c` or `src/seqlz_decompress_literals.c`
at a time, built, and step 4's blocks, step 6's pairs and then 1 hour of step 7's fuzzing with 4
instances on it. Each must be found. The mutants, at least:

- each reject site of step 8 left out, one at a time;
- each bound of a reject site off by one (`>` for `>=` and back);
- `last` starting at 0 or 2 instead of 1;
- an escaped token of 3072 to 4095 accepted;
- `w = 4 + ...` instead of `5 + ...`;
- literal `j` read from stream `j + 1 mod 8`;
- `dt = 3` with `last = 3` giving `x = 3` instead of 6;
- a delta added where `p < x`;
- the padding of `B`, of a literal stream, and of a block's own table not checked, each on its own;
- a run of `2^(k-1) + x` byte values without a code off by one;
- `code` 9 taken as 8.

Each mutant must build without warnings before its result counts: with `-Werror`, a mutant that leaves
a variable unused does not build, the old binary runs, and the mutant looks as if it survived. A
surviving mutant is either equivalent (the notes show why no block tells it apart) or a hole in steps
4 to 7, which then gets a block that finds it. No mutant stays in the tree.

## Step 10: the numbers of format.md

Every row of I's `CLAIMS.md` is checked, by `tools/format-check/claims.py` (to be written) where a
program can, by a short argument in the run's notes where not. At least these:

- the table of tables: the symbols of each table, its longest code, and that every table is complete;
- "only 2048 codes of 11 bits, fewer than the 3072 tokens": the number of tokens with a code of their
  own;
- `w` 5 to 12 holds every stream: at most `BLOCK * 10 / 64` bytes, 640, which needs `w = 10`;
- the largest length value, `2^P - 1`, and `BLOCK - 19` as the largest one a block needs;
- at most `BLOCK / 4 + 1` sequences, also for invalid blocks;
- "a valid block has `n >= 1`" and "the first match's offset is at most that sequence's `ll`";
- the `x` of `dt = 3` for each `last`;
- the example, bit by bit, as its table says;
- the numbers in "Why:" sentences, e.g. "0.4 to 0.5 bytes per byte value with a code": measured again,
  or the sentence changed.

## Step 11: big-endian and 32-bit

The blocks of steps 4 (the made-up part) and 6 through `seqlz_decompress()` on s390x and 32-bit x86, as
CI's endian job does with `test/seqlz_endian.c`: the verdicts and the SHA-256 of every block the same
as on x86-64. The clean-room decoders too, if they build there; a difference of theirs between CPUs is
a bug in them, not in format.md.

## What a difference means

Every difference is cut down to the smallest block that shows it (`afl-tmin`, or libFuzzer's
`-minimize_crash=1`). Then the runner decides, from the text of format.md and not from which decoder
is older:

- **format.md is clear, `src/` does something else**: a bug in `src/`, or a rule format.md should
  change to. The maintainer decides which; a change of the format follows
  [format-and-tables.md](../.claude/rules/format-and-tables.md).
- **format.md is clear, a clean-room decoder does something else**: a bug in that decoder (kind D),
  fixed in its own commit. If two agents read the same sentence wrong, the sentence changes too.
- **format.md allows both**: kind B, format.md changes.

Every difference gets its block as a test in `test/seqlz_rules_test.cpp`, with the rule ID, whatever
the outcome.

## After a change of format.md

A change of format.md during the run means that the decoders were written from a text that is no
longer the one that is checked. So: new clean rooms, new sessions, steps 1 to 7 again in full, step 8
and 10 for the changed rules, step 9 for the changed reject sites. Never the old session with the new
text: it has the old reading in its context.

A later change of the format, or a clearer sentence in format.md, needs at least: new D, W and I
from step 1, steps 3 to 6, step 7 for 2 hours, and step 9 for the reject sites the change
touched. The decoders of earlier runs stay in `tools/format-check/runs/` and in the diff
target only as long as they are updated for the change, by someone who reads format.md, not `src/`.

## What this does not show

- The same family of models wrote format.md, `src/`, `seqlz_ref.c` and, here, the clean-room code.
  They may read a sentence wrong in the same way, and then all decoders agree and the questions list
  is empty. A clean room on a model of another vendor, or a person who writes a decoder, would show
  more.
- One clean room shows that one reader got it right. A guess that came out right shows only in
  `QUESTIONS.md`, and only if the agent noticed that it guessed.
- Fuzzing that found nothing in its time shows that, not that there is no difference.
- "What the compressor writes" is not part of the format and not checked, except that the blocks
  `seqlz_compress()` writes decode.
- How the tables were trained is not checked; `tools/seqlz-ref/check.sh` checks that they are what
  `tools/kernel-port/gen-seqlz-lengths.py` makes from the counts, and format.md's SHA-256 that a decoder
  has the right ones.

## Runs

None yet. Each run gets one entry: the date, the commit of format.md, the models of D, R, W and I, the
audits, the questions by kind and the changes of format.md they led to, the number of blocks of step
4, the execs and paths of step 7, the mutants found and survived, the coverage of step 8, and every
difference with its outcome. The summary also goes to format.md's
[How this file was checked](format.md#how-this-file-was-checked).
