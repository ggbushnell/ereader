# Pokemon aux display: pixel layout

Design pass for the four companion views described in
`docs/pokemon-aux-display-plan-2026-09-12.md`. Written 2026-09-12. This is
the contract the view code (`src/pokemon_*`) is built from; every
coordinate below is a panel pixel on the 680 wide by 920 tall portrait canvas
(`ui::gfx()`, rotation set in `ui::begin()`), every graphic is a Game Boy
8x8 tile from the pack drawn at an integer scale, and every glyph is the
game's own font. Nothing on these screens comes from a u8g2 font or an
Adafruit_GFX icon. The reader's chrome (the top bar and the hint band) uses
the game font too, drawn white on black, which the game itself does for
selected list rows and the Pokedex, so it stays inside the look.

Asset facts checked against the pokered checkout
(`third_party/pokered`):

- `gfx/font/font.png`: 128x64, 1 bpp, 128 tiles, codes $80 to $FF (A = $80,
  a = $A0, 0 = $F6). Punctuation present: `( ) : ; [ ] ' - ? ! . / ,` plus
  `é` $BA, `▷` $EC, `▶` $ED, `▼` $EE, `♂` $EF, `¥` $F0 (drawn as `$` in the
  US ROM, the money glyph), `×` $F1, `♀` $F5. The font has NO `%`, `+`, `<`,
  `>`, `#` or `*`; the layouts below never use those characters.
- `gfx/font/font_extra.png`: 128x16, 2 bpp, 32 tiles at $60 to $7F. The text
  box border is `┌` $79, `─` $7A, `┐` $7B, `│` $7C, `└` $7D, `┘` $7E, box
  interior space $7F.
- `gfx/font/font_battle_extra.png`: 120x16, 2 bpp, 30 tiles at $62 to $7F
  (HP bar and status glyphs). `$62` left cap, `$63` empty fill, `$64` to
  `$6A` partial fill 1 to 7 px, `$6B` full fill, `$6C` right cap (party
  menu), `$6D` right cap (battle and status screen), `$6E` the `<LV>` glyph,
  `$70` narrow `to`, `$71` the `HP:` glyph, `$73` `<ID>`, `$74` `№`.
- `gfx/battle/battle_hud_1.png` to `_3.png`: three 1 bpp tiles each, the HUD
  rules and corners. Not used; the box border tiles carry every divider.
- `gfx/town_map/town_map.png`: 32x32, 16 tiles (2 bpp); `town_map.rle`: the
  20x18 tilemap; `town_map_cursor.png`: 16x16 1 bpp cursor sprite;
  `mon_nest_icon.png`: 8x8 (not used here).
- `gfx/trainer_card/badges.png`: 16x256, 2 bpp, 8 groups of 4 tile rows: for
  badge k (0 based) rows 4k and 4k+1 are the gym leader face (16x16), rows
  4k+2 and 4k+3 the badge (16x16). That is exactly how `DrawBadges` in
  `engine/menus/draw_badges.asm` picks them (face tile id + 4 = badge).
  `badge_numbers.png`: 16x32, 8 tiles, the `[1]` to `[8]` markers.
- Party icons: 16x16, two animation frames each (`data/icon_pointers.asm`);
  frame 0 only is drawn here.
- Front sprites: 40x40 (48 species), 48x48 (49) or 56x56 (56). Back sprites:
  32x32, all 151.
- Overworld player sprite `gfx/sprites/red.png`: 16x96, six 16x16 frames
  (down, up, left, and the three walking frames); right facing is left
  mirrored, the way the game does it.
- `gfx/battle/balls.png`: 4 tiles, the party ball markers. Not used.
- `GetHealthBarColor` (`home/palettes.asm`): with `e` the fill in pixels out
  of 48, green when `e >= 27`, yellow when `10 <= e < 27`, red when `e < 10`.
- `DrawHPBar` (`home/pokemon.asm`): `[$71 HP:][$62][6 fill tiles][$6C or
  $6D]`, fill pixels `e = 48 * hp / maxhp` floored, forced to 1 when hp > 0.
- Status strings (`engine/pokemon/status_ailments.asm`): `SLP PSN BRN FRZ
  PAR`, plus `FNT` for a fainted mon and blank for none.
- Wild tables live in WRAM already (`wGrassRate`, `wGrassMons`,
  `wWaterRate`, `wWaterMons`, `ram/wram.asm`), ten (level, species) pairs
  each; per slot odds are the game's fixed 256ths 51 51 39 25 25 25 13 13 3 3,
  printed as 20 20 15 10 10 10 5 5 1 1 percent.

## Grid, scales and shared rules

Two tile grids share one origin at (4, 24). Everything is placed on 8 px
multiples from that origin, so 2x tiles (16 px) and 3x tiles (24 px) can sit
in the same view without a half pixel anywhere.

| Grid | Tile | Columns | Rows | x of column c | y of row r | Last edge |
|---|---|---|---|---|---|---|
| 2x | 16 px | 42 | 54 | 4 + 16c | 24 + 16r | x 676, y 888 |
| 3x | 24 px | 28 | 36 | 4 + 24c | 24 + 24r | x 676, y 888 |

