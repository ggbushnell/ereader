#!/bin/sh
# Builds ./pokeview from the firmware's own companion sources plus the shims.
set -e
cd "$(dirname "$0")"
E=..
c++ -std=c++17 -O2 -Wall -Wno-unused-function \
  -I shim -I "$E/include" -I "$E/src" \
  main.cpp ui_native.cpp viewjson.cpp assets.cpp \
  "$E/src/pokemon_state.cpp" "$E/src/gbgfx.cpp" "$E/src/pokemon_views.cpp" "$E/src/pack.cpp" \
  "$E/src/pokemon_achievements.cpp" "$E/src/pokemon_data.cpp" \
  -lz -o pokeview
# The firmware reads the pack at /games/aux/pokered.pack; the stub server's
# root keeps it at aux/pokered.pack. One symlink lets both read the same file.
GAMES_ROOT="${GAMES_ROOT:-$HOME/Projects/wifi-ereader/stub_games}"
if [ -d "$GAMES_ROOT" ]; then
  mkdir -p "$GAMES_ROOT/games"
  [ -e "$GAMES_ROOT/games/aux" ] || ln -s ../aux "$GAMES_ROOT/games/aux"
fi
echo "built ./pokeview"
