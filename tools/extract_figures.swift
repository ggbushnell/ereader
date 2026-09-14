// Extract photographs / figures from a scanned-book PDF and prepare them for a
// 1-bit 920x680 e-ink panel (Good Display GDEH0576T81) used in portrait, so
// the target page is 680 wide by 920 tall.
//
// Usage:
//   swift tools/extract_figures.swift input.pdf outdir [--pages A-B] [--full-res]
//                                      [--min-frac 0.03] [--force-pages 1,162]
//
// --force-pages takes 1-based page numbers whose whole content area (everything
// inside the scan-chrome trim) is emitted as one figure, with no text masking and
// no detection. It is for cover art, which is a full-bleed image that the
// detector has no paper-white reference to work from.
//
// --debug adds the per-page density grid, content area and candidate box stats
// to the stderr progress, which is how you tune the detector on a new book.
//
// Each PDF page is rendered at 3x through PDFKit (same as tools/ocr_pdf.swift),
// Vision supplies text-line boxes (the PDF's own text layer is ignored), and
// whatever ink is left over after masking the text is clustered into figure
// regions. Accepted regions are cropped and box-downsampled, then run through a
// tone pipeline built for 1-bit output at this size: a p2/p98 contrast stretch, a
// large-radius unsharp mask (the step that keeps macro structure legible once the
// halftone is gone), and an adaptive gamma that lifts dark crops only. Finally
// Floyd-Steinberg to 1 bit.
//
// --debug reports two eyes-free quality numbers per figure: the Pearson
// correlation between coarse-grid ink density and source darkness (macro tone
// fidelity) and the spread of that ink density (how much structure is left rather
// than even noise).
//
// Outputs (paths in the manifest are relative to outdir):
//   outdir/figs/pNNN_fK.pbm     binary PBM (P4), rows packed MSB first, 1 = black
//   outdir/review/pNNN_fK.png   the same bitmap as a human-review PNG
//   outdir/manifest.json        array of figure records, in page order
//
// Progress goes to stderr, one line per page.

import Foundation
import PDFKit
import Vision
import AppKit

// ---------------------------------------------------------------- arguments

let argv = CommandLine.arguments

func usage() -> Never {
    FileHandle.standardError.write("""
    usage: swift extract_figures.swift input.pdf outdir [--pages A-B] [--full-res]
                                       [--min-frac 0.03] [--force-pages 1,162] [--debug]

    """.data(using: .utf8)!)
    exit(2)
}

var positional: [String] = []
var pageFrom = 1
var pageTo = Int.max
var fullRes = false
var minFrac = 0.03
var debug = false
var forcePages = Set<Int>()

var ai = 1
while ai < argv.count {
    let a = argv[ai]
    switch a {
    case "--full-res":
        fullRes = true
    case "--debug":
        debug = true
    case "--force-pages":
        ai += 1
        guard ai < argv.count else { usage() }
        for part in argv[ai].split(separator: ",") {
            guard let v = Int(part.trimmingCharacters(in: .whitespaces)) else { usage() }
            forcePages.insert(v)
        }
    case "--pages":
        ai += 1
        guard ai < argv.count else { usage() }
        let parts = argv[ai].split(separator: "-", maxSplits: 1).map(String.init)
        if parts.count == 2, let a0 = Int(parts[0]), let b0 = Int(parts[1]) {
            pageFrom = a0; pageTo = b0
        } else if parts.count == 1, let a0 = Int(parts[0]) {
            pageFrom = a0; pageTo = a0
        } else { usage() }
    case "--min-frac":
        ai += 1
        guard ai < argv.count, let v = Double(argv[ai]) else { usage() }
        minFrac = v
    default:
        if a.hasPrefix("--") { usage() }
        positional.append(a)
    }
    ai += 1
}
guard positional.count >= 2 else { usage() }

let inputPath = positional[0]
let outDir = URL(fileURLWithPath: positional[1], isDirectory: true)

// Page geometry. These mirror tools/pdf2book.py (PANEL_*, CONTENT_*, IMAGE_*),
// which is the single source of truth on the host side, and include/config.h
// on the device. The UI is PORTRAIT: the 920x680 panel is rotated 90 degrees
// by the firmware, so the page is 680 wide by 920 tall. The bottom 24 px are
// the status strip, so art gets the 680x896 content area. Half res output is
// stored at 272x358 and drawn 2x2 on device (MPG1_IMAGE_FLAG_DOUBLE).
let panelW = 680
let panelH = 920
let statusStripH = 24
let contentW = panelW              // 680
let contentH = panelH - statusStripH  // 896
// Reduced (default) box is 2/5 of the content area, mirroring IMAGE_HALF_* in
// tools/pdf2book.py: 1/2 would let a doubled plate fill the page exactly but
// costs too much filesystem for a book with ~50 plates. A doubled 272x358
// plate still covers 544x716 of the 680x896 page.
let reducedW = contentW * 2 / 5    // 272
let reducedH = contentH * 2 / 5    // 358
let targetW = fullRes ? contentW : reducedW
let targetH = fullRes ? contentH : reducedH
let flags = fullRes ? 0 : 1
let reviewScale = fullRes ? 1 : 2

