#include "pokemon_data.h"

#include "pack.h"
#include "pokemon_state.h"

namespace {

const char *S_DEX_ORDER = "dex_order";
const char *S_SPECIES_NAMES = "species_names";
const char *S_BASE_STATS = "base_stats";
const char *S_GROWTH = "growth";
const char *S_TYPE_NAMES = "type_names";
const char *S_TYPE_CHART = "type_chart";
const char *S_ITEM_NAMES = "item_names";
const char *S_ITEM_PRICES = "item_prices";
const char *S_MOVE_NAMES = "move_names";
const char *S_MOVE_DATA = "move_data";
const char *S_MAP_LABELS = "map_labels";
const char *S_TRAINER_CLASS = "trainer_class";
const char *S_MAP_INDEX = "map_index";
const char *S_WILD = "wild";
const char *S_ITEMS_PLACED = "items_placed";
const char *S_ITEMS_HIDDEN = "items_hidden";

uint8_t typeChart[128 * 3];
int typeChartRecs = 0;
bool chartTried = false;

}  // namespace

namespace pokedata {

uint16_t le16(const uint8_t *p) {
  return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}
uint32_t le32(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
         ((uint32_t)p[3] << 24);
}

String packString(const char *section, int index, size_t cap) {
  if (index < 0) return String();
  uint8_t head[2];
  if (pack::read(section, 0, head, 2) != 2) return String();
  int count = (int)le16(head);
  if (index >= count) return String();
  uint8_t off[2];
  if (pack::read(section, 2 + 2 * (size_t)index, off, 2) != 2) return String();
  char buf[40];
  if (cap > sizeof(buf) - 1) cap = sizeof(buf) - 1;
  size_t n = pack::read(section, le16(off), buf, cap);
  buf[n] = 0;
  return String(buf);
}

uint8_t dexOf(uint8_t internalIndex) {
  if (!internalIndex) return 0;
  uint8_t dex = 0;
  pack::read(S_DEX_ORDER, (size_t)internalIndex - 1, &dex, 1);
  return dex;
}

String speciesNameByDex(uint8_t dex) {
  if (!dex || dex > pokemon::speciesCount()) return String();
  return packString(S_SPECIES_NAMES, dex - 1, 12);
}

String speciesName(uint8_t internalIndex) {
  return speciesNameByDex(dexOf(internalIndex));
}

bool baseStats(uint8_t dex, uint8_t out[9]) {
  if (!dex || dex > pokemon::speciesCount()) return false;
  return pack::read(S_BASE_STATS, (size_t)(dex - 1) * 9, out, 9) == 9;
}

uint8_t growthRate(uint8_t dex) {
  if (!dex || dex > pokemon::speciesCount()) return 0;
  uint8_t g = 0;
  if (pack::read(S_GROWTH, (size_t)dex - 1, &g, 1) != 1) return 0;
  return g;
}

String typeName(uint8_t type) { return packString(S_TYPE_NAMES, type, 10); }

String typeLine(uint8_t t1, uint8_t t2) {
  String a = typeName(t1);
  if (t2 == t1) return a;
  String b = typeName(t2);
  if (!b.length()) return a;
  return a + "/" + b;
}

String itemName(uint8_t id) { return packString(S_ITEM_NAMES, id, 14); }

uint16_t itemPrice(uint8_t id) {
  uint8_t p[2];
  if (pack::read(S_ITEM_PRICES, (size_t)id * 2, p, 2) != 2) return 0;
  return le16(p);
}

String moveName(uint8_t id) {
  if (!id) return String();
  return packString(S_MOVE_NAMES, id - 1, 14);
}

bool moveData(uint8_t id, uint8_t out[4]) {
  if (!id) return false;
  return pack::read(S_MOVE_DATA, (size_t)(id - 1) * 4, out, 4) == 4;
}

String mapLabel(uint16_t map) { return packString(S_MAP_LABELS, map, 30); }

String trainerClassName(uint8_t cls) {
  if (!cls) return String();
  return packString(S_TRAINER_CLASS, cls - 1, 14);
}

const char *statusText(uint8_t status, bool fainted) {
  if (fainted) return "FNT";
  if (status & 0x07) return "SLP";
  if (status & 0x08) return "PSN";
  if (status & 0x10) return "BRN";
  if (status & 0x20) return "FRZ";
  if (status & 0x40) return "PAR";
  return "";
}

// ---- type chart

int chartMul(uint8_t atk, uint8_t def) {
  if (!chartTried) {
    chartTried = true;
    size_t len = pack::size(S_TYPE_CHART);
    if (len > sizeof(typeChart)) len = sizeof(typeChart);
    size_t got = pack::read(S_TYPE_CHART, 0, typeChart, len);
    typeChartRecs = (int)(got / 3);
  }
  for (int i = 0; i < typeChartRecs; i++) {
    const uint8_t *r = typeChart + i * 3;
    if (r[0] == atk && r[1] == def) return r[2];
  }
  return 10;   // any pair the table does not list is neutral
}

int effPercent(uint8_t moveType, uint8_t d1, uint8_t d2) {
  // Both entries are ten times their multiplier, so the product is already a
  // percentage of neutral: 5 and 5 give 25, 20 and 20 give 400.
  int m = chartMul(moveType, d1);
  int n = (d2 != d1) ? chartMul(moveType, d2) : 10;
  return m * n;
}

String effBadge(int percent) {
  switch (percent) {
    case 0: return "x0 ";
    case 25: return "1/4";
    case 50: return "1/2";
    case 200: return "x2 ";
    case 400: return "x4 ";
    default: return String();
  }
}

static const uint8_t TYPE_IDS_GEN1[15] = {0, 1, 2, 3, 4, 5, 7, 8, 20, 21, 22, 23, 24, 25, 26};
static const uint8_t TYPE_IDS_GEN2[17] = {0, 1, 2, 3, 4, 5, 7, 8, 9, 20, 21, 22, 23, 24, 25, 26, 27};

const uint8_t *typeIds(int &n) {
  if (pokemon::game() == pokemon::Game::GEN2) { n = 17; return TYPE_IDS_GEN2; }
  n = 15;
  return TYPE_IDS_GEN1;
}

Matchups matchups(uint8_t t1, uint8_t t2) {
  Matchups m;
  m.nWeak = m.nResist = m.nImmune = 0;
  int nt = 0;
  const uint8_t *ids = typeIds(nt);
  for (int i = 0; i < nt; i++) {
    uint8_t t = ids[i];
    int e = effPercent(t, t1, t2);
    if (e > 100) m.weak[m.nWeak++] = t;
    else if (e == 0) m.immune[m.nImmune++] = t;
    else if (e < 100) m.resist[m.nResist++] = t;
  }
  return m;
}

// ---- maps

MapInfo mapInfo(uint16_t map) {
  MapInfo mi;
  mi.ok = false;
  mi.offset = 0;
  mi.w = mi.h = mi.tileset = mi.border = 0;
  uint8_t rec[8];
  if (pack::read(S_MAP_INDEX, (size_t)map * 8, rec, 8) != 8) return mi;
  mi.offset = le32(rec);
  mi.w = rec[4];
  mi.h = rec[5];
  mi.tileset = rec[6];
  mi.border = rec[7];
  mi.ok = mi.w > 0 && mi.h > 0;
  return mi;
}

// ---- wild encounters

const int SLOT_ODDS[10] = {20, 20, 15, 10, 10, 10, 5, 5, 4, 1};

int mergeSlots(const uint8_t *slots, Encounter *out) {
  int n = 0;
  for (int i = 0; i < 10; i++) {
    uint8_t level = slots[i * 2];
    uint8_t species = slots[i * 2 + 1];
    if (!species) continue;
    int found = -1;
    for (int j = 0; j < n; j++) {
      if (out[j].species == species) {
        found = j;
        break;
      }
    }
    if (found < 0) {
      out[n].species = species;
      out[n].levelLo = level;
      out[n].levelHi = level;
      out[n].odds = SLOT_ODDS[i];
      n++;
    } else {
      if (level < out[found].levelLo) out[found].levelLo = level;
      if (level > out[found].levelHi) out[found].levelHi = level;
      out[found].odds += SLOT_ODDS[i];
    }
  }
  return n;
}

WildTable wildTable(uint16_t map) {
  WildTable t;
  t.ok = false;
  t.grassRate = t.waterRate = 0;
  t.nGrass = t.nWater = 0;
  uint8_t wild[42];
  if (pack::read(S_WILD, (size_t)map * 42, wild, 42) != 42) return t;
  t.grassRate = wild[0];
  t.waterRate = wild[21];
  t.ok = t.grassRate || t.waterRate;
  if (t.grassRate) t.nGrass = mergeSlots(wild + 1, t.grass);
  if (t.waterRate) t.nWater = mergeSlots(wild + 22, t.water);
  return t;
}

// ---- pickups

int collectItems(uint16_t map, MapItem *out, int cap) {
  int n = 0;
  uint8_t rec[5];
  size_t count = pack::size(S_ITEMS_PLACED) / 5;
  for (size_t i = 0; i < count && n < cap; i++) {
    if (pack::read(S_ITEMS_PLACED, i * 5, rec, 5) != 5) break;
    if (rec[0] != map) continue;
    out[n].x = rec[1];
    out[n].y = rec[2];
    out[n].item = rec[3];
    out[n].hidden = false;
    out[n].flag = rec[4];
    out[n].taken = pokemon::objectTaken(rec[4]);
    n++;
  }
  count = pack::size(S_ITEMS_HIDDEN) / 5;
  for (size_t i = 0; i < count && n < cap; i++) {
    if (pack::read(S_ITEMS_HIDDEN, i * 5, rec, 5) != 5) break;
    if (rec[0] != map) continue;
    out[n].x = rec[1];
    out[n].y = rec[2];
    out[n].item = rec[3];
    out[n].hidden = true;
    out[n].flag = rec[4];
    out[n].taken = pokemon::hiddenItemTaken(rec[4]);
    n++;
  }
  return n;
}

// ---- battle arithmetic

int catchPercent(const pokemon::BattleMon &e, Ball ball) {
  int r1Max = ball == Ball::POKE ? 255 : (ball == Ball::GREAT ? 200 : 150);
  int factor = ball == Ball::GREAT ? 8 : 12;
  int status = 0;
  if ((e.status & 0x07) || (e.status & 0x20)) {
    status = 25;   // asleep or frozen
  } else if (e.status & (0x08 | 0x10 | 0x40)) {
    status = 12;   // poisoned, burned or paralysed
  }
  long quarter = e.hp / 4;
  if (quarter < 1) quarter = 1;
  long w = ((long)e.maxHp * 255 / factor) / quarter;
  long num = 0;
  for (int r1 = 0; r1 <= r1Max; r1++) {
    if (status > r1) {
      num += 256;
    } else if (r1 - status > (int)e.catchRate) {
      continue;
    } else if (w > 255) {
      num += 256;
    } else {
      num += w + 1;
    }
  }
  long den = 256L * (r1Max + 1);
  return (int)((num * 100 + den / 2) / den);
}

long expForLevel(int g, int n) {
  static const int T[6][5] = {{1, 1, 0, 0, 0},     {3, 4, 10, 0, 30},
                              {3, 4, 20, 0, 70},   {6, 5, -15, 100, 140},
                              {4, 5, 0, 0, 0},     {5, 4, 0, 0, 0}};
  if (g < 0 || g > 5) g = 0;
  long n3 = (long)n * n * n;
  long v = (T[g][0] * n3) / T[g][1] + (long)T[g][2] * n * n + (long)T[g][3] * n - T[g][4];
  return v < 0 ? 0 : v;
}

long expToNextLevel(int slot, uint8_t internalIndex, uint8_t level) {
  if (level >= 100) return 0;
  uint8_t dex = dexOf(internalIndex);
  if (!dex || !pack::has(S_GROWTH)) return -1;
  long need = expForLevel(growthRate(dex), level + 1) - (long)pokemon::partyExp(slot);
  return need < 0 ? 0 : need;
}

// ---- HP bar

int hpFill(int hp, int maxHp) {
  if (maxHp <= 0 || hp <= 0) return 0;
  int e = 48 * hp / maxHp;
  if (e == 0) e = 1;
  if (e > 48) e = 48;
  return e;
}

int hpColor(int e) { return e >= 27 ? 0 : (e >= 10 ? 1 : 2); }

void reset() {
  chartTried = false;
  typeChartRecs = 0;
}

}  // namespace pokedata