Bands that are the reader's, not the game's:

| Element | x | y | w | h | Scale | Notes |
|---|---|---|---|---|---|---|
| Top bar | 0 | 0 | 680 | 24 | 2x text | black fill, white glyphs at y 4 |
| Content area | 0 | 24 | 680 | 864 | | 24 to 888, both grids end at 888 |
| Hint band | 0 | 896 | 680 | 24 | 2x text | black fill, white glyphs at y 900; the 8 px 888 to 896 stays white, same rule line the reader's status strip sits under |

Top bar fields (2x font, 16 px per cell, white on black):

| Field | x | Cells | Example |
|---|---|---|---|
| view name | 12 | 9 | `INVENTORY` |
| player name | 172 | 7 | `RED` |
| play time | 292 | 11 | `TIME 123:45` (hours:minutes) |
| last update | 476 | 12 | `UPD 21:07:55` (reader clock at the last accepted WRAM snapshot, `UPD --:--:--` before SNTP has run) |

Hint band, 2x font, white on black, x 12, y 900. One string per view, the
pad names are the silkscreen labels:

- Home: `R:INVENTORY  L:BACK  U:TERRAIN  C:EXIT`
- Inventory: `R:HOME  L:BACK  U:TERRAIN  C:EXIT`
- Terrain: `R:NEXT PAGE  L:BACK  U:HOME  C:EXIT`
- Battle: `R:NEXT PAGE  L:BACK  U:TERRAIN  C:EXIT`

Text scale rules:

- 2x (16 px cells) is the body scale: lists, numbers, types, coordinates,
  every line that has to pack. Forty cells fit inside a full width 2x box.
- 3x (24 px cells) is for names and headers: nicknames, enemy name, move
  names, bag item names. Never mix scales inside one line of text; a 3x name
  line can sit above a 2x detail line, that is the pattern used everywhere
  below.
- Text is left aligned and starts one cell inside a box's left border (the
  game leaves that cell blank too). Numbers that are right aligned pad with
  spaces on the left, the game's own `PrintNumber` style.
- Text boxes are drawn with the border tiles at the box's own scale: `┌ ─ ┐`
  on the top row, `│` down both sides, `└ ─ ┘` on the bottom row, and the
  interior is left white (not the $7F tile, so partial redraws only touch the
  glyphs). Adjacent boxes touch at their borders (no gutter); a box top never
  touches the black top bar, there is always an 8 px white gap under the bar.
- Glyph cells are 8x8 at 1x, so a 2x text row is 16 px tall and a 3x one 24
  px. Lines of the same scale stack with no extra leading; blank rows are the
  spacing tool, exactly as on the Game Boy.
- Inverted cells (white on black) mean "selected or important": the active
  battle mon's name, an effectiveness badge, a red HP number. Nothing else is
  inverted inside the content area.

Shade mapping, 2 bpp tile pixel to the 1 bit panel. The pattern is evaluated
in absolute panel coordinates (px, py) so it tiles seamlessly across tile
edges and across scales:

| GB shade | Meaning | Panel rule |
|---|---|---|
| 0 | white | always white |
| 1 | light gray | black when `(px & 1) == 0 && (py & 1) == 0` (one dot in four) |
| 2 | dark gray | black when `((px + py) & 1) == 0` (checker) |
| 3 | black | always black |

At 2x each source pixel is a 2x2 block so shade 1 lands one dot per block
and shade 2 a two dot checker, which is the "2x2 dither per shade" the plan
describes. At 3x the same rule gives a 3x3 block four or five dots for shade
2 and two or three for shade 1; the result reads as the same two grays. A
1x draw (not used by these views) thresholds shades 0 and 1 to white, 2 and
3 to black. 1 bpp assets (font, cursor, hud) have only shades 0 and 3.
Dimmed drawing (used for missing badges) shifts every shade one step
lighter before the rule: 3 to 2, 2 to 1, 1 to 0.

Sprites (front, back, icons, overworld) are 2 bpp and use the same rule.
Shade 0 in a sprite is transparent (the panel white shows through), which is
what lets the cursor and the player marker sit over the map.

### HP bar rendering

The game's bar is nine tiles: `HP:` glyph, left cap, six fill tiles (48 px
of fill at 1x), right cap. Inside every fill tile rows 2 and 5 are the black
rails and rows 3 and 4 are the 2 px fill band (shade 2 in the ROM); rows 0,
1, 6, 7 are white. Sizes: 72x8 at 1x, 144x16 at 2x, 216x24 at 3x.

1. Compute `e = floor(48 * hp / maxhp)`, and `e = 1` if `hp > 0 && e == 0`.
   Fainted mons get `e = 0`.
2. Draw the nine tiles from `font_battle_extra` at the bar's scale: `$71`,
   `$62`, then for each of the six fill tiles `$6B` while 8 or more fill
   pixels remain, `$63 + remaining` for the last partial tile, `$63` after
   that, then the right cap (`$6D` in the battle and status views, `$6C` in
   the party lists, matching `wHPBarType`).
