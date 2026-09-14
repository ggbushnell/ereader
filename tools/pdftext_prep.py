#!/usr/bin/env python3
"""Turn a print-proof PDF into the plain text tools/txt2book.py expects: pages
separated by form feeds, paragraphs separated by blank lines.

Written for INSPIRED (Wiley, LaTeX print proof), whose text layer carries four
things that defeat plain extraction:

  1. A per-page slug line ("Trim Size: 6in x 9in ... Cagan c16.tex ... Page 76")
     and stray lone "k" registration marks, some of them level with body copy.
  2. Running heads: "76   INSPIRED" on versos, "Product Vision ...   125" on
     rectos.
  3. Pull-quote sidebars typeset beside the body text.
  4. Paragraphs marked by first-line indent alone, with no blank line.

The input is `pdftotext -bbox-layout`, not `-layout`. The character-grid dump
is unusable here because on a page like page 26 the body and the sidebar touch
with a single space, leaving no gutter to split on. The bbox dump carries real
x/y for every line, so columns are separated geometrically instead: two lines
that overlap vertically but not horizontally are side by side, and that is what
finds the gutter.

Usage:
    pdftotext -bbox-layout book.pdf book.xml
    python3 tools/pdftext_prep.py book.xml clean.txt [--drop-pages 36,47]
"""

import argparse
import html
import re
import sys

# Furniture is removed geometrically, which is decisive where a text match is
# not: poppler breaks the slug line into several fragments ("3:44pm", "Page 10")
# that no single regex catches, and the running head differs on versos and
# rectos. Measured over the whole book, body lines run y 168 to 712 on the
# 595x842 pt page; the slug sits at y 69, the running head at y 141, and the
# registration marks at y 61 and y 763.
CONTENT_TOP = 160.0
CONTENT_BOTTOM = 740.0
# Layout thresholds, in PDF points on this book's 595x842 pt page.
INDENT_PT = 6.0           # extra first-line indent that starts a paragraph
GAP_FACTOR = 1.6          # line gap over this many leadings starts a paragraph
Y_OVERLAP = 0.35          # fraction of line height that counts as side by side
# A list item is set with a hanging indent: the marker line sits left of the
# lines that continue it, which is the opposite of a paragraph's first-line
# indent, so indent alone would start a new paragraph on every wrapped line.
LIST_MARKER = re.compile(r"^([\u2022*\-\u2013]|\d{1,2}\.)\s")
# Where a body line and a sidebar line happen to share a baseline, poppler
# emits them as one <line>. Such a line is cut only at a gap this wide that
# also straddles the gutter the rest of the page agrees on, so a stretched word
# space in a narrow justified column is never mistaken for a column break.
SPLIT_GAP_PT = 8.0
# A real column break is at least this wide. Chapter openers set a drop cap
# beside the first two lines with about 2 pt of clearance, which without this
# floor reads as a two-column page and throws the opening paragraphs to the
# bottom of the page as a "sidebar".
MIN_GUTTER_PT = 12.0

BLOCK_RE = re.compile(r"<block ([^>]*)>(.*?)</block>", re.S)
LINE_RE = re.compile(r"<line ([^>]*)>(.*?)</line>", re.S)
WORD_RE = re.compile(r"<word ([^>]*)>(.*?)</word>", re.S)
PAGE_RE = re.compile(r"<page ([^>]*)>(.*?)</page>", re.S)
ATTR_RE = re.compile(r'(\w+)="([^"]*)"')


class Line(object):
    __slots__ = ("x0", "x1", "y0", "y1", "text", "words")

    def __init__(self, x0, x1, y0, y1, text, words=None):
        self.x0, self.x1, self.y0, self.y1, self.text = x0, x1, y0, y1, text
        self.words = words or [(x0, x1, text)]

    @property
    def height(self):
        return self.y1 - self.y0


def parse_pages(path):
    """Return a list of pages, each a list of Line, in document order."""
    with open(path, encoding="utf-8") as fh:
        doc = fh.read()

    pages = []
    for _attrs, page_body in PAGE_RE.findall(doc):
        lines = []
        for _battrs, block_body in BLOCK_RE.findall(page_body):
            for lattrs, line_body in LINE_RE.findall(block_body):
                a = dict(ATTR_RE.findall(lattrs))
                y0, y1 = float(a["yMin"]), float(a["yMax"])
                words = []
                for wattrs, word in WORD_RE.findall(line_body):
                    wa = dict(ATTR_RE.findall(wattrs))
                    text = html.unescape(word)
                    if not text.strip():
                        continue
                    words.append((float(wa["xMin"]), float(wa["xMax"]), text))
                if not words:
                    continue
                lines.append(Line(words[0][0], words[-1][1], y0, y1,
                                  " ".join(t for _a, _b, t in words), words))
        lines.sort(key=lambda l: (l.y0, l.x0))
        pages.append(lines)
    return pages