guard let doc = PDFDocument(url: URL(fileURLWithPath: inputPath)) else {
    FileHandle.standardError.write("cannot open PDF: \(inputPath)\n".data(using: .utf8)!)
    exit(1)
}

let fm = FileManager.default
let figsDir = outDir.appendingPathComponent("figs", isDirectory: true)
let reviewDir = outDir.appendingPathComponent("review", isDirectory: true)
try? fm.createDirectory(at: figsDir, withIntermediateDirectories: true)
try? fm.createDirectory(at: reviewDir, withIntermediateDirectories: true)

func err(_ s: String) {
    FileHandle.standardError.write((s + "\n").data(using: .utf8)!)
}

// ---------------------------------------------------------------- helpers

/// 8-bit grayscale pixel buffer, row 0 = top of the page.
struct GrayImage {
    var w: Int
    var h: Int
    var px: [UInt8]
    subscript(x: Int, y: Int) -> UInt8 { px[y * w + x] }
}

func renderGray(_ page: PDFPage, scale: CGFloat) -> GrayImage? {
    let bounds = page.bounds(for: .mediaBox)
    let size = NSSize(width: bounds.width * scale, height: bounds.height * scale)
    let image = page.thumbnail(of: size, for: .mediaBox)
    guard let cg = image.cgImage(forProposedRect: nil, context: nil, hints: nil) else { return nil }
    let w = cg.width, h = cg.height
    guard w > 0, h > 0 else { return nil }
    var buf = [UInt8](repeating: 255, count: w * h)
    let ok: Bool = buf.withUnsafeMutableBytes { raw -> Bool in
        guard let ctx = CGContext(data: raw.baseAddress,
                                  width: w, height: h,
                                  bitsPerComponent: 8, bytesPerRow: w,
                                  space: CGColorSpaceCreateDeviceGray(),
                                  bitmapInfo: CGImageAlphaInfo.none.rawValue) else { return false }
        ctx.setFillColor(CGColor(gray: 1.0, alpha: 1.0))
        ctx.fill(CGRect(x: 0, y: 0, width: w, height: h))
        ctx.interpolationQuality = .high
        // CGBitmapContext memory row 0 is the top row of the rendered image, so
        // this buffer is already top-down.
        ctx.draw(cg, in: CGRect(x: 0, y: 0, width: w, height: h))
        return true
    }
    guard ok else { return nil }
    return GrayImage(w: w, h: h, px: buf)
}

func renderCG(_ page: PDFPage, scale: CGFloat) -> CGImage? {
    let bounds = page.bounds(for: .mediaBox)
    let size = NSSize(width: bounds.width * scale, height: bounds.height * scale)
    let image = page.thumbnail(of: size, for: .mediaBox)
    return image.cgImage(forProposedRect: nil, context: nil, hints: nil)
}

func percentile(_ sorted: [Double], _ p: Double) -> Double {
    if sorted.isEmpty { return 0 }
    let idx = min(sorted.count - 1, max(0, Int(p * Double(sorted.count - 1))))
    return sorted[idx]
}

struct Box {
    var x0: Int, y0: Int, x1: Int, y1: Int  // inclusive cell coordinates
    var w: Int { x1 - x0 + 1 }
    var h: Int { y1 - y0 + 1 }
    func expanded(by g: Int) -> Box { Box(x0: x0 - g, y0: y0 - g, x1: x1 + g, y1: y1 + g) }
    func intersects(_ o: Box) -> Bool {
        !(x1 < o.x0 || o.x1 < x0 || y1 < o.y0 || o.y1 < y0)
    }
    mutating func union(_ o: Box) {
        x0 = min(x0, o.x0); y0 = min(y0, o.y0); x1 = max(x1, o.x1); y1 = max(y1, o.y1)
    }
}