3. Colour, from `GetHealthBarColor`: green `e >= 27`, yellow `10 <= e < 27`,
   red `e < 10`. The bar's fill band (rows 3 and 4 of the fill tiles, scaled:
   4 px tall at 2x, 6 px at 3x) over the first `e` source pixels is then
   repainted:
   - green: solid black (override the shade 2 checker the ROM tile would
     give).
   - yellow: the shade 2 checker, which is what the plain tile blit already
     produced, so nothing changes.
   - red: the shade 1 sparse pattern (one dot in four), and the HP numbers
     next to the bar are drawn inverted (white glyphs on black cells) so a
     one pixel red sliver still reads from across the desk.
   Concretely: after the blit, walk the fill band rectangle
   `x = bar_x + 16 * s .. bar_x + 16 * s + e * s - 1`, `y = bar_y + 3 * s ..
   bar_y + 5 * s - 1` (s = scale) and set each pixel from the pattern for the
   colour. The rails, caps and the unfilled part are left as the ROM drew
   them.
4. HP numbers are `cur/max` with the current value right aligned in 3 cells
   and the max in 3 cells: `123/123`, ` 45/123`. Seven cells, always. Where a
   line has no room for both, the numbers move to the line below the bar,
   never dropped.

## View 1: Home

Town map at 2x, not 3x. Reasons: at 3x (480x432) the map alone takes half the
content height and forces the party into single 2x lines with no room for
the trainer card; the town map is a 16 tile pictogram whose 8x8 cells carry
no detail that 3x would reveal; at 2x the map's cells are exactly the 2x text
grid, so the cursor cell, the location box and the flanking cards all sit on
the same 16 px lattice; and the 180 px on either side become the trainer
card, which is what the game shows on the same screen family (the Town Map
and the Trainer Card share the pause menu). The result is one screen that
looks like the Town Map with the Trainer Card and the Party menu blown up
around it.

```
+----------------------------------------------------------------------+ 0
| HOME      RED      TIME 123:45                       UPD 21:07:55    | top bar
+----------------------------------------------------------------------+ 24
 +--------+     [ town map 20x18 tiles at 2x, 320x288 ]     +--------+  32
 |NAME    |     [                                     ]     |POKéDEX |
 | RED    |     [        (cursor sprite over the      ]     |OWN 151 |
 |ID№     |     [         current cell, dotted box)   ]     |SEEN 151|
 | 12345  |     [                                     ]     |        |
 |        |     [                                     ]     |TIME    |
 |MONEY   |     [                                     ]     | 123:45 |
 |$123456 |     [                                     ]     |        |
 |        |     [                                     ]     |REPEL   |
 |COINS   |     [                                     ]     | 100    |
 | 9999   |     [                                     ]     |        |
 |        |     [                                     ]     |MAP 12  |
 |RIVAL   |     [                                     ]     |X 5 Y 7 |
 | BLUE   |     [                                     ]     |        |
 |        |     [                                     ]     |BAG     |
 |        |     [                                     ]     | 20/20  |
 |        |     +-----------------------------------+       |        | 328
 |        |     | PALLET TOWN                       |       |        |
 +--------+     +-----------------------------------+       +--------+ 368/376
 +---------------------------------------------------------------------+ 384
 | [icon] PIKACHU    L:100  PAR                                        | 400
 |        HP:[=========   ] 123/123   ELECTRIC                         |
 |        PIKACHU  OT RED 12345                                        |
 | [icon] ...  (six rows, 64 px pitch)                                 |
 +---------------------------------------------------------------------+ 800
 +---------------------------------------------------------------------+ 808
 | [1][BADGE] [2][BADGE] [3][face] [4][face] [5]... [8][face]          |
 +---------------------------------------------------------------------+ 888
+----------------------------------------------------------------------+ 896
| R:INVENTORY  L:BACK  U:TERRAIN  C:EXIT                               | hint
+----------------------------------------------------------------------+ 920
```

