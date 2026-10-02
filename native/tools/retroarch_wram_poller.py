#!/usr/bin/env python3
"""Mirror a RetroArch core's Game Boy work RAM into the companion's stub server.

RetroArch (on Batocera, OnionOS, a desktop) exposes a UDP network command
interface when `network_cmd_enable = true` (default port 55355). Its
READ_CORE_MEMORY command returns bytes at a core virtual address as hex text:

    > READ_CORE_MEMORY c000 16
    < READ_CORE_MEMORY c000 00 11 22 ...

Gambatte and mGBA map the Game Boy's address space, so C000..DFFF is the 8 KB
work RAM the companion decodes. This script reads it in chunks once a second,
and when it changed, POSTs the 8192 raw bytes to the stub's /api/wram exactly
like the browser page does. Nothing downstream can tell the difference.

    python3 retroarch_wram_poller.py --host batocera.local [--port 55355]
                                     [--stub http://localhost:8080] [--chunk 1024]
                                     [--sync-web <stub root>/saves/pokemon_red.gb]
                                     [--user onion --pi-srm "/mnt/SDCARD/Saves/CurrentProfile/saves/Gambatte/<rom>.srm"]

With --sync-web the battery save is kept in step as well (save_sync.py): when
the Pi's game stops, its save is pulled to the web side if it is ahead; while
the Pi sits at its menu, a newer web save is pushed to it. Save states on the
receiving side are moved aside (dated), never deleted.

Setup on Batocera: append to /userdata/system/configs/retroarch/retroarchcustom.cfg
    network_cmd_enable = "true"
    network_cmd_port = "55355"
then launch the game. Test one read first:
    printf 'READ_CORE_MEMORY c000 16' | nc -u -w1 batocera.local 55355
"""
import argparse, hashlib, os, socket, sys, time, urllib.request

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
try:
    from save_sync import Sync
except ImportError:
    Sync = None

WRAM_BASE = 0xC000
WRAM_BYTES = 0x2000


def read_memory(sock, addr, n, timeout=1.0):
    sock.settimeout(timeout)
    sock.send(f"READ_CORE_MEMORY {addr:x} {n}".encode())
    data = sock.recv(65535).decode("ascii", "replace").split()
    # "READ_CORE_MEMORY c000 00 11 ..." or "READ_CORE_MEMORY c000 -1" when the core has no map
    if len(data) < 3 or data[0] != "READ_CORE_MEMORY":
        raise RuntimeError("unexpected reply: %r" % " ".join(data[:4]))
    if data[2] == "-1":
        raise RuntimeError("core reports no memory at %x (no memory map? wrong core?)" % addr)
    return bytes(int(x, 16) for x in data[2:])


def read_wram(sock, chunk, timeout=2.5):
    """All chunks requested at once, replies matched by address: one round trip
    per snapshot even on a slow Wi-Fi handheld. Some RetroArch builds cap a
    reply at a few hundred bytes (OnionOS 1.15: ~330), so --chunk 256 is safe
    everywhere; a bigger chunk is only faster where it is honoured."""
    want = {}
    for off in range(0, WRAM_BYTES, chunk):
        n = min(chunk, WRAM_BYTES - off)
        want[WRAM_BASE + off] = n
        sock.send(f"READ_CORE_MEMORY {WRAM_BASE + off:x} {n}".encode())
    got = {}
    deadline = time.time() + timeout
    while len(got) < len(want):
        left = deadline - time.time()
        if left <= 0:
            raise RuntimeError("timed out with %d of %d chunks" % (len(got), len(want)))
        sock.settimeout(left)
        data = sock.recv(65535).decode("ascii", "replace").split()
        if len(data) < 3 or data[0] != "READ_CORE_MEMORY":
            continue
        addr = int(data[1], 16)
        if addr not in want:
            continue
        if data[2] == "-1":
            raise RuntimeError("core reports no memory at %x (no memory map? wrong core?)" % addr)
        part = bytes(int(x, 16) for x in data[2:])
        if len(part) != want[addr]:
            raise RuntimeError("short read at %x: %d of %d bytes (lower --chunk)" % (addr, len(part), want[addr]))
        got[addr] = part
    return b"".join(got[a] for a in sorted(got))


def post_wram(stub, wram):
    req = urllib.request.Request(stub + "/api/wram", data=wram, method="POST",
                                 headers={"Content-Type": "application/octet-stream"})
    with urllib.request.urlopen(req, timeout=3) as r:
        return r.status


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", required=True)
    ap.add_argument("--port", type=int, default=55355)
    ap.add_argument("--stub", default="http://localhost:8080")
    ap.add_argument("--chunk", type=int, default=256)
    ap.add_argument("--interval", type=float, default=1.0)
    ap.add_argument("--sync-web", default=None, help="stub save base path to keep in step with the Pi's .srm")
    ap.add_argument("--pi-srm", default="/userdata/saves/gb/pokemon_red.srm", help="the device's .srm path")
    ap.add_argument("--user", default="root", help="ssh user on the device (Batocera root, Onion onion)")
    ap.add_argument("--password-env", default=None, help="env var with the ssh password, for a device that cannot take a key")
    a = ap.parse_args()
    pw = os.environ.get(a.password_env) if a.password_env else None
    sync = Sync(a.host, a.sync_web, a.pi_srm, a.user, pw) if (a.sync_web and Sync) else None
    if a.sync_web and not Sync:
        print("save_sync.py not found next to this script; running without save sync", flush=True)

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.connect((a.host, a.port))
    print(f"polling {a.host}:{a.port} every {a.interval}s -> {a.stub}/api/wram", flush=True)
    last = None
    posts = fails = 0
    running = False          # a game answered on the last tick
    stopped_at = None        # when it stopped answering
    next_push_check = 0.0
    while True:
        t0 = time.time()
        if sync and running is False and stopped_at is not None and t0 - stopped_at > 6:
            # The game just ended: RetroArch has written its .srm by now.
            stopped_at = None
            try:
                sync.pull()
            except Exception as e:
                print("sync pull failed:", e, flush=True)
        if sync and not running and t0 >= next_push_check:
            next_push_check = t0 + 30
            try:
                if sync.pi_reachable() and not sync.pi_game_running():
                    sync.push()
            except Exception as e:
                print("sync push check failed:", e, flush=True)
        try:
            wram = read_wram(sock, a.chunk)
            h = hashlib.sha1(wram).hexdigest()
            if h != last:
                post_wram(a.stub, wram)
                last = h
                posts += 1
                if posts % 10 == 1:
                    print(f"posted #{posts} ({sum(1 for b in wram if b)} nonzero bytes)", flush=True)
            fails = 0
            if not running:
                print("game running on the Pi", flush=True)
            running = True
        except (socket.timeout, ConnectionRefusedError, OSError, RuntimeError) as e:
            fails += 1
            if running:
                running = False
                stopped_at = time.time()
                print("game stopped on the Pi", flush=True)
            if fails in (1, 10, 60):
                print(f"no memory from RetroArch ({e}); is a game running with network commands on?", flush=True)
        time.sleep(max(0.0, a.interval - (time.time() - t0)))


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        pass
