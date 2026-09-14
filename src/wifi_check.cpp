#include "wifi_check.h"

#include <Arduino.h>
#include <WiFi.h>
#include <esp_task_wdt.h>

#include <vector>

#include "config.h"
#include "input.h"
#include "net.h"
#include "ui.h"
#include "wifi_config.h"
#include "wifi_store.h"

namespace {

void feedWdt() { esp_task_wdt_reset(); }

// Sets the task watchdog timeout, keeping the panic-on-timeout behaviour the
// boot-time wdtBegin() armed. Local copy of the helper in news_sync.cpp: that
// one is file-static there and this module must not reach into it.
void setWdtTimeout(uint32_t ms) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  esp_task_wdt_config_t cfg = {};
  cfg.timeout_ms = ms;
  cfg.idle_core_mask = 0;
  cfg.trigger_panic = true;
  esp_task_wdt_reconfigure(&cfg);
#else
  esp_task_wdt_init(ms / 1000, true);
#endif
}

// Widens the watchdog window for the whole check and puts the normal window
// back on every exit path. A single join can sit inside the supplicant for
// longer than the normal 8 s WDT_TIMEOUT_MS with nothing this task can feed,
// and a check of several networks is several of those in a row.
struct WdtWindow {
  WdtWindow() { setWdtTimeout(NEWS_WDT_TIMEOUT_MS); }
  ~WdtWindow() {
    esp_task_wdt_reset();
    setWdtTimeout(WDT_TIMEOUT_MS);
  }
};

// Waits for a button or a timeout, whichever comes first, feeding the
// watchdog. Local copy of the helper in main.cpp, which is file-static there.
void waitForButtonOr(uint32_t ms) {
  uint32_t start = millis();
  while (millis() - start < ms) {
    feedWdt();
    Button b;
    if (input::poll(b)) return;
    delay(BUTTON_POLL_MS);
  }
}

void showLines(const std::vector<String> &body, bool forceFull = false) {
  std::vector<String> lines;
  lines.push_back("Check WiFi");
  lines.push_back("");
  for (size_t i = 0; i < body.size(); i++) lines.push_back(body[i]);
  ui::renderStatusScreen("wifi check", lines, forceFull);
}

}  // namespace

namespace wifi_check {

void run() {
  WdtWindow wdtWindow;

  {
    std::vector<String> body;
    body.push_back("Checking WiFi ...");
    showLines(body, true);
  }

  std::vector<wifi_store::Net> known = wifi_store::list();
  if (known.empty()) {
    std::vector<String> body;
    body.push_back("No saved networks.");
    body.push_back("");
    body.push_back("Run WiFi setup first.");
    body.push_back("");
    body.push_back("Press any button.");
    showLines(body);
    input::flush();
    waitForButtonOr(60000);
    input::flush();
    return;
  }

  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  feedWdt();

  {
    std::vector<String> body;
    body.push_back("Checking WiFi ...");
    body.push_back("");
    body.push_back("scanning");
    showLines(body);
  }

  int found = WiFi.scanNetworks(false, false);
  feedWdt();

  // Strongest beacon per stored SSID, or a flag that the scan never saw it.
  std::vector<bool> inRange(known.size(), false);
  std::vector<int32_t> rssi(known.size(), -127);
  for (int i = 0; i < found; i++) {
    String ssid = WiFi.SSID(i);
    for (size_t k = 0; k < known.size(); k++) {
      if (known[k].ssid != ssid) continue;
      int32_t r = WiFi.RSSI(i);
      if (!inRange[k] || r > rssi[k]) rssi[k] = r;
      inRange[k] = true;
    }
  }
  WiFi.scanDelete();
  feedWdt();

  std::vector<String> results;
  for (size_t k = 0; k < known.size(); k++) {
    if (!inRange[k]) {
      results.push_back(known[k].ssid + ": not in range");
      continue;
    }

    {
      // Progress: results so far, plus the network being tried right now.
      std::vector<String> body = results;
      body.push_back(known[k].ssid + ": trying ...");
      showLines(body);
    }

    feedWdt();
    bool ok = wifi_join_one(known[k].ssid, known[k].pass);
    feedWdt();
    if (ok) {
      results.push_back(known[k].ssid + ": ok " + String((int)rssi[k]) +
                        " dBm");
      // Drop the association before the next candidate so each join is tested
      // from the same starting state.
      WiFi.disconnect(false, false);
      feedWdt();
      delay(200);
    } else {
      results.push_back(known[k].ssid + ": wrong password / no join");
    }
  }

  wifi_off();
  feedWdt();

  std::vector<String> body = results;
  body.push_back("");
  body.push_back("Press any button.");
  showLines(body, true);

  input::flush();
  waitForButtonOr(120000);
  input::flush();
}

}  // namespace wifi_check
