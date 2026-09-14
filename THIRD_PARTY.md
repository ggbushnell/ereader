# Third party components

Bundled in this repository:

- `www/vendor/nes.js`: [jsnes](https://github.com/bfirsh/jsnes), Apache-2.0.
- `www/vendor/gb.js`: [WasmBoy](https://github.com/torch2424/wasmboy) 0.7.1,
  GPL-3.0-or-later. It runs in the player's browser as its own program; the
  firmware only stores and serves the file. If you redistribute a build, keep
  this notice and WasmBoy's license with it.
- `src/drivers/GxEPD2_576_T81_Fast.*`: a fast partial refresh driver derived
  from [GxEPD2](https://github.com/ZinggJM/GxEPD2) (GPL-3.0) for the
  GDEH0576T81 panel, see the file header.

Pulled in at build time by PlatformIO (see `platformio.ini` for versions):
GxEPD2, Adafruit GFX, U8g2_for_Adafruit_GFX, Adafruit BME280, Adafruit
Unified Sensor, ArduinoJson.

Not included, never to be committed:

- Game ROMs. Upload your own dumps through the games page.
- Pokemon Red/Blue assets. `tools/pokered_pack.py` builds the companion
  display's asset pack from your own checkout of the
  [pret/pokered](https://github.com/pret/pokered) disassembly; the pack and
  the checkout stay on your machine (`build_pack/`, `third_party/` are
  ignored).
- Book text. The converters in `tools/` turn your own PDFs and text files into
  the reader's page format; the outputs live in ignored folders.
