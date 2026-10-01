#!/usr/bin/env python3
"""Keep the Pokemon Red battery save in step between the games server and a Pi.

Two homes for the same 32 KB save:
  web  <stub root>/saves/<rom>.sav          (the browser page loads it at start)
  pi   /userdata/saves/gb/<rom>.srm          (RetroArch loads it when the game starts
                                              and rewrites it when the game exits)

Only the in-game save travels. Save states (web .auto/.state, Pi .state.auto)
are emulator-specific, and both sides prefer a state over the battery save
when one exists, so whenever a save is installed on a side, that side's
states are moved aside with a dated suffix (never deleted). The web page's
"Continue" then falls back to Play, which loads the installed save and lets
the game's own title screen offer CONTINUE.

Which side wins: each side's save is "changed" when its hash differs from the
hash recorded at the last sync. One side changed -> it wins. Both changed ->
the save with the larger in-game play time wins (SRAM 0x2CED..0x2CF0, the
copy of wPlayTimeHours/Maxed/Minutes/Seconds), ties going to the side that
just finished running. The loser is kept as <file>.replaced-<stamp>.

    save_sync.py status|pull|push --host batocera.local --web <stub root>/saves/pokemon_red.gb
"""
import argparse, hashlib, json, os, shutil, subprocess, sys, time

PI_SRM_DEFAULT = "/userdata/saves/gb/pokemon_red.srm"
PLAYTIME_OFF = 0x2CED
SSH = ["ssh", "-o", "BatchMode=yes", "-o", "ConnectTimeout=4"]


def log(msg):
    print(time.strftime("%H:%M:%S") + "  sync: " + msg, flush=True)


def stamp():
    return time.strftime("%Y%m%d-%H%M%S")


def sha(b):
    return hashlib.sha1(b).hexdigest() if b else ""


def playtime(sram):
    if not sram or len(sram) < PLAYTIME_OFF + 4:
        return -1
    h, _maxed, m, s = sram[PLAYTIME_OFF:PLAYTIME_OFF + 4]
    return h * 3600 + m * 60 + s


