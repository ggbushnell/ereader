# Pokered asset inventory, 2026-09-12

Source checkout: `/Users/Shared/Documents/Muon_Decider/third_party/pokered`
(pret/pokered disassembly, gitignored, referenced from `docs/pokemon-aux-display-plan-2026-09-12.md`)

All paths below are relative to that checkout root unless given absolute. This is a
read-only inventory: no files in the checkout or the repo were modified while producing it.

Two corrections to the plan doc's WRAM sketch turned up during this research and are
called out inline and again in the summary: `wMissableObjectFlags` does not exist in this
checkout (it is a Gen 2 name), and the battle struct is 29 bytes, not 28. Both are detailed
in section 8.

---

## 1. Font, text box, cursor glyphs

### Font graphics (`gfx/font/`)

| File | Pixel size | Depth | Tiles | Used for |
|---|---|---|---|---|
| `gfx/font/font.png` | 128 x 64 | 1 bpp | 128 (16 cols x 8 rows, row-major) | `FontGraphics`, the main A-Z/a-z/digit/punctuation font |
| `gfx/font/font_extra.png` | 128 x 16 | 2 bpp (4 shades) | 32 (16 x 2) | `TextBoxGraphics`, text box border and extra punctuation |
| `gfx/font/font_battle_extra.png` | 120 x 16 | 2 bpp | 30 (15 x 2) | `HpBarAndStatusGraphics`, HP bar and battle status glyphs |
| `gfx/font/AB.png` | 8 x 16 | 2 bpp | 2 | `ABTiles`, the A/B button glyph pair |
| `gfx/font/ED.png` | 8 x 8 | 1 bpp | 1 | `<ED>` glyph, naming screen |
| `gfx/font/P.png` | 8 x 8 | 1 bpp | 1 | `<BOLD_P>` glyph, stat screen |

