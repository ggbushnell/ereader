#!/bin/sh
# Laptop stand-in for the e-reader's games server (no hardware needed).
# Page: http://localhost:8080/   Phone on the same Wi-Fi: http://<Mac IP>:8080/
# GAMES_ROOT holds roms/, saves/, aux/pokered.pack — never inside the repo.
GAMES_ROOT="${GAMES_ROOT:-$HOME/Projects/wifi-ereader/stub_games}"
cd "$(dirname "$0")/.." || exit 1
echo "Mac LAN IP: $(ipconfig getifaddr en0 2>/dev/null || echo '?')"
exec python3 tools/www_stub_server.py --port 8080 --root "$GAMES_ROOT"
