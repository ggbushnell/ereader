#!/usr/bin/env python3
"""Build ereader/build_pack/pokered.pack (EPK1) from a pokered checkout.

The reader's companion screen for Pokemon Red draws with the game's own
graphics and tables. This script turns a pret/pokered source checkout into one
flat asset pack the firmware reads through src/pack.cpp. The repo ships this
generator, never the assets.

Usage:
    python3 tools/pokered_pack.py [--src third_party/pokered] [--out build_pack]

Only Pillow is used, and only to decode PNGs. A pure zlib fallback covers the
case where Pillow is missing, because every pokered PNG is a plain
non-interlaced grayscale image.

The byte layout of every section is documented in docs/pokered-pack-format.md.
"""

import argparse

# constants/pokemon_data_constants.asm GROWTH_* order (GrowthRateTable rows).
GROWTH_IDS = {"MEDIUM_FAST": 0, "SLIGHTLY_FAST": 1, "SLIGHTLY_SLOW": 2,
              "MEDIUM_SLOW": 3, "FAST": 4, "SLOW": 5}
import os
import re
import struct
import sys
import zlib

# ---------------------------------------------------------------- PNG reading

try:
    from PIL import Image
    HAVE_PIL = True
except ImportError:
    HAVE_PIL = False


def read_png_gray(path):
    """Returns (width, height, list of rows, each row a list of 0..255 gray)."""
    if HAVE_PIL:
        im = Image.open(path).convert("L")
        w, h = im.size
        data = list(im.tobytes())
        return w, h, [data[y * w:(y + 1) * w] for y in range(h)]
    return _read_png_gray_pure(path)


