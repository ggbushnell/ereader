#!/usr/bin/env python3
"""Restore paragraph and verse breaks in OCR text using the PDF's own layout.

tools/ocr_pdf.swift gives the best character accuracy on a scanned book, but
Vision returns lines with no geometry, so every page arrives as one
undifferentiated run of lines. tools/txt2book.py then joins the whole page into
a single paragraph: readable, but a book of indented block quotes and four line
poems loses its shape.

Poppler's `pdftotext -bbox-layout` keeps the real x position of every line. Its
OCR text (the scanner's own layer) is worse than Vision's, so it is used only as
a structure oracle: this script reads each line's left edge, decides where
paragraphs start and which lines are verse, then transfers those breaks onto the
Vision lines by fuzzy matching the first few words of each line.

Working in points rather than the -layout character grid matters here. That grid
rounds a 12 pt first line indent and a 2 pt block quote inset to the same three
spaces, so block quotes came out as verse.

Output is the Vision text with a blank line before every paragraph start, which
is exactly the paragraph separator tools/pdf2book.pages_to_paragraphs expects.
Verse lines are emitted as one paragraph each, so their line breaks survive
pagination.

Usage:
    python3 tools/ocr_paragraphs.py vision.txt book.pdf out.txt
    python3 tools/ocr_paragraphs.py vision.txt book.xml out.txt   (bbox-layout
                                                                   run already)

The Vision text is form feed separated and must have the same page count and
page order as the PDF, which is what ocr_pdf.swift --no-spread guarantees.
"""

import difflib
import os
import re
import subprocess
import sys
import tempfile

# All measurements are PDF points, relative to the page's modal left margin
# (the body text margin). A first line indent in this book is about 12 pt; a
# verse line sits about 45 pt in.
INDENT_MIN = 6
# A line this far right, and short, is verse: it stands on its own line rather
# than being joined into the paragraph around it. The width test is what keeps
# a deeply inset block quote out.
VERSE_MIN = 36
VERSE_MAX_FRAC = 0.62
# Fuzzy match floor when locating a layout line's opening words in the Vision
# text. Below this the break is dropped rather than guessed. A candidate at or
# above MATCH_GOOD is taken as soon as it is seen: the two texts run in the same
# order, so the earliest strong match is the right one even when a later line
# repeats the same opening words.
MATCH_MIN = 0.72
MATCH_GOOD = 0.86

WORD_RE = re.compile(r"\S+")
NORM_RE = re.compile(r"[^a-z0-9]")


def norm(text):
    return NORM_RE.sub("", text.lower())


def bbox_xml_for(path):
    """Return the -bbox-layout XML, running pdftotext if given a PDF."""
    if not path.lower().endswith(".pdf"):
        with open(path, encoding="utf-8", errors="replace") as fh:
            return fh.read()
    handle, tmp = tempfile.mkstemp(suffix=".xml")
    os.close(handle)
    subprocess.check_call(["pdftotext", "-bbox-layout", path, tmp])
    with open(tmp, encoding="utf-8", errors="replace") as fh:
        xml = fh.read()
    os.unlink(tmp)
    return xml


PAGE_RE = re.compile(r"<page\b.*?</page>", re.S)
LINE_RE = re.compile(r'<line xMin="([\d.]+)"[^>]*xMax="([\d.]+)"[^>]*>(.*?)</line>', re.S)
WORD_TEXT_RE = re.compile(r"<word[^>]*>(.*?)</word>", re.S)

XML_ENTITIES = {"&amp;": "&", "&lt;": "<", "&gt;": ">", "&quot;": '"', "&apos;": "'"}


def unescape(text):
    for entity, char in XML_ENTITIES.items():
        text = text.replace(entity, char)
    return text


def parse_pages(xml):
    """Return one list of (xMin, xMax, text) line tuples per page."""
    pages = []
    for page in PAGE_RE.findall(xml):
        lines = []
        for x_min, x_max, body in LINE_RE.findall(page):
            words = [unescape(w).strip() for w in WORD_TEXT_RE.findall(body)]
            text = " ".join(w for w in words if w)
            if text:
                lines.append((float(x_min), float(x_max), text))
        pages.append(lines)
    return pages