/// Area-average resample of a rectangular region of a gray image.
func cropScaleGray(_ img: GrayImage, rx: Int, ry: Int, rw: Int, rh: Int,
                   outW: Int, outH: Int) -> [UInt8] {
    var out = [UInt8](repeating: 255, count: outW * outH)
    for oy in 0..<outH {
        let sy0 = ry + (oy * rh) / outH
        var sy1 = ry + ((oy + 1) * rh) / outH
        if sy1 <= sy0 { sy1 = sy0 + 1 }
        for ox in 0..<outW {
            let sx0 = rx + (ox * rw) / outW
            var sx1 = rx + ((ox + 1) * rw) / outW
            if sx1 <= sx0 { sx1 = sx0 + 1 }
            var sum = 0, n = 0
            var y = sy0
            while y < sy1 && y < img.h {
                let base = y * img.w
                var x = sx0
                while x < sx1 && x < img.w {
                    sum += Int(img.px[base + x]); n += 1
                    x += 1
                }
                y += 1
            }
            out[oy * outW + ox] = n > 0 ? UInt8(sum / n) : 255
        }
    }
    return out
}

/// Rotate an 8-bit grayscale buffer 90 degrees counterclockwise: the top edge
/// of the source becomes the left edge of the result.
func rotateCCW(_ g: [UInt8], w: Int, h: Int) -> [UInt8] {
    var out = [UInt8](repeating: 0, count: w * h)
    for yp in 0..<w {
        let srcX = w - 1 - yp
        for xp in 0..<h {
            out[yp * h + xp] = g[xp * w + srcX]
        }
    }
    return out
}

/// Stretch contrast so near-paper goes white and the darkest tones go black.
func contrastStretch(_ g: inout [UInt8]) {
    var hist = [Int](repeating: 0, count: 256)
    for v in g { hist[Int(v)] += 1 }
    let total = g.count
    let loCut = Int(Double(total) * 0.02)
    let hiCut = Int(Double(total) * 0.98)
    var lo = 0, hi = 255, acc = 0
    for v in 0..<256 {
        acc += hist[v]
        if acc >= loCut { lo = v; break }
    }
    acc = 0
    for v in 0..<256 {
        acc += hist[v]
        if acc >= hiCut { hi = v; break }
    }
    if hi - lo < 24 { return }
    let span = Double(hi - lo)
    for i in 0..<g.count {
        let v = (Double(Int(g[i]) - lo) / span) * 255.0
        g[i] = UInt8(max(0.0, min(255.0, v)))
    }
}

/// Separable box blur, three passes, which is close enough to a Gaussian for an
/// unsharp mask. Radius is in pixels.
func boxBlur(_ g: [UInt8], w: Int, h: Int, radius: Int) -> [Double] {
    var buf = g.map { Double($0) }
    guard radius >= 1, w > 1, h > 1 else { return buf }
    var tmp = [Double](repeating: 0, count: w * h)
    for _ in 0..<3 {
        // Horizontal pass with a running sum.
        for y in 0..<h {
            let row = y * w
            var sum = 0.0
            for x in 0...min(radius, w - 1) { sum += buf[row + x] }
            var count = Double(min(radius, w - 1) + 1)
            for x in 0..<w {
                tmp[row + x] = sum / count
                let addX = x + radius + 1
                let dropX = x - radius
                if addX < w { sum += buf[row + addX]; count += 1 }
                if dropX >= 0 { sum -= buf[row + dropX]; count -= 1 }
            }
        }
        // Vertical pass.
        for x in 0..<w {
            var sum = 0.0
            for y in 0...min(radius, h - 1) { sum += tmp[y * w + x] }
            var count = Double(min(radius, h - 1) + 1)
            for y in 0..<h {
                buf[y * w + x] = sum / count
                let addY = y + radius + 1
                let dropY = y - radius
                if addY < h { sum += tmp[addY * w + x]; count += 1 }
                if dropY >= 0 { sum -= tmp[dropY * w + x]; count -= 1 }
            }
        }
    }
    return buf
}

/// Unsharp mask with a large radius. This is the step that keeps a halftone
/// photograph legible after it has been squeezed to 400 pixels wide and one bit
/// deep: it restores the macro structure (a figure against mist, a roof against
/// sky) that a global tone curve alone flattens into even noise.
func unsharpMask(_ g: inout [UInt8], w: Int, h: Int, radiusFrac: Double, amount: Double) {
    let radius = max(2, Int((Double(max(w, h)) * radiusFrac).rounded()))
    let blur = boxBlur(g, w: w, h: h, radius: radius)
    for i in 0..<g.count {
        let v = Double(g[i])
        let sharp = v + amount * (v - blur[i])
        g[i] = UInt8(max(0.0, min(255.0, sharp.rounded())))
    }
}

