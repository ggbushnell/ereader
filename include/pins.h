#pragma once

// Single source of truth for all GPIO allocation (ESP32-S3, lolin_s3_mini
// board definition).
//
// Pins avoided on purpose:
//   0, 3, 45, 46  strapping pins (boot mode and log level)
//   19, 20        native USB D-/D+
//   33 through 37 claimed by octal PSRAM on N16R8 style boards
//   43, 44        UART0 TX/RX
// Everything below is free on both the N8R2 and the N16R8 bench boards.

// E-paper: Good Display GDEH0576T81 (5.76 inch, 920x680, 1 bit) on a DESPI-C02
// adapter. Same SPI assignment as the Muon firmware and the shelf_display
// project, so this is a known-good map. The adapter labels its header BUSY,
// RES, D/C, CS, SCK, SDI, GND, VCC; set its RESE switch to 0.47.
// Re-cabled 2026-09-09 on the second N8R2 board (header pin 15 fell out, so
// nothing lives on 15). Allocated from the 17 end of the right-hand header
// row; 11, 12, 13 are spare, 3 is left alone as a strapping pin.
static const int PIN_EINK_MOSI = 17;  // adapter SDI
static const int PIN_EINK_SCK = 18;   // adapter SCK
static const int PIN_EINK_CS = 8;     // adapter CS
static const int PIN_EINK_DC = 9;     // adapter D/C
static const int PIN_EINK_RST = 10;   // adapter RES
static const int PIN_EINK_BUSY = 46;  // adapter BUSY (strapping pin; idles LOW at reset so download mode still works)
static const int PIN_EINK_MISO = -1;  // panel is write-only; no MISO wired

// The DESPI-C02 has no panel power enable line: VCC to 3V3 and the panel is
// live, so there is nothing to drive in setup().

// I2C, BME280. No sensor is fitted in the cased build (2026-09-08); the
// firmware probes and shows "n/a" when nothing answers. Pins kept so the
// probe has somewhere harmless to point (12/13 are spare on this build).
static const int PIN_BME_SDA = 12;
static const int PIN_BME_SCL = 13;

// Buttons to GND, INPUT_PULLUP, active LOW.
// Names are physical directions with the device held in portrait. The d-pad
// module is mounted rotated 90 degrees, so its silkscreen labels do not match:
// module RIGHT points up, LEFT points down, UP points left, DOWN points right.
// Moved to the far side of the module 2026-09-08 after the GPIO 15 trace was
// lifted and 5/6 were found bridged. GPIO 15 is dead on this board; never
// assign it. Header pins RX (44) and TX (43) are also free (UART0 is unused,
// logging goes over native USB CDC) and can stand in if reach is easier.
static const int PIN_BTN_UP = 40;     // module RIGHT
static const int PIN_BTN_DOWN = 41;   // module LEFT
static const int PIN_BTN_CENTER = 42;
static const int PIN_BTN_LEFT = 1;    // module UP
static const int PIN_BTN_RIGHT = 2;   // module DOWN
