#include "ui.h"

#include <GxEPD2_BW.h>
#include <SPI.h>
#include <U8g2_for_Adafruit_GFX.h>
#include <esp_task_wdt.h>
#include <time.h>

#include <algorithm>
#include <vector>

#include "config.h"
#include "drivers/GxEPD2_576_T81_Fast.h"
#include "pins.h"

namespace {

// ------------------------------------------------------------ menu layout
//
// Two column grid of cards on the 680x920 portrait canvas. Header band
// (solid black, sensor line in white), then MENU_ROWS rows of MENU_COLS tiles
// with a gutter between them, then the usual status strip. Each tile carries
// its 1-based index as a large numeral top right, a short accent hairline, a
// bold label and a small state line bottom left. The tile at the cursor is
// filled black with the same content in white. Rows scroll to keep the
// cursor's row visible when there are more tiles than MENU_ROWS * MENU_COLS.
//
// Geometry: the grid runs from MENU_GRID_TOP to MENU_GRID_BOTTOM, which is 16
// px above the status rule, so with 6 rows and a 12 px gutter a tile is
// (880 - 64 - 5*12) / 6 = 126 px tall and (648 - 12) / 2 = 318 px wide. The
// band, gutter, tile width and pad are the shared card constants in ui.h,
// which the numbers view draws with as well.
static const int MENU_COLS = ui::GRID_COLS;
static const int MENU_ROWS = 6;
static const int MENU_HEADER_H = ui::HEADER_H;
static const int MENU_HEADER_BASELINE_Y = ui::HEADER_BASELINE_Y;
static const int MENU_GUTTER = ui::GRID_GUTTER;
static const int MENU_GRID_TOP = MENU_HEADER_H + 20;          // 64
static const int MENU_GRID_BOTTOM = ui::GRID_BOTTOM;          // 880
static const int MENU_TILE_W = ui::TILE_W;
static const int MENU_TILE_H =
    (MENU_GRID_BOTTOM - MENU_GRID_TOP - MENU_GUTTER * (MENU_ROWS - 1)) /
    MENU_ROWS;
static const int MENU_TILE_PAD = ui::TILE_PAD;
// Offsets inside a tile, from its top edge. The numeral is fub42 (42 px
// digits), the label helvB24 (24 px caps), the sub line 7x14. A label wider
// than the tile wraps onto a second line at the tighter LABEL2 pitch, which
// pushes the sub line down to SUB2 (7x14 descenders end at 124, inside the
// 126 px tile).
static const int MENU_NUM_BASELINE = 50;
static const int MENU_ACCENT_Y = 60;
static const int MENU_ACCENT_W = ui::TILE_ACCENT_W;
static const int MENU_LABEL_BASELINE = 92;
static const int MENU_SUB_BASELINE = 114;
static const int MENU_LABEL2_BASELINE_1 = 82;
static const int MENU_LABEL2_BASELINE_2 = 106;
static const int MENU_SUB2_BASELINE = 121;

// Good Display GDEH0576T81, 920x680 native, 1 bit, driven through a DESPI-C02
// adapter, rotated to a 680x920 portrait canvas (see DISPLAY_ROTATION in
// config.h). The page buffer template parameter is the full native panel
// height, so a whole frame is composed in one firstPage/nextPage pass:
// 920*680/8 = 78200 bytes of static RAM out of the S3's 327 KB. Rotation is a
// coordinate transform inside GxEPD2, so it costs no extra RAM. That is the same choice the Muon
// firmware makes for this panel (src/drivers/display.cpp), and it matters more
// here than there: paging would re-run the text draw loop once per stripe.
//
// The panel class is GxEPD2_576_T81_Fast from src/drivers/, NOT the stock
// GxEPD2_576_GDEH0576T81 that GxEPD2_BW.h pulls in by itself. The stock driver
// has hasFastPartialUpdate = false and runs the full waveform even on a partial
// window, so every page turn flashes for seconds. Ours carries a real partial
// LUT and a previous-frame shadow buffer. See the header for why it is a
// separate class name instead of a header shadowing the stock one.
//
// Its writeImage() always consumes a whole 920x680 frame and ignores the window
// rectangle, so the page height below must stay the full panel height and every
// partial window must be the whole screen (see beginFrame).
GxEPD2_BW<GxEPD2_576_T81_Fast, GxEPD2_576_T81_Fast::HEIGHT> display(
    GxEPD2_576_T81_Fast(PIN_EINK_CS, PIN_EINK_DC, PIN_EINK_RST,
                        PIN_EINK_BUSY));

U8G2_FOR_ADAFRUIT_GFX u8g2;

uint16_t partialsSinceFull = 0;

// Set while the frame on glass is an image page. The next text page has to
// clear it with a full refresh, otherwise the artwork ghosts through.
bool lastFrameWasImage = false;

const uint8_t *FONT_BODY = u8g2_font_profont22_mf;
const uint8_t *FONT_BODY_LARGE = u8g2_font_profont29_mf;
const uint8_t *FONT_SMALL = u8g2_font_6x12_tf;
// Page counter on the right of the status strip, a step larger than the title.
const uint8_t *FONT_STATUS_RIGHT = u8g2_font_7x14_tf;
// Same metrics, bold: the page counter is the one thing in the strip a reader
// looks for on purpose, so it carries the weight.
const uint8_t *FONT_STATUS_RIGHT_BOLD = u8g2_font_7x14B_tf;
const uint8_t *FONT_BIG = u8g2_font_fub42_tn;
// Menu tile label: bold proportional, the one non-monospace face in the build
// (about 3 KB of flash). The tile numeral reuses FONT_BIG, already linked for
// the jump screen.
const uint8_t *FONT_MENU_LABEL = u8g2_font_helvB24_tr;
const uint8_t *FONT_MENU_NUM = u8g2_font_fub42_tn;

// One text grid. The numbers come from config.h and must match the host
// converter, which paginated the page we are about to draw.
struct TextGrid {
  const uint8_t *font;
  int cols;
  int rows;
  int firstBaselineY;
  int pitch;
};

TextGrid gridFor(uint8_t fontId) {
  if (fontId == BOOK_FONT_PROFONT29) {
    return TextGrid{FONT_BODY_LARGE, TEXT_LARGE_COLS, TEXT_LARGE_ROWS,
                    TEXT_LARGE_FIRST_BASELINE_Y, TEXT_LARGE_LINE_PITCH};
  }
  return TextGrid{FONT_BODY, TEXT_COLS, TEXT_ROWS, TEXT_FIRST_BASELINE_Y,
                  TEXT_LINE_PITCH};
}

// Full refreshes hold the panel busy for seconds and a flaky BUSY wire can
// stretch that to GxEPD2's 10 s timeout; feed the watchdog from the busy poll
// so a slow refresh can never starve it into a reboot loop.
void feedWatchdogWhileBusy(const void *) { esp_task_wdt_reset(); }

void setInk(bool inverted) {
  u8g2.setForegroundColor(inverted ? GxEPD_WHITE : GxEPD_BLACK);
  u8g2.setBackgroundColor(inverted ? GxEPD_BLACK : GxEPD_WHITE);
}

// Chooses full vs fast partial refresh and rations full refreshes so partial
// ghosting gets cleared periodically.
//
// setFastRefresh() is what actually picks the waveform. It swaps the LUT the
// driver loads at the next _InitDisplay(): the partial LUT drives only the
// pixels that changed against the shadow buffer (no white flash, a few hundred
// ms), the full LUT drives every pixel through the ghost-clearing waveform. It
// has to be set before setFullWindow/setPartialWindow so the flag is right by
// the time nextPage() reaches the driver.
//
// Shadow coherence is the driver's job and needs nothing here: a full frame
// goes through writeImageForFullRefresh(), which rewrites the shadow from the
// same bitmap it just sent, so the next partial interleaves against exactly
// what is on glass.
void beginFrame(bool forceFull) {
  bool full = forceFull || partialsSinceFull >= PARTIALS_BEFORE_FULL;
  if (full) {
    display.epd2.setFastRefresh(false);
    display.setFullWindow();
    partialsSinceFull = 0;
  } else {
    display.epd2.setFastRefresh(true);
    // Whole screen on purpose: the driver ignores the window rectangle and
    // always ships a full frame, so a smaller window would send stale rows.
    display.setPartialWindow(0, 0, display.width(), display.height());
    partialsSinceFull++;
  }
}

// Local HH:MM, or an empty string while the clock has never been set. The
// reader has no RTC battery: time(nullptr) is 1970 from a cold boot until a
// news sync runs SNTP, and stays right from then until the board loses power.
// Nothing here ticks on its own, which is the point: the strip is redrawn only
// when a frame is drawn, so the clock advances on a page turn and never wakes
// the panel by itself.
String clockText() {
  time_t now = time(nullptr);
  if (now < (time_t)NET_TIME_SANE_EPOCH) return String();
  now += (time_t)CLOCK_UTC_OFFSET_MINUTES * 60;
  struct tm parts;
  gmtime_r(&now, &parts);  // already shifted, so gmtime and not localtime
  char buf[8];
  snprintf(buf, sizeof(buf), "%02d:%02d", parts.tm_hour, parts.tm_min);
  return String(buf);
}

// Cuts `s` so it fits `maxW` pixels in the current u8g2 font, ending in ".."
// when anything was dropped. Two dots rather than an ellipsis glyph: the
// bitmap fonts in this build do not all carry one.
String fitText(const String &s, int maxW) {
  if ((int)u8g2.getUTF8Width(s.c_str()) <= maxW) return s;
  String out = s;
  while (out.length() > 1) {
    out.remove(out.length() - 1);
    String probe = out;
    probe.trim();
    probe += "..";
    if ((int)u8g2.getUTF8Width(probe.c_str()) <= maxW) return probe;
  }
  return String("..");
}

// Greedy word wrap of `s` into at most `maxLines` lines that fit `maxW` in
// the current font. Every line goes through fitText, so a single word wider
// than a line is cut with "..", and so is the last line when the text does
// not fit in `maxLines` (the remainder is packed onto it and cut).
std::vector<String> wrapText(const String &s, int maxW, int maxLines) {
  std::vector<String> words;
  int start = 0;
  while (start <= (int)s.length()) {
    int sp = s.indexOf(' ', start);
    if (sp < 0) sp = s.length();
    if (sp > start) words.push_back(s.substring(start, sp));
    start = sp + 1;
  }
  std::vector<String> lines;
  size_t i = 0;
  while (i < words.size()) {
    if ((int)lines.size() == maxLines - 1) {
      String rest = words[i];
      for (size_t j = i + 1; j < words.size(); j++) rest += " " + words[j];
      lines.push_back(fitText(rest, maxW));
      return lines;
    }
    String line = words[i++];
    while (i < words.size()) {
      String probe = line + " " + words[i];
      if ((int)u8g2.getUTF8Width(probe.c_str()) > maxW) break;
      line = probe;
      i++;
    }
    lines.push_back(fitText(line, maxW));
  }
  if (lines.empty()) lines.push_back(String());
  return lines;
}

// One menu card at (x, y). `selected` inverts it.
void drawMenuTile(int x, int y, int index, const ui::MenuTile &tile,
                  bool selected) {
  const int w = MENU_TILE_W;
  const int h = MENU_TILE_H;
  if (selected) {
    display.fillRect(x, y, w, h, GxEPD_BLACK);
  } else {
    display.drawRect(x, y, w, h, GxEPD_BLACK);
  }
  const uint16_t ink = selected ? GxEPD_WHITE : GxEPD_BLACK;
  setInk(selected);

  // Index numeral, top right.
  char num[8];
  snprintf(num, sizeof(num), "%02d", index);
  u8g2.setFont(FONT_MENU_NUM);
  int nw = u8g2.getUTF8Width(num);
  u8g2.setCursor(x + w - MENU_TILE_PAD - nw, y + MENU_NUM_BASELINE);
  u8g2.print(num);

  // Accent hairline, 2 px, so the card reads as a card and not a box.
  display.fillRect(x + MENU_TILE_PAD, y + MENU_ACCENT_Y, MENU_ACCENT_W, 2, ink);

  // Label and state line, bottom left. A label that does not fit on one line
  // ("News brief  2026-09-12 08:07") wraps onto two, and the state line moves
  // down to make room.
  const int textW = w - 2 * MENU_TILE_PAD;
  u8g2.setFont(FONT_MENU_LABEL);
  std::vector<String> lines = wrapText(tile.label, textW, 2);
  int subBaseline = MENU_SUB_BASELINE;
  if (lines.size() == 1) {
    u8g2.setCursor(x + MENU_TILE_PAD, y + MENU_LABEL_BASELINE);
    u8g2.print(lines[0]);
  } else {
    u8g2.setCursor(x + MENU_TILE_PAD, y + MENU_LABEL2_BASELINE_1);
    u8g2.print(lines[0]);
    u8g2.setCursor(x + MENU_TILE_PAD, y + MENU_LABEL2_BASELINE_2);
    u8g2.print(lines[1]);
    subBaseline = MENU_SUB2_BASELINE;
  }
  if (tile.sub.length()) {
    u8g2.setFont(FONT_STATUS_RIGHT);
    u8g2.setCursor(x + MENU_TILE_PAD, y + subBaseline);
    u8g2.print(fitText(tile.sub, textW));
  }
}

void drawStatusStrip(const String &left, const String &right,
                     bool boldRight = false) {
  display.drawFastHLine(TEXT_MARGIN_X, STATUS_RULE_Y,
                        display.width() - 2 * TEXT_MARGIN_X, GxEPD_BLACK);
  u8g2.setFont(FONT_SMALL);
  setInk(false);
  u8g2.setCursor(TEXT_MARGIN_X, STATUS_BASELINE_Y);
  u8g2.print(left);
  int leftEnd = TEXT_MARGIN_X + u8g2.getUTF8Width(left.c_str());

  u8g2.setFont(boldRight ? FONT_STATUS_RIGHT_BOLD : FONT_STATUS_RIGHT);
  int w = u8g2.getUTF8Width(right.c_str());
  int rightStart = display.width() - TEXT_MARGIN_X - w;
  u8g2.setCursor(rightStart, STATUS_BASELINE_Y);
  u8g2.print(right);

  // Clock in the middle of the strip, dropped rather than overlapped when the
  // title and the right hand text leave it no room (the menu's help line is
  // wide enough to claim the whole strip).
  String clock = clockText();
  if (clock.length()) {
    u8g2.setFont(FONT_STATUS_RIGHT);
    int wc = u8g2.getUTF8Width(clock.c_str());
    int x = (display.width() - wc) / 2;
    if (x > leftEnd + 12 && x + wc < rightStart - 12) {
      u8g2.setCursor(x, STATUS_BASELINE_Y);
      u8g2.print(clock);
    }
  }
}

}  // namespace

