r"""The code of Part II: one file of src/ as it is at the book's commit, as a chapter of the book, with a
short note next to each block of it. Under a note, buttons open what does not fit next to the code,
under the block and as wide as it: a figure of the book, or a longer explanation.

build.py makes the chapter of each notes file, annotated/<file>.md, after the other chapters, from
what it built: the file's highlighted lines, the listings of it and the chapters' ids. So the notes
show the code exactly as the listings do, and need no compiler of their own.

A notes file:

  # src/seqlz_decompress.c
  markdown: how to read the file, shown above the code

  ## Copies
      a section of the file, in the map at the top

  @@ 7-30
  markdown: the note on lines 7 to 30
  @anim overlap <label>
  @stepper decode <label>
      a figure of the book with the label of its button
  @more {#id} <title>
  markdown with what a chapter has but listings: boxes, tables, formulas, figures, {{facts}},
  @program, @output, @assert
  @end
      a longer explanation, its title on its button; {#id} is optional
  @anchor <id>
      one more id at the block, e.g. the id of a chapter that its blocks replaced

@more, @anim and @stepper in the intro, before the first section, are about the whole file: their
buttons are under the intro.

The blocks have to cover the file, in order, without gaps. A link `#b<line>` goes to that block of the
same file, `<file>.html` and `<file>.html#b<line>` to another file of src/ and its block, `book:#<id>`
or `#<id>` to an id of the book; build.py fails the build on a link to an id the book does not have.
"""
import html
import pathlib
import re
import sys

HERE = pathlib.Path(__file__).resolve().parent
HEAD = re.compile(r"#\s+(\S+)\s*$")
BLOCK = re.compile(r"@@\s+(\d+)-(\d+)\s*$")
MORE = re.compile(r"@more\s+(?:\{#([\w-]+)\}\s+)?(\S.*)$")


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


def link_name(src):
    """the name of a file of src/ in links between notes, <name>.html: seqlz_decompress.c, and
    linux-seqlz.h for src/linux/seqlz.h"""
    return src.removeprefix("src/").replace("/", "-")


def file_id(src):
    """the id of a file's chapter, for a file of src/ or its link name: f-seqlz_decompress-c,
    f-linux-seqlz-h; its blocks, lines and sections start with it"""
    return "f-" + link_name(src).replace(".", "-")


def section_id(fid, title):
    return f"{fid}--" + re.sub(r"[^\w]+", "-", title.lower()).strip("-")


def only_comment(h):
    """a highlighted line with nothing but a comment, or blank: folded away while comments are hidden"""
    return not re.sub(r'<span class="cm">.*?</span>', "", h).strip()


def parse_notes(path, text=None):
    """the intro, the sections, each a title and its blocks, and the figures and panels of the intro.
    text: the notes, if not path's"""
    lines = (path.read_text() if text is None else text).split("\n")
    intro, sections, top = [], [], []
    cur, more = intro, None
    for k, line in enumerate(lines[1:], 2):
        if more is not None:
            if line.strip() == "@end":
                more = None
            else:
                more["text"].append(line)
            continue
        if line.startswith("## "):
            sections.append({"title": line[3:].strip(), "blocks": []})
            cur = None
            continue
        m = BLOCK.match(line)
        if m:
            if not sections:
                fail(f"{path}:{k}: a block before the first '## section'")
            block = {"a": int(m.group(1)), "b": int(m.group(2)), "text": [], "extra": [], "anchors": [], "at": k}
            sections[-1]["blocks"].append(block)
            cur = block["text"]
            continue
        if line.startswith("@") and cur is None:
            fail(f"{path}:{k}: {line.split()[0]} outside a block")
        extra = top if cur is intro else sections[-1]["blocks"][-1]["extra"] if cur is not None else None
        m = re.match(r"@(anim|stepper)\s+([\w-]+)\s+(\S.*)$", line)
        if m:
            extra.append({"kind": m.group(1), "name": m.group(2), "title": m.group(3).strip(), "at": k})
            continue
        m = MORE.match(line)
        if m:
            more = {"kind": "more", "id": m.group(1), "title": m.group(2).strip(), "text": [], "at": k}
            extra.append(more)
            continue
        m = re.match(r"@anchor\s+([\w-]+)\s*$", line)
        if m and cur is not intro:
            sections[-1]["blocks"][-1]["anchors"].append(m.group(1))
            continue
        if line.startswith("@"):
            fail(f"{path}:{k}: '@anim <name> <label>', '@stepper <name> <label>', '@more [{{#id}}] <title>' "
                 f"... '@end', '@anchor <id>' or '@@ <from>-<to>'")
        if cur is None:
            if line.strip():
                fail(f"{path}:{k}: text between a section title and its first block")
            continue
        cur.append(line)
    if more is not None:
        fail(f"{path}:{more['at']}: @more without its @end")
    return "\n".join(intro).strip(), sections, top


