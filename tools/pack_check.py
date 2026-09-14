#!/usr/bin/env python3
"""Print the section table of an EPK1 asset pack.

The reader's Pokemon companion screen draws from build_pack/pokered.pack, which
is uploaded through the games page. This is the host side check: open a pack,
verify the magic and the table of contents, and print every section with its
offset and length so a bad or truncated upload is obvious without a device.

    python3 tools/pack_check.py [path/to/pokered.pack]

Exit status is 0 when the pack parses, 1 when it does not.
"""

import os
import struct
import sys

DEFAULT = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                       "build_pack", "pokered.pack")

# src/pack.cpp refuses a pack with more sections than this.
MAX_SECTIONS = 48


def main(argv):
    path = argv[1] if len(argv) > 1 else DEFAULT
    if not os.path.exists(path):
        print("no such pack: %s" % path)
        return 1
    data = open(path, "rb").read()
    if len(data) < 8 or data[0:4] != b"EPK1":
        print("not an EPK1 pack: %s" % path)
        return 1
    count = struct.unpack_from("<I", data, 4)[0]
    print("%s" % path)
    print("%d bytes, %d sections" % (len(data), count))
    if count == 0 or count > MAX_SECTIONS:
        print("section count outside 1..%d, the reader would reject this pack"
              % MAX_SECTIONS)
        return 1
    if len(data) < 8 + 24 * count:
        print("table of contents is truncated")
        return 1

    print("")
    print("%-16s %10s %10s %10s" % ("section", "offset", "length", "end"))
    bad = 0
    payload = 0
    for i in range(count):
        rec = data[8 + 24 * i:8 + 24 * (i + 1)]
        name = rec[0:16].split(b"\0")[0].decode("ascii", "replace")
        offset, length = struct.unpack_from("<II", rec, 16)
        end = offset + length
        note = ""
        if end > len(data):
            note = "  PAST THE END OF THE FILE"
            bad += 1
        elif offset < 8 + 24 * count:
            note = "  OVERLAPS THE TABLE OF CONTENTS"
            bad += 1
        payload += length
        print("%-16s %10d %10d %10d%s" % (name, offset, length, end, note))

    print("")
    print("payload %d bytes, container overhead %d bytes"
          % (payload, len(data) - payload))
    if bad:
        print("%d section(s) the reader would drop" % bad)
        return 1
    print("pack looks well formed")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
