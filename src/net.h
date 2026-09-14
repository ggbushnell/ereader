#pragma once

#include <Arduino.h>

// WiFi is off whenever the reader is not syncing or in setup. These two calls
// are the only places the radio is brought up outside wifi_setup.

// Brings the radio up in station mode, scans, and joins the strongest network
// that is in the credential store. Tries the next best one if a join fails,
// each with a WIFI_CONNECT_TIMEOUT_MS budget. Returns true once associated and
// leaves the radio on; the caller owns wifi_off(). `ssidOut` receives the SSID
// that was joined.
bool wifi_connect(String *ssidOut = nullptr);

// Joins one network and waits up to WIFI_CONNECT_TIMEOUT_MS for association,
// feeding the task watchdog while it waits. Returns true if associated and
// leaves the radio joined; on failure it disconnects and returns false. The
// caller owns the radio mode: this does not bring the radio up or take it
// down. Used by wifi_connect() and by the "Check WiFi" pass in wifi_check.
bool wifi_join_one(const String &ssid, const String &pass);

// Disconnects and powers the radio down.
void wifi_off();

// Best effort clock set, so TLS certificate validity dates can be checked. The
// reader has no RTC and no NTP in its normal life, but mbedtls rejects every
// certificate as "not yet valid" while the clock still reads 1970, so a sync
// asks SNTP for the time once it is on the network. Returns true if the clock
// now looks sane (past 2025). Never blocks for more than about 5 seconds.
bool net_time_sync();
