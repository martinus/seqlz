#!/usr/bin/env python3
r"""Builds the annotated source: one file of src/ as it is at the book's commit, with a short note
next to each block of it and links into the book.

    python3 annotate.py [--out DIR] annotated/seqlz_decompress.c.md ...

build.py runs it for every annotated/*.md after the book, so it is rarely run by hand. It writes
annotated/<file>.html, for the artifact host like the book's page, and annotated/<file>.standalone.html,
for a browser, which links to the book's standalone page next to it, and the book back to it.
Everything about the code comes from the built book, seqlz-line-by-line.html in DIR (default: next
to this script): the highlighted lines, which chapters explain them, the animations and the
step-through with their data. So the notes need no C compiler and cannot show other code than the
book does.

A notes file:

  # src/seqlz_decompress.c
  markdown: how to read the file, shown above the code

  ## Copies
      a section of the file, in the map at the top

  @@ 7-30
  markdown: the note on lines 7 to 30
  @anim overlap [label]
  @stepper decode [label]
      a figure of the book, opened under the block on a click

The blocks have to cover the file, in order, without gaps. `book:#<id>` in a link is a link to that
id of the book; an id the book does not have fails the build, as does a line range past the file.
"""
import html
import json
import os
import pathlib
import re
import subprocess
import sys

import markdown

HERE = pathlib.Path(__file__).resolve().parent
REPO = HERE.parent.parent
OUT_DIR = pathlib.Path(sys.argv[sys.argv.index("--out") + 1]) if "--out" in sys.argv else HERE
BOOK_HTML = OUT_DIR / "seqlz-line-by-line.html"
# where the book's links go: the published book from the artifact's page, the book's standalone page
# next to it from the standalone one, in the same tab, so that the browser's back comes back here
BOOK_URL = {"html": "https://martin.ankerl.com/seqlz/", "standalone.html": "../seqlz-line-by-line.standalone.html"}
# set when build.py runs this: then REPO is the tree of the book's commit, see build.py
PINNED = "SEQLZ_BOOK_COMMIT"


def notes_files(here=HERE):
    """the notes there are: the file of src/ each one is about, and its notes"""
    out = {}
    for f in sorted((here / "annotated").glob("*.md")):
        m = re.match(r"#\s+(\S+)\s*$", f.read_text().split("\n", 1)[0])
        if m:
            out[m.group(1)] = f
    return out


def fail(msg):
    sys.exit(f"annotate.py: {msg}")


def md(text):
    return markdown.markdown(text, extensions=["tables", "attr_list", "sane_lists"], output_format="html5")


def parse_notes(path):
    """the file's path in the repository, the intro, and the sections, each a title and its blocks"""
    lines = path.read_text().split("\n")
    m = re.match(r"#\s+(\S+)\s*$", lines[0])
    if not m:
        fail(f"{path}:1: the first line is '# src/<file>'")
    src, intro, sections = m.group(1), [], []
    cur = intro
    for k, line in enumerate(lines[1:], 2):
        if line.startswith("## "):
            sections.append({"title": line[3:].strip(), "blocks": []})
            cur = None
            continue
        m = re.match(r"@@\s+(\d+)-(\d+)\s*$", line)
        if m:
            if not sections:
                fail(f"{path}:{k}: a block before the first '## section'")
            block = {"a": int(m.group(1)), "b": int(m.group(2)), "text": [], "figs": [], "at": k}
            sections[-1]["blocks"].append(block)
            cur = block["text"]
            continue
        m = re.match(r"@(anim|stepper)\s+([\w-]+)\s*(.*)$", line)
        if m:
            if cur is intro or cur is None:
                fail(f"{path}:{k}: a figure outside a block")
            sections[-1]["blocks"][-1]["figs"].append((m.group(1), m.group(2), m.group(3).strip()))
            continue
        if cur is None:
            if line.strip():
                fail(f"{path}:{k}: text between a section title and its first block")
            continue
        cur.append(line)
    return src, "\n".join(intro).strip(), sections


