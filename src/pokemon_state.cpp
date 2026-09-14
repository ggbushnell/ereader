#include "pokemon_state.h"

namespace {

uint8_t buf[pokemon::WRAM_BYTES];
bool valid = false;
uint32_t stamp = 0;
uint32_t counter = 0;

// ------------------------------------------------------------ WRAM map
//
// pokered wram.asm. Everything here is a raw GB address; pokemon::rd turns it
// into an offset into the snapshot.

const uint16_t W_IS_IN_BATTLE = 0xD057;
const uint16_t W_CUR_OPPONENT = 0xD059;
const uint16_t W_ENEMY_MON_NICK = 0xCFDA;
const uint16_t W_ENEMY_MON = 0xCFE5;
const uint16_t W_REPEL_STEPS = 0xD0DB;
const uint16_t W_PLAYER_NAME = 0xD158;
const uint16_t W_PARTY_COUNT = 0xD163;
const uint16_t W_PARTY_MONS = 0xD16B;
const uint16_t W_PARTY_NICKS = 0xD2B5;
const uint16_t W_DEX_OWNED = 0xD2F7;
const uint16_t W_DEX_SEEN = 0xD30A;
const uint16_t W_BAG_COUNT = 0xD31D;
const uint16_t W_BAG_ITEMS = 0xD31E;
const uint16_t W_MONEY = 0xD347;
const uint16_t W_RIVAL_NAME = 0xD34A;
const uint16_t W_BADGES = 0xD356;
const uint16_t W_PLAYER_ID = 0xD359;
const uint16_t W_CUR_MAP = 0xD35E;
const uint16_t W_PLAYER_Y = 0xD361;
const uint16_t W_PLAYER_X = 0xD362;
const uint16_t W_CUR_BOX_NUM = 0xD5A0;
const uint16_t W_COINS = 0xD5A4;
const uint16_t W_PLAY_TIME_H = 0xDA41;
const uint16_t W_PLAY_TIME_M = 0xDA43;
const uint16_t W_PLAY_TIME_S = 0xDA44;
const uint16_t W_DAY_CARE_IN_USE = 0xDA48;
const uint16_t W_NUM_IN_BOX = 0xDA80;

// Companion screen extras. Same wram.asm walk as the block above.
const uint16_t W_SPRITE_PLAYER_FACING = 0xC109;   // sprite 0, state data 1 + 9
const uint16_t W_PLAYER_MON_NUMBER = 0xCC2F;
const uint16_t W_BATTLE_MON = 0xD014;
const uint16_t W_TRAINER_CLASS = 0xD031;
const uint16_t W_OT_NAMES = 0xD273;
const uint16_t W_NUM_BOX_ITEMS = 0xD53A;
const uint16_t W_BOX_ITEMS = 0xD53B;
const uint16_t W_TOGGLEABLE_OBJECT_FLAGS = 0xD5A6;
const uint16_t W_HIDDEN_ITEM_FLAGS = 0xD6F0;
const uint16_t W_ENEMY_PARTY_COUNT = 0xD89C;
const uint16_t W_ENEMY_MONS = 0xD8A4;
const uint16_t W_DAY_CARE_MON_NAME = 0xDA49;
const uint16_t W_DAY_CARE_MON = 0xDA5F;
const uint16_t W_BOX_SPECIES = 0xDA81;
const uint16_t W_BOX_MONS = 0xDA96;

const int PARTY_STRUCT = 44;
const int BOX_STRUCT = 33;
const int NAME_LEN = 11;
const int TOGGLEABLE_FLAG_BYTES = 32;
const int HIDDEN_FLAG_BYTES = 14;

// Party struct field offsets (pokered wPartyMon).
const int MON_SPECIES = 0;
const int MON_HP = 1;      // u16 big endian
const int MON_STATUS = 4;
const int MON_TYPE1 = 5;
const int MON_TYPE2 = 6;
const int MON_MOVES = 8;   // 4 bytes
const int MON_OT_ID = 12;  // u16 big endian
const int MON_PP = 29;     // 4 bytes
const int MON_LEVEL = 33;
const int MON_MAX_HP = 34;  // u16 big endian

// Box struct field offsets, the 33 byte prefix of the party struct. Its level
// field is the one at +3, which the party struct leaves stale.
const int BOX_SPECIES = 0;
const int BOX_LEVEL = 3;

// Battle struct field offsets (pokered wEnemyMon and wBattleMon), 29 bytes:
// species, hp, box level, status, types, catch rate, moves, DVs, level, max
// hp, the four stat words, then PP.
const int BAT_SPECIES = 0;
const int BAT_HP = 1;
const int BAT_STATUS = 4;
const int BAT_TYPE1 = 5;
const int BAT_TYPE2 = 6;
const int BAT_CATCH_RATE = 7;
const int BAT_MOVES = 8;
const int BAT_LEVEL = 14;
const int BAT_MAX_HP = 15;
const int BAT_ATTACK = 17;
const int BAT_DEFENSE = 19;
const int BAT_SPEED = 21;
const int BAT_SPECIAL = 23;
const int BAT_PP = 25;

// ------------------------------------------------------------ charmap
//
// The single character part of the Gen 1 charmap, as pokered's charmap.asm
// has it. The plan doc's table swaps a few codes (it has 0xE6 as '!' and
// 0xF3 as '-'); pokered wins, since the decode has to match the ROM.
// 0xE1 and 0xE2 are the two character "PK" and "MN" and are handled by the
// decoder, not this table.

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

// 0xBB..0xBF are the apostrophe pairs 'd 'l 's 't 'v.
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

// A run of BCD bytes, most significant first, as the money and coin counters
// store them. Two digits per byte.
uint32_t readBcd(uint16_t addr, int bytes) {
  uint32_t v = 0;
  for (int i = 0; i < bytes; i++) {
    uint8_t b = pokemon::rd(addr + i);
    v = v * 100 + (uint32_t)((b >> 4) * 10 + (b & 0x0F));
  }
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

void copyNick(uint16_t addr, char *dst, size_t cap) {
  String s = pokemon::textDecode(addr, NAME_LEN);
  size_t n = s.length();
  if (n > cap - 1) n = cap - 1;
  memcpy(dst, s.c_str(), n);
  dst[n] = 0;
}

}  // namespace

namespace pokemon {

// ---------------------------------------------------------------- snapshot

uint8_t *wramBuffer() { return buf; }

void wramCommit(size_t len) {
  if (len != WRAM_BYTES) return;
  valid = true;
  stamp = millis();
  counter++;
}

const uint8_t *wram() { return buf; }
bool wramValid() { return valid; }
uint32_t wramStamp() { return stamp; }
uint32_t wramCounter() { return counter; }

uint8_t rd(uint16_t addr) {
  if (!valid) return 0;
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

String playerName() { return textDecode(W_PLAYER_NAME, NAME_LEN); }
String rivalName() { return textDecode(W_RIVAL_NAME, NAME_LEN); }

uint8_t partyCount() {
  uint8_t n = rd(W_PARTY_COUNT);
  return n > 6 ? 0 : n;
}

bool partyMon(int index, Mon &out) {
  if (index < 0 || index >= (int)partyCount()) return false;
  uint16_t base = W_PARTY_MONS + (uint16_t)(index * PARTY_STRUCT);
  out.species = rd(base + MON_SPECIES);
  out.hp = rdBE(base + MON_HP);
  out.maxHp = rdBE(base + MON_MAX_HP);
  out.level = rd(base + MON_LEVEL);
  out.status = rd(base + MON_STATUS);
  out.type1 = rd(base + MON_TYPE1);
  out.type2 = rd(base + MON_TYPE2);
  for (int i = 0; i < 4; i++) {
    out.moves[i] = rd(base + MON_MOVES + i);
    out.pp[i] = rd(base + MON_PP + i) & 0x3F;   // top two bits are PP Ups
  }
  copyNick(W_PARTY_NICKS + (uint16_t)(index * NAME_LEN), out.nick,
           sizeof(out.nick));
  return true;
}

uint32_t money() { return readBcd(W_MONEY, 3); }
uint16_t coins() { return (uint16_t)readBcd(W_COINS, 2); }
uint8_t badges() { return rd(W_BADGES); }

int badgeCount() {
  uint8_t b = badges();
  int n = 0;
  while (b) {
    n += b & 1;
    b >>= 1;
  }
  return n;
}

uint8_t curMap() { return rd(W_CUR_MAP); }
uint8_t playerX() { return rd(W_PLAYER_X); }
uint8_t playerY() { return rd(W_PLAYER_Y); }

PlayTime playTime() {
  PlayTime t;
  t.hours = rd(W_PLAY_TIME_H);
  t.minutes = rd(W_PLAY_TIME_M);
  t.seconds = rd(W_PLAY_TIME_S);
  return t;
}

int dexOwned() { return popcountField(W_DEX_OWNED, 19); }
int dexSeen() { return popcountField(W_DEX_SEEN, 19); }

uint8_t bagCount() {
  uint8_t n = rd(W_BAG_COUNT);
  return n > 20 ? 0 : n;
}

bool bagItem(int index, uint8_t &id, uint8_t &qty) {
  if (index < 0 || index >= (int)bagCount()) return false;
  id = rd(W_BAG_ITEMS + (uint16_t)(index * 2));
  qty = rd(W_BAG_ITEMS + (uint16_t)(index * 2) + 1);
  return true;
}

uint8_t inBattle() { return rd(W_IS_IN_BATTLE); }
uint8_t opponent() { return rd(W_CUR_OPPONENT); }

bool enemyMon(EnemyMon &out) {
  if (!inBattle()) return false;
  out.species = rd(W_ENEMY_MON + BAT_SPECIES);
  out.hp = rdBE(W_ENEMY_MON + BAT_HP);
  out.maxHp = rdBE(W_ENEMY_MON + BAT_MAX_HP);
  out.level = rd(W_ENEMY_MON + BAT_LEVEL);
  out.status = rd(W_ENEMY_MON + BAT_STATUS);
  for (int i = 0; i < 4; i++) out.moves[i] = rd(W_ENEMY_MON + BAT_MOVES + i);
  copyNick(W_ENEMY_MON_NICK, out.nick, sizeof(out.nick));
  return true;
}

uint8_t repelSteps() { return rd(W_REPEL_STEPS); }
// The top bit of the box number byte is a "box changed" flag, not a count.
uint8_t boxNumber() { return (uint8_t)((rd(W_CUR_BOX_NUM) & 0x7F) + 1); }
uint8_t boxCount() {
  uint8_t n = rd(W_NUM_IN_BOX);
  return n > 20 ? 0 : n;
}
bool dayCareInUse() { return rd(W_DAY_CARE_IN_USE) != 0; }

// ------------------------------------------------- companion screen extras

uint16_t playerId() { return rdBE(W_PLAYER_ID); }

bool dexOwnedBit(uint8_t dex) {
  if (!dex || dex > 151) return false;
  return (rd(W_DEX_OWNED + (uint16_t)((dex - 1) >> 3)) >> ((dex - 1) & 7)) & 1;
}

bool dexSeenBit(uint8_t dex) {
  if (!dex || dex > 151) return false;
  return (rd(W_DEX_SEEN + (uint16_t)((dex - 1) >> 3)) >> ((dex - 1) & 7)) & 1;
}

String otName(int index) {
  if (index < 0 || index >= 6) return String();
  return textDecode(W_OT_NAMES + (uint16_t)(index * NAME_LEN), NAME_LEN);
}

uint16_t otId(int index) {
  if (index < 0 || index >= 6) return 0;
  return rdBE(W_PARTY_MONS + (uint16_t)(index * PARTY_STRUCT) + MON_OT_ID);
}

uint8_t playerFacing() { return rd(W_SPRITE_PLAYER_FACING); }

uint8_t partyMonNumber() {
  uint8_t n = rd(W_PLAYER_MON_NUMBER);
  return n > 5 ? 0 : n;
}

namespace {

// One 29 byte battle struct at `base`, with its nickname read from `nickAddr`.
bool readBattleStruct(uint16_t base, uint16_t nickAddr, BattleMon &out) {
  out.species = rd(base + BAT_SPECIES);
  out.hp = rdBE(base + BAT_HP);
  out.maxHp = rdBE(base + BAT_MAX_HP);
  out.level = rd(base + BAT_LEVEL);
  out.status = rd(base + BAT_STATUS);
  out.type1 = rd(base + BAT_TYPE1);
  out.type2 = rd(base + BAT_TYPE2);
  out.catchRate = rd(base + BAT_CATCH_RATE);
  for (int i = 0; i < 4; i++) {
    out.moves[i] = rd(base + BAT_MOVES + i);
    out.pp[i] = rd(base + BAT_PP + i) & 0x3F;
  }
  out.atk = rdBE(base + BAT_ATTACK);
  out.def = rdBE(base + BAT_DEFENSE);
  out.spd = rdBE(base + BAT_SPEED);
  out.spc = rdBE(base + BAT_SPECIAL);
  copyNick(nickAddr, out.nick, sizeof(out.nick));
  return true;
}

}  // namespace

bool enemyBattleMon(BattleMon &out) {
  if (!inBattle()) return false;
  return readBattleStruct(W_ENEMY_MON, W_ENEMY_MON_NICK, out);
}

bool playerBattleMon(BattleMon &out) {
  if (!inBattle()) return false;
  // The battling mon's nickname is the party nickname of the active slot; the
  // battle struct itself carries no name.
  uint16_t nick = W_PARTY_NICKS + (uint16_t)(partyMonNumber() * NAME_LEN);
  return readBattleStruct(W_BATTLE_MON, nick, out);
}

uint8_t trainerClass() { return inBattle() == 2 ? rd(W_TRAINER_CLASS) : 0; }

uint8_t enemyPartyCount() {
  uint8_t n = rd(W_ENEMY_PARTY_COUNT);
  return n > 6 ? 0 : n;
}

int enemyAliveCount() {
  int n = 0;
  for (int i = 0; i < (int)enemyPartyCount(); i++) {
    if (rdBE(W_ENEMY_MONS + (uint16_t)(i * PARTY_STRUCT) + MON_HP) > 0) n++;
  }
  return n;
}

uint8_t pcItemCount() {
  uint8_t n = rd(W_NUM_BOX_ITEMS);
  return n > 50 ? 0 : n;
}

bool pcItem(int index, uint8_t &id, uint8_t &qty) {
  if (index < 0 || index >= (int)pcItemCount()) return false;
  id = rd(W_BOX_ITEMS + (uint16_t)(index * 2));
  qty = rd(W_BOX_ITEMS + (uint16_t)(index * 2) + 1);
  return true;
}

uint8_t boxMonSpecies(int index) {
  if (index < 0 || index >= (int)boxCount()) return 0;
  return rd(W_BOX_SPECIES + (uint16_t)index);
}

uint8_t boxMonLevel(int index) {
  if (index < 0 || index >= (int)boxCount()) return 0;
  return rd(W_BOX_MONS + (uint16_t)(index * BOX_STRUCT) + BOX_LEVEL);
}

String dayCareName() {
  if (!dayCareInUse()) return String();
  return textDecode(W_DAY_CARE_MON_NAME, NAME_LEN);
}

uint8_t dayCareLevel() {
  if (!dayCareInUse()) return 0;
  return rd(W_DAY_CARE_MON + BOX_LEVEL);
}

bool objectTaken(uint8_t index) {
  if (index >= TOGGLEABLE_FLAG_BYTES * 8) return false;
  return (rd(W_TOGGLEABLE_OBJECT_FLAGS + (uint16_t)(index >> 3)) >>
          (index & 7)) &
         1;
}

bool hiddenItemTaken(uint8_t index) {
  if (index >= HIDDEN_FLAG_BYTES * 8) return false;
  return (rd(W_HIDDEN_ITEM_FLAGS + (uint16_t)(index >> 3)) >> (index & 7)) & 1;
}

}  // namespace pokemon

namespace pokemon {

uint32_t viewSignature() {
  static const uint16_t ranges[][2] = {
      {0xCFDA, 0xD032},  // enemy nick, enemy and player battle structs
      {0xD057, 0xD05A},  // in battle, opponent
      {0xD158, 0xD370},  // names, party, dex, bag, money, badges, map, coords
      {0xD53A, 0xD5A8},  // PC items, box number, coins
      {0xDA41, 0xDA44},  // play time hours and minutes (seconds excluded)
      {0xDA48, 0xDA81},  // day care, box count
  };
  const uint8_t *w = wram();
  uint32_t h = 2166136261u;
  for (size_t r = 0; r < sizeof(ranges) / sizeof(ranges[0]); r++) {
    for (uint16_t a = ranges[r][0]; a < ranges[r][1]; a++) {
      h ^= w[a - 0xC000];
      h *= 16777619u;
    }
  }
  return h;
}

}  // namespace pokemon
