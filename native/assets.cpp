// pokeview --dump-assets <dir>: the pack's graphics as PNG files, coloured
// with the game's own Super Game Boy palettes, for front ends that are not
// one-bit e-paper. Written once; the web app serves them as static files.
//
//   <dir>/palettes.json            every SGB palette (hex) + species -> palette
//   <dir>/sprites/front/NNN.png    151 front pictures, species palette, shade 0 transparent
//   <dir>/sprites/back/NNN.png     151 back pictures
//   <dir>/icons/NN.png             15 party icons, 16x16, grey
//   <dir>/badges/N.png             8 badges and <dir>/leaders/N.png the 8 faces, PAL_BADGE
//   <dir>/townmap.png              the Kanto map, 160x144, PAL_TOWNMAP
//   <dir>/player_walk/N.png        Red's three walking frames, PAL_ROUTE
#include <Arduino.h>

#include <zlib.h>

#include <cstdio>
#include <string>
#include <sys/stat.h>
#include <vector>

#include "pack.h"
#include "pokemon_data.h"

namespace {

struct Rgb { uint8_t r, g, b; };
struct Palette { Rgb c[4]; };

std::vector<Palette> palettes;
std::vector<uint8_t> monPal;   // index = dex
std::vector<String> palNames;

void loadPalettes() {
  size_t n = pack::size("sgb_palettes") / 12;
  palettes.clear();
  for (size_t i = 0; i < n; i++) {
    uint8_t rec[12];
    if (pack::read("sgb_palettes", i * 12, rec, 12) != 12) break;
    Palette p;
    for (int c = 0; c < 4; c++) p.c[c] = {rec[c * 3], rec[c * 3 + 1], rec[c * 3 + 2]};
    palettes.push_back(p);
    palNames.push_back(pokedata::packString("sgb_pal_names", (int)i, 20));
  }
  monPal.assign(152, 0);
  pack::read("mon_palettes", 0, monPal.data(), 152);
}

Palette greyPalette() {
  Palette p;
  p.c[0] = {255, 255, 255}; p.c[1] = {170, 170, 170}; p.c[2] = {85, 85, 85}; p.c[3] = {0, 0, 0};
  return p;
}

Palette paletteOr(int idx, const Palette &fallback) {
  return (idx >= 0 && (size_t)idx < palettes.size()) ? palettes[(size_t)idx] : fallback;
}

// ---- a minimal PNG writer (RGBA8, zlib via compress2)
void put32(std::vector<uint8_t> &v, uint32_t x) {
  v.push_back(x >> 24); v.push_back(x >> 16); v.push_back(x >> 8); v.push_back(x);
}
void chunk(std::vector<uint8_t> &out, const char *type, const std::vector<uint8_t> &data) {
  put32(out, (uint32_t)data.size());
  std::vector<uint8_t> td(type, type + 4);
  td.insert(td.end(), data.begin(), data.end());
  out.insert(out.end(), td.begin(), td.end());
  put32(out, (uint32_t)crc32(0, td.data(), (uInt)td.size()));
}
bool writePng(const std::string &path, int w, int h, const std::vector<uint8_t> &rgba) {
  std::vector<uint8_t> raw;
  raw.reserve((size_t)(w * 4 + 1) * h);
  for (int y = 0; y < h; y++) {
    raw.push_back(0);
    raw.insert(raw.end(), rgba.begin() + (size_t)y * w * 4, rgba.begin() + (size_t)(y + 1) * w * 4);
  }
  uLongf zlen = compressBound((uLong)raw.size());
  std::vector<uint8_t> z(zlen);
  if (compress2(z.data(), &zlen, raw.data(), (uLong)raw.size(), 9) != Z_OK) return false;
  z.resize(zlen);
  std::vector<uint8_t> out = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
  std::vector<uint8_t> ihdr;
  put32(ihdr, (uint32_t)w); put32(ihdr, (uint32_t)h);
  ihdr.push_back(8); ihdr.push_back(6); ihdr.push_back(0); ihdr.push_back(0); ihdr.push_back(0);
  chunk(out, "IHDR", ihdr);
  chunk(out, "IDAT", z);
  chunk(out, "IEND", {});
  FILE *f = fopen(path.c_str(), "wb");
  if (!f) return false;
  fwrite(out.data(), 1, out.size(), f);
  fclose(f);
  return true;
}

// ---- 2 bpp tiles -> RGBA
struct Image {
  int w, h;
  std::vector<uint8_t> px;
  Image(int w_, int h_) : w(w_), h(h_), px((size_t)w_ * h_ * 4, 0) {}
  void set(int x, int y, Rgb c, uint8_t a) {
    if (x < 0 || y < 0 || x >= w || y >= h) return;
    uint8_t *p = &px[((size_t)y * w + x) * 4];
    p[0] = c.r; p[1] = c.g; p[2] = c.b; p[3] = a;
  }
};

void blitTile(Image &img, const uint8_t *tile16, int x, int y, const Palette &pal, bool shade0Transparent) {
  for (int row = 0; row < 8; row++) {
    uint8_t lo = tile16[row * 2], hi = tile16[row * 2 + 1];
    for (int col = 0; col < 8; col++) {
      uint8_t bit = (uint8_t)(7 - col);
      uint8_t shade = (uint8_t)(((lo >> bit) & 1) | (((hi >> bit) & 1) << 1));
      img.set(x + col, y + row, pal.c[shade], (shade == 0 && shade0Transparent) ? 0 : 255);
    }
  }
}

// `tiles` holds tilesW*tilesH tiles row-major
Image tilesImage(const uint8_t *tiles, int tilesW, int tilesH, const Palette &pal, bool transparent0) {
  Image img(tilesW * 8, tilesH * 8);
  for (int ty = 0; ty < tilesH; ty++)
    for (int tx = 0; tx < tilesW; tx++)
      blitTile(img, tiles + (size_t)(ty * tilesW + tx) * 16, tx * 8, ty * 8, pal, transparent0);
  return img;
}

void mkdirp(const std::string &d) { mkdir(d.c_str(), 0755); }

std::string hex(Rgb c) {
  char b[8];
  snprintf(b, sizeof(b), "#%02x%02x%02x", c.r, c.g, c.b);
  return b;
}

int dumpPics(const std::string &dir, bool front) {
  const char *idx = front ? "front_index" : "back_index";
  const char *blob = front ? "front" : "back";
  mkdirp(dir);
  int n = 0;
  for (int dex = 1; dex <= 151; dex++) {
    uint8_t rec[5];
    if (pack::read(idx, (size_t)(dex - 1) * 5, rec, 5) != 5) continue;
    uint32_t off = pokedata::le32(rec);
    int side = rec[4];
    if (side < 1 || side > 7) continue;
    std::vector<uint8_t> pic((size_t)side * side * 16);
    if (pack::read(blob, off, pic.data(), pic.size()) != pic.size()) continue;
    Palette pal = paletteOr(dex < (int)monPal.size() ? monPal[(size_t)dex] : -1, greyPalette());
    Image img = tilesImage(pic.data(), side, side, pal, true);
    char name[16];
    snprintf(name, sizeof(name), "/%03d.png", dex);
    if (writePng(dir + name, img.w, img.h, img.px)) n++;
  }
  return n;
}

int dumpSheet(const std::string &dir, const char *section, int count, int tilesW, int tilesH, int palIdx,
              bool transparent0) {
  mkdirp(dir);
  int n = 0;
  size_t per = (size_t)tilesW * tilesH * 16;
  std::vector<uint8_t> buf(per);
  Palette pal = paletteOr(palIdx, greyPalette());
  for (int k = 0; k < count; k++) {
    if (pack::read(section, (size_t)k * per, buf.data(), per) != per) break;
    Image img = tilesImage(buf.data(), tilesW, tilesH, pal, transparent0);
    char name[16];
    snprintf(name, sizeof(name), "/%d.png", k);
    if (writePng(dir + name, img.w, img.h, img.px)) n++;
  }
  return n;
}

int palIndexNamed(const char *name) {
  for (size_t i = 0; i < palNames.size(); i++)
    if (palNames[i] == name) return (int)i;
  return -1;
}

}  // namespace

