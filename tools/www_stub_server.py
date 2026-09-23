#!/usr/bin/env python3
"""Local stand-in for the ESP32-S3 e-reader books and games server.

Implements the same HTTP contract as the firmware, against a folder on disk,
so the browser side can be developed and tested without the device.
Serves the UNgzipped files from ereader/www/.

    python3 tools/www_stub_server.py [--port 8080] [--root ./stub_games]
"""

import argparse
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import time
from urllib.parse import parse_qs, unquote_plus
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

# Book upload rules, the same numbers as include/config.h.
BOOK_RESERVE = 256 * 1024          # BOOK_UPLOAD_MIN_FREE_BYTES
BOOK_TXT_FACTOR = 5                # BOOK_TXT_SPACE_FACTOR
BOOK_TXT_MAX = 1024 * 1024         # BOOK_MAX_TXT_BYTES
TOOLS = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, TOOLS)


def book_header(path):
    """Title, page count of variant 0 and variant count of a .pgs, or None."""
    try:
        with open(path, "rb") as f:
            head = f.read(4096)
        magic = head[:4]
        if magic == b"MPG1":
            pages, tl = struct.unpack_from("<IH", head, 4)
            return head[10:10 + tl].decode("utf-8", "replace"), pages, 1
        if magic == b"MPG2":
            vc, tl = struct.unpack_from("<BH", head, 4)
            title = head[7:7 + tl].decode("utf-8", "replace")
            pages = struct.unpack_from("<I", head, 7 + tl + 1)[0]
            return title, pages, vc
    except (OSError, struct.error):
        pass
    return None


def multipart_file(headers, body):
    """(filename, bytes) of the first file part in a multipart body."""
    m = re.search(r'boundary=("?)([^";]+)\1', headers.get("Content-Type", ""))
    if not m:
        return None, None
    dashed = ("--" + m.group(2)).encode()
    for part in body.split(dashed):
        head, sep, data = part.partition(b"\r\n\r\n")
        fm = re.search(rb'filename="([^"]*)"', head) if sep else None
        if fm:
            if data.endswith(b"\r\n"):
                data = data[:-2]
            return fm.group(1).decode("utf-8", "replace"), data
    return None, None


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

    def books_dir(self):
        return os.path.join(self.root, "books")

    def free_bytes(self):
        st = os.statvfs(self.root)
        return st.f_bavail * st.f_frsize

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
                "total": st.f_blocks * st.f_frsize,
                "reserve": BOOK_RESERVE,
                "txtFactor": BOOK_TXT_FACTOR,
                "txtMax": BOOK_TXT_MAX,
                "books": len([n for n in os.listdir(self.books_dir()) if n.endswith(".pgs")]),
                "uptime": int(time.time() - START),
                "pack": os.path.isfile(os.path.join(self.aux_dir(), "pokered.pack")),
                "wram": WRAM["bytes"] is not None,
            })

        if p == "/api/wram":
            if WRAM["bytes"] is None:
                return self.send(404, "no snapshot")
            return self.send(200, WRAM["bytes"], "application/octet-stream")

        if p == "/api/books":
            out = []
            for name in sorted(os.listdir(self.books_dir())):
                if not name.endswith(".pgs") or name.startswith("."):
                    continue
                full = os.path.join(self.books_dir(), name)
                info = book_header(full)
                if not info:
                    continue
                out.append({"slug": name[:-4], "title": info[0], "pages": info[1],
                            "page": 0, "sizes": info[2], "size": os.path.getsize(full)})
            return self.send_json(out)

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

        if p == "/api/books/delete":
            body = self.rfile.read(length).decode("utf-8", "replace")
            slug = sanitize(parse_qs(body).get("slug", [""])[0])
            path = os.path.join(self.books_dir(), slug + ".pgs")
            if not slug or slug.startswith(".") or not os.path.isfile(path):
                return self.send_json({"ok": False, "error": "no such book"}, 404)
            os.remove(path)
            return self.send_json({"ok": True})

        if p == "/api/books/upload":
            import pdf2book
            query = parse_qs(self.path.split("?", 1)[1] if "?" in self.path else "")
            body = self.rfile.read(length)
            fname, data = multipart_file(self.headers, body)
            if fname is None:
                return self.send_json({"ok": False, "error": "no file in the request"}, 400)
            base = os.path.basename(fname)
            low = base.lower()
            if not (low.endswith(".txt") or low.endswith(".pgs")):
                return self.send_json({"ok": False, "error": "only .txt and .pgs books"}, 400)
            slug = pdf2book.slugify(base[:-4])
            if slug in ("news", "numbers"):
                slug += "_book"
            room = max(0, self.free_bytes() - BOOK_RESERVE)
            need = len(data) * (BOOK_TXT_FACTOR if low.endswith(".txt") else 1)
            if need > room:
                return self.send_json({"ok": False, "error": "not enough free space"}, 400)
            target = os.path.join(self.books_dir(), slug + ".pgs")
            if low.endswith(".pgs"):
                if data[:4] not in (b"MPG1", b"MPG2"):
                    return self.send_json({"ok": False, "error": "not a valid .pgs book: bad magic"}, 400)
                with open(target, "wb") as f:
                    f.write(data)
            else:
                title = query.get("title", [""])[0].strip() or base[:-4].replace("_", " ")
                work = tempfile.mkdtemp()
                try:
                    src = os.path.join(work, "in.txt")
                    with open(src, "wb") as f:
                        f.write(data)
                    run = subprocess.run(
                        [sys.executable, os.path.join(TOOLS, "txt2book.py"), src,
                         "--title", title, "--dual", "--out", work],
                        capture_output=True, text=True)
                    made = [n for n in os.listdir(work) if n.endswith(".pgs")]
                    if run.returncode != 0 or not made:
                        return self.send_json({"ok": False, "error": "could not paginate"}, 400)
                    shutil.move(os.path.join(work, made[0]), target)
                finally:
                    shutil.rmtree(work, ignore_errors=True)
            info = book_header(target) or ("?", 0, 1)
            return self.send_json({"ok": True, "slug": slug, "title": info[0], "pages": info[1]})

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
    os.makedirs(os.path.join(Handler.root, "books"), exist_ok=True)

    print("serving %s from %s on http://0.0.0.0:%d" % (WWW, Handler.root, args.port))
    ThreadingHTTPServer(("0.0.0.0", args.port), Handler).serve_forever()


if __name__ == "__main__":
    main()
