#include "books.h"

#include <LittleFS.h>

#include "config.h"

namespace {

const uint32_t MAX_PAGE_COUNT = 100000;
const uint16_t MAX_TITLE_LEN = 512;

bool readU8(fs::File &f, uint8_t &out) {
  return f.read(&out, 1) == 1;
}

bool readU32(fs::File &f, uint32_t &out) {
  uint8_t b[4];
  if (f.read(b, 4) != 4) return false;
  out = (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) |
        ((uint32_t)b[3] << 24);
  return true;
}

bool readU16(fs::File &f, uint16_t &out) {
  uint8_t b[2];
  if (f.read(b, 2) != 2) return false;
  out = (uint16_t)((uint16_t)b[0] | ((uint16_t)b[1] << 8));
  return true;
}

uint32_t decodeU32(const uint8_t *b) {
  return (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) |
         ((uint32_t)b[3] << 24);
}

String bookPath(const String &slug) {
  return String(BOOKS_DIR "/") + slug + ".pgs";
}

String posPath(const String &slug) {
  return String(BOOKS_DIR "/") + slug + ".pos";
}

// Fallback title for a book whose header carries none. The synced news brief
// writes its own title (see src/news_sync.cpp), so this only matters for a
// news.pgs left behind by an interrupted sync, but the mapping belongs here
// with the rest of the filename to title derivation.
String defaultTitleForSlug(const String &slug) {
  if (slug == NEWS_SLUG) return String(NEWS_TITLE);
  return slug;
}

}  // namespace

bool Book::open(const String &slug) {
  close();
  slug_ = slug;

  fs::File f = LittleFS.open(bookPath(slug).c_str(), "r");
  if (!f) {
    error_ = "Cannot open book file";
    return false;
  }
  fileSize_ = (uint32_t)f.size();

  char magic[5] = {0};
  if (f.read((uint8_t *)magic, 4) != 4) {
    error_ = "Truncated header";
    f.close();
    return false;
  }

  bool ok = false;
  if (strcmp(magic, "MPG1") == 0) {
    ok = parseMpg1(f);
  } else if (strcmp(magic, "MPG2") == 0) {
    ok = parseMpg2(f);
  } else {
    error_ = "Bad magic (not an MPG1 or MPG2 file)";
  }
  f.close();

  if (!ok) {
    variants_.clear();
    return false;
  }

  if (!title_.length()) title_ = defaultTitleForSlug(slug_);
  activeVariant_ = 0;
  open_ = true;
  error_ = "";
  return true;
}

// MPG1: u32 page_count, u16 title_len, title, u32 offsets[page_count]. Page p
// runs from offsets[p] to offsets[p+1], or to EOF for the last page.
bool Book::parseMpg1(fs::File &f) {
  uint32_t pages = 0;
  if (!readU32(f, pages) || pages == 0 || pages > MAX_PAGE_COUNT) {
    error_ = "Bad page count";
    return false;
  }

  uint16_t titleLen = 0;
  if (!readU16(f, titleLen) || titleLen > MAX_TITLE_LEN) {
    error_ = "Bad title length";
    return false;
  }
  if (titleLen > 0) {
    std::vector<char> buf(titleLen + 1, 0);
    if (f.read((uint8_t *)buf.data(), titleLen) != (int)titleLen) {
      error_ = "Truncated title";
      return false;
    }
    title_ = String(buf.data());
  }

  BookVariant variant;
  variant.fontId = BOOK_FONT_PROFONT22;
  variant.pages.resize(pages);
  for (uint32_t i = 0; i < pages; i++) {
    uint32_t off = 0;
    if (!readU32(f, off) || off > fileSize_) {
      error_ = "Truncated or invalid offset table";
      return false;
    }
    variant.pages[i].offset = off;
    if (i > 0) {
      uint32_t prev = variant.pages[i - 1].offset;
      variant.pages[i - 1].length = (off > prev) ? (off - prev) : 0;
    }
  }
  uint32_t last = variant.pages[pages - 1].offset;
  variant.pages[pages - 1].length = (fileSize_ > last) ? (fileSize_ - last) : 0;

  variants_.push_back(variant);
  return true;
}

