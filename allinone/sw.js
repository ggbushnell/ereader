// The one-tab companion's "server", running inside the browser.
//
// The game page (www/) and the browser companion (native/web/) are served
// unchanged and still talk to the e-reader / stub server routes: /api/roms,
// /saves/..., POST /api/wram, /view.json, /assets/... This service worker
// answers all of them itself, from IndexedDB and the WebAssembly build of
// pokeview, so nothing the pages send leaves the browser. A same-origin request
// outside this site that is not handled here gets a local 404 rather than
// going to the network.
//
// Build BUILD_SHA (CI stamps it, so every deploy is a new worker).
'use strict';
importScripts('pokeview-sw.js');   // defines createPokeview

const BASE = new URL('./', self.location).pathname;   // e.g. /ereader/
const WRAM_BYTES = 0x2000;
const MAX_SAVE = 1024 * 1024;
const ROM_EXT = /\.(nes|gb|gbc)$/i;
const SAVE_EXT = ['.sav', '.state', '.auto'];
const SERVER_ID = String(Date.now());   // the companion reloads when this changes
const START = Date.now();

// The game page is shown without its Books section (the e-reader's, not
// ours), and sendBeacon is turned off so its unload save takes the fetch
// path this worker sees instead of a beacon that could bypass it.
const GAME_HEAD = `<style>#space,#bookBox,#bookList,#bookEmpty,#library .hint,#library h2:first-of-type{display:none!important}</style>
<script>try { navigator.sendBeacon = function () { return false; }; } catch (e) {}</script>`;

// ---------------------------------------------------------------- storage
// One object store; keys "roms/<name>", "saves/<name>.sav", "aux/pokered.pack",
// "aux/achievements.bin", "live/wram". Values are Uint8Arrays.

let dbp = null;
function db() {
  if (!dbp) {
    dbp = new Promise((res, rej) => {
      const r = indexedDB.open('companion', 1);
      r.onupgradeneeded = () => r.result.createObjectStore('files');
      r.onsuccess = () => res(r.result);
      r.onerror = () => rej(r.error);
    });
  }
  return dbp;
}
const req = (r) => new Promise((res, rej) => { r.onsuccess = () => res(r.result); r.onerror = () => rej(r.error); });
const store = async (mode) => (await db()).transaction('files', mode).objectStore('files');
const getFile = async (k) => req((await store('readonly')).get(k));
const putFile = async (k, v) => req((await store('readwrite')).put(v, k));
const delFile = async (k) => req((await store('readwrite')).delete(k));
const allKeys = async () => req((await store('readonly')).getAllKeys());

// ---------------------------------------------------------------- responses

