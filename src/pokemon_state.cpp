#include "pokemon_state.h"

namespace {

uint8_t buf[pokemon::WRAM_BYTES];
bool valid = false;
uint32_t stamp = 0;
uint32_t counter = 0;

// ------------------------------------------------------------ layouts
//
// One table per game. Every entry is a raw GB address or a struct offset;
// pokemon::rd turns addresses into offsets into the snapshot. 0 means "this
// game has no such thing".

struct Layout {
  // identity and party
  uint16_t playerId, playerName, rivalName;
  uint16_t partyCount, partyMons, partyNicks, otNames;
  uint8_t partyStruct;                 // bytes per party member
  uint8_t mSpecies, mItem, mMoves, mOtId, mExp, mDvs, mPp, mHappiness, mLevel, mStatus, mHp, mMaxHp;
  bool mHasTypes; uint8_t mType1, mType2;
  // economy, badges, dex
  uint16_t money; bool moneyBcd; uint16_t coins;
  uint16_t badges; uint8_t badgeBytes;
  uint16_t dexOwned, dexSeen; uint8_t dexBytes; uint16_t species;
  // bag, pc
  uint16_t bagCount, bagItems; uint8_t bagMax;
  uint16_t ballCount, ballItems; uint8_t ballMax;
  uint16_t pcCount, pcItems; uint8_t pcMax;
  // world
  uint16_t curMap, mapGroup, mapNumber, playerX, playerY, facing, repel, timeOfDay;
  uint16_t playHours; bool hoursWord; uint16_t playMinutes, playSeconds;
  uint16_t visited; uint8_t visitedSlots;
  uint16_t eventFlags; uint16_t eventBytes;
  uint16_t objectFlags; uint8_t objectBytes; uint16_t hiddenFlags; uint8_t hiddenBytes;
  // battle
  uint16_t inBattle, opponent, trainerClass, curBattleMon;
  uint16_t enemyMon, enemyNick, battleMon, enemyCatchRate;
  uint8_t bSpecies, bItem, bMoves, bDvs, bPp, bLevel, bStatus, bHp, bMaxHp, bAtk, bDef, bSpd, bSpc, bSpdef, bType1, bType2, bCatchRate;
  bool bHasTypes;
  uint16_t enemyPartyCount, enemyMons;
  // box, day care
  uint16_t curBox, numInBox, boxSpecies, boxMons; uint8_t boxStruct, boxLevel;
  uint16_t dayCareFlag, dayCareName, dayCareMon;
};

// pokered wram.asm
const Layout GEN1 = {
  0xD359, 0xD158, 0xD34A,
  0xD163, 0xD16B, 0xD2B5, 0xD273,
  44,
  0, 0, 8, 12, 14, 27, 29, 0, 33, 4, 1, 34,
  true, 5, 6,
  0xD347, true, 0xD5A4,
  0xD356, 1,
  0xD2F7, 0xD30A, 19, 151,
  0xD31D, 0xD31E, 20,
  0, 0, 0,
  0xD53A, 0xD53B, 50,
  0xD35E, 0, 0, 0xD362, 0xD361, 0xC109, 0xD0DB, 0,
  0xDA41, false, 0xDA43, 0xDA44,
  0xD70B, 11,
  0xD747, 320,
  0xD5A6, 32, 0xD6F0, 14,
  0xD057, 0xD059, 0xD031, 0xCC2F,
  0xCFE5, 0xCFDA, 0xD014, 0,
  0, 0, 8, 12, 25, 14, 4, 1, 15, 17, 19, 21, 23, 0, 5, 6, 7,
  true,
  0xD89C, 0xD8A4,
  0xD5A0, 0xDA80, 0xDA81, 0xDA96, 33, 3,
  0xDA48, 0xDA49, 0xDA5F,
};

// pokegold.sym (Gold and Silver share this map). party_struct is 48 bytes:
// species, item, moves x4, id, exp x3, stat exp x10, DVs, PP x4, happiness,
// pokerus, 2 unused, level, status, unused, hp, max hp, five stat words.
// battle_struct is 32: species, item, moves x4, DVs, PP x4, happiness, level,
// status x2, hp, max hp, five stat words, type1, type2.
const Layout GEN2 = {
  0xD1A1, 0xD1A3, 0xD1B9,
  0xDA22, 0xDA2A, 0xDB8C, 0xDB4A,
  48,
  0, 1, 2, 6, 8, 0x15, 0x17, 0x1B, 0x1F, 0x20, 0x22, 0x24,
  false, 0, 0,
  0xD573, false, 0xD57A,
  0xD57C, 2,
  0xDBE4, 0xDC04, 32, 251,
  0xD5B7, 0xD5B8, 20,
  0xD5FC, 0xD5FD, 12,
  0xD616, 0xD617, 50,
  0, 0xDA00, 0xDA01, 0xDA03, 0xDA02, 0xD205, 0xD9EB, 0xD157,
  0xD1EB, true, 0xD1ED, 0xD1EE,
  0xD9EE, 28,
  0xD7B7, 100,
  0, 0, 0, 0,
  0xD116, 0xD118, 0xD118, 0xCFC6,
  0xD0EF, 0xCAF6, 0xCB0C, 0xD114,
  0, 1, 2, 6, 8, 13, 14, 16, 18, 20, 22, 24, 26, 28, 30, 31, 0,
  true,
  0xDD55, 0xDD5D,
  0xD8BC, 0, 0, 0, 48, 0x1F,
  0xDC40, 0, 0,
};

const Layout *L = &GEN1;
pokemon::Game curGame = pokemon::Game::GEN1;

const int NAME_LEN = 11;

// ------------------------------------------------------------ charmap
//
// The single character part of the Gen 1 charmap, as pokered's charmap.asm
// has it; Gen 2 shares the letters, digits and these symbols. 0xE1 and 0xE2
// are the two character "PK" and "MN" and are handled by the decoder.

char oneChar(uint8_t c) {
  if (c >= 0x80 && c <= 0x99) return (char)('A' + (c - 0x80));
  if (c >= 0xA0 && c <= 0xB9) return (char)('a' + (c - 0xA0));
  if (c >= 0xF6 && c <= 0xFF) return (char)('0' + (c - 0xF6));
  if (c >= 0x9A && c <= 0x9F) return "():;[]"[c - 0x9A];
  switch (c) {
    case 0x7F: return ' ';
    case 0xBA: return 'e';   // accented e, approximated on an ASCII screen
    case 0xE0: return '\'';
    case 0xE3: return '-';
    case 0xE6: return '?';
    case 0xE7: return '!';
    case 0xE8: return '.';
    case 0xEF: return 'M';   // male sign
    case 0xF0: return '$';   // pokedollar
    case 0xF1: return 'x';   // multiplication sign
    case 0xF2: return '.';
    case 0xF3: return '/';
    case 0xF4: return ',';
    case 0xF5: return 'F';   // female sign
    default: return 0;
  }
}

const char *pairFor(uint8_t c) {
  switch (c) {
    case 0xBB: return "'d";
    case 0xBC: return "'l";
    case 0xBD: return "'s";
    case 0xBE: return "'t";
    case 0xBF: return "'v";
    case 0xE1: return "PK";
    case 0xE2: return "MN";
    default: return nullptr;
  }
}

uint32_t readBcd(uint16_t addr, int bytes) {
  uint32_t v = 0;
  for (int i = 0; i < bytes; i++) {
    uint8_t b = pokemon::rd(addr + i);
    v = v * 100 + (uint32_t)((b >> 4) * 10 + (b & 0x0F));
  }
  return v;
}

uint32_t readBE(uint16_t addr, int bytes) {
  uint32_t v = 0;
  for (int i = 0; i < bytes; i++) v = (v << 8) | pokemon::rd(addr + i);
  return v;
}

int popcountField(uint16_t addr, int bytes) {
  int n = 0;
  for (int i = 0; i < bytes; i++) {
    uint8_t b = pokemon::rd(addr + i);
    while (b) {
      n += b & 1;
      b >>= 1;
    }
  }
  return n;
}

bool bitAt(uint16_t base, uint16_t index, uint32_t bytes) {
  if (!base || index >= bytes * 8) return false;
  return (pokemon::rd(base + (uint16_t)(index >> 3)) >> (index & 7)) & 1;
}

void copyNick(uint16_t addr, char *dst, size_t cap) {
  String s = addr ? pokemon::textDecode(addr, NAME_LEN) : String();
  size_t n = s.length();
  if (n > cap - 1) n = cap - 1;
  memcpy(dst, s.c_str(), n);
  dst[n] = 0;
}

void splitDvs(uint8_t d0, uint8_t d1, uint8_t &a, uint8_t &d, uint8_t &s, uint8_t &c) {
  a = d0 >> 4; d = d0 & 0x0F; s = d1 >> 4; c = d1 & 0x0F;
}

}  // namespace

