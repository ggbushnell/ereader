#pragma once

#include <Arduino.h>

// Last work RAM snapshot of a running Gen 1 Pokemon game plus typed decoders.
//
// The browser page posts the emulator's whole 8 KB work RAM (GB 0xC000..0xDFFF)
// to POST /api/wram every few seconds while a Game Boy game runs; see
// src/games.cpp for the route and SPEC.md ("Games server") for the contract.
// The page knows nothing about Pokemon: every address below lives here.
//
// Addresses come from pokered wram.asm and the plan in
// docs/pokemon-aux-display-plan-2026-09-12.md. A read outside the snapshot,
// or a read before the first snapshot arrived, returns zeros rather than
// garbage, so a view never has to check wramValid() on every field.

namespace pokemon {

static const size_t WRAM_BYTES = 0x2000;   // 0xC000 .. 0xDFFF
static const uint16_t WRAM_BASE = 0xC000;

// ---------------------------------------------------------------- snapshot

// The buffer the HTTP route writes into. Always WRAM_BYTES long.
uint8_t *wramBuffer();
// Marks the buffer as a fresh snapshot: stamps the millis clock and bumps the
// counter. `len` must be WRAM_BYTES.
void wramCommit(size_t len);
// Forgets the last snapshot, so a restarted games server shows its status
// screen until the page sends a fresh one.
void wramInvalidate();

const uint8_t *wram();
bool wramValid();
uint32_t wramStamp();     // millis() of the last commit
uint32_t wramCounter();
// Hash of the WRAM ranges the companion views draw from (party, bag, money,
// map and coordinates, battle structs, play time to the minute). The screen
// only redraws when this changes, since work RAM itself changes every frame.
uint32_t viewSignature();   // bumps once per snapshot, for redraw checks

// One byte at a GB address, 0 when out of range or before the first snapshot.
uint8_t rd(uint16_t addr);
uint16_t rdBE(uint16_t addr);   // big endian word, as the party structs store

// ---------------------------------------------------------------- text

// Gen 1 text to ASCII. Stops at 0x50 or after `max` bytes. Multi character
// codes ("PK", "MN") expand; unknown codes become '?'.
String textDecode(uint16_t addr, size_t max);

// ASCII to a Gen 1 character code, for indexing the font tiles in the pack.
// The inverse of the single character part of textDecode. Returns 0x7F (space)
// for anything with no Gen 1 glyph.
uint8_t gen1Encode(char c);

// ---------------------------------------------------------------- decoders

struct Mon {
  uint8_t species;     // internal index, not dex number
  uint16_t hp;
  uint16_t maxHp;
  uint8_t level;
  uint8_t status;
  uint8_t type1;       // current types, as the struct carries them
  uint8_t type2;
  uint8_t moves[4];
  uint8_t pp[4];
  char nick[12];       // decoded, NUL terminated
};

// A battle struct (29 bytes), which is what both sides of a fight use. Same
// fields as Mon plus the in-battle stat words and the catch rate.
struct BattleMon {
  uint8_t species;
  uint16_t hp;
  uint16_t maxHp;
  uint8_t level;
  uint8_t status;
  uint8_t type1;
  uint8_t type2;
  uint8_t catchRate;
  uint8_t moves[4];
  uint8_t pp[4];
  uint16_t atk;
  uint16_t def;
  uint16_t spd;
  uint16_t spc;
  char nick[12];
};

struct EnemyMon {
  uint8_t species;
  uint16_t hp;
  uint16_t maxHp;
  uint8_t level;
  uint8_t status;
  uint8_t moves[4];
  char nick[12];
};

struct PlayTime {
  uint8_t hours;
  uint8_t minutes;
  uint8_t seconds;
};

String playerName();
String rivalName();

uint8_t partyCount();               // 0..6
bool partyMon(int index, Mon &out);  // false when index >= partyCount()

uint32_t money();      // 3 byte BCD
uint16_t coins();      // 2 byte BCD
uint8_t badges();      // bit per badge, bit 0 = Boulder
int badgeCount();

uint8_t curMap();
uint8_t playerX();
uint8_t playerY();

PlayTime playTime();

int dexOwned();   // popcount of the 19 byte owned bitfield
int dexSeen();

uint8_t bagCount();                                  // 0..20
bool bagItem(int index, uint8_t &id, uint8_t &qty);   // false past the count

uint8_t inBattle();   // 0 none, 1 wild, 2 trainer
uint8_t opponent();
bool enemyMon(EnemyMon &out);   // false when not in battle

uint8_t repelSteps();
uint8_t boxNumber();   // 1..12
uint8_t boxCount();    // mons in the current box
bool dayCareInUse();

// ------------------------------------------------- companion screen extras
//
// Everything the four views in src/pokemon_views.cpp read that the first pass
// of this decoder did not carry.

uint16_t playerId();         // the 5 digit trainer id
bool dexOwnedBit(uint8_t dex);
bool dexSeenBit(uint8_t dex);

String otName(int index);    // original trainer of party slot `index`
uint16_t otId(int index);    // and their 5 digit id

// The overworld player sprite's facing direction byte: 0 down, 4 up, 8 left,
// 12 right. The terrain view picks Red's frame from it.
uint8_t playerFacing();

uint8_t partyMonNumber();    // 0 based party slot that is out in a battle
uint32_t partyExp(int index); // total experience of party slot `index`

bool enemyBattleMon(BattleMon &out);    // wEnemyMon, false when not in battle
bool playerBattleMon(BattleMon &out);   // wBattleMon, same

uint8_t trainerClass();       // 0 when the battle is not a trainer battle
uint8_t enemyPartyCount();    // mons the opposing trainer brought
int enemyAliveCount();        // of those, the ones that are not fainted

uint8_t pcItemCount();                                // 0..50
bool pcItem(int index, uint8_t &id, uint8_t &qty);

uint8_t boxMonSpecies(int index);   // internal index, 0 past the box count
uint8_t boxMonLevel(int index);

String dayCareName();
uint8_t dayCareLevel();

// Pickup flags. `objectTaken` is the placed item ball flag array (this pokered
// checkout calls them toggleable objects, the Gen 2 name is missable objects);
// `hiddenItemTaken` is the ground item flag array. Both index bit (i & 7) of
// byte (i >> 3), and both indices come out of the pack's items_placed and
// items_hidden records.
bool objectTaken(uint8_t index);
bool hiddenItemTaken(uint8_t index);

// The Fly list: one bit per city map (map ids 0..10, Pallet Town first), set
// the first time the player walks into that town.
bool townVisited(uint8_t cityMap);

// One of pokered's wEventFlags (story progress): bit `event` as numbered by
// constants/event_constants.asm. False past the array.
bool eventFlag(uint16_t event);

}  // namespace pokemon