Declared in `gfx/font.asm`:
```
FontGraphics:: INCBIN "gfx/font/font.1bpp"
FontGraphicsEnd::
ABTiles: INCBIN "gfx/font/AB.2bpp"
HpBarAndStatusGraphics:: INCBIN "gfx/font/font_battle_extra.2bpp"
TextBoxGraphics:: INCBIN "gfx/font/font_extra.2bpp"
TextBoxGraphicsEnd::
```
PNG bit depths were confirmed directly against the PNG IHDR chunk (bit depth byte), not
inferred: font.png is bit depth 1, the rest listed 2 bpp are bit depth 2 (grayscale
colortype 0, meaning the raw pixel value 0-3 is the shade index, "up to 4 shades" per
CLAUDE.md's description of pokered PNGs).

### Charmap (`constants/charmap.asm`)

Gen 1 text encoding, byte to character, quoted from the file:

Control bytes:
```
charmap "<NULL>",    $00
charmap "<NEXT>",    $4e   ; line break, new textbox line
charmap "<LINE>",    $4f   ; line break, same textbox
charmap "@",         $50   ; string terminator
charmap "<CONT>",    $55
```

$60-$7F range, glyphs from `font_extra.png` (text box borders and punctuation), overridden
in battle context by `font_battle_extra.png`:
```
charmap "<COLON>",   $6d
charmap "<LV>",      $6e   ; battle-context override
charmap "<to>",      $70   ; battle-context override
charmap "<ID>",      $73   ; battle-context override
charmap "No",        $74   ; battle-context override
charmap "┌",         $79
charmap "─",         $7a
charmap "┐",         $7b
charmap "│",         $7c
charmap "└",         $7d
charmap "┘",         $7e
charmap " ",         $7f
```

$80-$FF range, main alphanumeric font (`font.png`):
```
charmap "A",         $80
...
charmap "Z",         $99
charmap "(",         $9a
charmap ")",         $9b
charmap ":",         $9c
charmap ";",         $9d
charmap "[",         $9e
charmap "]",         $9f
charmap "a",         $a0
...
charmap "z",         $b9
charmap "'",         $e0
charmap "-",         $e3
charmap "?",         $e6
charmap "!",         $e7
charmap ".",         $e8
charmap "▷",         $ec   ; unfilled cursor arrow
charmap "▶",         $ed   ; filled cursor arrow (menu selection)
charmap "▼",         $ee   ; blinking scroll-down arrow
charmap "♂",         $ef
charmap "¥",         $f0
charmap "×",         $f1
charmap "/",         $f3
charmap ",",         $f4
charmap "♀",         $f5
charmap "0",         $f6
...
charmap "9",         $ff
```
Space is $7F, string terminator is `@` = $50, line breaks are `<NEXT>` = $4E and `<LINE>` = $4F.

### Charmap byte to font tile index

There is no separate lookup table: the charmap byte itself is the Game Boy tile id.
`vFont` (128 tiles) is mapped at the signed tile-data block $8800-$8FFF, addressed by tile
numbers $80-$FF directly, and `home/load_font.asm` blits `FontGraphics` straight into it:
```
LoadFontTilePatterns::
	ld hl, FontGraphics
	ld de, vFont
	ld bc, FontGraphicsEnd - FontGraphics
```
`PlaceNextChar` (`home/text.asm`) writes the raw character byte into the tilemap unmodified:
```
PlaceNextChar::
	ld a, [de]
	cp '@'
	jr nz, .NotTerminator
	...
	ld [hli], a
```
So font tile index = `byte - $80` (0-127): tile 0 = 'A', tile $19 = 'Z', tiles 108/109/110
= '▷'/'▶'/'▼'. The $60-$7F border/punctuation range works the same way against
`TextBoxGraphics`, loaded at `vChars2 tile $60`, so border tile index = `byte - $60`.

### Text box border tiles

`TextBoxBorder::` in `home/text.asm` draws the border using the charmap glyphs directly:
```
TextBoxBorder::
	push hl
	ld a, '┌'
	ld [hli], a
	inc a ; "─"
	call .PlaceChars
	inc a ; "┐"
	ld [hl], a
```
All sourced from `font_extra.png` (128 x 16, 2bpp, 32 tiles): corners are `┌` (tile 25,
top-left), `┐` (tile 27, top-right), `└` (tile 29, bottom-left), `┘` (tile 30,
bottom-right); edge fill is `─` (tile 26, top/bottom) and `│` (tile 28, sides); interior
fill is space (tile 31).

### Cursor / arrow glyphs

Also in the main `font.png` sheet, not a separate file: `▷` = $EC (tile 108, secondary
"unfilled" cursor), `▶` = $ED (tile 109, the active menu-selection arrow), `▼` = $EE (tile
110, blinking scroll indicator). Drawn in `home/window.asm`:
```
PlaceMenuCursor::
	ld a, '▶'
	ld [hl], a
PlaceUnfilledArrowMenuCursor::
	ld [hl], '▷'
EraseMenuCursor::
	ld [hl], ' '
HandleDownArrowBlinkTiming::
	ld a, '▼'
```

---

## 2. Town map

### Tile graphics

`gfx/town_map/town_map.png`: 32 x 32 px, 2 bpp indexed (4 shades), confirmed by IHDR
(bit depth 2, colortype 0). At 8x8 px per tile that is a 4x4 grid, 16 tiles, loaded at
`vChars2 tile $60` in `LoadTownMap` (`engine/items/town_map.asm`).

### Compressed 20x18 layout

Data file: `gfx/town_map/town_map.rle`, 171 bytes, included as:
```
CompressedMap: INCBIN "gfx/town_map/town_map.rle"
```
Decoder, `LoadTownMap` in `engine/items/town_map.asm`:
```
	hlcoord 0, 0
	ld de, CompressedMap
.nextTile
	ld a, [de]
	and a
	jr z, .done
	ld b, a
	and $f
	ld c, a          ; c = run length (low nibble)
	ld a, b
	swap a
	and $f
	add $60          ; a = tile id (high nibble + $60)
.writeRunLoop
	ld [hli], a
	dec c
	jr nz, .writeRunLoop
	inc de
	jr .nextTile
.done
```
Encoding: each byte packs two nibbles. High nibble is a tile-index offset added to $60
(the base tile of the town map tileset in VRAM); low nibble is a run length, 1-15. The
decoder writes that tile `run length` times into consecutive destination bytes (starting
at the top-left of a linear 20x18 = 360 tile background buffer, `hl` auto-increments
across the whole area with no explicit width tracking needed) then moves to the next
source byte. A `$00` byte terminates the stream. So it is a nibble-pair RLE, not a
byte-pair RLE: 171 encoded bytes expand to 360 output tiles.

### Player cursor sprite

`gfx/town_map/town_map_cursor.png`: 16 x 16 px, 1 bpp. Compiled to `.1bpp` and loaded as
`TownMapCursor` at sprite tile index `BIRD_BASE_TILE` = $04 in `vSprites` (same slot is
reused for the Fly bird sprite and for `gfx/town_map/mon_nest_icon.png`). Positioned via
`TownMapCoordsToOAMCoords` from the map's packed x/y nibble coordinate.

### `data/maps/town_map_entries.asm`

Two tables. Outdoor table `ExternalMapEntries`, 3 bytes per entry, indexed directly by
map id (`ExternalMapEntries + map_id * 3`):
```
MACRO outdoor_map
	dn \2, \1
	dw \3
ENDM
outdoor_map  2, 11, PalletTownName
```
`dn \2, \1` packs y (high nibble) and x (low nibble) into one byte; the following `dw` is
a pointer to the `@`-terminated location name string.

Indoor table `InternalMapEntries`, 4 bytes per entry:
```
MACRO indoor_map
	db INDOORGROUP_\1
	dn \3, \2
	dw \4
ENDM
indoor_map PALLET_TOWN,         2, 11, PalletTownName
```
Byte 0 is an `INDOORGROUP_*` threshold constant (see below), byte 1 is the packed y/x
coordinate, bytes 2-3 are the name pointer. Table ends with sentinel `db -1`.

### Indoor group indexing

`constants/map_constants.asm` defines the group boundary mechanism:
```
DEF NUM_INDOOR_MAP_GROUPS EQU 0
MACRO end_indoor_group
	DEF INDOORGROUP_\1 EQU const_value
	REDEF NUM_INDOOR_MAP_GROUPS EQU NUM_INDOOR_MAP_GROUPS + 1
ENDM
```
Because map ids are assigned sequentially by `map_const`, calling `end_indoor_group NAME`
right after a run of indoor maps captures `const_value` as one past the last map id in
that group, for example:
```
DEF FIRST_INDOOR_MAP EQU const_value
	map_const REDS_HOUSE_1F,                  4,  4 ; $25
	map_const REDS_HOUSE_2F,                  4,  4 ; $26
	map_const BLUES_HOUSE,                    4,  4 ; $27
	map_const OAKS_LAB,                       5,  6 ; $28
	end_indoor_group PALLET_TOWN
```
so `INDOORGROUP_PALLET_TOWN` = $29 (`VIRIDIAN_POKECENTER`'s id, the first map of the next
group). The lookup, `LoadTownMapEntry` in `engine/items/town_map.asm`:
```
LoadTownMapEntry:
	cp FIRST_INDOOR_MAP
	jr c, .external
	ld bc, 4
	ld hl, InternalMapEntries
.loop
	cp [hl]
	jr c, .foundEntry
	add hl, bc
	jr .loop
```
If the map id is below `FIRST_INDOOR_MAP` it is outdoor and indexed directly into
`ExternalMapEntries`. Otherwise the code linearly scans `InternalMapEntries` 4 bytes at a
time until it finds the first record whose `INDOORGROUP_*` threshold byte is greater than
the map id, i.e. it walks the group-boundary list to find which shared coordinate/name
entry covers this indoor map.

---

## 3. Overworld tilesets and maps

### Tile graphics (`gfx/tilesets/*.png`)

19 files, `gfx/blocksets` holds the matching `.bst` files separately (see below, not under
`data/tilesets`). All tileset PNGs are 128 px wide (16 tile columns), 2 bpp indexed
(4 shades), confirmed by IHDR. Height varies: most are 48 px (6 rows, e.g. `overworld.png`,
`forest.png`), some are 40 px (5 rows: `cavern.png`, `club.png`, `plateau.png`,
`reds_house.png`), `underground.png` is 16 px (2 rows). Raw uncompressed 2bpp size across
all 19 files totals 27,136 bytes.

### Blocksets (`gfx/blocksets/*.bst`, not `data/tilesets/*.bst`)

19 files, 22,960 bytes total. `gfx/blocksets/overworld.bst` is exactly 2048 bytes.
Block size is confirmed as 4x4 tiles (16 tiles = 32x32 px per block) by
`constants/gfx_constants.asm`:
```
DEF BLOCK_WIDTH EQU 4 ; tiles
DEF BLOCK_HEIGHT EQU BLOCK_WIDTH ; tiles
```
and by the block-drawing routine `DrawTileBlock` in `home/overworld.asm`:
```
; bc = tile block ID * 0x10
	ld c, BLOCK_HEIGHT ; 4 loop iterations
.loop
	push bc
REPT BLOCK_WIDTH - 1
	ld a, [de]
	ld [hli], a
	inc de
ENDR
```
2048 / 16 = 128 blocks in `overworld.bst`. So a `.bst` file is a flat array of blocks,
each block 16 bytes, each byte a tile index into that tileset's graphics sheet, laid out
row-major within the 4x4 block.

### `data/tilesets/tileset_headers.asm`

```
MACRO tileset
	db BANK(\1_GFX)
	dw \1_Block, \1_GFX, \1_Coll
	db \2, \3, \4 ; counter tiles
	db \5         ; grass tile
	db \6         ; animations (TILEANIM_* value)
ENDM

Tilesets:
	table_width 12
	tileset Overworld,    -1, -1, -1, $52, TILEANIM_WATER_FLOWER
	tileset RedsHouse1,   -1, -1, -1,  -1, TILEANIM_NONE
```
12 bytes per entry: graphics bank (1B), blockset pointer (2B), tile graphics pointer
(2B), collision data pointer (2B), 3 counter-tile ids (3B), grass tile id (1B), animation
type (1B).

### `.blk` map files

228 files under `maps/*.blk`, 24,518 bytes total (average 107.5 bytes). One byte per
block position, row-major, `width` bytes per row where width is the map's width in
blocks. Confirmed exactly: `maps/PalletTown.blk` is 90 bytes and `PALLET_TOWN` is declared
`map_const PALLET_TOWN, 10, 9` (width 10, height 9 blocks), 10 x 9 = 90.

### `data/maps/headers/*.asm`

Example, `data/maps/headers/PalletTown.asm`:
```
	map_header PalletTown, PALLET_TOWN, OVERWORLD
	connection north, Route1, ROUTE_1, 0
	connection south, Route21, ROUTE_21, 0
	end_map_header
```
`map_header` (`macros/scripts/maps.asm`) expands to: tileset id (1B), height then width
in blocks (2B, `db height, width`), pointer to the `.blk` data (2B), text pointers table
(2B), script pointer (2B), a connection bitmask byte (bits for which of N/S/E/W have a
`connection`), followed by each `connection` macro's own data block (target map, window
offsets, computed from the neighboring map's own width/height). `end_map_header` appends
a final 2B pointer to the map's object data (`data/maps/objects/*.asm`).

### `data/maps/map_header_pointers.asm`

A flat `dw` table, one pointer per map id, in the same order as the map id constants,
indexed directly by map id:
```
MapHeaderPointers::
	table_width 2
	dw PalletTown_h
	dw ViridianCity_h
	...
	assert_table_length NUM_MAPS
```
A parallel table `data/maps/map_header_banks.asm` holds one bank byte per map id in the
same order (far-pointer bank for each header).

### `constants/map_constants.asm`, map id ordering and indoor/outdoor

Comment in the file: "Order: towns/cities, then routes, then indoor/dungeon maps." Cities
example:
```
	const_def
	map_const PALLET_TOWN,                   10,  9 ; $00
	map_const VIRIDIAN_CITY,                 20, 18 ; $01
	...
	map_const SAFFRON_CITY,                  20, 18 ; $0A
DEF NUM_CITY_MAPS EQU const_value
```
Indoor boundary:
```
	map_const ROUTE_25,                      30,  9 ; $24
DEF FIRST_INDOOR_MAP EQU const_value
	map_const REDS_HOUSE_1F,                  4,  4 ; $25
	map_const REDS_HOUSE_2F,                  4,  4 ; $26
	map_const BLUES_HOUSE,                    4,  4 ; $27
	map_const OAKS_LAB,                       5,  6 ; $28
	end_indoor_group PALLET_TOWN
```
A map id is indoor if and only if it is `>= FIRST_INDOOR_MAP` ($25). This exact
comparison is what `LoadTownMapEntry` uses (`cp FIRST_INDOOR_MAP` / `jr c, .external`).
There is no name-pattern check in code; it is purely the numeric boundary, though by
convention indoor maps are named for their building and grouped immediately after their
parent outdoor map, closed by `end_indoor_group <parent town>`.

---

## 4. Pokemon sprites and icons

### Front and back sprite PNGs

`gfx/pokemon/front/*.png`: 153 files, 612 KB on disk (PNG-compressed). 151 are real
species; the other two are `fossilaerodactyl.png` and `fossilkabutops.png` (museum fossil
pictures, not species). All are 2 bpp indexed (4 shades), confirmed by IHDR. Three fixed
sizes observed:

| Size | Tiles | Count |
|---|---|---|
| 40 x 40 | 5x5 | 48 |
| 48 x 48 | 6x6 | 49 |
| 56 x 56 | 7x7 | 56 |

`gfx/pokemon/back/*.png`: 151 files (species name + `b` suffix, e.g. `bulbasaurb.png`),
604 KB on disk, all fixed at 32 x 32 px (4x4 tiles), 2 bpp, regardless of the front sprite
size.

Raw (uncompressed 2bpp tile data, i.e. what a `.2bpp` build artifact would total)
sizes: front sprites 91,328 bytes total, back sprites 38,656 bytes total.

These PNGs are the uncompressed source art, not the ROM's runtime format. The build
pipeline (`Makefile`):
```
%.2bpp: %.png
	$(RGBGFX) --colors dmg $(RGBGFXFLAGS) -o $@ $<
%.pic: %.2bpp
	tools/pkmncompress $< $@
```
So PNG to `.2bpp` (raw Game Boy planar tiles, 16 bytes/tile) is a straight conversion;
`.2bpp` to `.pic` (`tools/pkmncompress.c`) applies Gray-code and RLE compression and is
the actual ROM-embedded format. For an asset pack that is not itself a Game Boy ROM, the
`.2bpp` stage (or the source PNG) is the right input, not `.pic`.

### Sprite dimension byte

There is no `sprite_dimensions` macro; base stats files pull byte 0 straight out of the
compiled `.pic` file via `INCBIN`, e.g. `data/pokemon/base_stats/bulbasaur.asm`:
```
	INCBIN "gfx/pokemon/front/bulbasaur.pic", 0, 1 ; sprite dimensions
	dw BulbasaurPicFront, BulbasaurPicBack
```
That byte is produced in `tools/pkmncompress.c`:
```c
output[0] = (width << 4) | width;
```
Width in 8x8 tiles is packed into both nibbles (sprites are always square), so:
Bulbasaur 40x40 (5 tiles) has dimension byte $55, Charizard 56x56 (7 tiles) has $77,
Rattata 40x40 (5 tiles) has $55.

### Icons (`gfx/icons/*.png`)

Not a single sheet: 4 separate files, one per "icon shape" category, each 8 x 32 px,
2 bpp, 1 tile wide x 4 tiles tall (two animation frames of 2 tiles each, stacked
vertically):
```
gfx/icons/bug.png        8x32
gfx/icons/plant.png      8x32
gfx/icons/quadruped.png  8x32
gfx/icons/snake.png      8x32
```
Confirmed in `engine/gfx/mon_icons.asm`:
```
DEF INC_FRAME_1 EQUS "0, $20"
DEF INC_FRAME_2 EQUS "$20, $20"
BugIconFrame1: INCBIN "gfx/icons/bug.2bpp", INC_FRAME_1
BugIconFrame2: INCBIN "gfx/icons/bug.2bpp", INC_FRAME_2
```
Other icon shapes (`ICON_MON`, `ICON_BALL`, `ICON_FAIRY`, `ICON_BIRD`, `ICON_WATER`,
`ICON_TRADEBUBBLE`) are pulled from unrelated general sprite sheets, not `gfx/icons/`.

### Species to icon mapping (`data/pokemon/menu_icons.asm`)

A nybble array, one 4-bit icon-shape value per species in internal index order, packed
2 species per byte (~76 bytes for 151 species):
```
MonPartyData:
	nybble_array
	nybble ICON_GRASS     ; Bulbasaur
	nybble ICON_GRASS     ; Ivysaur
	nybble ICON_GRASS     ; Venusaur
	nybble ICON_MON       ; Charmander
	...
	nybble ICON_QUADRUPED ; Rattata
```
Icon-shape constants (`constants/icon_constants.asm`): `ICON_MON` $0, `ICON_BALL` $1,
`ICON_HELIX` $2, `ICON_FAIRY` $3, `ICON_BIRD` $4, `ICON_WATER` $5, `ICON_BUG` $6,
`ICON_GRASS` $7, `ICON_SNAKE` $8, `ICON_QUADRUPED` $9, ... `ICON_TRADEBUBBLE` $e.

---

## 5. Data tables

| Table | File | Format | Record size | Count |
|---|---|---|---|---|
| Species names | `data/pokemon/names.asm` | `dname` macro, fixed-width, `@`-padded | 10 bytes | 151, internal index order (Rhydon = index 1), not dex order |
| Internal to dex map | `data/pokemon/dex_order.asm` | one `db DEX_*` per internal index | 1 byte | ~152 (includes MissingNo slot) |
| Base stats | `data/pokemon/base_stats/*.asm` | raw db/dw/INCBIN, no dedicated macro | 28 bytes | 151 files |
| Item names | `data/items/names.asm` | `li`/`list_start`, `@`-terminated, variable length | variable, max 12 | 97 |
| Move names | `data/moves/names.asm` | `li`/`list_start` | variable, max 12 | 165 |
| Move data | `data/moves/moves.asm` | `move` macro | 6 bytes (`MOVE_LENGTH`) | 165 |
| Type matchups | `data/types/type_matchups.asm` | `db attacker, defender, multiplier` | 3 bytes | 82 |
| Map names | `data/maps/names.asm` | flat labeled `@`-terminated strings | variable | 53 |

### Species names, `dname` macro (`macros/data.asm`)

```
MACRO? dname
	db \1
	ds n - CHARLEN(\1), '@'
ENDM
```
`NAME_LENGTH EQU 11` (`constants/text_constants.asm`), so each record is
`NAME_LENGTH - 1` = 10 bytes, `@`-padded. Example:
```
MonsterNames::
	table_width NAME_LENGTH - 1
	dname "RHYDON"
	dname "KANGASKHAN"
	dname "NIDORAN♂"
```

### Base stats, full example (`data/pokemon/base_stats/bulbasaur.asm`)

```
	db DEX_BULBASAUR ; pokedex id

	db  45,  49,  49,  45,  65
	;   hp  atk  def  spd  spc

	db GRASS, POISON ; type
	db 45 ; catch rate
	db 64 ; base exp

	INCBIN "gfx/pokemon/front/bulbasaur.pic", 0, 1 ; sprite dimensions

	dw BulbasaurPicFront, BulbasaurPicBack

	db TACKLE, GROWL, NO_MOVE, NO_MOVE ; level 1 learnset
	db GROWTH_MEDIUM_SLOW ; growth rate

	; tm/hm learnset
	tmhm SWORDS_DANCE, TOXIC,        BODY_SLAM,    TAKE_DOWN,    DOUBLE_EDGE,  \
	     RAGE,         MEGA_DRAIN,   SOLARBEAM,    MIMIC,        DOUBLE_TEAM,  \
	     REFLECT,      BIDE,         REST,         SUBSTITUTE,   CUT
	; end

	db 0 ; padding
```
Byte layout, in order: dex number (1), HP/Atk/Def/Spd/Spc (5), type1/type2 (2), catch
rate (1), base exp (1), front sprite dimension byte read from the compiled `.pic` (1),
front pic pointer (2), back pic pointer (2), level-1 moveset (4), growth rate (1), TM/HM
learnset bitfield (7 bytes, `ceil(55/8)` for `NUM_TM_HM` = 55 TMs and HMs), padding (1).
Total 28 bytes per species, matching `Species.asm`'s `table_width BASE_DATA_SIZE`
ordering (internal index order, Bulbasaur first, not dex order; `dex_order.asm` is the
internal-to-dex map).

### Move data (`data/moves/moves.asm`)

```
MACRO move
	db \1 ; animation (interchangeable with move id)
	db \2 ; effect
	db \3 ; power
	db \4 ; type
	db \5 percent ; accuracy
	db \6 ; pp
ENDM

Moves:
	table_width MOVE_LENGTH
	move POUND,        NO_ADDITIONAL_EFFECT,        40, NORMAL,       100, 35
	move KARATE_CHOP,  NO_ADDITIONAL_EFFECT,        50, NORMAL,       100, 25
```
`MOVE_LENGTH EQU 6` (`constants/battle_constants.asm`). Field order: animation/move id,
effect, power, type, accuracy, pp.

### `constants/type_constants.asm` (full, in order)

```
DEF PHYSICAL EQU const_value
	const NORMAL       ; $00
	const FIGHTING     ; $01
	const FLYING       ; $02
	const POISON       ; $03
	const GROUND       ; $04
	const ROCK         ; $05
	const BIRD         ; $06
	const BUG          ; $07
	const GHOST        ; $08

	const_next 20   ; unused physical/special gap, $09-$13

DEF SPECIAL EQU const_value
	const FIRE         ; $14
	const WATER        ; $15
	const GRASS        ; $16
	const ELECTRIC     ; $17
	const PSYCHIC_TYPE ; $18
	const ICE          ; $19
	const DRAGON       ; $1a
DEF NUM_TYPES EQU const_value   ; $1b = 27
```

### Type matchups (`data/types/type_matchups.asm`)

3-byte records, `-1` sentinel terminates the table:
```
TypeEffects:
	db WATER,        FIRE,         SUPER_EFFECTIVE
	db FIRE,         GRASS,        SUPER_EFFECTIVE
	db GROUND,       FLYING,       NO_EFFECT
	db WATER,        WATER,        NOT_VERY_EFFECTIVE
	db DRAGON,       DRAGON,       SUPER_EFFECTIVE
	db -1 ; end
```
Multiplier constants, scaled by 10 (`constants/battle_constants.asm`):
`SUPER_EFFECTIVE` 20, `MORE_EFFECTIVE` 15, `EFFECTIVE` 10, `NOT_VERY_EFFECTIVE` 5,
`NO_EFFECT` 0.

### Badge tiles and gym leader names

`gfx/trainer_card/badges.png`: 16 x 256 px, 2 bpp, 2 tiles wide x 32 tiles tall (64 tiles):
8 gym leader faces (4 tiles each, 32 tiles) followed by 8 badge icons (4 tiles each, 32
tiles). `engine/menus/draw_badges.asm` selects the badge over the face by adding 4 to the
base face tile id:
```
.CheckBadge
	srl b
	jr nc, .NextBadge
	ld a, [hl]
	add 4 ; Badge graphics are after each face
```
`gfx/trainer_card/badge_numbers.png`: 16 x 32 px, 2 bpp (8 digit-glyph tiles). There is no
dedicated gym leader names table: names appear as literal dialogue strings in per-gym text
files, e.g. `text/PewterGym.asm` ("I'm BROCK!"), and similarly in `text/CeruleanGym.asm`,
`text/VermilionGym_2.asm`, `text/CeladonGym.asm`, `text/FuchsiaGym.asm`,
`text/SaffronGym.asm`, `text/CinnabarGym.asm`, `text/ViridianGym.asm` (Giovanni).

### Map names shown on the town map (`data/maps/names.asm`)

Flat, labeled, `@`-terminated strings, not an indexed array:
```
PalletTownName:      db "PALLET TOWN@"
ViridianCityName:    db "VIRIDIAN CITY@"
```
Map id to name/coordinate association is in `data/maps/town_map_entries.asm` (section 2
above). Display order for the town map's scrolling location list is a separate 1-byte
per-entry table, `data/maps/town_map_order.asm`.

### Status condition bits

There is no `constants/status_constants.asm` file; the status byte layout is defined in
`constants/battle_constants.asm`:
```
DEF SLP_MASK EQU %111 ; 0-7 turns
	const_def 3
	const PSN ; 3
	const BRN ; 4
	const FRZ ; 5
	const PAR ; 6
```
Bits 0-2: sleep turn counter (0-7, nonzero = asleep). Bit 3: poison. Bit 4: burn. Bit 5:
freeze. Bit 6: paralyze. Bit 7: unused. Confirmed by usage, e.g.
`engine/battle/core.asm`: `and (1 << FRZ) | SLP_MASK`.

---

## 6. Wild encounters

### Per-map pointer table (`data/wild/grass_water.asm`)

One 2-byte pointer per map id, in map id order, indexed directly by map id:
```
WildDataPointers:
	table_width 2
	dw NothingWildMons         ; PALLET_TOWN
	dw Route1WildMons          ; ROUTE_1
	dw Route2WildMons          ; ROUTE_2
	...
	assert_table_length NUM_MAPS
	dw -1 ; end
```
Comment in the same file documents the record format: "first byte is encounter rate,
followed by 20 bytes: level, species (ten times)... if first byte == 0, no wild pokemon
on this map." Grass and water are two consecutive sub-tables inside the same per-map
block (not two independent top-level lists).

### Example record, full (`data/wild/maps/Route21.asm`)

```
Route21WildMons:
	def_grass_wildmons 25 ; encounter rate
	db 21, RATTATA
	db 23, PIDGEY
	db 30, RATICATE
	db 23, RATTATA
	db 21, PIDGEY
	db 30, PIDGEOTTO
	db 32, PIDGEOTTO
	db 28, TANGELA
	db 30, TANGELA
	db 32, TANGELA
	end_grass_wildmons

	def_water_wildmons 5 ; encounter rate
	db  5, TENTACOOL
	db 10, TENTACOOL
	db 15, TENTACOOL
	db  5, TENTACOOL
	db 10, TENTACOOL
	db 15, TENTACOOL
	db 20, TENTACOOL
	db 30, TENTACOOL
	db 35, TENTACOOL
	db 40, TENTACOOL
	end_water_wildmons
```
Exactly 10 slots each for grass and water, each slot 2 bytes (level, species).

### Macros (`macros/asserts.asm`)

```
MACRO? def_grass_wildmons
	DEF CURRENT_GRASS_WILDMONS_RATE = \1
{CURRENT_GRASS_WILDMONS_LABEL}:
	db \1
ENDM
```
`def_water_wildmons` is identical apart from naming. `WILDDATA_LENGTH`
(`constants/pokemon_data_constants.asm`):
```
DEF NUM_WILDMONS EQU 10
DEF WILDDATA_LENGTH EQU 1 + NUM_WILDMONS * 2   ; 21 bytes
```
If the encounter rate is 0 the record is just that single zero byte (`data/wild/maps/nothing.asm`).
There are 59 files under `data/wild/maps/*.asm`. Fishing rods (Old, Good, Super) are a
separate, unrelated system in `data/wild/good_rod.asm` / `data/wild/super_rod.asm`, not
part of the grass/water tables.

---

## 7. Items on the ground

This checkout uses different names from the classic pret/pokered layout described in the
task prompt: there is no `data/events/hidden_objects.asm` and no
`wMissableObjectFlags`/`MissableObjectList`. "Missable objects" have been renamed
"toggleable objects" throughout. The semantics (map-placed item balls, a persistent
picked-up flag per object) are the same; only the names differ.

### Hidden items (`data/events/hidden_item_coords.asm`)

```
MACRO hidden_item
	db \1, \3, \2   ; map id, x, y
ENDM

HiddenItemCoords:
	table_width 3
	hidden_item VIRIDIAN_FOREST,                1,  18
	hidden_item VIRIDIAN_FOREST,               16,  42
	hidden_item MT_MOON_B2F,                   18,  12
	hidden_item ROUTE_25,                      38,   3
	assert_max_table_length MAX_HIDDEN_ITEMS
	db -1 ; end
```
54 entries. This record is only (map, x, y): the row's 0-based position in the table is
itself the flag index (see below); the item id and trigger routine live in a separate
per-map table, `data/events/hidden_events.asm`:
```
MACRO hidden_event
	db \2 ; y
	db \1 ; x
	db \4 ; function argument (item id, for item routines)
	dba \3 ; event function
ENDM

	hidden_events_for REDS_HOUSE_2F
	hidden_event  0,  1, OpenRedsPC, SPRITE_FACING_UP
	hidden_event  3,  5, PrintRedSNESText, ANY_FACING
	db -1 ; end
```
Entries whose function pointer targets `HiddenItems` or `HiddenCoins`
(`engine/events/hidden_items.asm`) are real ground items or hidden coins, with the item id
carried in the function-argument byte; entries pointing at any other routine
(`OpenRedsPC`, `PrintBookcaseText`, poster/PC/terminal handlers, etc.) are non-item hidden
interactions. The distinction is made purely by which routine label the entry's `dba`
points at, not by a type tag in the record.

### Placed item balls (`macros/scripts/maps.asm`, `object_event`)

```
MACRO object_event
	db \3                    ; sprite id
	db \2 + 4                ; y + 4
	db \1 + 4                ; x + 4
	db \4                    ; movement (WALK/STAY)
	db \5                    ; direction or range
	IF _NARG > 7
		db TRAINER | \6
		db \7
		db \8
	ELIF _NARG > 6
		db ITEM | \6
		db \7
	ELSE
		db \6
	ENDC
ENDM
```
Field order in the emitted bytes: sprite id, y+4, x+4, movement, direction/range, then
(text id) or (`ITEM`-tagged text id, item id) or (`TRAINER`-tagged text id, trainer class,
trainer level), the branch chosen by argument count (6 for a plain NPC/sign, 7 for an
item, 8 for a trainer). Item ball examples (`SPRITE_POKE_BALL`, `STAY`, `NONE`):
```
; data/maps/objects/Route25.asm
	object_event 22,  2, SPRITE_POKE_BALL, STAY, NONE, TEXT_ROUTE25_TM_SEISMIC_TOSS, TM_SEISMIC_TOSS

; data/maps/objects/Route2.asm
	object_event 13, 54, SPRITE_POKE_BALL, STAY, NONE, TEXT_ROUTE2_MOON_STONE, MOON_STONE
	object_event 13, 45, SPRITE_POKE_BALL, STAY, NONE, TEXT_ROUTE2_HP_UP, HP_UP
```
There is no missable index field on `object_event` itself. Each toggleable object is
separately registered per map in `data/maps/toggleable_objects.asm`:
```
MACRO toggle_object_state
	db toggle_map_id ; from the enclosing toggleable_objects_for
	db \1             ; map-local object id
	db \2             ; ON/OFF
ENDM

	toggleable_objects_for ROUTE_25
	toggle_object_state ROUTE25_TM_SEISMIC_TOSS, ON
```
At load time these 3-byte records are converted into a runtime list,
`wToggleableObjectList` (map-local sprite id, global toggleable index), the global index
being `(record offset from the start of the states table) / 3`.

### Pickup tracking, bit math

Both flag arrays use the identical `index >> 3` for byte, `index & 7` for bit scheme.

Hidden items, `wObtainedHiddenItemsFlags`, via `FlagAction` (`engine/flag_action.asm`,
called through `predef FlagActionPredef`):
```
FlagAction:
; Perform action b on bit c in the bitfield at hl. 0=reset 1=set 2=read
	ld a, c
	ld d, a
	and 7          ; bit index = c mod 8
	ld e, a
	ld a, d
	srl a
	srl a
	srl a          ; byte offset = c / 8
	add l
	ld l, a
```
The row index into `HiddenItemCoords` (found by `FindHiddenItemOrCoinsIndex`, a linear
scan comparing map/x/y) is the bit index `c` passed into `FlagAction` against
`wObtainedHiddenItemsFlags`.

Placed objects, `wToggleableObjectFlags`, via `ToggleableObjectFlagAction`
(`engine/overworld/toggleable_objects.asm`), byte-for-byte the same shift/mask scheme
against the global toggleable object index assigned at map load.

---

## 8. WRAM offsets

Anchors given and confirmed exact against `ram/wram.asm`
(`/Users/Shared/Documents/Muon_Decider/third_party/pokered/ram/wram.asm`):
`wPartyCount` = $D163, `wPokedexOwned` = $D2F7, `wPlayTimeSeconds` = $DA44.

`layout.link`'s `WRAM0` block places `SECTION "WRAM"`, `"Party Data"`, `"Main Data"`, and
`"Current Box Data"` back to back with no `org` between them, so byte-offset arithmetic
across all four sections is valid (only `"Audio RAM"`/`"Sprite State Data"`/etc before, and
`"Stack"` after, are separated by explicit `org`s). All three given anchors independently
resolve to the same base address for `SECTION "WRAM"`: $CBFC. That three-way agreement
(computed by walking every intervening db/dw/ds/macro from that base) is the check that
the byte count below is not miscounted anywhere in between.

### Struct macros (`macros/ram.asm`)

```
MACRO box_struct
\1Species::    db
\1HP::         dw
\1BoxLevel::   db
\1Status::     db
\1Type1::      db
\1Type2::      db
\1CatchRate::  db
\1Moves::      ds NUM_MOVES        ; 4
\1OTID::       dw
\1Exp::        ds 3
\1HPExp::      dw
\1AttackExp::  dw
\1DefenseExp:: dw
\1SpeedExp::   dw
\1SpecialExp:: dw
\1DVs::        dw
\1PP::         ds NUM_MOVES        ; 4
ENDM
```
Sum: 1+2+1+1+1+1+1+4+2+3+2+2+2+2+2+2+4 = **33 bytes**. Matches the plan doc's claim.

```
MACRO party_struct
	box_struct \1
\1Level::      db
\1MaxHP::      dw
\1Attack::     dw
\1Defense::    dw
\1Speed::      dw
\1Special::    dw
ENDM
```
Sum: 33 + 1 + 2+2+2+2+2 = **44 bytes**. Matches the plan doc's claim, and is independently
confirmed by `constants/pokemon_data_constants.asm`: `DEF PARTYMON_STRUCT_LENGTH EQU _RS ; $2c` ($2c = 44).

```
MACRO battle_struct
\1Species::    db
\1HP::         dw
\1BoxLevel::   db
\1Status::     db
\1Type1::      db
\1Type2::      db
\1CatchRate::  db
\1Moves::      ds NUM_MOVES        ; 4
\1DVs::        dw
\1Level::      db
\1MaxHP::      dw
\1Attack::     dw
\1Defense::    dw
\1Speed::      dw
\1Special::    dw
\1PP::         ds NUM_MOVES        ; 4
ENDM
```
Sum: 1+2+1+1+1+1+1+4+2+1+2+2+2+2+2+4 = **29 bytes**. The plan doc's WRAM sketch says 28;
that is off by one. Use 29.

### Per-symbol offsets

All addresses below were computed by summing every declared db/dw/ds/macro size between
`SECTION "WRAM"`'s start ($CBFC) and the target symbol, using real constants pulled from
`constants/*.asm` (not assumed): `NUM_MOVES` = 4, `PARTY_LENGTH` = 6, `MONS_PER_BOX` = 20,
`NAME_LENGTH` = 11, `BAG_ITEM_CAPACITY` = 20, `PC_ITEM_CAPACITY` = 50,
`MAX_HIDDEN_ITEMS` = 112, `MAX_HIDDEN_COINS` = 16, `NUM_POKEMON` = 151, `NUM_EVENTS` =
2560.

**wMissableObjectFlags: does not exist in this checkout.** `grep -ril "missable"` over the
whole tree returns nothing; this is a Gen 2 (pokecrystal) name, not a pret/pokered one.
The nearest real equivalents are `wObtainedHiddenItemsFlags` and
`wObtainedHiddenCoinsFlags` (hidden items) and `wToggleableObjectFlags` (placed item balls
and other map objects, functionally the "missable object" flags, see section 7).

- **wObtainedHiddenItemsFlags**: `flag_array MAX_HIDDEN_ITEMS` = `ceil(112/8)` = 14 bytes.
  Offset 2804 (0xAF4) from $CBFC = **$D6F0**. Immediately followed by
  `wObtainedHiddenCoinsFlags:: flag_array MAX_HIDDEN_COINS` = `ceil(16/8)` = 2 bytes, at
  **$D6FE**.

- **wEventFlags**: `flag_array NUM_EVENTS`, `NUM_EVENTS` = 2560 (from
  `constants/event_constants.asm`, `const_next $A00`), size = `2560/8` = 320 bytes. Offset
  2891 (0xB4B) = **$D747**, spanning $D747-$D886.

- **wEnemyMon / wEnemyMonNick**: `wEnemyMonNick:: ds NAME_LENGTH` (11 bytes) at offset 990
  (0x3DE) = **$CFDA**; `wEnemyMon:: battle_struct wEnemyMon` (29 bytes) immediately after,
  offset 1001 (0x3E9) = **$CFE5**.

- **wBattleMon**: `wBattleMon:: battle_struct wBattleMon` (29 bytes), offset 1048 (0x418)
  = **$D014**.

- **wIsInBattle**: offset 1115 (0x45B) = **$D057**. Matches the plan doc exactly (not an
  approximation).

- **wCurOpponent**: offset 1117 (0x45D) = **$D059**. Also matches the plan doc exactly.

- **wPlayerMonNumber**: offset 51 (0x33) = **$CC2F**.

- **wNumBagItems**: offset 1825 (0x721) = **$D31D**. **wBagItems**:
  `ds BAG_ITEM_CAPACITY * 2 + 1` (41 bytes, item/quantity pairs plus a terminator),
  immediately after, offset 1826 (0x722) = **$D31E**.

- **wPlayerCoins**: `dw` (BCD), offset 2472 (0x9A8) = **$D5A4**.

- **wCurrentBoxNum**: offset 2468 (0x9A4) = **$D5A0** (bits 0-6 box number, bit 7 whether
  the player has ever changed boxes).

- **wNumInBox: does not exist under that name.** `grep -in "numinbox"` over `ram/wram.asm`
  finds nothing. The real symbol for "count of mons in the currently-viewed box" is
  `wBoxCount`, first byte of the separate `"Current Box Data"` section (still contiguous
  with the anchors per `layout.link`), offset 3716 (0xE84) = **$DA80**.

- **wDayCareInUse**: offset 3660 (0xE4C) = **$DA48**. **wDayCareMonName**:
  `ds NAME_LENGTH` (11 bytes), immediately after, offset 3661 (0xE4D) = **$DA49**.

- **wRepelRemainingSteps**: offset 1247 (0x4DF) = **$D0DB**.

- **wPlayTimeHours**: offset 3653 (0xE45) = **$DA41**, followed by
  `wPlayTimeMaxed`, `wPlayTimeMinutes`, `wPlayTimeSeconds` (each 1 byte), so
  `wPlayTimeSeconds` lands 3 bytes later at offset 3656 (0xE48) = $DA44, matching the given
  anchor exactly and cross-checking the whole chain.

---

## Byte size estimate for a full pack

Measured directly from the checkout (raw uncompressed 2bpp/1bpp tile data and binary
files, not the PNG-compressed on-disk sizes, since PNG compression is not what would ship
in the pack):

| Component | Bytes | Notes |
|---|---|---|
| Font (`font.1bpp` + `font_extra.2bpp` + `font_battle_extra.2bpp` + `AB`/`ED`/`P`) | ~2,064 | 1bpp main font is 1024B alone |
| Town map (tile gfx 256B + `.rle` 171B + cursor 32B) | ~459 | |
| Tileset graphics, all 19, raw 2bpp | 27,136 | |
| Blocksets, all 19 `.bst` | 22,960 | |
| Map `.blk` files, all 228 | 24,518 | avg 107.5B/map |
| Front sprites, all 153 (151 species + 2 fossil pics), raw 2bpp | 91,328 | |
| Back sprites, all 151, raw 2bpp | 38,656 | fixed 32x32 each |
| Icons, 4 shape sheets | 256 | |
| Tables (species/item/move names, base stats, moves, type matchups, map names, wild data, hidden items, menu icon nybbles, badges) | ~13,400 | see per-table breakdown below |
| **Total** | **~220,800 bytes (~216 KB)** | plus a few hundred bytes of pack container header (section table) |

Table breakdown used for the ~13,400 byte line: species names 151x10=1,510; dex order
151x1=151; base stats 151x28=4,228; item names 97 entries, ~873; move names 165 entries,
~1,485; move data 165x6=990; type matchups 82x3=246; map names 53 entries, ~636; wild
encounter data, 59 map files, ~1,858; hidden item coords 54x3=162; menu icon nybbles
ceil(151/2)=76; badge graphics 1,024+128=1,152.

~216 KB total is comfortably small for an ESP32-S3 asset pack (fits easily on SD or in a
SPIFFS/LittleFS partition alongside the existing dashboard assets); front/back Pokemon
sprites dominate at just under 60% of the pack.

---

## Summary of surprises

- **`wMissableObjectFlags` does not exist in this checkout.** It is a Gen 2
  (pokecrystal/gold) name that got into the plan doc's WRAM sketch by cross-contamination.
  This pret/pokered checkout calls the equivalent mechanism "toggleable objects"
  (`wToggleableObjectFlags`, `wToggleableObjectList`, `data/maps/toggleable_objects.asm`),
  and separately tracks hidden ground items via `wObtainedHiddenItemsFlags` /
  `wObtainedHiddenCoinsFlags`. Both use the same `index >> 3` / `index & 7` bit math the
  plan doc expected, just under different names.
- **`wNumInBox` also does not exist**; the real symbol is `wBoxCount` at $DA80, the first
  byte of a separate but still-contiguous `"Current Box Data"` linker section.
- **`battle_struct` is 29 bytes, not 28** as sketched in the plan doc (`box_struct` at 33
  and `party_struct` at 44 are both correct as assumed). This affects `wEnemyMon` (starts
  $CFE5) and `wBattleMon` (starts $D014) sizing if anything downstream slices these
  structs by a hardcoded 28.
- The classic pret/pokered `data/events/hidden_objects.asm` file is split in this
  checkout into `data/events/hidden_item_coords.asm` (map/x/y only) and
  `data/events/hidden_events.asm` (item id and routine), with the item-vs-other
  distinction made purely by which routine label a `dba` operand points to, not a type
  tag.
- Item ball placement (`object_event`) carries no missable/toggle index itself: the index
  is assigned separately, at load time, from a per-map `data/maps/toggleable_objects.asm`
  table, computed as `(table offset) / 3`.
- Back sprites are all a fixed 32x32 px (4x4 tiles) regardless of species, while front
  sprites vary between 40x40 (5x5), 48x48 (6x6), and 56x56 (7x7) tiles; the "sprite
  dimensions" byte in base stats is read directly out of byte 0 of the compiled `.pic`
  file rather than being an independently authored field, and is generated by
  `tools/pkmncompress.c` as `(width << 4) | width` (both nibbles equal, since sprites are
  always square).
- Blocksets are `gfx/blocksets/*.bst`, not `data/tilesets/*.bst` as the task prompt
  guessed; `data/tilesets/` only holds header/metadata `.asm` files (tileset headers,
  collision ids, warp tile ids, etc.), no `.bst` binaries.
- Party icons are not a single sprite sheet: only 4 tiny 8x32 px "shape" graphics exist
  (bug, plant, quadruped, snake) plus several more shapes borrowed from unrelated general
  sprite sheets; species are mapped to a shape via a packed nybble array, not a per-species
  icon graphic.