const NO_STORE = { 'Cache-Control': 'no-store' };
const json = (o, status = 200) => new Response(JSON.stringify(o), { status, headers: { ...NO_STORE, 'Content-Type': 'application/json' } });
const text = (status, s) => new Response(s, { status, headers: { ...NO_STORE, 'Content-Type': 'text/plain' } });
const bin = (bytes, type = 'application/octet-stream') => new Response(bytes, { headers: { ...NO_STORE, 'Content-Type': type } });
const unescape = (s) => { try { return decodeURIComponent(s); } catch (e) { return s; } };
const nameOf = (s) => unescape(s).split(/[\\/]/).pop().replace(/[^\w.\- ()&',!+]/g, '_');

async function page(src, head) {
  const html = await (await fetch(src, { cache: 'no-cache' })).text();
  return new Response(html.replace('</head>', head + '\n</head>'), { headers: { ...NO_STORE, 'Content-Type': 'text/html; charset=utf-8' } });
}

// ---------------------------------------------------------------- pokeview

let wasmBinary = null;

// One fresh module per command, exactly like the native companion runs one
// process per command: no state carries over between runs except files.
async function pokeview(args, files) {
  if (!wasmBinary) wasmBinary = new Uint8Array(await (await fetch(BASE + 'pokeview-sw.wasm')).arrayBuffer());
  const lines = [];
  const M = await createPokeview({ wasmBinary, print: (s) => lines.push(s), printErr: (s) => console.log('pokeview:', s) });
  M.FS.mkdirTree('/g/games/aux');
  for (const [path, bytes] of Object.entries(files)) M.FS.writeFile(path, bytes);
  let rc;
  try { rc = M.callMain(args); } catch (e) { console.log('pokeview crashed:', e); rc = -1; }
  return { rc, out: lines.join('\n'), FS: M.FS };
}

async function packFiles() {
  const f = {};
  const pack = await getFile('aux/pokered.pack');
  const ach = await getFile('aux/achievements.bin');
  if (pack) f['/g/games/aux/pokered.pack'] = pack;
  if (ach) f['/g/games/aux/achievements.bin'] = ach;
  return f;
}

// The pack's graphics as PNGs (pokeview --dump-assets), once per pack.
let assetsP = null;
function assets() {
  if (!assetsP) {
    assetsP = (async () => {
      const m = new Map();
      const files = await packFiles();
      if (!files['/g/games/aux/pokered.pack']) return m;
      const r = await pokeview(['--root', '/g', '--dump-assets', '/assets'], files);
      const walk = (dir, rel) => {
        for (const n of r.FS.readdir(dir)) {
          if (n === '.' || n === '..') continue;
          const p = dir + '/' + n;
          if (r.FS.isDir(r.FS.stat(p).mode)) walk(p, rel + n + '/');
          else m.set(rel + n, r.FS.readFile(p));
        }
      };
      walk('/assets', '');
      return m;
    })();
  }
  return assetsP;
}

// ---------------------------------------------------------------- live state

const live = { bytes: null, hash: 0, changedAt: 0, game: 'gen1', renders: 0, view: null, frame: null, map: null, busy: false, again: false };
let restored = null;

// After the worker restarts, pick up the last snapshot so the companion is
// not blank while a paused game posts nothing new.
function restore() {
  if (!restored) {
    restored = getFile('live/wram').then((s) => {
      if (s && !live.bytes) {
        Object.assign(live, { bytes: s.bytes, hash: hash32(s.bytes), changedAt: s.at, game: s.game });
        return render();
      }
    }).catch((e) => console.log('restore failed:', e));
  }
  return restored;
}

function hash32(b) {
  let h = 0x811c9dc5;
  for (let i = 0; i < b.length; i++) { h ^= b[i]; h = Math.imul(h, 0x01000193) >>> 0; }
  return h;
}
const same = (a, b) => !!a && !!b && a.length === b.length && a.every((v, i) => v === b[i]);

async function render() {
  if (live.busy) { live.again = true; return; }
  live.busy = true;
  try {
    do {
      live.again = false;
      const files = await packFiles();
      files['/snap.wram'] = live.bytes;
      const common = ['--root', '/g', '--game', live.game, '--wram', '/snap.wram'];
      // The e-paper frame first: drawing judges and stamps achievements, and
      // the JSON below should see the same stamps.
      const fr = await pokeview([...common, '--page', '0', '--out', '/frame.pgm'], files);
      if (fr.rc === 0) {
        live.frame = pgmToBmp(fr.FS.readFile('/frame.pgm'));
        try {
          const ach = fr.FS.readFile('/g/games/aux/achievements.bin');
          if (!same(ach, files['/g/games/aux/achievements.bin'])) {
            files['/g/games/aux/achievements.bin'] = ach;
            await putFile('aux/achievements.bin', ach);
          }
        } catch (e) { /* no pack, no achievements */ }
      }
      const vj = await pokeview([...common, '--view-json', '--page', '0'], files);
      if (vj.rc === 0 && vj.out.trim().startsWith('{')) live.view = vj.out.trim();
      const mp = await pokeview([...common, '--map-png', '/map.png'], files);
      if (mp.rc === 0) { try { live.map = mp.FS.readFile('/map.png'); } catch (e) { /* no map here */ } }
      live.renders++;
    } while (live.again);
  } finally {
    live.busy = false;
  }
}

async function onWram(bytes) {
  const h = hash32(bytes);
  if (h === live.hash && live.bytes) return;
  Object.assign(live, { bytes, hash: h, changedAt: Date.now() });
  await putFile('live/wram', { bytes, at: live.changedAt, game: live.game });
  await render();
}

// 8-bit grayscale BMP from pokeview's P5 PGM; browsers cannot show PGM.
function pgmToBmp(pgm) {
  const head = new TextDecoder().decode(pgm.subarray(0, 32)).split(/\s+/);
  const w = +head[1], h = +head[2];
  const off = `P5\n${w} ${h}\n255\n`.length;
  return bmp(w, h, (y) => pgm.subarray(off + y * w, off + (y + 1) * w));
}
function bmp(w, h, row) {
  const stride = (w + 3) & ~3, pal = 1024, size = 54 + pal + stride * h;
  const b = new Uint8Array(size), v = new DataView(b.buffer);
  b[0] = 0x42; b[1] = 0x4d; v.setUint32(2, size, true); v.setUint32(10, 54 + pal, true);
  v.setUint32(14, 40, true); v.setInt32(18, w, true); v.setInt32(22, h, true);
  v.setUint16(26, 1, true); v.setUint16(28, 8, true); v.setUint32(34, stride * h, true); v.setUint32(46, 256, true);
  for (let i = 0; i < 256; i++) b[54 + i * 4] = b[55 + i * 4] = b[56 + i * 4] = i;
  for (let y = 0; y < h; y++) b.set(row(y), 54 + pal + (h - 1 - y) * stride);
  return b;
}
let blank = null;
const blankFrame = () => blank || (blank = bmp(680, 920, () => new Uint8Array(680).fill(255)));

// ---------------------------------------------------------------- routes

async function romList() {
  const keys = new Set(await allKeys());
  const out = [];
  for (const k of [...keys].filter((k) => k.startsWith('roms/')).sort()) {
    const name = k.slice(5);
    const rom = await getFile(k);
    out.push({ name, size: rom ? rom.length : 0, sav: keys.has(`saves/${name}.sav`), state: keys.has(`saves/${name}.state`), auto: keys.has(`saves/${name}.auto`) });
  }
  return out;
}

async function route(event, url) {
  const p = url.pathname, method = event.request.method;

  // The two pages, from their unchanged sources.
  if (p === BASE + 'game/') return page(BASE + 'www/index.html', GAME_HEAD);
  if (p === BASE + 'companion/') return page(BASE + 'web/index.html', '');

  // What the game page asks the reader for.
  if (p === '/app.js') return fetch(BASE + 'www/app.js', { cache: 'no-cache' });
  if (p === '/gb.js') return fetch(BASE + 'www/vendor/gb.js');
  if (p === '/nes.js') return fetch(BASE + 'www/vendor/nes.js');
  if (p === '/api/ping') {
    const est = navigator.storage && navigator.storage.estimate ? await navigator.storage.estimate() : {};
    return json({
      ok: true, free: Math.max(0, (est.quota || 0) - (est.usage || 0)), total: est.quota || 0,
      reserve: 0, txtFactor: 1, txtMax: 0, books: 0, uptime: Math.floor((Date.now() - START) / 1000),
      pack: !!(await getFile('aux/pokered.pack')), wram: !!live.bytes,
    });
  }
  if (p === '/api/roms') return json(await romList());
  if (p.startsWith('/roms/')) {
    const name = nameOf(p.slice(6));
    const rom = await getFile('roms/' + name);
    if (!rom) return text(404, 'not found');
    // Gold and Silver share one work-RAM layout (gen2); everything else is read as Red/Blue.
    live.game = /gold|silver/i.test(name) ? 'gen2' : 'gen1';
    return bin(rom);
  }
  if (p.startsWith('/saves/')) {
    const name = nameOf(p.slice(7));
    if (!SAVE_EXT.some((e) => name.toLowerCase().endsWith(e))) return text(400, 'bad extension');
    if (method === 'POST') {
      const bytes = new Uint8Array(await event.request.arrayBuffer());
      if (bytes.length > MAX_SAVE) return text(413, 'too large');
      await putFile('saves/' + name, bytes);
      return json({ ok: true, size: bytes.length });
    }
    const s = await getFile('saves/' + name);
    return s ? bin(s) : text(404, 'not found');
  }
  if (p === '/api/wram') {
    if (method === 'POST') {
      const bytes = new Uint8Array(await event.request.arrayBuffer());
      if (bytes.length !== WRAM_BYTES) return json({ ok: false, want: WRAM_BYTES }, 400);
      event.waitUntil(onWram(bytes).catch((e) => console.log('render failed:', e)));
      return json({ ok: true, size: bytes.length });
    }
    return live.bytes ? bin(live.bytes) : text(404, 'no snapshot');
  }
  if (p === '/api/upload' && method === 'POST') {
    const f = (await event.request.formData()).get('rom');
    if (!f || !f.name) return text(400, 'no rom field');
    const name = nameOf(f.name), bytes = new Uint8Array(await f.arrayBuffer());
    if (/\.pack$/i.test(name)) {
      await putFile('aux/' + name, bytes);
      assetsP = null;
      if (live.bytes) event.waitUntil(render());
    } else if (ROM_EXT.test(name)) {
      await putFile('roms/' + name, bytes);
    } else {
      return text(400, 'bad extension');
    }
    return text(200, 'ok');
  }
  if (p === '/api/delete' && method === 'POST') {
    const name = nameOf(new URLSearchParams(await event.request.text()).get('name') || '');
    if (name) for (const k of ['roms/' + name, ...SAVE_EXT.map((e) => `saves/${name}${e}`)]) await delFile(k);
    return text(200, 'ok');
  }
  if (p === '/api/books') return json([]);
  if (p.startsWith('/api/books/')) return json({ ok: false, error: 'books are not part of the one-tab companion' }, 400);

  // What the companion asks the e-ink server for.
  if (p.startsWith('/web/')) return fetch(BASE + 'web/' + p.slice(5), { cache: 'no-cache' });
  if (p === '/view.json') {
    await restore();
    if (!live.view) return json({ server: SERVER_ID });
    const age = live.changedAt ? Math.floor((Date.now() - live.changedAt) / 1000) : -1;
    return new Response(live.view.replace(/}\s*$/, '') +
      `,"server":"${SERVER_ID}","renders":${live.renders},"wramOk":${!!live.bytes},"snapshotAge":${age}}`,
      { headers: { ...NO_STORE, 'Content-Type': 'application/json' } });
  }
  if (p === '/frame.png') return bin(live.frame || blankFrame(), 'image/bmp');
  if (p === '/map.png') return live.map ? bin(live.map, 'image/png') : text(404, 'no map yet');
  if (p.startsWith('/assets/')) {
    const a = (await assets()).get(decodeURIComponent(p.slice(8)));
    return a ? new Response(a, { headers: { 'Content-Type': 'image/png', 'Cache-Control': 'max-age=3600' } }) : text(404, 'not found');
  }

  // This site's own files come from the network (GitHub Pages)...
  if (p.startsWith(BASE)) return null;
  // ...and nothing else on this origin is ever asked for.
  return text(404, 'not found');
}

self.addEventListener('install', () => self.skipWaiting());
self.addEventListener('activate', (e) => e.waitUntil(self.clients.claim()));
self.addEventListener('fetch', (e) => {
  const url = new URL(e.request.url);
  if (url.origin !== self.location.origin) return;
  const p = url.pathname;
  // Static files of this site go straight to the network, untouched.
  if (p.startsWith(BASE) && p !== BASE + 'game/' && p !== BASE + 'companion/') return;
  e.respondWith(route(e, url).then((r) => r || fetch(e.request))
    .catch((err) => text(500, String((err && err.message) || err))));
});
