"""Tests for tools/savediff.py: LZ4 decoding, .ess and .skse parsing, the comparison report.

Run from the repository root:  python -m unittest discover -s tools/tests -v
Set CHARSELECT_TEST_SAVES to a saves folder to also parse the newest real save there.
"""
import contextlib
import io
import os
import struct
import sys
import tempfile
import unittest
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import savediff  # noqa: E402


# ---- builders -----------------------------------------------------------------


def lz4_literals(data):
    """A valid LZ4 block holding data as one run of literals (no matches)."""
    n = len(data)
    if n < 15:
        return bytes([n << 4]) + data
    out = bytearray([0xF0])
    rest = n - 15
    while rest >= 255:
        out.append(255)
        rest -= 255
    out.append(rest)
    return bytes(out) + data


def wstr(text):
    raw = text.encode("latin1")
    return struct.pack("<H", len(raw)) + raw


def papyrus_block(wide=False, strings=("Actor", "Quest"), scripts=2, instances=3, references=1, arrays=2, active=4):
    """A Papyrus global data block with the given counts (16- or 32-bit string table)."""
    idx = (lambda v: struct.pack("<I", v)) if wide else (lambda v: struct.pack("<H", v))
    b = bytearray(struct.pack("<H", 3))
    b += struct.pack("<I", len(strings)) if wide else struct.pack("<H", len(strings))
    for s in strings:
        b += wstr(s)
    b += struct.pack("<I", scripts)
    for _ in range(scripts):
        b += idx(0) + idx(1) + struct.pack("<I", 1) + idx(0) + idx(1)  # name, parent, one member
    b += struct.pack("<I", instances)
    for i in range(instances):
        b += struct.pack("<Q", i) + idx(0) + struct.pack("<HH", 0, 0) + b"\0\0\0" + b"\0"
    b += struct.pack("<I", references)
    for i in range(references):
        b += struct.pack("<Q", i) + idx(1)
    b += struct.pack("<I", arrays)
    for i in range(arrays):
        b += struct.pack("<Q", i)
        if i % 2 == 0:
            b += b"\x01" + idx(0)  # object array: element type
        else:
            b += b"\x02"
        b += struct.pack("<I", 5)
    b += struct.pack("<I", 0)  # runtime
    b += struct.pack("<I", active)
    return bytes(b)


def ess_bytes(player="Sven", level=66, version=12, compression=0, plugins=("Skyrim.esm", "Update.esm"), light=("ccbgssse001-fish.esm",),
              changes=(("REFR", 10), ("ACHR", 20), ("REFR", 300)), papyrus=None, form_version=78, extra_globals=None, shot=(2, 1)):
    """A whole save file. changes: (type name, data length[, refID[, fill byte]]) per change form."""
    width, height = shot
    header = struct.pack("<II", version, 7) + wstr(player) + struct.pack("<I", level) + wstr("Whiterun") + wstr("4E 201") + wstr("NordRace")
    header += struct.pack("<Hff", 0, 1.0, 2.0) + struct.pack("<Q", 133000000000000000) + struct.pack("<II", width, height)
    if version >= 12:
        header += struct.pack("<H", compression)

    body = bytearray([form_version]) + struct.pack("<I", 0)
    body += bytes([len(plugins)]) + b"".join(wstr(p) for p in plugins)
    if form_version >= 78:
        body += struct.pack("<H", len(light)) + b"".join(wstr(p) for p in light)
    table1 = [(0, b"stats"), (1, b"loc")]
    table2 = [(100, b"process lists")]
    table3 = [(1001, papyrus if papyrus is not None else papyrus_block())] + list(extra_globals or [])
    # Table 3's count is stored one short (an engine quirk the reader expects).
    flt = [0] * 6 + [len(table1), len(table2), len(table3) - 1, len(changes)]
    body += struct.pack("<10I", *flt) + b"\0" * 60
    for kind, data in table1 + table2:
        body += struct.pack("<II", kind, len(data)) + data
    for i, change in enumerate(changes):
        name, length = change[0], change[1]
        ref_id = change[2] if len(change) > 2 else i
        fill = change[3] if len(change) > 3 else 0xAA
        kind = savediff.CHANGE_TYPES.index(name)
        if length < 256:
            body += struct.pack("<3sIBBBB", ref_id.to_bytes(3, "big"), 0, kind, 0, length, 0)
        else:
            body += struct.pack("<3sIBBHH", ref_id.to_bytes(3, "big"), 0, kind | 0x40, 0, length, 0)
        body += bytes([fill]) * length
    for kind, data in table3:
        body += struct.pack("<II", kind, len(data)) + data

    out = b"TESV_SAVEGAME" + struct.pack("<I", len(header)) + header
    out += b"\x10" * (width * height * (4 if version >= 11 else 3))
    if compression == 2:
        packed = lz4_literals(bytes(body))
        out += struct.pack("<II", len(body), len(packed)) + packed
    elif compression == 1:
        packed = zlib.compress(bytes(body))
        out += struct.pack("<II", len(body), len(packed)) + packed
    else:
        out += bytes(body)
    return out