def render(path, text, src, lines, refs, rich, fig, files, label, near=(None, None)):
    """the chapter of the notes at path, text after the facts are in. lines: the file's highlighted
    lines; refs: (first, last, href, chapter, title) of the chapters' listings of the file; rich(text,
    first line): markdown with the chapters' directives to HTML; fig(kind, name, where): a figure's
    markup; files: the names of the files with notes; label: the chapter's label; near: the files
    before and after it in the reading order, or None"""
    intro, sections, top = parse_notes(path, text)
    blocks = [b for s in sections for b in s["blocks"]]
    nxt = 1
    for b in blocks:
        if b["a"] != nxt or b["b"] < b["a"]:
            fail(f"{path}:{b['at']}: block {b['a']}-{b['b']}, expected one from line {nxt}")
        nxt = b["b"] + 1
    if nxt != len(lines) + 1:
        fail(f"{path}: the blocks end at line {nxt - 1}, {src} has {len(lines)}")
    fid = file_id(src)
    starts = {b["a"] for b in blocks}

    def links(h):
        def one(m):
            target = m.group(1)
            fm = re.fullmatch(r"([\w.-]+)\.html(?:#b(\d+))?", target)
            if fm and fm.group(1) in files:
                return f'href="#{file_id(fm.group(1))}' + (f'-b{fm.group(2)}"' if fm.group(2) else '"')
            bm = re.fullmatch(r"#b(\d+)", target)
            if bm:
                if int(bm.group(1)) not in starts:
                    fail(f"{path}: link to #b{bm.group(1)}, where no block starts")
                return f'href="#{fid}-b{bm.group(1)}"'
            if target.startswith("book:#"):
                return f'href="{target[5:]}"'
            return m.group(0)
        return re.sub(r'href="([^"]+)"', one, h)

    used = []

    def extras(items, owner):
        """the buttons and the panels of a block's or the intro's figures and longer explanations"""
        buttons, panels = [], []
        for x in items:
            where = f"{path}:{x['at']}"
            if x["kind"] == "more":
                pid = x["id"] or f"{owner}-" + re.sub(r"[^\w]+", "-", x["title"].lower()).strip("-")[:48].strip("-")
                body = rich("\n".join(x["text"]), str(path), x["at"] + 1)
                what = ""
            else:
                if (x["kind"], x["name"]) in used:
                    fail(f"{where}: {x['kind']} {x['name']} twice in this file")
                used.append((x["kind"], x["name"]))
                body = fig(x["kind"], x["name"], where)
                pid = re.search(r'<figure[^>]* id="([^"]+)"', body).group(1) + "-p"
                what = "Step through" if x["kind"] == "stepper" else "Animation"
            buttons.append(f'<button type="button" class="pbtn pbtn-{x["kind"]}" aria-expanded="false" aria-controls="{pid}">'
                           f'<svg viewBox="0 0 24 24" aria-hidden="true"><path d="M9 6l7 6-7 6"/></svg>'
                           f'<span>' + (f'<span class="fk">{what}</span> ' if what else "")
                           + f'{md_inline(x["title"])}</span></button>')
            panels.append(f'<div class="bpanel bpanel-{x["kind"]}" id="{pid}" hidden="until-found">'
                          f'<div class="phead"><p class="ptitle">' + (f'<span class="fk">{what}</span> ' if what else "")
                          + f'{md_inline(x["title"])}</p><button type="button" class="icon pclose" aria-label="Close" title="Close">'
                          f'<svg viewBox="0 0 24 24" aria-hidden="true"><path d="M6 6l12 12M18 6L6 18"/></svg></button></div>'
                          f'<div class="pbody">{links(body)}</div></div>')
        return buttons, panels

    sec_html, cells = [], []
    for si, s in enumerate(sections):
        sid = section_id(fid, s["title"])
        a0, b0 = s["blocks"][0]["a"], s["blocks"][-1]["b"]
        cells.append(f'<a class="cell" href="#{sid}" style="flex-grow:{b0 - a0 + 1}" data-sec="{sid}">'
                     f'<span class="ct">{html.escape(s["title"])}</span><span class="cr">{a0}&ndash;{b0}</span></a>')
        arts = []
        for b in s["blocks"]:
            bid = f'{fid}-b{b["a"]}'
            code = "".join(f'<span class="{"line cmline" if only_comment(lines[n - 1]) else "line"}" id="{fid}-L{n}">'
                           f'<a class="ln" href="#{fid}-L{n}" data-n="{n}" aria-label="line {n}"></a>'
                           f'<span class="lc">{lines[n - 1]}</span></span>' for n in range(b["a"], b["b"] + 1))
            seen, ref_html = set(), []
            for a, z, href, chap, title in refs:
                if z >= b["a"] and a <= b["b"] and (chap, title) not in seen:
                    seen.add((chap, title))
                    ref_html.append(f'<li><a href="{href}"><span class="rc">{html.escape(chap)}</span> '
                                    f'{html.escape(title)}</a></li>')
            buttons, panels = extras(b["extra"], bid)
            rng = f"line {b['a']}" if b["a"] == b["b"] else f"lines {b['a']}&ndash;{b['b']}"
            arts.append(
                f'<article class="blk" id="{bid}" data-sec="{sid}">'
                + "".join(f'<span class="anchor" id="{a}"></span>' for a in b["anchors"])
                + f'<div class="bcode"><pre class="code numbered"><code>{code}</code></pre></div>'
                f'<aside class="bnote"><div class="ninner"><p class="nrange"><a href="#{bid}">{rng}</a></p>'
                + links(rich("\n".join(b["text"]).strip(), str(path), b["at"] + 1))
                + (f'<div class="pbtns">{"".join(buttons)}</div>' if buttons else "")
                + (f'<p class="rhead">In the chapters</p><ul class="refs">{"".join(ref_html)}</ul>' if ref_html else "")
                + '</div></aside>'
                + "".join(panels)
                + '</article>')
        sec_html.append(f'<section class="sec"><h2 class="sech" id="{sid}"><span class="secno">{si + 1}</span>'
                        f'{html.escape(s["title"])}</h2>{"".join(arts)}</section>')

    top_buttons, top_panels = extras(top, fid)
    nav = "".join(f'<a class="{cls}" href="#{file_id(f)}"><span class="fnk">{what}</span>'
                  f'<code>{html.escape(f.removeprefix("src/"))}</code></a>'
                  for f, cls, what in ((near[0], "fprev", "previous file"), (near[1], "fnext", "next file")) if f)
    name = src.removeprefix("src/")
    return (f'<section class="chapter codefile" data-chapter="{fid}">'
            f'<header class="chapterhead"><span class="chapno">{html.escape(label)}</span>'
            f'<h1 id="{fid}"><code>{html.escape(name)}</code></h1>'
            f'<p class="fmeta"><span>{len(lines)} lines in {len(blocks)} blocks</span>'
            f'<span>Keys: <kbd>j</kbd> <kbd>k</kbd> next and previous block, <kbd>c</kbd> comments</span>'
            f'<button type="button" class="cmbtn" aria-pressed="false" title="Hide the lines that are only a comment (c)">'
            f'hide comments</button></p></header>'
            f'<nav class="fmap" aria-label="Sections of {html.escape(name)}">{"".join(cells)}</nav>'
            f'<div class="fintro">{links(rich(intro, str(path), 2))}</div>'
            + (f'<div class="ftop"><div class="pbtns">{"".join(top_buttons)}</div>{"".join(top_panels)}</div>'
               if top_buttons else "")
            + f'{"".join(sec_html)}'
            f'<nav class="filenav" aria-label="Files of Part II">{nav}</nav></section>')


def md_inline(s):
    """a title: code spans, nothing else"""
    return re.sub(r"`([^`]+)`", lambda m: f"<code>{m.group(1)}</code>", html.escape(s, quote=False))