def modal_left(lines):
    """The left margin the body text is set at, in points.

    Full measure lines only: a short last line of a paragraph sits at the body
    margin too, but an indented one would pull a histogram of every line
    towards the indent.
    """
    if not lines:
        return 0.0
    width = max(x_max - x_min for x_min, x_max, _ in lines)
    counts = {}
    for x_min, x_max, _ in lines:
        if x_max - x_min < 0.8 * width:
            continue
        bucket = round(x_min)
        counts[bucket] = counts.get(bucket, 0) + 1
    if not counts:
        return min(x_min for x_min, _, _ in lines)
    return float(max(counts.items(), key=lambda kv: (kv[1], -kv[0]))[0])


# A set-off block (a quotation, a letter) is inset to about the same depth as a
# first line indent, so depth alone cannot tell the two apart. What separates
# them is that block lines come in runs: this many consecutive indented lines
# are read as a block, whose own left edge then becomes the baseline for
# paragraph starts inside it.
BLOCK_RUN = 3
# How far past the block's own right edge a line may reach and still count as
# part of it, in points.
BLOCK_RIGHT_SLACK = 6

# An interview turn is set with a hanging indent, so its opening line sits at
# the body margin and geometry cannot see it. The speaker tag can.
SPEAKER_RE = re.compile(r"^(?:Q|[A-Z][A-Za-z'-]{0,14}):\s")


def page_breaks(lines):
    """Classify one page's lines.

    Returns a list of (kind, text), kind being "para" (starts a paragraph),
    "cont" (continues one) or "verse" (stands on its own line).
    """
    if not lines:
        return []
    left = modal_left(lines)
    width = max(x_max - x_min for x_min, x_max, _ in lines)

    kinds = []
    for x_min, x_max, _ in lines:
        rel = x_min - left
        short = (x_max - x_min) < VERSE_MAX_FRAC * width
        if rel >= VERSE_MIN and short:
            kinds.append("verse")
        elif rel >= INDENT_MIN:
            kinds.append("indent")
        else:
            kinds.append("cont")

    # Resolve runs of indented lines: a lone one is an ordinary paragraph
    # start, a long one is a set-off block measured against its own margin.
    index = 0
    while index < len(kinds):
        if kinds[index] != "indent":
            index += 1
            continue
        # A block's lines share a measure. The body paragraph that follows the
        # block is indented too, so without the right edge test it would be
        # swallowed as the block's last line.
        end = index
        while (end < len(kinds) and kinds[end] == "indent"
               and lines[end][1] <= lines[index][1] + BLOCK_RIGHT_SLACK):
            end += 1
        if end - index < BLOCK_RUN:
            for position in range(index, end):
                kinds[position] = "para"
        else:
            block_left = min(lines[position][0] for position in range(index, end))
            for position in range(index, end):
                kinds[position] = ("para"
                                   if position == index
                                   or lines[position][0] - block_left >= INDENT_MIN
                                   else "cont")
            # Body text resumes after a set-off block, and its first line is a
            # new paragraph whatever the print does with the indent.
            if end < len(kinds) and kinds[end] == "cont":
                kinds[end] = "para"
        index = end

    out = []
    for position, (kind, line) in enumerate(zip(kinds, lines)):
        # The line after a verse run resumes ordinary text, so it opens a
        # paragraph even where the print has no indent there.
        if kind == "cont" and position and kinds[position - 1] == "verse":
            kind = "para"
        if kind == "cont" and SPEAKER_RE.match(line[2]):
            kind = "para"
        out.append((kind, line[2]))
    return out


KEY_WORDS = 4


def key_of(text, words=KEY_WORDS):
    """Normalized opening words, kept separate so a candidate that starts one
    word early scores badly instead of matching on contained text."""
    return "|".join(norm(w) for w in text.split()[:words] if norm(w))


