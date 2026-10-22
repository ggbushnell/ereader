#!/bin/sh
# Builds pokeview as WebAssembly (out/wasm/pokeview.mjs + pokeview.wasm) from
# the same sources as build.sh, for the one-tab browser companion. Needs em++;
# CI runs it inside the emscripten/emsdk image, never on the laptop.
#
# The module keeps the CLI: the page writes the pack and a work-RAM snapshot
# into the in-memory filesystem and calls main with the usual arguments
# (Module.callMain(["--root", "/g", "--wram", "/w", "--view-json"])).
set -e
cd "$(dirname "$0")"
E=..
OUT="${OUT:-out/wasm}"
mkdir -p "$OUT"
em++ -std=c++17 -O2 -Wall -Wno-unused-function \
  -I shim -I "$E/include" -I "$E/src" \
  main.cpp ui_native.cpp viewjson.cpp assets.cpp \
  "$E/src/pokemon_state.cpp" "$E/src/gbgfx.cpp" "$E/src/pokemon_views.cpp" "$E/src/pack.cpp" \
  "$E/src/pokemon_achievements.cpp" "$E/src/pokemon_data.cpp" \
  -sUSE_ZLIB=1 -sMODULARIZE=1 -sEXPORT_ES6=1 -sEXPORT_NAME=createPokeview \
  -sINVOKE_RUN=0 -sEXIT_RUNTIME=0 -sALLOW_MEMORY_GROWTH=1 -sFORCE_FILESYSTEM=1 \
  -sENVIRONMENT=web,worker,node -sEXPORTED_RUNTIME_METHODS=callMain,FS \
  -o "$OUT/pokeview.mjs"
echo "built $OUT/pokeview.mjs + pokeview.wasm"