| Element | x | y | w | h | Tile scale | Font scale | Pack asset / source |
|---|---|---|---|---|---|---|---|
| Town map | 180 | 32 | 320 | 288 | 2x | | `town_map` tiles (16) + decoded 20x18 tilemap; cell (cx, cy) is at (180 + 16cx, 32 + 16cy) |
| Current cell box | 180 + 16cx - 4 | 32 + 16cy - 4 | 24 | 24 | | | 1 px rectangle drawn with every other pixel black (sparse), 4 px outside the cell; the "subtle box" |
| Map cursor | 180 + 16cx - 8 | 32 + 16cy - 8 | 32 | 32 | 2x | | `town_map_cursor` 16x16 1 bpp sprite, shade 0 transparent, centred on the cell, drawn last. Cell comes from the town map entry for `wCurMap` (`data/maps/town_map_entries.asm`; an indoor map uses its outdoor parent's entry) |
| Location box | 180 | 328 | 320 | 48 | 2x | | border tiles, 20x3 tiles |
| Location name | 196 | 344 | 288 | 16 | | 2x | map name string from the pack (`data/maps/names.asm`), left aligned, at most 18 cells |
| Trainer card left box | 4 | 32 | 160 | 336 | 2x | | border tiles, 10x21 tiles; inner rows at y = 48 + 16k, k 0..18, x 20, 8 cells |
| left row 0 | 20 | 48 | 128 | 16 | | 2x | `NAME` |
| left row 1 | 20 | 64 | 128 | 16 | | 2x | space + player name (`wPlayerName`, up to 7) |
| left row 3 | 20 | 96 | 128 | 16 | | 2x | `<ID>` $73 and `№` $74 tiles, then 5 digit `wPlayerID` starting row 4 with one leading space |
| left row 6 / 7 | 20 | 144 / 160 | 128 | 16 | | 2x | `MONEY` / `$123456` (`¥` tile $F0 then 6 digits from BCD `wPlayerMoney`, leading zeros dropped, right aligned in 7 cells) |
| left row 9 / 10 | 20 | 192 / 208 | 128 | 16 | | 2x | `COINS` / 4 digit `wPlayerCoins`, right aligned in 5 cells |
| left row 12 / 13 | 20 | 240 / 256 | 128 | 16 | | 2x | `RIVAL` / space + `wRivalName` |
| Trainer card right box | 516 | 32 | 160 | 336 | 2x | | border tiles, 10x21; inner rows y = 48 + 16k, x 532, 8 cells |
| right row 0 | 532 | 48 | 128 | 16 | | 2x | `POKéDEX` (é is $BA) |
| right row 1 / 2 | 532 | 64 / 80 | 128 | 16 | | 2x | `OWN 151` / `SEEN 151` (bit counts of `wPokedexOwned`, `wPokedexSeen`, right aligned in 3) |
| right row 4 / 5 | 532 | 112 / 128 | 128 | 16 | | 2x | `TIME` / ` 123:45` (`wPlayTimeHours`:`wPlayTimeMinutes`) |
| right row 7 / 8 | 532 | 160 / 176 | 128 | 16 | | 2x | `REPEL` / ` 100` steps from `wRepelRemainingSteps`, or ` none` when 0 |
| right row 10 / 11 | 532 | 208 / 224 | 128 | 16 | | 2x | `MAP 12` (`wCurMap`) / `X 5 Y 7` (`wXCoord`, `wYCoord`, 2 digits each, `X12 Y34`) |
| right row 13 / 14 | 532 | 256 / 272 | 128 | 16 | | 2x | `BAG` / ` 20/20` (`wNumBagItems` of 20) |
| Party box | 4 | 384 | 672 | 416 | 2x | | border tiles, 42x26 tiles; inner x 20..660, rows top at y = 400 + 64r, r 0..5 |
| party icon | 20 | 400 + 64r | 48 | 48 | 3x | | party icon frame 0 for the species' icon class (`data/pokemon/menu_icons.asm`), shade 0 transparent |
| party nickname | 76 | 400 + 64r | 240 | 24 | | 3x | `wPartyMonNicks[r]`, 10 cells |
| party level | 340 | 400 + 64r | 96 | 24 | 3x tile + 3x text | | `<LV>` tile $6E at x 340, then level 3 cells at x 364 (`wPartyMon[r].level`), right aligned |
| party status | 460 | 400 + 64r | 72 | 24 | | 3x | `SLP PSN BRN FRZ PAR FNT` from the status byte and hp == 0; blank when healthy |
| party HP bar | 76 | 424 + 64r | 144 | 16 | 2x | | nine bar tiles, party cap $6C, colour rule above |
| party HP numbers | 228 | 424 + 64r | 112 | 16 | | 2x | `123/123`, inverted when red |
| party types | 356 | 424 + 64r | 240 | 16 | | 2x | `ELECTRIC/FLYING` or `NORMAL`, from base stats in the pack |
| party detail line | 76 | 440 + 64r | 576 | 16 | | 2x | species name (10 cells), two spaces, `OT`, space, OT name (7), space, OT id (5): `PIKACHU   OT RED 12345` |
| Badges box | 4 | 808 | 672 | 80 | 2x | | border tiles, 42x5 tiles |
| badge number k | 28 + 80k | 824 | 16 | 16 | 2x | | `badge_numbers` tile k (the game's `[k+1]` marker), k 0..7 |
| badge k | 44 + 80k | 824 | 48 | 48 | 3x | | `badges` sheet rows 4k+2..4k+3 (the badge) at full contrast when bit k of `wObtainedBadges` is set; otherwise rows 4k..4k+1 (the gym leader face) drawn dimmed. That is what the game's trainer card does (face until earned), with the dim added so the earned ones pop |

Empty party slots (r >= `wPartyCount`) leave their 64 px row blank. Party
rows never scroll: six is the cap.

## View 2: Inventory

Bag at 3x in one column of twenty rows (the bag holds at most 20 items, so
the list never scrolls), then two 2x boxes: the PC item list on the left,
money, coins, box, day care and the current box's mons on the right.

```
+----------------------------------------------------------------------+ 0
| INVENTORY RED      TIME 123:45                       UPD 21:07:55    |
+----------------------------------------------------------------------+ 24
 +--------------------------------------------------------------------+ 32
 | MASTER BALL   ×  1                                          $    0 | 56
 | ULTRA BALL    × 23                                          $ 1200 |
 | THUNDERSTONE  ×  1                                          $ 2100 |
 | TM24          ×  1                                          $ 3000 |
 | ... twenty rows, 24 px pitch                                       |
 +--------------------------------------------------------------------+ 560
 +---------------------------------+----------------------------------+ 568
 |PC ITEMS 23/50                   |MONEY  $123456                    | 584
 |POTION       ×99                 |COINS    9999                     |
 |RARE CANDY   × 4                 |                                  |
 |...  (16 rows)                   |BOX 3  12/20                      |
 |                                 |DAY CARE                          |
 |                                 | PIKACHU  L:25                    |
 |                                 |                                  |
 |                                 |IN BOX 3                          |
 |                                 | BULBASAUR  L: 5                  |
 |..7 MORE                         | ...  (8 rows)  ..4 MORE          |
 +---------------------------------+----------------------------------+ 888
+----------------------------------------------------------------------+ 896
| R:HOME  L:BACK  U:TERRAIN  C:EXIT                                    |
+----------------------------------------------------------------------+ 920
```

| Element | x | y | w | h | Tile scale | Font scale | Pack asset / source |
|---|---|---|---|---|---|---|---|
| Bag box | 4 | 32 | 672 | 528 | 3x | | border tiles, 28x22 tiles; inner x 28..652 (26 cells), rows y = 56 + 24i, i 0..19 |
| bag item name | 52 | 56 + 24i | 312 | 24 | | 3x | item name from the pack (`data/items/names.asm`), 13 cells (`THUNDERSTONE` is 12; `HM01`, `TM24` as the game prints them) |
| bag quantity | 388 | 56 + 24i | 72 | 24 | | 3x | `×` $F1 then the count right aligned in 2 cells, `× 1`, `×23` (quantities of 1 still show, unlike the game's key items, so a row never looks empty) |
| bag price | 508 | 56 + 24i | 144 | 24 | | 3x | `$` $F0 then the buy price right aligned in 5 cells, from `data/items/prices.asm` in the pack, `$    0` for key items; the Mart's own column |
| PC items box | 4 | 568 | 336 | 320 | 2x | | border tiles, 21x20 tiles; inner x 20..324 (19 cells), rows y = 584 + 16k, k 0..17 |
| pc row 0 | 20 | 584 | 304 | 16 | | 2x | `PC ITEMS 23/50` (`wNumBoxItems` of 50) |
| pc rows 1..16 | 20 | 600 + 16j | 304 | 16 | | 2x | `wBoxItems[j]`: name 12 cells, space, `×` and 2 digits |
| pc row 17 | 20 | 856 | 304 | 16 | | 2x | `..7 MORE` when more than 16 PC items, else the 17th item |
| Status box | 340 | 568 | 336 | 320 | 2x | | border tiles, 21x20; inner x 356..660, rows y = 584 + 16k |
| status row 0 | 356 | 584 | 304 | 16 | | 2x | `MONEY  $123456` |
| status row 1 | 356 | 600 | 304 | 16 | | 2x | `COINS    9999` |
| status row 3 | 356 | 632 | 304 | 16 | | 2x | `BOX 3  12/20` (`wCurrentBoxNum` low bits + 1, `wNumInBox` of 20) |
| status row 4 / 5 | 356 | 648 / 664 | 304 | 16 | | 2x | `DAY CARE` / space, `wDayCareMonName` (10), two spaces, `<LV>` tile, level from `wDayCareMon`; ` EMPTY` when `wDayCareInUse` is 0 |
| status row 7 | 356 | 696 | 304 | 16 | | 2x | `IN BOX 3` |
| status rows 8..16 | 356 | 712 + 16j | 304 | 16 | | 2x | box mon j: space, species name (10), two spaces, `<LV>` tile, level 3 cells (`wBoxSpecies`, `wBoxMons`) |
| status row 17 | 356 | 856 | 304 | 16 | | 2x | `..4 MORE` past nine, else the tenth mon |

Empty bag rows are blank. The `▶` cursor is not drawn: the bag cursor in
WRAM is only meaningful while the bag is open on the phone.

## View 3: Terrain

The current map from its real tileset, one block (4x4 tiles) = 64x64 px at
2x, a ten by eight block window (640x512) around the player, under it two
2x boxes: wild encounters on the left, map items on the right.

```
+----------------------------------------------------------------------+ 0
| TERRAIN   RED      TIME 123:45                       UPD 21:07:55    |
+----------------------------------------------------------------------+ 24
 VIRIDIAN FOREST  MAP 51  X12 Y34  BLK 17×24  WIN 7,13                   24 (caption)
 [ map window 10x8 blocks, 640x512 at 2x, x 20..660, y 40..552 ]         40
 [                                                               ]
 [                    [player sprite 32x32]                      ]
 [                                                               ]
 [                                                               ]       552
 +-------------------------------+--------------------------------+ 568
 |GRASS   8 PCT                  |ITEMS  2/4                      | 584
 |CATERPIE  L 3  40              |HPOTION      12,05              |
 |WEEDLE    L 3  20              | ANTIDOTE    04,11   (struck)   |
 |PIKACHU   L 3   5              | POKé BALL   16,27              |
 |...                            |                                |
 |                               |                                |
 |WATER   0 PCT                  |                                |
 |                               |                                |
 +-------------------------------+--------------------------------+ 888
+----------------------------------------------------------------------+ 896
| R:NEXT PAGE  L:BACK  U:HOME  C:EXIT                                  |
+----------------------------------------------------------------------+ 920
```

| Element | x | y | w | h | Tile scale | Font scale | Pack asset / source |
|---|---|---|---|---|---|---|---|
| Caption | 12 | 24 | 656 | 16 | | 2x | map name, two spaces, `MAP n`, `Xnn Ynn` (step coordinates), `BLK w×h` (map size in blocks), `WIN bx,by` (window origin in blocks, only when the map is larger than the window). Right hand fields are dropped in order WIN, BLK when the line would pass 41 cells. Sits directly under the bar with no gap; the caption is the reader's, not a box |
| Map window | 20 | 40 | 640 | 512 | 2x | | tileset tiles (2 bpp, `gfx/tilesets/*.png` via the pack), blockset (16 tile ids per block), map block layout; block (i, j) of the window at (20 + 64i, 40 + 64j). Window origin `(bx, by) = clamp(player_block - (5, 4), 0, map_size - (10, 8))`; a map smaller than the window in a dimension is centred in that dimension and the outside is filled with the map header's border block, exactly as the game fills its own screen edge |
| Map window, no tables | 20 | 40 | 640 | 832 | 2x | | when the map has neither a wild table nor items (interiors), the window grows to 10x13 blocks (40 to 872) and the two boxes are not drawn |
| Player marker | 20 + 32(px - 2bx) | 40 + 32(py - 2by) - 8 | 32 | 32 | 2x | | `red` overworld frame for the facing direction (`wSpritePlayerStateData1FacingDirection`), right = left mirrored, shade 0 transparent. Player step (px, py) is `wXCoord`, `wYCoord`; one step is two tiles = 32 px at 2x; the 8 px lift is the game's own sprite offset |
| Encounter box | 4 | 568 | 320 | 320 | 2x | | border tiles, 20x20 tiles; inner x 20..308 (18 cells), rows y = 584 + 16k, k 0..17 |
| grass header | 20 | 584 | 288 | 16 | | 2x | `GRASS  10 PCT` (`wGrassRate` * 100 / 256 rounded; `%` does not exist in the font) |
| grass rows | 20 | 600 + 16j | 288 | 16 | | 2x | species name 10 cells, space, `L` and level 2 cells right aligned, two spaces, odds 2 cells right aligned: `CATERPIE  L 3  40`. Slots with the same species and level are merged and their odds summed, so the list is at most 10 rows and usually 4 to 7 |
| water header | 20 | (after grass rows + 1 blank) | 288 | 16 | | 2x | `WATER  10 PCT`, only when `wWaterRate` is nonzero; rows as for grass |
| overflow row | 20 | 856 | 288 | 16 | | 2x | when grass + blank + water would exceed 18 rows, the lowest odds rows are dropped and the last row reads `..3 MORE` |
| Items box | 324 | 568 | 352 | 320 | 2x | | border tiles, 22x20 tiles; inner x 340..660 (20 cells), rows y = 584 + 16k |
| items header | 340 | 584 | 320 | 16 | | 2x | `ITEMS  2/4` (taken of total on this map) |
| item rows | 340 | 600 + 16j | 320 | 16 | | 2x | flag cell (`H` hidden, blank placed), name 12 cells, space, `xx,yy` step coordinates 2 digits each: `HPOTION      12,05`, ` ANTIDOTE    04,11`. Sources: placed items from the map's object list (`data/maps/objects/*.asm`, item objects) and their `wMissableObjectFlags` bit; hidden items from `data/events/hidden_objects.asm` and `wHiddenItemFlags`; both tables go into the pack keyed by map id |
| taken strike | 356 | 607 + 16j | 288 | 2 | | | 2 px black line across the name and coordinates of a taken item, rows 7 and 8 of the 16 px cell |
| items overflow | 340 | 856 | 320 | 16 | | 2x | `..5 MORE` past 17 |

Column rules when only one table exists: with no wild table the items box
takes the full width (x 4, w 672, inner 40 cells) as two item columns of 20
cells (x 20 and x 340), 34 items; with no items the encounter box takes the
full width and grass sits in the left 20 cells, water in the right 20, both
starting on row 0. With neither, the window grows as noted above.

## View 4: Battle

Shown automatically while `wIsInBattle` is 1 (wild) or 2 (trainer). The
game's own battle screen is enemy top left with the sprite top right, and the
player's back sprite bottom left with the HUD to its right; the same
arrangement, then the four moves, the party strip and the enemy's moves.

```
+----------------------------------------------------------------------+ 0
| BATTLE    RED      TIME 123:45                       UPD 21:07:55    |
+----------------------------------------------------------------------+ 24
 PIDGEOT      L: 57  PAR                            +----------------+  40
 HP:[=============       ] 123/145                  |                |  72
 NORMAL/FLYING    CATCH  45                         |  front sprite  | 104
 №018  OWNED                                        |  up to 56x56   | 128
 HP 83 ATK 80 DEF 75                                |  at 3x = 168   | 152
 SPD 91 SPC 70                                      |                | 176
 ───────────────────────────────────────────────────+----------------+  216
 +----------+  PIKACHU      L:100  PSN                                  240
 |          |  HP:[=================  ] 123/123                         272
 |  back    |  ELECTRIC           PIKACHU                               304
 |  32x32   |  ATK 120 DEF 67 SPD 199 SPC 111                           328
 +----------+                                                           344
 +--------------------------------------------------------------------+ 360
 | THUNDERBOLT   15/15                                          [×2]  | 384
 |  ELECTRIC  POW 95  ACC100  STAB                                    |
 | QUICK ATTACK  30/30                                          [   ] |
 |  NORMAL  POW 40  ACC100                                            |
 | ... four moves, 48 px pitch                                        |
 +--------------------------------------------------------------------+ 600
 +--------------------------------------------------------------------+ 600
 | [icon] PIKACHU   L:100 PSN   [icon] CHARIZARD L: 56                | 616
 |        HP:[====] 123/123            HP:[====]  90/210              |
 | ... three rows of two                                              |
 +--------------------------------------------------------------------+ 776
 | ENEMY MOVES                                                        | 792
 | WING ATTACK  FLYING    PP 35/35  POW 35  [×2]                      |
 | ... four rows                                                      |
 +--------------------------------------------------------------------+ 888
+----------------------------------------------------------------------+ 896
| R:NEXT PAGE  L:BACK  U:TERRAIN  C:EXIT                               |
+----------------------------------------------------------------------+ 920
```

| Element | x | y | w | h | Tile scale | Font scale | Pack asset / source |
|---|---|---|---|---|---|---|---|
| Enemy sprite slot | 492 | 40 | 168 | 168 | 3x | | front sprite of the enemy species (`wEnemyMon` species at `CFE5`): a sprite of side s px is drawn 3s wide at x = 492 + (168 - 3s) / 2, y = 208 - 3s, bottom aligned and centred, the game's own placement of small sprites in its 7x7 tile frame |
| enemy name | 20 | 40 | 240 | 24 | | 3x | `wEnemyMonNick` (10 cells); for a trainer's mon that is the species name, like the game |
| enemy level | 284 | 40 | 96 | 24 | 3x tile + 3x text | | `<LV>` tile at 284, level 3 cells at 308 (`wEnemyMonLevel`) |
| enemy status | 404 | 40 | 72 | 24 | | 3x | `SLP PSN BRN FRZ PAR` from `wEnemyMonStatus`, blank when none |
| enemy HP bar | 20 | 72 | 216 | 24 | 3x | | nine bar tiles, battle cap $6D, colour rule above |
| enemy HP numbers | 252 | 76 | 112 | 16 | | 2x | `123/145` from `wEnemyMonHP`, `wEnemyMonMaxHP`; the game hides these for the enemy, the companion shows them, inverted when red |
| enemy types | 20 | 104 | 240 | 16 | | 2x | `NORMAL/FLYING` from base stats, 15 cells |
| enemy catch or trainer | 276 | 104 | 192 | 16 | | 2x | wild: `CATCH 255` (base catch rate from base stats, 3 cells). Trainer: `<TRAINER>` text is not needed here, see the next row |
| enemy dex or trainer line | 20 | 128 | 448 | 16 | | 2x | wild: `№018  OWNED` / `№018  SEEN` / `№018  NEW` (`№` $74, dex number 3 digits, then the flag state from `wPokedexOwned` / `wPokedexSeen`). Trainer: `TRAINER LANCE  3/5 LEFT` (class name from `wTrainerClass` via the pack's trainer class names, `wTrainerName` when the class has a name, then mons not fainted of `wEnemyPartyCount`) |
| enemy base stats 1 | 20 | 152 | 448 | 16 | | 2x | `HP 83 ATK 80 DEF 75` (base stats, 2 to 3 digits, single spaces) |
| enemy base stats 2 | 20 | 176 | 448 | 16 | | 2x | `SPD 91 SPC 70` |
| Divider | 4 | 216 | 672 | 16 | 2x | | 42 `─` $7A tiles from `font_extra` |
| Player back sprite | 20 | 248 | 96 | 96 | 3x | | back sprite of `wBattleMon` species (`D014`), 32x32 at 3x |
| player name | 132 | 240 | 240 | 24 | | 3x | `wBattleMonNick` (10 cells) |
| player level | 396 | 240 | 96 | 24 | 3x tile + 3x text | | `<LV>` tile at 396, level 3 cells at 420 |
| player status | 516 | 240 | 72 | 24 | | 3x | status abbreviation |
| player HP bar | 132 | 272 | 216 | 24 | 3x | | nine bar tiles, battle cap $6D |
| player HP numbers | 364 | 272 | 168 | 24 | | 3x | `123/123`, 7 cells, inverted when red; 3x here because the player's own HP is the number people look at first |
| player types | 132 | 304 | 240 | 16 | | 2x | `ELECTRIC` or `WATER/FLYING` |
| player species | 388 | 304 | 160 | 16 | | 2x | species name (differs from the nickname) |
| player battle stats | 132 | 328 | 432 | 16 | | 2x | `ATK 120 DEF 67 SPD 199 SPC 111` from the battle struct's stat words (27 cells) |
| Moves box | 4 | 360 | 672 | 240 | 3x | | border tiles, 28x10 tiles; inner x 28..652; move rows top at y = 384 + 48m, m 0..3 |
| move name | 28 | 384 + 48m | 288 | 24 | | 3x | move name from the pack (`data/moves/names.asm`), 12 cells (`THUNDERBOLT`, `QUICK ATTACK`) |
| move PP | 340 | 384 + 48m | 120 | 24 | | 3x | `15/15` (`wBattleMon` PP byte current, max from the move table plus PP Ups in the PP byte's top bits), 5 cells |
| effectiveness badge | 580 | 384 + 48m | 72 | 24 | | 3x | three inverted cells: `×2 `, `×4 `, `1/2`, `1/4`, `×0 `; nothing drawn when neutral (1x). Value is the product of the type chart entries (`data/types/type_matchups.asm` in the pack) of the move's type against both enemy types; a status move (power 0) shows nothing |
| move detail | 28 | 408 + 48m | 528 | 16 | | 2x | type name (8 cells), two spaces, `POW 95` (3 cells right aligned), two spaces, `ACC100`, then `  STAB` when the move's type is one of the user's types: `ELECTRIC  POW 95  ACC100  STAB`, at most 30 cells so it never reaches the badge column |
| Party box | 4 | 600 | 672 | 176 | 2x | | border tiles, 42x11 tiles; inner x 20..660; rows top at y = 616 + 48r, r 0..2; two columns at cx = 20 and cx = 340 (20 cells each); party slot p = 2r + column |
| strip icon | cx | 616 + 48r | 32 | 32 | 2x | | party icon frame 0, 16x16 at 2x |
| strip name | cx + 40 | 616 + 48r | 160 | 16 | | 2x | nickname 10 cells; the mon that is `wPlayerMonNumber` is drawn inverted (white on black over its 10 cells) |
| strip level | cx + 200 | 616 + 48r | 64 | 16 | 2x tile + 2x text | | `<LV>` tile at cx + 200, level 3 cells at cx + 216 |
| strip status | cx + 272 | 616 + 48r | 48 | 16 | | 2x | `PAR` etc, `FNT` at 0 HP, ends exactly at the column edge |
| strip HP bar | cx + 40 | 632 + 48r | 144 | 16 | 2x | | nine bar tiles, party cap $6C |
| strip HP numbers | cx + 192 | 632 + 48r | 112 | 16 | | 2x | `123/123`, inverted when red |
| Enemy moves box | 4 | 776 | 672 | 112 | 2x | | border tiles, 42x7 tiles; inner x 20..660, rows y = 792 + 16k, k 0..4 |
| enemy moves header | 20 | 792 | 640 | 16 | | 2x | `ENEMY MOVES` |
| enemy move row | 20 | 808 + 16j | 640 | 16 | | 2x | name 12 cells at 20, type 8 cells at 228, `PP 15/15` at 372 (PP from `wEnemyMonPP`, unknown moves of a wild mon are still in the struct so they can be shown), `POW 95` at 516, effectiveness of that move against our active mon as a 3 cell inverted badge at 612..660 (same encoding as the moves box); empty move slots blank |

Trainer battles: the enemy sprite slot shows the trainer's current mon, and
row "enemy dex or trainer line" carries the trainer. The trainer class
sprite is not drawn anywhere (the sprite slot is the mon's). The `CATCH`
field is blank in a trainer battle.

## Refresh notes for the implementer

- Every view is composed in one frame (the reader's page buffer is the full
  panel). A state change inside a view redraws the whole frame through the
  partial path; a view switch, and the first battle frame, use a full
  refresh, as the plan says. The sprite and map areas are dense dither, so
  the ration of one full per ten partials that the reader already runs is
  kept.
- The 8 px white gap under the top bar and the white strip at 888 to 896
  are deliberate: they keep the black bands from fusing with a box border on
  a ghosted partial.
- Text inversion is done by filling the cell run black and drawing the 1 bpp
  glyphs with ink white; the font has no inverted variant.
- All the tile ids above are the game's VRAM ids only for reference; the
  pack sections carry tiles by index (font 0..127 = $80..$FF, font_extra
  0..31 = $60..$7F, font_battle_extra 0..29 = $62..$7F), and the generator's
  format doc (`docs/pokered-pack-format.md`) is the authority on section
  names.