def split_at_gutter(line, gutter):
    """Cut a line in two if a wide word gap straddles the page's gutter.

    Only called once the gutter is known from lines that are unambiguously side
    by side, so an ordinary stretched word space in justified text is never
    mistaken for a column break.
    """
    for i in range(1, len(line.words)):
        prev_x1 = line.words[i - 1][1]
        next_x0 = line.words[i][0]
        if next_x0 - prev_x1 <= SPLIT_GAP_PT:
            continue
        if prev_x1 < gutter < next_x0:
            left = line.words[:i]
            right = line.words[i:]
            return [Line(left[0][0], left[-1][1], line.y0, line.y1,
                         " ".join(t for _a, _b, t in left), left),
                    Line(right[0][0], right[-1][1], line.y0, line.y1,
                         " ".join(t for _a, _b, t in right), right)]
    return [line]


def is_mark(text):
    """True for a line whose only ink is stray 'k' registration marks."""
    return bool(text.strip()) and set(text.split()) <= {"k"}


def strip_furniture(lines):
    """Keep only the lines inside the content band, minus registration marks."""
    return [l for l in lines
            if l.y0 >= CONTENT_TOP and l.y1 <= CONTENT_BOTTOM
            and not is_mark(l.text)]


def side_by_side(a, b):
    """True if two lines sit on the same band of the page, in two columns."""
    overlap = min(a.y1, b.y1) - max(a.y0, b.y0)
    if overlap < Y_OVERLAP * min(a.height, b.height):
        return False
    return (b.x0 - a.x1 >= MIN_GUTTER_PT) or (a.x0 - b.x1 >= MIN_GUTTER_PT)


def split_sidebar(lines):
    """Return (body, sidebar) after lifting a pull quote out of the body text.

    The sidebar is whatever sits to the right of the gutter, where the gutter is
    found from the pairs of lines that share a band of the page. Returns an
    empty sidebar when the page is single column.
    """
    lefts, rights = [], []
    for i, a in enumerate(lines):
        for b in lines[i + 1:]:
            if not side_by_side(a, b):
                continue
            left, right = (a, b) if a.x0 < b.x0 else (b, a)
            lefts.append(left)
            rights.append(right)
    if not rights:
        return lines, []

    gutter = (max(l.x1 for l in lefts) + min(r.x0 for r in rights)) / 2.0

    # Now that the gutter is known, cut the lines poppler merged across it.
    cut = []
    for l in lines:
        cut.extend(split_at_gutter(l, gutter))
    cut.sort(key=lambda l: (l.y0, l.x0))
    rights = [l for l in cut if l.x0 >= gutter]
    if not rights:
        return cut, []
    top = min(r.y0 for r in rights)
    bottom = max(r.y1 for r in rights)

    body, sidebar = [], []
    for l in cut:
        if l.x0 >= gutter and top - 1 <= l.y0 and l.y1 <= bottom + 1:
            sidebar.append(l)
        else:
            body.append(l)
    return body, sidebar


def merge_drop_caps(lines):
    """Fold a chapter opener's drop cap into the line it belongs to.

    The cap is set as its own tall one-letter line, immediately left of the
    first line of text, which would otherwise read as a stray paragraph and
    leave the opening word beheaded ("eople are always searching...").
    """
    if len(lines) < 2:
        return lines
    typical = median([l.height for l in lines])
    out = []
    skip = False
    for i, l in enumerate(lines):
        if skip:
            skip = False
            continue
        nxt = lines[i + 1] if i + 1 < len(lines) else None
        if (nxt is not None and len(l.text.strip()) == 1 and l.text.strip().isalpha()
                and typical and l.height > 1.5 * typical
                and 0 <= nxt.x0 - l.x1 < MIN_GUTTER_PT
                and min(l.y1, nxt.y1) - max(l.y0, nxt.y0) > 0):
            out.append(Line(l.x0, nxt.x1, nxt.y0, nxt.y1,
                            l.text.strip() + nxt.text, nxt.words))
            # The lines beside the cap are indented to clear it. Pull them back
            # to the cap's own left edge, which is the paragraph's true margin,
            # so the indent test does not read them as fresh paragraphs.
            for later in lines[i + 2:]:
                if later.y0 >= l.y1 - 1:
                    break
                later.x0 = l.x0
            skip = True
        else:
            out.append(l)
    return out