def fmt_pt(sec):
    return "n/a" if sec < 0 else "%d:%02d:%02d" % (sec // 3600, sec % 3600 // 60, sec % 60)


def valid(sram):
    return sram is not None and len(sram) == 32768 and 0x80 <= sram[0x2598] <= 0x99


class Sync:
    def __init__(self, host, web_base, pi_srm=PI_SRM_DEFAULT):
        self.host, self.web_base, self.pi_srm = host, web_base, pi_srm
        self.state_path = web_base + ".sync.json"
        try:
            self.state = json.load(open(self.state_path))
        except Exception:
            self.state = {"web": "", "pi": ""}

    # ---- helpers
    def _ssh(self, cmd, data=None, timeout=20):
        return subprocess.run(SSH + ["root@" + self.host, cmd], input=data, capture_output=True, timeout=timeout)

    def pi_reachable(self):
        return self._ssh("true", timeout=8).returncode == 0

    def pi_game_running(self):
        r = self._ssh("pgrep -c retroarch", timeout=8)
        return r.returncode == 0 and r.stdout.strip() not in (b"", b"0")

    def read_pi(self):
        r = self._ssh("cat " + self.pi_srm)
        return r.stdout if r.returncode == 0 and r.stdout else None

    def read_web(self):
        p = self.web_base + ".sav"
        return open(p, "rb").read() if os.path.exists(p) else None

    def _save_state(self):
        json.dump(self.state, open(self.state_path, "w"))

    def _aside(self, path, why):
        if os.path.exists(path):
            dst = path + "." + why + "-" + stamp()
            shutil.move(path, dst)
            log("moved aside %s -> %s" % (os.path.basename(path), os.path.basename(dst)))

    # ---- the two directions
    def install_on_web(self, sram, why="from-pi"):
        p = self.web_base + ".sav"
        if os.path.exists(p):
            shutil.copy2(p, p + ".replaced-" + stamp())
        open(p, "wb").write(sram)
        for ext in (".auto", ".state"):
            self._aside(self.web_base + ext, "stale")
        self.state["web"] = self.state["pi"] = sha(sram)
        self._save_state()
        log("installed Pi save on the web side (%s, play time %s)" % (why, fmt_pt(playtime(sram))))

    def install_on_pi(self, sram, why="from-web"):
        d = os.path.dirname(self.pi_srm)
        base = os.path.splitext(self.pi_srm)[0]
        st = stamp()
        cmd = ("cd %s && ([ -e %s ] && cp %s %s.replaced-%s; true) && cat > %s.tmp && mv %s.tmp %s && "
               "for f in %s.state.auto %s.state.auto.png; do [ -e $f ] && mv $f $f.stale-%s; done; true") % (
            d, self.pi_srm, self.pi_srm, self.pi_srm, st, self.pi_srm, self.pi_srm, self.pi_srm, base, base, st)
        r = self._ssh(cmd, data=sram)
        if r.returncode != 0:
            log("push to Pi failed: " + r.stderr.decode(errors="replace")[-200:])
            return False
        self.state["web"] = self.state["pi"] = sha(sram)
        self._save_state()
        log("installed web save on the Pi (%s, play time %s)" % (why, fmt_pt(playtime(sram))))
        return True

    # ---- policy
    def decide(self, web, pi, just_ran=None):
        """Returns ('web'|'pi'|None, reason)."""
        hw, hp = sha(web), sha(pi)
        if hw == hp:
            return None, "already identical"
        wchg = hw != self.state.get("web", "")
        pchg = hp != self.state.get("pi", "")
        if not valid(pi):
            return ("web", "Pi save empty or invalid") if valid(web) else (None, "neither side holds a valid save")
        if not valid(web):
            return "pi", "web save empty or invalid"
        if pchg and not wchg:
            return "pi", "only the Pi changed since the last sync"
        if wchg and not pchg:
            return "web", "only the web changed since the last sync"
        tw, tp = playtime(web), playtime(pi)
        if tp > tw:
            return "pi", "both changed; Pi play time %s > web %s" % (fmt_pt(tp), fmt_pt(tw))
        if tw > tp:
            return "web", "both changed; web play time %s > Pi %s" % (fmt_pt(tw), fmt_pt(tp))
        return (just_ran or "pi"), "both changed, equal play time; the side that just ran wins"

    def pull(self):
        """Pi game just stopped: bring its save to the web side if it should win."""
        pi, web = self.read_pi(), self.read_web()
        if pi is None:
            log("no save file on the Pi")
            return
        winner, why = self.decide(web, pi, just_ran="pi")
        if winner == "pi":
            self.install_on_web(pi, why)
        elif winner == "web":
            log("web save is ahead (%s); push it before the next Pi session" % why)
        else:
            log(why)

    def push(self):
        """Pi idle at its menu: send the web save over if it should win."""
        if self.pi_game_running():
            log("Pi game running; not touching its save")
            return
        pi, web = self.read_pi(), self.read_web()
        winner, why = self.decide(web, pi, just_ran="web")
        if winner == "web":
            self.install_on_pi(web, why)
        elif winner == "pi":
            log("Pi save is ahead (%s); it goes to the web when its game next stops" % why)
        else:
            log(why)

    def status(self):
        pi, web = self.read_pi(), self.read_web()
        print("web  %s  play %s  %s" % (sha(web)[:8] or "(none)", fmt_pt(playtime(web)), "valid" if valid(web) else "invalid/empty"))
        print("pi   %s  play %s  %s" % (sha(pi)[:8] or "(none)", fmt_pt(playtime(pi)), "valid" if valid(pi) else "invalid/empty"))
        print("last sync  web %s  pi %s" % (self.state.get("web", "")[:8] or "-", self.state.get("pi", "")[:8] or "-"))
        print("decision:", self.decide(web, pi))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("action", choices=["status", "pull", "push"])
    ap.add_argument("--host", default="batocera.local")
    ap.add_argument("--web", required=True, help="stub save base path, e.g. stub_games/saves/pokemon_red.gb")
    ap.add_argument("--pi-srm", default=PI_SRM_DEFAULT)
    a = ap.parse_args()
    s = Sync(a.host, a.web, a.pi_srm)
    getattr(s, a.action)()


if __name__ == "__main__":
    main()
