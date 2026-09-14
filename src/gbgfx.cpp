#include "gbgfx.h"

#include "pack.h"
#include "pokemon_state.h"
#include "ui.h"

namespace {

const char *SECTION_FONT = "font";
const char *SECTION_BORDER = "border";
const size_t FONT_BYTES = 256 * 8;   // 256 tiles, 1 bpp, 8 bytes each
const size_t BORDER_BYTES = 9 * 8;

uint8_t *fontPage = nullptr;
bool fontTried = false;
uint8_t borderTiles[BORDER_BYTES];
bool borderTried = false;
bool borderOk = false;

void loadFont() {
  if (fontTried) return;
  fontTried = true;
  if (!pack::isOpen() || pack::size(SECTION_FONT) < FONT_BYTES) return;
  fontPage = (uint8_t *)malloc(FONT_BYTES);
  if (!fontPage) return;
  if (pack::read(SECTION_FONT, 0, fontPage, FONT_BYTES) != FONT_BYTES) {
    free(fontPage);
    fontPage = nullptr;
  }
}

void loadBorder() {
  if (borderTried) return;
  borderTried = true;
  if (!pack::isOpen() || pack::size(SECTION_BORDER) < BORDER_BYTES) return;
  borderOk = pack::read(SECTION_BORDER, 0, borderTiles, BORDER_BYTES) ==
             BORDER_BYTES;
}

// One shade of one source pixel, expanded to a scale x scale block. Shade 1 is
// one dot per 2x2 cell and shade 2 a checker, both keyed off the absolute
// pixel position so neighbouring tiles line up into one continuous pattern.
void blockForShade(int px, int py, int scale, uint8_t shade, bool dither) {
  Adafruit_GFX &g = ui::gfx();
  if (shade == 0) return;
  if (shade == 3) {
    g.fillRect(px, py, scale, scale, ui::INK_BLACK);
    return;
  }
  if (scale < 2 || !dither) {
    // No room for a pattern: threshold, shade 2 goes black and shade 1 white.
    if (shade == 2) g.fillRect(px, py, scale, scale, ui::INK_BLACK);
    return;
  }
  for (int dy = 0; dy < scale; dy++) {
    for (int dx = 0; dx < scale; dx++) {
      int ax = px + dx, ay = py + dy;
      bool on = (shade == 2) ? (((ax + ay) & 1) == 0)
                             : (((ax & 1) == 0) && ((ay & 1) == 0));
      if (on) g.drawPixel(ax, ay, ui::INK_BLACK);
    }
  }
}

}  // namespace

