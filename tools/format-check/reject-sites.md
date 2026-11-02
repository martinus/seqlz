# Where seqlz_decompress() rejects a block, and which rule says so

Step 8 of [docs/format-check.md](../../docs/format-check.md): every place in `src/seqlz_decompress.c` and
`src/seqlz_decompress_literals.c` that rejects a block, against the rules of the clean-room agent I's
list, [`cleanroom/I/RULES.md`](cleanroom/I/RULES.md), both ways. Line numbers of the commit in the
entry of [Runs](../../docs/format-check.md#runs); `test/seqlz_rules_test.cpp` has a valid and an invalid
block for each rule named here.

## Site to rule

| site | what it checks | rules |
| --- | --- | --- |
| `seqlz_decompress.c:437` | `src_len < 2` | R1 |
| `seqlz_decompress.c:450` | `n > BLOCK` | R2 |
| `seqlz_decompress.c:456` | no scratch memory given | none: the call, not the block |
| `seqlz_decompress.c:460` | the coded literals, any of the sites of `seqlz_decompress_literals.c` below | R4 to R9, R19 to R21 |
| `seqlz_decompress.c:476` | raw literals: `2 + n > len` | R3 |
| `seqlz_decompress.c:520` | an escaped token of 3072 or more | R13 |
| `seqlz_decompress.c:565` | the fast path: offset 0, or past `\|out\|` after the literals | R14, R15 |
| `seqlz_decompress.c:577` | more literals than are left (also `lit > lit_end` after the fast path's 16 bytes), or than bytes left in the block | R17, R18 |
| `seqlz_decompress.c:591` | offset 0, or past `\|out\|` after the literals; a match past the end of the block | R14, R15, R16 |
| `seqlz_decompress.c:620` | at the end: 8 or more bits left, bits needed past the end (`count` negative), bytes not read, a padding bit that is not 0 | R11, R12, R12b |
| `seqlz_decompress_literals.c:236` | a run of byte values without a code past 255 | R8c |
| `seqlz_decompress_literals.c:257` | an own table's code space overfull, a code for byte value 256, the table past `len`, a padding bit that is not 0 | R8a, R8b, R8e, R8d |
| `seqlz_decompress_literals.c:289` | `len < 3 + w + e`, which with `len < 3` takes the smallest `w` and is true | R4, R5 |
| `seqlz_decompress_literals.c:292` | `code` above 8 | R7 |
| `seqlz_decompress_literals.c:296` | with `e = 1`, `dt` not 1 to 3 | R6 |
| `seqlz_decompress_literals.c:317` | the own table, the sites 236 and 257 | R8 |
| `seqlz_decompress_literals.c:353` | `n > BLOCK`, which the caller checked already; kept for the compiler's bounds | R2 |
| `seqlz_decompress_literals.c:356` | the header, the sites 289 to 317 | R4 to R8 |
| `seqlz_decompress_literals.c:363` | the header, the own table and the streams past `len` | R9 |
| `seqlz_decompress_literals.c:404` | a stream's codes not ending in its last byte, a padding bit, a size for a stream without literals (`left` is then `8 * s[j]`) | R19, R20, R21 |

## Rule to site

| rule | sites |
| --- | --- |
| R1 | `seqlz_decompress.c:437` |
| R2 | `seqlz_decompress.c:450`, `seqlz_decompress_literals.c:353` |
| R3 | `seqlz_decompress.c:476` |
| R4, R5 | `seqlz_decompress_literals.c:289` |
| R6 | `seqlz_decompress_literals.c:296` |
| R7 | `seqlz_decompress_literals.c:292` |
| R8a, R8b, R8d, R8e | `seqlz_decompress_literals.c:257` |
| R8c | `seqlz_decompress_literals.c:236` |
| R9 | `seqlz_decompress_literals.c:363` |
| R10 | none, and none needed: I's list has it as no condition of its own, a table that never fills its code space fails R8b, R8c or R8e |
| R11, R12, R12b | `seqlz_decompress.c:620` |
| R13 | `seqlz_decompress.c:520` |
| R14, R15 | `seqlz_decompress.c:565`, `:591` |
| R16 | `seqlz_decompress.c:591` |
| R17, R18 | `seqlz_decompress.c:577` |
| R19, R20, R21 | `seqlz_decompress_literals.c:404` |

Every site but the one for missing scratch memory is a rule of the list, and every rule but R10 has a
site. So `src/` has no rule that format.md leaves out, and format.md none that `src/` does not check.
