# Companion dashboard game candidates (2026-09-13)

Research for the open source e-reader platform (ESP32-S3, 6" e-ink, 680x920 portrait). The
reader serves a browser emulator page; the page runs the emulator in the phone or laptop
browser and POSTs emulator work RAM to the reader, which decodes the RAM and draws a
companion dashboard built from in-game assets extracted at build time by a generator script.

The reference build is Pokemon Red/Blue: jsnes and WasmBoy in `www/app.js`, an
8 KB `POST /api/wram` route in `src/games.cpp`, a RAM decoder in `src/pokemon_state.cpp`
(about 60 named WRAM addresses), four views in `src/pokemon_views.cpp`, and a 1426-line
generator `tools/pokered_pack.py` that turns a `pret/pokered` checkout into a 238 KB asset
pack. Roughly 4300 lines of firmware plus the generator, one long session. That is the unit
of effort every estimate below is measured against.

Every repository and RAM map linked here was fetched and confirmed to exist. Where a claim
could not be verified it is called out as unverified rather than presented as fact.

---

## Ranked top 10

| # | Game | System | Emulator | RAM map | Disassembly | POST size | Richness | Effort |
|---|---|---|---|---|---|---|---|---|
| 1 | Pokemon Gold/Silver | GBC | WasmBoy / binjgb | [Data Crystal](https://datacrystal.tcrf.net/wiki/Pok%C3%A9mon_Gold_and_Silver/RAM_map) | [pret/pokegold](https://github.com/pret/pokegold) | 32 KB full, 4 KB curated | 5 | Low |
| 2 | Pokemon Crystal | GBC | WasmBoy / binjgb | [Data Crystal](https://datacrystal.tcrf.net/wiki/Pok%C3%A9mon_Crystal/RAM_map) | [pret/pokecrystal](https://github.com/pret/pokecrystal) | 32 KB full, 4 KB curated | 5 | Low |
| 3 | Pokemon Yellow | GB | WasmBoy (as today) | [Data Crystal](https://datacrystal.tcrf.net/wiki/Pok%C3%A9mon_Yellow/RAM_map) (stub) | [pret/pokeyellow](https://github.com/pret/pokeyellow) | 8 KB, unchanged | 4 | Trivial |
| 4 | Zelda: Link's Awakening DX | GB/GBC | WasmBoy / binjgb | [Data Crystal](https://datacrystal.tcrf.net/wiki/The_Legend_of_Zelda:_Link%27s_Awakening_(Game_Boy)/RAM_map) | [zladx/LADX-Disassembly](https://github.com/zladx/LADX-Disassembly) | 8 KB | 4 | Medium |
| 5 | Pokemon FireRed/LeafGreen | GBA | mGBA wasm (patched) or gbajs2 | [Data Crystal](https://datacrystal.tcrf.net/wiki/Pok%C3%A9mon_3rd_Generation/Pok%C3%A9mon_FireRed_and_LeafGreen/RAM_map) | [pret/pokefirered](https://github.com/pret/pokefirered) | 1 to 2 KB curated | 5 | Medium-high |
| 6 | Harvest Moon: Friends of Mineral Town | GBA | mGBA wasm (patched) or gbajs2 | [Data Crystal](https://datacrystal.tcrf.net/wiki/Harvest_Moon:_Friends_of_Mineral_Town/RAM_map) | [StanHash/fomt](https://github.com/StanHash/fomt) | 2 to 4 KB curated | 5 | Medium-high |
| 7 | Dragon Warrior IV | NES | jsnes | [Data Crystal](https://datacrystal.tcrf.net/wiki/Dragon_Warrior_IV:RAM_map) | [TheAnsarya/dragon-warrior-4-info](https://github.com/TheAnsarya/dragon-warrior-4-info) | 2 KB RAM + 8 KB SRAM | 5 | Medium |
| 8 | Final Fantasy (NES) | NES | jsnes | [Data Crystal](https://datacrystal.tcrf.net/wiki/Final_Fantasy/RAM_map) | [Entroper/FF1Disassembly](https://github.com/Entroper/FF1Disassembly) | 2 KB RAM + 8 KB SRAM | 4 | Medium |
| 9 | Zelda II: The Adventure of Link | NES | jsnes | [Data Crystal](https://datacrystal.tcrf.net/wiki/Zelda_II:_The_Adventure_of_Link:RAM_map) | none found | 2 KB | 4 | Medium-high |
| 10 | Zelda: Oracle of Ages/Seasons | GBC | WasmBoy / binjgb | none | [Stewmath/oracles-disasm](https://github.com/Stewmath/oracles-disasm) | 32 KB | 4 | High |

Richness is 1 to 5 for how much slowly-changing state a dashboard can show. Effort is relative
to the Pokemon Red build. Super Metroid and Sonic 1 would rank around 4 and 5 on game merit
but are excluded from the table because no browser SNES or Genesis core currently exposes
RAM to page JavaScript (see the emulator section); they are covered as blocked candidates.

---

## Per-candidate detail

### 1. Pokemon Gold/Silver (GBC)

**Emulator and memory.** WasmBoy, exactly as the reader uses it today. WasmBoy's
`WORK_RAM_LOCATION` region is `WORK_RAM_SIZE = 0x8000`, that is the full 32 KB of GBC banked
work RAM laid out flat and contiguous with banking already resolved by the core
(`core/constants.ts`). Today `www/app.js` deliberately slices only the first 8 KB with the
comment that a DMG game never sees more. For a GBC title you extend `WRAM_BYTES` to 0x8000
and the banks come along for free. No new emulator work at all.

**RAM map.** The [Gold/Silver Data Crystal map](https://datacrystal.tcrf.net/wiki/Pok%C3%A9mon_Gold_and_Silver/RAM_map)
is the best-documented map found anywhere in this survey, several hundred fields. Map bank
`$DA00`, map number `$DA01`, overworld X/Y `$DA02`/`$DA03`, party structs from `$DA4C` at
`$30` bytes per Pokemon, bag items `$D5B8`-`$D5DF`, key items `$D5E2`-`$D5FA`, balls
`$D5FD`-`$D614`, money `$D573`-`$D575`, clock and day `$D1DC`-`$D1E0`, time `$D1EB`-`$D1EF`,
Johto badges `$D57C`, Kanto badges `$D57D`, event flags `$D7B7`-`$D8B6`.

**Disassembly.** [pret/pokegold](https://github.com/pret/pokegold), 712 stars, pushed
2026-08-13, not archived. Byte-matching builds for both Gold and Silver with published SHA1
hashes. Same organisation, same RGBDS tooling, same `gfx/*.png` layout that
`tools/pokered_pack.py` already parses. Cross-check addresses against the repo's own
`ram/wram.asm` rather than the wiki where they disagree.

**Dashboard.** Home with the Johto and Kanto map plus position, real-time clock and day of
week (Gen 2 has an RTC, a genuinely new widget for the reader); party with HP bars, levels
and held items; bag across the multi-pocket inventory; badge case showing 16 badges across
two regions. Very high density.

**Effort: low.** Risk: the only real one is that the party struct is 48 bytes with a
different layout to Gen 1, so `pokemon_state.cpp` needs a parallel decoder rather than a
tweak. The asset generator is close to a copy of the existing one.

### 2. Pokemon Crystal (GBC)

Same emulator story and same 32 KB slice as Gold/Silver.
[pret/pokecrystal](https://github.com/pret/pokecrystal) is the largest and best-known of the
Gen 2 disassemblies (2499 stars, pushed 2026-08-28). The
[Crystal Data Crystal map](https://datacrystal.tcrf.net/wiki/Pok%C3%A9mon_Crystal/RAM_map)
documents party stats `$DCD7`-`$DDFE`, items from `$D892`, TMs `$D859`-`$D88A`, HMs
`$D88B`-`$D891`, balls `$D8D7`-`$D8FF`, money `$D84F`-`$D850`, badges `$D857` and `$D858`,
play time `$D4C4`-`$D4C8`, but the page states plainly that it is heavily incomplete and it
does not document player position or map id. Read those out of the repo's `ram/wram.asm`,
which is the source of truth anyway.

Dashboard and effort are as Gold/Silver, plus Crystal's animated sprites and the Battle Tower
if you want a fifth view. **Effort: low**, one notch behind Gold/Silver only because more
fields come from source rather than from the wiki.

### 3. Pokemon Yellow (GB)

The near-free one. [pret/pokeyellow](https://github.com/pret/pokeyellow) is real and current
(873 stars, pushed 2026-09-02). The
[Yellow RAM map](https://datacrystal.tcrf.net/wiki/Pok%C3%A9mon_Yellow/RAM_map) is admittedly
stubby, about four unique addresses, because the page's own guidance is that Yellow is
Red/Blue's map shifted by -1 for most fields. Memory stays 8 KB, the emulator path is
unchanged, the pack generator is the same script pointed at a different checkout.

Dashboard: identical four views, with Pikachu happiness (`$D46F` in the Yellow map) as a
natural fifth tile since it is the game's signature mechanic and changes slowly.

**Effort: trivial.** Risk: the -1 offset is a rule of thumb, not a guarantee. Verify each
address against `pokeyellow`'s `ram/wram.asm` rather than blind-shifting, and keep the
offsets in a per-game table so Red and Yellow share one decoder.

### 4. Zelda: Link's Awakening DX (GB/GBC)

**Emulator and memory.** 8 KB, unchanged. The
[Data Crystal map](https://datacrystal.tcrf.net/wiki/The_Legend_of_Zelda:_Link%27s_Awakening_(Game_Boy)/RAM_map)
addresses sit in the `$D4xx` to `$DBxx` range, which is flat DMG-style addressing, so the
existing 8 KB slice should cover it. Confirm against the disassembly's WRAM section before
assuming, since DX runs on GBC hardware.

**RAM map.** About 40 fields, thin next to Pokemon but they are exactly the right 40. Room
and area `$D401`-`$D403`, dungeon grid position `$DBAE`, current health `$DB5A` (0x08 per
heart), max health `$DB5B`, held items A and B `$DB00`/`$DB01`, inventory slots
`$DB02`-`$DB0B`, rupees `$DB5D`-`$DB5E`, per-dungeon keys `$DB10`-`$DB14`, key count `$DBD0`,
and world map visited flags across `$D800`-`$D8FF`.

**Disassembly.** [zladx/LADX-Disassembly](https://github.com/zladx/LADX-Disassembly), 901
stars, pushed 2026-08-27. Builds byte-identical ROMs across JP, EN, DE and FR revisions with
published checksums, and has an active wiki documenting the engine. Note it is structured as
`src/`, `tools/` and `docs/` with no pre-extracted PNG graphics, so tile and sprite
extraction is work the generator has to do itself against the graphics banks.

**Dashboard.** Home with the 16x16 overworld grid shaded by the `$D800` visited flags plus
Link's current room; inventory showing the A/B slots and the ten inventory slots as item
icons; a dungeon view with key count, compass and map state per dungeon; hearts and rupees as
a persistent header. Three to four views, good density, and the explored-map view is the most
visually distinctive thing in this whole survey.

**Effort: medium.** Risk: no PNG asset dump, so you write graphics extraction from scratch.
That is the single biggest delta from the Pokemon builds, where pret hands you PNGs.

### 5. Pokemon FireRed/LeafGreen (GBA)

**Emulator and memory.** This is the hard part and it is covered in full in the emulator
section below. Short version: no browser GBA emulator currently exposes RAM to page
JavaScript through a documented API. The mGBA `core.memory["wram"].readRange()` API is the
desktop Lua scripting interface, not the wasm binding; the shipped `mgba.d.ts` has no
memory read at all. The two viable routes are a small C patch to the mGBA wasm build or
running gbajs2 in pure-JS mode and reading `mmu.memory[REGION_WORKING_RAM].buffer`.

**Memory to POST.** Never the full 288 KB. Gen 3 relocates its save blocks at runtime as an
anti-cheat measure, so the page must read a pointer each tick and dereference it:

- Pointer table base `0x03005008` (IWRAM), `gSaveBlock1Ptr` at `0x0300500C`,
  `gSaveBlock2Ptr` at `0x03005010`.
- Player position and map, via `[0x03005008]`: X at `+0x0` (u16), Y at `+0x2` (u16),
  map number `+0x4`, map bank `+0x5`.
- Trainer info via `*gSaveBlock1Ptr`: name `+0x0` (8 bytes), gender `+0x8`, trainer ID
  `+0xA`, secret ID `+0xC`, play time h/m/s/frames `+0xE` to `+0x12`.
- Money is XOR-encrypted, key at `*gSaveBlock1Ptr + 0xF20`.
- `gPlayerParty` at `0x02024284`-`0x02024478` in EWRAM, six 100-byte structs, not scrambled.
- Enemy party `0x0202402C`-`0x02024220`.

Pointer table (12 bytes) plus party (600 bytes) plus the SaveBlock1 and SaveBlock2 fields
that matter comes to roughly 1 to 2 KB per tick, which is smaller than the 8 KB the reader
already handles. The DMA scramble is a two-step read, not a blocker.

**Disassembly.** [pret/pokefirered](https://github.com/pret/pokefirered), 1600 stars, pushed
2026-08-04, byte-identical decomp. Note that the `sym_ewram.txt` files in the repo are linker
manifests, not resolved addresses; numeric symbols only exist after a local build produces
the linked map file. Plan on doing one build to generate a symbol table rather than scraping
addresses off GitHub.

**Dashboard.** The same four views as Red/Blue at higher fidelity: Kanto map, party with HP
and held items, bag across pockets, battle view. Add a fifth for the Pokedex or the
Sevii Islands.

**Effort: medium-high**, and essentially all of the extra cost is the emulator lane, not the
game. Once a GBA core with a memory hook exists, FireRed itself is a low-effort follow-on
because the decoder shape and the generator are so close to `pokered`. Risk: the emulator
work is a C and Emscripten build-toolchain task, which is a different skillset from the
firmware and generator work.

### 6. Harvest Moon: Friends of Mineral Town (GBA)

The most interesting non-Pokemon candidate in the survey, because the game's state is almost
entirely slow-changing, which is precisely what e-ink wants.

**RAM map.** The [Data Crystal map](https://datacrystal.tcrf.net/wiki/Harvest_Moon:_Friends_of_Mineral_Town/RAM_map)
gives money `0x02004080`, in-game year, day, hour and minute at `0x020025E8`-`0x020025EB`,
per-character affection values as 2-byte fields (Lillia `0x02004324`, Popuri `0x02004358`,
and the rest of the cast at similar stride), and farm and cottage state around `0x020025D8`
and `0x020025FC`. Important caveat the page states itself: addresses shift by `+0x2834` after
the first battery save, so the decoder must detect which of the two layouts is live.

**Disassembly.** [StanHash/fomt](https://github.com/StanHash/fomt), 40 stars, pushed
2026-02-08, builds a matching ROM with a stated SHA1. A second effort exists at
[not-alons/hmfomt](https://github.com/not-alons/hmfomt).

**Dashboard.** A farm status board: calendar with season, day and weather as the home view;
money and shipping ledger; crop and livestock grid; a villager affection board showing hearts
for the whole cast. Four views, all of them slow, none of them wasted on a fast-refresh
panel. This is arguably a better fit for the hardware than Pokemon is.

**Effort: medium-high**, gated on the same GBA emulator work as FireRed. Risk: the two-stage
`+0x2834` offset, and a smaller community than pret so fewer people to check your work
against.

### 7. Dragon Warrior IV (NES)

**Emulator and memory.** jsnes, already shipped in `www/vendor/nes.js`. `nes.cpu.mem` is a
`Uint8Array(0x10000)` covering the whole 6502 map, so `mem.slice(0, 0x800)` is the 2 KB of
internal RAM and `mem.slice(0x6000, 0x8000)` is the 8 KB of battery SRAM, in the same array
(verified in `src/cpu.js` and `src/mappers/mapper0.js`). Both together are 10 KB, barely more
than the GB path already moves. The NES lane in `app.js` currently has no `workRam()` method
at all; adding one is a handful of lines.

**RAM map.** The [Data Crystal map](https://datacrystal.tcrf.net/wiki/Dragon_Warrior_IV:RAM_map)
is excellent, roughly 90 fields, with nearly all party state in SRAM. Map and sub-map `$0063`
and `$0064`. Per-character 30-byte blocks from `$6001` for eight party members plus two
companion slots: HP `$6002`-`$6003`, MP `$6004`-`$6005`, level `$6006`, strength `$6007`,
agility `$6008`, max HP `$600D`-`$600E`, max MP `$600F`-`$6010`, eight item slots
`$6014`-`$601B`. Gold `$6157`-`$6159`, chapter number `$615A`, return-spell locations
`$6165`-`$6167`, treasure chest flags `$625D`-`$6277`.

**Disassembly.** [TheAnsarya/dragon-warrior-4-info](https://github.com/TheAnsarya/dragon-warrior-4-info),
pushed 2026-02-05. This is the only repo found in the NES cluster with a working asset
extraction pipeline: sprites, tilesets and palettes come out as PNG and JSON, and it also
extracts 50 monsters, 100 items, 50 spells and 180 shops as structured data, which is a
ready-made source for name tables. It carries only 2 stars, so treat it as low-community-
validation and verify its output yourself rather than trusting it the way you would trust
pret.

**Dashboard.** Eight-portrait party roster with HP and MP bars and levels; chapter, location
and gold header (DW4's chapter structure is a natural progress spine); per-character
inventory and equipment; a quest board driven by the chest and event flag bytes. Very high
density, and the eight-character party fills a 680x920 portrait panel better than a
six-Pokemon party does.

**Effort: medium.** Risk: DW4 uses MMC5, and the mapper banks SRAM. Confirm that jsnes's
MMC5 implementation keeps the window you want visible at `$6000` rather than rotating it,
before committing to SRAM-resident addresses.

### 8. Final Fantasy (NES)

**RAM map.** The [Data Crystal map](https://datacrystal.tcrf.net/wiki/Final_Fantasy/RAM_map)
is dense, 200 or more fields. Party block `$6100`-`$61FF` in SRAM, magic `$6300`-`$63FF`,
overworld position `$0027`-`$002A`. Two real gaps: gold is not documented and the inventory
is not broken out beyond equipped slots. Both are short reverse-engineering tasks with a
save-state diff, not research projects.

**Disassembly.** [Entroper/FF1Disassembly](https://github.com/Entroper/FF1Disassembly),
fully commented and reassemblable, but explicitly not organised for asset extraction:
graphics are `INCBIN` blobs with inconsistent naming. Forks exist at
[BenWenger/FinalFantasyDisassembly](https://github.com/BenWenger/FinalFantasyDisassembly)
and [JiggeryPonkery/FF1-MMC5](https://github.com/JiggeryPonkery/FF1-MMC5).

**Dashboard.** Four-character party with HP, MP and class sprites; spell slots by level,
which is a genuinely unusual and information-dense grid; overworld position and airship or
canoe state; gold and inventory once those addresses are found.

**Effort: medium.** Risk: manual CHR extraction because the disassembly will not hand you
tiles, plus the two undocumented fields.

### 9. Zelda II: The Adventure of Link (NES)

**RAM map.** The densest NES map found, roughly 180 fields, all in plain WRAM, so a 2 KB
POST covers everything. Position `$004D` and `$0029`, map page `$003B`, scene index `$0561`,
magic `$0773`, life `$0774`, attack, magic and life levels `$0777`-`$0779`, magic and heart
containers `$0783` and `$0784`, experience `$0775`-`$0776`, eight spell flags
`$077B`-`$0782`, eight item flags `$0785`-`$078C`, crystals remaining `$0794`, keys `$0793`,
deaths `$079F`.

**Disassembly.** None found. This is the blocker. Assets would have to be ripped from the
ROM with community tooling rather than generated from source, which cuts against the
project's rule that the repo ships only a generator.

**Dashboard.** Experience and the three level tracks as bars; the eight-spell and eight-item
unlock grid, which is a perfect e-ink checklist; palace and crystal progress; overworld
position. Genuinely good, and the cheapest POST in the survey at 2 KB.

**Effort: medium-high** entirely because of assets. The Legend of Zelda (NES) is a close
sibling: an 80-field [RAM map](https://datacrystal.tcrf.net/wiki/The_Legend_of_Zelda/RAM_map)
with hearts `$066F`/`$0670`, rupees `$066D`, keys `$066E`, bombs `$0658`, an item block at
`$0658`-`$0676`, triforce `$0671`, and screen flags `$067F`-`$07FE`, plus a code-only
disassembly at [aldonunez/zelda1-disassembly](https://github.com/aldonunez/zelda1-disassembly)
that also ships no graphics.

### 10. Zelda: Oracle of Ages/Seasons (GBC)

**Disassembly.** [Stewmath/oracles-disasm](https://github.com/Stewmath/oracles-disasm), 213
stars, pushed 2026-08-11 (the repo moved from Drenn1 to Stewmath). Ages builds byte-matching;
Seasons is functionally correct but not byte-identical due to a padding quirk. Critically,
graphics are already extracted as 4-colour indexed PNGs, which is a better asset story than
LADX.

**RAM map.** None. No Data Crystal page exists for either game and the repo has no RAM map
file; its README flags scattered hardcoded RAM addresses as unfinished work. Every field has
to be derived from WRAM symbol names in the source.

**Dashboard.** Health, rupees and the ring box; the seasons or ages mechanic as its own
widget, which is the most distinctive state in any Zelda; essence tracker; dungeon keys.

**Effort: high**, entirely because of the missing RAM map. This is a read-the-source project
rather than a read-the-wiki project. Good assets, no addresses.

### Blocked on emulator: Super Metroid (SNES) and Sonic 1 (Genesis)

Both would be strong candidates on game merit and both have good documentation. They are
blocked because no browser SNES or Genesis core exposes RAM to page JavaScript today.

Super Metroid: [Data Crystal map](https://datacrystal.tcrf.net/wiki/Super_Metroid/RAM_map)
gives X `$7E0AF6`, Y `$7E0AFA`, room X/Y `$7E0B12`/`$7E0B16`, energy `$7E09C2`, missiles
`$7E09C6`, super missiles `$7E09CA`, power bombs `$7E09CE`. Disassembly at
[InsaneFirebat/sm_disassembly](https://github.com/InsaneFirebat/sm_disassembly) (pushed
2026-09-10). A dashboard would show energy and ammo, the item collection grid, room and map
position, and a run timer.

Sonic 1: [Data Crystal map](https://datacrystal.tcrf.net/wiki/Sonic_the_Hedgehog:RAM_map)
gives player X `$FFF700`, Y `$FFF704`, rings `$FFFE20`, score `$FFFE26`, lives `$FFFE12`,
timer `$FFFE23`/`$FFFE24`, zone and act `$FFFE10`-`$FFFE11`, and an object status table at
`$FFD000` with 0x40 bytes per entry. Disassembly at
[sonicretro/s1disasm](https://github.com/sonicretro/s1disasm). It is a fast-action platformer
though, so the dashboard surface is thin regardless of the emulator problem.

Neither is recommended until the emulator lane is solved, and solving it for a platformer is
poor value.

### Checked and rejected

Worth recording so nobody re-researches them.

- **Chrono Trigger.** A [RAM map exists](https://datacrystal.tcrf.net/wiki/Chrono_Trigger_(SNES)/RAM_map)
  but no disassembly project does, only the Temporal Flux event editor. The obvious
  assumption that a game this popular must have a decomp turns out to be false.
- **Final Fantasy Tactics Advance.** No decomp. The
  [RAM map](https://datacrystal.tcrf.net/wiki/Final_Fantasy_Tactics_Advance:RAM_map) documents
  only in-game date and a mission item table, no party stats, gil or clan roster.
- **Kirby: Nightmare in Dream Land.** No disassembly or decomp exists at all.
- **Golden Sun.** Three competing projects ([gsret/goldensun](https://github.com/gsret/goldensun),
  [Coaltergeist/goldensun-decomp](https://github.com/Coaltergeist/goldensun-decomp),
  [wowwheaties/alchemy](https://github.com/wowwheaties/alchemy)), none mature enough for
  reliable address extraction. Revisit later; the game itself would be an excellent fit.
- **Fire Emblem: The Sacred Stones** ([FireEmblemUniverse/fireemblem8u](https://github.com/FireEmblemUniverse/fireemblem8u))
  and **Advance Wars** ([ketsuban/advancewars](https://github.com/ketsuban/advancewars),
  archived 2020, fork at [pizdex/advancewars](https://github.com/pizdex/advancewars)). Real
  byte-identical decomps and ideal turn-based subject matter, but no community RAM map for
  either, so all address discovery is from source. Strong backlog candidates.
- **Phantasy Star II/IV, Shining Force I/II, Landstalker, Herzog Zwei, Story of Thor.** No
  usable RAM maps and no disassemblies. The Genesis RPG catalogue is essentially undocumented
  compared to the Nintendo side.
- **Might and Magic (NES).** Character data is known to live at `$6F00`-`$76FF` but the
  per-character byte offsets inside that block are not documented anywhere, and no
  disassembly exists.
- **Platformers generally** (Super Mario Land, Wario Land, Tetris, Kirby's Dream Land, Donkey
  Kong Land, Super Mario Bros, Castlevania, Mega Man, Sonic Advance, Metroid Fusion and Zero
  Mission). Many have good RAM maps and several have good disassemblies, but their state is
  lives, score, timer and current power-up: shallow, fast-changing, and the opposite of what
  a 0.5 s partial refresh wants. Deprioritised by design, not by documentation.
- **Pokemon Trading Card Game** ([pret/poketcg](https://github.com/pret/poketcg)). Low
  technical effort thanks to pret tooling, but there is nothing to display outside an active
  duel, so the ambient-dashboard premise fails.

---

## Emulator memory access by system

This is the gating constraint for anything beyond GB, GBC and NES. Repository activity below
was checked against the GitHub API on 2026-09-13.

### NES: solved

**jsnes** ([bfirsh/jsnes](https://github.com/bfirsh/jsnes), 6417 stars, pushed 2026-09-08).
`nes.cpu.mem` is a `Uint8Array(0x10000)` allocated in `src/cpu.js` covering the entire 6502
address space, not just the 2 KB of internal RAM. Battery SRAM is not a separate array:
`src/mappers/mapper0.js` reads and writes `$6000` and above straight through
`this.nes.cpu.mem[address]`, and `loadBatteryRam()` copies 0x2000 bytes into `cpu.mem` at
offset `0x6000`. So `nes.cpu.mem.slice(0, 0x800)` is work RAM and
`nes.cpu.mem.slice(0x6000, 0x8000)` is cart SRAM, 10 KB for both. Higher mappers layer
banking on top but still write through the same array, which is the caveat behind the MMC5
risk noted for Dragon Warrior IV.

The reader's NES lane currently has no `workRam()` method; adding one mirrors the existing
`GbCore.prototype.workRam`.

### Game Boy and Game Boy Color: solved, but consider migrating cores

**WasmBoy** ([torch2424/wasmboy](https://github.com/torch2424/wasmboy)) is what ships today.
`core/constants.ts` confirms `WORK_RAM_LOCATION = 0x004800` and `WORK_RAM_SIZE = 0x8000`, so
the full 32 KB of GBC banked WRAM is already flat and contiguous with banking resolved by the
core. Going from DMG to GBC is a constant change in `www/app.js`, nothing more.
`_getWasmConstant(name)` and `_getWasmMemorySection(start, end)` are both documented in the
project wiki as debug-only and explicitly not covered by the stability guarantee.

Two cautions. First, WasmBoy was last pushed 2023-03-04, over three years stale. It is not
archived and it works, but expect no fixes. Second, the constant
`WASMBOY_INTERNAL_MEMORY_LOCATION` does not appear in `core/constants.ts`; the real names are
`GAMEBOY_INTERNAL_MEMORY_LOCATION` and `WASMBOY_STATE_LOCATION`. Any code or documentation
referring to the former is using a stale alias.

**binjgb** ([binji/binjgb](https://github.com/binji/binjgb), 606 stars, pushed 2026-07-04) is
the maintained alternative and its memory API is better. `src/emscripten/exported.json`
exports `_emulator_get_wram_ptr`, `_emulator_get_hram_ptr`, `_emulator_read_mem`,
`_emulator_write_mem`, `_emulator_read_ext_ram` and `_emulator_write_ext_ram`. `src/emulator.c`
defines `typedef struct { u8 data[WORK_RAM_SIZE]; Address offset; u8 bank; } Wram;` with
`WORK_RAM_SIZE` as 32 KB, so like WasmBoy it keeps all banks resident and flat. Read it as
`Module.HEAPU8` sliced at `Module._emulator_get_wram_ptr(emulatorPtr)`.

Recommendation: stay on WasmBoy for now since it works and is wired up, but treat binjgb as
the migration target if WasmBoy's staleness ever bites. Both expose the same 32 KB shape, so
the decoder does not care which one is underneath.

Dead ends: GameBoy-Online (taisel) last pushed 2019 and is pure JS; Emulicious is a desktop
Java tool and not browser-embeddable at all.

### GBA: possible, but requires emulator work

This is the correction that matters most. mGBA's documented
`core.memory["wram"].readRange(offset, length)` API, with `read8`/`read16`/`read32` and named
domains, is the **desktop Lua scripting interface** at https://mgba.io/docs/dev/scripting.html.
It is not the wasm binding and it is not reachable from page JavaScript.

Verified directly: the shipped TypeScript definitions at
[`src/platform/wasm/mgba.d.ts`](https://github.com/thenick775/mgba/blob/feature/wasm/src/platform/wasm/mgba.d.ts)
(620 lines) declare game loading, save states, audio, video, input and `Module.FS` file
operations, and contain no `busRead`, no memory domain accessor and no raw read of any kind.
The closest thing is `getSave(): Uint8Array | null`, which returns cartridge save data, not
live RAM. The build's own `CMakeLists.txt` sets
`EXPORTED_RUNTIME_METHODS=["FS","cwrap","addFunction","removeFunction"]`, so `HEAPU8` is not
even exported by name, though `cwrap` is available for reaching a C symbol that has been kept
alive.

That leaves two honest routes:

1. **Patch mGBA wasm.** Add an `EMSCRIPTEN_KEEPALIVE` function returning a pointer to EWRAM
   and IWRAM, add `HEAPU8` to `EXPORTED_RUNTIME_METHODS`, rebuild. Bounded C and Emscripten
   build work, not research. The upstream core is
   [mgba-emu/mgba](https://github.com/mgba-emu/mgba) and the actively maintained wasm fork is
   [thenick775/mgba](https://github.com/thenick775/mgba) (pushed 2026-08-29), packaged as
   `@thenick775/mgba-wasm`. [gbajs3](https://github.com/thenick775/gbajs3) is the full browser
   frontend built on it and is the most actively developed GBA browser project found.
2. **Use gbajs2 in pure-JS mode.** [andychase/gbajs2](https://github.com/andychase/gbajs2)
   (pushed 2026-02-25) keeps memory in plain JS: `js/mmu.js` defines
   `this.memory[this.REGION_WORKING_RAM]` (256 KB EWRAM, `SIZE_WORKING_RAM = 0x00040000`) and
   `this.memory[this.REGION_WORKING_IRAM]` (32 KB IWRAM, `SIZE_WORKING_IRAM = 0x00008000`),
   each a `MemoryBlock` wrapping a `DataView` with a `.buffer`. Read with
   `new Uint8Array(mmu.memory[mmu.REGION_WORKING_RAM].buffer)`. No patching required, but a
   pure-JS GBA core is slower and less accurate than mGBA. gbajs3 can run this same core, so
   gbajs3 in JS-core mode is the zero-patch option.

**Payload.** Full EWRAM plus IWRAM is 288 KB. At one POST per second that is not viable, and
the reader's own configuration makes it worse: `platformio.ini` sets `HTTP_RAW_BUFLEN=1`, so
raw POST bodies are read one byte at a time (a deliberate workaround for a 5 s stall in
`WebServer::readBytes`), and `GAMES_WRAM_BYTES` is a fixed 8192-byte static buffer in
`include/config.h` with no PSRAM enabled in the build. Partial dumps are mandatory, and they
are also sufficient: the Gen 3 curated set is 1 to 2 KB, smaller than what already works.

### SNES and Genesis: not practical today

Be blunt about this one. There is no maintained browser SNES or Genesis core that exposes
work RAM to page JavaScript through a documented API.

**EmulatorJS** ([EmulatorJS/EmulatorJS](https://github.com/EmulatorJS/EmulatorJS), pushed
2026-08-07) is the only realistically embeddable option for both systems, shipping snes9x and
bsnes for SNES and Genesis Plus GX for Mega Drive. Its `data/src/GameManager.js` was fetched
and read: the `cwrap` table binds `restart`, `loadState`, `screenshot`, `simulateInput`,
`toggleMainLoop`, `getCoreOptions`, `setVariable`, `setCheat`, `resetCheat`, `toggleShader`,
disk management, save paths, `supportsStates`, fast-forward and rewind controls, `getFrameNum`
and video and input configuration. There is **no `getMemoryData`, no bound
`retro_get_memory_data`, and no `HEAPU8` export**. The premise that
`EJS_emulator.gameManager.getMemoryData()` exists is incorrect as of the current `main`
branch. Its cheat system works through libretro's cheat-code mechanism
(`Module.cwrap("set_cheat", ...)`), which patches addresses rather than returning a buffer,
and `getState()` returns a save-state blob rather than live RAM (and per
[issue #793](https://github.com/EmulatorJS/EmulatorJS/issues/793) that path is currently
partly broken).

The libretro interface underneath does support exactly what is wanted:
`retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM)` and `retro_get_memory_size` are standard
entry points implemented by both snes9x and Genesis Plus GX, and are how RetroAchievements
does live memory watching in native RetroArch (https://docs.libretro.com/guides/memorymonitoring/).
EmulatorJS's JavaScript wrapper simply never binds them. Getting one into page JS means
patching the wrapper and confirming the symbol survives in each compiled core, which is
fragile across EmulatorJS core updates.

**jgenesis** ([jsgroth/jgenesis](https://github.com/jsgroth/jgenesis), pushed 2026-09-12) is
the more promising long-term option: a from-scratch, actively developed Rust emulator for
Genesis, Sega CD, SNES, Master System and Game Gear with an official wasm target. No
JS-facing memory API is documented, but adding a `#[wasm_bindgen]` memory export to a modern
Rust codebase is cleaner work than patching decades-old C cores.

Other candidates checked and found not viable: standalone snes9x Emscripten ports are
unmaintained hobby forks; [angelo-wf/LakeSnes](https://github.com/angelo-wf/LakeSnes) is
designed to be Emscripten-compilable but the web build is aspirational;
[h1romas4/wasm-genplus](https://github.com/h1romas4/wasm-genplus) exists but documents no
memory hook; BlastEm is desktop-only with no maintained wasm build.

**Payload** would also be awkward: 128 KB for SNES WRAM and 64 KB for Genesis 68K RAM, both
well above what the reader's 8 KB static buffer and byte-at-a-time raw reader handle today.
Curated sub-ranges would be mandatory even after the emulator problem is solved.

Verdict: treat SNES and Genesis as a research project, not a feature. Do not promise them.

### Throughput and payload shaping

No benchmark specific to ESPAsyncWebServer was found, but two real-world data points agree on
the order of magnitude. A developer measuring roughly 500 KB JPEG HTTP POST uploads on an
ESP32-S3 reported about 550 Kbps, roughly 69 KB/s, unchanged by switching between
`esp_http_client` and Arduino `HTTPClient` (https://esp32.com/viewtopic.php?t=37777). An
iperf comparison found LWIP-based ESP32-S3 WiFi averaging about 9.2 Mbps under ideal
saturated conditions but with wide variance
(https://www.hackster.io/tjr0927/esp32-s3-wiznet-w5500-toe-vs-lwip-iperf-performance-test-274510).

Plan for 8 to 64 KB/s sustained for a real per-second POST-and-parse cycle, not the datasheet
figure. The 8 KB GB path sits comfortably inside that; 32 KB GBC is fine; 64 KB or more per
second is not, especially given `HTTP_RAW_BUFLEN=1`.

Four levers, cheapest first:

1. **Send less.** Post only the sub-ranges the decoder reads. For Gen 3 this is the
   difference between 288 KB and 1.5 KB. This alone solves the problem for every candidate.
2. **Debounce on change.** Already implemented: `uploadWram()` in `www/app.js` hashes the
   buffer and skips the POST when the hash is unchanged, which makes a paused game free.
   Keep this and it becomes more valuable, not less, as buffers grow.
3. **Delta or dirty-page diffing.** Compare against the previous snapshot in page JS and send
   changed 256-byte pages plus a bitmask. Work RAM is mostly static between ticks, so this
   typically cuts an order of magnitude for a few lines of JavaScript.
4. **Compression.** RLE is cheap and effective on mostly-zero work RAM. `CompressionStream`
   ("gzip", "deflate", "deflate-raw") is browser-native and widely available since May 2023
   (https://developer.mozilla.org/en-US/docs/Web/API/CompressionStream), but the reader would
   need an inflate on the firmware side. Only worth it if levers 1 through 3 are exhausted,
   which for these candidates they are not.

If a 32 KB GBC buffer is adopted, `GAMES_WRAM_BYTES` in `include/config.h` and the bounds
check in `handleWramData()` in `src/games.cpp` both need raising, and the 32 KB static
allocation should be weighed against enabling PSRAM on the N8R2 module.

---

## Recommended next three

**1. Pokemon Crystal or Pokemon Gold/Silver.** Pick one; Crystal has the bigger audience and
the better-known disassembly, Gold/Silver has the better RAM map. Either way this is the
highest value per hour available. The emulator lane needs one constant changed to take the
full 32 KB WasmBoy already exposes, the asset generator is close to a copy of
`tools/pokered_pack.py` against a repo with identical conventions, and the payoff is a
second full-fat dashboard plus a real-time clock widget the reader has never had. It also
forces the decoder to become table-driven per game, which is the refactor every subsequent
candidate needs anyway. Do Yellow in the same session as a freebie once that table exists:
it is the same 8 KB, the same generator, and mostly the same addresses shifted by one.

**2. Zelda: Link's Awakening DX.** The first non-Pokemon dashboard, and the reason to do it
second is that it proves the platform is not a Pokemon accessory. It stays on the existing
8 KB GB path with no emulator work at all, the RAM map is thin but covers exactly what a
dashboard needs, and the explored-room grid from the `$D800`-`$D8FF` flags is the most
visually interesting view in the whole survey on a 1-bit portrait panel. Budget the extra
time for graphics extraction, since `zladx` ships no PNGs and that tooling is genuinely new
work rather than a port of the existing generator.

**3. The GBA lane, landing on Pokemon FireRed and then Harvest Moon: Friends of Mineral
Town.** Treat this as an emulator project first and a game project second. The deliverable
is a GBA core in the page that can hand JavaScript a pointer into EWRAM and IWRAM, either by
adding an `EMSCRIPTEN_KEEPALIVE` accessor to the mGBA wasm build or by adopting gbajs3 in
pure-JS mode and reading `mmu.memory[...].buffer`. Once that exists, FireRed is a low-effort
follow-on because the curated Gen 3 dump is only 1 to 2 KB and the decoder shape matches
`pokered`. Harvest Moon is the one to build straight after, because a farm calendar, a money
ledger and a villager affection board change on the scale of in-game days, which suits a
panel that takes half a second to refresh better than any Pokemon game does.

Explicitly not recommended: SNES and Genesis. The games are there and the RAM maps are there,
but no browser core exposes memory to the page, and the fix is a C patch to a libretro core
build that then has to be maintained against upstream. Revisit if jgenesis grows a memory
export, since that is the one project where the work would be clean.