def dehyphenate(pieces):
    out = ""
    for piece in pieces:
        piece = piece.strip()
        if not piece:
            continue
        if not out:
            out = piece
        elif re.search(r"[A-Za-z]-$", out) and piece[:1].islower():
            out = out[:-1] + piece
        else:
            out += " " + piece
    return re.sub(r"\s{2,}", " ", out).strip()


def paragraphs(lines, single=False):
    """Group lines into paragraphs by first-line indent and vertical gaps.

    `single` joins everything into one paragraph, which is what a pull quote
    wants: it is centred, so its indent carries no paragraph information.
    """
    if not lines:
        return []
    if single:
        joined = dehyphenate([l.text for l in lines])
        return [joined] if joined else []

    leading = median([lines[i + 1].y0 - lines[i].y0 for i in range(len(lines) - 1)])

    flags = list_flags(lines)
    # A list item's own lines are hanging-indented, so they would drag the
    # margin right and hide the real paragraph indent. Measure without them.
    left = body_margin([l for l, f in zip(lines, flags) if not f] or lines)

    paras, current, prev = [], [], None
    for l, in_list in zip(lines, flags):
        marker = bool(LIST_MARKER.match(l.text))
        starts = False
        if not current:
            starts = False
        elif marker:
            starts = True                      # every list item is a paragraph
        elif leading and (l.y0 - prev.y0) > GAP_FACTOR * leading:
            # Tested before the list rule: extra leading ends a list, which is
            # how a heading that follows one keeps from being swallowed.
            starts = True
        elif in_list:
            starts = False                     # a wrapped line of that item
        elif l.x0 > left + INDENT_PT:
            starts = True
        if starts:
            paras.append(dehyphenate(current))
            current = []
        current.append(l.text)
        prev = l
    if current:
        paras.append(dehyphenate(current))
    return [p for p in paras if p]


def list_flags(lines):
    """Mark each line that belongs to a list item, marker line included."""
    flags = []
    item_x = None
    for l in lines:
        if LIST_MARKER.match(l.text):
            item_x = l.x0
            flags.append(True)
        elif item_x is not None and l.x0 > item_x + 1.0:
            flags.append(True)
        else:
            item_x = None
            flags.append(False)
    return flags


def body_margin(lines):
    """The left margin of running text: the most common line start.

    The minimum would be wrong wherever a page opens midway through a list
    item, where every line of the run is indented and none of them starts a
    paragraph, and wherever a bullet hangs left of the text it introduces.
    """
    counts = {}
    for l in lines:
        key = round(l.x0)
        counts[key] = counts.get(key, 0) + 1
    best = max(counts.items(), key=lambda kv: (kv[1], -kv[0]))
    return float(best[0])


def median(values):
    values = sorted(v for v in values if v > 0)
    if not values:
        return 0.0
    return values[len(values) // 2]


def process(lines):
    lines = merge_drop_caps(strip_furniture(lines))
    body, sidebar = split_sidebar(lines)
    return paragraphs(body) + paragraphs(sidebar, single=True)


def main(argv=None):
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("src", help="pdftotext -bbox-layout output")
    ap.add_argument("dst", help="cleaned text file to write")
    ap.add_argument("--drop-boxes", default="",
                    help="regions to cut, as PAGE:yMin-yMax in PDF points, comma "
                         "separated (e.g. 36:163-322,47:160-295). Use it on the "
                         "band a figure occupies, whose labels would otherwise "
                         "land in the text as loose words.")
    args = ap.parse_args(argv)

    drop = {}
    for spec in args.drop_boxes.split(","):
        spec = spec.strip()
        if not spec:
            continue
        page, _colon, span = spec.partition(":")
        y0, _dash, y1 = span.partition("-")
        drop.setdefault(int(page), []).append((float(y0), float(y1)))
    pages = parse_pages(args.src)

    out = []
    for index, lines in enumerate(pages, 1):
        for y0, y1 in drop.get(index, []):
            lines = [l for l in lines if not (l.y0 >= y0 and l.y1 <= y1)]
        out.append("\n\n".join(process(lines)))

    with open(args.dst, "w", encoding="utf-8") as fh:
        fh.write("\f".join(out))

    kept = sum(1 for p in out if p.strip())
    sys.stderr.write("%d pages in, %d with text, %d chars out\n"
                     % (len(pages), kept, sum(len(p) for p in out)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