// MPG2: u8 variant_count, u16 title_len, title, then per variant u8 font_id,
// u32 page_count, and page_count entries of {u32 offset, u32 length,
// u32 anchor}.
bool Book::parseMpg2(fs::File &f) {
  uint8_t variantCount = 0;
  if (!readU8(f, variantCount) || variantCount == 0 ||
      variantCount > MPG2_MAX_VARIANTS) {
    error_ = "Bad variant count";
    return false;
  }

  uint16_t titleLen = 0;
  if (!readU16(f, titleLen) || titleLen > MAX_TITLE_LEN) {
    error_ = "Bad title length";
    return false;
  }
  if (titleLen > 0) {
    std::vector<char> buf(titleLen + 1, 0);
    if (f.read((uint8_t *)buf.data(), titleLen) != (int)titleLen) {
      error_ = "Truncated title";
      return false;
    }
    title_ = String(buf.data());
  }

  variants_.reserve(variantCount);
  for (uint8_t v = 0; v < variantCount; v++) {
    BookVariant variant;
    if (!readU8(f, variant.fontId)) {
      error_ = "Truncated variant header";
      return false;
    }
    uint32_t pages = 0;
    if (!readU32(f, pages) || pages == 0 || pages > MAX_PAGE_COUNT) {
      error_ = "Bad page count";
      return false;
    }
    variant.pages.resize(pages);
    // Read the table 12 bytes at a time: one entry per read keeps the code
    // simple and the LittleFS read cache does the buffering.
    uint8_t entry[12];
    uint32_t prevAnchor = 0;
    for (uint32_t i = 0; i < pages; i++) {
      if (f.read(entry, 12) != 12) {
        error_ = "Truncated page table";
        return false;
      }
      PageRef &ref = variant.pages[i];
      ref.offset = decodeU32(entry);
      ref.length = decodeU32(entry + 4);
      ref.anchor = decodeU32(entry + 8);
      if (ref.length == 0 || ref.offset > fileSize_ ||
          ref.offset + ref.length > fileSize_) {
        error_ = "Page table entry out of range";
        return false;
      }
      if (i > 0 && ref.anchor < prevAnchor) {
        error_ = "Page anchors are not in order";
        return false;
      }
      prevAnchor = ref.anchor;
    }
    variants_.push_back(variant);
  }
  return true;
}

void Book::close() {
  open_ = false;
  slug_ = "";
  title_ = "";
  error_ = "";
  fileSize_ = 0;
  activeVariant_ = 0;
  variants_.clear();
}

uint32_t Book::pageCount() const {
  if (!open_ || activeVariant_ >= variants_.size()) return 0;
  return (uint32_t)variants_[activeVariant_].pages.size();
}

uint32_t Book::anchorOf(uint32_t page) const {
  if (!open_ || activeVariant_ >= variants_.size()) return 0;
  const std::vector<PageRef> &pages = variants_[activeVariant_].pages;
  return page < pages.size() ? pages[page].anchor : 0;
}

uint8_t Book::fontId() const {
  if (!open_ || activeVariant_ >= variants_.size()) return BOOK_FONT_PROFONT22;
  return variants_[activeVariant_].fontId;
}

bool Book::setVariant(uint8_t variant) {
  if (variant >= variants_.size()) return false;
  activeVariant_ = variant;
  return true;
}

uint32_t Book::mapPage(uint32_t page, uint8_t variant) const {
  if (!open_ || variant >= variants_.size()) return 0;
  if (activeVariant_ >= variants_.size()) return 0;
  const std::vector<PageRef> &from = variants_[activeVariant_].pages;
  const std::vector<PageRef> &to = variants_[variant].pages;
  if (from.empty() || to.empty()) return 0;
  if (page >= from.size()) page = (uint32_t)from.size() - 1;
  uint32_t anchor = from[page].anchor;

  // Anchors are non-decreasing, so binary search for the last page at or
  // before this anchor.
  uint32_t lo = 0;
  uint32_t hi = (uint32_t)to.size();  // first page known to be past `anchor`
  while (lo < hi) {
    uint32_t mid = lo + (hi - lo) / 2;
    if (to[mid].anchor <= anchor) {
      lo = mid + 1;
    } else {
      hi = mid;
    }
  }
  return lo > 0 ? lo - 1 : 0;
}

void PageData::clear() {
  isImage = false;
  text = "";
  width = 0;
  height = 0;
  flags = 0;
  raw.clear();
}

bool Book::loadPage(uint32_t index, PageData &out) {
  out.clear();
  if (!open_ || activeVariant_ >= variants_.size()) return false;
  const std::vector<PageRef> &pages = variants_[activeVariant_].pages;
  if (index >= pages.size()) return false;

  uint32_t start = pages[index].offset;
  uint32_t len = pages[index].length;
  if (len == 0 || start + len > fileSize_) return false;

  fs::File f = LittleFS.open(bookPath(slug_).c_str(), "r");
  if (!f) return false;
  if (!f.seek(start, SeekSet)) {
    f.close();
    return false;
  }

  if (len >= MPG1_IMAGE_HEADER_LEN) {
    uint8_t hdr[MPG1_IMAGE_HEADER_LEN];
    if (f.read(hdr, MPG1_IMAGE_HEADER_LEN) != (int)MPG1_IMAGE_HEADER_LEN) {
      f.close();
      return false;
    }
    if (hdr[0] == 0x01 && hdr[1] == 'I' && hdr[2] == 'M' && hdr[3] == 'G') {
      uint16_t w = (uint16_t)((uint16_t)hdr[4] | ((uint16_t)hdr[5] << 8));
      uint16_t h = (uint16_t)((uint16_t)hdr[6] | ((uint16_t)hdr[7] << 8));
      uint32_t stride = ((uint32_t)w + 7) / 8;
      uint32_t need = stride * (uint32_t)h;
      if (w == 0 || h == 0 || need > MPG1_MAX_IMAGE_BYTES ||
          need != len - MPG1_IMAGE_HEADER_LEN) {
        f.close();
        return false;
      }
      out.raw.assign(need, 0);
      int got = f.read(out.raw.data(), need);
      f.close();
      if (got != (int)need) {
        out.clear();
        return false;
      }
      out.isImage = true;
      out.width = w;
      out.height = h;
      out.flags = hdr[8];
      return true;
    }
    // Not an image page after all, so rewind and read it as text.
    if (!f.seek(start, SeekSet)) {
      f.close();
      return false;
    }
  }

  std::vector<char> buf(len + 1, 0);
  int got = f.read((uint8_t *)buf.data(), len);
  f.close();
  if (got <= 0) return false;
  buf[got] = 0;
  out.text = String(buf.data());
  return true;
}

