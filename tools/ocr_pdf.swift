// OCR an image-only PDF using the macOS Vision framework (Live Text engine).
// Usage: swift tools/ocr_pdf.swift input.pdf > out.txt
// Emits recognized text to stdout with a form feed (\f) between PDF pages,
// which is the page separator tools/txt2book.py expects. Progress on stderr.

import Foundation
import PDFKit
import Vision
import AppKit

var args = CommandLine.arguments
// --no-spread: never split a page into two columns. Use for single-page scans,
// it guarantees output form-feed chunks map 1:1 to PDF pages (needed when a
// figure manifest keyed by PDF page number is spliced in downstream).
let noSpread = args.contains("--no-spread")
args.removeAll { $0 == "--no-spread" }
guard args.count >= 2 else {
    FileHandle.standardError.write("usage: swift ocr_pdf.swift [--no-spread] input.pdf\n".data(using: .utf8)!)
    exit(2)
}
guard let doc = PDFDocument(url: URL(fileURLWithPath: args[1])) else {
    FileHandle.standardError.write("cannot open PDF: \(args[1])\n".data(using: .utf8)!)
    exit(1)
}

let pageCount = doc.pageCount
for i in 0..<pageCount {
    guard let page = doc.page(at: i) else { continue }
    let bounds = page.bounds(for: .mediaBox)
    // Render at 3x (roughly 216 dpi for a 72 dpi mediaBox). Enough for clean OCR
    // without huge bitmaps.
    let scale: CGFloat = 3.0
    let size = NSSize(width: bounds.width * scale, height: bounds.height * scale)
    let image = page.thumbnail(of: size, for: .mediaBox)
    guard let cg = image.cgImage(forProposedRect: nil, context: nil, hints: nil) else {
        FileHandle.standardError.write("page \(i + 1): render failed, skipped\n".data(using: .utf8)!)
        continue
    }

    let request = VNRecognizeTextRequest()
    request.recognitionLevel = .accurate
    request.usesLanguageCorrection = true
    request.recognitionLanguages = ["en-US"]

    let handler = VNImageRequestHandler(cgImage: cg, options: [:])
    do {
        try handler.perform([request])
    } catch {
        FileHandle.standardError.write("page \(i + 1): OCR failed (\(error)), skipped\n".data(using: .utf8)!)
        continue
    }

    // Vision bounding boxes are normalized with origin at bottom-left.
    let results = request.results ?? []

    // Book scans are often two-page spreads. If almost no line crosses the
    // vertical center, treat left and right halves as separate pages,
    // otherwise read the whole image as one column.
    func readingOrder(_ obs: [VNRecognizedTextObservation]) -> [String] {
        let sorted = obs.sorted { a, b in
            let ay = a.boundingBox.midY, by = b.boundingBox.midY
            if abs(ay - by) > 0.01 { return ay > by }
            return a.boundingBox.minX < b.boundingBox.minX
        }
        return sorted.compactMap { $0.topCandidates(1).first?.string }
    }

    let crossing = results.filter { $0.boundingBox.minX < 0.45 && $0.boundingBox.maxX > 0.55 }.count
    let isSpread = !noSpread && results.count >= 6 && crossing * 10 < results.count

    var pageTexts: [String] = []
    if isSpread {
        let left = results.filter { $0.boundingBox.midX < 0.5 }
        let right = results.filter { $0.boundingBox.midX >= 0.5 }
        pageTexts = [readingOrder(left).joined(separator: "\n"),
                     readingOrder(right).joined(separator: "\n")]
    } else {
        pageTexts = [readingOrder(results).joined(separator: "\n")]
    }
    print(pageTexts.filter { !$0.isEmpty }.joined(separator: "\n\u{0C}\n"))
    if i < pageCount - 1 { print("\u{0C}") }
    FileHandle.standardError.write("page \(i + 1)/\(pageCount)\n".data(using: .utf8)!)
}
