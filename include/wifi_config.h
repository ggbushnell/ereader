#pragma once

#include <stdint.h>

// WiFi store sizing, kept out of config.h on purpose.
//
// WIFI_STORE_MAX_NETWORKS supersedes WIFI_MAX_NETWORKS in include/config.h.
// Every module that touches the credential store (wifi_store.cpp, net.cpp,
// wifi_check.cpp, wifi_setup.cpp) uses this value; the old constant is left in
// config.h untouched and is no longer the limit.
//
// 8 entries is safe for the NVS key naming: the store writes "ssid0".."ssid7"
// and "pw0".."pw7", both well inside the 15 character Preferences key cap.
static const uint8_t WIFI_STORE_MAX_NETWORKS = 8;
