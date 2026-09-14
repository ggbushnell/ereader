# ereader

An open source ESP32-S3 e-reader with a 5.76 inch e-paper panel that is also
a Game Boy and NES game server for your phone, with a live Pokemon companion
dashboard on the e-ink while you play. Apache-2.0 code, CC BY 4.0 docs, see
`NOTICE`. Contract and internals: `SPEC.md`.

A small battery-optional e-reader built on an ESP32-S3 and a Good Display
GDEH0576T81 e-paper panel (5.76 inch, 920x680, 1 bit, run in portrait so the
page is 680 wide by 920 tall) on a DESPI-C02 adapter,
with a BME280 environment sensor and a 5-way d-pad. Books are plain text
paginated on the host: a Python converter turns
PDFs into a fixed 54 column by 37 row page grid and packs them into a compact
MPG1 container, so the firmware never has to wrap text and boot-to-page is
fast. Power is expected to come and go (plug in a battery, turn a few pages,
unplug), so the e-ink image persists with no power and the reading position is
written to flash on every single page turn. This is a standalone side project
by the people behind [Muon Sortes](https://muonsortes.com), not part of it.

## Games server and the Pokemon companion display

Pick the Games tile and the reader joins your WiFi (or raises its own access
point, `ereader-games`), announces `http://ereader.local/`, and serves a
browser page with an NES emulator (jsnes) and a Game Boy / Game Boy Color
emulator (WasmBoy). Upload your own ROM dumps from the page, play on a phone
or a laptop, and the reader keeps the saves so either device can continue
where the other stopped. The page has a touch pad, keyboard controls (arrows or
WASD, X or E for A, Z or R for B, Enter for Start, Shift for Select) and 1x/2x/4x
speed.

While Pokemon Red or Blue runs, the page posts the emulator's 8 KB work RAM
to the reader once a second and the e-ink turns into a companion dashboard
built entirely from the game's own tiles, font and sprites:

- **Home**: the Kanto town map with your position, trainer card, money,
  Pokedex counts, play time, the party with HP bars, levels, status and
  types, and the eight badges.
- **Inventory**: the bag with Mart prices, PC items, the day care and the
  current box.
- **Terrain**: the current map drawn from its real tileset around you, the
  wild encounters with their odds, and the placed and hidden items with
  taken ones struck through.
- **Battle**: front and back sprites, HP, types, base stats, catch rate, your
  four moves scored against the enemy's types, and the enemy's moves against
  you.

The repo ships the generator (`tools/pokered_pack.py`), which builds the
asset pack from your own checkout of the
[pret/pokered](https://github.com/pret/pokered) disassembly; no game assets
or ROMs are in the repo. Upload the resulting `pokered.pack` from the games
page. Design docs: `docs/pokemon-aux-display-plan-2026-09-12.md`,
`docs/pokemon-aux-display-layout.md`, `docs/pokered-pack-format.md`.

Want the same for another game? `docs/companion-dashboard-game-candidates-2026-09-13.md`
ranks games with documented RAM maps and full disassemblies (Pokemon
Gold/Silver/Crystal, Link's Awakening, FireRed, Harvest Moon, Dragon Warrior
IV and more) with the addresses, repos and emulator memory access per system.

## Hardware and wiring

Pins are declared in `include/pins.h`, the single source of truth.

| Signal | ESP32-S3 GPIO | Connects to | Notes |
|---|---|---|---|
| EINK_SCK | 12 | adapter SCK | |
| EINK_MOSI | 11 | adapter SDI | |
| EINK_MISO | none | not connected | panel is write-only; SPI.begin() gets -1 |
| EINK_CS | 10 | adapter CS | |
| EINK_DC | 9 | adapter D/C | |
| EINK_RST | 13 | adapter RES | |
| EINK_BUSY | 46 | adapter BUSY | |
| EINK_VCC | 3V3 | adapter VCC | panel runs at 3.3 V, not 5 V |
| EINK_GND | GND, any ground pin | adapter GND | |
| BME_SDA | 16 | BME280 SDA | Wire.begin(16, 17) |
| BME_SCL | 17 | BME280 SCL | |
| BME_VCC | 3V3 | BME280 VIN/VCC | 3.3 V; most breakouts have an onboard regulator and accept it |
| BME_GND | GND | BME280 GND | |
| BTN_UP | 4 | d-pad up | |
| BTN_DOWN | 5 | d-pad down | |
| BTN_LEFT | 6 | d-pad left | |
| BTN_RIGHT | 7 | d-pad right | |
| BTN_CENTER | 15 | d-pad center click | |
| BTN_COMMON | GND | d-pad common | all five buttons share one ground leg |

Pins deliberately left alone: 0, 3, 45 and 46 are strapping pins (holding a
d-pad direction through reset on one of those would change the boot mode), 19
and 20 are native USB, 33 through 37 are claimed by octal PSRAM on N16R8 style
boards, and 43/44 are UART0. The map above is free on both the N8R2 and the
N16R8 bench boards.

**Panel.** Good Display GDEH0576T81, 5.76 inch, 920x680, 1 bit, SSD2677
controller, driven through a DESPI-C02 adapter. **The reader runs the panel in
portrait.** `ui::begin()` rotates it so the logical page is 680 wide by 920
tall, which is what every layout number in `include/config.h` is written
against. The rotation is the single constant `DISPLAY_ROTATION` in
`include/config.h`: 1 (the default) puts the panel's FPC tail on the left edge
of the portrait page, 3 puts it on the right and turns the image 180 degrees.
If the first frame comes up upside down on the bench, set it to 3 and reflash;
nothing else changes. The adapter's header is labelled
BUSY, RES, D/C, CS, SCK, SDI, GND, VCC, which is the row the table above wires.
Set the adapter's RESE switch to 0.47. There is no panel power enable line on
this adapter, so nothing has to be driven before the first SPI access.

Wire the adapter with soldered joints, not Dupont crimps. Resistive crimps
sagged the panel rail under refresh current on the retired RP2040 build and
eventually damaged that controller. Put 470uF plus 100nF right across the
adapter's VCC/GND and confirm with a DMM that adapter VCC reads within 0.03 V of
the board's 3V3 at idle.

**Refresh regime.** The panel is not driven by GxEPD2's stock
`GxEPD2_576_GDEH0576T81` class. That driver declares
`hasFastPartialUpdate = false` and runs the full ghost-clearing waveform even
when you ask for a partial window, so every page turn and every menu cursor
move costs a multi-second white flash. The reader instead links
`src/drivers/GxEPD2_576_T81_Fast.{h,cpp}`, vendored from the Muon Sortes
firmware (`Muon_Decider/src/drivers/GxEPD2_576_GDEH0576T81.{h,cpp}`). It loads
its own partial LUT and keeps a shadow copy of the frame currently on glass.
The panel's wire format is 2 bits per pixel, `[old_value, new_value]`, and the
partial LUT drives only the pixels whose two bits differ, so a partial refresh
touches just the ink that actually changed: no flash, a few hundred
milliseconds.

The class is deliberately named differently from the stock one rather than
shadowing its header. `GxEPD2_BW.h` includes every panel header it can find and
the library's own `.cpp` is compiled either way, so a same-name copy would
depend on include order and on the linker preferring our object over the
archive member. With a distinct name the stock class is still compiled but
nothing instantiates it, and the linker drops it (confirmed: zero
`GxEPD2_576_GDEH0576T81` symbols in `firmware.elf`).

`ui::beginFrame()` in `src/ui.cpp` picks the waveform per frame:

- Partial: `display.epd2.setFastRefresh(true)` then `setPartialWindow()` over
  the whole screen. The driver ships a full frame and ignores the window
  rectangle, so the window must stay full-screen and the `GxEPD2_BW` page
  height must stay the full panel height.
- Full: `setFastRefresh(false)` then `setFullWindow()`. Forced every
  `PARTIALS_BEFORE_FULL` frames (`include/config.h`, 10), and always on the
  frame after an image page, which would otherwise ghost through.
- The shadow stays coherent by itself: a full frame goes out through
  `writeImageForFullRefresh()`, which rewrites the shadow from the same bitmap
  it just sent.

The shadow is about 78 KB, `malloc`ed on the first write and never freed, on
top of the 78 KB static page buffer. If that allocation ever fails the driver
refuses fast mode and stays on full refreshes, because a partial frame with no
shadow would drive no pixels at all and leave the stale image up.
`ui::begin()` prints the free heap and largest free block after display init so
you can check the margin against the roughly 50 KB a news sync needs for TLS.

**Buttons.** Five momentary switches, each with one leg to its GPIO and the
other leg to a common GND rail. The firmware uses internal pull-ups, so the
inputs are active LOW and no external resistors are needed. Debounce is about
20 ms in firmware, edge-triggered on press.

**Sensor.** BME280 on I2C at address 0x76, with 0x77 as the fallback. Boards
sold as "BMP280" will not report humidity.

## Workflow

Everything below runs from the project root.

1. Drop text-based PDFs into `books_src/`.
2. Convert them:

   ```
   python3 tools/pdf2book.py
   ```

   That writes one `data/books/<slug>.pgs` per PDF and prints a per-book
   summary plus the running total against the 6.09 MB LittleFS budget. You can
   also pass explicit paths and override the title:

   ```
   python3 tools/pdf2book.py books_src/moby.pdf --title "Moby-Dick"
   python3 tools/pdf2book.py books_src/moby.pdf --out /tmp/preview
   ```

3. Check the pagination without hardware:

   ```
   python3 tools/mpg1_dump.py data/books/moby.pgs      # header and limit checks
   python3 tools/mpg1_dump.py data/books/moby.pgs 12   # render page 12 as text
   ```

4. Upload the books (contents of `data/`) to the device filesystem:

   ```
   cd ereader && pio run -e esp32s3 -t uploadfs
   ```

   This rewrites the whole filesystem partition, so it also wipes the `.pos`
   reading positions and `/current.txt`. Every book reopens at page 1 after an
   `uploadfs`.

5. Build and upload the firmware:

   ```
   cd ereader && pio run -e esp32s3 -t upload
   ```

Firmware and books are uploaded separately. Adding a book needs only step 2 and
step 4; changing firmware needs only step 5.

### Converting a scanned book

`pdf2book.py` needs a real text layer. A scan has none worth using (Acrobat's
own Paper Capture layer runs words together and misreads whole letters), so a
scanned book goes through four steps instead of step 2. The Bill Porter *Road
to Heaven* conversion of 2026-09-02 is the worked example:

```
BOOK="books_src/road_to_heaven.pdf"

# 1. OCR with Vision. --no-spread keeps output pages 1:1 with PDF pages, which
#    the figure manifest depends on.
swift tools/ocr_pdf.swift --no-spread "$BOOK" > build/road.txt

# 2. Restore paragraphs, block quotes, verse and interview turns. Vision gives
#    the best characters but no geometry; pdftotext -bbox-layout gives the
#    geometry. This merges the two.
python3 tools/ocr_paragraphs.py build/road.txt "$BOOK" build/road_to_heaven.txt

# 3. Extract the plates. --full-res stores them at the full 680x896 content
#    area; drop it for the 2/5 reduced box if the filesystem is tight.
#    --force-pages emits a whole page with no detection, for cover art and for
#    line art the ink detector will not see (here, a two page map spread).
swift tools/extract_figures.swift "$BOOK" build/figures --full-res --force-pages 1
swift tools/extract_figures.swift "$BOOK" build/maps --full-res --pages 10-11 \
      --force-pages 10,11        # then merge the two manifests

# 4. Build the book. Running heads that carry a page number are unique per
#    page, so the automatic repeat detector cannot see them: name them with
#    --strip-regex (one per head form, verso and recto).
python3 tools/txt2book.py build/road_to_heaven.txt --title "Road to Heaven" \
      --dual --figures build/figures/manifest.json \
      --strip-regex '[0-9SOol]{1,4}[.,]?\s*[-*DO0oa]?\s*Road to Heaven' \
      --strip-regex '(?:Road to Heaven|Hermit Heaven|...)\s*[-*DO0oa]\s*[0-9SOol]{1,4}[.,]?'
```

Review the figures before step 4: delete any `build/figures/review/<id>.png`
and `txt2book.py --figures` leaves that figure out.

Full res plates cost about 76 KB each (85 bytes per row times 896), so 44 of
them are 2.87 MB. That is affordable on the 6.09 MB filesystem for one such
book at a time, not for two.

**If the upload does not start.** The bench ESP32-S3 boards do not always
auto-reset into the ROM downloader. Hold BOOT, tap RST, release BOOT, then run
the upload again. USB-CDC is on (`ARDUINO_USB_MODE=1`,
`ARDUINO_USB_CDC_ON_BOOT=1`), so the serial device disappears and comes back
around a flash; if `pio device monitor` cannot find the port, replug.

### Converter dependencies

The converter needs a PDF text extraction library. `pypdf` is preferred and is
what this project is tested against; `pdfminer.six` works as a fallback.

```
python3 -m pip install -r tools/requirements.txt
```

If pip is broken or blocked for your default `python3` (common with Homebrew
Python builds), use a virtual environment instead:

```
/usr/bin/python3 -m venv .venv
.venv/bin/pip install -r tools/requirements.txt
.venv/bin/python tools/pdf2book.py
```

## Controls

Five buttons: up, down, left, right, center click.

Reading mode:

- LEFT or DOWN: next page.
- RIGHT or UP: previous page.
- CENTER: open the menu.

LEFT/RIGHT is the original mapping (the founder holds the device so LEFT is the
natural forward tilt). DOWN/UP was added with the portrait UI, where the page
runs top to bottom and the vertical axis reads as forward and back. The menu
still uses UP/DOWN for the cursor, so the vertical page-turn mapping applies
only while reading.

Page turns use a fast partial refresh, with a full refresh every
`PARTIALS_BEFORE_FULL` (10) turns to clear ghosting. See "Refresh regime"
under Hardware and wiring.

Menu (cursor moves use partial refresh):

- The header shows live BME280 readings: temperature in C, humidity in percent,
  pressure in hPa, and the time of the last news sync if there has been one.
- Items: Resume, then one entry per book found on the filesystem (each shows
  its title and saved position), then Sync news, WiFi setup, Auto sync on/off,
  and Jump to page.
- UP and DOWN move the cursor, CENTER selects, LEFT goes back or exits.

Jump to page:

- LEFT and RIGHT step the target by 1.
- UP and DOWN step the target by 10.
- CENTER jumps. The target is clamped to 1 through N.

## WiFi and the news brief

The radio is off while you read. It comes up for two menu items only, and both
turn it back off before they return.

**WiFi setup.** Pick it from the menu and the reader puts up an open access
point called `ereader-setup`. Join it from a phone or a laptop and open
`http://192.168.4.1` (most phones offer the page by themselves, since the
reader answers every DNS query with its own address). Pick your network from
the list or type the name, enter the password, and press Save. The reader
stores it, then joins the network for real to check it and reports back on both
the web page and the panel. Press the center button on the reader to finish.
Up to four networks are remembered. They live in NVS, not in the filesystem, so
`pio run -t uploadfs` does not wipe them.

**Sync news.** Fetches a plain text brief over HTTPS, paginates it on the
device with the same rules the host converter uses, and writes it to
`/books/news.pgs`. It then shows up in the library as "News brief" with the
time it was fetched, and reads like any other book. An unchanged brief costs
one 304 and no download, because the previous response's ETag is sent back.
The brief always opens at page 1 after a sync, since the old reading position
points into text that is gone.

The device paginates the brief on both text grids and writes it as a dual size
MPG2 book, so "Text size" in the menu switches the brief between Normal and
Large the same way it does for a host built dual size book. The brief opens in
the large text after every sync.

**Auto sync: on/off.** When on, the reader syncs once at every boot, after it
has drawn the reading page. Default is off. There is no clock between boots, so
there is no "sync if older than 12 hours" rule to have: the toggle is the whole
policy.

Before the first sync, fill in `NEWS_TOKEN` in `include/config.h`. It ships as
`"CHANGE_ME"` and is the shared secret in the brief URL's query string.
`NEWS_URL` defaults to
`https://api.muonsortes.com/brief/latest.txt?k=<NEWS_TOKEN>`. Setting
`WIFI_SETUP_AP_PASSWORD` to 8 characters or more makes the setup AP WPA2
instead of open.

A sync needs room on the filesystem for the raw body and the page blobs at the
same time, and the brief is paginated twice (once per text size), so keep
roughly 1 MB free in the books partition if you expect full size briefs.

**If a sync fails.** The panel prints `error: <reason>`.

- `no saved network`: run WiFi setup first.
- `wifi`: none of the stored networks would associate.
- `connect -1`: the TLS connection did not come up. Usually the certificate
  chain moved; see `src/ca_roots.h` and the `NEWS_TLS_ALLOW_INSECURE_FALLBACK`
  note in SPEC.md.
- `http 401` / `http 404`: check `NEWS_TOKEN` and `NEWS_URL`.
- `brief too large`: the response was over 200 KB (`NEWS_MAX_BYTES`).

## Keeping the two paginators in sync

The device paginates a downloaded brief itself, and that C++ has to produce
exactly what `tools/txt2book.py` would have produced for the same text. This is
the check:

```
python3 tools/check_paginator.py            # built-in sample
python3 tools/check_paginator.py brief.txt  # your own text
```

It compiles `src/text_paginate.cpp` natively with the system `c++`, runs both
pipelines over the same bytes on both text grids, and diffs the pages line by
line. Run it after touching either `src/text_paginate.cpp` or the cleaning and
pagination functions in `tools/pdf2book.py`.

## File formats

**`/books/<slug>.pgs`, MPG1.** The book container, little-endian. Magic `MPG1`
(4 bytes), then u32 page count, then u16 title length and that many bytes of
UTF-8 title, then u32 absolute file offsets for each page, then the page blobs
back to back. A page blob is raw UTF-8 with its lines joined by `\n`; page p
runs from `offsets[p]` to `offsets[p+1]`, or to end of file for the last page.
Every line is at most 54 characters and every page at most 37 lines, so the
firmware renders lines verbatim. Text is ASCII plus common Latin-1 punctuation
only, since the profont22 font has no exotic glyphs.

**`/books/<slug>.pgs`, MPG2.** The same book paginated on more than one text
grid, which is what "Text size" switches between. Magic `MPG2`, then u8 variant
count, u16 title length and the title, then per variant a u8 font id, a u32
page count and a page table of `{u32 offset, u32 length, u32 anchor}` entries,
then the blob region. Anchors map a reading position from one text size to the
other. `tools/txt2book.py --dual` writes this, and so does a news sync. The
full field by field description is in SPEC.md.

**`/books/<slug>.pos`.** A u32 little-endian page index (0 based), rewritten on
every page turn. Page turns are seconds apart, so flash wear is negligible.

**`/current.txt`.** The slug of the book that is currently open, as plain text.
On boot the firmware reads this, reads the matching `.pos`, and renders that
page immediately.

Slugs come from the PDF filename stem: lowercased, every run of non-alphanumeric
characters replaced with `_`, truncated to 24 characters.

## Troubleshooting

**The panel is blank and nothing was ever drawn.** Check the DESPI-C02 RESE
switch is on 0.47, that the FPC is fully seated and latched, and that RES
(GPIO 13) and BUSY (GPIO 46) are connected. This failure mode is silent: SPI
succeeds, GxEPD2 returns no error, and the screen simply stays blank. Also
confirm the adapter VCC is on 3V3, not 5V. BUSY should read 0 at idle and swing
to about 3.3 V during a refresh; a mid-rail BUSY means a damaged controller.

**Sensor readings show as n/a.** Check the I2C wiring on GPIO 16 (SDA) and
GPIO 17 (SCL), that the BME280 has power and ground, and the I2C address. The firmware
tries 0x76 first and falls back to 0x77; some breakouts ship strapped the other
way. A part that is really a BMP280 will not return humidity.

**A book does not appear in the menu.** Confirm the `.pgs` file is in
`data/books/` on the host, then re-run `pio run -e esp32s3 -t uploadfs`. Uploading firmware
with `pio run -e esp32s3 -t upload` does not touch the filesystem, so a newly converted
book will not appear until the filesystem is uploaded again.

**The converter produces an empty or near-empty book.** The converter only
handles PDFs that carry a real text layer. A scanned PDF is a stack of page
images with no text, so extraction yields nothing. Run OCR on it first with a
tool such as `ocrmypdf` and convert the OCRed output. Building OCR into the
converter is out of scope for this project. Images and figures in any PDF are
skipped, since only text is extracted.

**`pio run -e esp32s3 -t uploadfs` fails or the device runs out of space.** The
filesystem partition is 6.09 MB, defined by the `spiffs` row in
`partitions.csv` (the image is LittleFS; the row is named `spiffs` because that
is the name the tooling looks for). The converter prints the running total for
`data/books/` and warns past about 1.76 MB. Remove a book and re-run, or on an 8
or 16 MB module extend that row to the end of flash.

## Brief delivery (server side)

The brief lives in Cloudflare KV behind the `muonsortes-api` Worker (`api/`):

- `POST https://api.muonsortes.com/brief`, `Authorization: Bearer <BRIEF_POST_TOKEN>`,
  plain-text body up to 200 KB. Written by a Claude Code routine (cloud, network
  access Custom with `api.muonsortes.com` allowed, token in the environment vars).
- `GET https://api.muonsortes.com/brief/latest.txt?k=<BRIEF_READ_TOKEN>`, returns
  the text with an `ETag`; the device sends `If-None-Match` and gets 304 when unchanged.

Both tokens are Worker secrets (`wrangler secret put`) and are recorded in
`.muonsortes-secrets.txt`. The device's copy of the read token is
`include/secrets.h` (gitignored; template in `secrets.h.example`).

Routine prompt tail:

    Write the brief to brief.txt (plain ASCII text, no Markdown, headline lines
    in CAPS, blank line between items, under 200 KB), then run:
    curl -sS -X POST https://api.muonsortes.com/brief \
      -H "Authorization: Bearer $BRIEF_POST_TOKEN" -H "Content-Type: text/plain" \
      --data-binary @brief.txt
    and confirm the response contains "ok":true.