String Book::page(uint32_t index) {
  PageData data;
  if (!loadPage(index, data) || data.isImage) return String();
  return data.text;
}

namespace books {

// Every entry is opened and parsed from scratch here, so a file that was
// replaced since the last scan (news.pgs after a sync, most obviously) is read
// as it is now, and one that fails to parse is quietly left out of the list
// rather than taking the menu down with it.
std::vector<BookEntry> list() {
  std::vector<BookEntry> out;

  fs::File dir = LittleFS.open(BOOKS_DIR, "r");
  if (!dir || !dir.isDirectory()) return out;

  fs::File entry = dir.openNextFile();
  while (entry) {
    String name = entry.name();
    // Some FS implementations return a full path; keep only the basename.
    int slash = name.lastIndexOf('/');
    if (slash >= 0) name = name.substring(slash + 1);
    uint32_t fileBytes = (uint32_t)entry.size();
    entry.close();

    if (name.endsWith(".pgs")) {
      String slug = name.substring(0, name.length() - 4);
      // The daily numbers feed is a JSON file drawn by its own view since the
      // grid version; a numbers.pgs is a leftover of the text version and is
      // not a book any more. A slug with a leading dot is a book upload being
      // assembled (BOOK_UPLOAD_STAGE_SLUG), or the leftover of one that died.
      if (slug == METRICS_SLUG || slug.startsWith(".")) {
        entry = dir.openNextFile();
        continue;
      }
      Book b;
      if (b.open(slug)) {
        BookEntry e;
        e.slug = slug;
        e.title = b.title();
        e.fileBytes = fileBytes;
        e.variantCount = b.variantCount();
        uint8_t variant = 0;
        e.savedPage = loadPosition(slug, &variant);
        if (variant >= e.variantCount) variant = 0;
        b.setVariant(variant);
        e.savedVariant = variant;
        e.pageCount = b.pageCount();
        if (e.savedPage >= e.pageCount) e.savedPage = 0;
        out.push_back(e);
      }
    }
    entry = dir.openNextFile();
  }
  dir.close();
  return out;
}

uint32_t loadPosition(const String &slug, uint8_t *variant) {
  if (variant) *variant = 0;
  fs::File f = LittleFS.open(posPath(slug).c_str(), "r");
  if (!f) return 0;
  uint8_t b[8];
  uint32_t page = 0;
  int got = f.read(b, 8);
  if (got >= 4) {
    page = (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) |
           ((uint32_t)b[3] << 24);
  }
  // A 4 byte file predates text size switching and means variant 0.
  if (got == 8 && variant) {
    uint32_t v = (uint32_t)b[4] | ((uint32_t)b[5] << 8) |
                 ((uint32_t)b[6] << 16) | ((uint32_t)b[7] << 24);
    if (v < MPG2_MAX_VARIANTS) *variant = (uint8_t)v;
  }
  f.close();
  return page;
}

void savePosition(const String &slug, uint32_t page, uint8_t variant) {
  fs::File f = LittleFS.open(posPath(slug).c_str(), "w");
  if (!f) return;
  uint8_t b[8] = {(uint8_t)(page & 0xFF), (uint8_t)((page >> 8) & 0xFF),
                  (uint8_t)((page >> 16) & 0xFF), (uint8_t)((page >> 24) & 0xFF),
                  variant, 0, 0, 0};
  f.write(b, 8);
  f.close();
}

String loadCurrentSlug() {
  fs::File f = LittleFS.open(CURRENT_FILE, "r");
  if (!f) return String();
  String s = f.readStringUntil('\n');
  f.close();
  s.trim();
  return s;
}

void saveCurrentSlug(const String &slug) {
  fs::File f = LittleFS.open(CURRENT_FILE, "w");
  if (!f) return;
  f.print(slug);
  f.close();
}

}  // namespace books
