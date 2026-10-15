#!/bin/sh
# Start both companion servers in the background (desktop PC / WSL2 use).
# Logs: ~/Projects/wifi-ereader/{stub_server,eink_server}.log
# Phone/Pi/Mini reach them at the desktop's LAN IP; COMPANION_LAN=1 is the default here
# because this script exists for the always-on home desktop. netguard still refuses on UMN.
export COMPANION_LAN="${COMPANION_LAN:-1}"
W="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$W" || exit 1
pgrep -f www_stub_server.py >/dev/null || (nohup ereader/native/start-stub.sh > stub_server.log 2>&1 &)
sleep 2
pgrep -f eink_server.py >/dev/null || (nohup ereader/native/start-eink.sh > eink_server.log 2>&1 &)
sleep 2
echo "--- stub ---"; head -2 stub_server.log; echo "--- companion ---"; head -2 eink_server.log
