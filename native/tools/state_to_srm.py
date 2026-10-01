#!/usr/bin/env python3
"""Pull the cartridge RAM (battery save) out of a games-page save state.

The browser page's save states (.state / .auto on the games server) are an
ERSV container: 32 byte header, then a gzipped "WBS1" payload of four
length-prefixed sections: internalState, paletteMemory, gameBoyMemory,
cartridgeRam (www/app.js, "save container"). The last one is the raw battery
save, byte-compatible with a RetroArch .srm or any emulator's .sav.

Why this exists: the page only uploads a battery save when it matches the
loaded state, so a session that lived on save states never produces a .sav.
This takes the save out of the state instead, e.g. to continue on a Pi:

    python3 state_to_srm.py stub_games/saves/pokemon_red.gb.state out.srm
    scp out.srm root@batocera.local:/userdata/saves/gb/pokemon_red.srm   # game NOT running
"""
import gzip, struct, sys


def main(argv):
    if len(argv) != 3:
        sys.exit(__doc__)
    raw = open(argv[1], "rb").read()
    if raw[:4] != b"ERSV":
        sys.exit("not an ERSV save container")
    gzipped = raw[6] & 1
    payload = raw[32:]
    if gzipped:
        payload = gzip.decompress(payload)
    if payload[:4] != b"WBS1":
        sys.exit("not a Game Boy (WBS1) payload; NES states carry no battery RAM here")
    off, secs = 4, []
    for _ in range(4):
        n = struct.unpack_from("<I", payload, off)[0]
        off += 4
        secs.append(payload[off:off + n])
        off += n
    sram = secs[3]
    if not sram:
        sys.exit("state has no cartridge RAM (cartridge without battery?)")
    open(argv[2], "wb").write(sram)
    name = sram[0x2598:0x2598 + 11]   # Gen 1 Pokemon: sPlayerName, for a quick sanity line
    text = "".join(chr(c - 0x80 + ord("A")) if 0x80 <= c <= 0x99 else "" for c in name)
    print("%d bytes written to %s%s" % (len(sram), argv[2], ("; Gen 1 player name reads %r" % text) if text else ""))


if __name__ == "__main__":
    main(sys.argv)
