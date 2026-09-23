#pragma once

#include <Arduino.h>
#include <FS.h>

#include <vector>

// MPG1 and MPG2 (.pgs) book reader over LittleFS.
//
// MPG1 holds one pagination of a book. MPG2 holds the same book paginated for
// more than one text size ("variants"), sharing a single copy of every image
// blob, and gives every page an anchor into the book's character stream so a
// reading position survives a size change. The two formats are told apart by
// the 4 byte magic.

// A loaded page: either UTF-8 text or a 1 bit bitmap. The bitmap payload
// lives in `raw`, so keep the PageData alive for as long as bits() is used.
struct PageData {
  bool isImage = false;

  String text;  // valid when isImage is false

  uint16_t width = 0;   // stored bitmap size, valid when isImage is true
  uint16_t height = 0;
  uint8_t flags = 0;  // bit0 (MPG1_IMAGE_FLAG_DOUBLE): render each pixel 2x2

  std::vector<uint8_t> raw;  // raster, stride = (width + 7) / 8

  const uint8_t *bits() const { return raw.empty() ? nullptr : raw.data(); }
  void clear();
};

// One page table entry. Lengths are explicit because image blobs are shared
// between variants, so a page does not always end where the next one begins.
struct PageRef {
  uint32_t offset = 0;
  uint32_t length = 0;
  uint32_t anchor = 0;
};

// One pagination of a book: a font id (see BOOK_FONT_* in config.h) and its
// page table.
struct BookVariant {
  uint8_t fontId = 0;
  std::vector<PageRef> pages;
};

struct BookEntry {
  String slug;
  String title;
  uint32_t pageCount = 0;
  uint32_t savedPage = 0;     // zero based, in savedVariant
  uint8_t savedVariant = 0;
  uint8_t variantCount = 1;
  uint32_t fileBytes = 0;     // size of the .pgs on the filesystem
};

class Book {
 public:
  // Opens /books/<slug>.pgs and parses the header. Returns false and sets
  // errorText() on any failure; the object is then closed and unusable.
  bool open(const String &slug);
  void close();

  bool isOpen() const { return open_; }
  const String &slug() const { return slug_; }
  const String &title() const { return title_; }
  const String &errorText() const { return error_; }

  // Page count and font of the active variant.
  uint32_t pageCount() const;
  uint8_t fontId() const;

  uint8_t variantCount() const { return (uint8_t)variants_.size(); }
  uint8_t activeVariant() const { return activeVariant_; }

  // Switches the active variant without touching any page number. Returns
  // false if the index is out of range.
  bool setVariant(uint8_t variant);

  // Position mapping for a size change: the largest page index in `variant`
  // whose anchor is at or before the anchor of `page` in the active variant.
  uint32_t mapPage(uint32_t page, uint8_t variant) const;

  // Returns page text (lines separated by \n), empty on failure. Image pages
  // come back empty here; use loadPage() to handle both kinds.
  String page(uint32_t index);

  // Loads a page of either kind into `out`. Returns false on any failure.
  // `out` is reused across calls, which keeps the raster vector's capacity.
  bool loadPage(uint32_t index, PageData &out);

 private:
  bool parseMpg1(fs::File &f);
  bool parseMpg2(fs::File &f);

  bool open_ = false;
  String slug_;
  String title_;
  String error_;
  uint32_t fileSize_ = 0;
  uint8_t activeVariant_ = 0;
  std::vector<BookVariant> variants_;
};

namespace books {

// Lists every /books/*.pgs, reading title, page count, and saved position.
std::vector<BookEntry> list();

// Reading position in /books/<slug>.pos: u32 LE page, then u32 LE variant.
// A legacy 4 byte file is read as page with variant 0.
uint32_t loadPosition(const String &slug, uint8_t *variant = nullptr);
void savePosition(const String &slug, uint32_t page, uint8_t variant = 0);

// Current book slug in /current.txt.
String loadCurrentSlug();
void saveCurrentSlug(const String &slug);

}  // namespace books
