#pragma once

#include <Arduino.h>

// Achievements for the Gen 1 companion screen, judged from the work RAM
// snapshot alone: story events (pokered's wEventFlags), badges, Pokedex
// counts, party, levels, money, towns visited, play time. No emulator
// support is needed, which is why RetroAchievements proper cannot run here
// and this can.
//
// "Earned" is always the live condition. What is persisted is when each was
// first seen true (play time, minutes) plus the player id the stamps belong
// to, in GAMES_ACHIEVEMENTS_PATH; a different trainer id starts the file
// over. A condition seen true with no stamp is newly earned: it is stamped,
// and shown as a toast for ACHIEVEMENT_TOAST_SEC of play time, except on the
// very first evaluation of a file that did not exist (a fresh device shows
// no toasts for a save that is already deep into the game).

namespace achievements {

enum class Kind : uint8_t {
  EVENT,       // wEventFlags bit `arg`
  BADGES,      // badgeCount() >= arg
  DEX_OWNED,   // dexOwned() >= arg
  DEX_SEEN,    // dexSeen() >= arg
  PARTY,       // partyCount() >= arg
  LEVEL,       // any party member at level >= arg
  MONEY,       // money() >= arg
  TOWNS,       // towns in the Fly list >= arg
  PLAY_HOURS,  // play time hours >= arg
};

struct Def {
  const char *title;   // at most 13 Gen 1 font cells
  const char *hint;    // what to do, at most 30 cells
  Kind kind;
  uint32_t arg;
  bool story;          // the ordered main line; the first unearned one is "next"
};

int count();
const Def &def(int index);
bool earned(int index);
uint16_t earnedAtMinutes(int index);   // play time when first seen, 0xFFFF if not
int earnedCount();
int storyCount();
int nextStory();                       // first unearned story index, -1 when done

// Re-judges every achievement against the current snapshot, stamps new ones
// and writes the file when anything changed. Returns how many are new.
int update();

// Index of the achievement to show as a toast right now, -1 for none.
int toast();

// Reads the stamp file (or starts empty). Call once the filesystem is up and
// again after the pack or a save changes; reset() forgets everything in RAM.
void load();
void reset();

}  // namespace achievements
