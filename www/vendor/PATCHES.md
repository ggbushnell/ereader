# Local patches to vendored bundles

## gb.js (WasmBoy) — fast forward runs frames per tick, 2026-10-01

WasmBoy's emulation Web Worker runs exactly one Game Boy frame per
`setTimeout` tick and makes fast forward by dividing the tick interval by the
speed (`lib/wasmboy/worker/update.js`, `scheduleNextUpdate`). On a phone the
timer cannot be scheduled that often, so 2x barely moved and 4x did nothing
while the audio (stretched on the main thread) raced ahead and crackled.

The worker is embedded in gb.js as a base64 `data:` URL. Three edits inside it:

1. `update()` executes `n = min(8, round(speed))` frames per tick when speed > 1
   (the audio-checked path chains `x()` n times through a new `X(a,b,n)`; the
   silent path loops `executeFrame()`), then posts graphics and memory once.
2. `scheduleNextUpdate` divides the interval by the speed only for speed < 1
   (slow motion), so the tick stays at the game's frame rate and the speed is
   carried by frames per tick.
3. Nothing else. The FPS throttle still compares ticks against
   `(gameboyFrameRate + 1) * speed`, which the tick rate never reaches.

To redo the patch on a new upstream gb.js: decode the data URL, apply the three
string edits (see git history of this file's commit), re-encode, run
`tools/build_www.sh`.

### Follow-up, same day: audio stretched to the achieved speed

Main thread, `playAudio`: the playback rate was `speed` whenever speed != 1,
so when the core could not keep up (a phone at 4x) the audio raced ahead of
the frames and crackled. Now, at speed > 1, the rate is `(averageFps / 60) *
speed`, the same correction the stock code already applied at 1x when the
core fell below 57 fps. Measured with the game's own play-time clock: laptop
Chrome 1x -> 0.9x, 2x -> 1.7x, 4x -> 2.8x (frames per tick working, 4x
capped by per-tick work); the phone, warm after an hour, held only 0.8x
even at 1x, so fast forward there is bounded by the device.