int dumpAssets(const std::string &dir) {
  loadPalettes();
  if (palettes.empty()) {
    fprintf(stderr, "dump-assets: the pack has no sgb_palettes section; rebuild it with tools/pokered_pack.py\n");
  }
  mkdirp(dir);
  int total = 0;

  // palettes.json
  {
    std::string j = "{\"palettes\":[";
    for (size_t i = 0; i < palettes.size(); i++) {
      j += std::string(i ? "," : "") + "{\"name\":\"" + palNames[i].c_str() + "\",\"colors\":[";
      for (int c = 0; c < 4; c++) j += std::string(c ? "," : "") + "\"" + hex(palettes[i].c[c]) + "\"";
      j += "]}";
    }
    j += "],\"speciesPalette\":[";
    for (size_t d = 0; d < monPal.size(); d++) j += std::string(d ? "," : "") + std::to_string(monPal[d]);
    j += "]}\n";
    FILE *f = fopen((dir + "/palettes.json").c_str(), "w");
    if (f) { fputs(j.c_str(), f); fclose(f); total++; }
  }

  mkdirp(dir + "/sprites");
  int nf = dumpPics(dir + "/sprites/front", true);
  int nb = dumpPics(dir + "/sprites/back", false);
  total += nf + nb;
  fprintf(stderr, "dump-assets: %d front, %d back pictures\n", nf, nb);

  // icons: header u16 count, u16 tiles, then 64 bytes each
  {
    uint8_t head[4];
    if (pack::read("icons", 0, head, 4) == 4) {
      int count = pokedata::le16(head);
      mkdirp(dir + "/icons");
      std::vector<uint8_t> icon(64);
      int n = 0;
      for (int k = 0; k < count; k++) {
        if (pack::read("icons", 4 + (size_t)k * 64, icon.data(), 64) != 64) break;
        Image img = tilesImage(icon.data(), 2, 2, greyPalette(), true);
        char name[16];
        snprintf(name, sizeof(name), "/%d.png", k);
        if (writePng(dir + "/icons" + name, img.w, img.h, img.px)) n++;
      }
      total += n;
      fprintf(stderr, "dump-assets: %d party icons\n", n);
    }
  }

  int badgePal = palIndexNamed("PAL_BADGE");
  total += dumpSheet(dir + "/badges", "badges", 8, 2, 2, badgePal, true);
  total += dumpSheet(dir + "/leaders", "leader_faces", 8, 2, 2, badgePal, true);
  total += dumpSheet(dir + "/player_walk", "player_walk", 3, 2, 2, palIndexNamed("PAL_ROUTE"), true);

  // town map: 16 tiles, 20x18 cells
  {
    uint8_t tiles[16 * 16], cells[20 * 18];
    if (pack::read("townmap_tiles", 0, tiles, sizeof(tiles)) == sizeof(tiles) &&
        pack::read("townmap_map", 0, cells, sizeof(cells)) == sizeof(cells)) {
      Palette pal = paletteOr(palIndexNamed("PAL_TOWNMAP"), greyPalette());
      Image img(160, 144);
      for (int cy = 0; cy < 18; cy++)
        for (int cx = 0; cx < 20; cx++)
          blitTile(img, tiles + (size_t)(cells[cy * 20 + cx] & 0x0F) * 16, cx * 8, cy * 8, pal, false);
      if (writePng(dir + "/townmap.png", img.w, img.h, img.px)) total++;
    }
  }
  fprintf(stderr, "dump-assets: %d files under %s\n", total, dir.c_str());
  return total;
}
