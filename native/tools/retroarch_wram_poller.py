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

Setup on Batocera: append to /userdata/system/configs/retroarch/retroarchcustom.cfg
    network_cmd_enable = "true"
    network_cmd_port = "55355"
then launch the game. Test one read first:
    printf 'READ_CORE_MEMORY c000 16' | nc -u -w1 batocera.local 55355
"""
import argparse, hashlib, socket, sys, time, urllib.request

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


def read_wram(sock, chunk):
    out = bytearray()
    for off in range(0, WRAM_BYTES, chunk):
        part = read_memory(sock, WRAM_BASE + off, min(chunk, WRAM_BYTES - off))
        if len(part) != min(chunk, WRAM_BYTES - off):
            raise RuntimeError("short read at %x: %d bytes" % (WRAM_BASE + off, len(part)))
        out += part
    return bytes(out)


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
    ap.add_argument("--chunk", type=int, default=1024)
    ap.add_argument("--interval", type=float, default=1.0)
    a = ap.parse_args()

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.connect((a.host, a.port))
    print(f"polling {a.host}:{a.port} every {a.interval}s -> {a.stub}/api/wram", flush=True)
    last = None
    posts = fails = 0
    while True:
        t0 = time.time()
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
        except (socket.timeout, ConnectionRefusedError, OSError, RuntimeError) as e:
            fails += 1
            if fails in (1, 10, 60):
                print(f"no memory from RetroArch ({e}); is a game running with network commands on?", flush=True)
        time.sleep(max(0.0, a.interval - (time.time() - t0)))


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        pass