def fourcc(tag):
    return struct.unpack("<I", tag.encode("latin1")[::-1])[0]


def skse_bytes(plugins):
    """A co-save. plugins: {uid (4cc str or int): [(chunk 4cc or int, version, length[, fill byte])]}."""
    out = bytearray(b"SKSE") + struct.pack("<III", 2, 0x02020060, 0x01070680) + struct.pack("<I", len(plugins))
    for uid, chunks in plugins.items():
        body = bytearray()
        for chunk in chunks:
            kind, version, length = chunk[:3]
            fill = chunk[3] if len(chunk) > 3 else 0x55
            body += struct.pack("<III", fourcc(kind) if isinstance(kind, str) else kind, version, length) + bytes([fill]) * length
        out += struct.pack("<III", fourcc(uid) if isinstance(uid, str) else uid, len(chunks), len(body)) + body
    return bytes(out)


class TempFiles:
    def __init__(self):
        self._dir = tempfile.TemporaryDirectory()
        self.path = Path(self._dir.name)

    def write(self, name, data):
        p = self.path / name
        p.write_bytes(data)
        return str(p)

    def cleanup(self):
        self._dir.cleanup()


def run(fn, *args):
    out = io.StringIO()
    with contextlib.redirect_stdout(out):
        fn(*args)
    return out.getvalue()


# ---- LZ4 ------------------------------------------------------------------------


class Lz4Tests(unittest.TestCase):
    def test_literals_only(self):
        self.assertEqual(savediff.lz4_block(b"\x50hello", 5), b"hello")

    def test_long_literal_runs(self):
        for n in (15, 16, 269, 270, 271, 1000):
            data = bytes(range(256)) * 4
            data = data[:n]
            self.assertEqual(savediff.lz4_block(lz4_literals(data), n), data, n)

    def test_overlapping_match_repeats_a_pattern(self):
        # "abc", then copy 9 bytes from 3 back.
        block = bytes([(3 << 4) | 5]) + b"abc" + b"\x03\x00"
        self.assertEqual(savediff.lz4_block(block, 12), b"abcabcabcabc")

    def test_long_match(self):
        # "a", then copy 15 + 4 + 10 = 29 bytes from 1 back.
        block = bytes([(1 << 4) | 15]) + b"a" + b"\x01\x00" + b"\x0A"
        self.assertEqual(savediff.lz4_block(block, 30), b"a" * 30)

    def test_non_overlapping_match_then_last_literals(self):
        block = bytes([(4 << 4) | 0]) + b"wxyz" + b"\x04\x00" + bytes([2 << 4]) + b"!!"
        self.assertEqual(savediff.lz4_block(block, 10), b"wxyzwxyz!!")

    def test_empty(self):
        self.assertEqual(savediff.lz4_block(b"\x00", 0), b"")


# ---- readers --------------------------------------------------------------------


