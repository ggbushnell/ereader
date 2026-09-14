/* Reader Games: browser side of the e-reader game server.
 *
 * The device only serves files and stores bytes. Everything here (emulation,
 * save container format, scheduling of uploads) is the page's business.
 * Plain ES2018, no modules, no build step.
 */
(function () {
  'use strict';

  // ---------------------------------------------------------------- constants

  var NES_W = 256, NES_H = 240;
  var GB_W = 160, GB_H = 144;
  var NES_FRAME_MS = 1000 / 60.098;   // real NTSC NES rate, not 60
  var coarsePointer = !!(window.matchMedia &&
                         window.matchMedia('(pointer: coarse)').matches);
  var PING_MS = 60000;                // device drops WiFi after 15 min idle
  var AUTO_MS = 60000;
  var SAV_POLL_MS = 5000;             // battery RAM change check, uploads only on change
  var WRAM_POLL_MS = 1000;            // work RAM mirror for the reader's companion screen
  var WRAM_BYTES = 0x2000;            // GB 0xC000..0xDFFF, all a DMG game uses
  var MAX_UPLOAD = 1024 * 1024;

  var MAGIC = 0x45525356;             // "ERSV"
  var VER = 1;
  var CON_NES = 1, CON_GB = 2;
  var HDR = 32;

  // Keyboard map. Arrows and WASD both drive the d-pad.
  var KEYMAP = {
    ArrowUp: 'UP', ArrowDown: 'DOWN', ArrowLeft: 'LEFT', ArrowRight: 'RIGHT',
    KeyW: 'UP', KeyS: 'DOWN', KeyA: 'LEFT', KeyD: 'RIGHT',
    KeyZ: 'B', KeyX: 'A', KeyE: 'A', KeyR: 'B',
    Enter: 'START',
    ShiftLeft: 'SELECT', ShiftRight: 'SELECT', Space: 'SELECT'
  };

  // ---------------------------------------------------------------- dom + util

  function $(id) { return document.getElementById(id); }
  var dom = {};
  ['library', 'status', 'romFile', 'uploadBtn', 'romList', 'empty', 'player', 'bar',
   'backBtn', 'romName', 'saveBtn', 'loadBtn', 'resetBtn', 'muteBtn', 'speedBtn', 'padBtn',
   'sync', 'stage', 'screen', 'pad'].forEach(function (k) { dom[k] = $(k); });

  function fmtBytes(n) {
    if (n >= 1073741824) return (n / 1073741824).toFixed(1) + ' GB';
    if (n >= 1048576) return (n / 1048576).toFixed(1) + ' MB';
    if (n >= 1024) return Math.round(n / 1024) + ' KB';
    return n + ' B';
  }
  function hhmm(ms) {
    var d = new Date(ms);
    return ('0' + d.getHours()).slice(-2) + ':' + ('0' + d.getMinutes()).slice(-2);
  }
  function consoleOf(name) {
    return /\.nes$/i.test(name) ? 'NES' : 'GB';
  }
  function hash32(bytes) {
    // FNV-1a, only used to notice that battery RAM changed.
    var h = 0x811c9dc5;
    for (var i = 0; i < bytes.length; i++) {
      h ^= bytes[i];
      h = (h + ((h << 1) + (h << 4) + (h << 7) + (h << 8) + (h << 24))) >>> 0;
    }
    return h;
  }

  // WasmBoy rejects with plain objects rather than Errors, so pull out
  // whatever text there is instead of logging undefined.
  function errText(e) {
    if (!e) return 'unknown';
    return e.message || e.reason || (typeof e === 'string' ? e : JSON.stringify(e));
  }

  // Anything that can silently never settle gets a deadline, so the sync
  // indicator recovers instead of sitting on "saving" forever. The original
  // promise keeps its own catch: once the race has been decided, a late
  // rejection would otherwise surface as an unhandled rejection.
  function withTimeout(promise, ms, what) {
    var settled = false;
    promise.catch(function (e) {
      if (settled) console.log(what + ' rejected after the deadline:', errText(e));
    });
    return Promise.race([
      promise,
      new Promise(function (_, rej) {
        setTimeout(function () {
          settled = true;
          rej(new Error(what + ' timed out'));
        }, ms);
      })
    ]);
  }

  var scripts = {};
  function loadScript(src) {
    // Injected on demand so a Game Boy session never pulls the NES core.
    if (scripts[src]) return scripts[src];
    scripts[src] = new Promise(function (res, rej) {
      var s = document.createElement('script');
      s.src = src;
      s.onload = function () { res(); };
      s.onerror = function () { rej(new Error('failed to load ' + src)); };
      document.head.appendChild(s);
    });
    return scripts[src];
  }

  // ---------------------------------------------------------------- transport

  var api = {
    roms: function () {
      return fetch('/api/roms', { cache: 'no-store' }).then(function (r) { return r.json(); });
    },
    ping: function () {
      return fetch('/api/ping', { cache: 'no-store' }).then(function (r) { return r.json(); });
    },
    rom: function (name) {
      return fetch('/roms/' + encodeURIComponent(name), { cache: 'no-store' })
        .then(function (r) {
          if (!r.ok) throw new Error('rom ' + r.status);
          return r.arrayBuffer();
        }).then(function (b) { return new Uint8Array(b); });
    },
    getSave: function (name, ext) {
      return fetch('/saves/' + encodeURIComponent(name + ext), { cache: 'no-store' })
        .then(function (r) {
          if (r.status === 404) return null;
          if (!r.ok) throw new Error('save ' + r.status);
          return r.arrayBuffer();
        }).then(function (b) { return b ? new Uint8Array(b) : null; });
    },
    putSave: function (name, ext, bytes) {
      if (bytes.length > MAX_UPLOAD) {
        return Promise.reject(new Error('save too big: ' + bytes.length));
      }
      return fetch('/saves/' + encodeURIComponent(name + ext), {
        method: 'POST',
        headers: { 'Content-Type': 'application/octet-stream' },
        body: bytes,
        keepalive: bytes.length < 60000   // keepalive bodies are capped by the browser
      }).then(function (r) {
        if (!r.ok) throw new Error('put ' + r.status);
        return r.json();
      });
    },
    // Work RAM mirror. The reader decodes these bytes itself (it knows about
    // Pokemon, the page does not) and draws a companion screen from them.
    putWram: function (bytes) {
      return fetch('/api/wram', {
        method: 'POST',
        headers: { 'Content-Type': 'application/octet-stream' },
        body: bytes
      }).then(function (r) {
        if (!r.ok) throw new Error('wram ' + r.status);
      });
    },
    beaconSave: function (name, ext, bytes) {
      var url = '/saves/' + encodeURIComponent(name + ext);
      var blob = new Blob([bytes], { type: 'application/octet-stream' });
      if (navigator.sendBeacon && navigator.sendBeacon(url, blob)) return true;
      try {
        fetch(url, { method: 'POST', body: blob, keepalive: true });
        return true;
      } catch (e) { return false; }
    },
    del: function (name) {
      return fetch('/api/delete', {
        method: 'POST',
        headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
        body: 'name=' + encodeURIComponent(name)
      });
    }
  };

  // ------------------------------------------------------------ save container
  //
  // Save states get a 32 byte header so "Continue" can compare .auto and .state
  // without understanding the payload.
  //
  //   0   4  magic "ERSV"
  //   4   1  format version (1)
  //   5   1  console (1 = NES, 2 = GB/GBC)
  //   6   1  flags, bit 0 = payload is gzipped
  //   7   1  reserved
  //   8   8  unix milliseconds, float64 LE
  //  16   4  payload byte length as stored, uint32 LE
  //  20  12  reserved zeros
  //
  // NES payload  = UTF-8 JSON of nes.toJSON().
  // GB payload   = "WBS1" then four (uint32 LE length + bytes) sections in the
  //                order internalState, paletteMemory, gameBoyMemory, cartridgeRam.
  //
  // The .sav file is deliberately NOT wrapped: it is raw cartridge battery RAM,
  // so it stays interchangeable with an ordinary .sav from any other emulator.

  function gzipSupported() { return typeof CompressionStream === 'function'; }

  function gzip(bytes) {
    if (!gzipSupported()) return Promise.resolve(null);
    var cs = new CompressionStream('gzip');
    var w = cs.writable.getWriter();
    w.write(bytes); w.close();
    return new Response(cs.readable).arrayBuffer()
      .then(function (b) { return new Uint8Array(b); })
      .catch(function () { return null; });
  }

  function gunzip(bytes) {
    var ds = new DecompressionStream('gzip');
    var w = ds.writable.getWriter();
    w.write(bytes); w.close();
    return new Response(ds.readable).arrayBuffer()
      .then(function (b) { return new Uint8Array(b); });
  }

  function packContainer(payload, con, gzipped) {
    var out = new Uint8Array(HDR + payload.length);
    var dv = new DataView(out.buffer);
    dv.setUint32(0, MAGIC, false);
    out[4] = VER;
    out[5] = con;
    out[6] = gzipped ? 1 : 0;
    dv.setFloat64(8, Date.now(), true);
    dv.setUint32(16, payload.length, true);
    out.set(payload, HDR);
    return out;
  }

  // Wrap a payload, gzipping when the browser can. sync:true skips gzip so the
  // page-hide path never has to await anything.
  function wrap(payload, con, sync) {
    if (sync || !gzipSupported()) return Promise.resolve(packContainer(payload, con, false));
    return gzip(payload).then(function (z) {
      return z && z.length < payload.length
        ? packContainer(z, con, true)
        : packContainer(payload, con, false);
    });
  }

  function readHeader(bytes) {
    if (!bytes || bytes.length < HDR) return null;
    var dv = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
    if (dv.getUint32(0, false) !== MAGIC) return null;
    if (bytes[4] !== VER) return null;
    return { con: bytes[5], gzipped: (bytes[6] & 1) === 1, time: dv.getFloat64(8, true) };
  }

  function unwrap(bytes) {
    var h = readHeader(bytes);
    if (!h) return Promise.reject(new Error('not a save container'));
    var payload = bytes.subarray(HDR);
    if (!h.gzipped) return Promise.resolve(payload);
    return gunzip(payload);
  }

  var TD = new TextDecoder();
  var TE = new TextEncoder();

  function packSections(arrays) {
    var total = 4, i;
    for (i = 0; i < arrays.length; i++) total += 4 + arrays[i].length;
    var out = new Uint8Array(total);
    out.set(TE.encode('WBS1'), 0);
    var dv = new DataView(out.buffer);
    var off = 4;
    for (i = 0; i < arrays.length; i++) {
      dv.setUint32(off, arrays[i].length, true); off += 4;
      out.set(arrays[i], off); off += arrays[i].length;
    }
    return out;
  }

  function unpackSections(bytes) {
    if (TD.decode(bytes.subarray(0, 4)) !== 'WBS1') throw new Error('bad GB payload');
    var dv = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
    var off = 4, out = [];
    while (off + 4 <= bytes.length) {
      var len = dv.getUint32(off, true); off += 4;
      // Copy, because WasmBoy transfers (and so detaches) the buffers we hand it.
      out.push(new Uint8Array(bytes.subarray(off, off + len)));
      off += len;
    }
    return out;
  }

  // ---------------------------------------------------------------- NES core

  function NesCore() {
    this.kind = 'NES';
    // toJSON() is pure JS, so a hidden page can still produce a fresh state.
    this.canSnapshotWhileHidden = true;
    this.width = NES_W;
    this.height = NES_H;
    this.nes = null;
    this.raf = 0;
    this.ctx = null;
    this.image = null;
    this.pixels = null;
    this.audio = null;
  }

  NesCore.prototype.start = function (rom, canvas) {
    var self = this;
    return loadScript('/nes.js').then(function () {
      self.ctx = canvas.getContext('2d', { alpha: false });
      self.image = self.ctx.createImageData(NES_W, NES_H);
      self.pixels = new Uint32Array(self.image.data.buffer);

      self.audio = new NesAudio();
      self.nes = new jsnes.NES({
        onFrame: function (buf) {
          // jsnes hands back 24 bit 0x00BBGGRR words with no alpha, so the
          // opaque byte has to go in for a little-endian Uint32 ImageData view.
          var px = self.pixels;
          for (var i = 0; i < px.length; i++) px[i] = 0xFF000000 | buf[i];
        },
        onAudioSample: function (l, r) { self.audio.push(l, r); },
        sampleRate: self.audio.rate,
        emulateSound: true
      });

      var s = '';
      for (var i = 0; i < rom.length; i += 4096) {
        s += String.fromCharCode.apply(null, rom.subarray(i, i + 4096));
      }
      self.nes.loadROM(s);
      self.run();
    });
  };

  NesCore.prototype.run = function () {
    var self = this, last = 0, acc = 0;
    function loop(t) {
      self.raf = requestAnimationFrame(loop);
      if (!last) { last = t; return; }
      var dt = t - last;
      last = t;
      // A backgrounded tab must not try to catch up on thousands of frames.
      if (dt > 250) dt = NES_FRAME_MS;
      // Fast forward simply runs more emulated frames per real frame.
      var speed = self.speed || 1;
      acc += dt * speed;
      var guard = 0, cap = 4 * speed;
      while (acc >= NES_FRAME_MS && guard < cap) {
        self.nes.frame();
        acc -= NES_FRAME_MS;
        guard++;
      }
      if (acc > NES_FRAME_MS * cap) acc = 0;
      self.ctx.putImageData(self.image, 0, 0);
    }
    this.raf = requestAnimationFrame(loop);
  };

  NesCore.prototype.setSpeed = function (n) { this.speed = n; };

  NesCore.prototype.stop = function () {
    if (this.raf) cancelAnimationFrame(this.raf);
    this.raf = 0;
    if (this.audio) this.audio.close();
    this.audio = null;
    this.nes = null;
    return Promise.resolve();
  };

  NesCore.prototype.reset = function () { this.nes.reset(); return Promise.resolve(); };

  NesCore.prototype.setMuted = function (m) { if (this.audio) this.audio.setMuted(m); };

  NesCore.prototype.setButton = function (btn, down) {
    var c = jsnes.Controller;
    var map = {
      UP: c.BUTTON_UP, DOWN: c.BUTTON_DOWN, LEFT: c.BUTTON_LEFT, RIGHT: c.BUTTON_RIGHT,
      A: c.BUTTON_A, B: c.BUTTON_B, START: c.BUTTON_START, SELECT: c.BUTTON_SELECT
    };
    if (!(btn in map)) return;
    if (down) this.nes.buttonDown(1, map[btn]);
    else this.nes.buttonUp(1, map[btn]);
  };

  // NES battery RAM already lives inside toJSON(), so there is no separate .sav.
  NesCore.prototype.battery = function () { return Promise.resolve(null); };

  NesCore.prototype.snapshot = function () {
    return Promise.resolve(TE.encode(JSON.stringify(this.nes.toJSON())));
  };

  NesCore.prototype.restore = function (payload) {
    this.nes.fromJSON(JSON.parse(TD.decode(payload)));
    return Promise.resolve();
  };

  // NES audio: jsnes emits samples from inside frame(), so a ring buffer feeds
  // a ScriptProcessor. AudioWorklet would need a second file, and the device
  // serves exactly four.
  function NesAudio() {
    var Ctx = window.AudioContext || window.webkitAudioContext;
    this.ctx = new Ctx();
    this.rate = this.ctx.sampleRate;
    this.size = 16384;
    this.mask = this.size - 1;
    this.l = new Float32Array(this.size);
    this.r = new Float32Array(this.size);
    this.w = 0;
    this.rd = 0;
    this.muted = false;

    var self = this;
    this.node = this.ctx.createScriptProcessor(2048, 0, 2);
    this.node.onaudioprocess = function (e) {
      var ol = e.outputBuffer.getChannelData(0);
      var or = e.outputBuffer.getChannelData(1);
      var avail = (self.w - self.rd) & self.mask;
      for (var i = 0; i < ol.length; i++) {
        if (i < avail) {
          ol[i] = self.l[self.rd]; or[i] = self.r[self.rd];
          self.rd = (self.rd + 1) & self.mask;
        } else {
          ol[i] = 0; or[i] = 0;   // underrun, stay silent rather than buzz
        }
      }
    };
    this.gain = this.ctx.createGain();
    this.node.connect(this.gain);
    this.gain.connect(this.ctx.destination);
    if (this.ctx.state === 'suspended') this.ctx.resume();
  }
  NesAudio.prototype.push = function (l, r) {
    // Drop samples rather than let latency grow. Capping the queue at ~4096
    // samples keeps it near 90 ms at 44.1 kHz, just above the 2048 sample
    // ScriptProcessor block, so there is slack without audible lag.
    if (((this.w - this.rd) & this.mask) > 4096) return;
    this.l[this.w] = l; this.r[this.w] = r;
    this.w = (this.w + 1) & this.mask;
  };
  NesAudio.prototype.setMuted = function (m) {
    this.muted = m;
    this.gain.gain.value = m ? 0 : 1;
    if (!m && this.ctx.state === 'suspended') this.ctx.resume();
  };
  NesAudio.prototype.close = function () {
    try { this.node.disconnect(); this.gain.disconnect(); this.ctx.close(); } catch (e) {}
  };

  // ---------------------------------------------------------------- GB core

  // Cartridge RAM size from the GB header byte at 0x0149.
  var GB_RAM_SIZES = [0, 2048, 8192, 32768, 131072, 65536];

  // The UMD build assigns its exports object to window.WasmBoy and then puts
  // the real API on a WasmBoy property inside it, so the global can be nested.
  var WB = null;
  function resolveWasmBoy() {
    var g = window.WasmBoy;
    if (g && typeof g.config === 'function') return g;
    if (g && g.WasmBoy && typeof g.WasmBoy.config === 'function') return g.WasmBoy;
    throw new Error('WasmBoy API not found on window');
  }

  function GbCore() {
    this.kind = 'GB';
    // WasmBoy.saveState() calls pause(), which awaits a requestAnimationFrame.
    // A hidden page never gets one, so a fresh state is impossible there and
    // the page-hide path has to fall back to the last cached autosave.
    this.canSnapshotWhileHidden = false;
    this.width = GB_W;
    this.height = GB_H;
    this.ramSize = 0;
    this.ramStart = -1;
    this.wramStart = -1;
    this.muted = false;
  }

  GbCore.prototype.start = function (rom, canvas) {
    var self = this;
    this.ramSize = GB_RAM_SIZES[rom[0x0149]] || 0;
    return loadScript('/gb.js').then(function () {
      WB = resolveWasmBoy();
      return WB.config({
        headless: false,
        useGbcWhenOptional: true,
        isAudioEnabled: true,
        frameSkip: 0,
        audioBatchProcessing: true,
        timersBatchProcessing: false,
        audioAccumulateSamples: true,
        graphicsBatchProcessing: false,
        graphicsDisableScanlineRendering: false,
        tileRendering: true,
        tileCaching: true,
        gameboyFrameRate: 60,
        // We own persistence, so WasmBoy's own auto-state rotation stays off.
        maxNumberOfAutoSaveStates: 0
      }, canvas);
    }).then(function () {
      // We drive input ourselves, so its global key handler must not fight us.
      try { WB.disableDefaultJoypad(); } catch (e) {}
      return WB.loadROM(rom);
    }).then(function () {
      // loadROM initialises WasmBoy's controller, which turns the default
      // joypad back on; left enabled it overwrites our state every frame
      // with "nothing held", so a touch press lasts a frame or two.
      try { WB.disableDefaultJoypad(); } catch (e) {}
      return self._findRamStart();
    });
  };

  GbCore.prototype._findRamStart = function () {
    var self = this;
    if (typeof WB._getWasmConstant !== 'function') return Promise.resolve();
    // Reading cartridge RAM straight out of wasm memory lets the battery poll
    // run without pausing the core (saveState() pauses, which hitches audio).
    // Work RAM comes out of the same window for the companion screen mirror.
    var jobs = [
      Promise.resolve(WB._getWasmConstant('WORK_RAM_LOCATION'))
        .then(function (v) { if (typeof v === 'number') self.wramStart = v; })
        .catch(function () {})
    ];
    if (self.ramSize) {
      jobs.push(Promise.resolve(WB._getWasmConstant('CARTRIDGE_RAM_LOCATION'))
        .then(function (v) { if (typeof v === 'number') self.ramStart = v; })
        .catch(function () {}));
    }
    return Promise.all(jobs).then(function () {});
  };

  // The first 8 KB of work RAM, which is GB 0xC000..0xDFFF. The core exports
  // WORK_RAM_SIZE as 0x8000 (the GBC's eight switchable banks); a DMG game
  // only ever sees the first two, so that is all that is worth sending.
  GbCore.prototype.workRam = function () {
    if (this.wramStart < 0) return Promise.resolve(null);
    var start = this.wramStart;
    return Promise.resolve(WB._getWasmMemorySection(start, start + WRAM_BYTES))
      .then(function (b) {
        return b && b.length === WRAM_BYTES ? new Uint8Array(b) : null;
      }).catch(function () { return null; });
  };

  GbCore.prototype.play = function () {
    // Audio only unlocks off a user gesture, and Play/Continue is one.
    // Both of these return promises that can reject on a slow worker.
    try {
      Promise.resolve(WB.resumeAudioContext()).catch(function () {});
    } catch (e) {}
    if (!this.holdTimer) this.startHoldKeeper();
    return Promise.resolve(WB.play()).then(function (r) {
      try { WB.disableDefaultJoypad(); } catch (e) {}
      return r;
    });
  };

  GbCore.prototype.stop = function () {
    if (this.holdTimer) { clearInterval(this.holdTimer); this.holdTimer = 0; }
    return Promise.resolve(WB.pause()).catch(function () {});
  };

  GbCore.prototype.reset = function () { return Promise.resolve(WB.reset()); };

  GbCore.prototype.setMuted = function (m) {
    this.muted = m;
    // WasmBoy owns its audio graph. _getAudioChannels() hands back an object
    // keyed by channel name (master, channel1..4), not an array, and each one
    // carries its own gain node.
    try {
      var chans = WB._getAudioChannels() || {};
      Object.keys(chans).forEach(function (k) {
        var c = chans[k];
        if (c && c.mute) { if (m) c.mute(); else c.unmute(); }
      });
    } catch (e) { console.log('gb mute failed:', errText(e)); }
  };

  // WasmBoy runs the core at a multiple of real time and stretches its own
  // audio to match, so a plain setSpeed is all it needs.
  GbCore.prototype.setSpeed = function (n) {
    try { WB.setSpeed(n); } catch (e) { console.log('speed failed:', errText(e)); }
  };

  GbCore.prototype.setButton = function (btn, down) {
    gbState[btn] = down;
    WB.setJoypadState(gbState);
  };

  // A held button is re-sent every 100 ms: if anything inside WasmBoy clears
  // the joypad (its own controller poll, a state load), the hold survives.
  GbCore.prototype.startHoldKeeper = function () {
    var self = this;
    this.holdTimer = setInterval(function () {
      var any = false;
      for (var k in gbState) if (gbState[k]) { any = true; break; }
      if (any) { try { WB.setJoypadState(gbState); } catch (e) {} }
    }, 100);
  };

  var gbState = { UP: false, DOWN: false, LEFT: false, RIGHT: false,
                  A: false, B: false, START: false, SELECT: false };

  GbCore.prototype.battery = function () {
    var self = this;
    if (!this.ramSize) return Promise.resolve(null);
    if (this.ramStart >= 0) {
      return Promise.resolve(
        WB._getWasmMemorySection(this.ramStart, this.ramStart + this.ramSize)
      ).then(function (b) {
        return b && b.length ? new Uint8Array(b) : null;
      }).catch(function () { return null; });
    }
    // Fallback: pull it out of a full snapshot (this one does pause the core).
    return this.snapshotRaw().then(function (st) {
      self.resumeSoon();
      return st ? new Uint8Array(st.wasmboyMemory.cartridgeRam) : null;
    });
  };

  GbCore.prototype.snapshotRaw = function () {
    // WasmBoy.saveState() pauses the core and does not resume it.
    return withTimeout(Promise.resolve(WB.saveState()), 5000, 'GB saveState')
      .catch(function (e) { console.log('GB snapshot:', errText(e)); return null; });
  };

  GbCore.prototype.resumeSoon = function () {
    // play() is a worker round trip that can reject; never leave it dangling.
    try {
      Promise.resolve(WB.play())
        .catch(function (e) { console.log('GB resume:', errText(e)); });
    } catch (e) { console.log('GB resume:', errText(e)); }
  };

  GbCore.prototype.snapshot = function () {
    var self = this;
    return this.snapshotRaw().then(function (st) {
      self.resumeSoon();
      if (!st || !st.wasmboyMemory) throw new Error('no GB state');
      var m = st.wasmboyMemory;
      // Copy now: loadState() later transfers these buffers away.
      return packSections([
        new Uint8Array(m.wasmBoyInternalState),
        new Uint8Array(m.wasmBoyPaletteMemory),
        new Uint8Array(m.gameBoyMemory),
        new Uint8Array(m.cartridgeRam)
      ]);
    });
  };

  GbCore.prototype.restore = function (payload) {
    var self = this;
    var s = unpackSections(payload);
    return Promise.resolve(WB.loadState({
      wasmboyMemory: {
        wasmBoyInternalState: s[0],
        wasmBoyPaletteMemory: s[1],
        gameBoyMemory: s[2],
        cartridgeRam: s[3]
      },
      date: Date.now(),
      isAuto: false
    })).then(function () { self.resumeSoon(); });
  };

  // Injecting battery RAM into a fresh boot: take the boot-time state, swap in
  // the .sav bytes, load it back. That keeps us off WasmBoy's IndexedDB.
  GbCore.prototype.restoreBattery = function (sav) {
    var self = this;
    if (!sav || !sav.length) return Promise.resolve();
    return this.snapshotRaw().then(function (st) {
      if (!st || !st.wasmboyMemory || !st.wasmboyMemory.cartridgeRam) return;
      var ram = new Uint8Array(st.wasmboyMemory.cartridgeRam.length);
      ram.set(sav.subarray(0, Math.min(sav.length, ram.length)));
      return WB.loadState({
        wasmboyMemory: {
          wasmBoyInternalState: new Uint8Array(st.wasmboyMemory.wasmBoyInternalState),
          wasmBoyPaletteMemory: new Uint8Array(st.wasmboyMemory.wasmBoyPaletteMemory),
          gameBoyMemory: new Uint8Array(st.wasmboyMemory.gameBoyMemory),
          cartridgeRam: ram
        },
        date: Date.now(),
        isAuto: false
      });
    }).catch(function (e) { console.log('battery restore skipped:', errText(e)); });
  };

  // ---------------------------------------------------------------- session

  var session = null;   // { name, core, muted, lastAuto, savHash, timers... }

  function setSync(text, cls) {
    dom.sync.textContent = text;
    dom.sync.className = cls || '';
  }

  function fitCanvas() {
    if (!session) return;
    var w = session.core.width, h = session.core.height;
    var r = dom.stage.getBoundingClientRect();
    if (r.width < 4 || r.height < 4) return;
    var s = Math.min(r.width / w, r.height / h);
    // Integer scaling keeps pixels square on a desktop monitor. On a phone the
    // floor throws away most of the screen (a 2.4x fit became 1x in
    // landscape), so let the picture fill the stage there.
    if (s >= 1 && !coarsePointer) s = Math.floor(s);
    dom.screen.style.width = Math.round(w * s) + 'px';
    dom.screen.style.height = Math.round(h * s) + 'px';
  }

  function uploadAuto(sync) {
    if (!session) return Promise.resolve();
    var s = session;
    return s.core.snapshot().then(function (payload) {
      return wrap(payload, s.core.kind === 'NES' ? CON_NES : CON_GB, sync);
    }).then(function (blob) {
      s.lastAuto = blob;
      if (blob.length > MAX_UPLOAD) {
        setSync('state too big', 'bad');
        console.log('auto state ' + blob.length + ' bytes, over the 1 MB cap');
        return;
      }
      setSync('saving', 'busy');
      return api.putSave(s.name, '.auto', blob).then(function () {
        setSync('saved to reader ' + hhmm(Date.now()));
      });
    }).catch(function (e) {
      console.log('autosave failed:', errText(e));
      setSync('not saved', 'bad');
    });
  }

  function uploadBattery() {
    if (!session) return Promise.resolve();
    var s = session;
    return s.core.battery().then(function (ram) {
      if (!ram || !ram.length) return;
      var h = hash32(ram);
      if (h === s.savHash) return;          // nothing changed since last upload
      s.savHash = h;
      return api.putSave(s.name, '.sav', ram);
    }).catch(function (e) { console.log('battery upload failed:', errText(e)); });
  }

  // Work RAM mirror, Game Boy only. Same debounce as the battery poll: hash
  // first, POST only when the bytes actually changed. Any failure is logged
  // and retried on the next tick; nothing here may disturb the session.
  function uploadWram() {
    if (!session) return Promise.resolve();
    var s = session;
    if (s.core.kind !== 'GB' || !s.core.workRam) return Promise.resolve();
    // Never stack uploads: a slow reader would otherwise queue stale snapshots.
    if (s.wramBusy) return Promise.resolve();
    s.wramBusy = true;
    var t0 = Date.now();
    return s.core.workRam().then(function (ram) {
      if (!ram || !ram.length) return;
      var t1 = Date.now();
      var h = hash32(ram);
      if (h === s.wramHash) return;
      s.wramHash = h;
      return api.putWram(ram).then(function () {
        var t2 = Date.now();
        if (t2 - t0 > 1500) {
          console.log('wram slow: read ' + (t1 - t0) + ' ms, post ' + (t2 - t1) + ' ms');
        }
      }).catch(function (e) {
        s.wramHash = 0;     // let the next tick try the same bytes again
        throw e;
      });
    }).catch(function (e) { console.log('wram upload failed:', errText(e)); })
      .then(function () { s.wramBusy = false; });
  }

  function manualSave() {
    if (!session) return;
    var s = session;
    setSync('saving', 'busy');
    s.core.snapshot().then(function (payload) {
      return wrap(payload, s.core.kind === 'NES' ? CON_NES : CON_GB, false);
    }).then(function (blob) {
      return api.putSave(s.name, '.state', blob);
    }).then(function () {
      setSync('saved to reader ' + hhmm(Date.now()));
      return uploadBattery();
    }).catch(function (e) {
      console.log('save failed:', errText(e));
      setSync('not saved', 'bad');
    });
  }

  function manualLoad() {
    if (!session) return;
    var s = session;
    setSync('loading', 'busy');
    api.getSave(s.name, '.state').then(function (bytes) {
      if (!bytes) { setSync('no save state', 'bad'); return; }
      return unwrap(bytes).then(function (p) { return s.core.restore(p); })
        .then(function () { setSync('loaded ' + hhmm(Date.now())); });
    }).catch(function (e) {
      console.log('load failed:', errText(e));
      setSync('load failed', 'bad');
    });
  }

  // Continue picks whichever of .auto and .state carries the newer timestamp.
  function newestSave(name) {
    return Promise.all([
      api.getSave(name, '.auto').catch(function () { return null; }),
      api.getSave(name, '.state').catch(function () { return null; })
    ]).then(function (both) {
      var best = null;
      both.forEach(function (b) {
        var h = readHeader(b);
        if (h && (!best || h.time > best.h.time)) best = { h: h, bytes: b };
      });
      return best;
    });
  }

  function startGame(name, wantContinue) {
    var isNes = consoleOf(name) === 'NES';
    var core = isNes ? new NesCore() : new GbCore();

    dom.library.classList.add('hidden');
    dom.player.classList.remove('hidden');
    document.body.classList.add('playing');
    dom.romName.textContent = name;
    setSync('loading', 'busy');

    dom.screen.width = core.width;
    dom.screen.height = core.height;

    session = { name: name, core: core, muted: false, lastAuto: null,
                savHash: 0, wramHash: 0, timers: [], savTimer: 0, speed: 1 };
    dom.speedBtn.textContent = '1x';

    api.rom(name).then(function (rom) {
      return core.start(rom, dom.screen);
    }).then(function () {
      // Battery RAM goes in before the first frame runs, every time.
      if (!isNes) {
        return api.getSave(name, '.sav').catch(function () { return null; })
          .then(function (sav) {
            if (sav) session.savHash = hash32(sav);
            return core.restoreBattery(sav);
          });
      }
    }).then(function () {
      if (!isNes) return core.play();
    }).then(function () {
      if (!wantContinue) return;
      return newestSave(name).then(function (best) {
        if (!best) return;
        return unwrap(best.bytes).then(function (p) { return core.restore(p); })
          .then(function () { console.log('continued from ' + new Date(best.h.time)); });
      }).catch(function (e) { console.log('continue failed:', errText(e)); });
    }).then(function () {
      fitCanvas();
      setSync('not saved');
      session.timers.push(setInterval(function () { uploadAuto(false); }, AUTO_MS));
      // Keep the device's WiFi awake: it sleeps after 15 minutes with no request.
      session.timers.push(setInterval(function () {
        api.ping().catch(function () {});
      }, PING_MS));
      // Battery RAM is checked every 5 s and uploaded only when it changed,
      // which is the debounce: a game that is not writing its save costs one
      // cheap wasm memory read and no request at all.
      session.timers.push(setInterval(uploadBattery, SAV_POLL_MS));
      // Work RAM mirror for the reader's companion screen, Game Boy only.
      if (!isNes) {
        session.timers.push(setInterval(uploadWram, WRAM_POLL_MS));
        uploadWram();
      }
    }).catch(function (e) {
      console.log('start failed:', errText(e), e);
      setSync('failed to start', 'bad');
    });
  }

  function leaveGame() {
    if (!session) return Promise.resolve();
    var s = session;
    s.timers.forEach(clearInterval);
    return uploadBattery()
      .then(function () { return uploadAuto(false); })
      .then(function () { return s.core.stop(); })
      .catch(function () {})
      .then(function () {
        session = null;
        document.body.classList.remove('playing');
        dom.player.classList.add('hidden');
        dom.library.classList.remove('hidden');
        refresh();
      });
  }

  // Page hide has no time for a round trip, so beacon the last wrapped state
  // first, then try for a fresh one only where that can actually work.
  function flushOnHide() {
    if (!session) return;
    var s = session;
    if (s.lastAuto) api.beaconSave(s.name, '.auto', s.lastAuto);
    // Battery RAM is read straight out of wasm memory, no pause involved, so
    // this one is safe on both cores even with the page already hidden.
    uploadBattery();
    if (s.core.canSnapshotWhileHidden || document.visibilityState !== 'hidden') {
      uploadAuto(false);
    }
  }

  window.addEventListener('pagehide', flushOnHide);
  document.addEventListener('visibilitychange', function () {
    if (document.visibilityState === 'hidden') flushOnHide();
  });

  // ---------------------------------------------------------------- input

  function press(btn, down) {
    if (session && session.core) session.core.setButton(btn, down);
    var k = dom.pad.querySelector('[data-btn="' + btn + '"]');
    if (k) k.classList.toggle('on', down);
  }

  document.addEventListener('keydown', function (e) {
    if (!session || e.repeat) return;
    var b = KEYMAP[e.code];
    if (!b) return;
    e.preventDefault();
    press(b, true);
  });
  document.addEventListener('keyup', function (e) {
    if (!session) return;
    var b = KEYMAP[e.code];
    if (!b) return;
    e.preventDefault();
    press(b, false);
  });

  // Touch pad: pointer capture on the container so a finger can slide between
  // the d-pad keys without losing the press. Touch events are swallowed at
  // the source too: on iOS a held finger otherwise starts the long press
  // magnifier or a double tap zoom, and the browser cancels the pointer.
  var held = {};   // pointerId -> button name
  ['touchstart', 'touchmove', 'touchend', 'touchcancel'].forEach(function (t) {
    dom.pad.addEventListener(t, function (e) { e.preventDefault(); }, { passive: false });
  });
  function btnAt(x, y) {
    var el = document.elementFromPoint(x, y);
    el = el && el.closest ? el.closest('[data-btn]') : null;
    return el ? el.getAttribute('data-btn') : null;
  }
  function setHeld(id, btn) {
    if (held[id] === btn) return;
    if (held[id]) press(held[id], false);
    if (btn) { held[id] = btn; press(btn, true); }
    else delete held[id];
  }
  var hasTouch = ('ontouchstart' in window) || (navigator.maxTouchPoints > 0);
  if (hasTouch) {
    // Touch devices drive the pad from touch events directly. Pointer events
    // on iOS drop a held finger after a moment (pointercancel or a spurious
    // lostpointercapture), which turned a hold into single steps.
    function touchEach(e, fn) {
      for (var i = 0; i < e.changedTouches.length; i++) fn(e.changedTouches[i]);
    }
    dom.pad.addEventListener('touchstart', function (e) {
      touchEach(e, function (t) {
        setHeld('t' + t.identifier, btnAt(t.clientX, t.clientY));
      });
    }, { passive: false });
    dom.pad.addEventListener('touchmove', function (e) {
      touchEach(e, function (t) {
        var id = 't' + t.identifier;
        if (id in held) setHeld(id, btnAt(t.clientX, t.clientY));
      });
    }, { passive: false });
    function touchRelease(e) {
      touchEach(e, function (t) {
        var id = 't' + t.identifier;
        if (!(id in held)) return;
        press(held[id], false);
        delete held[id];
      });
    }
    dom.pad.addEventListener('touchend', touchRelease, { passive: false });
    dom.pad.addEventListener('touchcancel', touchRelease, { passive: false });
  } else {
    dom.pad.addEventListener('pointerdown', function (e) {
      e.preventDefault();
      try { dom.pad.setPointerCapture(e.pointerId); } catch (err) {}
      setHeld(e.pointerId, btnAt(e.clientX, e.clientY));
    });
    dom.pad.addEventListener('pointermove', function (e) {
      if (!(e.pointerId in held)) return;
      e.preventDefault();
      setHeld(e.pointerId, btnAt(e.clientX, e.clientY));
    });
    function release(e) {
      if (!(e.pointerId in held)) return;
      press(held[e.pointerId], false);
      delete held[e.pointerId];
    }
    dom.pad.addEventListener('pointerup', release);
    dom.pad.addEventListener('pointercancel', release);
    dom.pad.addEventListener('lostpointercapture', release);
  }
  dom.pad.addEventListener('contextmenu', function (e) { e.preventDefault(); });

  // Stop the page from scrolling or rubber-banding under a finger while playing.
  document.addEventListener('touchmove', function (e) {
    if (session) e.preventDefault();
  }, { passive: false });

  // ---------------------------------------------------------------- library UI

  function romRow(r) {
    var row = document.createElement('div');
    row.className = 'rom';

    var badge = document.createElement('span');
    badge.className = 'badge';
    badge.textContent = consoleOf(r.name);
    row.appendChild(badge);

    var meta = document.createElement('div');
    meta.className = 'meta';
    var n = document.createElement('div');
    n.className = 'name';
    n.textContent = r.name;
    var sub = document.createElement('div');
    sub.className = 'sub';
    var bits = [fmtBytes(r.size)];
    if (r.state) bits.push('save state');
    if (r.auto) bits.push('autosave');
    if (r.sav) bits.push('battery');
    sub.textContent = bits.join(' · ');
    meta.appendChild(n); meta.appendChild(sub);
    row.appendChild(meta);

    var hasSave = r.auto || r.state;
    var play = document.createElement('button');
    play.className = 'btn primary';
    play.textContent = hasSave ? 'Continue' : 'Play';
    play.onclick = function () { startGame(r.name, hasSave); };
    row.appendChild(play);

    if (hasSave) {
      var fresh = document.createElement('button');
      fresh.className = 'btn';
      fresh.textContent = 'New';
      fresh.onclick = function () { startGame(r.name, false); };
      row.appendChild(fresh);
    }

    // Two-tap confirm, never window.confirm: dialogs block browser automation.
    var del = document.createElement('button');
    del.className = 'btn danger';
    del.textContent = 'Delete';
    var armed = false, armTimer = 0;
    del.onclick = function () {
      if (!armed) {
        armed = true;
        del.textContent = 'Sure?';
        armTimer = setTimeout(function () { armed = false; del.textContent = 'Delete'; }, 4000);
        return;
      }
      clearTimeout(armTimer);
      del.disabled = true;
      api.del(r.name).then(refresh).catch(function (e) { console.log('delete failed', e); });
    };
    row.appendChild(del);

    return row;
  }

  function refresh() {
    return api.roms().then(function (list) {
      dom.romList.textContent = '';
      list.forEach(function (r) { dom.romList.appendChild(romRow(r)); });
      dom.empty.classList.toggle('hidden', list.length > 0);
    }).catch(function (e) {
      console.log('rom list failed:', errText(e));
      dom.romList.textContent = '';
      dom.empty.classList.remove('hidden');
    });
  }

  function pingStatus() {
    return api.ping().then(function (p) {
      dom.status.textContent = 'reader connected · ' + fmtBytes(p.free) +
        ' free · up ' + Math.floor(p.uptime / 60) + 'm';
      dom.status.className = '';
    }).catch(function () {
      dom.status.textContent = 'reader not reachable';
      dom.status.className = 'bad';
    });
  }

  // ---------------------------------------------------------------- wiring

  dom.romFile.addEventListener('change', function () {
    dom.uploadBtn.disabled = !dom.romFile.files.length;
  });
  dom.uploadBtn.addEventListener('click', function () {
    var f = dom.romFile.files[0];
    if (!f) return;
    dom.uploadBtn.disabled = true;
    dom.uploadBtn.textContent = 'Uploading';
    var fd = new FormData();
    fd.append('rom', f, f.name);
    fetch('/api/upload', { method: 'POST', body: fd }).then(function () {
      dom.romFile.value = '';
      return refresh();
    }).catch(function (e) {
      console.log('upload failed:', errText(e));
    }).then(function () {
      dom.uploadBtn.textContent = 'Upload';
      dom.uploadBtn.disabled = !dom.romFile.files.length;
    });
  });

  dom.backBtn.onclick = function () { leaveGame(); };
  dom.saveBtn.onclick = manualSave;
  dom.loadBtn.onclick = manualLoad;
  dom.resetBtn.onclick = function () {
    // WasmBoy's worker calls reject on a slow round trip, so never leave this
    // promise unhandled.
    if (session) {
      Promise.resolve(session.core.reset())
        .catch(function (e) { console.log('reset failed:', errText(e)); });
    }
  };
  dom.muteBtn.onclick = function () {
    if (!session) return;
    session.muted = !session.muted;
    session.core.setMuted(session.muted);
    dom.muteBtn.textContent = session.muted ? 'Unmute' : 'Mute';
  };
  var SPEEDS = [1, 2, 4];
  dom.speedBtn.onclick = function () {
    if (!session) return;
    var i = (SPEEDS.indexOf(session.speed || 1) + 1) % SPEEDS.length;
    session.speed = SPEEDS[i];
    session.core.setSpeed(session.speed);
    dom.speedBtn.textContent = session.speed + 'x';
  };
  dom.padBtn.onclick = function () {
    dom.pad.classList.toggle('hidden');
    fitCanvas();
  };

  window.addEventListener('resize', fitCanvas);
  window.addEventListener('orientationchange', function () { setTimeout(fitCanvas, 200); });
  // Mobile browser chrome sliding away, or the pad being toggled, changes the
  // stage without firing a window resize.
  if (typeof ResizeObserver === 'function') {
    new ResizeObserver(fitCanvas).observe(dom.stage);
  }

  // Show the on-screen pad by default where there is no keyboard.
  if (coarsePointer) dom.pad.classList.remove('hidden');

  pingStatus();
  refresh();
  setInterval(pingStatus, PING_MS);

  // Exposed for automated testing only.
  window.__rg = {
    press: press, session: function () { return session; },
    wrap: wrap, unwrap: unwrap, readHeader: readHeader,
    packSections: packSections, unpackSections: unpackSections,
    uploadAuto: uploadAuto, manualSave: manualSave, manualLoad: manualLoad
  };
})();
