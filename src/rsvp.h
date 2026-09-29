#pragma once

#include <Arduino.h>

#include "books.h"
#include "input.h"

// Speed read: RSVP (rapid serial visual presentation) over the open book.
//
// A few words at a time are flashed in one fixed spot on the landscape panel,
// each chunk drawn in the largest Gelasio size that fits, on the partial LUT
// with only the changed pixels driven. Ported from the rsvp_eink proof of
// concept, whose timing and waveform settings were tuned on this panel (see
// the RSVP_* constants in config.h).
//
// The mode reads pages through the Book it is handed and keeps the book's
// reading position in step: whenever the chunk on glass starts on a new page,
// that page is saved exactly as a page turn would save it, so the menu and the
// normal reading view pick up where speed read left off.
//
// main.cpp owns the state machine. It calls enter(), then tick() from every
// loop() pass and handle() for every button, and leave() once handle() asks to
// exit, after which it draws the reading page with a full refresh.

namespace rsvp {

// Loads the stored speed from NVS. Call once at boot.
void begin();

// Words per minute, as shown on the menu tile.
int wpm();

// Words per flash, 1-3.
int words();

// Picker settings tiles: step words per flash 1 -> 2 -> 3 -> 1 (clamping the
// speed to that mode's top speed), and step the speed up by RSVP_WPM_STEP,
// wrapping to RSVP_WPM_MIN past the top. Both are saved to NVS.
void cycleWords();
void cycleWpm();

// In-page word to restart at when speed read is reopened on `page` of the book
// and variant it was last left on, so a pause and a look at the page does not
// lose the reader's place inside it. RAM only; 0 for anything else.
uint16_t resumeWord(const String &slug, uint8_t variant, uint32_t page);

// Starts on `book` (open, active variant already chosen) at word `word` of
// `page`. An image page or a page with no words moves on to the next page that
// has some. Switches the panel to landscape and the fast drive and shows the
// first chunk with a full refresh.
void enter(Book &book, uint32_t page, uint16_t word);

// Flashes the next chunk when it is due. Returns true when it drew a frame,
// so the caller can flush the input that piled up while the panel was busy.
bool tick();

// One button. Returns true when the reader asked to leave (CENTER while
// paused); the caller then calls leave().
bool handle(Button b);

// Puts the panel back to portrait and the normal drive, and remembers the word
// for resumeWord(). The caller follows with a full refresh of the page.
void leave();

// Page holding the first word of the chunk on glass: the reading position.
uint32_t page();

}  // namespace rsvp