def transfer(vision_page, structure):
    """Insert blank lines into a Vision page at the layout's paragraph starts."""
    words = [(m.start(), m.group()) for m in WORD_RE.finditer(vision_page)]
    if not words:
        return vision_page.strip("\n")
    norms = [norm(w) for _, w in words]
    # Vision keeps the printed line breaks, and a paragraph always starts a
    # line, so only line-opening words are candidate break points. That alone
    # rules out a break landing inside a line.
    line_starts = set()
    for index, (offset, _) in enumerate(words):
        if offset == 0 or vision_page[:offset].rstrip(" \t").endswith("\n"):
            line_starts.add(index)

    breaks = set()
    pointer = 0
    for position, (kind, text) in enumerate(structure):
        if kind == "cont":
            continue
        key = key_of(text)
        if len(key) < 6:
            continue
        best_score, best_index = 0.0, None
        for start in range(pointer, len(words)):
            if start not in line_starts:
                continue
            candidate = "|".join(n for n in norms[start:start + KEY_WORDS] if n)
            score = difflib.SequenceMatcher(None, key, candidate).ratio()
            if score > best_score:
                best_score, best_index = score, start
            if score >= MATCH_GOOD:
                break
        if best_index is None or best_score < MATCH_MIN:
            continue
        # Step past the words this line consumed so the next line cannot match
        # inside it.
        pointer = best_index + max(1, len(text.split()) - 1)
        if position:                      # the first line of a page needs no break
            breaks.add(words[best_index][0])

    pieces = []
    cursor = 0
    for offset, _ in words:
        if offset in breaks and offset > cursor:
            pieces.append(vision_page[cursor:offset].rstrip())
            pieces.append("\n\n")
            cursor = offset
    pieces.append(vision_page[cursor:])
    return "".join(pieces).strip("\n")


def speaker_breaks(page):
    """Open a paragraph at every speaker tag in the Vision text.

    The interviews are the reason this runs on the Vision side rather than the
    layout oracle: the scanner's own OCR turns "Jen:" into "fen:", "]en:" and
    "~en:", so only the Vision text can be trusted to spot a turn.
    """
    out = []
    for index, line in enumerate(page.split("\n")):
        if (index and SPEAKER_RE.match(line.strip())
                and out and out[-1].strip()):
            out.append("")
        out.append(line)
    return "\n".join(out)


def main(argv):
    if len(argv) != 4:
        sys.stderr.write(__doc__)
        return 2
    vision_path, layout_source, out_path = argv[1:]

    with open(vision_path, encoding="utf-8", errors="replace") as fh:
        vision_pages = fh.read().split("\f")
    layout_pages = parse_pages(bbox_xml_for(layout_source))
    # ocr_pdf.swift emits no trailing form feed; trim only a surplus blank tail,
    # since a genuinely textless page (a plate) must stay for the two sides to
    # remain in step.
    while len(vision_pages) > len(layout_pages) and not vision_pages[-1].strip():
        vision_pages.pop()
    if len(vision_pages) != len(layout_pages):
        sys.stderr.write("page count mismatch: vision %d, layout %d\n"
                         % (len(vision_pages), len(layout_pages)))
        return 1

    out_pages = []
    total_breaks = 0
    verse_lines = 0
    for vision, layout in zip(vision_pages, layout_pages):
        structure = page_breaks(layout)
        verse_lines += sum(1 for kind, _ in structure if kind == "verse")
        page = speaker_breaks(transfer(vision, structure))
        total_breaks += page.count("\n\n")
        out_pages.append(page)

    with open(out_path, "w", encoding="utf-8") as fh:
        # A newline either side of the form feed keeps the file line-greppable;
        # txt2book splits on the form feed first, so the blank lines are inert.
        fh.write("\n\f\n".join(out_pages))
    sys.stderr.write("%d pages, %d paragraph breaks, %d verse lines -> %s\n"
                     % (len(out_pages), total_breaks, verse_lines, out_path))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
