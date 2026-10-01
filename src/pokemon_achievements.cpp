#include "pokemon_achievements.h"

#include <LittleFS.h>

#include "config.h"
#include "pokemon_state.h"

namespace {

using achievements::Def;
using achievements::Kind;

// Event numbers are pokered constants/event_constants.asm, counted with its
// const_def / const_next / const_skip directives (see docs or
// native/tools). Story entries are in the order the game expects them.
const Def DEFS[] = {
    // ---- story
    {"OAKS PARCEL", "DELIVER THE PARCEL TO OAK", Kind::EVENT, 56, true},
    {"FIRST PARTNER", "CHOOSE A STARTER IN OAKS LAB", Kind::EVENT, 34, true},
    {"POKEDEX", "GET THE POKEDEX FROM OAK", Kind::EVENT, 37, true},
    {"BOULDER BADGE", "BEAT BROCK IN PEWTER CITY", Kind::EVENT, 119, true},
    {"CASCADE BADGE", "BEAT MISTY IN CERULEAN CITY", Kind::EVENT, 191, true},
    {"SS TICKET", "HELP BILL NORTH OF CERULEAN", Kind::EVENT, 1372, true},
    {"HM01 CUT", "FIND THE CAPTAIN ON THE SS ANNE", Kind::EVENT, 1504, true},
    {"THUNDER BADGE", "BEAT LT SURGE IN VERMILION", Kind::EVENT, 359, true},
    {"RAINBOW BADGE", "BEAT ERIKA IN CELADON CITY", Kind::EVENT, 425, true},
    {"ROCKET HQ", "CLEAR THE GAME CORNER HIDEOUT", Kind::EVENT, 1703, true},
    {"HM02 FLY", "THE HOUSE WEST ON ROUTE 16", Kind::EVENT, 1230, true},
    {"MR FUJI SAVED", "CLIMB POKEMON TOWER IN LAVENDER", Kind::EVENT, 1231, true},
    {"POKE FLUTE", "VISIT MR FUJI AFTER THE TOWER", Kind::EVENT, 298, true},
    {"SILPH CO", "CLEAR SILPH CO IN SAFFRON", Kind::EVENT, 1935, true},
    {"MARSH BADGE", "BEAT SABRINA IN SAFFRON CITY", Kind::EVENT, 865, true},
    {"SOUL BADGE", "BEAT KOGA IN FUCHSIA CITY", Kind::EVENT, 601, true},
    {"HM03 SURF", "THE SECRET HOUSE IN THE SAFARI", Kind::EVENT, 2176, true},
    {"VOLCANO BADGE", "BEAT BLAINE ON CINNABAR ISLAND", Kind::EVENT, 665, true},
    {"EARTH BADGE", "BEAT GIOVANNI IN VIRIDIAN GYM", Kind::EVENT, 81, true},
    {"RIVAL REMATCH", "HE BLOCKS ROUTE 22 TO THE LEAGUE", Kind::EVENT, 1318, true},
    {"LORELEI", "ELITE FOUR 1 AT INDIGO PLATEAU", Kind::EVENT, 2273, true},
    {"BRUNO", "ELITE FOUR 2 AT INDIGO PLATEAU", Kind::EVENT, 2281, true},
    {"AGATHA", "ELITE FOUR 3 AT INDIGO PLATEAU", Kind::EVENT, 2289, true},
    {"LANCE", "ELITE FOUR 4 AT INDIGO PLATEAU", Kind::EVENT, 2302, true},
    {"CHAMPION", "BEAT YOUR RIVAL ONE LAST TIME", Kind::EVENT, 2305, true},
    {"HALL OF FAME", "ENTER THE HALL OF FAME", Kind::EVENT, 3, true},
    {"MEWTWO", "CERULEAN CAVE AFTER THE LEAGUE", Kind::EVENT, 2241, true},
    // ---- milestones
    {"FIRST CATCH", "CATCH A SECOND POKEMON", Kind::DEX_OWNED, 2, false},
    {"RIVAL ROUTE22", "WEST OF VIRIDIAN, BEFORE BROCK", Kind::EVENT, 1317, false},
    {"FULL PARTY", "CARRY SIX POKEMON", Kind::PARTY, 6, false},
    {"SEEN 25", "SEE 25 SPECIES", Kind::DEX_SEEN, 25, false},
    {"SEEN 75", "SEE 75 SPECIES", Kind::DEX_SEEN, 75, false},
    {"SEEN 150", "SEE 150 SPECIES", Kind::DEX_SEEN, 150, false},
    {"OWNED 25", "OWN 25 SPECIES", Kind::DEX_OWNED, 25, false},
    {"OWNED 50", "OWN 50 SPECIES", Kind::DEX_OWNED, 50, false},
    {"OWNED 100", "OWN 100 SPECIES", Kind::DEX_OWNED, 100, false},
    {"OWNED 150", "OWN 150 SPECIES", Kind::DEX_OWNED, 150, false},
    {"LEVEL 20", "RAISE A POKEMON TO LEVEL 20", Kind::LEVEL, 20, false},
    {"LEVEL 50", "RAISE A POKEMON TO LEVEL 50", Kind::LEVEL, 50, false},
    {"LEVEL 100", "RAISE A POKEMON TO LEVEL 100", Kind::LEVEL, 100, false},
    {"$10000", "HOLD 10000 POKE DOLLARS", Kind::MONEY, 10000, false},
    {"$100000", "HOLD 100000 POKE DOLLARS", Kind::MONEY, 100000, false},
    {"5 TOWNS", "VISIT FIVE TOWNS", Kind::TOWNS, 5, false},
    {"ALL TOWNS", "VISIT ALL ELEVEN TOWNS", Kind::TOWNS, 11, false},
    {"BICYCLE", "TRADE THE VOUCHER IN CERULEAN", Kind::EVENT, 192, false},
    {"ZAPDOS", "THE POWER PLANT PAST ROUTE 10", Kind::EVENT, 1129, false},
    {"MOLTRES", "DEEP IN VICTORY ROAD", Kind::EVENT, 1342, false},
    {"ARTICUNO", "THE BOTTOM OF SEAFOAM ISLANDS", Kind::EVENT, 2522, false},
    {"10 HOURS", "PLAY FOR TEN HOURS", Kind::PLAY_HOURS, 10, false},
    {"40 HOURS", "PLAY FOR FORTY HOURS", Kind::PLAY_HOURS, 40, false},
};
const int N = sizeof(DEFS) / sizeof(DEFS[0]);

// ---- persisted
//   0  4  magic "EACH"
//   4  2  player id (little endian) the stamps belong to
//   6  2  index of the last achievement earned, 0xFFFF none
//   8  4  play time in seconds when it was earned
//  12  2  number of stamps that follow; a file written for a different table
//         is started over (the stamps would belong to the wrong rows)
//  14  N*2 stamps: play time minutes when first seen, 0xFFFF not yet
const char MAGIC[4] = {'E', 'A', 'C', '2'};
const int HEADER_BYTES = 14;
const uint16_t NONE = 0xFFFF;

uint16_t stamps[N];
uint16_t fileId = 0;
uint16_t lastIndex = NONE;
uint32_t lastPlaySec = 0;
bool loaded = false;
bool fileExisted = false;

uint32_t playSeconds() {
  pokemon::PlayTime pt = pokemon::playTime();
  return (uint32_t)pt.hours * 3600u + (uint32_t)pt.minutes * 60u + pt.seconds;
}

bool judge(const Def &d) {
  switch (d.kind) {
    case Kind::EVENT: return pokemon::eventFlag((uint16_t)d.arg);
    case Kind::BADGES: return pokemon::badgeCount() >= (int)d.arg;
    case Kind::DEX_OWNED: return pokemon::dexOwned() >= (int)d.arg;
    case Kind::DEX_SEEN: return pokemon::dexSeen() >= (int)d.arg;
    case Kind::PARTY: return pokemon::partyCount() >= d.arg;
    case Kind::LEVEL: {
      int n = pokemon::partyCount();
      for (int i = 0; i < n; i++) {
        pokemon::Mon m;
        if (pokemon::partyMon(i, m) && m.level >= d.arg) return true;
      }
      return false;
    }
    case Kind::MONEY: return pokemon::money() >= d.arg;
    case Kind::TOWNS: {
      int n = 0;
      for (uint8_t c = 0; c < 11; c++) n += pokemon::townVisited(c) ? 1 : 0;
      return n >= (int)d.arg;
    }
    case Kind::PLAY_HOURS: return pokemon::playTime().hours >= d.arg;
  }
  return false;
}

void clearStamps() {
  for (int i = 0; i < N; i++) stamps[i] = NONE;
  lastIndex = NONE;
  lastPlaySec = 0;
}

void save() {
  LittleFS.mkdir(GAMES_DIR);
  LittleFS.mkdir(GAMES_AUX_DIR);
  fs::File f = LittleFS.open(GAMES_ACHIEVEMENTS_PATH, "w");
  if (!f) return;
  uint8_t head[HEADER_BYTES];
  memcpy(head, MAGIC, 4);
  head[4] = (uint8_t)fileId;
  head[5] = (uint8_t)(fileId >> 8);
  head[6] = (uint8_t)lastIndex;
  head[7] = (uint8_t)(lastIndex >> 8);
  head[8] = (uint8_t)lastPlaySec;
  head[9] = (uint8_t)(lastPlaySec >> 8);
  head[10] = (uint8_t)(lastPlaySec >> 16);
  head[11] = (uint8_t)(lastPlaySec >> 24);
  head[12] = (uint8_t)N;
  head[13] = (uint8_t)(N >> 8);
  f.write(head, sizeof(head));
  for (int i = 0; i < N; i++) {
    uint8_t s[2] = {(uint8_t)stamps[i], (uint8_t)(stamps[i] >> 8)};
    f.write(s, 2);
  }
  f.close();
  fileExisted = true;
}

}  // namespace

