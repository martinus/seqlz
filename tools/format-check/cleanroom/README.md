# What the clean-room agents wrote

The code and lists of the last round of phase 1 of [docs/format-check.md](../../../docs/format-check.md),
round `2026-11-02-0703Z`, as the agents wrote them from `docs/format.md` (R from the kernel's copy)
without ever seeing `src/`. Their questions, audits and the runner's notes are in [`../runs/`](../runs).
Only the files the agents brought into their room are kept, not the spec, the tables they were given or
what their builds made; the table headers are their own generators' output.

| dir | agent | what |
| --- | --- | --- |
| [`D/`](D) | D, `claude-opus-5-5` | a decoder, `cr_decode()` in `src/cr_decode.c`, with tests |
| [`R/`](R) | R, `claude-fable-5-1` | a decoder from the kernel's copy, `cr_decode()` in `seqlz.c`, with tests |
| [`W/`](W) | W, `claude-opus-5-5` | a writer, `cr_write()` in `cr.c`: any bytes make a valid block; `FEATURES.md`, what it can write |
| [`I/`](I) | I, `claude-opus-5-5` | `RULES.md`, every rule that makes a block invalid, and `CLAIMS.md`, every number of format.md |

`fuzz/seqlz_diff_fuzz.c` holds D's and R's decoders against `seqlz_decompress()` and `seqlz_ref.c`, and
`fuzz/seqlz_gen_fuzz.c` has W write blocks that all of them must decode. Both build them with the
fuzzers' sanitizers but without the repository's warning flags, and rename D's and R's `cr_decode()`
when they compile them, so that their code stays as written. A bug fix in it is a commit of its own.
