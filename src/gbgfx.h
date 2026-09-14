#pragma once

#include <Arduino.h>

// Game Boy tile and Gen 1 font drawing on the 1 bit panel.
//
// Everything the companion screen draws comes from the pack (src/pack.*):
// 2 bpp graphics tiles, a 1 bpp font page in Gen 1 charmap order, and the nine
// text box border tiles. The four GB shades are mapped to a 2x2 dither at
// scale 2 and up (0 white, 1 one dot in four, 2 checker, 3 black) and to a
// threshold at scale 1, where a dither would just look like noise.
//
// Drawing goes through ui::gfx(), so these calls belong inside a
// ui::frameBegin() / frameNext() paging loop like any other view.

namespace gbgfx {

// One 8x8 tile in GB planar 2 bpp: 16 bytes, two bytes per row, low bitplane
// first. Drawn at `scale` (1 = 8x8 px, 2 = 16x16 px, and so on).
void drawTile2bpp(const uint8_t *tile16, int x, int y, int scale, bool dither);

// One 8x8 tile in 1 bpp: 8 bytes, one per row, MSB is the leftmost pixel,
// bit 1 = ink. `inverted` swaps ink and paper (white glyph on a black cell).
void drawTile1bpp(const uint8_t *tile8, int x, int y, int scale, bool inverted);

// Gen 1 font text. Each character is mapped to its Gen 1 code and drawn from
// the pack's "font" section (256 tiles of 8 bytes, charmap order). The section
// is cached in RAM on first use. Falls back to ui::printAt with the reader's
// own font when the pack has no font section, so a screen without a pack still
// says something. Returns the x past the last cell drawn.
int printGb(int x, int y, const char *ascii, int scale, bool inverted);
int printGb(int x, int y, const String &s, int scale, bool inverted);

// One glyph of the pack's font page by its raw Gen 1 character code, for the
// handful of tiles that have no ASCII spelling (the accented e of POKeDEX at
// $BA, the box rules at $79 to $7E). Draws nothing when there is no pack.
void drawFontCode(uint8_t code, int x, int y, int scale, bool inverted);

// Width in pixels printGb would take for `s` at `scale`.
int textWidthGb(const String &s, int scale);

// Height of one printGb line in pixels.
int lineHeightGb(int scale);

// The in-game text box: a wTiles x hTiles frame of border tiles from the
// pack's "border" section (9 tiles in the order top-left, top, top-right,
// left, center, right, bottom-left, bottom, bottom-right). The interior is
// filled with the centre tile. Draws a plain rectangle when the pack has no
// border section.
void drawBox(int x, int y, int wTiles, int hTiles, int scale);

// Drops the cached font page. Call when the pack is closed.
void reset();

// True when the pack carries a usable font page, so a view can decide between
// the game look and the reader's own font.
bool fontReady();

}  // namespace gbgfx
