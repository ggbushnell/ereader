#pragma once

#include <Arduino.h>

#include "pokemon_state.h"

// Game facts from the pack and the snapshot, with no drawing in them: names
// and tables out of the EPK1 pack, and the calculations the companion views
// make from them (type match-ups, capture odds, experience curves, encounter
// tables, pickups). src/pokemon_views.cpp draws from these; the native build
// also serialises them, so a browser or any other front end gets exactly the
// facts the e-paper shows.

namespace pokedata {

uint16_t le16(const uint8_t *p);
uint32_t le32(const uint8_t *p);

// One string out of a STRTAB section: u16 count, u16 offset per entry, then
// zero terminated strings. Empty when absent.
String packString(const char *section, int index, size_t cap = 32);

// ---- species, types, moves, items, maps
uint8_t dexOf(uint8_t internalIndex);      // 0 when unknown
String speciesNameByDex(uint8_t dex);
String speciesName(uint8_t internalIndex);
bool baseStats(uint8_t dex, uint8_t out[9]);   // hp atk def spd spc type1 type2 catch picsize
uint8_t growthRate(uint8_t dex);           // GROWTH_* index, 0 when the pack has no growth section
String typeName(uint8_t type);
String typeLine(uint8_t t1, uint8_t t2);   // "NORMAL/FLYING" or "NORMAL"
String itemName(uint8_t id);
uint16_t itemPrice(uint8_t id);
String moveName(uint8_t id);
bool moveData(uint8_t id, uint8_t out[4]); // type power accuracy pp
String mapLabel(uint16_t map);
String trainerClassName(uint8_t cls);
const char *statusText(uint8_t status, bool fainted);   // "SLP" "PSN" "BRN" "FRZ" "PAR" "FNT" ""

// ---- type chart
int chartMul(uint8_t atk, uint8_t def);                     // x10
int effPercent(uint8_t moveType, uint8_t d1, uint8_t d2);  // 0 25 50 100 200 400
String effBadge(int percent);                               // "x2 " "1/2" "x0 " "1/4" "x4 " or ""
// The game's real type ids (BIRD and the gaps skipped): 15 in Gen 1, 17 in
// Gen 2 (adds STEEL 9 and DARK 27). `n` receives the count.
const uint8_t *typeIds(int &n);
// Types that hit a (t1, t2) defender for more than, less than, or zero
// damage. Each out array holds up to 15 ids; returns are counts.
struct Matchups {
  uint8_t weak[17], resist[17], immune[17];
  int nWeak, nResist, nImmune;
};
Matchups matchups(uint8_t t1, uint8_t t2);

// ---- maps
struct MapInfo {
  uint32_t offset;
  uint8_t w, h, tileset, border;
  bool ok;
};
MapInfo mapInfo(uint16_t map);

// ---- wild encounters
struct Encounter {
  uint8_t species;        // internal index
  uint8_t levelLo, levelHi;
  int odds;               // percent, slots merged by species
};
extern const int SLOT_ODDS[10];
int mergeSlots(const uint8_t *slots, Encounter *out);   // 20 bytes of (level, species) -> <=10
struct WildTable {
  bool ok;                // the map has a table at all
  uint8_t grassRate, waterRate;
  Encounter grass[10], water[10];
  int nGrass, nWater;
};
WildTable wildTable(uint16_t map);

// ---- pickups on a map (item balls and hidden items), with the taken flag
struct MapItem {
  uint8_t x, y, item;
  uint16_t flag;   // the flag index the pack gave (Gen 2: an event number)
  bool hidden;
  bool taken;
};
int collectItems(uint16_t map, MapItem *out, int cap);

// ---- battle arithmetic
enum class Ball : uint8_t { POKE, GREAT, ULTRA };
// One throw at the enemy as it stands, whole percent, exactly ItemUseBall.
int catchPercent(const pokemon::BattleMon &e, Ball ball);
// Experience to reach level n under GROWTH_* index g, as CalcExperience.
long expForLevel(int g, int n);
// Experience still needed by party slot `slot` to reach the next level:
// -1 unknown (no pack growth data), 0 at level 100 or when already there.
long expToNextLevel(int slot, uint8_t internalIndex, uint8_t level);

// ---- HP bar (the game's own rule: 48 px wide, colour by GetHealthBarColor)
int hpFill(int hp, int maxHp);      // 0..48
int hpColor(int fill);              // 0 green 1 yellow 2 red

// Drop cached pack sections. Call when the pack is opened or closed.
void reset();

}  // namespace pokedata
