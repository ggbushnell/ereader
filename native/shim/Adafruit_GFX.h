// Native stand-in for Adafruit_GFX: the drawing surface the companion views
// paint on, as one byte per pixel (0 = black ink, 1 = white paper). Only the
// primitives the views and gbgfx call are implemented; colour semantics match
// GxEPD2 (0x0000 black, 0xFFFF white).
#pragma once
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

class Adafruit_GFX {
 public:
  Adafruit_GFX(int16_t w, int16_t h) : w_(w), h_(h), px_((size_t)w * h, 1) {}
  virtual ~Adafruit_GFX() {}

  int16_t width() const { return w_; }
  int16_t height() const { return h_; }

  virtual void drawPixel(int16_t x, int16_t y, uint16_t color) {
    if (x < 0 || y < 0 || x >= w_ || y >= h_) return;
    px_[(size_t)y * w_ + x] = color ? 1 : 0;
  }
  void fillScreen(uint16_t color) { memset(px_.data(), color ? 1 : 0, px_.size()); }
  void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
    if (w <= 0 || h <= 0) return;
    int x0 = x < 0 ? 0 : x, y0 = y < 0 ? 0 : y;
    int x1 = x + w > w_ ? w_ : x + w, y1 = y + h > h_ ? h_ : y + h;
    if (x0 >= x1 || y0 >= y1) return;
    uint8_t v = color ? 1 : 0;
    for (int yy = y0; yy < y1; yy++) memset(&px_[(size_t)yy * w_ + x0], v, (size_t)(x1 - x0));
  }
  void drawFastHLine(int16_t x, int16_t y, int16_t w, uint16_t color) { fillRect(x, y, w, 1, color); }
  void drawFastVLine(int16_t x, int16_t y, int16_t h, uint16_t color) { fillRect(x, y, 1, h, color); }
  void drawRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
    if (w <= 0 || h <= 0) return;
    drawFastHLine(x, y, w, color);
    drawFastHLine(x, y + h - 1, w, color);
    drawFastVLine(x, y, h, color);
    drawFastVLine(x + w - 1, y, h, color);
  }
  void drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color) {
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
      drawPixel(x0, y0, color);
      if (x0 == x1 && y0 == y1) break;
      int e2 = 2 * err;
      if (e2 >= dy) { err += dy; x0 += sx; }
      if (e2 <= dx) { err += dx; y0 += sy; }
    }
  }
  void drawCircle(int16_t, int16_t, int16_t, uint16_t) {}
  void fillCircle(int16_t, int16_t, int16_t, uint16_t) {}
  void setRotation(uint8_t) {}
  uint8_t getRotation() const { return 1; }

  const uint8_t *pixels() const { return px_.data(); }

 private:
  int16_t w_, h_;
  std::vector<uint8_t> px_;
};