namespace achievements {

int count() { return N; }
const Def &def(int index) { return DEFS[index < 0 || index >= N ? 0 : index]; }
bool earned(int index) {
  if (index < 0 || index >= N || !pokemon::wramValid()) return false;
  return judge(DEFS[index]);
}
uint16_t earnedAtMinutes(int index) {
  return (index < 0 || index >= N) ? NONE : stamps[index];
}
int earnedCount() {
  int n = 0;
  for (int i = 0; i < N; i++) n += earned(i) ? 1 : 0;
  return n;
}
int storyCount() {
  int n = 0;
  for (int i = 0; i < N; i++) n += DEFS[i].story ? 1 : 0;
  return n;
}
int nextStory() {
  for (int i = 0; i < N; i++) {
    if (DEFS[i].story && !earned(i)) return i;
  }
  return -1;
}

void load() {
  clearStamps();
  loaded = true;
  fileExisted = false;
  fs::File f = LittleFS.open(GAMES_ACHIEVEMENTS_PATH, "r");
  if (!f) return;
  uint8_t head[HEADER_BYTES];
  if (f.read(head, HEADER_BYTES) != HEADER_BYTES || memcmp(head, MAGIC, 4) != 0 ||
      ((int)head[12] | ((int)head[13] << 8)) != N) {
    f.close();
    return;
  }
  fileId = (uint16_t)head[4] | ((uint16_t)head[5] << 8);
  lastIndex = (uint16_t)head[6] | ((uint16_t)head[7] << 8);
  lastPlaySec = (uint32_t)head[8] | ((uint32_t)head[9] << 8) |
                ((uint32_t)head[10] << 16) | ((uint32_t)head[11] << 24);
  for (int i = 0; i < N; i++) {
    uint8_t s[2];
    if (f.read(s, 2) != 2) break;
    stamps[i] = (uint16_t)s[0] | ((uint16_t)s[1] << 8);
  }
  f.close();
  fileExisted = true;
}

void reset() {
  clearStamps();
  loaded = false;
  fileExisted = false;
}

int update() {
  if (!pokemon::wramValid()) return 0;
  if (!loaded) load();
  uint16_t id = pokemon::playerId();
  bool changed = false;
  // A different trainer: the stamps were someone else's game.
  if (fileExisted && id != fileId) {
    clearStamps();
    changed = true;
  }
  fileId = id;
  pokemon::PlayTime pt = pokemon::playTime();
  uint16_t nowMin = (uint16_t)(pt.hours * 60 + pt.minutes);
  uint32_t nowSec = playSeconds();
  int fresh = 0;
  for (int i = 0; i < N; i++) {
    if (stamps[i] != NONE || !judge(DEFS[i])) continue;
    stamps[i] = nowMin;
    changed = true;
    // A file that did not exist yet is a first look at an existing save:
    // stamp silently rather than toasting the whole backlog.
    if (fileExisted) {
      fresh++;
      lastIndex = (uint16_t)i;
      lastPlaySec = nowSec;
    }
  }
  if (changed) save();
  return fresh;
}

int toast() {
  if (lastIndex == NONE || lastIndex >= N || !pokemon::wramValid()) return -1;
  uint32_t now = playSeconds();
  if (now < lastPlaySec) return -1;   // the clock went backwards: a reload
  return (now - lastPlaySec) < ACHIEVEMENT_TOAST_SEC ? (int)lastIndex : -1;
}

}  // namespace achievements
