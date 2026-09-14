#pragma once

#include <Arduino.h>

#include <vector>

// Daily numbers: the metrics hub's cross project figures, drawn as a grid of
// cards in the same family as the menu.
//
// The data is /numbers.json (NUMBERS_FILE in config.h), stored verbatim by the
// feed sync from GET /metrics/ereader.json on the Worker. Every string in it is
// already formatted for the glass (ASCII, labels at most 14 characters, values
// at most 9, deltas signed), so this module does no number formatting: it
// parses, holds, and draws.
//
//   {"day":"2026-09-10","label":"Thu 10 Sep","generated":"08:07",
//    "windows":[{"name":"Yesterday","range":"Thu 10 Sep"},
//               {"name":"7 days","range":"4 Sep to 10 Sep"},
//               {"name":"30 days","range":"12 Aug to 10 Sep"},
//               {"name":"Total","range":"since 9 Sep"}],
//    "projects":[{"id":"muon","name":"Muon Sortes",
//      "headline":["Visits up 144%","Visits up 18%","Quiet day.",
//                  "Everything since Wed 9 Sep."],
//      "metrics":[{"l":"Visits","v":["22","140","500","900"],
//                  "a":["7d avg 9","prev 117","prev 400","avg 15/day"],
//                  "d":["+144%","+20%","+25%",null]}]}],
//    "notes":["ig skipped","reddit failed"]}
//
// Every metric carries one value, one foot line and one delta per time window
// (parallel arrays, in the order of "windows"); the view shows one window at
// a time and switches with the "right" pad. One screen per project, then one
// more listing the notes when there are any.
// Screen layout (680x920 portrait, constants in numbers_view.cpp):
//
//   black header band: project name (helvB24, white), "7 days  4 Sep to 10 Sep"
//   headline line (profont22), one per window
//   2 x 5 grid of cards, one metric each: label (profont22), accent hairline,
//     value (fub42, or helvB24 when it carries a letter or does not fit),
//     the foot line bottom left and the delta bottom right (7x14)
//   status strip: "daily numbers  2/5  08:07" and the button hint

namespace numbers_view {

// Windows the feed may carry. Anything past this is ignored.
static const int MAX_WINDOWS = 4;

struct Window {
  String name;   // "7 days"
  String range;  // "4 Sep to 10 Sep"
};

struct Metric {
  String label;
  String value[MAX_WINDOWS];  // "-" when the feed had nothing
  String foot[MAX_WINDOWS];   // empty when the feed sent null
  String delta[MAX_WINDOWS];  // empty when there is no comparison
  // Daily values for the chart window, on the shared axis: hist[i] is day
  // histOffset + i of Data::historyDays, NAN for a day with no row.
  std::vector<float> hist;
  int histOffset = 0;
};

struct Tick {
  int index;     // day index on the history axis
  String label;  // "9 Sep"
};

struct Project {
  String id;
  String name;
  String headline[MAX_WINDOWS];
  std::vector<Metric> metrics;
};

struct Data {
  String day;        // "2026-09-10", the reporting day
  String label;      // "Thu 10 Sep"
  String generated;  // "08:07", ICT clock of the render
  std::vector<Window> windows;  // at least one after load()
  int historyDays = 0;          // length of the chart axis, 0 = no history
  std::vector<Tick> ticks;      // x axis labels for the chart window
  std::vector<Project> projects;
  std::vector<String> notes;

  // Screens to page through: one per project, plus the notes screen when
  // there are notes. Never less than one, so an empty feed still shows a
  // "nothing in the window" screen instead of nothing.
  int screens() const;
};

// True when NUMBERS_FILE exists, so the caller can tell "never synced" from
// "failed to parse" before spending a frame.
bool available();

// Parses NUMBERS_FILE into `out`. Returns false and sets `err` on any failure
// (missing file, bad JSON, wrong shape); `out` is then cleared.
bool load(Data &out, String *err);

// Draws screen `index` of `data` (clamped to 0..screens()-1) in time window
// `window` (clamped to the windows the feed carries). Same refresh policy as
// the menu: partial unless `forceFull`, with the usual ration and the image
// page carry over handled inside ui.
void render(const Data &data, int index, int window, bool forceFull = false);

// The chart window: one full screen line chart of metric `metric` of project
// `index` over the history axis. Value labels at the left, date ticks along
// the bottom, a dot per day and a line between consecutive days that both
// have a value. `index` past the projects (the notes screen) draws the
// notes screen instead.
void renderChart(const Data &data, int index, int metric, bool forceFull = false);

// The status screen shown when there is nothing to draw: no file yet, or a
// file that did not parse (`err` is printed when non empty).
void renderMissing(const String &err, bool forceFull = false);

}  // namespace numbers_view
