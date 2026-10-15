#!/bin/sh
# Laptop stand-in for the e-reader's games server (no hardware needed).
# Page: http://localhost:8080/
# Phone: COMPANION_LAN=1 ./start-stub.sh  -> http://<Mac IP>:8080/  (home network ONLY;
#        netguard.py refuses on any UMN network and ignores the flag elsewhere)
# GAMES_ROOT holds roms/, saves/, aux/pokered.pack — never inside the repo.
GAMES_ROOT="${GAMES_ROOT:-$HOME/Projects/wifi-ereader/stub_games}"
cd "$(dirname "$0")/.." || exit 1
BIND=$(python3 native/netguard.py --bind) || exit 1
echo "network: $(python3 native/netguard.py)  ->  binding $BIND"
exec python3 tools/www_stub_server.py --port 8080 --root "$GAMES_ROOT" --bind "$BIND"
