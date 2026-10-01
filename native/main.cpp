// pokeview: render the e-reader's Pokemon companion screen on a Mac.
//
//   pokeview --root stub_games --wram wram.bin --page 0 [--terrain] --out frame.pgm
//   pokeview --root stub_games --wram wram.bin --all out_prefix   (home, inventory, terrain, battle-if-any)
//   pokeview --root stub_games --wram wram.bin --decode            (print the decoded state as JSON)
//   pokeview --root stub_games --wram wram.bin --view-json [--page N] [--terrain]   (every fact the views draw, as JSON)
//   pokeview --root stub_games --dump-assets <dir>                 (the pack's graphics as coloured PNGs)
//
// The root is the stub server's --root (its "flash"); the pack is read from
// <root>/games/aux/pokered.pack exactly where the firmware looks, so the stub
// layout needs a games/ link (see build.sh).
#include <Arduino.h>
#include <LittleFS.h>
#include "config.h"
#include "pack.h"
#include "pokemon_achievements.h"
#include "pokemon_state.h"
#include "pokemon_views.h"
#include "gbgfx.h"
#include "ui.h"

#include <string>
#include <vector>
#include <cstdio>
#include <cstring>

void printViewJson(int page, bool terrain);   // viewjson.cpp
int dumpAssets(const std::string &dir);       // assets.cpp

static bool loadWram(const char *path) {
  FILE *f = fopen(path, "rb");
  if (!f) { fprintf(stderr, "pokeview: cannot open %s\n", path); return false; }
  size_t n = fread(pokemon::wramBuffer(), 1, pokemon::WRAM_BYTES, f);
  fclose(f);
  if (n != pokemon::WRAM_BYTES) { fprintf(stderr, "pokeview: %s is %zu bytes, want %zu\n", path, n, pokemon::WRAM_BYTES); return false; }
  pokemon::wramCommit(n);
  return true;
}

static bool writePgm(const char *path) {
  const Adafruit_GFX &g = ui::gfx();
  FILE *f = fopen(path, "wb");
  if (!f) { fprintf(stderr, "pokeview: cannot write %s\n", path); return false; }
  fprintf(f, "P5\n%d %d\n255\n", g.width(), g.height());
  const uint8_t *p = g.pixels();
  std::vector<uint8_t> row((size_t)g.width());
  for (int y = 0; y < g.height(); y++) {
    for (int x = 0; x < g.width(); x++) row[(size_t)x] = p[(size_t)y * g.width() + x] ? 255 : 0;
    fwrite(row.data(), 1, row.size(), f);
  }
  fclose(f);
  return true;
}

static std::string jsonStr(const String &s) {
  std::string o = "\"";
  for (const char *p = s.c_str(); *p; p++) {
    if (*p == '"' || *p == '\\') o += '\\';
    if ((unsigned char)*p < 0x20) continue;
    o += *p;
  }
  return o + "\"";
}

