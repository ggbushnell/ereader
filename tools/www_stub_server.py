#!/usr/bin/env python3
"""Local stand-in for the ESP32-S3 e-reader game server.

Implements the same HTTP contract as the firmware, against a folder on disk,
so the browser side can be developed and tested without the device.
Serves the UNgzipped files from ereader/www/.

    python3 tools/www_stub_server.py [--port 8080] [--root ./stub_games]
"""

import argparse
import json
import os
import re
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

WWW = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "www")
WWW = os.path.normpath(WWW)

ROM_EXT = (".nes", ".gb", ".gbc")
PACK_EXT = ".pack"
SAVE_EXT = (".sav", ".state", ".auto")
MAX_SAVE = 1024 * 1024
WRAM_BYTES = 8192

# Last work RAM snapshot posted by the page, held in memory exactly like the
# firmware holds it. Not written to disk on either side.
WRAM = {"bytes": None, "stamp": 0.0, "count": 0}
SAFE = re.compile(r"[^A-Za-z0-9._-]")
START = time.time()


def sanitize(name):
    """Same rule as the firmware: non-portable characters become _, cut to 40."""
    return SAFE.sub("_", os.path.basename(name or ""))[:40]


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    root = "./stub_games"

    def log_message(self, fmt, *args):
        print("[stub] " + fmt % args)

    # helpers

    def send(self, code, body=b"", ctype="text/plain", extra=None):
        if isinstance(body, str):
            body = body.encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        for k, v in (extra or {}).items():
            self.send_header(k, v)
        self.end_headers()
        if self.command != "HEAD":
            self.wfile.write(body)

    def send_json(self, obj, code=200):
        self.send(code, json.dumps(obj), "application/json")

    def redirect(self):
        self.send(303, b"", "text/plain", {"Location": "/"})

    def send_file(self, path, ctype):
        if not os.path.isfile(path):
            return self.send(404, "not found")
        with open(path, "rb") as f:
            self.send(200, f.read(), ctype)

    def roms_dir(self):
        return os.path.join(self.root, "roms")

    def saves_dir(self):
        return os.path.join(self.root, "saves")

    def aux_dir(self):
        return os.path.join(self.root, "aux")

    # routing

    def do_GET(self):
        p = self.path.split("?", 1)[0]

        if p == "/" or p == "/index.html":
            return self.send_file(os.path.join(WWW, "index.html"), "text/html")
        if p == "/app.js":
            return self.send_file(os.path.join(WWW, "app.js"), "application/javascript")
        if p == "/nes.js":
            return self.send_file(os.path.join(WWW, "vendor", "nes.js"), "application/javascript")
        if p == "/gb.js":
            return self.send_file(os.path.join(WWW, "vendor", "gb.js"), "application/javascript")

        if p == "/api/ping":
            st = os.statvfs(self.root)
            return self.send_json({
                "ok": True,
                "free": st.f_bavail * st.f_frsize,
                "uptime": int(time.time() - START),
                "pack": os.path.isfile(os.path.join(self.aux_dir(), "pokered.pack")),
                "wram": WRAM["bytes"] is not None,
            })

        if p == "/api/wram":
            if WRAM["bytes"] is None:
                return self.send(404, "no snapshot")
            return self.send(200, WRAM["bytes"], "application/octet-stream")

        if p == "/api/roms":
            out = []
            for name in sorted(os.listdir(self.roms_dir())):
                if not name.lower().endswith(ROM_EXT):
                    continue
                full = os.path.join(self.roms_dir(), name)
                out.append({
                    "name": name,
                    "size": os.path.getsize(full),
                    "sav": os.path.isfile(os.path.join(self.saves_dir(), name + ".sav")),
                    "state": os.path.isfile(os.path.join(self.saves_dir(), name + ".state")),
                    "auto": os.path.isfile(os.path.join(self.saves_dir(), name + ".auto")),
                })
            return self.send_json(out)

        if p.startswith("/roms/"):
            name = sanitize(p[len("/roms/"):])
            if not name.lower().endswith(ROM_EXT):
                return self.send(400, "bad extension")
            return self.send_file(os.path.join(self.roms_dir(), name), "application/octet-stream")

        if p.startswith("/saves/"):
            name = sanitize(p[len("/saves/"):])
            if not name.lower().endswith(SAVE_EXT):
                return self.send(400, "bad extension")
            return self.send_file(os.path.join(self.saves_dir(), name), "application/octet-stream")

        return self.send(404, "not found")

    def do_HEAD(self):
        self.do_GET()

    def do_POST(self):
        p = self.path.split("?", 1)[0]
        length = int(self.headers.get("Content-Length") or 0)

        if p.startswith("/saves/"):
            name = sanitize(p[len("/saves/"):])
            if not name.lower().endswith(SAVE_EXT):
                return self.send(400, "bad extension")
            if length > MAX_SAVE:
                return self.send(413, "too large")
            body = self.rfile.read(length)
            os.makedirs(self.saves_dir(), exist_ok=True)
            with open(os.path.join(self.saves_dir(), name), "wb") as f:
                f.write(body)
            return self.send_json({"ok": True, "size": len(body)})

        if p == "/api/wram":
            body = self.rfile.read(length)
            if len(body) != WRAM_BYTES:
                return self.send_json({"ok": False, "want": WRAM_BYTES}, 400)
            WRAM["bytes"] = body
            WRAM["stamp"] = time.time()
            WRAM["count"] += 1
            return self.send_json({"ok": True, "size": len(body), "n": WRAM["count"]})

        if p == "/api/delete":
            body = self.rfile.read(length).decode("utf-8", "replace")
            name = ""
            for pair in body.split("&"):
                if pair.startswith("name="):
                    from urllib.parse import unquote_plus
                    name = sanitize(unquote_plus(pair[5:]))
            if name:
                for path in [os.path.join(self.roms_dir(), name)] + [
                    os.path.join(self.saves_dir(), name + e) for e in SAVE_EXT
                ]:
                    if os.path.isfile(path):
                        os.remove(path)
            return self.redirect()

        if p == "/api/upload":
            ctype = self.headers.get("Content-Type", "")
            m = re.search(r'boundary=("?)([^";]+)\1', ctype)
            if not m:
                return self.send(400, "no boundary")
            body = self.rfile.read(length)
            dashed = ("--" + m.group(2)).encode()
            for part in body.split(dashed):
                head, sep, data = part.partition(b"\r\n\r\n")
                if not sep or b'name="rom"' not in head:
                    continue
                fm = re.search(rb'filename="([^"]*)"', head)
                if not fm:
                    continue
                name = sanitize(fm.group(1).decode("utf-8", "replace"))
                low = name.lower()
                if low.endswith(PACK_EXT):
                    target = self.aux_dir()
                elif low.endswith(ROM_EXT):
                    target = self.roms_dir()
                else:
                    return self.send(400, "bad extension")
                os.makedirs(target, exist_ok=True)
                with open(os.path.join(target, name), "wb") as f:
                    f.write(data.rstrip(b"\r\n-"))
                return self.redirect()
            return self.send(400, "no rom field")

        return self.send(404, "not found")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=8080)
    ap.add_argument("--root", default="./stub_games")
    args = ap.parse_args()

    Handler.root = os.path.abspath(args.root)
    os.makedirs(os.path.join(Handler.root, "roms"), exist_ok=True)
    os.makedirs(os.path.join(Handler.root, "saves"), exist_ok=True)
    os.makedirs(os.path.join(Handler.root, "aux"), exist_ok=True)

    print("serving %s from %s on http://0.0.0.0:%d" % (WWW, Handler.root, args.port))
    ThreadingHTTPServer(("0.0.0.0", args.port), Handler).serve_forever()


if __name__ == "__main__":
    main()
