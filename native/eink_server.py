#!/usr/bin/env python3
"""Virtual e-ink companion screen for the laptop (Stage B1).

Polls the stub games server for the Game Boy work RAM mirror, renders the
firmware's own companion views through ./pokeview, and serves the frame as a
680x920 PNG on a page that refreshes once a second. The pad buttons follow
src/games.cpp: UP next page, DOWN previous page, LEFT terrain on/off.

    python3 eink_server.py [--stub http://localhost:8080] [--port 8081] [--root ../stub_games]
"""
import argparse, hashlib, io, json, os, subprocess, sys, threading, time, urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

try:
    from PIL import Image
except ImportError:
    sys.exit("eink_server: pip install pillow")

HERE = os.path.dirname(os.path.abspath(__file__))
POKEVIEW = os.path.join(HERE, "pokeview")

state = {
    "page": 0, "terrain": False, "counter": 0, "wram_ok": False, "last_wram_at": 0.0,
    "render_ms": 0, "renders": 0, "decode": {}, "error": "", "view": "no snapshot",
}
frame_png = b""
frame_lock = threading.Lock()
pad_event = threading.Event()
ARGS = None


def fetch_wram():
    try:
        with urllib.request.urlopen(ARGS.stub + "/api/wram", timeout=2) as r:
            if r.status != 200:
                return None
            data = r.read()
            return data if len(data) == 8192 else None
    except Exception as e:  # stub down or no snapshot yet (404)
        state["error"] = str(e)[:120]
        return None


def render(wram, page, terrain):
    global frame_png
    wram_path = os.path.join(HERE, "out", "live.wram")
    pgm_path = os.path.join(HERE, "out", "live.pgm")
    with open(wram_path, "wb") as f:
        f.write(wram)
    t0 = time.time()
    cmd = [POKEVIEW, "--root", ARGS.root, "--wram", wram_path, "--page", str(page), "--out", pgm_path]
    if terrain:
        cmd.append("--terrain")
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        state["error"] = r.stderr[-300:]
        return
    d = subprocess.run([POKEVIEW, "--root", ARGS.root, "--wram", wram_path, "--decode"], capture_output=True, text=True)
    try:
        state["decode"] = json.loads(d.stdout)
    except Exception:
        state["decode"] = {}
    im = Image.open(pgm_path).convert("1")
    buf = io.BytesIO()
    im.save(buf, format="PNG", optimize=True)
    with frame_lock:
        frame_png = buf.getvalue()
    state["render_ms"] = int((time.time() - t0) * 1000)
    state["renders"] += 1
    state["error"] = ""
    dec = state["decode"]
    if dec.get("inBattle"):
        state["view"] = "BATTLE"
    elif terrain:
        state["view"] = "TERRAIN"
    else:
        state["view"] = "INVENTORY" if page % 2 else "HOME"


def poll_loop():
    last_hash = None
    last_pad = None
    while True:
        wram = fetch_wram()
        if wram is not None:
            state["wram_ok"] = True
            state["last_wram_at"] = time.time()
            h = hashlib.sha1(wram).hexdigest()
            pad = (state["page"], state["terrain"])
            if h != last_hash or pad != last_pad or pad_event.is_set():
                pad_event.clear()
                last_hash, last_pad = h, pad
                state["counter"] += 1
                render(wram, *pad)
        else:
            state["wram_ok"] = False
        pad_event.wait(ARGS.interval)
        pad_event.clear()