/// Lift a crop that is still too dark to dither legibly, and only then. Gamma is
/// clamped so this never darkens an image and never fully normalizes one: a dark
/// misty photograph should stay darker than a bright one.
func adaptiveGamma(_ g: inout [UInt8], floor: Double, target: Double) {
    guard !g.isEmpty else { return }
    var hist = [Int](repeating: 0, count: 256)
    for v in g { hist[Int(v)] += 1 }
    let n = Double(g.count)
    func meanFor(_ gamma: Double) -> Double {
        var s = 0.0
        for v in 0..<256 where hist[v] != 0 {
            s += Double(hist[v]) * pow(Double(v) / 255.0, gamma) * 255.0
        }
        return s / n
    }
    let mean0 = meanFor(1.0)
    if mean0 >= floor { return }
    // Lower gamma brightens, so the mean decreases monotonically in gamma.
    var lo = 0.55, hi = 1.0
    if meanFor(lo) <= target { for i in 0..<g.count { g[i] = UInt8(max(0.0, min(255.0, (pow(Double(g[i]) / 255.0, lo) * 255.0).rounded()))) }; return }
    for _ in 0..<24 {
        let mid = (lo + hi) / 2
        if meanFor(mid) > target { lo = mid } else { hi = mid }
    }
    let gc = (lo + hi) / 2
    var lut = [UInt8](repeating: 0, count: 256)
    for v in 0..<256 {
        lut[v] = UInt8(max(0.0, min(255.0, (pow(Double(v) / 255.0, gc) * 255.0).rounded())))
    }
    for i in 0..<g.count { g[i] = lut[Int(g[i])] }
}

/// Spread (standard deviation) of coarse-cell ink fractions. Correlation alone
/// does not catch the failure mode that matters here: Floyd-Steinberg always
/// tracks local mean tone, so a crop whose tonal range has been squeezed flat
/// still correlates well while looking like even noise on glass. The spread is
/// what collapses in that case.
func inkSpread(_ bits: [Bool], w: Int, h: Int, cols: Int = 24, rows: Int = 32) -> Double {
    var v = [Double]()
    for r in 0..<rows {
        let y0 = r * h / rows, y1 = max(y0 + 1, (r + 1) * h / rows)
        for c in 0..<cols {
            let x0 = c * w / cols, x1 = max(x0 + 1, (c + 1) * w / cols)
            var ink = 0.0, n = 0.0
            for y in y0..<min(y1, h) {
                for x in x0..<min(x1, w) {
                    if bits[y * w + x] { ink += 1 }
                    n += 1
                }
            }
            if n > 0 { v.append(ink / n) }
        }
    }
    guard v.count > 2 else { return 0 }
    let m = v.reduce(0, +) / Double(v.count)
    var s = 0.0
    for x in v { s += (x - m) * (x - m) }
    return (s / Double(v.count)).squareRoot()
}

/// Pearson correlation between the ink density of the dithered result and the
/// darkness of the source crop, both reduced to a coarse grid. This is the
/// eyes-free check that macro structure survived: a photograph that still looks
/// like itself scores well above 0.7, a crop that dithered to even noise does not.
func structureCorrelation(_ bits: [Bool], _ src: [UInt8], w: Int, h: Int,
                          cols: Int = 24, rows: Int = 32) -> Double {
    var a = [Double](), b = [Double]()
    for r in 0..<rows {
        let y0 = r * h / rows, y1 = max(y0 + 1, (r + 1) * h / rows)
        for c in 0..<cols {
            let x0 = c * w / cols, x1 = max(x0 + 1, (c + 1) * w / cols)
            var ink = 0.0, dark = 0.0, n = 0.0
            for y in y0..<min(y1, h) {
                for x in x0..<min(x1, w) {
                    if bits[y * w + x] { ink += 1 }
                    dark += 255.0 - Double(src[y * w + x])
                    n += 1
                }
            }
            if n == 0 { continue }
            a.append(ink / n); b.append(dark / n)
        }
    }
    let n = Double(a.count)
    guard n > 2 else { return 0 }
    let ma = a.reduce(0, +) / n, mb = b.reduce(0, +) / n
    var num = 0.0, da = 0.0, db = 0.0
    for i in 0..<a.count {
        let x = a[i] - ma, y = b[i] - mb
        num += x * y; da += x * x; db += y * y
    }
    if da <= 0 || db <= 0 { return 0 }
    return num / (da * db).squareRoot()
}

/// Floyd-Steinberg to 1 bit. Returns true = black.
func dither(_ gray: [UInt8], w: Int, h: Int) -> [Bool] {
    var buf = gray.map { Double($0) }
    var out = [Bool](repeating: false, count: w * h)
    for y in 0..<h {
        let leftToRight = (y % 2 == 0)  // serpentine
        let xs: [Int] = leftToRight ? Array(0..<w) : Array((0..<w).reversed())
        for x in xs {
            let i = y * w + x
            let old = buf[i]
            let newV: Double = old < 128 ? 0 : 255
            out[i] = (newV == 0)
            let e = old - newV
            let fwd = leftToRight ? 1 : -1
            func add(_ xx: Int, _ yy: Int, _ f: Double) {
                if xx < 0 || xx >= w || yy < 0 || yy >= h { return }
                buf[yy * w + xx] += e * f
            }
            add(x + fwd, y, 7.0 / 16.0)
            add(x - fwd, y + 1, 3.0 / 16.0)
            add(x, y + 1, 5.0 / 16.0)
            add(x + fwd, y + 1, 1.0 / 16.0)
        }
    }
    return out
}

