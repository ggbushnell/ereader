#pragma once

#include <Arduino.h>

// Games server, reached from the menu.
//
// Blocks until the user presses CENTER or the server has seen no request for
// GAMES_IDLE_TIMEOUT_MS. Joins the stored WiFi (if any) and raises the
// GAMES_AP_SSID access point, then serves the browser emulator page embedded
// in the firmware (www/dist, see platformio.ini board_build.embed_files), the
// ROMs under GAMES_ROM_DIR and the save files under GAMES_SAVE_DIR. All
// emulation happens in the browser; the reader only stores bytes. The HTTP
// contract is documented in SPEC.md ("Games server"). The radio is off when
// this returns.

namespace games {

void run();

// Number of ROM files under GAMES_ROM_DIR, for the menu tile.
size_t romCount();

}  // namespace games