PAGE = """<!doctype html><html><head><meta charset="utf-8"><title>Companion e-ink</title>
<meta name="viewport" content="width=device-width,initial-scale=1">
<style>
 body{margin:0;background:#2b2b2b;color:#ddd;font:14px -apple-system,Helvetica,sans-serif;display:flex;flex-direction:column;align-items:center}
 .panel{margin:16px;padding:14px;background:#d9d6cf;border-radius:10px;box-shadow:0 8px 30px rgba(0,0,0,.6)}
 img{display:block;width:340px;height:460px;image-rendering:pixelated;background:#fff}
 img.big{width:680px;height:920px}
 .bar{display:flex;gap:8px;flex-wrap:wrap;justify-content:center;margin:4px 16px 16px}
 button{background:#444;color:#eee;border:1px solid #666;border-radius:6px;padding:8px 14px;font-size:14px;cursor:pointer}
 button:hover{background:#555}
 #s{font-family:ui-monospace,Menlo,monospace;font-size:12px;color:#bbb;max-width:680px;text-align:center;margin:0 16px 20px;white-space:pre-wrap}
 .warn{color:#f6c343}
</style></head><body>
<div class="bar">
 <button onclick="pad('up')">▲ UP · next page</button>
 <button onclick="pad('down')">▼ DOWN · prev page</button>
 <button onclick="pad('left')">◀ LEFT · terrain</button>
 <button onclick="toggleSize()">1:1 / fit</button>
</div>
<div class="panel"><img id="f" src="/frame.png" alt="companion frame"></div>
<div id="s">connecting…</div>
<script>
const img=document.getElementById('f'),s=document.getElementById('s');
function pad(d){fetch('/pad?dir='+d,{method:'POST'}).then(tick)}
function toggleSize(){img.classList.toggle('big')}
document.addEventListener('keydown',e=>{const m={ArrowUp:'up',ArrowDown:'down',ArrowLeft:'left'};if(m[e.key]){e.preventDefault();pad(m[e.key])}});
let last=-1;
async function tick(){
  try{
    const r=await fetch('/state.json',{cache:'no-store'});const st=await r.json();
    if(st.renders!==last){last=st.renders;img.src='/frame.png?'+last}
    const d=st.decode||{};const age=st.last_wram_at?Math.round(Date.now()/1000-st.last_wram_at):null;
    s.innerHTML=(st.wram_ok?'':'<span class=warn>no WRAM from the stub server yet — start Pokémon Red at '+st.stub+'</span>\\n')
      +`view ${st.view}  ·  page ${st.page}  ·  terrain ${st.terrain?'on':'off'}  ·  render #${st.renders} (${st.render_ms} ms)`
      +(age!==null?`  ·  snapshot ${age}s ago`:'')
      +(d.player?`\\n${d.player} vs ${d.rival}  ·  ¥${d.money}  ·  map ${d.map} (${d.x},${d.y})  ·  party ${(d.party||[]).length}  ·  badges ${d.badges}  ·  battle ${d.inBattle}`:'')
      +(st.error?`\\n<span class=warn>${st.error}</span>`:'');
  }catch(e){s.textContent='companion server unreachable';}
}
tick();setInterval(tick,1000);
</script></body></html>"""


class H(BaseHTTPRequestHandler):
    def log_message(self, fmt, *a):
        if "/state.json" in fmt % a or "/frame.png" in fmt % a:
            return
        sys.stderr.write("[eink] " + fmt % a + "\n")

    def send(self, code, ctype, body):
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        p = self.path.split("?")[0]
        if p in ("/", "/eink"):
            return self.send(200, "text/html; charset=utf-8", PAGE.encode())
        if p == "/frame.png":
            with frame_lock:
                png = frame_png
            if not png:
                im = Image.new("1", (680, 920), 1)
                buf = io.BytesIO(); im.save(buf, format="PNG"); png = buf.getvalue()
            return self.send(200, "image/png", png)
        if p == "/state.json":
            st = dict(state); st["stub"] = ARGS.stub
            return self.send(200, "application/json", json.dumps(st).encode())
        self.send(404, "text/plain", b"not found")

    def do_POST(self):
        p, _, q = self.path.partition("?")
        if p == "/pad":
            d = dict(kv.split("=", 1) for kv in q.split("&") if "=" in kv).get("dir", "")
            if d == "up":
                state["page"] = (state["page"] + 1) % 2
            elif d == "down":
                state["page"] = (state["page"] - 1) % 2
            elif d == "left":
                state["terrain"] = not state["terrain"]
            pad_event.set()
            return self.send(200, "application/json", b'{"ok":true}')
        self.send(404, "text/plain", b"not found")


def main():
    global ARGS
    ap = argparse.ArgumentParser()
    ap.add_argument("--stub", default="http://localhost:8080")
    ap.add_argument("--port", type=int, default=8081)
    ap.add_argument("--root", default=os.path.join(HERE, "..", "stub_games"))
    ap.add_argument("--interval", type=float, default=1.0)
    ARGS = ap.parse_args()
    ARGS.root = os.path.abspath(ARGS.root)
    os.makedirs(os.path.join(HERE, "out"), exist_ok=True)
    if not os.path.exists(POKEVIEW):
        sys.exit("eink_server: build ./pokeview first (./build.sh)")
    threading.Thread(target=poll_loop, daemon=True).start()
    srv = ThreadingHTTPServer(("0.0.0.0", ARGS.port), H)
    ip = subprocess.run(["ipconfig", "getifaddr", "en0"], capture_output=True, text=True).stdout.strip() or "?"
    print(f"companion e-ink page: http://localhost:{ARGS.port}/   (LAN: http://{ip}:{ARGS.port}/)", flush=True)
    print(f"watching {ARGS.stub}/api/wram every {ARGS.interval}s", flush=True)
    srv.serve_forever()


if __name__ == "__main__":
    main()
