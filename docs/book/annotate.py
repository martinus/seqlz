r"""The annotated source: one file of src/ as it is at the book's commit, with a short note next to each
block of it, links to the sections of the book that explain those lines, and the book's figures under
the blocks they belong to.

build.py makes one page per notes file, annotated/<file>.md, right after the book, from what it built:
the file's highlighted lines, the listings of it, the page's ids and the figures. So the notes show
the code exactly as the book does, and need no compiler of their own.

A notes file:

  # src/seqlz_decompress.c
  markdown: how to read the file, shown above the code

  ## Copies
      a section of the file, in the map at the top

  @@ 7-30
  markdown: the note on lines 7 to 30
  @anim overlap <label>
  @stepper decode <label>
      a figure of the book with the label of its button; it opens under the block

The blocks have to cover the file, in order, without gaps. `book:#<id>` in a link is a link to that
id of the book; an id the book does not have fails the build, as does a line range past the file.
"""
import bisect
import html
import pathlib
import re
import sys

HERE = pathlib.Path(__file__).resolve().parent
HEAD = re.compile(r"#\s+(\S+)\s*$")
BLOCK = re.compile(r"@@\s+(\d+)-(\d+)\s*$")


def fail(msg):
    sys.exit(f"annotate: {msg}")


def notes_files(here=HERE):
    """the notes there are: the file of src/ each one is about, and its notes"""
    out = {}
    for f in sorted((here / "annotated").glob("*.md")):
        m = HEAD.match(f.read_text().split("\n", 1)[0])
        if m:
            out[m.group(1)] = f
    return out


def only_comment(h):
    """a highlighted line with nothing but a comment, or blank: folded away while comments are hidden"""
    return not re.sub(r'<span class="cm">.*?</span>', "", h).strip()


def parse_notes(path):
    """the intro, and the sections, each a title and its blocks"""
    lines = path.read_text().split("\n")
    intro, sections = [], []
    cur = intro
    for k, line in enumerate(lines[1:], 2):
        if line.startswith("## "):
            sections.append({"title": line[3:].strip(), "blocks": []})
            cur = None
            continue
        m = BLOCK.match(line)
        if m:
            if not sections:
                fail(f"{path}:{k}: a block before the first '## section'")
            block = {"a": int(m.group(1)), "b": int(m.group(2)), "text": [], "figs": [], "at": k}
            sections[-1]["blocks"].append(block)
            cur = block["text"]
            continue
        m = re.match(r"@(anim|stepper)\s+([\w-]+)\s+(\S.*)$", line)
        if m:
            if cur is intro or cur is None:
                fail(f"{path}:{k}: a figure outside a block")
            sections[-1]["blocks"][-1]["figs"].append((f"{m.group(1)}-{m.group(2)}", m.group(1), m.group(3).strip()))
            continue
        if line.startswith("@"):
            fail(f"{path}:{k}: '@anim <name> <label>', '@stepper <name> <label>' or '@@ <from>-<to>'")
        if cur is None:
            if line.strip():
                fail(f"{path}:{k}: text between a section title and its first block")
            continue
        cur.append(line)
    return "\n".join(intro).strip(), sections


