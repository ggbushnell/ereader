#pragma once

// "Check WiFi" pass, reached from the menu.
//
// Blocking. Brings the radio up, scans once, then tries to join every stored
// network that the scan actually saw, with a WIFI_CONNECT_TIMEOUT_MS budget
// each, and reports one line per stored network:
//
//   ssid: ok -52 dBm                 associated, scan RSSI
//   ssid: wrong password / no join   in range but the join timed out
//   ssid: not in range               no beacon in the scan, not tried
//
// Every in-range network is tried, not just up to the first success: the point
// is to find out which stored credentials are stale, not to get online. The
// radio is off when this returns. The result screen is held until a button is
// pressed.

namespace wifi_check {

void run();

}  // namespace wifi_check