namespace gbgfx {

void reset() {
  if (fontPage) {
    free(fontPage);
    fontPage = nullptr;
  }
  fontTried = false;
  borderTried = false;
  borderOk = false;
}

bool fontReady() {
  loadFont();
  return fontPage != nullptr;
}

void drawTile2bpp(const uint8_t *tile16, int x, int y, int scale, bool dither) {
  if (!tile16 || scale < 1) return;
  Adafruit_GFX &g = ui::gfx();
  for (int row = 0; row < 8; row++) {
    uint8_t lo = tile16[row * 2];
    uint8_t hi = tile16[row * 2 + 1];
    // Runs of black are one fillRect instead of eight; the dithered shades
    // still go pixel by pixel.
    int col = 0;
    while (col < 8) {
      uint8_t bit = (uint8_t)(7 - col);
      uint8_t shade = (uint8_t)(((lo >> bit) & 1) | (((hi >> bit) & 1) << 1));
      bool solid = (shade == 3) || (shade == 2 && (scale < 2 || !dither));
      if (solid) {
        int run = 1;
        while (col + run < 8) {
          uint8_t b2 = (uint8_t)(7 - (col + run));
          uint8_t s2 = (uint8_t)(((lo >> b2) & 1) | (((hi >> b2) & 1) << 1));
          bool solid2 = (s2 == 3) || (s2 == 2 && (scale < 2 || !dither));
          if (!solid2) break;
          run++;
        }
        g.fillRect(x + col * scale, y + row * scale, run * scale, scale,
                   ui::INK_BLACK);
        col += run;
      } else {
        blockForShade(x + col * scale, y + row * scale, scale, shade, dither);
        col++;
      }
    }
  }
}

void drawTile1bpp(const uint8_t *tile8, int x, int y, int scale, bool inverted) {
  if (!tile8 || scale < 1) return;
  Adafruit_GFX &g = ui::gfx();
  if (inverted) g.fillRect(x, y, 8 * scale, 8 * scale, ui::INK_BLACK);
  uint16_t ink = inverted ? ui::INK_WHITE : ui::INK_BLACK;
  for (int row = 0; row < 8; row++) {
    uint8_t bits = tile8[row];
    if (!bits) continue;   // a clear row is paper, already painted
    int col = 0;
    while (col < 8) {
      if (!((bits >> (7 - col)) & 1)) { col++; continue; }
      int run = 1;
      while (col + run < 8 && ((bits >> (7 - col - run)) & 1)) run++;
      g.fillRect(x + col * scale, y + row * scale, run * scale, scale, ink);
      col += run;
    }
  }
}

void drawFontCode(uint8_t code, int x, int y, int scale, bool inverted) {
  loadFont();
  if (!fontPage) return;
  drawTile1bpp(fontPage + (size_t)code * 8, x, y, scale, inverted);
}

int lineHeightGb(int scale) { return 8 * (scale < 1 ? 1 : scale); }

int textWidthGb(const String &s, int scale) {
  if (scale < 1) scale = 1;
  return (int)s.length() * 8 * scale;
}

int printGb(int x, int y, const char *ascii, int scale, bool inverted) {
  if (!ascii) return x;
  if (scale < 1) scale = 1;
  loadFont();
  if (!fontPage) {
    // No pack: fall back to the reader's own text layer. `y` is the top of the
    // cell here, and ui::printAt wants a baseline.
    ui::setFont(ui::Font::STATUS);
    ui::setInk(inverted);
    ui::printAt(x, y + 8 * scale - 2, String(ascii));
    ui::setInk(false);
    return x + ui::textWidth(String(ascii));
  }
  int cx = x;
  for (const char *p = ascii; *p; p++) {
    uint8_t code = pokemon::gen1Encode(*p);
    drawTile1bpp(fontPage + (size_t)code * 8, cx, y, scale, inverted);
    cx += 8 * scale;
  }
  return cx;
}

int printGb(int x, int y, const String &s, int scale, bool inverted) {
  return printGb(x, y, s.c_str(), scale, inverted);
}

void drawBox(int x, int y, int wTiles, int hTiles, int scale) {
  if (wTiles < 2 || hTiles < 2) return;
  if (scale < 1) scale = 1;
  loadBorder();
  int step = 8 * scale;
  if (!borderOk) {
    Adafruit_GFX &g = ui::gfx();
    g.fillRect(x, y, wTiles * step, hTiles * step, ui::INK_WHITE);
    g.drawRect(x, y, wTiles * step, hTiles * step, ui::INK_BLACK);
    g.drawRect(x + 2, y + 2, wTiles * step - 4, hTiles * step - 4,
               ui::INK_BLACK);
    return;
  }
  for (int ty = 0; ty < hTiles; ty++) {
    int rowKind = (ty == 0) ? 0 : (ty == hTiles - 1 ? 2 : 1);
    for (int tx = 0; tx < wTiles; tx++) {
      int colKind = (tx == 0) ? 0 : (tx == wTiles - 1 ? 2 : 1);
      const uint8_t *tile = borderTiles + (size_t)(rowKind * 3 + colKind) * 8;
      drawTile1bpp(tile, x + tx * step, y + ty * step, scale, false);
    }
  }
}

}  // namespace gbgfx