def render(path, src, lines, listings, body, ids, figures, cores, md, commit):
    """the page of the notes at path: a fragment like the book's page, with {{BOOK}} where the address
    of the book goes. lines: the file's highlighted lines; listings: (id, first, last) of each listing
    of the file, in the book's order; body: the book's chapters as HTML, ids: its ids; figures: id ->
    (the key in cores of what it needs on the page before it, its markup); md: markdown to HTML"""
    intro, sections = parse_notes(path)
    blocks = [b for s in sections for b in s["blocks"]]
    nxt = 1
    for b in blocks:
        if b["a"] != nxt or b["b"] < b["a"]:
            fail(f"{path}:{b['at']}: block {b['a']}-{b['b']}, expected one from line {nxt}")
        nxt = b["b"] + 1
    if nxt != len(lines) + 1:
        fail(f"{path}: the blocks end at line {nxt - 1}, {src} has {len(lines)}")

    def links(h):
        def one(m):
            if m.group(1) not in ids:
                fail(f"{path}: link to #{m.group(1)}, which the book does not have")
            return f'href="{{{{BOOK}}}}#{m.group(1)}"'
        return re.sub(r'href="book:#([^"]+)"', one, h)

    # the section and the chapter of each listing: the last heading with an id and the last chapter
    # label before it
    heads = [(m.start(), m.group(1), re.sub(r"<[^>]+>", "", m.group(2)).strip())
             for m in re.finditer(r'<h[123] id="([^"]+)"[^>]*>(.*?)</h[123]>', body, re.S)]
    chapnos = [(m.start(), m.group(1)) for m in re.finditer(r'<span class="chapno">([^<]*)</span>', body)]
    refs_of = []
    for lid, a, b in listings:
        at = body.index(f'id="{lid}"')
        _, hid, title = heads[bisect.bisect(heads, (at,)) - 1]
        refs_of.append((a, b, hid, title, chapnos[bisect.bisect(chapnos, (at,)) - 1][1], lid))

    used, sec_html, cells = [], [], []
    for si, s in enumerate(sections):
        sid = "s-" + re.sub(r"[^\w]+", "-", s["title"].lower()).strip("-")
        a0, b0 = s["blocks"][0]["a"], s["blocks"][-1]["b"]
        cells.append(f'<a class="cell" href="#{sid}" style="flex-grow:{b0 - a0 + 1}" data-sec="{sid}">'
                     f'<span class="ct">{html.escape(s["title"])}</span><span class="cr">{a0}&ndash;{b0}</span></a>')
        arts = []
        for b in s["blocks"]:
            code = "".join(f'<span class="{"line cmline" if only_comment(lines[n - 1]) else "line"}" id="L{n}">'
                           f'<a class="ln" href="#L{n}" data-n="{n}" aria-label="line {n}"></a>'
                           f'<span class="lc">{lines[n - 1]}</span></span>' for n in range(b["a"], b["b"] + 1))
            refs, seen = [], set()
            for a, z, hid, title, chap, lid in refs_of:
                if z >= b["a"] and a <= b["b"] and hid not in seen:
                    seen.add(hid)
                    refs.append(f'<li><a href="{{{{BOOK}}}}#{lid}"><span class="rc">{html.escape(chap)}</span> '
                                f'{html.escape(title)}</a></li>')
            buttons = []
            for fid, kind, label in b["figs"]:
                if fid not in figures:
                    fail(f"{path}:{b['at']}: the book has no {fid}")
                if fid in used:
                    fail(f"{path}:{b['at']}: {fid} twice; a figure can be on the page once")
                used.append(fid)
                what = "Step through" if kind == "stepper" else "Animation"
                buttons.append(f'<button type="button" class="figbtn" data-fig="{fid}" aria-expanded="false">'
                               f'<svg viewBox="0 0 24 24" aria-hidden="true"><path d="M8 5l11 7-11 7z"/></svg>'
                               f'<span><span class="fk">{what}</span> {html.escape(label)}</span></button>')
            rng = f"line {b['a']}" if b["a"] == b["b"] else f"lines {b['a']}&ndash;{b['b']}"
            arts.append(
                f'<article class="blk" id="b{b["a"]}" data-sec="{sid}">'
                f'<div class="bcode"><pre class="code numbered"><code>{code}</code></pre></div>'
                f'<aside class="bnote"><div class="ninner"><p class="nrange"><a href="#b{b["a"]}">{rng}</a></p>'
                + links(md("\n".join(b["text"]).strip()))
                + (f'<div class="figs">{"".join(buttons)}</div>' if buttons else "")
                + (f'<p class="rhead">In the book</p><ul class="refs">{"".join(refs)}</ul>' if refs else "")
                + '</div></aside>'
                + ('<div class="bfig" hidden></div>' if buttons else "")
                + '</article>')
        sec_html.append(f'<section class="sec" id="{sid}"><h2 class="sech"><span class="secno">{si + 1}</span>'
                        f'{html.escape(s["title"])}</h2>{"".join(arts)}</section>')

    m = re.match(r"(.*?)<style>\n\{\{KATEX_CSS\}\}\n(.*?)</style>", (HERE / "template.html").read_text(), re.S)
    if not m:
        fail("template.html does not start with its fonts and <style> {{KATEX_CSS}}, the book's CSS")
    fonts, css = m.groups()
    keys = list(dict.fromkeys(figures[f][0] for f in used))
    name = pathlib.Path(src).name
    return ((HERE / "annotate.html").read_text()
            .replace("{{FONTS}}", "\n".join(line for line in fonts.split("\n") if line.startswith("<link")))
            .replace("{{BOOK_CSS}}", css)
            .replace("{{ASSETS}}", "".join(cores[k] for k in keys))
            .replace("{{NAME}}", html.escape(name))
            .replace("{{SRC}}", html.escape(src))
            .replace("{{LINES}}", str(len(lines)))
            .replace("{{BLOCKS}}", str(len(blocks)))
            .replace("{{COMMIT}}", commit[:7])
            .replace("{{GITHUB}}", f"https://github.com/martinus/seqlz/blob/{commit}/{src}")
            .replace("{{MAP}}", "".join(cells))
            .replace("{{INTRO}}", links(md(intro)))
            .replace("{{BODY}}", "".join(sec_html))
            .replace("{{TEMPLATES}}", "".join(f'<template id="t-{f}">{figures[f][1]}</template>' for f in used)))
