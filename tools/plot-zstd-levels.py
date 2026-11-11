#!/usr/bin/env python3
# SPDX-License-Identifier: MIT OR GPL-2.0-only
"""seqlz's levels against zstd at every level: memory against time, from logs of tools/zram-vm/run.sh with
MODE=swap and zstd's levels as zram's algorithm_params, e.g. ALGOS=lz4,seqlz-2,zstd:-1,zstd:3.

One row per corpus, from one or more logs of it; a codec in several logs gets the mean of them. Three
panels: the swap-out with one page per call, the swap-in with the compressed data cold, and the time per
page written of the score, docs/plan.md §1.1, all means per page. zstd's levels are one line, in the
order of the levels. Other codecs in the logs than zstd, lz4, lzo-rle and seqlz's levels, e.g. wkdm, are
left out, also of the line of the best choices. The numbers come from seqlz-score, so they are the score's: the corpus's pages
without the same-filled ones the swap mode adds. Lower left is better.

    ALGOS=lz4,seqlz-2,zstd:-5,zstd:-1,zstd:1,zstd:3 MODE=swap KARGS=zram.zram_prefetch=8 \\
        tools/zram-vm/run.sh <linux tree> <corpus> >a.log
    tools/plot-zstd-levels.py --row "Desktop=a.log,b.log" --out zstd-levels.svg

Needs matplotlib, the Noto Sans font, and a build with seqlz-score.
"""

import argparse
import importlib.util
import math
import subprocess
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import matplotlib.ticker

HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location("plot_codecs", HERE / "plot-codecs.py")
plot_codecs = importlib.util.module_from_spec(spec)
spec.loader.exec_module(plot_codecs)

ZSTD = plot_codecs.STYLE["zstd"][0]
OTHERS = {"lz4": "lz4", "lzo-rle": "lzo-rle", "seqlz": "seqlz-1", "seqlz-lit": "seqlz-2", "seqlz-1": "seqlz-1",
          "seqlz-2": "seqlz-2", "seqlz-3": "seqlz-3"}


def score_rows(score, log, r):
    """bytes per page, write and read in us, per zram device of a log, from seqlz-score"""
    out = subprocess.run([score, "--reads-per-write", str(r), log], capture_output=True, text=True, check=True).stdout
    rows = {}
    for line in out.splitlines()[3:]:
        f = line.split()
        # codec, bytes, write, read, recompression, per page written, and since #203 the memory per CPU
        if len(f) not in (6, 7):
            break
        rows[f[0]] = (float(f[1]), float(f[2]), float(f[3]))
    return rows


def mean_rows(score, logs, r):
    sums = {}
    for log in logs:
        for name, v in score_rows(score, log, r).items():
            sums.setdefault(name, []).append(v)
    return {n: tuple(sum(x) / len(vs) for x in zip(*vs)) for n, vs in sums.items()}


