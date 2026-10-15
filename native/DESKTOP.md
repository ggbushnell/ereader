# Running the companion on the home desktop PC (Windows + WSL2)

**Why this file exists.** UMN Security ticket #779032 (2026-10-02): Microsoft Defender for
Endpoint on the UMN laptop flagged this project's LAN servers, a subnet sweep and password SSH.
Decision (Grace, 2026-10-15): **the whole project — runtime and development — lives on the home
desktop from now on.** The UMN laptop keeps a frozen clone behind `netguard.py` and is not used
for this project any more. This file is the handoff; it travels with the repo.

The desktop is not managed by UMN, so the Defender problem goes away. Keep the habits anyway:
servers stay localhost unless `COMPANION_LAN=1`, and nothing ever scans the network for a device.

## 1. Windows side (once)

1. **WSL2 + Ubuntu.** Admin PowerShell: `wsl --install -d Ubuntu`, reboot, pick a Linux username.
2. **Mirrored networking** so the phone / Pi / Mini can reach WSL at the desktop's own LAN IP
   (Windows 11 22H2 or newer). Create `C:\Users\<you>\.wslconfig`:
   ```ini
   [wsl2]
   networkingMode=mirrored
   ```
   then `wsl --shutdown` and reopen Ubuntu. On Windows 10 use port forwarding instead:
   `netsh interface portproxy add v4tov4 listenport=8080 connectaddress=<WSL IP> connectport=8080`
   (and 8081); the WSL IP is `hostname -I` inside Ubuntu and changes on reboot.
3. **Firewall:** allow inbound TCP 8080 and 8081 (Windows Security → Firewall → Advanced →
   Inbound Rules → New Rule → Port). If a phone still can't connect in mirrored mode, run in
   admin PowerShell: `Set-NetFirewallHyperVVMSetting -Name '{40E0AC32-46A5-438A-A0B2-2B479E8F2E90}' -DefaultInboundAction Allow`.
4. **Power:** Settings → System → Power → Sleep: Never (the always-on part is the point).

## 2. Ubuntu side (once)

```sh
sudo apt update && sudo apt install -y git python3 python3-pil build-essential zlib1g-dev openssh-client expect curl
curl -fsSL https://claude.ai/install.sh | bash        # Claude Code; then `claude` and log in (Team org)
ssh-keygen -t ed25519 -C "215737988+ggbushnell@users.noreply.github.com"
cat ~/.ssh/id_ed25519.pub     # add at github.com/settings/keys on the ggbushnell (personal) account
mkdir -p ~/Projects/wifi-ereader && cd ~/Projects/wifi-ereader
git clone -b companion-native git@github.com:ggbushnell/ereader.git
cd ereader && git remote add upstream https://github.com/AllanBinder/ereader
git config user.name "Grace Bushnell" && git config user.email "215737988+ggbushnell@users.noreply.github.com"
cd .. && git clone https://github.com/pret/pokered         # pack asset source
git clone https://github.com/pret/pokegold                 # Gen 2 symbols (pokegold.sym is in the bundle; rgbds only if you rebuild)
```

## 3. The files that are not in git (the handoff bundle)

On the laptop they were staged in **OneDrive (personal) → `wifi-ereader-handoff/`**. From Ubuntu,
OneDrive is at `/mnt/c/Users/<you>/OneDrive/wifi-ereader-handoff/`:

```sh
cd ~/Projects/wifi-ereader
unzip /mnt/c/Users/<you>/OneDrive/wifi-ereader-handoff/stub_games.zip     # roms/ saves/ aux/ books/
cp /mnt/c/Users/<you>/OneDrive/wifi-ereader-handoff/pokegold.sym pokegold/
cp /mnt/c/Users/<you>/OneDrive/wifi-ereader-handoff/{README.md,start-stub.sh,start-eink.sh} .
chmod +x start-*.sh
```

`stub_games/` = ROMs, the Red and Gold saves (including the `*.broken-113339` forensics files
from the 10-02 Gold stall), `aux/pokered.pack`, `aux/achievements.bin`. It is gitignored on purpose.

## 4. Build and run

```sh
cd ~/Projects/wifi-ereader/ereader/native && ./build.sh      # -> ./pokeview (needs zlib1g-dev)
cd ~/Projects/wifi-ereader
ereader/native/start-all.sh                                   # both servers, LAN mode, logs in this folder
```
Then on any device on the home Wi-Fi: game at `http://<desktop IP>:8080/`, companion at
`http://<desktop IP>:8081/web/` (e-paper view at `:8081/`). Gold: `COMPANION_GAME=gen2`.
`netguard.py` assumes the home subnet is `192.168.68.0/22`; set `COMPANION_HOME_NETS` if it differs.

**Keep it up across logins:** Task Scheduler → new task, trigger *At log on*, action
`wsl.exe -d Ubuntu -u <linuxuser> -- bash -lc "$HOME/Projects/wifi-ereader/ereader/native/start-all.sh"`.

**Game sources other than the browser** (see the vault's companion-sources notes, mirrored below):
- Pi 5 / Batocera: `ssh-copy-id root@batocera.local` once (default password `linux`), then
  `python3 ereader/native/tools/retroarch_wram_poller.py --host batocera.local --sync-web stub_games/saves/pokemon_red.gb`
- Miyoo Mini Plus / OnionOS: SSH only at the Onion menu with Enable on; password `onion`
  (`--user onion --password-env COMPANION_SSH_PASS`); its DHCP address changes — read it from the
  Mini's own Network menu, never by scanning. SD card is the zero-risk way to move saves.

## 5. First Claude Code session on the desktop — paste this

> Read `~/Projects/wifi-ereader/README.md` and `ereader/native/DESKTOP.md`. This is the
> Wi-Fi e-reader / Pokémon companion project, moved here from the UMN laptop on 2026-10-15.
> State: Milestone 0 done (browser + Pi 5 + Miyoo game sources, e-paper and browser companions,
> achievements, Gen 1 complete). Gen 2: decoder done and verified on a real Gold party; the
> companion runs with `COMPANION_GAME=gen2` but still shows Red's names/map. **Next action:
> `tools/pokegold_pack.py`** — same section names as the Red pack where formats match
> (`dex_order`, `species_names` 251, `base_stats` with types, `type_names`/`type_chart` with
> Dark+Steel, moves, items, `growth`), Gen-2-shaped sections for maps keyed `(group<<8)|number`,
> the two-region town map, morn/day/nite wild tables, 16 badges, GBC palettes. Then step 2c view
> tweaks. Open bug: Gold stalled twice in Chrome in PLAYERS_HOUSE_1F (MBC3 RTC script); on a
> freeze, capture the console before any reload. Parked: phone runs 0.6–0.8× speed (add an FPS
> readout first), gamepad on the phone, hardware kit decision. Rules: commit on `companion-native`
> with the repo-local identity; never scan the network for a device; the Obsidian vault is on the
> laptop, so keep project notes in `README.md` here.
