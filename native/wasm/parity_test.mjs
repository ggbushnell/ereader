// Runs the native pokeview and its WebAssembly build on the same pack and
// work-RAM snapshots and fails on any difference in what they print or draw.
//
//   node native/wasm/parity_test.mjs <native pokeview> <pokered.pack> [snapshot.wram ...]
//
// With no snapshots it uses a blank (all-zero) one for each game. A blank
// snapshot exercises every decoder and view path without any real save; real
// snapshots stay off the public repo and are checked by hand.
import { execFileSync } from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import createPokeview from '../out/wasm/pokeview.mjs';

const WRAM_BYTES = 0x2000;
const [nativeBin, packPath, ...wramPaths] = process.argv.slice(2);
if (!nativeBin || !packPath) {
  console.error('usage: parity_test.mjs <native pokeview> <pokered.pack> [snapshot.wram ...]');
  process.exit(2);
}
const pack = fs.readFileSync(packPath);
const snapshots = wramPaths.length
  ? wramPaths.map((p) => ({ name: path.basename(p), bytes: fs.readFileSync(p) }))
  : [{ name: 'blank', bytes: Buffer.alloc(WRAM_BYTES) }];

// Each case: arguments after --root/--wram, and the files it writes (compared byte for byte).
const CASES = [
  { args: ['--decode'] },
  { args: ['--view-json'] },
  { args: ['--view-json', '--page', '1'] },
  { args: ['--view-json', '--terrain'] },
  { args: ['--all', 'OUT/x'], files: ['x-home.pgm', 'x-inventory.pgm', 'x-terrain.pgm'] },
];

function runNative(wram, game, args) {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'pv-'));
  fs.mkdirSync(path.join(root, 'games/aux'), { recursive: true });
  fs.writeFileSync(path.join(root, 'games/aux/pokered.pack'), pack);
  fs.writeFileSync(path.join(root, 'snap.wram'), wram);
  fs.mkdirSync(path.join(root, 'out'));
  const a = args.map((s) => s.replace('OUT/', path.join(root, 'out') + '/'));
  const stdout = execFileSync(nativeBin, ['--root', root, '--wram', path.join(root, 'snap.wram'), '--game', game, ...a],
    { encoding: 'utf8', stdio: ['ignore', 'pipe', 'pipe'] });
  const read = (f) => fs.readFileSync(path.join(root, 'out', f));
  return { stdout: stdout.trim(), read };
}

async function runWasm(wram, game, args) {
  const lines = [];
  const mod = await createPokeview({ print: (s) => lines.push(s), printErr: () => {} });
  mod.FS.mkdirTree('/g/games/aux');
  mod.FS.mkdirTree('/out');
  mod.FS.writeFile('/g/games/aux/pokered.pack', pack);
  mod.FS.writeFile('/snap.wram', wram);
  const a = args.map((s) => s.replace('OUT/', '/out/'));
  const rc = mod.callMain(['--root', '/g', '--wram', '/snap.wram', '--game', game, ...a]);
  if (rc) throw new Error(`wasm pokeview exited ${rc}`);
  return { stdout: lines.join('\n').trim(), read: (f) => Buffer.from(mod.FS.readFile('/out/' + f)) };
}

// The service worker keeps one module and calls main again for every
// command, so the same cases also run back to back on one shared instance.
let shared = null, sharedLines = [];
async function runShared(wram, game, args) {
  if (!shared) {
    shared = await createPokeview({ print: (s) => sharedLines.push(s), printErr: () => {} });
    shared.FS.mkdirTree('/g/games/aux');
    shared.FS.mkdirTree('/out');
  }
  const FS = shared.FS;
  FS.writeFile('/g/games/aux/pokered.pack', pack);
  // Each native run starts from a fresh root with no achievement stamps.
  try { FS.unlink('/g/games/aux/achievements.bin'); } catch (e) { /* not there yet */ }
  FS.writeFile('/snap.wram', wram);
  sharedLines = [];
  const a = args.map((s) => s.replace('OUT/', '/out/'));
  const rc = shared.callMain(['--root', '/g', '--wram', '/snap.wram', '--game', game, ...a]);
  if (rc) throw new Error(`shared wasm pokeview exited ${rc}`);
  return { stdout: sharedLines.join('\n').trim(), read: (f) => Buffer.from(FS.readFile('/out/' + f)) };
}

let failures = 0, checks = 0;
for (const run of [runWasm, runShared]) {
for (const snap of snapshots) {
  for (const game of ['gen1', 'gen2']) {
    for (const c of CASES) {
      const label = `${run === runShared ? 'shared ' : ''}${snap.name} ${game} ${c.args.join(' ')}`;
      try {
        const n = runNative(snap.bytes, game, c.args);
        const w = await run(snap.bytes, game, c.args);
        checks++;
        if (n.stdout !== w.stdout) {
          failures++;
          let i = 0;
          while (i < n.stdout.length && n.stdout[i] === w.stdout[i]) i++;
          console.log(`FAIL ${label}: output differs at char ${i}`);
          console.log(`  native: …${n.stdout.slice(Math.max(0, i - 40), i + 80)}`);
          console.log(`  wasm:   …${w.stdout.slice(Math.max(0, i - 40), i + 80)}`);
          continue;
        }
        for (const f of c.files || []) {
          checks++;
          if (!n.read(f).equals(w.read(f))) { failures++; console.log(`FAIL ${label}: ${f} differs`); }
        }
        console.log(`ok   ${label}`);
      } catch (e) {
        failures++;
        console.log(`FAIL ${label}: ${e.message}`);
      }
    }
  }
}
}
console.log(`${checks - failures}/${checks} checks identical`);
process.exit(failures ? 1 : 0);
