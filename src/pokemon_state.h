#pragma once

#include <Arduino.h>

// Last work RAM snapshot of a running Gen 1 or Gen 2 Pokemon game plus typed
// decoders.
//
// The browser page (or the device poller) posts the emulator's 8 KB work RAM
// (GB 0xC000..0xDFFF) to POST /api/wram every second; see src/games.cpp for
// the route and SPEC.md ("Games server") for the contract. The page knows
// nothing about Pokemon: every address lives here, in one layout table per
// game. Gen 1 addresses come from pret/pokered, Gen 2 (Gold/Silver, one
// engine) from a pret/pokegold build that is byte-identical to the retail
// ROM; both games keep everything the companion needs in WRAM banks 0-1, so
// the same 8 KB mirror serves both.
//
// A read outside the snapshot, or before the first snapshot arrived, returns
// zeros rather than garbage, so a view never has to check wramValid().

namespace pokemon {

static const size_t WRAM_BYTES = 0x2000;   // 0xC000 .. 0xDFFF
static const uint16_t WRAM_BASE = 0xC000;

// ---------------------------------------------------------------- game

enum class Game : uint8_t { GEN1, GEN2 };
void setGame(Game g);      // picks the layout table; GEN1 until told otherwise
Game game();
int speciesCount();        // 151 or 251
int badgeTotal();          // 8 or 16

// ---------------------------------------------------------------- snapshot

uint8_t *wramBuffer();
void wramCommit(size_t len);
void wramInvalidate();
const uint8_t *wram();
bool wramValid();
uint32_t wramStamp();
uint32_t wramCounter();
uint32_t viewSignature();

uint8_t rd(uint16_t addr);
uint16_t rdBE(uint16_t addr);

// ---------------------------------------------------------------- text

String textDecode(uint16_t addr, size_t max);
uint8_t gen1Encode(char c);

// ---------------------------------------------------------------- decoders

struct Mon {
  uint8_t species;     // internal index (Gen 1) or dex number (Gen 2: they coincide)
  uint16_t hp;
  uint16_t maxHp;
  uint8_t level;
  uint8_t status;
  bool hasTypes;       // Gen 1 party structs carry types; Gen 2's do not (0xFF)
  uint8_t type1;
  uint8_t type2;
  uint8_t moves[4];
  uint8_t pp[4];
  uint8_t item;        // held item (Gen 2), 0 in Gen 1
  uint8_t happiness;   // Gen 2, 0 in Gen 1
  uint8_t dvAtk, dvDef, dvSpd, dvSpc;
  char nick[12];
};

struct BattleMon {
  uint8_t species;
  uint16_t hp;
  uint16_t maxHp;
  uint8_t level;
  uint8_t status;
  uint8_t type1;
  uint8_t type2;
  uint8_t catchRate;
  uint8_t item;        // Gen 2
  uint8_t moves[4];
  uint8_t pp[4];
  uint16_t atk;
  uint16_t def;
  uint16_t spd;
  uint16_t spc;        // Gen 1 Special, Gen 2 Special Attack
  uint16_t spdef;      // Gen 2 Special Defense, 0 in Gen 1
  uint8_t dvAtk, dvDef, dvSpd, dvSpc;
  uint8_t dvHp;
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
  uint16_t hours;      // Gen 2 stores a word
  uint8_t minutes;
  uint8_t seconds;
};

String playerName();
String rivalName();

uint8_t partyCount();
bool partyMon(int index, Mon &out);

uint32_t money();      // Gen 1: 3 byte BCD; Gen 2: 3 byte big endian
uint16_t coins();
uint16_t badges();     // bit per badge; Gen 2: Johto in the low byte, Kanto in the high
int badgeCount();

// Map identity. Gen 1: the map id. Gen 2: (group << 8) | number, which is
// also how the Gen 2 pack keys its map tables.
uint16_t curMap();
uint8_t curMapGroup();     // Gen 2 only, 0 in Gen 1
uint8_t curMapNumber();
uint8_t playerX();
uint8_t playerY();
uint8_t timeOfDay();       // Gen 2: 0 morn, 1 day, 2 nite; Gen 1: 1

PlayTime playTime();

int dexOwned();
int dexSeen();

uint8_t bagCount();
bool bagItem(int index, uint8_t &id, uint8_t &qty);
// Gen 2 keeps Balls in their own pocket; Gen 1 reports 0 here.
uint8_t ballCount();
bool ballItem(int index, uint8_t &id, uint8_t &qty);

uint8_t inBattle();   // 0 none, 1 wild, 2 trainer
uint8_t opponent();
bool enemyMon(EnemyMon &out);

uint8_t repelSteps();
uint8_t boxNumber();
uint8_t boxCount();    // Gen 2: 0 (the box lives in SRAM)
bool dayCareInUse();

// ------------------------------------------------- companion screen extras

uint16_t playerId();
bool dexOwnedBit(uint8_t dex);
bool dexSeenBit(uint8_t dex);

String otName(int index);
uint16_t otId(int index);
uint32_t partyExp(int index);

// 0 down, 4 up, 8 left, 12 right in both games.
uint8_t playerFacing();

uint8_t partyMonNumber();

bool enemyBattleMon(BattleMon &out);
bool playerBattleMon(BattleMon &out);

uint8_t trainerClass();
uint8_t enemyPartyCount();
int enemyAliveCount();

uint8_t pcItemCount();
bool pcItem(int index, uint8_t &id, uint8_t &qty);

uint8_t boxMonSpecies(int index);
uint8_t boxMonLevel(int index);

String dayCareName();
uint8_t dayCareLevel();

// Pickup flags, indexed as the pack's items_placed / items_hidden records say.
// Gen 1: two flag arrays (toggleable objects, hidden items). Gen 2: both are
// plain event flags, so the pack stores event numbers and these read them.
bool objectTaken(uint16_t index);
bool hiddenItemTaken(uint16_t index);

// Gen 1: the Fly list, one bit per city map (0..10). Gen 2: wVisitedSpawns,
// one bit per spawn point (0..27; 2 Kanto-side specials, Kanto 2..13,
// Johto 14..27).
bool townVisited(uint8_t index);
int townSlots();           // 11 or 28

// Story progress bits: pokered wEventFlags (2560) / pokegold (800).
bool eventFlag(uint16_t event);

}  // namespace pokemon
