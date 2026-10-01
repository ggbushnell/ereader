#!/bin/sh
# Virtual e-paper companion screen. Needs start-stub.sh running (port 8080).
GAMES_ROOT="${GAMES_ROOT:-$HOME/Projects/wifi-ereader/stub_games}"
cd "$(dirname "$0")" || exit 1
[ -x ./pokeview ] || ./build.sh
exec python3 eink_server.py --port 8081 --stub http://localhost:8080 --root "$GAMES_ROOT"