def _read_png_gray_pure(path):
    raw = open(path, "rb").read()
    if raw[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("%s is not a PNG" % path)
    pos = 8
    idat = b""
    w = h = depth = color = 0
    interlace = 0
    palette = b""
    while pos < len(raw):
        ln = struct.unpack(">I", raw[pos:pos + 4])[0]
        typ = raw[pos + 4:pos + 8]
        body = raw[pos + 8:pos + 8 + ln]
        if typ == b"IHDR":
            w, h, depth, color, _comp, _filt, interlace = struct.unpack(
                ">IIBBBBB", body)
        elif typ == b"PLTE":
            palette = body
        elif typ == b"IDAT":
            idat += body
        elif typ == b"IEND":
            break
        pos += 12 + ln
    if interlace:
        raise ValueError("%s is interlaced, unsupported" % path)
    if color not in (0, 3):
        raise ValueError("%s has color type %d, unsupported" % (path, color))
    data = zlib.decompress(idat)

    samples = 1
    bpp_bytes = max(1, (depth * samples) // 8)
    stride = (w * depth * samples + 7) // 8
    out = []
    prev = bytearray(stride)
    p = 0
    for _y in range(h):
        ftype = data[p]
        p += 1
        line = bytearray(data[p:p + stride])
        p += stride
        for i in range(stride):
            a = line[i - bpp_bytes] if i >= bpp_bytes else 0
            b = prev[i]
            c = prev[i - bpp_bytes] if i >= bpp_bytes else 0
            x = line[i]
            if ftype == 1:
                x += a
            elif ftype == 2:
                x += b
            elif ftype == 3:
                x += (a + b) >> 1
            elif ftype == 4:
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                x += a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
            line[i] = x & 0xFF
        prev = line
        row = []
        if depth == 8:
            row = list(line[:w])
        else:
            per = 8 // depth
            mask = (1 << depth) - 1
            for i in range(w):
                byte = line[i // per]
                shift = 8 - depth * (i % per + 1)
                row.append((byte >> shift) & mask)
            if color == 0:
                scale = 255 // mask
                row = [v * scale for v in row]
        if color == 3:
            row = [palette[v * 3] for v in row]
        out.append(row)
    return w, h, out


# ------------------------------------------------------------ tile conversion

def gray_to_shade(v):
    """PNG gray sample to a Game Boy shade: 0 white .. 3 black."""
    return 3 - (v // 85)


class Sheet(object):
    """A decoded PNG as a shade grid, plus 8x8 tile access."""

    def __init__(self, path):
        w, h, rows = read_png_gray(path)
        self.path = path
        self.w = w
        self.h = h
        self.px = [[gray_to_shade(v) for v in row] for row in rows]
        self.cols = w // 8
        self.rows = h // 8
        self.tiles = self.cols * self.rows

    def tile_shades(self, index):
        """Row-major tile index to an 8x8 list of shade rows."""
        tx = (index % self.cols) * 8
        ty = (index // self.cols) * 8
        return [self.px[ty + r][tx:tx + 8] for r in range(8)]

    def rect_shades(self, x, y, w, h):
        return [self.px[y + r][x:x + w] for r in range(h)]


def enc_2bpp(shades):
    """8x8 shade rows to 16 bytes, GB planar, pixel 0 is bit 7."""
    out = bytearray()
    for row in shades:
        lo = hi = 0
        for col in range(8):
            s = row[col]
            bit = 7 - col
            lo |= (s & 1) << bit
            hi |= ((s >> 1) & 1) << bit
        out.append(lo)
        out.append(hi)
    return bytes(out)


def enc_1bpp(shades):
    """8x8 shade rows to 8 bytes, one bit per pixel, shade >= 2 is ink."""
    out = bytearray()
    for row in shades:
        byte = 0
        for col in range(8):
            if row[col] >= 2:
                byte |= 1 << (7 - col)
        out.append(byte)
    return bytes(out)


def rect_to_tiles_2bpp(shades, w, h):
    """A shade rectangle to row-major 8x8 tiles, 2 bpp."""
    out = bytearray()
    for ty in range(h // 8):
        for tx in range(w // 8):
            out += enc_2bpp([shades[ty * 8 + r][tx * 8:tx * 8 + 8]
                             for r in range(8)])
    return bytes(out)


def mirror(shades):
    return [list(reversed(row)) for row in shades]


BLANK8 = [[0] * 8 for _ in range(8)]


# ------------------------------------------------------------- asm parsing

COMMENT = re.compile(r";.*$")


def lines_of(path):
    """Source lines with comments stripped, macro bodies removed, blanks gone."""
    out = []
    in_macro = False
    for raw in open(path, encoding="utf-8").read().splitlines():
        s = COMMENT.sub("", raw).rstrip()
        if not s.strip():
            continue
        head = s.strip()
        if head.startswith("MACRO") or head.startswith("MACRO?"):
            in_macro = True
            continue
        if in_macro:
            if head == "ENDM":
                in_macro = False
            continue
        out.append(s)
    return out


def num(tok):
    """A rgbasm integer literal to a Python int."""
    tok = tok.strip()
    if tok.startswith("$"):
        return int(tok[1:], 16)
    if tok.startswith("%"):
        return int(tok[1:], 2)
    return int(tok, 0)


def parse_consts(path, start=0, macros=None):
    """Walks a constants file and returns {NAME: value} in declaration order.

    `macros` maps a macro name to a template for the constant it declares, for
    example {"map_const": "%s", "add_hm": "HM_%s"}.
    """
    macros = macros or {}
    value = start
    out = {}
    for line in lines_of(path):
        t = line.strip()
        parts = t.replace(",", " ").split()
        if not parts:
            continue
        op = parts[0]
        if op == "const_def":
            value = num(parts[1]) if len(parts) > 1 else 0
        elif op == "const":
            out[parts[1]] = value
            value += 1
        elif op == "const_skip":
            value += num(parts[1]) if len(parts) > 1 else 1
        elif op == "const_next":
            value = num(parts[1])
        elif op in macros:
            out[macros[op] % parts[1]] = value
            value += 1
    return out


def parse_string_list(path, macro="li"):
    """`li "TEXT"` entries in order."""
    out = []
    for line in lines_of(path):
        t = line.strip()
        if t.startswith(macro + " ") or t.startswith(macro + '\t'):
            m = re.search(r'"(.*)"', t)
            if m:
                out.append(m.group(1))
    return out


# Gen 1 glyphs that have no ASCII spelling. The pack's name tables are plain
# ASCII, so they are transliterated here and the firmware never sees them.
TRANSLIT = {
    "é": "e",    # POKeDEX
    "♂": "M",    # NIDORAN male
    "♀": "F",    # NIDORAN female
    "×": "x",
    "¥": "$",
    "’": "'",
}


def to_ascii(s):
    out = []
    for ch in s:
        out.append(TRANSLIT.get(ch, ch))
    return "".join(c if 32 <= ord(c) < 127 else "?" for c in "".join(out))


def strtab(strings):
    """u16 count, u16 offsets from section start, then zero terminated ASCII."""
    n = len(strings)
    head = 2 + 2 * n
    blob = bytearray()
    offs = []
    for s in strings:
        offs.append(head + len(blob))
        blob += to_ascii(s).encode("ascii", "replace") + b"\x00"
    out = bytearray(struct.pack("<H", n))
    for o in offs:
        out += struct.pack("<H", o)
    return bytes(out + blob)


# -------------------------------------------------------------- pack writer

MAX_SECTIONS = 48
MAX_NAME = 15


class Pack(object):
    def __init__(self):
        self.sections = []

    def add(self, name, data):
        if len(name) > MAX_NAME:
            raise ValueError("section name too long: %s" % name)
        if any(s[0] == name for s in self.sections):
            raise ValueError("duplicate section: %s" % name)
        self.sections.append((name, bytes(data)))

    def build(self):
        n = len(self.sections)
        if n > MAX_SECTIONS:
            raise ValueError("%d sections, the reader caps at %d" %
                             (n, MAX_SECTIONS))
        head = 8 + 24 * n
        toc = bytearray()
        blob = bytearray()
        for name, data in self.sections:
            toc += name.encode("ascii").ljust(16, b"\x00")
            toc += struct.pack("<II", head + len(blob), len(data))
            blob += data
        return bytes(b"EPK1" + struct.pack("<I", n) + toc + blob)


# ------------------------------------------------------------------- builder

class Builder(object):
    def __init__(self, src, version="red"):
        self.version = version
        self.src = src
        self.notes = []

    def p(self, *parts):
        return os.path.join(self.src, *parts)

    def sheet(self, *parts):
        return Sheet(self.p(*parts))

    def note(self, text):
        self.notes.append(text)

    # -- constants -------------------------------------------------------

    def load_constants(self):
        self.map_ids, self.indoor_groups, self.first_indoor, self.num_maps = \
            self.parse_map_constants()
        self.item_ids = parse_consts(
            self.p("constants/item_constants.asm"),
            macros={"add_hm": "HM_%s", "add_tm": "TM_%s"})
        self.move_ids = parse_consts(self.p("constants/move_constants.asm"))
        self.species_ids = parse_consts(
            self.p("constants/pokemon_constants.asm"))
        self.dex_ids = parse_consts(self.p("constants/pokedex_constants.asm"))
        self.type_ids = parse_consts(self.p("constants/type_constants.asm"))
        self.tileset_ids = parse_consts(
            self.p("constants/tileset_constants.asm"))
        self.icon_ids = parse_consts(self.p("constants/icon_constants.asm"))
        self.trainer_ids = parse_consts(
            self.p("constants/trainer_constants.asm"),
            macros={"trainer_const": "%s"})
        self.num_types = max(self.type_ids.values()) + 1

    def parse_map_constants(self):
        """Map name to id, indoor group name to threshold, and the boundaries."""
        path = self.p("constants/map_constants.asm")
        value = 0
        ids = {}
        groups = []
        first_indoor = None
        num_maps = None
        for line in lines_of(path):
            t = line.strip()
            parts = t.replace(",", " ").split()
            if not parts:
                continue
            if parts[0] == "const_def":
                value = num(parts[1]) if len(parts) > 1 else 0
            elif parts[0] == "map_const":
                ids[parts[1]] = value
                value += 1
            elif parts[0] == "end_indoor_group":
                groups.append((parts[1], value))
            elif t.startswith("DEF FIRST_INDOOR_MAP"):
                first_indoor = value
            elif t.startswith("DEF NUM_MAPS"):
                num_maps = value
        return ids, groups, first_indoor, num_maps

    # -- font ------------------------------------------------------------

    def build_font(self, pack):
        """One 256 tile 1 bpp page indexed by the Gen 1 character code."""
        main = self.sheet("gfx/font/font.png")          # 128 tiles at $80
        extra = self.sheet("gfx/font/font_extra.png")   # 32 tiles at $60
        page = [bytes(8)] * 256
        for i in range(128):
            page[0x80 + i] = enc_1bpp(main.tile_shades(i))
        for i in range(32):
            page[0x60 + i] = enc_1bpp(extra.tile_shades(i))
        font = b"".join(page)
        assert len(font) == 2048
        # The check the task asks for: glyph pixels must be ink. 'A' is $80.
        if not any(font[0x80 * 8:0x80 * 8 + 8]):
            raise ValueError("font tile for 'A' is blank, polarity is wrong")
        pack.add("font", font)

        pack.add("font_extra", b"".join(
            enc_1bpp(extra.tile_shades(i)) for i in range(32)))

        # The battle page overrides $62..$7F and really uses three shades
        # (the HP bar fill band is shade 2), so it stays 2 bpp.
        battle = self.sheet("gfx/font/font_battle_extra.png")
        tiles = [bytes(16)] * 32
        for i in range(battle.tiles):
            tiles[2 + i] = enc_2bpp(battle.tile_shades(i))
        pack.add("font_battle", b"".join(tiles))

        # Text box border: nine 1 bpp tiles in the order gbgfx::drawBox wants.
        # The game uses the same vertical rule tile on both sides.
        codes = [0x79, 0x7A, 0x7B, 0x7C, 0x7F, 0x7C, 0x7D, 0x7A, 0x7E]
        pack.add("border", b"".join(
            enc_1bpp(extra.tile_shades(c - 0x60)) for c in codes))

    # -- town map --------------------------------------------------------

    def build_town_map(self, pack):
        tiles = self.sheet("gfx/town_map/town_map.png")
        pack.add("townmap_tiles", b"".join(
            enc_2bpp(tiles.tile_shades(i)) for i in range(tiles.tiles)))

        rle = open(self.p("gfx/town_map/town_map.rle"), "rb").read()
        out = bytearray()
        for byte in rle:
            if byte == 0:
                break
            out += bytes([(byte >> 4) & 0xF]) * (byte & 0xF)
        if len(out) != 360:
            raise ValueError("town map RLE expanded to %d, expected 360" %
                             len(out))
        pack.add("townmap_map", out)

        cur = self.sheet("gfx/town_map/town_map_cursor.png")
        pack.add("townmap_cursor", rect_to_tiles_2bpp(
            cur.rect_shades(0, 0, 16, 16), 16, 16))

        self.build_town_map_entries(pack)

    def build_town_map_entries(self, pack):
        text = open(self.p("data/maps/town_map_entries.asm"),
                    encoding="utf-8").read()
        outdoor = []
        indoor = []
        for line in lines_of(self.p("data/maps/town_map_entries.asm")):
            t = line.strip()
            if t.startswith("outdoor_map"):
                a = [x.strip() for x in t[len("outdoor_map"):].split(",")]
                outdoor.append((num(a[0]), num(a[1]), a[2]))
            elif t.startswith("indoor_map"):
                a = [x.strip() for x in t[len("indoor_map"):].split(",")]
                indoor.append((a[0], num(a[1]), num(a[2]), a[3]))
        del text

        group_value = dict(self.indoor_groups)

        # The name labels, in first-seen order, become the map_names table.
        labels = []
        for _x, _y, lab in outdoor:
            if lab not in labels:
                labels.append(lab)
        for _g, _x, _y, lab in indoor:
            if lab not in labels:
                labels.append(lab)
        names = self.parse_map_name_strings()
        missing = [l for l in labels if l not in names]
        if missing:
            raise ValueError("town map name labels with no string: %s" %
                             ", ".join(missing))
        pack.add("map_names", strtab([names[l] for l in labels]))
        self.name_index = {lab: i for i, lab in enumerate(labels)}

        rec = bytearray()
        for map_id in range(self.num_maps):
            if map_id < self.first_indoor:
                if map_id < len(outdoor):
                    x, y, lab = outdoor[map_id]
                else:
                    x, y, lab = 0, 0, labels[0]
            else:
                hit = None
                for group, ex, ey, lab in indoor:
                    if map_id < group_value.get(group, 0):
                        hit = (ex, ey, lab)
                        break
                if hit is None:
                    hit = (indoor[-1][1], indoor[-1][2], indoor[-1][3])
                x, y, lab = hit
            rec += bytes([x & 0xFF, y & 0xFF, self.name_index[lab]])
        pack.add("townmap_entries", rec)

    def parse_map_name_strings(self):
        out = {}
        for line in lines_of(self.p("data/maps/names.asm")):
            m = re.match(r'\s*(\w+):\s*db\s*"(.*)@"', line)
            if m:
                out[m.group(1)] = m.group(2)
        return out

    # -- map labels ------------------------------------------------------

    KEEP_UPPER = {"SS", "HQ", "PC", "TM", "HM", "B1F", "B2F", "B3F", "B4F"}

    def pretty_map_name(self, const):
        words = []
        for tok in const.split("_"):
            if not tok:
                continue
            if tok in self.KEEP_UPPER or any(c.isdigit() for c in tok):
                words.append(tok)
            else:
                words.append(tok[0] + tok[1:].lower())
        return " ".join(words)

    def build_map_labels(self, pack):
        by_id = {v: k for k, v in self.map_ids.items()}
        out = []
        for map_id in range(self.num_maps):
            const = by_id.get(map_id)
            out.append(self.pretty_map_name(const) if const else "")
        pack.add("map_labels", strtab(out))

    # -- badges ----------------------------------------------------------

    def build_badges(self, pack):
        sheet = self.sheet("gfx/trainer_card/badges.png")
        faces = bytearray()
        badges = bytearray()
        for k in range(8):
            faces += rect_to_tiles_2bpp(
                sheet.rect_shades(0, 32 * k, 16, 16), 16, 16)
            badges += rect_to_tiles_2bpp(
                sheet.rect_shades(0, 32 * k + 16, 16, 16), 16, 16)
        pack.add("badges", badges)
        pack.add("leader_faces", faces)

        numbers = self.sheet("gfx/trainer_card/badge_numbers.png")
        pack.add("badge_numbers", b"".join(
            enc_2bpp(numbers.tile_shades(i)) for i in range(numbers.tiles)))

    # -- party icons -----------------------------------------------------

    # Icon id to (png, top tile, bottom tile) for the symmetric icons, whose
    # right half the game produces with the OAM x-flip bit. The helix is the
    # one asymmetric icon and carries four real tiles.
    ICON_SOURCES = {
        "ICON_MON": ("gfx/sprites/monster.png", 12, 14),
        "ICON_BALL": ("gfx/sprites/poke_ball.png", 0, 2),
        "ICON_FAIRY": ("gfx/sprites/fairy.png", 12, 14),
        "ICON_BIRD": ("gfx/sprites/bird.png", 12, 14),
        "ICON_WATER": ("gfx/sprites/seel.png", 0, 2),
        "ICON_BUG": ("gfx/icons/bug.png", 2, 3),
        "ICON_GRASS": ("gfx/icons/plant.png", 2, 3),
        "ICON_SNAKE": ("gfx/icons/snake.png", 0, 1),
        "ICON_QUADRUPED": ("gfx/icons/quadruped.png", 0, 1),
        "ICON_TRADEBUBBLE": ("gfx/trade/bubble.png", 0, 2),
    }
    ICON_HELIX_PNG = "gfx/sprites/fossil.png"

    def icon_tiles(self, name):
        """Four 2 bpp tiles, row-major, for one 16x16 party icon."""
        if name == "ICON_HELIX":
            sheet = self.sheet(self.ICON_HELIX_PNG)
            quads = [sheet.tile_shades(i) for i in range(4)]
        else:
            path, top, bottom = self.ICON_SOURCES[name]
            sheet = self.sheet(path)
            a = sheet.tile_shades(top)
            b = sheet.tile_shades(bottom)
            quads = [a, mirror(a), b, mirror(b)]
        return b"".join(enc_2bpp(q) for q in quads)

    def build_icons(self, pack):
        count = max(self.icon_ids.values()) + 1
        blob = bytearray(struct.pack("<HH", count, 4))
        for icon_id in range(count):
            name = None
            for k, v in self.icon_ids.items():
                if v == icon_id:
                    name = k
            if name and (name == "ICON_HELIX" or name in self.ICON_SOURCES):
                blob += self.icon_tiles(name)
            else:
                blob += bytes(64)
        pack.add("icons", blob)

        # Species to icon id, one byte per dex number, packed nybbles unpacked.
        nybbles = []
        for line in lines_of(self.p("data/pokemon/menu_icons.asm")):
            t = line.strip()
            if t.startswith("nybble "):
                nybbles.append(self.icon_ids[t.split()[1]])
        if len(nybbles) < 151:
            raise ValueError("menu_icons has %d entries" % len(nybbles))
        pack.add("icon_map", bytes(nybbles[:151]))

    # -- pokemon sprites -------------------------------------------------

    def species_files(self):
        """Dex number to the base file stem used by the sprite PNGs."""
        out = {}
        for fn in sorted(os.listdir(self.p("data/pokemon/base_stats"))):
            if not fn.endswith(".asm"):
                continue
            stem = fn[:-4]
            text = open(self.p("data/pokemon/base_stats", fn),
                        encoding="utf-8").read()
            m = re.search(r"db\s+(DEX_\w+)", text)
            dex = self.dex_ids[m.group(1)]
            out[dex] = stem
        return out

    FILE_FIXUPS = {"mrmime": "mr.mime"}

    def build_sprites(self, pack):
        stems = self.species_files()
        front = bytearray()
        front_index = bytearray()
        back = bytearray()
        back_index = bytearray()
        self.sprite_size = {}
        for dex in range(1, 152):
            stem = self.FILE_FIXUPS.get(stems[dex], stems[dex])
            f = self.sheet("gfx/pokemon/front", stem + ".png")
            if f.w != f.h or f.w % 8:
                raise ValueError("front sprite %s is %dx%d" % (stem, f.w, f.h))
            side = f.w // 8
            self.sprite_size[dex] = side
            front_index += struct.pack("<IB", len(front), side)
            front += rect_to_tiles_2bpp(f.rect_shades(0, 0, f.w, f.h),
                                        f.w, f.h)
            b = self.sheet("gfx/pokemon/back", stem + "b.png")
            back_index += struct.pack("<IB", len(back), b.w // 8)
            back += rect_to_tiles_2bpp(b.rect_shades(0, 0, b.w, b.h),
                                       b.w, b.h)
        pack.add("front", front)
        pack.add("front_index", front_index)
        pack.add("back", back)
        pack.add("back_index", back_index)

    def build_player_walk(self, pack):
        red = self.sheet("gfx/sprites/red.png")
        out = bytearray()
        for frame in range(3):   # down, up, left; right is left mirrored
            out += rect_to_tiles_2bpp(
                red.rect_shades(0, frame * 16, 16, 16), 16, 16)
        pack.add("player_walk", out)

    # -- tilesets, blocksets, maps ---------------------------------------

    def parse_tileset_gfx_map(self):
        """Tileset label to (png stem, bst stem), following the shared labels."""
        gfx = {}
        blocks = {}
        pending_g = []
        pending_b = []
        for line in lines_of(self.p("gfx/tilesets.asm")):
            t = line.strip()
            m = re.match(r'(\w+)_GFX::\s*INCBIN\s*"gfx/tilesets/(\w+)\.2bpp"', t)
            if m:
                for label in pending_g + [m.group(1)]:
                    gfx[label] = m.group(2)
                pending_g = []
                continue
            m = re.match(r'(\w+)_Block::\s*INCBIN\s*"gfx/blocksets/(\w+)\.bst"',
                         t)
            if m:
                for label in pending_b + [m.group(1)]:
                    blocks[label] = m.group(2)
                pending_b = []
                continue
            m = re.match(r"(\w+)_GFX::$", t)
            if m:
                pending_g.append(m.group(1))
                continue
            m = re.match(r"(\w+)_Block::$", t)
            if m:
                pending_b.append(m.group(1))
        return gfx, blocks

    def build_tilesets(self, pack):
        labels = []
        for line in lines_of(self.p("data/tilesets/tileset_headers.asm")):
            t = line.strip()
            if t.startswith("tileset "):
                labels.append(t.split()[1].rstrip(","))
        gfx, blocks = self.parse_tileset_gfx_map()

        tiles_blob = bytearray()
        tiles_index = bytearray()
        tiles_at = {}
        block_blob = bytearray()
        block_index = bytearray()
        block_at = {}
        for label in labels:
            stem = gfx[label]
            if stem not in tiles_at:
                sheet = self.sheet("gfx/tilesets", stem + ".png")
                data = b"".join(enc_2bpp(sheet.tile_shades(i))
                                for i in range(sheet.tiles))
                tiles_at[stem] = (len(tiles_blob), sheet.tiles)
                tiles_blob += data
            off, count = tiles_at[stem]
            tiles_index += struct.pack("<IH", off, count)

            bstem = blocks[label]
            if bstem not in block_at:
                data = open(self.p("gfx/blocksets", bstem + ".bst"), "rb").read()
                block_at[bstem] = (len(block_blob), len(data) // 16)
                block_blob += data
            off, count = block_at[bstem]
            block_index += struct.pack("<IH", off, count)

        pack.add("tilesets", tiles_blob)
        pack.add("tileset_index", tiles_index)
        pack.add("blocksets", block_blob)
        pack.add("blockset_index", block_index)
        self.tileset_count = len(labels)

    def parse_map_blk_files(self):
        """Header label to the .blk file that supplies its blocks."""
        out = {}
        pending = []
        for line in lines_of(self.p("maps.asm")):
            t = line.strip()
            m = re.match(r'(\w+)_Blocks:\s*INCBIN\s*"maps/(\w+)\.blk"', t)
            if m:
                for label in pending + [m.group(1)]:
                    out[label] = m.group(2)
                pending = []
                continue
            m = re.match(r"(\w+)_Blocks:$", t)
            if m:
                pending.append(m.group(1))
        return out

    def parse_map_headers(self):
        """Map id to {label, tileset id}.

        The map id comes from MapHeaderPointers, which is the authoritative
        one-entry-per-map-id table, not from the `map_header` macro's own map
        constant. Two headers in this checkout claim UNDERGROUND_PATH_ROUTE_7
        (the second is really the _COPY map), and unused map ids deliberately
        alias a real header, both of which the pointer table gets right.
        """
        tileset_of = {}
        self.header_const = {}
        d = self.p("data/maps/headers")
        for fn in sorted(os.listdir(d)):
            if not fn.endswith(".asm"):
                continue
            for line in lines_of(os.path.join(d, fn)):
                t = line.strip()
                if t.startswith("map_header "):
                    a = [x.strip() for x in t[len("map_header"):].split(",")]
                    tileset_of[a[0]] = self.tileset_ids[a[2]]
                    self.header_const[a[0]] = a[1]

        out = {}
        map_id = 0
        for line in lines_of(self.p("data/maps/map_header_pointers.asm")):
            m = re.match(r"\s*dw\s+(\w+)_h$", line)
            if not m:
                continue
            label = m.group(1)
            out[map_id] = {"label": label, "tileset": tileset_of[label]}
            map_id += 1
        return out

    def parse_map_objects(self):
        """Header label to {border, consts, items} from data/maps/objects."""
        out = {}
        d = self.p("data/maps/objects")
        for fn in sorted(os.listdir(d)):
            if not fn.endswith(".asm"):
                continue
            consts = []
            border = 0
            items = []
            label = None
            obj_index = 0
            for line in lines_of(os.path.join(d, fn)):
                t = line.strip()
                if t.startswith("const_export ") or (
                        t.startswith("const ") and label is None):
                    consts.append(t.split()[1])
                    continue
                m = re.match(r"(\w+)_Object:", t)
                if m:
                    label = m.group(1)
                    continue
                m = re.match(r"db\s+(\S+)\s*$", t)
                if m and label and border == 0 and not items:
                    try:
                        border = num(m.group(1))
                    except ValueError:
                        pass
                    continue
                if t.startswith("object_event"):
                    a = [x.strip()
                         for x in t[len("object_event"):].split(",")]
                    obj_index += 1
                    if len(a) == 7:   # x, y, sprite, move, dir, text, item
                        items.append({
                            "x": num(a[0]),
                            "y": num(a[1]),
                            "item": a[6],
                            "object": obj_index,
                        })
            if label:
                out[label] = {"border": border, "consts": consts,
                              "items": items}
        return out

    def build_maps(self, pack):
        headers = self.parse_map_headers()
        blk_of = self.parse_map_blk_files()
        objects = self.parse_map_objects()
        self.map_headers = headers
        self.map_objects = objects

        blob = bytearray()
        at = {}
        index = bytearray()
        for map_id in range(self.num_maps):
            h = headers.get(map_id)
            if not h:
                index += struct.pack("<IBBBB", 0, 0, 0, 0, 0)
                continue
            stem = blk_of[h["label"]]
            if stem not in at:
                data = open(self.p("maps", stem + ".blk"), "rb").read()
                at[stem] = len(blob)
                blob += data
            # Dimensions come from the header's own map constant, so an unused
            # map id that aliases a real header gets that header's size.
            w, ht = self.map_dims[self.header_const[h["label"]]]
            border = objects.get(h["label"], {}).get("border", 0)
            index += struct.pack("<IBBBB", at[stem], w, ht, h["tileset"],
                                 border)
        pack.add("maps", blob)
        pack.add("map_index", index)

    def parse_map_dims(self):
        out = {}
        for line in lines_of(self.p("constants/map_constants.asm")):
            t = line.strip()
            if t.startswith("map_const"):
                a = [x.strip() for x in t[len("map_const"):].split(",")]
                out[a[0]] = (num(a[1]), num(a[2]))
        self.map_dims = out

    # -- data tables -----------------------------------------------------

    def build_tables(self, pack):
        # Species names and base stats, both keyed by dex number.
        internal_names = []
        for line in lines_of(self.p("data/pokemon/names.asm")):
            m = re.search(r'dname\s+"(.*)"', line)
            if m:
                internal_names.append(m.group(1))
        dex_order = []
        for line in lines_of(self.p("data/pokemon/dex_order.asm")):
            t = line.strip()
            if t.startswith("db "):
                arg = t[3:].strip()
                dex_order.append(0 if arg == "0" else self.dex_ids[arg])
        pack.add("dex_order", bytes(dex_order))

        by_dex = {}
        for i, dex in enumerate(dex_order):
            if dex and dex not in by_dex:
                by_dex[dex] = internal_names[i]
        pack.add("species_names", strtab([by_dex[d] for d in range(1, 152)]))

        stems = self.species_files()
        stats = bytearray()
        growth = []
        for dex in range(1, 152):
            text = open(self.p("data/pokemon/base_stats", stems[dex] + ".asm"),
                        encoding="utf-8").read()
            body = [COMMENT.sub("", l).strip() for l in text.splitlines()]
            body = [l for l in body if l]
            nums = re.search(
                r"db\s+(\d+),\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+)", text)
            hp, atk, df, spd, spc = [int(x) for x in nums.groups()]
            tm = re.search(r"db\s+(\w+),\s*(\w+)\s*;\s*type", text)
            t1 = self.type_ids[tm.group(1)]
            t2 = self.type_ids[tm.group(2)]
            cm = re.search(r"db\s+(\d+)\s*;\s*catch rate", text)
            catch = int(cm.group(1))
            stats += bytes([hp, atk, df, spd, spc, t1, t2, catch,
                            self.sprite_size[dex]])
            gm = re.search(r"db\s+GROWTH_(\w+)\s*;\s*growth rate", text)
            growth.append(GROWTH_IDS[gm.group(1)])
        pack.add("base_stats", stats)
        # Growth rate per dex number, as the GROWTH_* index into the game's
        # GrowthRateTable (data/growth_rates.asm), for experience-to-next-level.
        pack.add("growth", bytes(growth))
        self.build_palettes(pack)

        # Items, indexed straight by item id so the sparse TM and HM ids work.
        names = parse_string_list(self.p("data/items/names.asm"))
        prices = []
        for line in lines_of(self.p("data/items/prices.asm")):
            t = line.strip()
            if t.startswith("bcd3 "):
                prices.append(int(t.split()[1]))
        tm_prices = []
        for line in lines_of(self.p("data/items/tm_prices.asm")):
            t = line.strip()
            if t.startswith("nybble "):
                tm_prices.append(int(t.split()[1]) * 1000)

        item_names = [""] * 256
        item_prices = [0] * 256
        for i, nm in enumerate(names):
            item_names[i + 1] = nm
        for i, pr in enumerate(prices):
            item_prices[i + 1] = pr
        for n in range(1, 6):
            hid = self.item_ids["HM_" + self.hm_move(n)]
            item_names[hid] = "HM%02d" % n
        for n in range(1, 51):
            tid = self.item_ids["TM_" + self.tm_move(n)]
            item_names[tid] = "TM%02d" % n
            item_prices[tid] = tm_prices[n - 1] if n - 1 < len(tm_prices) else 0
        pack.add("item_names", strtab(item_names))
        pack.add("item_prices",
                 b"".join(struct.pack("<H", min(p, 0xFFFF))
                          for p in item_prices))

        # Moves.
        move_names = parse_string_list(self.p("data/moves/names.asm"))
        pack.add("move_names", strtab(move_names))
        moves = bytearray()
        for line in lines_of(self.p("data/moves/moves.asm")):
            t = line.strip()
            if not t.startswith("move "):
                continue
            a = [x.strip() for x in t[len("move"):].split(",")]
            power = int(a[2])
            mtype = self.type_ids[a[3]]
            acc = int(a[4].split()[0])
            pp = int(a[5])
            moves += bytes([mtype, power, acc, pp])
        pack.add("move_data", moves)

        # Types and the matchup chart.
        pack.add("type_names", strtab(self.type_names()))
        chart = bytearray()
        for line in lines_of(self.p("data/types/type_matchups.asm")):
            t = line.strip()
            m = re.match(r"db\s+(\w+),\s*(\w+),\s*(\w+)$", t)
            if not m:
                continue
            mult = {"SUPER_EFFECTIVE": 20, "MORE_EFFECTIVE": 15,
                    "EFFECTIVE": 10, "NOT_VERY_EFFECTIVE": 5,
                    "NO_EFFECT": 0}.get(m.group(3))
            if mult is None:
                continue
            chart += bytes([self.type_ids[m.group(1)],
                            self.type_ids[m.group(2)], mult])
        pack.add("type_chart", chart)

        # Trainer class names, index = class id - 1.
        pack.add("trainer_class",
                 strtab(parse_string_list(self.p("data/trainers/names.asm"))))

    def hm_move(self, n):
        for name, _v in self.hm_tm_order("add_hm"):
            if _v == n:
                return name
        raise KeyError(n)

    def tm_move(self, n):
        for name, _v in self.hm_tm_order("add_tm"):
            if _v == n:
                return name
        raise KeyError(n)

    def hm_tm_order(self, macro):
        key = "_order_" + macro
        if not hasattr(self, key):
            out = []
            i = 0
            for line in lines_of(self.p("constants/item_constants.asm")):
                t = line.strip()
                if t.startswith(macro + " "):
                    i += 1
                    out.append((t.split()[1], i))
            setattr(self, key, out)
        return getattr(self, key)

    def type_names(self):
        path = self.p("data/types/names.asm")
        order = []
        strings = {}
        in_rept = False
        for line in lines_of(path):
            t = line.strip()
            if in_rept:
                if t == "ENDR":
                    in_rept = False
                continue
            m = re.match(r"dw\s+\.(\w+)$", t)
            if m:
                order.append(m.group(1))
                continue
            m = re.match(r"REPT\s+(\S+)\s*-\s*(\S+)", t)
            if m:
                # The unused type gap repeats .Normal. Its length is the
                # distance between the two unused-type boundary constants.
                in_rept = True
                gap = (self.type_ids.get("FIRE", 0x14) -
                       self.type_ids.get("GHOST", 0x08) - 1)
                order.extend(["Normal"] * gap)
                continue
            m = re.match(r'\.(\w+):\s*db\s*"(.*)@"', t)
            if m:
                strings[m.group(1)] = m.group(2)
        out = [strings[o] for o in order]
        if len(out) != self.num_types:
            raise ValueError("type names: %d entries, expected %d" %
                             (len(out), self.num_types))
        return out

    # -- Super Game Boy palettes -----------------------------------------
    #
    # The SGB-enhanced ROM colours each species' picture with one of ten
    # four-colour palettes, and areas, the town map and the trainer card with
    # others. constants/palette_constants.asm gives the PAL_* order,
    # data/sgb/sgb_palettes.asm the colours (5 bits per channel) and
    # data/pokemon/palettes.asm the species -> palette table in dex order
    # (entry 0 is MissingNo). Stored so a colour front end tints exactly as
    # the game would.

    def build_palettes(self, pack):
        names = []
        block = 0
        for line in lines_of(self.p("constants/palette_constants.asm")):
            t = line.strip()
            if t.startswith("const_def"):
                block += 1
                continue
            m = re.match(r"const\s+(PAL_\w+)", t)
            if m and block == 2:
                names.append(m.group(1))
        rgb_by_name = {}
        # the palette's name is only in the trailing comment, so read raw lines
        with open(self.p("data/sgb/sgb_palettes.asm"), encoding="utf-8") as fd:
            raw_lines = fd.read().splitlines()
        for line in raw_lines:
            m = re.match(r"\s*RGB\s+([\d,\s]+);\s*(PAL_\w+)", line)
            if not m:
                continue
            vals = [int(x) for x in m.group(1).replace(" ", "").strip(",").split(",")]
            if len(vals) == 12:
                rgb_by_name[m.group(2)] = vals
        missing = [n for n in names if n not in rgb_by_name]
        if missing:
            raise ValueError("palettes without colours: %s" % ", ".join(missing))
        blob = bytearray()
        for n in names:
            blob += bytes(v * 255 // 31 for v in rgb_by_name[n])
        pack.add("sgb_palettes", bytes(blob))
        pack.add("sgb_pal_names", strtab(names))
        mon = []
        for line in lines_of(self.p("data/pokemon/palettes.asm")):
            m = re.match(r"\s*db\s+(PAL_\w+)", line)
            if m:
                mon.append(names.index(m.group(1)))
        if len(mon) != 152:
            raise ValueError("expected 152 species palettes, got %d" % len(mon))
        pack.add("mon_palettes", bytes(mon))

    # -- wild encounters -------------------------------------------------

    def build_wild(self, pack):
        pointers = []
        for line in lines_of(self.p("data/wild/grass_water.asm")):
            t = line.strip()
            m = re.match(r"dw\s+(\w+)$", t)
            if m:
                pointers.append(m.group(1))
        pointers = pointers[:self.num_maps]

        records = {}
        d = self.p("data/wild/maps")
        for fn in sorted(os.listdir(d)):
            if not fn.endswith(".asm"):
                continue
            label = None
            grass_rate = water_rate = 0
            grass = []
            water = []
            mode = None
            # Version-split tables: `IF DEF(_RED)` ... `ENDC` and the Blue
            # twin. Only the block for the version being packed is read;
            # lines outside any block (the shared Pikachu slots in Viridian
            # Forest, say) always are. 34 maps split this way.
            active = True
            for line in lines_of(os.path.join(d, fn)):
                t = line.strip()
                m = re.match(r"IF\s+DEF\((_\w+)\)", t)
                if m:
                    active = m.group(1).lower() == "_" + self.version
                    continue
                if t.startswith("ELSE"):
                    active = not active
                    continue
                if t.startswith("ENDC"):
                    active = True
                    continue
                if not active:
                    continue
                m = re.match(r"(\w+):$", t)
                if m:
                    label = m.group(1)
                    continue
                if t.startswith("def_grass_wildmons"):
                    grass_rate = num(t.split()[1])
                    mode = "g"
                elif t.startswith("def_water_wildmons"):
                    water_rate = num(t.split()[1])
                    mode = "w"
                elif t.startswith("db "):
                    a = [x.strip() for x in t[3:].split(",")]
                    slot = (int(a[0]), self.species_ids[a[1]])
                    (grass if mode == "g" else water).append(slot)
            if label:
                records[label] = (grass_rate, grass, water_rate, water)

        def pack_side(rate, slots):
            out = bytearray([rate])
            slots = slots[:10] if rate else []
            for i in range(10):
                if i < len(slots):
                    out += bytes([slots[i][0], slots[i][1]])
                else:
                    out += b"\x00\x00"
            return out

        blob = bytearray()
        for map_id in range(self.num_maps):
            label = pointers[map_id] if map_id < len(pointers) else "NothingWildMons"
            gr, g, wr, w = records.get(label, (0, [], 0, []))
            blob += pack_side(gr, g)
            blob += pack_side(wr, w)
        pack.add("wild", blob)
        self.wild_records = records
        self.wild_pointers = pointers

    # -- items on the ground ---------------------------------------------

    def build_items(self, pack):
        # Placed item balls. The global toggleable index is the row number in
        # ToggleableObjectStates, which is what the game's flag bit indexes.
        toggle_index = {}
        row = 0
        current = None
        for line in lines_of(self.p("data/maps/toggleable_objects.asm")):
            t = line.strip()
            if t.startswith("toggleable_objects_for"):
                current = t.split()[1]
                continue
            if t.startswith("toggle_object_state"):
                a = [x.strip() for x in t[len("toggle_object_state"):].split(",")]
                toggle_index[(current, a[0])] = row
                row += 1
        self.toggle_rows = row

        # A header label maps back to its own declared map constant, not to
        # whichever id aliased it in MapHeaderPointers.
        by_label = {label: self.map_ids[const]
                    for label, const in self.header_const.items()}
        by_id = {v: k for k, v in self.map_ids.items()}

        placed = bytearray()
        self.placed_count = 0
        for label, info in sorted(self.map_objects.items()):
            if label not in by_label:
                continue
            map_id = by_label[label]
            map_const = by_id[map_id]
            for it in info["items"]:
                item_id = self.item_ids.get(it["item"])
                if item_id is None:
                    continue
                const = None
                if it["object"] - 1 < len(info["consts"]):
                    const = info["consts"][it["object"] - 1]
                idx = toggle_index.get((map_const, const), 0xFF)
                placed += bytes([map_id, it["x"], it["y"], item_id, idx])
                self.placed_count += 1
        pack.add("items_placed", placed)

        # Hidden items. The flag index is the row number in HiddenItemCoords.
        coords = []
        for line in lines_of(self.p("data/events/hidden_item_coords.asm")):
            t = line.strip()
            if t.startswith("hidden_item "):
                a = [x.strip() for x in t[len("hidden_item"):].split(",")]
                coords.append((self.map_ids[a[0]], num(a[1]), num(a[2])))

        events = {}
        current = None
        for line in lines_of(self.p("data/events/hidden_events.asm")):
            t = line.strip()
            if t.startswith("hidden_events_for"):
                current = self.map_ids[t.split()[1]]
                continue
            if t.startswith("hidden_event "):
                a = [x.strip() for x in t[len("hidden_event"):].split(",")]
                if a[2] != "HiddenItems":
                    continue
                events[(current, num(a[0]), num(a[1]))] = a[3]

        hidden = bytearray()
        self.hidden_count = 0
        unmatched = 0
        for flag, (map_id, x, y) in enumerate(coords):
            item = events.get((map_id, x, y))
            if item is None:
                unmatched += 1
                continue
            item_id = self.item_ids.get(item, 0)
            hidden += bytes([map_id, x, y, item_id, flag])
            self.hidden_count += 1
        if unmatched:
            self.note("%d hidden item coords had no HiddenItems event" %
                      unmatched)
        pack.add("items_hidden", hidden)

    # -- top level -------------------------------------------------------

    def build(self):
        pack = Pack()
        self.load_constants()
        self.parse_map_dims()
        self.build_font(pack)
        self.build_town_map(pack)
        self.build_map_labels(pack)
        self.build_badges(pack)
        self.build_icons(pack)
        self.build_sprites(pack)
        self.build_player_walk(pack)
        self.build_tilesets(pack)
        self.build_maps(pack)
        self.build_tables(pack)
        self.build_wild(pack)
        self.build_items(pack)
        return pack


# ------------------------------------------------------------------ previews

def write_preview(path, shade_rows, scale=3):
    """Writes a shade grid to a grayscale PNG, 0 white .. 3 black."""
    h = len(shade_rows)
    w = len(shade_rows[0]) if h else 0
    lut = [255, 170, 85, 0]
    raw = bytearray()
    for row in shade_rows:
        line = bytearray()
        for s in row:
            line += bytes([lut[s]]) * scale
        for _ in range(scale):
            raw += b"\x00" + line

    def chunk(typ, body):
        return (struct.pack(">I", len(body)) + typ + body +
                struct.pack(">I", zlib.crc32(typ + body) & 0xFFFFFFFF))

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", w * scale, h * scale,
                                      8, 0, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(raw), 9))
    png += chunk(b"IEND", b"")
    open(path, "wb").write(png)


def tiles_to_shades(data, tiles_wide, count, bpp=2):
    """Decoded pack tile bytes back to a shade grid, row-major tiles."""
    step = 16 if bpp == 2 else 8
    rows = (count + tiles_wide - 1) // tiles_wide
    grid = [[0] * (tiles_wide * 8) for _ in range(rows * 8)]
    for i in range(count):
        tx = (i % tiles_wide) * 8
        ty = (i // tiles_wide) * 8
        tile = data[i * step:(i + 1) * step]
        for r in range(8):
            if bpp == 2:
                lo, hi = tile[r * 2], tile[r * 2 + 1]
            else:
                lo, hi = tile[r], tile[r]
            for c in range(8):
                bit = 7 - c
                if bpp == 2:
                    s = ((lo >> bit) & 1) | (((hi >> bit) & 1) << 1)
                else:
                    s = 3 if (lo >> bit) & 1 else 0
                grid[ty + r][tx + c] = s
    return grid


def sections_of(blob):
    n = struct.unpack("<I", blob[4:8])[0]
    out = {}
    for i in range(n):
        rec = blob[8 + 24 * i:8 + 24 * (i + 1)]
        name = rec[:16].rstrip(b"\x00").decode("ascii")
        off, ln = struct.unpack("<II", rec[16:24])
        out[name] = blob[off:off + ln]
    return out


def make_previews(blob, out_dir, builder):
    os.makedirs(out_dir, exist_ok=True)
    s = sections_of(blob)

    write_preview(os.path.join(out_dir, "font.png"),
                  tiles_to_shades(s["font"], 16, 256, bpp=1), scale=2)

    # Town map: 20x18 cells of the 16 town map tiles.
    tiles = s["townmap_tiles"]
    layout = s["townmap_map"]
    grid = [[0] * (20 * 8) for _ in range(18 * 8)]
    for cell in range(360):
        t = layout[cell]
        cx, cy = (cell % 20) * 8, (cell // 20) * 8
        one = tiles_to_shades(tiles[t * 16:(t + 1) * 16], 1, 1)
        for r in range(8):
            for c in range(8):
                grid[cy + r][cx + c] = one[r][c]
    write_preview(os.path.join(out_dir, "town_map.png"), grid, scale=3)

    # One front sprite: dex 25, Pikachu.
    idx = s["front_index"]
    off, side = struct.unpack("<IB", idx[(25 - 1) * 5:(25 - 1) * 5 + 5])
    data = s["front"][off:off + side * side * 16]
    write_preview(os.path.join(out_dir, "front_pikachu.png"),
                  tiles_to_shades(data, side, side * side), scale=4)

    write_preview(os.path.join(out_dir, "player_walk.png"),
                  tiles_to_shades(s["player_walk"], 2, 12), scale=5)

    write_preview(os.path.join(out_dir, "badges.png"),
                  tiles_to_shades(s["badges"], 2, 32), scale=3)

    # Pallet Town from its tileset, blockset and block layout.
    mi = s["map_index"]
    map_id = builder.map_ids["PALLET_TOWN"]
    off, w, h, tset, border = struct.unpack(
        "<IBBBB", mi[map_id * 8:map_id * 8 + 8])
    blocks = s["maps"][off:off + w * h]
    toff, tcount = struct.unpack("<IH", s["tileset_index"][tset * 6:tset * 6 + 6])
    tdata = s["tilesets"][toff:toff + tcount * 16]
    boff, bcount = struct.unpack(
        "<IH", s["blockset_index"][tset * 6:tset * 6 + 6])
    bdata = s["blocksets"][boff:boff + bcount * 16]
    grid = [[0] * (w * 32) for _ in range(h * 32)]
    for by in range(h):
        for bx in range(w):
            blk = blocks[by * w + bx]
            for ty in range(4):
                for tx in range(4):
                    tid = bdata[blk * 16 + ty * 4 + tx]
                    one = tiles_to_shades(tdata[tid * 16:(tid + 1) * 16], 1, 1)
                    px, py = bx * 32 + tx * 8, by * 32 + ty * 8
                    for r in range(8):
                        for c in range(8):
                            grid[py + r][px + c] = one[r][c]
    write_preview(os.path.join(out_dir, "map_pallet_town.png"), grid, scale=2)


# ---------------------------------------------------------------- self check

def read_str(sect, index):
    n = struct.unpack("<H", sect[0:2])[0]
    if index >= n:
        return ""
    off = struct.unpack("<H", sect[2 + 2 * index:4 + 2 * index])[0]
    end = sect.index(b"\x00", off)
    return sect[off:end].decode("ascii")


def sanity(blob, builder):
    s = sections_of(blob)
    problems = []

    if len(s["font"]) != 2048:
        problems.append("font is %d bytes" % len(s["font"]))
    if len(s["border"]) != 72:
        problems.append("border is %d bytes" % len(s["border"]))
    if len(s["townmap_map"]) != 360:
        problems.append("townmap_map is %d bytes" % len(s["townmap_map"]))

    # Route 1 grass slots must be Pidgey and Rattata only.
    route1 = builder.map_ids["ROUTE_1"]
    rec = s["wild"][route1 * 42:(route1 + 1) * 42]
    rate = rec[0]
    species = sorted({rec[1 + 2 * i + 1] for i in range(10)})
    names = sorted({read_str(s["species_names"],
                             s["dex_order"][sp - 1] - 1) for sp in species})
    print("  Route 1 grass rate %d, species %s" % (rate, ", ".join(names)))
    if names != ["PIDGEY", "RATTATA"]:
        problems.append("Route 1 grass is %s" % names)
    if rec[21] != 0:
        problems.append("Route 1 water rate is %d, expected 0" % rec[21])

    hidden = len(s["items_hidden"]) // 5
    placed = len(s["items_placed"]) // 5
    print("  hidden items %d, placed item balls %d" % (hidden, placed))
    if not 40 <= hidden <= 60:
        problems.append("hidden item count %d is out of range" % hidden)
    if not 60 <= placed <= 140:
        problems.append("placed item count %d is out of range" % placed)

    # Spot check a few names.
    print("  species 1 %s, 151 %s" % (read_str(s["species_names"], 0),
                                      read_str(s["species_names"], 150)))
    print("  item 1 %s, move 1 %s, type 0 %s" %
          (read_str(s["item_names"], 1), read_str(s["move_names"], 0),
           read_str(s["type_names"], 0)))
    print("  map label 0 %s, 37 %s" % (read_str(s["map_labels"], 0),
                                       read_str(s["map_labels"], 37)))
    return problems


# ---------------------------------------------------------------------- main

def main():
    here = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--src", default=os.path.join(here, "third_party/pokered"))
    ap.add_argument("--out", default=os.path.join(here, "build_pack"))
    ap.add_argument("--no-preview", action="store_true")
    ap.add_argument("--version", choices=["red", "blue"], default="red",
                    help="which version's encounter tables to pack (default red)")
    args = ap.parse_args()

    if not os.path.isdir(args.src):
        sys.exit("pokered checkout not found at %s" % args.src)
    if not HAVE_PIL:
        print("Pillow not found, using the built in PNG reader")

    builder = Builder(args.src, args.version)
    pack = builder.build()
    blob = pack.build()

    os.makedirs(args.out, exist_ok=True)
    path = os.path.join(args.out, "pokered.pack")
    open(path, "wb").write(blob)

    print("%-16s %10s %10s" % ("section", "offset", "bytes"))
    total = 0
    for i, (name, data) in enumerate(pack.sections):
        rec = blob[8 + 24 * i:8 + 24 * (i + 1)]
        off = struct.unpack("<I", rec[16:20])[0]
        print("%-16s %10d %10d" % (name, off, len(data)))
        total += len(data)
    print("%-16s %10s %10d" % ("(payload)", "", total))
    print("%-16s %10s %10d" % ("(file)", "", len(blob)))
    print("wrote %s, %d sections" % (path, len(pack.sections)))

    print("sanity:")
    problems = sanity(blob, builder)
    for n in builder.notes:
        print("  note: %s" % n)
    for p in problems:
        print("  PROBLEM: %s" % p)

    if not args.no_preview:
        make_previews(blob, os.path.join(args.out, "preview"), builder)
        print("previews in %s" % os.path.join(args.out, "preview"))

    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