def book_commit():
    if PINNED in os.environ:
        return os.environ[PINNED]
    pin = json.loads((HERE / "book.json").read_text())["src"]
    return subprocess.run(["git", "-C", str(REPO), "rev-parse", "--verify", f"{pin}^{{commit}}"],
                          capture_output=True, text=True, check=True).stdout.strip()


def src_text(sha, src):
    """the file at the book's commit: as it is in the tree build.py extracted, else from git"""
    if PINNED in os.environ:
        return (REPO / src).read_text()
    return subprocess.run(["git", "-C", str(REPO), "show", f"{sha}:{src}"], capture_output=True, text=True,
                          check=True).stdout


def listings(book, src):
    """the book's listings of src: (first line, rows by line number, chapter label, section title,
    section id, listing id), in the book's order"""
    out = []
    fid = "L-" + src.replace("/", "-").replace(".", "-") + "-"
    heads = [(m.start(), m.group(1), m.group(2)) for m in
             re.finditer(r'<h[123] id="([^"]+)"[^>]*>(.*?)</h[123]>', book, re.S)]
    chapnos = [(m.start(), m.group(1)) for m in re.finditer(r'<span class="chapno">([^<]*)</span>', book)]
    for m in re.finditer(r'<figure class="listing" id="(' + re.escape(fid) + r'\d+)">.*?<code>(.*?)</code></pre></figure>',
                         book, re.S):
        rows = {}
        for piece in m.group(2).split('<span class="line">')[1:]:
            r = re.match(r'<span class="ln" data-n="(\d+)"></span><span class="lc">(.*)</span></span>$', piece, re.S)
            if not r:
                fail(f"a line of listing {m.group(1)} that is not as build.py writes them")
            rows[int(r.group(1))] = r.group(2)
        head = [h for h in heads if h[0] < m.start()][-1]
        chap = [c for c in chapnos if c[0] < m.start()][-1][1]
        title = re.sub(r"<[^>]+>", "", head[2]).strip()
        out.append((min(rows), rows, chap, title, head[1], m.group(1)))
    if not out:
        fail(f"the book has no listing of {src}")
    return out


def extract(book, pattern, what):
    m = re.search(pattern, book, re.S)
    if not m:
        fail(f"the book has no {what}: build it first, python3 build.py")
    return m.group(0)


def figure(book, kind, name, label):
    """the figure's markup and data from the book, its script from its file, and the stylesheets and
    scripts it needs on the page before it"""
    if kind == "anim":
        fig = extract(book, rf'<figure class="anim[^"]*" id="anim-{name}".*?</figure>', f"animation {name}")
        data = re.search(rf'<script type="application/json" id="anim-{name}-data">.*?</script>', book, re.S)
        js = (HERE / "anim" / f"{name}.js").read_text()
        css = HERE / "anim" / f"{name}.css"
        needs = ["anim-core"] + ([f"anim-css-{name}"] if css.exists() else [])
        default = re.sub(r"<[^>]+>", "", re.search(r"<figcaption>(.*?)</figcaption>", fig, re.S).group(1)) \
            if "<figcaption>" in fig else name
    else:
        fig = extract(book, rf'<figure class="stepper[^"]*" id="stepper-{name}".*?</figure>', f"stepper {name}")
        data = re.search(rf'<script type="application/json" id="stepper-{name}-data">.*?</script>', book, re.S)
        js = (HERE / "stepper" / ("stepper.js" if name == "decode" else "match.js")).read_text()
        needs = ["stepper-core"]
        default = re.search(r'aria-label="([^"]+)"', fig).group(1)
    label = label or re.split(r"(?<=[.:])\s", default, maxsplit=1)[0].rstrip(".:")
    body = fig + (data.group(0) if data else "") + f"<script>{js}</script>"
    return {"id": f"{kind}-{name}", "kind": kind, "label": label, "body": body, "needs": needs}