namespace ui {

// ----- primitives for views composed outside this file (numbers_view) -----

void setFont(Font f) {
  switch (f) {
    case Font::SMALL: u8g2.setFont(FONT_SMALL); break;
    case Font::STATUS: u8g2.setFont(FONT_STATUS_RIGHT); break;
    case Font::STATUS_BOLD: u8g2.setFont(FONT_STATUS_RIGHT_BOLD); break;
    case Font::BODY: u8g2.setFont(FONT_BODY); break;
    case Font::LABEL: u8g2.setFont(FONT_MENU_LABEL); break;
    case Font::NUM: u8g2.setFont(FONT_MENU_NUM); break;
  }
}

int textWidth(const String &s) { return (int)u8g2.getUTF8Width(s.c_str()); }

void setInk(bool inverted) { ::setInk(inverted); }

void printAt(int x, int baselineY, const String &s) {
  u8g2.setCursor(x, baselineY);
  u8g2.print(s);
}

String fitText(const String &s, int maxW) { return ::fitText(s, maxW); }

void drawStatusStrip(const String &left, const String &right, bool boldRight) {
  ::drawStatusStrip(left, right, boldRight);
}

Adafruit_GFX &gfx() { return display; }

void setRotation(uint8_t rotation) {
  if (rotation != 1 && rotation != 3) rotation = DISPLAY_ROTATION;
  display.setRotation(rotation);
}

uint8_t rotation() { return display.getRotation(); }

void frameBegin(bool forceFull) {
  beginFrame(forceFull || lastFrameWasImage);
  display.firstPage();
}

bool frameNext() { return display.nextPage(); }

void frameEnd() { lastFrameWasImage = false; }

void begin() {
  SPI.begin(PIN_EINK_SCK, PIN_EINK_MISO, PIN_EINK_MOSI, PIN_EINK_CS);

  display.epd2.setBusyCallback(feedWatchdogWhileBusy);
  // 50 ms reset pulse, the same value the Muon firmware uses for this panel.
  // The SSD2677 does not come up reliably on the 2 ms pulse the smaller
  // Waveshare panel was happy with.
  {
    // Wiring diagnostic: a floating BUSY follows whichever pull we apply.
    pinMode(PIN_EINK_BUSY, INPUT_PULLUP); delay(2); int up = digitalRead(PIN_EINK_BUSY);
    pinMode(PIN_EINK_BUSY, INPUT_PULLDOWN); delay(2); int dn = digitalRead(PIN_EINK_BUSY);
    pinMode(PIN_EINK_BUSY, INPUT);
    pinMode(PIN_EINK_RST, OUTPUT); digitalWrite(PIN_EINK_RST, LOW); delay(50);
    digitalWrite(PIN_EINK_RST, HIGH); delay(50);
    int afterRst = digitalRead(PIN_EINK_BUSY);
    Serial.printf("BUSY diag: pullup=%d pulldown=%d afterReset=%d (%s)\n", up, dn, afterRst,
                  (up != dn) ? "FLOATING, BUSY wire open" : (afterRst ? "driven HIGH" : "driven LOW"));
  }
  // 10 MHz SPI: the stock 4 MHz plus per byte transfers cost over half a
  // second per refresh on the 156 KB two plane frame. The driver now writes
  // whole rows in one burst; drop back to 4 MHz here if the panel ever shows
  // streaks.
  display.epd2.selectSPI(SPI, SPISettings(10000000, MSBFIRST, SPI_MODE0));
  display.init(115200, true, 50, false);
  // Portrait. The native frame is landscape, so an odd rotation gives the
  // 680x920 canvas every layout constant in config.h is written against.
  // Flip DISPLAY_ROTATION between 1 and 3 if the image comes up upside down.
  display.setRotation(DISPLAY_ROTATION);
  u8g2.begin(display);
  u8g2.setFontMode(1);
  u8g2.setFontDirection(0);
  setInk(false);

  // Heap budget check. The partial-refresh driver malloc's a ~78 KB shadow of
  // the previous frame on its first write, on top of the 78 KB page buffer that
  // is already static. A news sync then wants roughly 50 KB more for TLS on top
  // of the WiFi stack, so this line is how you confirm on the bench that the
  // reader still has room for both.
  Serial.printf("heap after display init: %u free, %u largest block "
                "(partial-refresh shadow will take %u)\n",
                (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap(),
                (unsigned)(((920 + 7) / 8) * 680));
}

void renderPage(const String &title, const String &pageText, uint8_t fontId,
                uint32_t pageIndex, uint32_t pageCount, bool forceFull) {
  String status = "page " + String((unsigned long)(pageIndex + 1)) + "/" +
                  String((unsigned long)pageCount);
  const TextGrid grid = gridFor(fontId);

  beginFrame(forceFull || lastFrameWasImage);
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    u8g2.setFont(grid.font);
    setInk(false);

    int row = 0;
    int start = 0;
    while (row < grid.rows && start <= (int)pageText.length()) {
      int nl = pageText.indexOf('\n', start);
      String line = (nl < 0) ? pageText.substring(start)
                             : pageText.substring(start, nl);
      line.replace("\r", "");
      if (line.length() > (unsigned)grid.cols) line = line.substring(0, grid.cols);
      u8g2.setCursor(TEXT_MARGIN_X, grid.firstBaselineY + row * grid.pitch);
      u8g2.print(line);
      row++;
      if (nl < 0) break;
      start = nl + 1;
    }

    drawStatusStrip(title, status, true);
  } while (display.nextPage());
  lastFrameWasImage = false;
}

void renderImagePage(const String &title, const uint8_t *bits, uint16_t width,
                     uint16_t height, uint8_t flags, uint32_t pageIndex,
                     uint32_t pageCount) {
  String status = "page " + String((unsigned long)(pageIndex + 1)) + "/" +
                  String((unsigned long)pageCount);
  const bool doubled = (flags & MPG1_IMAGE_FLAG_DOUBLE) != 0;
  const int scale = doubled ? 2 : 1;
  const int srcStride = (width + 7) / 8;

  // Content area is everything above the status rule.
  const int areaH = STATUS_RULE_Y;
  int drawW = (int)width * scale;
  int drawH = (int)height * scale;
  int x0 = (display.width() - drawW) / 2;
  int y0 = (areaH - drawH) / 2;
  if (x0 < 0) x0 = 0;
  if (y0 < 0) y0 = 0;
  // Rows that would run under the status strip are dropped rather than drawn.
  int visibleRows = height;
  if (y0 + drawH > areaH) visibleRows = (areaH - y0) / scale;
  if (visibleRows < 0) visibleRows = 0;

  // Row expansion buffer for the doubled case: one stored row widened to 2x
  // and held twice, so a single drawBitmap call paints both output rows.
  const int dstStride = (drawW + 7) / 8;
  std::vector<uint8_t> rowBuf;
  if (doubled) rowBuf.assign((size_t)dstStride * 2, 0);

  // Always a full refresh: dithered art under a partial refresh ghosts badly.
  beginFrame(true);
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);

