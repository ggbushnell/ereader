// The companion's facts as JSON, for front ends other than the e-paper: every
// number the four views draw, taken from the same decoders (pokemon_state,
// pokemon_data, pokemon_achievements). Nothing here knows how a view looks.
#include <Arduino.h>

#include <cstdio>
#include <string>

#include "pokemon_achievements.h"
#include "pokemon_data.h"
#include "pokemon_state.h"
#include "pack.h"

using namespace pokedata;

namespace {

std::string js(const String &s) {
  std::string o = "\"";
  for (const char *p = s.c_str(); *p; p++) {
    if (*p == '"' || *p == '\\') o += '\\';
    if ((unsigned char)*p < 0x20) continue;
    o += *p;
  }
  return o + "\"";
}
std::string js(const char *s) { return js(String(s)); }

std::string typeNames(const uint8_t *ids, int n) {
  std::string o = "[";
  for (int i = 0; i < n; i++) o += (i ? "," : "") + js(typeName(ids[i]));
  return o + "]";
}

uint8_t iconOf(uint8_t dex) {
  uint8_t icon = 0;
  if (dex) pack::read("icon_map", (size_t)dex - 1, &icon, 1);
  return icon;
}

std::string monJson(const pokemon::Mon &m, int slot) {
  uint8_t dex = dexOf(m.species);
  char b[512];
  snprintf(b, sizeof(b),
           "{\"slot\":%d,\"nick\":%s,\"species\":%s,\"dex\":%u,\"icon\":%u,\"level\":%u,\"hp\":%u,\"maxHp\":%u,"
           "\"status\":%s,\"types\":[%s,%s],\"hpFill\":%d,\"hpColor\":%d,\"expToNext\":%ld}",
           slot, js(String(m.nick)).c_str(), js(speciesNameByDex(dex)).c_str(), dex, iconOf(dex), m.level, m.hp,
           m.maxHp, js(statusText(m.status, m.hp == 0)).c_str(), js(typeName(m.type1)).c_str(),
           js(typeName(m.type2)).c_str(), hpFill(m.hp, m.maxHp), hpColor(hpFill(m.hp, m.maxHp)),
           expToNextLevel(slot, m.species, m.level));
  return b;
}

std::string movesJson(const uint8_t *moves, const uint8_t *pp, uint8_t myT1, uint8_t myT2,
                      uint8_t foeT1, uint8_t foeT2) {
  std::string o = "[";
  bool first = true;
  for (int i = 0; i < 4; i++) {
    uint8_t id = moves[i];
    if (!id) continue;
    uint8_t md[4];
    if (!moveData(id, md)) continue;
    int eff = effPercent(md[0], foeT1, foeT2);
    char b[256];
    snprintf(b, sizeof(b),
             "%s{\"name\":%s,\"type\":%s,\"power\":%u,\"accuracy\":%u,\"pp\":%u,\"maxPp\":%u,"
             "\"stab\":%s,\"effPercent\":%d,\"badge\":%s}",
             first ? "" : ",", js(moveName(id)).c_str(), js(typeName(md[0])).c_str(), md[1], md[2],
             pp ? pp[i] : 0, md[3], (md[0] == myT1 || md[0] == myT2) ? "true" : "false", eff,
             js(effBadge(eff)).c_str());
    o += b;
    first = false;
  }
  return o + "]";
}

}  // namespace

