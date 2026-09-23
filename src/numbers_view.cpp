#include "numbers_view.h"

#include <ArduinoJson.h>
#include <LittleFS.h>
#include <math.h>
#include <string.h>

#include "config.h"
#include "ui.h"

namespace {

// ------------------------------------------------------------------ layout
//
// The grid starts lower than the menu's because a headline line sits between
// the band and the first row; with 5 rows the tile is
// (880 - 100 - 4*12) / 5 = 146 px tall. Width, gutter, pad and the band are
// the shared constants in ui.h. The mock these numbers were tuned on is
// scratchpad numbers_mock.py / numbers_mock.png.
const int ROWS = 5;
const int MAX_TILES = ui::GRID_COLS * ROWS;  // 10, the feed's own cap
const int NAME_BASELINE_Y = 33;              // helvB24 in the 44 px band
const int HEADLINE_BASELINE_Y = ui::HEADER_H + 36;  // 80
const int GRID_TOP = ui::HEADER_H + 56;             // 100
const int TILE_H =
    (ui::GRID_BOTTOM - GRID_TOP - ui::GRID_GUTTER * (ROWS - 1)) / ROWS;  // 146

// Inside a tile, from its top edge.
const int LABEL_BASELINE = 36;   // profont22
const int ACCENT_Y = 50;         // 2 px hairline, clear of the label descenders
const int VALUE_BASELINE = 104;  // fub42 digits are 42 px tall
const int FOOT_BASELINE = 128;   // 7x14 avg and delta

// Ads screen: the channel name sits on its own line between the headline and
// the grid, so the grid starts lower and the tile loses a few pixels. The
// tile content (deepest baseline 128) still clears it.
const int ADS_CHAN_BASELINE_Y = ui::HEADER_H + 72;  // 116, profont22
const int ADS_GRID_TOP = ui::HEADER_H + 80;         // 124
const int ADS_TILE_H =
    (ui::GRID_BOTTOM - ADS_GRID_TOP - ui::GRID_GUTTER * (ROWS - 1)) / ROWS;  // 141

// Notes screen: one line per note in the body font, below the headline.
const int NOTES_FIRST_BASELINE = GRID_TOP + 30;
const int NOTES_PITCH = 32;

// Chart window. The plot sits between a value column on the left and the
// right margin, with the date ticks under it. A dot is DOT px square.
const int CHART_LEFT = TEXT_MARGIN_X + 92;      // room for "12,345,678"
const int CHART_RIGHT = SCREEN_W - TEXT_MARGIN_X - 8;
const int CHART_TOP = GRID_TOP + 12;            // 112
const int CHART_BOTTOM = ui::GRID_BOTTOM - 44;  // 836, ticks below
const int TICK_BASELINE = CHART_BOTTOM + 30;
const int DOT = 5;

// A JSON body larger than this is not a numbers feed. The real one is a few
// KB plus the history strings (about 4 bytes per metric per day, so 90 days
// of 50 metrics is under 20 KB); ArduinoJson wants about twice the text in
// heap for the parsed document, and it is freed again the moment the strings
// are copied out.
// The ads block adds up to ten more metrics with their own histories, so the
// cap is a little above what 60 metrics of 90 days come to.
const size_t MAX_JSON_BYTES = 128 * 1024;

// Characters fub42_tn can draw. Anything else in a value (the "M" or "k" of a
// compact number) is printed in helvB24 after the numeric part.
bool numGlyph(char c) {
  return (c >= '0' && c <= '9') || c == ',' || c == '.' || c == '+' ||
         c == '-';
}

String jsonString(JsonVariantConst v) {
  if (v.isNull()) return String();
  const char *s = v.as<const char *>();
  return s ? String(s) : String();
}

// Fills `out[0..MAX_WINDOWS)` from a JSON array of strings. A plain string
// (the single window feed from before 2026-09-12) lands in slot 0 so an old
// file still draws something.
void jsonStrings(JsonVariantConst v, String *out) {
  for (int i = 0; i < numbers_view::MAX_WINDOWS; i++) out[i] = String();
  if (v.is<JsonArrayConst>()) {
    int i = 0;
    for (JsonVariantConst e : v.as<JsonArrayConst>()) {
      if (i >= numbers_view::MAX_WINDOWS) break;
      out[i++] = jsonString(e);
    }
  } else {
    out[0] = jsonString(v);
  }
}

// "16,12,,5" into floats, NAN for an empty field.
void parseHistory(JsonVariantConst v, std::vector<float> &out) {
  out.clear();
  const char *p = v.as<const char *>();
  if (!p || !*p) return;
  while (true) {
    const char *end = strchr(p, ',');
    size_t len = end ? (size_t)(end - p) : strlen(p);
    if (len == 0) {
      out.push_back(NAN);
    } else {
      out.push_back(strtof(p, nullptr));
    }
    if (!end) break;
    p = end + 1;
  }
}

// One metric object, the same shape wherever it appears (a project's grid or
// an ads channel's column).
void parseMetric(JsonObjectConst m, numbers_view::Metric &out) {
  out.label = jsonString(m["l"]);
  jsonStrings(m["v"], out.value);
  jsonStrings(m["a"], out.foot);
  jsonStrings(m["d"], out.delta);
  parseHistory(m["h"], out.hist);
  out.histOffset = m["ho"].as<int>();
}

// "12,345" / "1.2M" / "45k" style for the chart's value labels. Small values
// keep one decimal when they have one.
String compactValue(float v) {
  float a = fabsf(v);
  const char *sign = v < 0 ? "-" : "";
  char buf[24];
  if (a >= 1000000.0f) {
    snprintf(buf, sizeof(buf), "%s%.1fM", sign, a / 1000000.0f);
  } else if (a >= 10000.0f) {
    snprintf(buf, sizeof(buf), "%s%.0fk", sign, a / 1000.0f);
  } else if (a >= 1000.0f) {
    int n = (int)(a + 0.5f);
    snprintf(buf, sizeof(buf), "%s%d,%03d", sign, n / 1000, n % 1000);
  } else if (a == floorf(a)) {
    snprintf(buf, sizeof(buf), "%s%d", sign, (int)a);
  } else {
    snprintf(buf, sizeof(buf), "%s%.1f", sign, a);
  }
  return String(buf);
}

int clampWindow(const numbers_view::Data &d, int window) {
  int n = (int)d.windows.size();
  if (n < 1) return 0;
  if (window < 0) return 0;
  if (window >= n) return n - 1;
  return window;
}

// ------------------------------------------------------------------ drawing

void drawHeader(const String &name, const String &right) {
  Adafruit_GFX &g = ui::gfx();
  g.fillRect(0, 0, SCREEN_W, ui::HEADER_H, ui::INK_BLACK);
  ui::setInk(true);
  ui::setFont(ui::Font::STATUS);
  int rw = ui::textWidth(right);
  ui::printAt(SCREEN_W - TEXT_MARGIN_X - rw, ui::HEADER_BASELINE_Y, right);
  ui::setFont(ui::Font::LABEL);
  int nameW = SCREEN_W - 2 * TEXT_MARGIN_X - rw - 24;
  ui::printAt(TEXT_MARGIN_X, NAME_BASELINE_Y, ui::fitText(name, nameW));
  ui::setInk(false);
}

// The value, as large as it fits: fub42 for the digits, with any leading
// symbol (the "$" of "$41.20") and any trailing letters (the "M" of
// "123.5M") in helvB24 on the same baseline, since the numeric font has
// neither. Falls back to helvB24 for the whole string when even that is too
// wide or the value is not a number at all.
void drawValue(int x, int baselineY, const String &value, int maxW) {
  const int len = (int)value.length();
  int start = 0;
  while (start < len && !numGlyph(value[start])) start++;
  int split = start;
  while (split < len && numGlyph(value[split])) split++;
  String prefix = value.substring(0, start);
  String digits = value.substring(start, split);
  String suffix = value.substring(split);

  bool big = digits.length() > 0;
  int prefixW = 0;
  int digitsW = 0;
  int w = 0;
  if (big) {
    ui::setFont(ui::Font::NUM);
    digitsW = ui::textWidth(digits);
    ui::setFont(ui::Font::LABEL);
    if (prefix.length()) prefixW = ui::textWidth(prefix) + 2;
    w = prefixW + digitsW;
    if (suffix.length()) w += ui::textWidth(suffix) + 2;
    if (w > maxW) big = false;
  }

  if (big) {
    if (prefix.length()) {
      ui::setFont(ui::Font::LABEL);
      ui::printAt(x, baselineY, prefix);
    }
    ui::setFont(ui::Font::NUM);
    ui::printAt(x + prefixW, baselineY, digits);
    if (suffix.length()) {
      ui::setFont(ui::Font::LABEL);
      ui::printAt(x + prefixW + digitsW + 2, baselineY, suffix);
    }
    return;
  }
  ui::setFont(ui::Font::LABEL);
  ui::printAt(x, baselineY, ui::fitText(value, maxW));
}

// One metric card. `h` is the tile height, which the project grid and the
// slightly shorter ads grid set differently; everything inside is placed from
// the tile's top edge, so only the frame cares.
void drawTile(int x, int y, const numbers_view::Metric &m, int w, int h) {
  Adafruit_GFX &g = ui::gfx();
  g.drawRect(x, y, ui::TILE_W, h, ui::INK_BLACK);
  const int textW = ui::TILE_W - 2 * ui::TILE_PAD;
  const int left = x + ui::TILE_PAD;

  ui::setFont(ui::Font::BODY);
  ui::printAt(left, y + LABEL_BASELINE, ui::fitText(m.label, textW));
  g.fillRect(left, y + ACCENT_Y, ui::TILE_ACCENT_W, 2, ui::INK_BLACK);

  drawValue(left, y + VALUE_BASELINE, m.value[w].length() ? m.value[w] : String("-"),
            textW);

  // Foot line left, delta right; the foot gives way to the delta when the
  // two would collide.
  int deltaW = 0;
  if (m.delta[w].length()) {
    ui::setFont(ui::Font::STATUS_BOLD);
    deltaW = ui::textWidth(m.delta[w]);
    ui::printAt(x + ui::TILE_W - ui::TILE_PAD - deltaW, y + FOOT_BASELINE,
                m.delta[w]);
  }
  if (m.foot[w].length()) {
    ui::setFont(ui::Font::STATUS);
    int room = textW - (deltaW ? deltaW + 12 : 0);
    ui::printAt(left, y + FOOT_BASELINE, ui::fitText(m.foot[w], room));
  }
}

void drawProject(const numbers_view::Project &p, const String &right, int w) {
  drawHeader(p.name, right);
  ui::setFont(ui::Font::BODY);
  ui::printAt(TEXT_MARGIN_X, HEADLINE_BASELINE_Y,
              ui::fitText(p.headline[w], SCREEN_W - 2 * TEXT_MARGIN_X));
  int n = (int)p.metrics.size();
  if (n > MAX_TILES) n = MAX_TILES;
  for (int i = 0; i < n; i++) {
    int r = i / ui::GRID_COLS;
    int c = i % ui::GRID_COLS;
    int x = TEXT_MARGIN_X + c * (ui::TILE_W + ui::GRID_GUTTER);
    int y = GRID_TOP + r * (TILE_H + ui::GRID_GUTTER);
    drawTile(x, y, p.metrics[i], w, TILE_H);
  }
}

// The ads screen: a column per channel, a row per metric, so Spend sits
// beside Spend. With one channel column 1 is left blank.
void drawAds(const numbers_view::Ads &ads, const String &right, int w) {
  drawHeader(ads.name, right);
  ui::setFont(ui::Font::BODY);
  ui::printAt(TEXT_MARGIN_X, HEADLINE_BASELINE_Y,
              ui::fitText(ads.headline[w], SCREEN_W - 2 * TEXT_MARGIN_X));
  int cols = (int)ads.channels.size();
  if (cols > ui::GRID_COLS) cols = ui::GRID_COLS;
  for (int c = 0; c < cols; c++) {
    const numbers_view::Channel &ch = ads.channels[c];
    int x = TEXT_MARGIN_X + c * (ui::TILE_W + ui::GRID_GUTTER);
    ui::setFont(ui::Font::BODY);
    ui::printAt(x + ui::TILE_PAD, ADS_CHAN_BASELINE_Y,
                ui::fitText(ch.name, ui::TILE_W - 2 * ui::TILE_PAD));
    int n = (int)ch.metrics.size();
    if (n > ROWS) n = ROWS;
    for (int i = 0; i < n; i++) {
      int y = ADS_GRID_TOP + i * (ADS_TILE_H + ui::GRID_GUTTER);
      drawTile(x, y, ch.metrics[i], w, ADS_TILE_H);
    }
  }
}

void drawNotes(const numbers_view::Data &d, const String &right) {
  drawHeader("Sync notes", right);
  ui::setFont(ui::Font::BODY);
  ui::printAt(TEXT_MARGIN_X, HEADLINE_BASELINE_Y,
              "Collectors that did not report.");
  int y = NOTES_FIRST_BASELINE;
  for (size_t i = 0; i < d.notes.size() && y < ui::GRID_BOTTOM; i++) {
    ui::printAt(TEXT_MARGIN_X + ui::TILE_PAD, y,
                ui::fitText(d.notes[i], SCREEN_W - 2 * TEXT_MARGIN_X - ui::TILE_PAD));
    y += NOTES_PITCH;
  }
  y += 16;
  ui::setFont(ui::Font::STATUS);
  ui::printAt(TEXT_MARGIN_X + ui::TILE_PAD, y,
              "generated " + d.generated + " ICT");
}

// The chart: axis frame, three value rules (min, mid, max) with labels, date
// ticks, then the series. The y range starts at zero for a series that never
// goes negative, so a count chart is honest about its baseline; a flat
// series gets a little headroom so the line is not glued to the top.
void drawChart(const numbers_view::Data &d, const numbers_view::Metric &m) {
  Adafruit_GFX &g = ui::gfx();
  const int days = d.historyDays > 0 ? d.historyDays : 1;
  const int plotW = CHART_RIGHT - CHART_LEFT;
  const int plotH = CHART_BOTTOM - CHART_TOP;

  float lo = 0, hi = 0;
  bool any = false;
  for (float v : m.hist) {
    if (isnan(v)) continue;
    if (!any) { lo = hi = v; any = true; }
    else { if (v < lo) lo = v; if (v > hi) hi = v; }
  }
  if (lo > 0) lo = 0;
  if (hi <= lo) hi = lo + (lo == 0 ? 1.0f : fabsf(lo));
  hi += (hi - lo) * 0.08f;  // headroom

  auto xOf = [&](int dayIndex) {
    return CHART_LEFT + (int)((long)dayIndex * (plotW - 1) / (days > 1 ? days - 1 : 1));
  };
  auto yOf = [&](float v) {
    return CHART_BOTTOM - (int)((v - lo) / (hi - lo) * plotH);
  };

  // Frame and value rules.
  g.drawFastVLine(CHART_LEFT, CHART_TOP, plotH + 1, ui::INK_BLACK);
  g.drawFastHLine(CHART_LEFT, CHART_BOTTOM, plotW + 1, ui::INK_BLACK);
  ui::setFont(ui::Font::STATUS);
  const float marks[3] = {lo, (lo + hi) / 2, hi / (1.08f) + lo * (1 - 1 / 1.08f)};
  for (int i = 0; i < 3; i++) {
    int y = yOf(marks[i]);
    if (i > 0) {
      for (int x = CHART_LEFT + 4; x < CHART_RIGHT; x += 6) g.drawPixel(x, y, ui::INK_BLACK);
    }
    String lab = compactValue(marks[i]);
    int w = ui::textWidth(lab);
    ui::printAt(CHART_LEFT - 8 - w, y + 5, lab);
  }

  // Date ticks.
  for (const numbers_view::Tick &t : d.ticks) {
    if (t.index < 0 || t.index >= days) continue;
    int x = xOf(t.index);
    g.drawFastVLine(x, CHART_BOTTOM, 6, ui::INK_BLACK);
    int w = ui::textWidth(t.label);
    int lx = x - w / 2;
    if (lx + w > CHART_RIGHT) lx = CHART_RIGHT - w;
    if (lx < CHART_LEFT - 40) lx = CHART_LEFT - 40;
    ui::printAt(lx, TICK_BASELINE, t.label);
  }

  // Series: a dot per day, a line between neighbours that both have a value.
  bool havePrev = false;
  int px = 0, py = 0;
  for (size_t i = 0; i < m.hist.size(); i++) {
    float v = m.hist[i];
    int dayIndex = m.histOffset + (int)i;
    if (dayIndex >= days) break;
    if (isnan(v)) { havePrev = false; continue; }
    int x = xOf(dayIndex);
    int y = yOf(v);
    if (havePrev) {
      g.drawLine(px, py, x, y, ui::INK_BLACK);
      g.drawLine(px, py + 1, x, y + 1, ui::INK_BLACK);  // 2 px stroke
    }
    g.fillRect(x - DOT / 2, y - DOT / 2, DOT, DOT, ui::INK_BLACK);
    px = x; py = y; havePrev = true;
  }

  if (!any) {
    ui::setFont(ui::Font::BODY);
    String msg = "No history for this metric yet.";
    int w = ui::textWidth(msg);
    ui::printAt(CHART_LEFT + (plotW - w) / 2, CHART_TOP + plotH / 2, msg);
  }
}

void drawEmpty(const String &right) {
  drawHeader("Daily numbers", right);
  ui::setFont(ui::Font::BODY);
  ui::printAt(TEXT_MARGIN_X, HEADLINE_BASELINE_Y, "No data in the last week.");
}

}  // namespace