func writePBM(_ bits: [Bool], w: Int, h: Int, to url: URL) -> Int {
    let rowBytes = (w + 7) / 8
    var data = Data("P4\n\(w) \(h)\n".utf8)
    var rows = [UInt8](repeating: 0, count: rowBytes * h)
    for y in 0..<h {
        for x in 0..<w where bits[y * w + x] {
            rows[y * rowBytes + (x >> 3)] |= UInt8(0x80 >> (x & 7))
        }
    }
    data.append(contentsOf: rows)
    try? data.write(to: url)
    return data.count
}

func writeReviewPNG(_ bits: [Bool], w: Int, h: Int, scale: Int, to url: URL) {
    let ow = w * scale, oh = h * scale
    guard let rep = NSBitmapImageRep(bitmapDataPlanes: nil,
                                     pixelsWide: ow, pixelsHigh: oh,
                                     bitsPerSample: 8, samplesPerPixel: 1,
                                     hasAlpha: false, isPlanar: false,
                                     colorSpaceName: .deviceWhite,
                                     bytesPerRow: ow, bitsPerPixel: 8),
          let dst = rep.bitmapData else { return }
    for oy in 0..<oh {
        let sy = oy / scale
        for ox in 0..<ow {
            let sx = ox / scale
            dst[oy * ow + ox] = bits[sy * w + sx] ? 0 : 255
        }
    }
    if let png = rep.representation(using: .png, properties: [:]) {
        try? png.write(to: url)
    }
}

// ---------------------------------------------------------------- detection

let CELL = 16  // density-grid cell size, in rendered (3x) pixels

struct Figure {
    var page: Int
    var id: String
    var pbm: String
    var png: String
    var w: Int
    var h: Int
    var flags: Int
}

var figures: [Figure] = []
var totalBytes = 0
var perPage: [Int: Int] = [:]

let scale: CGFloat = 3.0
let lastPage = min(doc.pageCount, pageTo)

// The forced pages are honored even when they fall outside --pages.
var pageIndices = Set<Int>()
if lastPage >= pageFrom {
    for pi in (pageFrom - 1)..<lastPage { pageIndices.insert(pi) }
}
for n in forcePages { pageIndices.insert(n - 1) }

