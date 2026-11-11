# seqlz in short: the result, and the choices

This page was the overview until 11th November 2026. Its parts said what other pages say too, so
each now lives in one place, and the headings below only point there.

## The result: lz4's time, zstd's size, almost

The numbers against zram's codecs, on the kernel VM, the Mi 9T and the Pixel 9a, with the charts:
[why.md, What it buys](why.md#3-what-it-buys).

## How it works, in short

[The README, How it works](../README.md#how-it-works), and the book,
[seqlz, line by line](https://martin.ankerl.com/seqlz/), for every detail.

## Level 3: zstd 3's memory in less than half its time

The numbers: [why.md, What it buys](why.md#3-what-it-buys). What level 3 does:
[the README](../README.md#how-it-works) and the book's
[deltas](https://martin.ankerl.com/seqlz/#ch-header--deltas).

## What is not known yet

[why.md, Objections](why.md#5-objections) and [Open, and what would settle it](why.md#7-open-and-what-would-settle-it).

## The choices, with their numbers

The book's chapter [Why seqlz is shaped this way](https://martin.ankerl.com/seqlz/#ch-decisions): each choice
against its alternative, with what it was measured at.

## The sequences of 30th September

The counts of offsets, literal and match lengths with today's classes are in the book's
[format chapter](https://martin.ankerl.com/seqlz/#ch-format); the ones of 30th September in
[explored-designs.md](explored-designs.md).
