#!/usr/bin/env python3
"""Gen 2 spike: decode the main fields of a Pokemon Gold/Silver work-RAM dump.

Addresses are from pret/pokegold's symbol file (build byte-identical to the
retail USA/Europe Gold ROM). All of them live in WRAM banks 0-1, i.e. inside
the 8 KB window the browser page and the device poller already mirror, so if
this decodes a live dump, Gen 2 needs no change to the mirror itself.

    curl -s localhost:8080/api/wram -o gold.wram && python3 gen2_spike.py gold.wram
"""
import sys

BASE = 0xC000
# pokegold.sym (bank 1 = D000..DFFF, the default-mapped bank)
A = dict(
    playerId=0xD1A1, playerName=0xD1A3, rivalName=0xD1B9,
    hours=0xD1EB, minutes=0xD1ED, seconds=0xD1EE, timeOfDay=0xD157, direction=0xD205,
    money=0xD573, coins=0xD57A, johtoBadges=0xD57C, kantoBadges=0xD57D,
    numItems=0xD5B7, numBalls=0xD5FC, eventFlags=0xD7B7, repel=0xD9EB,
    mapGroup=0xDA00, mapNumber=0xDA01, y=0xDA02, x=0xDA03,
    partyCount=0xDA22, partySpecies=0xDA23, partyMon1=0xDA2A,
    partyOTs=0xDB4A, partyNicks=0xDB8C, dexCaught=0xDBE4, dexSeen=0xDC04,
    battleMode=0xD116, enemyMon=0xD0EF, enemyDVs=0xD0F5, enemyLevel=0xD0FC, enemyHP=0xD0FF,
    battleMon=0xCB0C, battleMonDVs=0xCB12,
)
PARTY_STRUCT = 48      # Gen 2 party_struct (Gen 1 was 44): adds held item etc.
M_ITEM, M_EXP, M_DVS, M_HAPPY, M_LEVEL, M_HP, M_MAXHP = 1, 8, 0x15, 0x1B, 0x1F, 0x22, 0x24
NAME_LEN = 11


def text(b):
    # Gen 2 charmap shares A-Z (0x80..), a-z (0xA0..), 0x50 terminator with Gen 1
    out = ""
    for c in b:
        if c == 0x50: break
        if 0x80 <= c <= 0x99: out += chr(ord("A") + c - 0x80)
        elif 0xA0 <= c <= 0xB9: out += chr(ord("a") + c - 0xA0)
        elif 0xF6 <= c <= 0xFF: out += chr(ord("0") + c - 0xF6)
        elif c == 0x7F: out += " "
        else: out += "?"
    return out


def main(path):
    w = open(path, "rb").read()
    assert len(w) == 0x2000, "expected an 8 KB WRAM mirror"
    rd = lambda a: w[a - BASE]
    rd16 = lambda a: (rd(a) << 8) | rd(a + 1)
    rd24 = lambda a: (rd(a) << 16) | (rd(a + 1) << 8) | rd(a + 2)
    name = text(w[A["playerName"] - BASE:A["playerName"] - BASE + NAME_LEN])
    print(f"player      {name!r}  id {rd16(A['playerId'])}  rival {text(w[A['rivalName']-BASE:A['rivalName']-BASE+NAME_LEN])!r}")
    print(f"money       {rd24(A['money'])}  (Gen 2 money is plain big-endian, not BCD)  coins {rd16(A['coins'])}")
    jb, kb = rd(A["johtoBadges"]), rd(A["kantoBadges"])
    print(f"badges      johto {bin(jb).count('1')}/8  kanto {bin(kb).count('1')}/8")
    print(f"map         group {rd(A['mapGroup'])} number {rd(A['mapNumber'])}  at x {rd(A['x'])} y {rd(A['y'])}  facing {rd(A['direction'])}  time-of-day {rd(A['timeOfDay'])}")
    print(f"play time   {rd16(A['hours'])}:{rd(A['minutes']):02d}:{rd(A['seconds']):02d}")
    n = rd(A["partyCount"])
    print(f"party       {n}")
    for i in range(min(n, 6)):
        base = A["partyMon1"] + i * PARTY_STRUCT
        sp = rd(base); lvl = rd(base + M_LEVEL); hp = rd16(base + M_HP); mx = rd16(base + M_MAXHP)
        item = rd(base + M_ITEM); happy = rd(base + M_HAPPY)
        d0, d1 = rd(base + M_DVS), rd(base + M_DVS + 1)
        nick = text(w[A["partyNicks"] - BASE + i * NAME_LEN:A["partyNicks"] - BASE + (i + 1) * NAME_LEN])
        shiny = (d0 & 0x0F) == 10 and (d1 & 0xF0) == 0xA0 and (d1 & 0x0F) == 10 and (d0 >> 4) in (2, 3, 6, 7, 10, 11, 14, 15)
        print(f"   {i}: {nick!r:14} species {sp:3d}  L{lvl:3d}  HP {hp}/{mx}  item {item}  happiness {happy}  DV atk {d0>>4} def {d0&15} spd {d1>>4} spc {d1&15}{'  SHINY' if shiny else ''}")
    seen = sum(bin(x).count("1") for x in w[A["dexSeen"] - BASE:A["dexSeen"] - BASE + 32])
    caught = sum(bin(x).count("1") for x in w[A["dexCaught"] - BASE:A["dexCaught"] - BASE + 32])
    print(f"pokedex     caught {caught}  seen {seen}")
    print(f"battle      mode {rd(A['battleMode'])} (0 none, 1 wild, 2 trainer)")
    if rd(A["battleMode"]):
        print(f"   enemy species {rd(A['enemyMon'])} L{rd(A['enemyLevel'])} HP {rd16(A['enemyHP'])}  DV bytes {rd(A['enemyDVs']):02x}{rd(A['enemyDVs']+1):02x}")


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "gold.wram")
