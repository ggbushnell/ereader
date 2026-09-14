#!/usr/bin/env python3
"""Cut named regions out of a text-based PDF and prepare them as e-reader image
pages: a binary PBM plus a review PNG plus the manifest tools/txt2book.py reads.

tools/extract_figures.swift is the tool for a scanned book: it masks text with
Vision and clusters whatever ink is left. That machinery is wasted on a PDF
whose figures are vector line art, where the regions are known and few. Here
you name the regions and get a deterministic crop.

Usage:
    python3 tools/figures_from_pdf.py book.pdf outdir 36:163-322 47:160-295

Each region is PAGE:yMin-yMax in PDF points, matching the --drop-boxes spec
given to tools/pdftext_prep.py so the figure's own labels leave the text.

Outputs, with manifest paths relative to outdir:
    outdir/figs/pNNN_f1.pbm      binary PBM (P4), 1 = black
    outdir/review/pNNN_f1.png    the same bitmap to look at
    outdir/manifest.json         one record per figure, in page order

Delete a review PNG to veto that figure; txt2book.py leaves it out.
"""

import argparse
import json
import os
import subprocess
import sys
import tempfile

from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pdf2book as p

RENDER_DPI = 204                 # 2x the panel's ~102 dpi page scale
PT_PER_INCH = 72.0
TRIM_THRESHOLD = 245             # anything lighter than this is page white
PAD_PX = 6                       # white kept around the trimmed figure


def render_page(pdf, page, workdir):
    """Render one page to greyscale PNG at RENDER_DPI. Returns an Image."""
    stem = os.path.join(workdir, "page")
    subprocess.check_call([
        "pdftoppm", "-f", str(page), "-l", str(page), "-r", str(RENDER_DPI),
        "-gray", "-png", pdf, stem])
    for name in sorted(os.listdir(workdir)):
        if name.startswith("page") and name.endswith(".png"):
            return Image.open(os.path.join(workdir, name)).convert("L")
    raise RuntimeError("pdftoppm produced nothing for page %d" % page)


def trim(img):
    """Crop away the surrounding page white, then put a small margin back."""
    mask = img.point(lambda v: 0 if v < TRIM_THRESHOLD else 255)
    box = mask.point(lambda v: 255 - v).getbbox()
    if box is None:
        return img
    x0, y0, x1, y1 = box
    return img.crop((max(0, x0 - PAD_PX), max(0, y0 - PAD_PX),
                     min(img.width, x1 + PAD_PX), min(img.height, y1 + PAD_PX)))


def to_panel(img):
    """Scale to fit the panel content area and reduce to 1 bit.

    Line art and type survive a threshold better than a dither at this size: a
    dither breaks thin strokes into dotted lines, while the artwork here has no
    continuous tone for a dither to buy anything on.
    """
    scale = min(p.IMAGE_FULL_W / float(img.width),
                p.IMAGE_FULL_H / float(img.height), 1.0)
    size = (max(1, int(img.width * scale)), max(1, int(img.height * scale)))
    small = img.resize(size, Image.LANCZOS)
    return small.point(lambda v: 255 if v > 176 else 0).convert("1")


def write_pbm(path, img):
    """Write a binary PBM (P4): rows top to bottom, MSB first, bit 1 = black."""
    width, height = img.size
    stride = (width + 7) // 8
    pixels = img.load()
    raster = bytearray(stride * height)
    for y in range(height):
        base = y * stride
        for x in range(width):
            if pixels[x, y] == 0:            # mode "1": 0 is black
                raster[base + (x >> 3)] |= 0x80 >> (x & 7)
    with open(path, "wb") as fh:
        fh.write(b"P4\n%d %d\n" % (width, height))
        fh.write(bytes(raster))


def main(argv=None):
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("pdf")
    ap.add_argument("outdir")
    ap.add_argument("regions", nargs="+", metavar="PAGE:yMin-yMax")
    args = ap.parse_args(argv)

    figs_dir = os.path.join(args.outdir, "figs")
    review_dir = os.path.join(args.outdir, "review")
    for d in (figs_dir, review_dir):
        if not os.path.isdir(d):
            os.makedirs(d)

    scale = RENDER_DPI / PT_PER_INCH
    manifest = []
    counters = {}
    for spec in args.regions:
        page_str, _colon, span = spec.partition(":")
        y0_str, _dash, y1_str = span.partition("-")
        page = int(page_str)
        y0, y1 = float(y0_str), float(y1_str)

        workdir = tempfile.mkdtemp(prefix="figpage")
        try:
            full = render_page(args.pdf, page, workdir)
        finally:
            for name in os.listdir(workdir):
                os.remove(os.path.join(workdir, name))
            os.rmdir(workdir)

        crop = full.crop((0, int(y0 * scale), full.width, int(y1 * scale)))
        bitmap = to_panel(trim(crop))

        counters[page] = counters.get(page, 0) + 1
        fig_id = "p%03d_f%d" % (page, counters[page])
        pbm_rel = os.path.join("figs", fig_id + ".pbm")
        png_rel = os.path.join("review", fig_id + ".png")
        write_pbm(os.path.join(args.outdir, pbm_rel), bitmap)
        bitmap.save(os.path.join(args.outdir, png_rel))

        manifest.append({
            "id": fig_id, "page": page, "pbm": pbm_rel, "png": png_rel,
            "w": bitmap.width, "h": bitmap.height, "flags": 0,
        })
        print("%s  page %d  %dx%d" % (fig_id, page, bitmap.width, bitmap.height))

    with open(os.path.join(args.outdir, "manifest.json"), "w", encoding="utf-8") as fh:
        json.dump(manifest, fh, indent=2)
    return 0


if __name__ == "__main__":
    sys.exit(main())