namespace pokemon {

// ---------------------------------------------------------------- game

void setGame(Game g) {
  curGame = g;
  L = (g == Game::GEN2) ? &GEN2 : &GEN1;
}
Game game() { return curGame; }
int speciesCount() { return L->species; }
int badgeTotal() { return L->badgeBytes * 8; }

// ---------------------------------------------------------------- snapshot

uint8_t *wramBuffer() { return buf; }

void wramCommit(size_t len) {
  if (len != WRAM_BYTES) return;
  valid = true;
  stamp = millis();
  counter++;
}

void wramInvalidate() {
  valid = false;
  counter = 0;
}

const uint8_t *wram() { return buf; }
bool wramValid() { return valid; }
uint32_t wramStamp() { return stamp; }
uint32_t wramCounter() { return counter; }

uint8_t rd(uint16_t addr) {
  if (!valid || !addr) return 0;
  uint32_t off = (uint32_t)addr - WRAM_BASE;
  if (off >= WRAM_BYTES) return 0;
  return buf[off];
}

uint16_t rdBE(uint16_t addr) {
  return (uint16_t)((uint16_t)rd(addr) << 8) | rd(addr + 1);
}

// ---------------------------------------------------------------- text

String textDecode(uint16_t addr, size_t max) {
  String out;
  out.reserve(max + 4);
  for (size_t i = 0; i < max; i++) {
    uint8_t c = rd(addr + i);
    if (c == 0x50 || c == 0x00) break;
    const char *pair = pairFor(c);
    if (pair) {
      out += pair;
      continue;
    }
    char one = oneChar(c);
    out += one ? one : '?';
  }
  out.trim();
  return out;
}

uint8_t gen1Encode(char c) {
  if (c >= 'A' && c <= 'Z') return (uint8_t)(0x80 + (c - 'A'));
  if (c >= 'a' && c <= 'z') return (uint8_t)(0xA0 + (c - 'a'));
  if (c >= '0' && c <= '9') return (uint8_t)(0xF6 + (c - '0'));
  switch (c) {
    case ' ': return 0x7F;
    case '(': return 0x9A;
    case ')': return 0x9B;
    case ':': return 0x9C;
    case ';': return 0x9D;
    case '[': return 0x9E;
    case ']': return 0x9F;
    case '\'': return 0xE0;
    case '-': return 0xE3;
    case '?': return 0xE6;
    case '!': return 0xE7;
    case '.': return 0xE8;
    case '$': return 0xF0;
    case 'x': return 0xF1;
    case '/': return 0xF3;
    case ',': return 0xF4;
    default: return 0x7F;
  }
}

// ---------------------------------------------------------------- decoders

String playerName() { return textDecode(L->playerName, NAME_LEN); }
String rivalName() { return textDecode(L->rivalName, NAME_LEN); }

uint8_t partyCount() {
  uint8_t n = rd(L->partyCount);
  return n > 6 ? 0 : n;
}

bool partyMon(int index, Mon &out) {
  if (index < 0 || index >= (int)partyCount()) return false;
  uint16_t base = L->partyMons + (uint16_t)(index * L->partyStruct);
  out.species = rd(base + L->mSpecies);
  out.hp = rdBE(base + L->mHp);
  out.maxHp = rdBE(base + L->mMaxHp);
  out.level = rd(base + L->mLevel);
  out.status = rd(base + L->mStatus);
  out.hasTypes = L->mHasTypes;
  out.type1 = L->mHasTypes ? rd(base + L->mType1) : 0xFF;
  out.type2 = L->mHasTypes ? rd(base + L->mType2) : 0xFF;
  for (int i = 0; i < 4; i++) {
    out.moves[i] = rd(base + L->mMoves + i);
    out.pp[i] = rd(base + L->mPp + i) & 0x3F;   // top two bits are PP Ups
  }
  out.item = L->mItem ? rd(base + L->mItem) : 0;
  out.happiness = L->mHappiness ? rd(base + L->mHappiness) : 0;
  splitDvs(rd(base + L->mDvs), rd(base + L->mDvs + 1), out.dvAtk, out.dvDef, out.dvSpd, out.dvSpc);
  copyNick(L->partyNicks + (uint16_t)(index * NAME_LEN), out.nick, sizeof(out.nick));
  return true;
}

uint32_t money() { return L->moneyBcd ? readBcd(L->money, 3) : readBE(L->money, 3); }
uint16_t coins() { return (uint16_t)(L->moneyBcd ? readBcd(L->coins, 2) : readBE(L->coins, 2)); }

uint16_t badges() {
  uint16_t b = rd(L->badges);
  if (L->badgeBytes > 1) b |= (uint16_t)rd(L->badges + 1) << 8;
  return b;
}

int badgeCount() {
  uint16_t b = badges();
  int n = 0;
  while (b) {
    n += b & 1;
    b >>= 1;
  }
  return n;
}

uint16_t curMap() {
  if (L->curMap) return rd(L->curMap);
  return (uint16_t)((uint16_t)rd(L->mapGroup) << 8) | rd(L->mapNumber);
}
uint8_t curMapGroup() { return L->mapGroup ? rd(L->mapGroup) : 0; }
uint8_t curMapNumber() { return L->mapNumber ? rd(L->mapNumber) : (uint8_t)rd(L->curMap); }
uint8_t playerX() { return rd(L->playerX); }
uint8_t playerY() { return rd(L->playerY); }
uint8_t timeOfDay() { return L->timeOfDay ? (uint8_t)(rd(L->timeOfDay) & 3) : 1; }

PlayTime playTime() {
  PlayTime t;
  t.hours = L->hoursWord ? rdBE(L->playHours) : rd(L->playHours);
  t.minutes = rd(L->playMinutes);
  t.seconds = rd(L->playSeconds);
  return t;
}

int dexOwned() { return popcountField(L->dexOwned, L->dexBytes); }
int dexSeen() { return popcountField(L->dexSeen, L->dexBytes); }

uint8_t bagCount() {
  uint8_t n = rd(L->bagCount);
  return n > L->bagMax ? 0 : n;
}

bool bagItem(int index, uint8_t &id, uint8_t &qty) {
  if (index < 0 || index >= (int)bagCount()) return false;
  id = rd(L->bagItems + (uint16_t)(index * 2));
  qty = rd(L->bagItems + (uint16_t)(index * 2) + 1);
  return true;
}

uint8_t ballCount() {
  if (!L->ballCount) return 0;
  uint8_t n = rd(L->ballCount);
  return n > L->ballMax ? 0 : n;
}

bool ballItem(int index, uint8_t &id, uint8_t &qty) {
  if (index < 0 || index >= (int)ballCount()) return false;
  id = rd(L->ballItems + (uint16_t)(index * 2));
  qty = rd(L->ballItems + (uint16_t)(index * 2) + 1);
  return true;
}

uint8_t inBattle() { return rd(L->inBattle); }
uint8_t opponent() { return rd(L->opponent); }

uint8_t repelSteps() { return rd(L->repel); }
// Gen 1: the top bit of the box number byte is a "box changed" flag.
uint8_t boxNumber() { return (uint8_t)((rd(L->curBox) & 0x7F) + 1); }
uint8_t boxCount() {
  if (!L->numInBox) return 0;
  uint8_t n = rd(L->numInBox);
  return n > 20 ? 0 : n;
}
bool dayCareInUse() { return L->dayCareFlag ? (rd(L->dayCareFlag) & 1) != 0 : false; }

// ------------------------------------------------- companion screen extras

uint16_t playerId() { return rdBE(L->playerId); }

bool dexOwnedBit(uint8_t dex) {
  if (!dex || dex > L->species) return false;
  return bitAt(L->dexOwned, (uint16_t)(dex - 1), L->dexBytes);
}

bool dexSeenBit(uint8_t dex) {
  if (!dex || dex > L->species) return false;
  return bitAt(L->dexSeen, (uint16_t)(dex - 1), L->dexBytes);
}

String otName(int index) {
  if (index < 0 || index >= 6) return String();
  return textDecode(L->otNames + (uint16_t)(index * NAME_LEN), NAME_LEN);
}

uint16_t otId(int index) {
  if (index < 0 || index >= 6) return 0;
  return rdBE(L->partyMons + (uint16_t)(index * L->partyStruct) + L->mOtId);
}

uint32_t partyExp(int index) {
  if (index < 0 || index >= (int)partyCount()) return 0;
  return readBE(L->partyMons + (uint16_t)(index * L->partyStruct) + L->mExp, 3);
}

uint8_t playerFacing() { return rd(L->facing); }

uint8_t partyMonNumber() {
  uint8_t n = rd(L->curBattleMon);
  return n > 5 ? 0 : n;
}

namespace {

bool readBattleStruct(uint16_t base, uint16_t nickAddr, BattleMon &out) {
  out.species = rd(base + L->bSpecies);
  out.hp = rdBE(base + L->bHp);
  out.maxHp = rdBE(base + L->bMaxHp);
  out.level = rd(base + L->bLevel);
  out.status = rd(base + L->bStatus);
  out.type1 = rd(base + L->bType1);
  out.type2 = rd(base + L->bType2);
  out.item = L->bItem ? rd(base + L->bItem) : 0;
  for (int i = 0; i < 4; i++) {
    out.moves[i] = rd(base + L->bMoves + i);
    out.pp[i] = rd(base + L->bPp + i) & 0x3F;
  }
  splitDvs(rd(base + L->bDvs), rd(base + L->bDvs + 1), out.dvAtk, out.dvDef, out.dvSpd, out.dvSpc);
  out.dvHp = (uint8_t)(((out.dvAtk & 1) << 3) | ((out.dvDef & 1) << 2) |
                       ((out.dvSpd & 1) << 1) | (out.dvSpc & 1));
  out.atk = rdBE(base + L->bAtk);
  out.def = rdBE(base + L->bDef);
  out.spd = rdBE(base + L->bSpd);
  out.spc = rdBE(base + L->bSpc);
  out.spdef = L->bSpdef ? rdBE(base + L->bSpdef) : 0;
  copyNick(nickAddr, out.nick, sizeof(out.nick));
  return true;
}

}  // namespace

bool enemyMon(EnemyMon &out) {
  if (!inBattle()) return false;
  BattleMon b;
  readBattleStruct(L->enemyMon, L->enemyNick, b);
  out.species = b.species; out.hp = b.hp; out.maxHp = b.maxHp; out.level = b.level; out.status = b.status;
  for (int i = 0; i < 4; i++) out.moves[i] = b.moves[i];
  memcpy(out.nick, b.nick, sizeof(out.nick));
  return true;
}

bool enemyBattleMon(BattleMon &out) {
  if (!inBattle()) return false;
  readBattleStruct(L->enemyMon, L->enemyNick, out);
  // Gen 1 keeps the catch rate inside the struct; Gen 2 beside it.
  out.catchRate = L->enemyCatchRate ? rd(L->enemyCatchRate) : rd(L->enemyMon + L->bCatchRate);
  return true;
}

bool playerBattleMon(BattleMon &out) {
  if (!inBattle()) return false;
  uint16_t nick = L->partyNicks + (uint16_t)(partyMonNumber() * NAME_LEN);
  readBattleStruct(L->battleMon, nick, out);
  out.catchRate = 0;
  return true;
}

uint8_t trainerClass() { return inBattle() == 2 ? rd(L->trainerClass) : 0; }

uint8_t enemyPartyCount() {
  uint8_t n = rd(L->enemyPartyCount);
  return n > 6 ? 0 : n;
}

int enemyAliveCount() {
  int n = 0;
  for (int i = 0; i < (int)enemyPartyCount(); i++) {
    if (rdBE(L->enemyMons + (uint16_t)(i * L->partyStruct) + L->mHp) > 0) n++;
  }
  return n;
}

uint8_t pcItemCount() {
  uint8_t n = rd(L->pcCount);
  return n > L->pcMax ? 0 : n;
}

bool pcItem(int index, uint8_t &id, uint8_t &qty) {
  if (index < 0 || index >= (int)pcItemCount()) return false;
  id = rd(L->pcItems + (uint16_t)(index * 2));
  qty = rd(L->pcItems + (uint16_t)(index * 2) + 1);
  return true;
}

uint8_t boxMonSpecies(int index) {
  if (!L->boxSpecies || index < 0 || index >= (int)boxCount()) return 0;
  return rd(L->boxSpecies + (uint16_t)index);
}

uint8_t boxMonLevel(int index) {
  if (!L->boxMons || index < 0 || index >= (int)boxCount()) return 0;
  return rd(L->boxMons + (uint16_t)(index * L->boxStruct) + L->boxLevel);
}

String dayCareName() {
  if (!dayCareInUse() || !L->dayCareName) return String();
  return textDecode(L->dayCareName, NAME_LEN);
}

uint8_t dayCareLevel() {
  if (!dayCareInUse() || !L->dayCareMon) return 0;
  return rd(L->dayCareMon + L->boxLevel);
}

bool objectTaken(uint16_t index) {
  if (L->objectFlags) return bitAt(L->objectFlags, index, L->objectBytes);
  return eventFlag(index);   // Gen 2: pickups are event flags
}

bool hiddenItemTaken(uint16_t index) {
  if (L->hiddenFlags) return bitAt(L->hiddenFlags, index, L->hiddenBytes);
  return eventFlag(index);
}

bool townVisited(uint8_t index) {
  if (index >= L->visitedSlots) return false;
  return bitAt(L->visited, index, (L->visitedSlots + 7) / 8);
}
int townSlots() { return L->visitedSlots; }

bool eventFlag(uint16_t event) { return bitAt(L->eventFlags, event, L->eventBytes); }

uint32_t viewSignature() {
  static const uint16_t gen1[][2] = {
      {0xCFDA, 0xD032}, {0xD057, 0xD05A}, {0xD158, 0xD370}, {0xD53A, 0xD5A8},
      {0xD70B, 0xD70D}, {0xD747, 0xD887}, {0xDA41, 0xDA44}, {0xDA48, 0xDA81},
  };
  static const uint16_t gen2[][2] = {
      {0xCAF6, 0xCB2C},  // enemy nick, player battle struct
      {0xD0EF, 0xD11A},  // enemy battle struct, catch rate, battle mode, trainer class
      {0xD157, 0xD158},  // time of day
      {0xD1A1, 0xD1EE},  // id, names, play time hours and minutes
      {0xD205, 0xD206},  // facing
      {0xD573, 0xD580},  // money, coins, badges
      {0xD5B7, 0xD5B7 + 1 + 20 * 2 + 1 + 1 + 12 * 2 + 1 + 1 + 25 + 1},  // item pockets
      {0xD616, 0xD616 + 1 + 50 * 2 + 1},  // PC items
      {0xD7B7, 0xD7B7 + 100},  // event flags
      {0xD8BC, 0xD8BD},  // current box
      {0xD9EB, 0xD9F2},  // repel, visited spawns
      {0xDA00, 0xDA04},  // map, coords
      {0xDA22, 0xDC24},  // party, OT names, nicknames, dex owned+seen
      {0xDD55, 0xDD56},  // enemy party count
  };
  const uint16_t (*ranges)[2] = (curGame == Game::GEN2) ? gen2 : gen1;
  size_t n = (curGame == Game::GEN2) ? sizeof(gen2) / sizeof(gen2[0]) : sizeof(gen1) / sizeof(gen1[0]);
  const uint8_t *w = wram();
  uint32_t h = 2166136261u;
  for (size_t r = 0; r < n; r++) {
    for (uint16_t a = ranges[r][0]; a < ranges[r][1]; a++) {
      h ^= w[a - 0xC000];
      h *= 16777619u;
    }
  }
  return h;
}

}  // namespace pokemon