def place_labels(fig, ax, labels, obstacles, lines):
    """Puts each label (text, (x, y) in data, style) next to its point, where it covers no marker, no other
    label, no line of `lines` and no box of `obstacles` (display coordinates), and stays inside the axes:
    the nearest free position of rings of candidates around the point, in display points. A label further
    out than the first ring gets a thin line to its point. Labels of points with many neighbours go first,
    so that they get the near places."""
    renderer = fig.canvas.get_renderer()
    box = ax.get_window_extent(renderer)
    scale = fig.dpi / 72
    marker = 4 * scale  # about half the size of a marker, in pixels
    taken = [b for b in obstacles]
    for _, xy, _ in labels:
        x, y = ax.transData.transform(xy)
        taken.append((x - marker, y - marker, x + marker, y + marker))

    def hits(b1, b2):
        return b1[0] < b2[2] and b2[0] < b1[2] and b1[1] < b2[3] and b2[1] < b1[3]

    def cuts(bb, seg):
        # a segment against a box: sample it, the segments here are short against the boxes
        (x1, y1), (x2, y2) = seg
        return any(bb[0] <= x1 + (x2 - x1) * t / 16 <= bb[2] and bb[1] <= y1 + (y2 - y1) * t / 16 <= bb[3]
                   for t in range(17))

    segs = [(ax.transData.transform(a), ax.transData.transform(b)) for a, b in lines]

    def neighbours(xy):
        x, y = ax.transData.transform(xy)
        return sum(1 for _, o, _ in labels if abs(ax.transData.transform(o)[0] - x) < 40 * scale
                   and abs(ax.transData.transform(o)[1] - y) < 40 * scale)

    def crosses(p1, p2, q1, q2):
        # whether two segments cross, by the signs of the cross products
        def side(a, b, c):
            return (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0])
        d1, d2 = side(q1, q2, p1), side(q1, q2, p2)
        d3, d4 = side(p1, p2, q1), side(p1, p2, q2)
        return d1 * d2 < 0 and d3 * d4 < 0

    def blocked(seg, own):
        # a line from a point to its label: it may not cross a line, another such line or a label
        if any(crosses(*seg, *sg) for sg in segs + leaders):
            return True
        a, b = seg
        inner = ((a[0] + (b[0] - a[0]) * 0.15, a[1] + (b[1] - a[1]) * 0.15), (a[0] + (b[0] - a[0]) * 0.85,
                                                                              a[1] + (b[1] - a[1]) * 0.85))
        return any(cuts(o, inner) for o in taken if o is not own)

    leaders = []
    directions = [(1, 0.35), (-1, 0.35), (0, 1), (0, -1), (1, -0.6), (-1, -0.6), (1, 1), (-1, 1), (1, -1), (-1, -1),
                  (0.5, 1), (-0.5, 1), (0.5, -1), (-0.5, -1), (1, 0), (-1, 0)]
    for text, xy, style in sorted(labels, key=lambda lb: -neighbours(lb[1])):
        t = ax.text(0, 0, text, **style)
        w, h = (lambda e: (e.width, e.height))(t.get_window_extent(renderer))
        t.remove()
        px, py = ax.transData.transform(xy)
        best = None
        fallback = None
        for ring in (6, 11, 17, 24, 32, 42, 54, 68, 84):
            for dx, dy in directions:
                cx, cy = px + dx * (ring * scale + (w / 2 if dx else 0)), py + dy * (ring * scale) + (h / 2 if dy > 0 else -h / 2 if dy < 0 else 0)
                bb = (cx - w / 2 - 2, cy - h / 2 - 1, cx + w / 2 + 2, cy + h / 2 + 1)
                if bb[0] < box.x0 or bb[2] > box.x1 or bb[1] < box.y0 or bb[3] > box.y1:
                    continue
                bad = sum(hits(bb, o) for o in taken) + sum(cuts(bb, sg) for sg in segs + leaders)
                if ring > 6:
                    bad += blocked(((px, py), (cx, cy)), None)
                if bad:
                    if fallback is None or bad < fallback[0]:
                        fallback = (bad, (cx, cy, bb, ring))
                    continue
                best = (cx, cy, bb, ring)
                break
            if best:
                break
        if best is None:
            # nowhere free: where it collides least
            best = fallback[1]
        cx, cy, bb, ring = best
        taken.append(bb)
        if ring > 6:
            leaders.append(((px, py), (cx, cy)))
        data = ax.transData.inverted().transform((cx, cy))
        arrow = None
        if ring > 6:
            arrow = dict(arrowstyle="-", color=style.get("color", "#555555"), lw=0.6, alpha=0.7, shrinkA=0, shrinkB=5)
        ax.annotate(text, xy, xytext=data, textcoords="data", ha="center", va="center", arrowprops=arrow, **style)


def zoom(ax, points):
    """An inset in the lower right of ax that enlarges the area of points, (label, (x, y)) in the order of zstd's
    levels, with their line, and a frame around that area in ax; returns its labels to place, as the panels'."""
    xs, ys = [p[1][0] for p in points], [p[1][1] for p in points]
    padx, pady = (max(xs) - min(xs)) * 0.18 + 0.02, (max(ys) - min(ys)) * 0.35 + 0.2
    ins = ax.inset_axes([0.40, 0.06, 0.57, 0.36])
    ins.plot(xs, ys, color=ZSTD, lw=1.4, marker="D", ms=6, mec="white", mew=0.6, zorder=3)
    ins.set_xlim(min(xs) - padx, max(xs) + padx)
    ins.set_ylim(min(ys) - pady, max(ys) + pady)
    ins.tick_params(labelsize=8, length=2)
    ins.yaxis.set_major_formatter(matplotlib.ticker.FuncFormatter(lambda v, _: f"{v:g}%"))
    ins.set_title("zstd's levels from 1 on, enlarged", fontsize=8.5, color=ZSTD, loc="left")
    ins.grid(True, color="#eeeeee", lw=0.6)
    ax.indicate_inset_zoom(ins, edgecolor="#999999", alpha=0.8)
    labels = [(t, xy, dict(fontsize=9.5, color=ZSTD)) for t, xy in points]
    return ins, labels, [], [(points[k][1], points[k + 1][1]) for k in range(len(points) - 1)]


