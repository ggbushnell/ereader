#!/usr/bin/env python3
"""Diff the on-device paginator against the host pipeline.

The firmware paginates a downloaded news brief itself (src/text_paginate.cpp).
That C++ has to produce exactly what tools/txt2book.py would have produced for
the same plain text, or a book made on the device would not match one made on
the host. This script is how the two are kept in sync: it compiles the C++ with
the system compiler, runs both over the same input, and diffs the pages line by
line.

Usage:
    python3 tools/check_paginator.py                 built-in sample
    python3 tools/check_paginator.py some_brief.txt  a file of your own
    python3 tools/check_paginator.py --cols 55 --rows 20

Exit status is 0 when every page of every grid matches.
"""

import argparse
import os
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pdf2book as p


ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "src")
TOOLS = os.path.join(ROOT, "tools")

# Exercises the transliteration table, the page number filter, dehyphenation,
# paragraph joining, space collapsing, hard word breaks and blank line runs.
SAMPLE = (
    "The Muon Bench News Brief\n"
    "\n"
    "12\n"
    "\n"
    "Curly \u201cquotes\u201d and \u2018singles\u2019 and a long dash \u2014 plus an\n"
    "ellipsis… and a non breaking space and a soft­hyphen.\n"
    "\n"
    "A paragraph that is hard wrapped by the sender with a hyphen-\n"
    "ated word split across the line break, which the cleaner has to\n"
    "put back together again before it wraps to the reading grid.\n"
    "\n"
    "iv\n"
    "\n"
    "Ligatures: oﬀice, ﬁnd, ﬂow, and a fraction slash ⁄ too.\n"
    "Bullets • become stars and ‹guillemets› become angle brackets.\n"
    "\n"
    "Supercalifragilisticexpialidociousandthensomemoreletterspastthecolumnlimitto"
    "forceahardbreakintheword right here.\n"
    "\n"
    "  Leading and trailing spaces   with   internal   runs   collapse.  \n"
    "\n"
    "\n"
    "\n"
    "A final short paragraph.\n"
) + ("Filler sentence number %d to push the text past a single page boundary. "
     % 0) + " ".join(
    "Filler sentence number %d to push the text past a single page boundary."
    % i for i in range(1, 60)) + "\n"


def host_pages(text, cols, rows):
    """Run the host pipeline exactly as txt2book.py does for a plain text."""
    raw_pages = text.split("\f")
    paragraphs = p.pages_to_paragraphs(raw_pages)
    if not paragraphs:
        return [[""]]
    pages, _consumed = p.paginate_spans(paragraphs, cols, rows, 0)
    return [lines for lines, _anchor in pages]


def device_pages(binary, data, cols, rows):
    proc = subprocess.run([binary, str(cols), str(rows)], input=data,
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if proc.returncode != 0:
        sys.stderr.write(proc.stderr.decode("utf-8", "replace"))
        raise SystemExit("paginate_test failed")
    pages = []
    current = None
    for line in proc.stdout.decode("utf-8").split("\n")[:-1]:
        if line.startswith("=== page "):
            if current is not None:
                pages.append(current)
            current = []
        else:
            current.append(line)
    if current is not None:
        pages.append(current)
    return pages


def build(out_path):
    cmd = ["c++", "-std=c++11", "-O1", "-Wall", "-I", SRC,
           os.path.join(TOOLS, "paginate_test.cpp"),
           os.path.join(SRC, "text_paginate.cpp"),
           "-o", out_path]
    print("building: %s" % " ".join(cmd))
    subprocess.run(cmd, check=True)


def compare(host, device, cols, rows):
    ok = True
    if len(host) != len(device):
        print("  page count differs: host %d, device %d"
              % (len(host), len(device)))
        ok = False
    for i in range(min(len(host), len(device))):
        if host[i] != device[i]:
            ok = False
            print("  page %d differs" % i)
            for j in range(max(len(host[i]), len(device[i]))):
                h = host[i][j] if j < len(host[i]) else "<missing>"
                d = device[i][j] if j < len(device[i]) else "<missing>"
                if h != d:
                    print("    line %d host   : %r" % (j, h))
                    print("    line %d device : %r" % (j, d))
    return ok


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("txt", nargs="?", default=None,
                    help="input text file (default: a built-in sample)")
    ap.add_argument("--cols", type=int, default=None)
    ap.add_argument("--rows", type=int, default=None)
    args = ap.parse_args(argv)

    if args.txt:
        with open(args.txt, "r", encoding="utf-8", errors="replace") as fh:
            text = fh.read()
    else:
        text = SAMPLE
    data = text.encode("utf-8")

    if args.cols and args.rows:
        grids = [(args.cols, args.rows)]
    else:
        grids = [p.GRIDS[0], p.GRIDS[1]]

    with tempfile.TemporaryDirectory() as tmp:
        binary = os.path.join(tmp, "paginate_test")
        build(binary)
        all_ok = True
        for cols, rows in grids:
            host = host_pages(text, cols, rows)
            device = device_pages(binary, data, cols, rows)
            print("grid %dx%d: host %d pages, device %d pages"
                  % (cols, rows, len(host), len(device)))
            if compare(host, device, cols, rows):
                print("  match")
            else:
                all_ok = False

    if not all_ok:
        print("MISMATCH: the paginators have drifted apart")
        return 1
    print("all grids match")
    return 0


if __name__ == "__main__":
    sys.exit(main())