    if (bits != nullptr && visibleRows > 0) {
      if (!doubled) {
        display.drawBitmap(x0, y0, bits, width, visibleRows, GxEPD_BLACK);
      } else {
        for (int y = 0; y < visibleRows; y++) {
          const uint8_t *src = bits + (size_t)y * srcStride;
          std::fill(rowBuf.begin(), rowBuf.end(), 0);
          for (int x = 0; x < (int)width; x++) {
            if (!(src[x >> 3] & (0x80 >> (x & 7)))) continue;
            int dx = x * 2;
            rowBuf[dx >> 3] |= (uint8_t)(0x80 >> (dx & 7));
            dx++;
            rowBuf[dx >> 3] |= (uint8_t)(0x80 >> (dx & 7));
          }
          // Second output row is a copy of the first.
          std::copy(rowBuf.begin(), rowBuf.begin() + dstStride,
                    rowBuf.begin() + dstStride);
          display.drawBitmap(x0, y0 + y * 2, rowBuf.data(), drawW, 2,
                             GxEPD_BLACK);
        }
      }
    }

    setInk(false);
    drawStatusStrip(title, status, true);
  } while (display.nextPage());
  lastFrameWasImage = true;
}

int menuColumns() { return MENU_COLS; }

void renderMenu(const String &header, const std::vector<MenuTile> &tiles,
                int cursor, bool forceFull) {
  const int count = (int)tiles.size();
  const int totalRows = (count + MENU_COLS - 1) / MENU_COLS;
  if (cursor < 0) cursor = 0;
  if (cursor >= count) cursor = count - 1;
  // Scroll by whole rows, only as far as it takes to show the cursor's row.
  const int cursorRow = (cursor >= 0) ? cursor / MENU_COLS : 0;
  int firstRow = cursorRow - MENU_ROWS + 1;
  if (firstRow < 0) firstRow = 0;

  String footerLeft = "menu";
  if (totalRows > MENU_ROWS) {
    int lastShown = firstRow + MENU_ROWS;
    if (lastShown > totalRows) lastShown = totalRows;
    footerLeft += "  rows " + String(firstRow + 1) + "-" + String(lastShown) +
                  " of " + String(totalRows);
  }

  // The menu is entered with a full refresh in most cases (lastFrameWasImage
  // or the caller's forceFull); cursor moves ride the partial LUT and the
  // ration in beginFrame clears the ghost the inverted tile leaves behind.
  beginFrame(forceFull || lastFrameWasImage);
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);

    // Header band: solid black, MENU tag plus the sensor line in white.
    display.fillRect(0, 0, display.width(), MENU_HEADER_H, GxEPD_BLACK);
    setInk(true);
    u8g2.setFont(FONT_STATUS_RIGHT_BOLD);
    u8g2.setCursor(TEXT_MARGIN_X, MENU_HEADER_BASELINE_Y);
    u8g2.print("MENU");
    int hx = TEXT_MARGIN_X + u8g2.getUTF8Width("MENU") + 28;
    u8g2.setFont(FONT_STATUS_RIGHT);
    u8g2.setCursor(hx, MENU_HEADER_BASELINE_Y);
    u8g2.print(fitText(header, display.width() - TEXT_MARGIN_X - hx));

    // Tiles.
    for (int i = 0; i < count; i++) {
      int r = i / MENU_COLS - firstRow;
      if (r < 0) continue;
      if (r >= MENU_ROWS) break;
      int c = i % MENU_COLS;
      int x = TEXT_MARGIN_X + c * (MENU_TILE_W + MENU_GUTTER);
      int y = MENU_GRID_TOP + r * (MENU_TILE_H + MENU_GUTTER);
      drawMenuTile(x, y, i + 1, tiles[i], i == cursor);
    }

    setInk(false);
    // The founder's "right" button is BTN_LEFT in the enum; the hint names
    // the button as held, not as wired.
    drawStatusStrip(footerLeft, "DOWN row   RIGHT column   CENTER pick");
  } while (display.nextPage());
  lastFrameWasImage = false;
}

