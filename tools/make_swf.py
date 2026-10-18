"""Writes the empty Scaleform movie the CharSelect menu loads.

The menu builds its whole UI at runtime from C++ through the GFx API
(createEmptyMovieClip, drawing API, createTextField), so the SWF only needs
a stage: 1280x720, 30 fps, one empty frame, SWF version 8 (AVM1, which is
what Skyrim's Scaleform runs).

    python tools/make_swf.py dist-files/Interface/CharSelectMenu.swf
"""

import struct
import sys

WIDTH, HEIGHT, FPS = 1280, 720, 30


def rect(xmin, xmax, ymin, ymax, nbits=16):
    bits = format(nbits, "05b")
    for v in (xmin, xmax, ymin, ymax):
        bits += format(v & ((1 << nbits) - 1), f"0{nbits}b")
    bits += "0" * (-len(bits) % 8)
    return bytes(int(bits[i:i + 8], 2) for i in range(0, len(bits), 8))


def tag(code, payload=b""):
    if len(payload) < 0x3F:
        return struct.pack("<H", (code << 6) | len(payload)) + payload
    return struct.pack("<HI", (code << 6) | 0x3F, len(payload)) + payload


def build():
    body = rect(0, WIDTH * 20, 0, HEIGHT * 20)
    body += struct.pack("<HH", FPS << 8, 1)            # frame rate (8.8), frame count
    body += tag(69, struct.pack("<I", 0))               # FileAttributes: AVM1, no network
    body += tag(9, bytes((0, 0, 0)))                    # SetBackgroundColor (alpha comes from the menu)
    body += tag(1)                                      # ShowFrame
    body += tag(0)                                      # End
    return b"FWS" + bytes((8,)) + struct.pack("<I", 8 + len(body)) + body


if __name__ == "__main__":
    out = sys.argv[1] if len(sys.argv) > 1 else "CharSelectMenu.swf"
    data = build()
    with open(out, "wb") as f:
        f.write(data)
    print(f"wrote {out} ({len(data)} bytes)")
