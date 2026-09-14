#pragma once

// On-device text paginator: the C++ twin of the host pipeline in
// tools/pdf2book.py (normalize_text, pages_to_paragraphs, wrap_paragraph_spans,
// paginate_spans) as tools/txt2book.py drives it for a plain text input with no
// form feeds and no figures.
//
// The two implementations have to agree exactly or a book paginated on the
// device would render differently from one paginated on the host. See
// tools/paginate_test.cpp and tools/check_paginator.py: that test compiles this
// file natively and diffs its pages against the Python pipeline character for
// character.
//
// Deliberately free of Arduino, FreeRTOS and LittleFS so it can be built with a
// plain g++ on the host. Everything is streaming: bytes go in a chunk at a
// time, finished pages come out through a callback, and nothing larger than one
// paragraph is ever held in memory.
//
// Character model. Input is UTF-8. Normalization transliterates the codepoints
// the host converter knows about, keeps ASCII plus the Latin-1 supplement, and
// drops everything else, exactly like pdf2book.normalize_text. The result is
// held as one byte per character (Latin-1), which is what makes column counting
// match Python's, where a column is a codepoint and not a byte. Page blobs are
// encoded back to UTF-8 on the way out, so the bytes in the .pgs file are the
// same bytes the host writer would have produced.

#include <stddef.h>
#include <stdint.h>

#include <functional>
#include <string>
#include <vector>

namespace textpage {

// A finished page: at most `rows` lines, each at most `cols` characters, in the
// Latin-1 character model described above. Use encodeUtf8() before writing.
using PageSink = std::function<void(const std::vector<std::string> &lines)>;

// A paragraph longer than this is flushed early rather than buffered without
// limit. Real prose does not come close; this only fires on pathological input
// (a whole file with no blank line in it), and when it does the on-device
// pagination stops matching the host for that book. Reported by truncated().
static const size_t PARAGRAPH_MAX_CHARS = 32768;

// Latin-1 characters to UTF-8 bytes, the encoding the page blobs use on disk.
std::string encodeUtf8(const std::string &latin1);

class Pipeline {
 public:
  Pipeline(int cols, int rows, PageSink sink);

  // Feed raw UTF-8 input. Safe to call with a partial multi-byte sequence at
  // the end of a chunk; the decoder carries state across calls.
  void feed(const uint8_t *data, size_t len);

  // Flushes the last paragraph and the last page. Emits one empty page if the
  // input held no text at all, which is what the host writer does. Calling it
  // twice is harmless.
  void finish();

  uint32_t pageCount() const { return pages_; }
  bool truncated() const { return truncated_; }

  // Anchor of the page currently being handed to the sink: the cumulative
  // character count into the book's logical text stream at the start of that
  // page, which is what an MPG2 page table stores. Only meaningful from inside
  // the sink callback. The stream is the cleaned paragraphs concatenated with
  // one separator character each, so the same word carries the same anchor on
  // every grid. Mirrors pdf2book.paginate_spans with base 0.
  uint32_t pageAnchor() const { return emitAnchor_; }

 private:
  void emitCodepoint(uint32_t cp);  // one decoded codepoint, normalized
  void pushChar(char c);        // a normalized character or '\n'
  void endLine();               // current line is complete
  void flushParagraph();        // paragraph buffer is complete
  void addParagraph(const std::string &para);
  void emitPage();

  int cols_;
  int rows_;
  PageSink sink_;

  // UTF-8 decoder state.
  uint32_t cp_ = 0;
  uint32_t min_ = 0;  // smallest codepoint this length may encode (overlong check)
  int need_ = 0;

  std::string line_;
  std::string para_;
  std::vector<std::string> current_;
  bool firstParagraph_ = true;
  bool finished_ = false;
  bool truncated_ = false;
  uint32_t pages_ = 0;

  // Anchor bookkeeping, mirroring paginate_spans' para_base / current_anchor.
  uint32_t paraBase_ = 0;
  uint32_t currentAnchor_ = 0;
  uint32_t emitAnchor_ = 0;
};

// Exposed for the host parity test.
namespace detail {

// One wrapped line plus the offset of its first character inside the
// paragraph. Two grids wrapping the same paragraph produce different lines but
// the same offsets for the same words, which is what makes anchors comparable
// across variants.
// Aggregate on purpose (no default member initializers), so it can be built
// with brace initialization under the C++11 the Arduino core compiles with.
struct LineSpan {
  std::string text;
  size_t offset;
};

bool isPageNumber(const std::string &stripped);
std::string strip(const std::string &s);
std::string dehyphenate(const std::string &s);
std::vector<LineSpan> wrapParagraphSpans(const std::string &para, int cols);
std::vector<std::string> wrapParagraph(const std::string &para, int cols);
}  // namespace detail

}  // namespace textpage