class ReaderTests(unittest.TestCase):
    def test_values(self):
        r = savediff.Reader(struct.pack("<BHIQf", 1, 2, 3, 4, 0.5) + wstr("Bj\xf6rn"))
        self.assertEqual((r.u8(), r.u16(), r.u32(), r.u64(), r.f32()), (1, 2, 3, 4, 0.5))
        self.assertEqual(r.wstr(), "Bj\xf6rn")

    def test_running_off_the_end_raises(self):
        r = savediff.Reader(b"\x01\x02")
        with self.assertRaises(EOFError):
            r.u32()


# ---- .ess -------------------------------------------------------------------------


class EssTests(unittest.TestCase):
    def setUp(self):
        self.files = TempFiles()

    def tearDown(self):
        self.files.cleanup()

    def test_uncompressed_save(self):
        s = savediff.read_ess(self.files.write("a.ess", ess_bytes()))
        self.assertEqual(s["info"]["player"], "Sven")
        self.assertEqual(s["info"]["level"], 66)
        self.assertEqual(s["info"]["location"], "Whiterun")
        self.assertEqual(s["formVersion"], 78)
        self.assertEqual(s["plugins"], ["Skyrim.esm", "Update.esm"])
        self.assertEqual(s["light"], ["ccbgssse001-fish.esm"])
        self.assertEqual(s["changes"], {"REFR": 2, "ACHR": 1})
        self.assertEqual(s["changeBytes"], {"REFR": 310, "ACHR": 20})
        self.assertEqual(set(s["globals"]), {0, 1, 100, 1001})
        self.assertEqual(s["papyrus"]["script instances"], 3)
        self.assertEqual(s["papyrus"]["string table"], "16-bit")

    def test_lz4_and_zlib_saves_match_the_uncompressed_one(self):
        plain = savediff.read_ess(self.files.write("p.ess", ess_bytes(compression=0)))
        for compression in (1, 2):
            s = savediff.read_ess(self.files.write(f"c{compression}.ess", ess_bytes(compression=compression)))
            self.assertEqual(s["changes"], plain["changes"], compression)
            self.assertEqual(s["globals"], plain["globals"], compression)
            self.assertEqual(s["papyrus"], plain["papyrus"], compression)

    def test_old_form_version_has_no_light_plugins(self):
        s = savediff.read_ess(self.files.write("a.ess", ess_bytes(form_version=74)))
        self.assertEqual(s["light"], [])
        self.assertEqual(s["plugins"], ["Skyrim.esm", "Update.esm"])

    def test_long_change_form_lengths(self):
        s = savediff.read_ess(self.files.write("a.ess", ess_bytes(changes=(("NPC_", 70000 // 2), ("QUST", 5)))))
        self.assertEqual(s["changeBytes"]["NPC_"], 35000)
        self.assertEqual(s["changes"]["QUST"], 1)

    def test_not_a_save(self):
        with self.assertRaises(ValueError):
            savediff.read_ess(self.files.write("a.ess", b"FO4_SAVEGAME and so on"))

    def test_truncated_save(self):
        data = ess_bytes()
        with self.assertRaises(EOFError):
            savediff.read_ess(self.files.write("a.ess", data[:len(data) - 10]))


# ---- Papyrus ------------------------------------------------------------------------


class PapyrusTests(unittest.TestCase):
    def test_vanilla_string_table(self):
        p = savediff.papyrus_summary(papyrus_block())
        self.assertEqual(p["string table"], "16-bit")
        self.assertEqual((p["strings"], p["script types"], p["script instances"], p["references"], p["arrays"], p["active scripts"]),
                         (2, 2, 3, 1, 2, 4))

    def test_32_bit_string_table_of_big_modded_saves(self):
        p = savediff.papyrus_summary(papyrus_block(wide=True, strings=["s%d" % i for i in range(40)], instances=7))
        self.assertEqual(p["string table"], "32-bit")
        self.assertEqual(p["strings"], 40)
        self.assertEqual(p["script instances"], 7)

    def test_no_block(self):
        self.assertIsNone(savediff.papyrus_summary(None))
        self.assertIsNone(savediff.papyrus_summary(b""))

    def test_garbage_reports_an_error(self):
        p = savediff.papyrus_summary(b"\x01\x02\x03")
        self.assertIn("error", p)
        self.assertIn("16-bit", p["error"])
        self.assertIn("32-bit", p["error"])

    def test_implausible_counts_are_rejected(self):
        block = bytearray(papyrus_block(wide=False))
        block[-4:] = struct.pack("<I", 60_000_000)  # active scripts
        self.assertIn("error", savediff.papyrus_summary(bytes(block)))


# ---- co-save ------------------------------------------------------------------------


class CoSaveTests(unittest.TestCase):
    def setUp(self):
        self.files = TempFiles()

    def tearDown(self):
        self.files.cleanup()

    def test_plugins_and_chunks(self):
        path = self.files.write("a.skse", skse_bytes({"SKEE": [("MORP", 3, 100), ("MORP", 3, 50), ("OVRD", 1, 10)], 0x00001234: [(0x00000001, 1, 4)]}))
        s = savediff.read_skse(path)
        self.assertEqual(set(s), {"SKEE", "00001234"})  # unprintable IDs in hex
        sizes = {p: {c: n for c, (n, _) in chunks.items()} for p, chunks in s.items()}
        self.assertEqual(sizes["SKEE"], {"MORP v3": 150, "OVRD v1": 10})  # same chunk type summed
        self.assertEqual(sizes["00001234"], {"00000001 v1": 4})

    def test_chunk_contents_are_hashed(self):
        a = savediff.read_skse(self.files.write("a.skse", skse_bytes({"SKEE": [("MORP", 3, 100, 0x01)]})))
        b = savediff.read_skse(self.files.write("b.skse", skse_bytes({"SKEE": [("MORP", 3, 100, 0x02)]})))
        self.assertEqual(a["SKEE"]["MORP v3"][0], b["SKEE"]["MORP v3"][0])
        self.assertNotEqual(a["SKEE"]["MORP v3"][1], b["SKEE"]["MORP v3"][1])

    def test_missing_co_save(self):
        self.assertIsNone(savediff.read_skse(str(self.files.path / "nope.skse")))

    def test_not_a_co_save(self):
        self.assertEqual(savediff.read_skse(self.files.write("a.skse", b"NOPE" + b"\0" * 20)), {"error": "not an SKSE co-save"})

    def test_empty_co_save(self):
        self.assertEqual(savediff.read_skse(self.files.write("a.skse", skse_bytes({}))), {})


# ---- report -------------------------------------------------------------------------


class ReportTests(unittest.TestCase):
    def setUp(self):
        self.files = TempFiles()

    def tearDown(self):
        self.files.cleanup()

    def pair(self, a_ess, b_ess, a_skse=None, b_skse=None):
        pa = self.files.write("A.ess", a_ess)
        pb = self.files.write("B.ess", b_ess)
        if a_skse is not None:
            self.files.write("A.skse", a_skse)
        if b_skse is not None:
            self.files.write("B.skse", b_skse)
        return pa, pb

    def test_identical_saves_report_no_differences(self):
        co = skse_bytes({"SKEE": [("MORP", 3, 100)]})
        out = run(savediff.compare, *self.pair(ess_bytes(), ess_bytes(), co, co))
        self.assertIn("Change forms by type (count): no differences", out)
        self.assertIn("Global data blocks (bytes): no differences", out)
        self.assertIn("Papyrus: no differences", out)
        self.assertIn("Co-save chunks (bytes): no differences", out)
        self.assertIn("Change forms by ID: no differences", out)
        self.assertIn("Co-save chunks with the same size but different contents: none", out)
        self.assertNotIn("Warning", out)

    def test_differences_are_listed(self):
        a = ess_bytes(changes=(("REFR", 10),))
        b = ess_bytes(changes=(("REFR", 10), ("ACHR", 5)), papyrus=papyrus_block(instances=5))
        out = run(savediff.compare, *self.pair(a, b, skse_bytes({"SKEE": [("MORP", 3, 100)]}),
                                               skse_bytes({"SKEE": [("MORP", 3, 284)], "LOBY": [("DATA", 1, 8)]})))
        self.assertIn("Change forms by type (count): 1 differ", out)
        self.assertRegex(out, r"ACHR\s+A\s+0\s+B\s+1\s+\(\+1\)")
        self.assertRegex(out, r"script instances\s+A\s+3\s+B\s+5\s+\(\+2\)")
        self.assertIn("only in B: LOBY", out)
        self.assertRegex(out, r"SKEE / MORP v3\s+A\s+100 B\s+B\s+284 B\s+\(\+184 B\)")

    def test_a_replaced_form_of_the_same_type_shows(self):
        # Same counts and sizes: only the form IDs differ (review L16).
        a = ess_bytes(changes=(("ACHR", 20, 0x000100),))
        b = ess_bytes(changes=(("ACHR", 20, 0xFF0801),))
        out = run(savediff.compare, *self.pair(a, b))
        self.assertIn("Change forms by type (count): no differences", out)
        self.assertIn("Change forms by ID: 1 types differ", out)
        self.assertIn("only in A: 000100", out)
        self.assertIn("only in B: FF0801", out)

    def test_changed_form_contents_show(self):
        a = ess_bytes(changes=(("NPC_", 20, 0x14, 0xAA),))
        b = ess_bytes(changes=(("NPC_", 20, 0x14, 0xBB),))
        out = run(savediff.compare, *self.pair(a, b))
        self.assertRegex(out, r"NPC_\s+only in A\s+0\s+only in B\s+0\s+different contents\s+1")

    def test_same_size_co_save_contents_show(self):
        out = run(savediff.compare, *self.pair(ess_bytes(), ess_bytes(), skse_bytes({"SKEE": [("MORP", 3, 100, 0x01)]}),
                                               skse_bytes({"SKEE": [("MORP", 3, 100, 0x02)]})))
        self.assertIn("Co-save chunks (bytes): no differences", out)
        self.assertIn("Co-save chunks with the same size but different contents: 1", out)
        self.assertIn("  SKEE / MORP v3", out)

    def test_warnings_for_different_characters_and_plugins(self):
        out = run(savediff.compare, *self.pair(ess_bytes(player="Sven"), ess_bytes(player="Ingrid", plugins=("Skyrim.esm",))))
        self.assertIn("different characters", out)
        self.assertIn("plugin lists differ", out)

    def test_missing_co_save_is_reported(self):
        out = run(savediff.compare, *self.pair(ess_bytes(), ess_bytes(), skse_bytes({})))
        self.assertIn("Co-save: missing for B", out)

    def test_summary_of_one_save(self):
        path = self.files.write("S.ess", ess_bytes())
        self.files.write("S.skse", skse_bytes({"SKEE": []}))
        out = run(savediff.summary, path, savediff.load(path))
        self.assertIn("Sven lvl 66, Whiterun", out)
        self.assertIn("2 plugins + 1 light; 3 change forms", out)
        self.assertIn("co-save: 1 plugins", out)


# ---- real saves (opt-in) ------------------------------------------------------------


@unittest.skipUnless(os.environ.get("CHARSELECT_TEST_SAVES"), "CHARSELECT_TEST_SAVES not set")
class RealSaveTests(unittest.TestCase):
    def test_newest_real_save_parses(self):
        folder = Path(os.environ["CHARSELECT_TEST_SAVES"])
        saves = sorted(folder.glob("*.ess"), key=lambda p: p.stat().st_mtime, reverse=True)
        self.assertTrue(saves, f"no .ess in {folder}")
        s = savediff.load(str(saves[0]))
        self.assertTrue(s["plugins"])
        self.assertGreater(sum(s["changes"].values()), 0)
        self.assertIsNotNone(s["papyrus"])
        self.assertNotIn("error", s["papyrus"])
        if s["skse"] is not None:
            self.assertNotIn("error", s["skse"])


if __name__ == "__main__":
    unittest.main()