static void decodeJson() {
  pokemon::PlayTime pt = pokemon::playTime();
  printf("{");
  printf("\"player\":%s,\"rival\":%s,\"id\":%u,", jsonStr(pokemon::playerName()).c_str(),
         jsonStr(pokemon::rivalName()).c_str(), pokemon::playerId());
  printf("\"money\":%u,\"coins\":%u,\"badges\":%d,\"badgeBits\":%u,", pokemon::money(), pokemon::coins(),
         pokemon::badgeCount(), pokemon::badges());
  printf("\"map\":%u,\"x\":%u,\"y\":%u,\"facing\":%u,", pokemon::curMap(), pokemon::playerX(), pokemon::playerY(),
         pokemon::playerFacing());
  printf("\"playTime\":\"%u:%02u:%02u\",", pt.hours, pt.minutes, pt.seconds);
  printf("\"dexOwned\":%d,\"dexSeen\":%d,\"bag\":%u,\"pcItems\":%u,", pokemon::dexOwned(), pokemon::dexSeen(),
         pokemon::bagCount(), pokemon::pcItemCount());
  printf("\"inBattle\":%u,\"opponent\":%u,\"trainerClass\":%u,", pokemon::inBattle(), pokemon::opponent(),
         pokemon::trainerClass());
  printf("\"party\":[");
  int n = pokemon::partyCount();
  for (int i = 0; i < n; i++) {
    pokemon::Mon m;
    if (!pokemon::partyMon(i, m)) break;
    printf("%s{\"nick\":%s,\"species\":%u,\"level\":%u,\"hp\":%u,\"maxHp\":%u,\"status\":%u}", i ? "," : "",
           jsonStr(String(m.nick)).c_str(), m.species, m.level, m.hp, m.maxHp, m.status);
  }
  printf("]");
  if (pokemon::inBattle()) {
    pokemon::EnemyMon e;
    if (pokemon::enemyMon(e))
      printf(",\"enemy\":{\"nick\":%s,\"species\":%u,\"level\":%u,\"hp\":%u,\"maxHp\":%u}", jsonStr(String(e.nick)).c_str(),
             e.species, e.level, e.hp, e.maxHp);
  }
  // Achievements: judged now (and stamped, like a draw would).
  achievements::update();
  int next = achievements::nextStory();
  int toast = achievements::toast();
  printf(",\"achievements\":{\"earned\":%d,\"total\":%d,", achievements::earnedCount(),
         achievements::count());
  printf("\"toast\":%s,", toast >= 0 ? jsonStr(String(achievements::def(toast).title)).c_str() : "null");
  int last = achievements::lastEarned();
  if (last >= 0) {
    printf("\"last\":{\"title\":%s,\"atMinutes\":%d},", jsonStr(String(achievements::def(last).title)).c_str(),
           (int)achievements::earnedAtMinutes(last));
  } else {
    printf("\"last\":null,");
  }
  if (next >= 0) {
    printf("\"next\":{\"title\":%s,\"hint\":%s},", jsonStr(String(achievements::def(next).title)).c_str(),
           jsonStr(String(achievements::def(next).hint)).c_str());
  } else {
    printf("\"next\":null,");
  }
  printf("\"list\":[");
  for (int i = 0; i < achievements::count(); i++) {
    const achievements::Def &d = achievements::def(i);
    uint16_t at = achievements::earnedAtMinutes(i);
    printf("%s{\"title\":%s,\"story\":%s,\"earned\":%s,\"atMinutes\":%d}", i ? "," : "",
           jsonStr(String(d.title)).c_str(), d.story ? "true" : "false",
           achievements::earned(i) ? "true" : "false", at == 0xFFFF ? -1 : (int)at);
  }
  printf("]}");
  printf(",\"packSections\":%d}\n", pack::count());
}

int main(int argc, char **argv) {
  std::string root = ".", wram, out, allPrefix, assetsDir;
  int page = 0;
  bool terrain = false, decode = false, viewJson = false;
  for (int i = 1; i < argc; i++) {
    std::string a = argv[i];
    auto next = [&](std::string &dst) { if (i + 1 < argc) dst = argv[++i]; };
    if (a == "--root") next(root);
    else if (a == "--wram") next(wram);
    else if (a == "--out") next(out);
    else if (a == "--all") next(allPrefix);
    else if (a == "--page") { std::string v; next(v); page = atoi(v.c_str()); }
    else if (a == "--terrain") terrain = true;
    else if (a == "--decode") decode = true;
    else if (a == "--view-json") viewJson = true;
    else if (a == "--dump-assets") next(assetsDir);
    else { fprintf(stderr, "pokeview: unknown argument %s\n", a.c_str()); return 2; }
  }
  if (wram.empty() && assetsDir.empty()) { fprintf(stderr, "pokeview: --wram is required\n"); return 2; }

  LittleFS.setRoot(root);
  if (!pack::open()) fprintf(stderr, "pokeview: no pack at %s/%s (views fall back to NO PACK screen)\n", root.c_str(), GAMES_PACK_PATH);
  achievements::load();
  if (!assetsDir.empty()) return dumpAssets(assetsDir) > 0 ? 0 : 1;
  if (!loadWram(wram.c_str())) return 1;

  if (decode) { decodeJson(); return 0; }
  if (viewJson) { printViewJson(page, terrain); return 0; }

  if (!allPrefix.empty()) {
    struct { const char *name; int page; bool terrain; } views[] = {
        {"home", 0, false}, {"inventory", 1, false}, {"terrain", 0, true}};
    for (auto &v : views) {
      pokemon_views::draw(v.page, v.terrain);
      std::string path = allPrefix + "-" + v.name + ".pgm";
      if (!writePgm(path.c_str())) return 1;
      fprintf(stderr, "pokeview: wrote %s\n", path.c_str());
    }
    return 0;
  }
  if (out.empty()) { fprintf(stderr, "pokeview: --out or --all is required\n"); return 2; }
  pokemon_views::draw(page, terrain);
  return writePgm(out.c_str()) ? 0 : 1;
}
