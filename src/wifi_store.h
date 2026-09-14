#pragma once

#include <Arduino.h>

#include <vector>

// WiFi credential store, backed by NVS (Preferences) so it survives both a
// firmware upload and a `pio run -t uploadfs`, which rewrites the LittleFS
// partition and would wipe anything kept in a file.
//
// NVS namespace "wifi":
//   "n"      u8, number of stored entries (0 to WIFI_STORE_MAX_NETWORKS)
//   "ssid0"  String, SSID of entry 0 (ssid1 .. ssid7 for the rest)
//   "pw0"    String, passphrase of entry 0 (pw1 .. pw7 for the rest)
//
// The store size is WIFI_STORE_MAX_NETWORKS in include/wifi_config.h, which
// supersedes WIFI_MAX_NETWORKS in include/config.h.
//
// Entry 0 is the most recently saved network. Adding a network that is already
// stored replaces it in place, so a password change does not eat a slot.

namespace wifi_store {

struct Net {
  String ssid;
  String pass;
};

// Reads every stored entry. Order is newest first.
std::vector<Net> list();

int count();

// Saves a network. An existing SSID is replaced, otherwise the network becomes
// entry 0 and the oldest entry falls off the end once the store is full.
// Returns false only if the SSID is empty or NVS refused the write.
bool add(const String &ssid, const String &pass);

// Removes a stored SSID. Returns false if it was not there.
bool remove(const String &ssid);

void clear();

// Passphrase for a stored SSID, empty if unknown.
String passwordFor(const String &ssid);

}  // namespace wifi_store
