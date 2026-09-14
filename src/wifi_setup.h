#pragma once

#include <Arduino.h>

// Captive portal WiFi setup, reached from the menu.
//
// Blocks until the user presses CENTER. Brings up a soft AP named
// WIFI_SETUP_AP_SSID with a catch all DNS server and a one page web server on
// WIFI_SETUP_AP_IP_TEXT, saves what the user types into the credential store,
// then verifies it by joining the network for real (the AP stays up while it
// tries, so the phone does not get dropped mid setup). The result is reported
// both on the phone and on the panel. The radio is off when this returns.

namespace wifi_setup {

void run();

}  // namespace wifi_setup