namespace numbers_view {

int Data::screens() const {
  int n = (int)projects.size() + (ads.present ? 1 : 0) + (notes.empty() ? 0 : 1);
  return n > 0 ? n : 1;
}

int Data::adsScreen() const {
  return ads.present ? (int)projects.size() : -1;
}

int metricsOnScreen(const Data &data, int screen) {
  if (screen < 0) return 0;
  if (screen < (int)data.projects.size()) {
    return (int)data.projects[screen].metrics.size();
  }
  if (screen == data.adsScreen()) {
    int n = 0;
    for (const Channel &c : data.ads.channels) n += (int)c.metrics.size();
    return n;
  }
  return 0;
}

bool available() { return LittleFS.exists(NUMBERS_FILE); }

bool load(Data &out, String *err) {
  out = Data();
  auto fail = [&](const char *why) {
    if (err) *err = why;
    out = Data();
    return false;
  };

  fs::File f = LittleFS.open(NUMBERS_FILE, "r");
  if (!f) return fail("no numbers file");
  if (f.size() > MAX_JSON_BYTES) {
    f.close();
    return fail("numbers file too large");
  }

  JsonDocument doc;
  DeserializationError e = deserializeJson(doc, f);
  f.close();
  if (e) {
    if (err) *err = String("json: ") + e.c_str();
    out = Data();
    return false;
  }
  if (!doc.is<JsonObjectConst>()) return fail("json: not an object");
  JsonObjectConst root = doc.as<JsonObjectConst>();

  out.day = jsonString(root["day"]);
  out.label = jsonString(root["label"]);
  out.generated = jsonString(root["generated"]);

  for (JsonObjectConst w : root["windows"].as<JsonArrayConst>()) {
    if (out.windows.size() >= (size_t)MAX_WINDOWS) break;
    Window win;
    win.name = jsonString(w["name"]);
    win.range = jsonString(w["range"]);
    if (win.name.length()) out.windows.push_back(win);
  }
  if (out.windows.empty()) {
    // Single window feed (before 2026-09-12): the reporting day only.
    Window win;
    win.name = "Yesterday";
    win.range = out.label;
    out.windows.push_back(win);
  }

  JsonObjectConst hist = root["history"].as<JsonObjectConst>();
  if (!hist.isNull()) {
    out.historyDays = hist["days"].as<int>();
    for (JsonArrayConst t : hist["ticks"].as<JsonArrayConst>()) {
      if (t.size() < 2) continue;
      Tick tick;
      tick.index = t[0].as<int>();
      tick.label = jsonString(t[1]);
      out.ticks.push_back(tick);
    }
  }

  for (JsonObjectConst p : root["projects"].as<JsonArrayConst>()) {
    Project proj;
    proj.id = jsonString(p["id"]);
    proj.name = jsonString(p["name"]);
    jsonStrings(p["headline"], proj.headline);
    for (JsonObjectConst m : p["metrics"].as<JsonArrayConst>()) {
      Metric metric;
      parseMetric(m, metric);
      proj.metrics.push_back(metric);
      if (proj.metrics.size() >= (size_t)MAX_TILES) break;
    }
    if (proj.name.length()) out.projects.push_back(proj);
  }

  // The optional ads block: one screen, a column per channel. Absent in a
  // feed with no ad spend in the last week, and then nothing changes.
  JsonObjectConst ads = root["ads"].as<JsonObjectConst>();
  if (!ads.isNull()) {
    out.ads.name = jsonString(ads["name"]);
    jsonStrings(ads["headline"], out.ads.headline);
    for (JsonObjectConst c : ads["channels"].as<JsonArrayConst>()) {
      if (out.ads.channels.size() >= (size_t)ui::GRID_COLS) break;
      Channel ch;
      ch.id = jsonString(c["id"]);
      ch.name = jsonString(c["name"]);
      for (JsonObjectConst m : c["metrics"].as<JsonArrayConst>()) {
        Metric metric;
        parseMetric(m, metric);
        ch.metrics.push_back(metric);
        if (ch.metrics.size() >= (size_t)ROWS) break;
      }
      if (ch.name.length()) out.ads.channels.push_back(ch);
    }
    if (!out.ads.name.length()) out.ads.name = "Ads";
    out.ads.present = !out.ads.channels.empty();
  }
  for (JsonVariantConst n : root["notes"].as<JsonArrayConst>()) {
    String s = jsonString(n);
    if (s.length()) out.notes.push_back(s);
  }
  return true;
}

void render(const Data &data, int index, int window, bool forceFull) {
  const int total = data.screens();
  if (index < 0) index = 0;
  if (index >= total) index = total - 1;
  const int w = clampWindow(data, window);

  // Header right: the window and its date range. The render clock moves to
  // the status strip so the band has room for the range.
  String right;
  if (!data.windows.empty()) {
    right = data.windows[w].name;
    if (data.windows[w].range.length()) right += "  " + data.windows[w].range;
  }
  String footerLeft = "daily numbers  " + String(index + 1) + "/" + String(total);
  if (data.generated.length()) footerLeft += "  " + data.generated;

  ui::frameBegin(forceFull);
  do {
    ui::gfx().fillScreen(ui::INK_WHITE);
    ui::setInk(false);
    if (index < (int)data.projects.size()) {
      drawProject(data.projects[index], right, w);
    } else if (index == data.adsScreen()) {
      drawAds(data.ads, right, w);
    } else if (!data.notes.empty()) {
      drawNotes(data, right);
    } else {
      drawEmpty(right);
    }
    ui::setInk(false);
    // The founder's "right" button is BTN_LEFT in the enum; the hint names
    // the button as held, not as wired.
    ui::drawStatusStrip(footerLeft, "DOWN project   RIGHT window   CENTER menu");
  } while (ui::frameNext());
  ui::frameEnd();
}

void renderChart(const Data &data, int index, int metric, bool forceFull) {
  const int total = data.screens();
  if (index < 0) index = 0;
  if (index >= total) index = total - 1;

  String right = "Charts";
  if (data.historyDays > 0) right += "  last " + String(data.historyDays) + " days";
  String footerLeft = "daily numbers  " + String(index + 1) + "/" + String(total);

  // The screen's band title, and the metric the chart index lands on. A
  // project charts its own metrics; the ads screen charts its channels
  // flattened, channel 0 first, and names the channel in the title.
  const bool onProject = index < (int)data.projects.size();
  const bool onAds = index == data.adsScreen();
  String bandName;
  String chartTitle;
  const Metric *m = nullptr;
  const int count = metricsOnScreen(data, index);
  if (count > 0) {
    if (metric < 0) metric = 0;
    if (metric >= count) metric = count - 1;
    footerLeft += "  chart " + String(metric + 1) + "/" + String(count);
  }
  if (onProject) {
    bandName = data.projects[index].name;
    if (count > 0) {
      m = &data.projects[index].metrics[metric];
      chartTitle = m->label;
    }
  } else if (onAds) {
    bandName = data.ads.name;
    int left = metric;
    for (const Channel &c : data.ads.channels) {
      if (left < (int)c.metrics.size()) {
        m = &c.metrics[left];
        chartTitle = c.name + ": " + m->label;
        break;
      }
      left -= (int)c.metrics.size();
    }
  }

  ui::frameBegin(forceFull);
  do {
    ui::gfx().fillScreen(ui::INK_WHITE);
    ui::setInk(false);
    if (m) {
      drawHeader(bandName, right);
      // Headline slot: the metric and its latest value.
      ui::setFont(ui::Font::BODY);
      String line = chartTitle;
      if (m->value[0].length()) line += "   " + m->value[0] + " yesterday";
      ui::printAt(TEXT_MARGIN_X, HEADLINE_BASELINE_Y,
                  ui::fitText(line, SCREEN_W - 2 * TEXT_MARGIN_X));
      drawChart(data, *m);
    } else if (onProject || onAds) {
      drawHeader(bandName, right);
      ui::setFont(ui::Font::BODY);
      ui::printAt(TEXT_MARGIN_X, HEADLINE_BASELINE_Y, "No metrics to chart.");
    } else if (!data.notes.empty()) {
      drawNotes(data, right);
    } else {
      drawEmpty(right);
    }
    ui::setInk(false);
    ui::drawStatusStrip(footerLeft, "DOWN metric   RIGHT window   CENTER menu");
  } while (ui::frameNext());
  ui::frameEnd();
}

void renderMissing(const String &err, bool forceFull) {
  std::vector<String> lines;
  lines.push_back("No numbers yet, run Sync feeds.");
  if (err.length()) {
    lines.push_back("");
    lines.push_back(err);
  }
  lines.push_back("");
  lines.push_back("CENTER returns to the menu.");
  ui::renderStatusScreen("daily numbers", lines, forceFull);
}

}  // namespace numbers_view
