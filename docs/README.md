# Documentation

| document | what it is | read it if you want to |
| --- | --- | --- |
| [why.md](why.md) | the case for seqlz and against it, for a reviewer: what it costs the kernel, what it buys, the alternatives, the objections, what would make it not worth merging, and how to measure it on your own pages | decide whether seqlz belongs in the kernel |
| [seqlz.md](seqlz.md) | the overview: the result against zram's codecs, what is not known yet, and each choice with its numbers; how it works in short, with links into the book for the details | see what seqlz gives and costs, and why each choice was made |
| [seqlz, line by line](https://martin.ankerl.com/seqlz/) | a book that explains every line of `src/`, with animations and step-throughs of the real code; built from [`book/`](book) by CI | understand the code itself, line by line |
| [format.md](format.md) | the bytes of a compressed page, and which pages are valid | write a decoder, or check one |
| [format-check.md](format-check.md) | the procedure that checks format.md: decoders written from it alone, in a clean room, against `src/`, fuzzed, rule by rule | check that format.md is complete and exact, or run that check again |
| [reading.md](reading.md) | the order in which to read and review `src/`, and what to check in each part | review the code |
| [measuring.md](measuring.md) | the commands: corpus, clock, kernel VM, userspace harness, phone | reproduce a number, or measure a change |
| [plan.md](plan.md) | the goal, the score, what zram needs from a codec, the phases, the risks, and where the project stands | know why the project does what it does |
| [explored-designs.md](explored-designs.md) | every design that was measured, kept or not, with its numbers, and an index by topic | try something new without measuring it twice |

The charts in [`plots/`](plots) are made by the scripts in [`tools/`](../tools/README.md) from the logs
of the measurements.

**Where to start.** To review seqlz for the kernel: [why.md](why.md). For the codec: [seqlz.md](seqlz.md) for the result and the overview, then the
book, [seqlz, line by line](https://martin.ankerl.com/seqlz/), for how it works. For the project: [plan.md §1.1](plan.md#11-the-score-memory-against-time-not-bars)
and [§9](plan.md#9-where-the-project-stands-and-the-next-actions). Before changing anything:
[explored-designs.md](explored-designs.md) and [CONTRIBUTING.md](../CONTRIBUTING.md).
