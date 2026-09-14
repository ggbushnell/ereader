#include "net.h"

#include <WiFi.h>
#include <esp_task_wdt.h>
#include <time.h>

#include <vector>

#include "config.h"
#include "wifi_config.h"
#include "wifi_store.h"

namespace {

// Scan result for a network we hold credentials for.
struct Candidate {
  String ssid;
  String pass;
  int32_t rssi;
};

void feedWdt() { esp_task_wdt_reset(); }

}  // namespace

bool wifi_join_one(const String &ssid, const String &pass) {
  WiFi.begin(ssid.c_str(), pass.length() ? pass.c_str() : nullptr);
  uint32_t start = millis();
  while (millis() - start < WIFI_CONNECT_TIMEOUT_MS) {
    if (WiFi.status() == WL_CONNECTED) return true;
    feedWdt();
    delay(100);
  }
  WiFi.disconnect(false, false);
  return false;
}

bool wifi_connect(String *ssidOut) {
  std::vector<wifi_store::Net> known = wifi_store::list();
  if (known.empty()) return false;

  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  feedWdt();

  // Scan first so the strongest known network is tried before a distant one
  // that happens to sit earlier in the store.
  int found = WiFi.scanNetworks(false, false);
  feedWdt();

  std::vector<Candidate> candidates;
  for (int i = 0; i < found; i++) {
    String ssid = WiFi.SSID(i);
    for (size_t k = 0; k < known.size(); k++) {
      if (known[k].ssid != ssid) continue;
      bool already = false;
      for (size_t c = 0; c < candidates.size(); c++) {
        if (candidates[c].ssid == ssid) already = true;
      }
      if (already) break;
      Candidate cand;
      cand.ssid = ssid;
      cand.pass = known[k].pass;
      cand.rssi = WiFi.RSSI(i);
      candidates.push_back(cand);
      break;
    }
  }
  WiFi.scanDelete();

  // Strongest first (insertion sort: at most WIFI_STORE_MAX_NETWORKS entries).
  for (size_t i = 1; i < candidates.size(); i++) {
    Candidate key = candidates[i];
    size_t j = i;
    while (j > 0 && candidates[j - 1].rssi < key.rssi) {
      candidates[j] = candidates[j - 1];
      j--;
    }
    candidates[j] = key;
  }

  // A hidden network never shows up in a scan, so fall back to trying the
  // stored list in order when the scan matched nothing.
  if (candidates.empty()) {
    for (size_t k = 0; k < known.size(); k++) {
      Candidate cand;
      cand.ssid = known[k].ssid;
      cand.pass = known[k].pass;
      cand.rssi = -127;
      candidates.push_back(cand);
    }
  }

  for (size_t i = 0; i < candidates.size(); i++) {
    if (wifi_join_one(candidates[i].ssid, candidates[i].pass)) {
      if (ssidOut) *ssidOut = candidates[i].ssid;
      return true;
    }
  }

  wifi_off();
  return false;
}

void wifi_off() {
  WiFi.disconnect(true, false);
  WiFi.mode(WIFI_OFF);
}

bool net_time_sync() {
  time_t now = time(nullptr);
  if (now > NET_TIME_SANE_EPOCH) return true;  // already set this session

  configTime(0, 0, NET_SNTP_SERVER_1, NET_SNTP_SERVER_2);
  uint32_t start = millis();
  while (millis() - start < NET_SNTP_TIMEOUT_MS) {
    feedWdt();
    delay(100);
    now = time(nullptr);
    if (now > NET_TIME_SANE_EPOCH) return true;
  }
  return false;
}