for pi in pageIndices.sorted() {
    guard pi >= 0, pi < doc.pageCount, let page = doc.page(at: pi) else { continue }
    let pageNo = pi + 1
    let forced = forcePages.contains(pageNo)

    guard let cg = renderCG(page, scale: scale), let img = renderGray(page, scale: scale) else {
        err("page \(pageNo): render failed, skipped")
        continue
    }
    let W = img.w, H = img.h

    // Vision text lines. The PDF's own text layer is deliberately ignored. Forced
    // pages take the whole content area, so there is nothing to mask off.
    var obs: [VNRecognizedTextObservation] = []
    if !forced {
        let request = VNRecognizeTextRequest()
        request.recognitionLevel = .accurate
        request.usesLanguageCorrection = true
        request.recognitionLanguages = ["en-US"]
        do {
            try VNImageRequestHandler(cgImage: cg, options: [:]).perform([request])
            obs = request.results ?? []
        } catch {
            err("page \(pageNo): OCR failed (\(error)), continuing with no text mask")
        }
    }

    let gw = W / CELL, gh = H / CELL
    guard gw > 4, gh > 4 else { err("page \(pageNo): too small, skipped"); continue }

    // Mean gray and standard deviation per cell. The deviation separates flat
    // scanner shading from real content, which always carries some texture.
    var cellGray = [Double](repeating: 255, count: gw * gh)
    var cellSD = [Double](repeating: 0, count: gw * gh)
    let cellN = Double(CELL * CELL)
    for cy in 0..<gh {
        for cx in 0..<gw {
            var sum = 0.0, sumSq = 0.0
            for y in (cy * CELL)..<(cy * CELL + CELL) {
                let base = y * W + cx * CELL
                for k in 0..<CELL {
                    let v = Double(img.px[base + k])
                    sum += v; sumSq += v * v
                }
            }
            let m = sum / cellN
            cellGray[cy * gw + cx] = m
            cellSD[cy * gw + cx] = max(0.0, sumSq / cellN - m * m).squareRoot()
        }
    }

    // Paper-white level for this page: bright end of the cell distribution.
    let sortedGray = cellGray.sorted()
    let paper = percentile(sortedGray, 0.90)
    let inkCut = paper - 45.0

    // Trim the scan chrome. These scans carry a wide, smooth gutter shadow down
    // one edge (it alternates sides between verso and recto) plus dark bands at
    // the top and bottom. They are large, dark and text-free, so they read as
    // figures, and the gutter runs right up against the content so it also glues
    // every component on the page into one blob. They are flat, though, and they
    // have a recognisable shape, which is what trimRun below keys on.
    let flatSD = 12.0
    // Median rather than mean along the line: a band's own edge cells are noisy
    // and would otherwise drag the average out of range.
    func colMed(_ arr: [Double], _ cx: Int) -> Double {
        var v = [Double](); v.reserveCapacity(gh)
        for cy in 0..<gh { v.append(arr[cy * gw + cx]) }
        return percentile(v.sorted(), 0.5)
    }
    func rowMed(_ arr: [Double], _ cy: Int) -> Double {
        var v = [Double](); v.reserveCapacity(gw)
        for cx in 0..<gw { v.append(arr[cy * gw + cx]) }
        return percentile(v.sorted(), 0.5)
    }
    // Walk inward from an edge to measure the scan chrome. The gutter shadow has
    // a very consistent shape: it darkens from the page edge to a dip, then
    // brightens steadily back toward paper (or up to the first line of content).
    // A photograph that simply runs to the page edge does not do that, so the
    // required rise is what keeps this from eating real pictures.
    func trimRun(_ order: [Int], _ med: (Int) -> Double, _ sdMed: (Int) -> Double,
                 maxTrim: Int) -> Int {
        let n = min(maxTrim, order.count)
        if n < 4 { return 0 }
        var m = [Double](), sv = [Double]()
        for k in 0..<n { m.append(med(order[k])); sv.append(sdMed(order[k])) }
        var dip = 0
        while dip + 1 < n && m[dip + 1] <= m[dip] { dip += 1 }
        if m[dip] > paper - 60 { return 0 }   // no real shadow on this edge
        var k = dip
        // Keep climbing while the line stays flat, or while it is still clearly
        // darker than paper (the sharp black-to-white lip of a top or bottom band
        // is textured, but it is still chrome).
        while k + 1 < n && m[k + 1] >= m[k]
                && (sv[k + 1] < flatSD || m[k + 1] < paper - 40) { k += 1 }
        if m[k] - m[dip] < 30 { return 0 }    // never climbed back out, so it is content
        return k + 1
    }
    let maxTrimX = gw / 4, maxTrimY = gh / 5
    let cols = Array(0..<gw), rows = Array(0..<gh)
    var trimL = trimRun(cols, { colMed(cellGray, $0) }, { colMed(cellSD, $0) }, maxTrim: maxTrimX)
    var trimR = gw - 1 - trimRun(cols.reversed(), { colMed(cellGray, $0) }, { colMed(cellSD, $0) }, maxTrim: maxTrimX)
    var trimT = trimRun(rows, { rowMed(cellGray, $0) }, { rowMed(cellSD, $0) }, maxTrim: maxTrimY)
    var trimB = gh - 1 - trimRun(rows.reversed(), { rowMed(cellGray, $0) }, { rowMed(cellSD, $0) }, maxTrim: maxTrimY)
    // Always drop the outermost ring as well.
    trimL = max(trimL, 1); trimT = max(trimT, 1)
    trimR = min(trimR, gw - 2); trimB = min(trimB, gh - 2)
    if trimL >= trimR || trimT >= trimB { err("page \(pageNo): no usable content area, skipped"); continue }

    // Text mask: any cell touched by a text observation box, expanded a little
    // so ascenders/descenders and tight leading do not leak through.
    var isText = [Bool](repeating: false, count: gw * gh)
    for o in obs {
        let bb = o.boundingBox
        let x0 = bb.minX * Double(W)
        let x1 = bb.maxX * Double(W)
        // Vision origin is bottom-left, the pixel buffer is top-down.
        let yTop = (1.0 - bb.maxY) * Double(H)
        let yBot = (1.0 - bb.minY) * Double(H)
        let lineH = max(8.0, yBot - yTop)
        let mx = max(8.0, lineH * 0.45)
        let my = max(6.0, lineH * 0.45)
        let cx0 = max(0, Int((x0 - mx) / Double(CELL)))
        let cx1 = min(gw - 1, Int((x1 + mx) / Double(CELL)))
        let cy0 = max(0, Int((yTop - my) / Double(CELL)))
        let cy1 = min(gh - 1, Int((yBot + my) / Double(CELL)))
        if cx0 > cx1 || cy0 > cy1 { continue }
        for cy in cy0...cy1 { for cx in cx0...cx1 { isText[cy * gw + cx] = true } }
    }

    // Inky cells: dark, not text, inside the trimmed content area.
    var inky = [Bool](repeating: false, count: gw * gh)
    for cy in trimT...trimB {
        for cx in trimL...trimR {
            let i = cy * gw + cx
            if isText[i] { continue }
            if cellGray[i] < inkCut { inky[i] = true }
        }
    }

    // Connected components (8-connected) over inky cells.
    var seen = [Bool](repeating: false, count: gw * gh)
    var boxes: [Box] = []
    for cy in 0..<gh {
        for cx in 0..<gw {
            let start = cy * gw + cx
            if !inky[start] || seen[start] { continue }
            var stack = [start]
            seen[start] = true
            var b = Box(x0: cx, y0: cy, x1: cx, y1: cy)
            var count = 0
            while let cur = stack.popLast() {
                let ux = cur % gw, uy = cur / gw
                count += 1
                b.union(Box(x0: ux, y0: uy, x1: ux, y1: uy))
                for dy in -1...1 {
                    for dx in -1...1 {
                        if dx == 0 && dy == 0 { continue }
                        let nx = ux + dx, ny = uy + dy
                        if nx < 0 || ny < 0 || nx >= gw || ny >= gh { continue }
                        let ni = ny * gw + nx
                        if inky[ni] && !seen[ni] { seen[ni] = true; stack.append(ni) }
                    }
                }
            }
            // Specks (stray scan grit, the tail of a descender the text mask missed)
            // and hairlines (a rule, or a narrow strip of gutter shadow the edge trim
            // did not reach) are dropped here rather than after merging: left in, they
            // act as stepping stones that chain a photo to the text below it.
            let thin = (b.w <= 4 || b.h <= 4)
            let elongated = Double(max(b.w, b.h)) / Double(max(1, min(b.w, b.h))) > 6.0
            // A sprawling wisp of a component (a gutter hairline that happens to
            // touch a few surviving specks of text ink) has a huge bounding box and
            // almost nothing in it. Merging would let that box swallow a real photo.
            let solid = Double(count) >= 0.25 * Double(b.w * b.h)
            if count >= 8 && !(thin && elongated) && solid { boxes.append(b) }
        }
    }

    // Merge boxes that overlap or all but touch (a photo breaks into pieces where
    // a light sky or a white sill runs through it). The gap is deliberately tight:
    // a plate carrying several photographs should come out as several figures.
    let gap = 0
    var merged = true
    while merged {
        merged = false
        var out: [Box] = []
        for b in boxes {
            var cur = b
            var i = 0
            while i < out.count {
                if cur.expanded(by: gap).intersects(out[i].expanded(by: gap)) {
                    cur.union(out[i])
                    out.remove(at: i)
                    merged = true
                    i = 0
                } else {
                    i += 1
                }
            }
            out.append(cur)
        }
        boxes = out
    }

    // Filters. Photos in this book are large rectangles, so favor precision.
    let pageCells = Double(gw * gh)
    var kept: [Box] = []
    for b in boxes {
        let cells = Double(b.w * b.h)
        if cells / pageCells < minFrac { continue }
        if b.w < 6 || b.h < 6 { continue }
        // A figure in this book is a big rectangle. Anything that is a thin strip
        // relative to the page is a rule, a running head, or leftover scan chrome.
        if Double(b.w) < Double(gw) * 0.12 || Double(b.h) < Double(gh) * 0.12 { continue }
        let ar = Double(max(b.w, b.h)) / Double(max(1, min(b.w, b.h)))
        if ar > 8.0 { continue }
        var inkyCells = 0, textCells = 0, graySum = 0.0
        for cy in b.y0...b.y1 {
            for cx in b.x0...b.x1 {
                let i = cy * gw + cx
                if inky[i] { inkyCells += 1 }
                if isText[i] { textCells += 1 }
                graySum += cellGray[i]
            }
        }
        let fill = Double(inkyCells) / cells
        let textFrac = Double(textCells) / cells
        let meanGray = graySum / cells
        if fill < 0.45 { continue }          // sparse scatter, not a photo
        if textFrac > 0.35 { continue }      // mostly a text block
        if meanGray > paper - 30 { continue } // specks on near-white paper
        kept.append(b)
    }
    if debug {
        let inkyTotal = inky.filter { $0 }.count
        err("  [debug] grid \(gw)x\(gh) paper \(Int(paper)) content \(trimL)..\(trimR) x \(trimT)..\(trimB) inkyCells \(inkyTotal) candidates \(boxes.count)")
        for b in boxes {
            let cells = Double(b.w * b.h)
            var ic = 0, tc = 0; var gs = 0.0
            for cy in b.y0...b.y1 { for cx in b.x0...b.x1 {
                let i = cy * gw + cx
                if inky[i] { ic += 1 }
                if isText[i] { tc += 1 }
                gs += cellGray[i]
            } }
            err(String(format: "  [debug] box %d,%d %dx%d areaFrac %.3f fill %.2f text %.2f mean %.0f",
                       b.x0, b.y0, b.w, b.h, cells / pageCells, Double(ic) / cells,
                       Double(tc) / cells, gs / cells))
        }
    }
    kept.sort { a, b in a.y0 != b.y0 ? a.y0 < b.y0 : a.x0 < b.x0 }

    // A forced page ignores everything above and emits its content area whole.
    if forced { kept = [Box(x0: trimL, y0: trimT, x1: trimR, y1: trimB)] }

    // Crop, scale, dither, write.
    var n = 0
    for b in kept {
        n += 1
        let rx = b.x0 * CELL
        let ry = b.y0 * CELL
        let rw = min(W - rx, b.w * CELL)
        let rh = min(H - ry, b.h * CELL)
        if rw <= 0 || rh <= 0 { continue }

        // A crop whose orientation does not match the page box is rotated 90
        // CCW so it fills the page (reader turns the device a quarter turn).
        // The page is portrait now, so LANDSCAPE crops rotate and portrait
        // crops fit directly; under the old landscape UI it was the reverse.
        // Derived from the target box rather than hard coded, so flipping the
        // panel orientation again needs no edit here.
        let targetPortrait = targetH > targetW
        let cropPortrait = rh > rw
        let rotate = cropPortrait != targetPortrait
        // When rotating, fit the crop against the transposed target box.
        let fit = rotate
            ? min(Double(targetW) / Double(rh), Double(targetH) / Double(rw))
            : min(Double(targetW) / Double(rw), Double(targetH) / Double(rh))
        var ow = max(1, min(rotate ? targetH : targetW, Int((Double(rw) * fit).rounded())))
        var oh = max(1, min(rotate ? targetW : targetH, Int((Double(rh) * fit).rounded())))

        var raw = cropScaleGray(img, rx: rx, ry: ry, rw: rw, rh: rh, outW: ow, outH: oh)
        if rotate {
            raw = rotateCCW(raw, w: ow, h: oh)
            swap(&ow, &oh)
        }
        var gray = raw
        contrastStretch(&gray)                                        // p2 to black, p98 to white
        unsharpMask(&gray, w: ow, h: oh, radiusFrac: 0.03, amount: 0.8)
        adaptiveGamma(&gray, floor: 100, target: 130)                 // lift dark crops only
        let bits = dither(gray, w: ow, h: oh)

        if debug {
            let r = structureCorrelation(bits, raw, w: ow, h: oh)
            var inkN = 0
            for v in bits where v { inkN += 1 }
            err(String(format: "  [debug] fig %d: %dx%d ink %.3f structure r %+.3f spread %.3f",
                       n, ow, oh, Double(inkN) / Double(ow * oh), r,
                       inkSpread(bits, w: ow, h: oh)))
        }
        let id = String(format: "p%03d_f%d", pageNo, n)
        let pbmRel = "figs/\(id).pbm"
        let pngRel = "review/\(id).png"
        let bytes = writePBM(bits, w: ow, h: oh, to: outDir.appendingPathComponent(pbmRel))
        writeReviewPNG(bits, w: ow, h: oh, scale: reviewScale, to: outDir.appendingPathComponent(pngRel))
        totalBytes += bytes
        figures.append(Figure(page: pageNo, id: id, pbm: pbmRel, png: pngRel,
                              w: ow, h: oh, flags: flags))
    }
    perPage[pageNo] = n
    err("page \(pageNo)/\(doc.pageCount): \(n) figure(s)")
}

// ---------------------------------------------------------------- manifest

var json = "[\n"
for (i, f) in figures.enumerated() {
    json += "  {\"page\": \(f.page), \"id\": \"\(f.id)\", \"pbm\": \"\(f.pbm)\", "
    json += "\"png\": \"\(f.png)\", \"w\": \(f.w), \"h\": \(f.h), \"flags\": \(f.flags)}"
    json += (i == figures.count - 1) ? "\n" : ",\n"
}
json += "]\n"
try? json.data(using: .utf8)!.write(to: outDir.appendingPathComponent("manifest.json"))

let pagesWithFigures = perPage.values.filter { $0 > 0 }.count
err("total: \(figures.count) figures on \(pagesWithFigures) pages, \(totalBytes) PBM bytes")
