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


def shq(path):
    """Single-quote a path for a remote shell."""
    return "'" + path.replace("'", "'\\''") + "'"


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
    def __init__(self, host, web_base, pi_srm=PI_SRM_DEFAULT, user="root", password=None):
        self.host, self.web_base, self.pi_srm, self.user = host, web_base, pi_srm, user
        # A device whose root filesystem cannot hold an authorized key (OnionOS:
        # squashfs, HOME=/) is driven through `expect` with its password instead.
        self.password = password
        self.state_path = web_base + ".sync.json"
        try:
            self.state = json.load(open(self.state_path))
        except Exception:
            self.state = {"web": "", "pi": ""}

    # ---- helpers
    def _ssh(self, cmd, data=None, timeout=20):
        if self.password is None:
            return subprocess.run(SSH + [self.user + "@" + self.host, cmd], input=data, capture_output=True, timeout=timeout)
        # Password path: stdin data travels inside the command as base64 (the
        # saves are 32 KB, well inside a remote command line); the remote
        # output is captured after the password exchange. Braces are stripped
        # because the command is quoted with Tcl braces.
        import base64
        if data is not None:
            cmd = "echo %s | base64 -d | (%s)" % (base64.b64encode(data).decode(), cmd)
        cmd = cmd.replace("{", "").replace("}", "")
        script = "\n".join([
            "set timeout %d" % timeout,
            "log_user 0",
            "spawn ssh -o StrictHostKeyChecking=accept-new -o PubkeyAuthentication=no %s@%s {%s}" % (self.user, self.host, cmd),
            "expect {",
            "  -re {(?i)password:} { send \"%s\\r\"; exp_continue }" % self.password,
            "  eof",
            "}",
            "puts -nonewline \"@@BEGIN@@\"",
            "puts -nonewline $expect_out(buffer)",
            "",
        ])
        r = subprocess.run(["expect", "-"], input=script.encode(), capture_output=True, timeout=timeout + 10)
        out = r.stdout.split(b"@@BEGIN@@", 1)[-1]
        if b"password:" in out:
            out = out.split(b"password:", 1)[1].lstrip(b" \r\n")
        class R:
            pass
        res = R()
        res.returncode = 0 if r.returncode == 0 else 1
        res.stdout = out.replace(b"\r\n", b"\n")
        res.stderr = r.stderr
        return res

    def _read_remote(self, path):
        """Binary-safe read of a remote file (base64 over the wire in password mode)."""
        if self.password is None:
            r = self._ssh("cat " + shq(path))
            return r.stdout if r.returncode == 0 and r.stdout else None
        import base64
        r = self._ssh("base64 " + shq(path) + " 2>/dev/null && echo @@EOF@@")
        if b"@@EOF@@" not in r.stdout:
            return None
        text = r.stdout.split(b"@@EOF@@", 1)[0]
        try:
            return base64.b64decode(b"".join(text.split()))
        except Exception:
            return None

    def pi_reachable(self):
        return self._ssh("true", timeout=8).returncode == 0

    def pi_game_running(self):
        r = self._ssh("ps | grep -i retroarch | grep -v grep | wc -l", timeout=12)
        try:
            return int(r.stdout.strip().split()[-1]) > 0
        except Exception:
            return False

    def read_pi(self):
        return self._read_remote(self.pi_srm)

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

    def _scp_to(self, local, remote):
        """Copy a local file to the device; scp in key mode, expect-driven scp in password mode."""
        target = "%s@%s:%s" % (self.user, self.host, shq(remote))
        if self.password is None:
            r = subprocess.run(["scp", "-q", "-o", "BatchMode=yes", local, target], capture_output=True, timeout=60)
            return r.returncode == 0
        # Dropbear devices often have no scp binary; stream the file over ssh's
        # stdin instead (the password still arrives on the pty, which expect owns).
        inner = "ssh -o StrictHostKeyChecking=accept-new -o PubkeyAuthentication=no %s@%s %s < %s" % (
            self.user, self.host, shq("cat > " + shq(remote)), shq(local))
        script = "\n".join([
            "set timeout 60",
            "log_user 0",
            "spawn sh -c %s" % ("{" + inner.replace("{", "").replace("}", "") + "}"),
            "expect {",
            "  -re {(?i)password:} { send \"%s\\r\"; exp_continue }" % self.password,
            "  eof",
            "}",
            "catch wait result",
            "exit [lindex $result 3]",
            "",
        ])
        r = subprocess.run(["expect", "-"], input=script.encode(), capture_output=True, timeout=90)
        return r.returncode == 0

    def install_on_pi(self, sram, why="from-web"):
        srm = shq(self.pi_srm)
        base = shq(os.path.splitext(self.pi_srm)[0])
        st = stamp()
        # The bytes go over scp (a remote command line cannot carry 32 KB on
        # every ssh server); the swap and the backups are one short command.
        tmp_local = self.web_base + ".push.tmp"
        open(tmp_local, "wb").write(sram)
        ok = self._scp_to(tmp_local, self.pi_srm + ".tmp")
        try:
            os.remove(tmp_local)
        except OSError:
            pass
        if not ok:
            log("push to the device failed (scp)")
            return False
        cmd = ("([ -e %s ] && cp %s %s.replaced-%s; true) && mv %s.tmp %s && "
               "for f in %s.state.auto %s.state.auto.png; do [ -e \"$f\" ] && mv \"$f\" \"$f.stale-%s\"; done; "
               "[ -s %s ] && echo PUSH_OK") % (srm, srm, srm, st, srm, srm, base, base, st, srm)
        r = self._ssh(cmd)
        if b"PUSH_OK" not in r.stdout:
            log("push to the device failed: " + (r.stdout + r.stderr).decode(errors="replace")[-200:])
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
    ap.add_argument("--pi-srm", default=PI_SRM_DEFAULT, help="the device's .srm path")
    ap.add_argument("--user", default="root", help="ssh user on the device (Batocera root, Onion onion)")
    ap.add_argument("--password-env", default=None, help="name of an env var holding the ssh password (devices that cannot take a key)")
    a = ap.parse_args()
    s = Sync(a.host, a.web, a.pi_srm, a.user, os.environ.get(a.password_env) if a.password_env else None)
    getattr(s, a.action)()


if __name__ == "__main__":
    main()
