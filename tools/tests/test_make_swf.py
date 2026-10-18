"""Tests for tools/make_swf.py: the empty Scaleform movie the select menu loads."""
import struct
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import make_swf  # noqa: E402


def read_rect(data):
    """Decodes an SWF RECT: (xmin, xmax, ymin, ymax, bytes used)."""
    bits = "".join(format(b, "08b") for b in data)
    nbits = int(bits[:5], 2)
    values = [int(bits[5 + i * nbits:5 + (i + 1) * nbits], 2) for i in range(4)]
    used = (5 + 4 * nbits + 7) // 8
    return (*values, used)


def read_tags(data):
    """(code, payload) for each tag."""
    tags, pos = [], 0
    while pos < len(data):
        word = struct.unpack_from("<H", data, pos)[0]
        pos += 2
        code, length = word >> 6, word & 0x3F
        if length == 0x3F:
            length = struct.unpack_from("<I", data, pos)[0]
            pos += 4
        tags.append((code, data[pos:pos + length]))
        pos += length
    return tags


class MakeSwfTests(unittest.TestCase):
    def setUp(self):
        self.swf = make_swf.build()

    def test_header(self):
        self.assertEqual(self.swf[:3], b"FWS")  # uncompressed
        self.assertEqual(self.swf[3], 8)  # SWF 8: AVM1, what Skyrim's Scaleform runs
        self.assertEqual(struct.unpack_from("<I", self.swf, 4)[0], len(self.swf))

    def test_stage_size_and_frame_rate(self):
        xmin, xmax, ymin, ymax, used = read_rect(self.swf[8:])
        self.assertEqual((xmin, xmax, ymin, ymax), (0, 1280 * 20, 0, 720 * 20))  # twips
        rate, frames = struct.unpack_from("<HH", self.swf, 8 + used)
        self.assertEqual(rate, 30 << 8)  # 8.8 fixed point
        self.assertEqual(frames, 1)

    def test_tags(self):
        _, _, _, _, used = read_rect(self.swf[8:])
        tags = read_tags(self.swf[8 + used + 4:])
        self.assertEqual([code for code, _ in tags], [69, 9, 1, 0])  # FileAttributes, SetBackgroundColor, ShowFrame, End
        self.assertEqual(tags[0][1], struct.pack("<I", 0))  # AVM1, no network access

    def test_matches_the_shipped_movie(self):
        shipped = (ROOT / "dist-files" / "Interface" / "CharSelectMenu.swf").read_bytes()
        self.assertEqual(self.swf, shipped, "dist-files/Interface/CharSelectMenu.swf is out of date: run tools/make_swf.py")

    def test_rect_encodes_negative_and_padded_values(self):
        data = make_swf.rect(-20, 40, 0, 1)
        self.assertEqual(len(data), 9)  # 5 + 4*16 bits, padded to bytes
        xmin, xmax, ymin, ymax, _ = read_rect(data)
        self.assertEqual(xmin, (-20) & 0xFFFF)  # two's complement in nbits
        self.assertEqual((xmax, ymin, ymax), (40, 0, 1))

    def test_long_tags(self):
        short = make_swf.tag(9, b"\0" * 0x3E)
        self.assertEqual(len(short), 2 + 0x3E)
        long = make_swf.tag(9, b"\0" * 0x3F)
        self.assertEqual(len(long), 6 + 0x3F)
        self.assertEqual(read_tags(long), [(9, b"\0" * 0x3F)])


if __name__ == "__main__":
    unittest.main()