def level(name):
    return int(name.split(":", 1)[1]) if name.startswith("zstd:") else 3


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--row", action="append", required=True, metavar="TITLE=LOG[,LOG...]",
                    help="a row: its title and the logs of one corpus")
    ap.add_argument("--out", action="append", required=True, help="output file, .png or .svg, can be given more than once")
    ap.add_argument("--score", default=str(HERE.parent / "build" / "seqlz-score"), help="the seqlz-score binary")
    ap.add_argument("--reads-per-write", type=float, default=0.73, help="r of the score, default 0.73, a phone's (docs/plan.md §1.1)")
    ap.add_argument("--max-us", type=float, default=60,
                    help="where the time axes of swap-out and score end; slower levels are arrows at the edge")
    ap.add_argument("--clock", default="4.5 GHz", help="the fixed clock of the CPU the VM ran on, for the footer")
    ap.add_argument("--source", default=None, help="what was measured, for the footer; default git describe")
    args = ap.parse_args()

    r = args.reads_per_write
    rows = []
    for row in args.row:
        title, sep, logs = row.partition("=")
        if not sep:
            raise SystemExit(f"--row {row}: TITLE=LOG[,LOG...]")
        codecs = mean_rows(args.score, logs.split(","), r)
        rows.append((title, {n: v for n, v in codecs.items() if n.startswith("zstd") or n in OTHERS}))

    panels = [("Swap-out, one page per call", lambda w, rd: w), ("Swap-in, compressed data cold", lambda w, rd: rd),
              (f"Time per page written: swap-out + {r:g} × swap-in", lambda w, rd: w + r * rd)]
    plt.rcParams.update({"font.family": "Noto Sans", "font.size": 10})
    head, foot, row_h = 0.8, 0.9, 4.6
    height = head + foot + row_h * len(rows)
    fig, axes = plt.subplots(len(rows), 3, figsize=(17, height), squeeze=False)
    top_mem = max(b for _, codecs in rows for b, _, _ in codecs.values()) / 4096 * 100
    ymax = 5 * math.ceil(top_mem * 1.1 / 5)

    pending = []
    for i, (title, codecs) in enumerate(rows):
        zstd = sorted((n for n in codecs if n.startswith("zstd")), key=level)
        for j, (what, time) in enumerate(panels):
            ax = axes[i][j]
            pts = {n: (time(w, rd), b / 4096 * 100) for n, (b, w, rd) in codecs.items()}
            # the swap-in panel shows all, the others end at --max-us: the slow levels would leave the rest in a
            # corner
            limit = None if j == 1 else args.max_us
            shown = [n for n in zstd if limit is None or pts[n][0] <= limit]
            right = limit or max(t for t, _ in pts.values()) * 1.08
            right = right if limit else math.ceil(right)
            ax.plot([pts[n][0] for n in shown], [pts[n][1] for n in shown], color=ZSTD, lw=1.6, marker="D", ms=6,
                    mec="white", mew=0.6, zorder=3, label="zstd, by level")
            # the levels from 1 on are a crowd in the swap-in panel, as they read alike: they get a zoomed
            # inset in the empty part below, with their labels, and only the others a label next to them
            crowd = [n for n in shown if limit is None and level(n) >= 1]
            if len(crowd) > 3:
                pending.append(zoom(ax, [(str(level(n)), pts[n]) for n in crowd]))
            else:
                crowd = []
            labels = [(str(level(n)), pts[n], dict(fontsize=10, color=ZSTD)) for n in shown if n not in crowd]
            lines = [(pts[m], pts[n]) for m, n in zip(shown, shown[1:])]
            obstacles = []
            beyond = [n for n in zstd if n not in shown]
            if beyond:
                ax.plot([pts[shown[-1]][0], limit], [pts[shown[-1]][1], pts[beyond[0]][1]], color=ZSTD, lw=1.6, ls=":",
                        zorder=3)
                ax.plot([limit] * len(beyond), [pts[n][1] for n in beyond], marker=">", ms=7, color=ZSTD, ls="none",
                        zorder=3, clip_on=False)
                text = "\n".join(f"level {level(n)}: {pts[n][0]:.0f} µs" for n in beyond)
                off = ax.annotate("off the axis:\n" + text, (limit, pts[beyond[0]][1]), textcoords="offset points",
                                  xytext=(-8, 18), ha="right", va="bottom", fontsize=8.5, color=ZSTD)
                obstacles.append(off)
                lines.append((pts[shown[-1]], (limit, pts[beyond[0]][1])))
            for n, label in OTHERS.items():
                if n in pts:
                    color, marker = plot_codecs.STYLE[label]
                    main_codec = label in plot_codecs.MAIN
                    ax.plot(*pts[n], marker=marker, ms=11 if main_codec else 8, color=color, mec="white", mew=0.8,
                            ls="none", zorder=4, label=label)
                    labels.append((label, pts[n], dict(fontsize=9, color=color,
                                                       fontweight="bold" if main_codec else "normal")))
            pending.append((ax, labels, obstacles, lines))
            if j == 2:
                h = [p for p in plot_codecs.hull([(t, m, n) for n, (t, m) in pts.items()]) if p[0] <= limit]
                ax.plot([p[0] for p in h], [p[1] for p in h], color="#aaaaaa", lw=1.2, ls="--", zorder=1,
                        label="best for some exchange rate")
                lines.extend(((p[0], p[1]), (q[0], q[1])) for p, q in zip(h, h[1:]))
            ax.set_xlim(0, right)
            ax.set_ylim(0, ymax)
            ax.set_yticks(range(0, ymax + 1, 5))
            ax.set_yticklabels([f"{v}%" for v in range(0, ymax + 1, 5)])
            ax.set_xlabel(f"{what.lower()}: mean per page, µs")
            if i == 0:
                ax.set_title(what, fontsize=12, fontweight="bold", loc="left")
            if j == 0:
                ax.set_ylabel(f"{title}\n\nmemory used by zsmalloc,\nin % of the uncompressed pages")
            ax.grid(True, color="#e6e6e6", lw=0.8)
            ax.set_axisbelow(True)
            for side in ("top", "right"):
                ax.spines[side].set_visible(False)

    handles, labels = axes[0][2].get_legend_handles_labels()
    fig.legend(handles, labels, loc="upper center", ncol=len(labels), frameon=False, bbox_to_anchor=(0.5, 1 - 0.42 / height))
    fig.suptitle("seqlz's levels against zstd at each level, in zram as swap: memory against time, lower left is better",
                 fontsize=14, fontweight="bold", y=1 - 0.05 / height)
    fig.text(
        0.01,
        0.01,
        f"Linux kernel in a VM, zram as the only swap, zsmalloc, one device per codec ({args.source or plot_codecs.git_source()}). "
        f"{plot_codecs.cpu_model()}, one CPU at a fixed {args.clock}. The levels are zram's algorithm_params, the numbers next to\n"
        f"zstd's points; levels slower than {args.max_us:g} µs are arrows at the edge of the swap-out and score panels. Per page the median of 3 runs, then the mean over the pages; a codec in several boots gets the mean of them. "
        "Swap-in cold: the compressed data flushed from the cache before the fault.\n"
        f"seqlz-2 prefetches the compressed data in its zram backend. Time per page written: the score of docs/plan.md §1.1, "
        f"{r:g} reads per write, with the time of the whole fault, which includes the kernel's part, the same for every codec.",
        fontsize=8.5,
        color="#555555",
        va="bottom",
    )
    fig.tight_layout(rect=(0, foot / height, 1, 1 - head / height))
    # the labels last, when the axes have their final size: they are placed in display coordinates
    renderer = fig.canvas.get_renderer()
    for ax, labels, obstacles, lines in pending:
        boxes = [tuple(o.get_window_extent(renderer).extents) for o in obstacles]
        place_labels(fig, ax, labels, boxes, lines)
    for out in args.out:
        fig.savefig(out, dpi=150)


if __name__ == "__main__":
    main()