void renderJump(const String &title, uint32_t target, uint32_t pageCount,
                bool forceFull) {
  beginFrame(forceFull || lastFrameWasImage);
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);

    u8g2.setFont(FONT_BODY);
    setInk(false);
    u8g2.setCursor(TEXT_MARGIN_X, TEXT_FIRST_BASELINE_Y);
    u8g2.print("Jump to page");

    // Big number on the vertical midline of the canvas, with the "of N" line
    // and the help line spaced below it. All three derive from SCREEN_H so the
    // block re-centres if the panel geometry ever changes again.
    const int centerY = SCREEN_H / 2;          // 460
    const int ofBaselineY = centerY + 70;      // 530
    const int helpBaselineY = centerY + 180;   // 640

    String num = String((unsigned long)target);
    u8g2.setFont(FONT_BIG);
    int w = u8g2.getUTF8Width(num.c_str());
    u8g2.setCursor((display.width() - w) / 2, centerY);
    u8g2.print(num);

    u8g2.setFont(FONT_BODY);
    String of = "of " + String((unsigned long)pageCount);
    int w2 = u8g2.getUTF8Width(of.c_str());
    u8g2.setCursor((display.width() - w2) / 2, ofBaselineY);
    u8g2.print(of);

    // Two lines: the 54 column portrait grid is too narrow for one.
    String help1 = "RIGHT +1  LEFT -1";
    String help2 = "UP +10  DOWN -10  CENTER go";
    int w3 = u8g2.getUTF8Width(help1.c_str());
    u8g2.setCursor((display.width() - w3) / 2, helpBaselineY);
    u8g2.print(help1);
    int w4 = u8g2.getUTF8Width(help2.c_str());
    u8g2.setCursor((display.width() - w4) / 2, helpBaselineY + TEXT_LINE_PITCH);
    u8g2.print(help2);

    drawStatusStrip(title, "jump");
  } while (display.nextPage());
  lastFrameWasImage = false;
}

void renderMessage(const String &title, const std::vector<String> &lines) {
  renderStatusScreen(title, lines, true);
}

void renderStatusScreen(const String &title, const std::vector<String> &lines,
                        bool forceFull) {
  beginFrame(forceFull || lastFrameWasImage);
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    u8g2.setFont(FONT_BODY);
    setInk(false);
    for (int i = 0; i < (int)lines.size() && i < TEXT_ROWS; i++) {
      u8g2.setCursor(TEXT_MARGIN_X,
                     TEXT_FIRST_BASELINE_Y + i * TEXT_LINE_PITCH);
      u8g2.print(lines[i]);
    }
    drawStatusStrip(title, "");
  } while (display.nextPage());
  lastFrameWasImage = false;
}

}  // namespace ui