def page_assets(needed):
    out = ""
    if "anim-core" in needed:
        out += (f'<style>{(HERE / "anim" / "core.css").read_text()}</style>'
                f'<script>{(HERE / "anim" / "core.js").read_text()}</script>')
    if "stepper-core" in needed:
        out += (f'<style>{(HERE / "stepper" / "stepper.css").read_text()}</style>'
                f'<script>{(HERE / "stepper" / "frame.js").read_text()}</script>')
    for n in sorted(needed):
        if n.startswith("anim-css-"):
            out += f'<style>{(HERE / "anim" / (n[9:] + ".css")).read_text()}</style>'
    return out


def only_comment(h):
    # as build.py's step-through: nothing but a comment, or blank
    return not re.sub(r'<span class="cm">.*?</span>', "", h).strip()


def build(notes_path):
    book = BOOK_HTML.read_text()
    sha = book_commit()
    if f"<code>{sha[:7]}," not in book:
        fail(f"{BOOK_HTML} is not built at the book's commit {sha[:7]}: run python3 build.py")
    src, intro, sections = parse_notes(notes_path)
    text = src_text(sha, src)
    nlines = len(text.rstrip("\n").split("\n"))
    lst = listings(book, src)
    rows = {}
    for _, r, *_ in lst:
        for n, h in r.items():
            rows.setdefault(n, h)
    raw = text.split("\n")
    book_ids = set(re.findall(r'id="([^"]+)"', book))

    blocks = [b for s in sections for b in s["blocks"]]
    nxt = 1
    for b in blocks:
        if b["a"] != nxt or b["b"] < b["a"]:
            fail(f"{notes_path}:{b['at']}: block {b['a']}-{b['b']}, expected one from line {nxt}")
        nxt = b["b"] + 1
    if nxt != nlines + 1:
        fail(f"{notes_path}: the blocks end at line {nxt - 1}, {src} has {nlines}")

    def links(h, book_url):
        def one(m):
            if m.group(1) not in book_ids:
                fail(f"{notes_path}: link to #{m.group(1)}, which the book does not have")
            return f'href="{book_url}#{m.group(1)}"'
        h = re.sub(r'href="book:#([^"]+)"', one, h)
        # the published book and other sites in a tab of their own; the book next to this page in
        # this one, so that the browser's back comes back to the line
        return re.sub(r'<a href="(?=https?:)', '<a target="_blank" rel="noopener" href="', h)

    def render(book_url):
        figs, needed, out_secs, mapcells = {}, set(), [], []
        unique = {lid for *_, lid in lst if book.count(f'id="{lid}"') == 1}
        for si, s in enumerate(sections):
            sid = "s-" + re.sub(r"[^\w]+", "-", s["title"].lower()).strip("-")
            a0, b0 = s["blocks"][0]["a"], s["blocks"][-1]["b"]
            mapcells.append(f'<a class="cell" href="#{sid}" style="flex-grow:{b0 - a0 + 1}" data-sec="{sid}">'
                            f'<span class="ct">{html.escape(s["title"])}</span><span class="cr">{a0}&ndash;{b0}</span></a>')
            arts = []
            for b in s["blocks"]:
                code = []
                for n in range(b["a"], b["b"] + 1):
                    h = rows.get(n, html.escape(raw[n - 1]))
                    cls = "line cmline" if only_comment(h) else "line"
                    code.append(f'<span class="{cls}" id="L{n}"><a class="ln" href="#L{n}" data-n="{n}" '
                                f'aria-label="line {n}"></a><span class="lc">{h}</span></span>')
                note = links(md("\n".join(b["text"]).strip()), book_url)
                refs, seen = [], set()
                for first, r, chap, title, hid, lid in lst:
                    if max(r) < b["a"] or first > b["b"]:
                        continue
                    target = lid if lid in unique else hid
                    if (chap, hid) in seen:
                        continue
                    seen.add((chap, hid))
                    refs.append(links(f'<li><a href="{book_url}#{target}"><span class="rc">{html.escape(chap)}</span> '
                                      f'{html.escape(title)}</a></li>', book_url))
                fbtns = []
                for kind, name, label in b["figs"]:
                    f = figure(book, kind, name, label)
                    if f["id"] in figs:
                        fail(f"{notes_path}:{b['at']}: {kind} {name} twice; a figure can be on the page once")
                    figs[f["id"]] = f
                    needed.update(f["needs"])
                    what = "Step through" if kind == "stepper" else "Animation"
                    fbtns.append(f'<button type="button" class="figbtn" data-fig="{f["id"]}" aria-expanded="false">'
                                 f'<svg viewBox="0 0 24 24" aria-hidden="true"><path d="M8 5l11 7-11 7z"/></svg>'
                                 f'<span><span class="fk">{what}</span> {html.escape(f["label"])}</span></button>')
                rng = f"line {b['a']}" if b["a"] == b["b"] else f"lines {b['a']}&ndash;{b['b']}"
                arts.append(
                    f'<article class="blk" id="b{b["a"]}" data-sec="{sid}">'
                    f'<div class="bcode"><pre class="code numbered"><code>{"".join(code)}</code></pre></div>'
                    f'<aside class="bnote"><div class="ninner"><p class="nrange"><a href="#b{b["a"]}">{rng}</a></p>'
                    f'{note}'
                    + (f'<div class="figs">{"".join(fbtns)}</div>' if fbtns else "")
                    + (f'<p class="rhead">In the book</p><ul class="refs">{"".join(refs)}</ul>' if refs else "")
                    + '</div></aside>'
                    + ('<div class="bfig" hidden></div>' if fbtns else "")
                    + '</article>')
            out_secs.append(f'<section class="sec" id="{sid}"><h2 class="sech"><span class="secno">{si + 1}</span>'
                            f'{html.escape(s["title"])}</h2>{"".join(arts)}</section>')
        templates = "".join(f'<template id="t-{k}">{f["body"]}</template>' for k, f in figs.items())
        tpl = (HERE / "annotate.html").read_text()
        head, css = re.match(r"(.*?)<style>\n\{\{KATEX_CSS\}\}\n(.*?)</style>", (HERE / "template.html").read_text(),
                             re.S).groups()
        name = pathlib.Path(src).name
        return (tpl.replace("{{FONTS}}", "\n".join(l for l in head.split("\n") if l.startswith("<link")))
                   .replace("{{BOOK_CSS}}", css)
                   .replace("{{ASSETS}}", page_assets(needed))
                   .replace("{{NAME}}", html.escape(name))
                   .replace("{{SRC}}", html.escape(src))
                   .replace("{{LINES}}", str(nlines))
                   .replace("{{BLOCKS}}", str(len(blocks)))
                   .replace("{{COMMIT}}", sha[:7])
                   .replace("{{GITHUB}}", f"https://github.com/martinus/seqlz/blob/{sha}/{src}")
                   .replace("{{BOOK}}", book_url)
                   .replace("{{MAP}}", "".join(mapcells))
                   .replace("{{INTRO}}", links(md(intro), book_url))
                   .replace("{{BODY}}", "".join(out_secs))
                   .replace("{{TEMPLATES}}", templates))

    out = OUT_DIR / "annotated"
    out.mkdir(parents=True, exist_ok=True)
    stem = pathlib.Path(src).name
    page = render(BOOK_URL["html"])
    (out / f"{stem}.html").write_text(page)
    # the same page for a browser without the artifact host: its own skeleton
    (out / f"{stem}.standalone.html").write_text(
        '<!doctype html>\n<html lang="en">\n<head>\n<meta charset="utf-8">\n'
        '<meta name="viewport" content="width=device-width, initial-scale=1, viewport-fit=cover">\n'
        '<style>body { margin: 0; } [hidden] { display: none !important; }</style>\n</head>\n<body>\n'
        + render(BOOK_URL["standalone.html"]) + "\n</body>\n</html>\n")
    print(f"wrote {out / stem}.html and .standalone.html, {len(page) // 1024} KiB, {len(blocks)} blocks")


if __name__ == "__main__":
    args = [a for i, a in enumerate(sys.argv[1:]) if a != "--out" and sys.argv[i] != "--out"]
    if not args:
        fail("which notes? e.g. python3 annotate.py annotated/seqlz_decompress.c.md")
    for a in args:
        build(pathlib.Path(a).resolve())
