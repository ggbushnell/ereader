#include "wifi_store.h"

#include <Preferences.h>

#include "config.h"
#include "wifi_config.h"

namespace {

// Preferences keys are capped at 15 characters, so short names it is.
String ssidKey(int i) { return "ssid" + String(i); }
String passKey(int i) { return "pw" + String(i); }

// Opening and closing around every call keeps the NVS handle out of the way of
// the rest of the firmware; these calls happen a handful of times per session.
class Store {
 public:
  Store(bool readOnly) { ok_ = prefs_.begin(NVS_NS_WIFI, readOnly); }
  ~Store() { if (ok_) prefs_.end(); }
  bool ok() const { return ok_; }
  Preferences &p() { return prefs_; }

 private:
  Preferences prefs_;
  bool ok_ = false;
};

int clampCount(int n) {
  if (n < 0) return 0;
  if (n > (int)WIFI_STORE_MAX_NETWORKS) return (int)WIFI_STORE_MAX_NETWORKS;
  return n;
}

}  // namespace

namespace wifi_store {

std::vector<Net> list() {
  std::vector<Net> out;
  Store s(true);
  if (!s.ok()) return out;
  int n = clampCount(s.p().getUChar(NVS_KEY_WIFI_COUNT, 0));
  for (int i = 0; i < n; i++) {
    Net net;
    net.ssid = s.p().getString(ssidKey(i).c_str(), "");
    net.pass = s.p().getString(passKey(i).c_str(), "");
    if (net.ssid.length()) out.push_back(net);
  }
  return out;
}

int count() {
  return (int)list().size();
}

bool add(const String &ssid, const String &pass) {
  if (!ssid.length()) return false;

  std::vector<Net> nets = list();
  // Drop any existing copy of this SSID, then put the new one at the front.
  for (size_t i = 0; i < nets.size(); i++) {
    if (nets[i].ssid == ssid) {
      nets.erase(nets.begin() + i);
      break;
    }
  }
  Net fresh;
  fresh.ssid = ssid;
  fresh.pass = pass;
  nets.insert(nets.begin(), fresh);
  if (nets.size() > WIFI_STORE_MAX_NETWORKS) nets.resize(WIFI_STORE_MAX_NETWORKS);

  Store s(false);
  if (!s.ok()) return false;
  for (size_t i = 0; i < nets.size(); i++) {
    s.p().putString(ssidKey((int)i).c_str(), nets[i].ssid);
    s.p().putString(passKey((int)i).c_str(), nets[i].pass);
  }
  for (size_t i = nets.size(); i < WIFI_STORE_MAX_NETWORKS; i++) {
    s.p().remove(ssidKey((int)i).c_str());
    s.p().remove(passKey((int)i).c_str());
  }
  s.p().putUChar(NVS_KEY_WIFI_COUNT, (uint8_t)nets.size());
  return true;
}

bool remove(const String &ssid) {
  std::vector<Net> nets = list();
  bool found = false;
  for (size_t i = 0; i < nets.size(); i++) {
    if (nets[i].ssid == ssid) {
      nets.erase(nets.begin() + i);
      found = true;
      break;
    }
  }
  if (!found) return false;

  Store s(false);
  if (!s.ok()) return false;
  for (size_t i = 0; i < nets.size(); i++) {
    s.p().putString(ssidKey((int)i).c_str(), nets[i].ssid);
    s.p().putString(passKey((int)i).c_str(), nets[i].pass);
  }
  for (size_t i = nets.size(); i < WIFI_STORE_MAX_NETWORKS; i++) {
    s.p().remove(ssidKey((int)i).c_str());
    s.p().remove(passKey((int)i).c_str());
  }
  s.p().putUChar(NVS_KEY_WIFI_COUNT, (uint8_t)nets.size());
  return true;
}

void clear() {
  Store s(false);
  if (!s.ok()) return;
  s.p().clear();
}

String passwordFor(const String &ssid) {
  std::vector<Net> nets = list();
  for (size_t i = 0; i < nets.size(); i++) {
    if (nets[i].ssid == ssid) return nets[i].pass;
  }
  return String();
}

}  // namespace wifi_store
