// Native implementation of the handful of ui:: calls the companion views and
// gbgfx make. The real ui.cpp drives GxEPD2 + u8g2; here the "panel" is a
// 680x920 byte-per-pixel canvas and text falls back to the pack's Gen 1 font
// through gbgfx (the firmware only reaches ui::printAt when the pack has no
// font, so this path is rarely hit).
#include "ui.h"
#include "gbgfx.h"
#include <LittleFS.h>

#include <ctime>

SerialShim Serial;
LittleFSShim LittleFS;

namespace {
Adafruit_GFX canvas(SCREEN_W, SCREEN_H);
bool inkInverted = false;
int fontScale = 2;   // 7x14 status font ~ one 16 px Gen 1 cell
}  // namespace

namespace ui {

void begin() {}
void setRotation(uint8_t) {}
uint8_t rotation() { return DISPLAY_ROTATION; }
void setLandscape(bool) {}
void setSpeedDrive(bool) {}
void setSpeedCut(int) {}

void setFont(Font f) { fontScale = (f == Font::SMALL) ? 1 : 2; }
int textWidth(const String &s) { return (int)s.length() * 8 * fontScale; }
void setInk(bool inverted) { inkInverted = inverted; }
void printAt(int x, int baselineY, const String &s) {
  // Gen 1 glyphs sit on the cell bottom; a baseline 2 px above the cell edge.
  int top = baselineY - 8 * fontScale + 2;
  gbgfx::printGb(x, top, s, fontScale, inkInverted);
}
String fitText(const String &s, int maxW) {
  int cell = 8 * fontScale;
  if ((int)s.length() * cell <= maxW) return s;
  int cells = maxW / cell - 2;
  if (cells < 1) return String("..");
  return s.substring(0, (size_t)cells) + "..";
}
void drawStatusStrip(const String &left, const String &right, bool) {
  canvas.drawFastHLine(TEXT_MARGIN_X, STATUS_RULE_Y, SCREEN_W - 2 * TEXT_MARGIN_X, INK_BLACK);
  setFont(Font::SMALL);
  setInk(false);
  printAt(TEXT_MARGIN_X, STATUS_BASELINE_Y, left);
  setFont(Font::STATUS);
  int w = textWidth(right);
  printAt(SCREEN_W - TEXT_MARGIN_X - w, STATUS_BASELINE_Y, right);
}
Adafruit_GFX &gfx() { return canvas; }

// A full framebuffer: one pass through the paging loop.
void frameBegin(bool) {}
void frameBeginRaw(bool) {}
bool frameNext() { return false; }
void frameEnd() {}

void renderPage(const String &, const String &, uint8_t, uint32_t, uint32_t, bool) {}
void renderImagePage(const String &, const uint8_t *, uint16_t, uint16_t, uint8_t, uint32_t, uint32_t) {}
void renderMenu(const String &, const std::vector<MenuTile> &, int, bool) {}
int menuColumns() { return GRID_COLS; }
void renderJump(const String &, uint32_t, uint32_t, bool) {}
void renderMessage(const String &, const std::vector<String> &) {}
void renderStatusScreen(const String &, const std::vector<String> &, bool) {}

}  // namespace ui