// Writes one JSON object to stdout. `page`/`terrain` mirror the pad state so
// the front end knows which view the e-paper would be on.
void printViewJson(int page, bool terrain) {
  std::string o = "{";
  char b[1024];

  // ---- player
  pokemon::PlayTime pt = pokemon::playTime();
  snprintf(b, sizeof(b),
           "\"player\":{\"name\":%s,\"rival\":%s,\"id\":%u,\"money\":%u,\"coins\":%u,\"badges\":%d,"
           "\"badgeBits\":%u,\"playTime\":\"%u:%02u:%02u\",\"dexOwned\":%d,\"dexSeen\":%d},",
           js(pokemon::playerName()).c_str(), js(pokemon::rivalName()).c_str(), pokemon::playerId(),
           pokemon::money(), pokemon::coins(), pokemon::badgeCount(), pokemon::badges(), pt.hours,
           pt.minutes, pt.seconds, pokemon::dexOwned(), pokemon::dexSeen());
  o += b;

  // ---- location
  uint8_t map = pokemon::curMap();
  MapInfo mi = mapInfo(map);
  std::string visited = "[";
  for (uint8_t c = 0; c < 11; c++) visited += std::string(c ? "," : "") + (pokemon::townVisited(c) ? "true" : "false");
  visited += "]";
  // Town map cells: the entries are the game's sprite coordinates; the tile
  // is two columns right and one row down (see the Home view's cursor maths).
  uint8_t te[3];
  int townX = -1, townY = -1;
  if (pack::read("townmap_entries", (size_t)map * 3, te, 3) == 3) { townX = te[0] % 16 + 2; townY = te[1] % 16 + 1; }
  std::string towns = "[";
  for (uint8_t c = 0; c < 11; c++) {
    uint8_t t2[3];
    int tx = -1, ty = -1;
    if (pack::read("townmap_entries", (size_t)c * 3, t2, 3) == 3) { tx = t2[0] % 16 + 2; ty = t2[1] % 16 + 1; }
    snprintf(b, sizeof(b), "%s{\"map\":%u,\"name\":%s,\"tx\":%d,\"ty\":%d,\"visited\":%s}", c ? "," : "", c,
             js(mapLabel(c)).c_str(), tx, ty, pokemon::townVisited(c) ? "true" : "false");
    towns += b;
  }
  towns += "]";
  snprintf(b, sizeof(b),
           "\"location\":{\"map\":%u,\"label\":%s,\"x\":%u,\"y\":%u,\"facing\":%u,\"mapW\":%u,\"mapH\":%u,"
           "\"townX\":%d,\"townY\":%d,\"visitedTowns\":%s,\"towns\":%s,\"repelSteps\":%u},",
           map, js(mapLabel(map)).c_str(), pokemon::playerX(), pokemon::playerY(), pokemon::playerFacing(),
           mi.w, mi.h, townX, townY, visited.c_str(), towns.c_str(), pokemon::repelSteps());
  o += b;

  // ---- party
  o += "\"party\":[";
  int n = pokemon::partyCount();
  for (int i = 0; i < n; i++) {
    pokemon::Mon m;
    if (!pokemon::partyMon(i, m)) break;
    o += (i ? "," : "") + monJson(m, i);
  }
  o += "],";

  // ---- bag and PC
  o += "\"bag\":[";
  for (int i = 0; i < pokemon::bagCount(); i++) {
    uint8_t id, qty;
    if (!pokemon::bagItem(i, id, qty)) break;
    snprintf(b, sizeof(b), "%s{\"item\":%s,\"qty\":%u}", i ? "," : "", js(itemName(id)).c_str(), qty);
    o += b;
  }
  o += "],\"pcItems\":[";
  for (int i = 0; i < pokemon::pcItemCount(); i++) {
    uint8_t id, qty;
    if (!pokemon::pcItem(i, id, qty)) break;
    snprintf(b, sizeof(b), "%s{\"item\":%s,\"qty\":%u}", i ? "," : "", js(itemName(id)).c_str(), qty);
    o += b;
  }
  snprintf(b, sizeof(b), "],\"box\":{\"number\":%u,\"count\":%u},", pokemon::boxNumber(), pokemon::boxCount());
  o += b;

  // ---- terrain: encounters and pickups for the current map
  WildTable wt = wildTable(map);
  auto encs = [&](const Encounter *e, int cnt) {
    std::string s = "[";
    for (int i = 0; i < cnt; i++) {
      uint8_t dex = dexOf(e[i].species);
      snprintf(b, sizeof(b),
               "%s{\"species\":%s,\"dex\":%u,\"levelLo\":%u,\"levelHi\":%u,\"odds\":%d,\"owned\":%s,\"seen\":%s}",
               i ? "," : "", js(speciesNameByDex(dex)).c_str(), dex, e[i].levelLo, e[i].levelHi, e[i].odds,
               pokemon::dexOwnedBit(dex) ? "true" : "false", pokemon::dexSeenBit(dex) ? "true" : "false");
      s += b;
    }
    return s + "]";
  };
  snprintf(b, sizeof(b), "\"encounters\":{\"grassRatePct\":%d,\"waterRatePct\":%d,\"grass\":",
           (wt.grassRate * 100 + 128) / 256, (wt.waterRate * 100 + 128) / 256);
  o += b;
  o += encs(wt.grass, wt.nGrass) + ",\"water\":" + encs(wt.water, wt.nWater) + "},";
  MapItem items[40];
  int ni = collectItems(map, items, 40);
  o += "\"pickups\":[";
  for (int i = 0; i < ni; i++) {
    snprintf(b, sizeof(b), "%s{\"item\":%s,\"x\":%u,\"y\":%u,\"hidden\":%s,\"taken\":%s}", i ? "," : "",
             js(itemName(items[i].item)).c_str(), items[i].x, items[i].y, items[i].hidden ? "true" : "false",
             items[i].taken ? "true" : "false");
    o += b;
  }
  o += "],";

  // ---- battle
  pokemon::BattleMon enemy, mine;
  bool haveEnemy = pokemon::enemyBattleMon(enemy);
  bool haveMine = pokemon::playerBattleMon(mine);
  snprintf(b, sizeof(b), "\"battle\":{\"inBattle\":%u,\"trainer\":%s,", pokemon::inBattle(),
           pokemon::inBattle() == 2 ? "true" : "false");
  o += b;
  if (pokemon::inBattle() == 2) {
    snprintf(b, sizeof(b), "\"trainerClass\":%s,\"enemyLeft\":%d,\"enemyParty\":%u,",
             js(trainerClassName(pokemon::trainerClass())).c_str(), pokemon::enemyAliveCount(),
             pokemon::enemyPartyCount());
    o += b;
  }
  if (haveEnemy) {
    uint8_t dex = dexOf(enemy.species);
    uint8_t bs[9];
    bool haveBs = baseStats(dex, bs);
    Matchups mu = matchups(enemy.type1, enemy.type2);
    int dvSum = enemy.dvAtk + enemy.dvDef + enemy.dvSpd + enemy.dvSpc;
    snprintf(b, sizeof(b),
             "\"enemy\":{\"nick\":%s,\"species\":%s,\"dex\":%u,\"icon\":%u,\"level\":%u,\"hp\":%u,\"maxHp\":%u,\"status\":%s,"
             "\"types\":[%s,%s],\"owned\":%s,\"seen\":%s,\"catchRate\":%u,"
             "\"catchOdds\":{\"poke\":%d,\"great\":%d,\"ultra\":%d},"
             "\"baseStats\":{\"hp\":%u,\"atk\":%u,\"def\":%u,\"spd\":%u,\"spc\":%u},"
             "\"dv\":{\"atk\":%u,\"def\":%u,\"spd\":%u,\"spc\":%u,\"hp\":%u,\"sum\":%d},"
             "\"hpFill\":%d,\"hpColor\":%d,",
             js(String(enemy.nick)).c_str(), js(speciesNameByDex(dex)).c_str(), dex, iconOf(dex), enemy.level, enemy.hp,
             enemy.maxHp, js(statusText(enemy.status, enemy.hp == 0)).c_str(), js(typeName(enemy.type1)).c_str(),
             js(typeName(enemy.type2)).c_str(), pokemon::dexOwnedBit(dex) ? "true" : "false",
             pokemon::dexSeenBit(dex) ? "true" : "false", enemy.catchRate, catchPercent(enemy, Ball::POKE),
             catchPercent(enemy, Ball::GREAT), catchPercent(enemy, Ball::ULTRA), haveBs ? bs[0] : 0,
             haveBs ? bs[1] : 0, haveBs ? bs[2] : 0, haveBs ? bs[3] : 0, haveBs ? bs[4] : 0, enemy.dvAtk,
             enemy.dvDef, enemy.dvSpd, enemy.dvSpc, enemy.dvHp, dvSum, hpFill(enemy.hp, enemy.maxHp),
             hpColor(hpFill(enemy.hp, enemy.maxHp)));
    o += b;
    o += "\"weakTo\":" + typeNames(mu.weak, mu.nWeak) + ",\"resists\":" + typeNames(mu.resist, mu.nResist) +
         ",\"immuneTo\":" + typeNames(mu.immune, mu.nImmune) + ",";
    o += "\"moves\":" + movesJson(enemy.moves, enemy.pp, enemy.type1, enemy.type2,
                                  haveMine ? mine.type1 : 0xFF, haveMine ? mine.type2 : 0xFF) + "},";
  }
  if (haveMine) {
    int slot = (int)pokemon::partyMonNumber();
    snprintf(b, sizeof(b),
             "\"mine\":{\"slot\":%d,\"nick\":%s,\"species\":%s,\"level\":%u,\"hp\":%u,\"maxHp\":%u,\"status\":%s,"
             "\"types\":[%s,%s],\"stats\":{\"atk\":%u,\"def\":%u,\"spd\":%u,\"spc\":%u},\"expToNext\":%ld,"
             "\"hpFill\":%d,\"hpColor\":%d,",
             slot, js(String(mine.nick)).c_str(), js(speciesName(mine.species)).c_str(), mine.level, mine.hp,
             mine.maxHp, js(statusText(mine.status, mine.hp == 0)).c_str(), js(typeName(mine.type1)).c_str(),
             js(typeName(mine.type2)).c_str(), mine.atk, mine.def, mine.spd, mine.spc,
             expToNextLevel(slot, mine.species, mine.level), hpFill(mine.hp, mine.maxHp),
             hpColor(hpFill(mine.hp, mine.maxHp)));
    o += b;
    Matchups mm = matchups(mine.type1, mine.type2);
    o += "\"weakTo\":" + typeNames(mm.weak, mm.nWeak) + ",\"resists\":" + typeNames(mm.resist, mm.nResist) +
         ",\"immuneTo\":" + typeNames(mm.immune, mm.nImmune) + ",";
    o += "\"moves\":" + movesJson(mine.moves, mine.pp, mine.type1, mine.type2,
                                  haveEnemy ? enemy.type1 : 0xFF, haveEnemy ? enemy.type2 : 0xFF) + "},";
  }
  if (o.back() == ',') o.pop_back();
  o += "},";

  // ---- achievements
  achievements::update();
  int next = achievements::nextStory();
  int toast = achievements::toast();
  int last = achievements::lastEarned();
  snprintf(b, sizeof(b), "\"achievements\":{\"earned\":%d,\"total\":%d,", achievements::earnedCount(),
           achievements::count());
  o += b;
  o += "\"toast\":" + (toast >= 0 ? js(achievements::def(toast).title) : std::string("null")) + ",";
  o += "\"last\":" + (last >= 0 ? "{\"title\":" + js(achievements::def(last).title) + ",\"atMinutes\":" +
                                      std::to_string(achievements::earnedAtMinutes(last)) + "}"
                                : std::string("null")) + ",";
  o += "\"next\":" + (next >= 0 ? "{\"title\":" + js(achievements::def(next).title) + ",\"hint\":" +
                                      js(achievements::def(next).hint) + "}"
                                : std::string("null")) + ",\"list\":[";
  for (int i = 0; i < achievements::count(); i++) {
    const achievements::Def &d = achievements::def(i);
    uint16_t at = achievements::earnedAtMinutes(i);
    snprintf(b, sizeof(b), "%s{\"title\":%s,\"hint\":%s,\"story\":%s,\"earned\":%s,\"atMinutes\":%d}", i ? "," : "",
             js(d.title).c_str(), js(d.hint).c_str(), d.story ? "true" : "false",
             achievements::earned(i) ? "true" : "false", at == 0xFFFF ? -1 : (int)at);
    o += b;
  }
  o += "]},";

  // ---- which view the e-paper shows for this pad state
  const char *view = !pack::isOpen() ? "nopack" : pokemon::inBattle() ? "battle" : terrain ? "terrain"
                     : (page % 3 == 1) ? "inventory" : (page % 3 == 2) ? "awards" : "home";
  snprintf(b, sizeof(b), "\"view\":%s,\"page\":%d,\"terrain\":%s,\"packSections\":%d}\n", js(view).c_str(),
           page, terrain ? "true" : "false", pack::count());
  o += b;
  fputs(o.c_str(), stdout);
}
