#pragma once

#include <Arduino.h>

enum Button : uint8_t {
  BTN_UP = 0,
  BTN_DOWN,
  BTN_LEFT,
  BTN_RIGHT,
  BTN_CENTER,
  BTN_COUNT
};

namespace input {

void begin();

// Polls all buttons, debounced. Returns true and sets `out` when a button was
// pressed since the previous poll. One event per call.
bool poll(Button &out);

// Drops anything the buttons did while the caller was not polling, and
// re-syncs the debouncer to the levels the pins read right now.
//
// A panel refresh blocks the loop for half a second or more, and the reader is
// deaf for all of it. Two things then arrive at the next poll: a press the user
// made while the frame was drawing (which reads as a queued, surprising second
// page turn) and any edge the refresh itself coupled into the button wiring,
// which lands on whichever pin runs next to the one that was pressed and turns
// a page the other way. Call this once every render returns so a press is only
// ever a press the reader was awake for.
void flush();

// Presses that happened while the loop was busy (a long companion draw) are
// remembered by a pin interrupt: any pad held for at least BUTTON_LATCH_MS
// sets a latch. takeLatched() hands back one latched press and clears it.
// flush() leaves the latch alone; callers that want the old "deaf while
// drawing" behaviour simply never call takeLatched().
bool takeLatched(Button &out);
void clearLatched();

// Raw debounced state of every button as a bitmask, bit i = Button i pressed.
// This is the unfiltered pad, not the event queue: the button test screen uses
// it to show which lines a single press actually pulls.
uint8_t rawMask();

// Same, sampled straight off the pins with no debounce.
uint8_t liveMask();

// Turned pad. When the picture is turned 180 degrees away from
// DISPLAY_ROTATION ("Flip screen"), the pad is turned with it: every event and
// mask above reports UP as DOWN, LEFT as RIGHT and the reverse, so the rest of
// the firmware keeps thinking in directions as the reader sees them.
void setTurned(bool turned);
bool turned();

// The GPIO a logical button currently reads, with the turn applied.
int gpioFor(Button b);

}  // namespace input
