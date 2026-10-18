"""Compare two Skyrim SE/AE saves (.ess + .skse co-save) to spot state that differs between them.

Made for the save-safety test (docs/SAVE_SAFETY.md): load the same save once from the vanilla main menu and
once through the CharSelect lobby, save straight away each time, then compare. Lobby state leaking into the
real game shows up as extra change forms, script instances or co-save data in the lobby run's save.

Usage:
    python savediff.py <save A .ess> <save B .ess>     compare (the .skse next to each is read if present)
    python savediff.py <save .ess>                     summary of one save

Read-only. Format notes: UESP's "Skyrim Mod:Save File Format" and SKSE's serialization (co-save) layout.
"""
import os
import struct
import sys
import zlib
from collections import Counter

# ---- LZ4 block decoding (SE saves are LZ4-compressed; no module needed) ----


def lz4_block(src, size):
    dst = bytearray(size)
    sp = dp = 0
    end = len(src)
    while sp < end:
        token = src[sp]
        sp += 1
        lit = token >> 4
        if lit == 15:
            while True:
                b = src[sp]
                sp += 1
                lit += b
                if b != 255:
                    break
        dst[dp:dp + lit] = src[sp:sp + lit]
        sp += lit
        dp += lit
        if sp >= end:
            break
        offset = src[sp] | (src[sp + 1] << 8)
        sp += 2
        match = (token & 15) + 4
        if (token & 15) == 15:
            while True:
                b = src[sp]
                sp += 1
                match += b
                if b != 255:
                    break
        start = dp - offset
        if offset >= match:
            dst[dp:dp + match] = dst[start:start + match]
        else:  # overlapping copy: repeat the pattern
            for i in range(match):
                dst[dp + i] = dst[start + i]
        dp += match
    return bytes(dst[:dp])


# ---- readers ----


class Reader:
    def __init__(self, data, pos=0):
        self.d, self.p = data, pos

    def take(self, n):
        v = self.d[self.p:self.p + n]
        if len(v) != n:
            raise EOFError(f"wanted {n} bytes at {self.p}, file ends")
        self.p += n
        return v

    def u8(self):
        return self.take(1)[0]

    def u16(self):
        return struct.unpack("<H", self.take(2))[0]

    def u32(self):
        return struct.unpack("<I", self.take(4))[0]

    def u64(self):
        return struct.unpack("<Q", self.take(8))[0]

    def f32(self):
        return struct.unpack("<f", self.take(4))[0]

    def wstr(self):
        return self.take(self.u16()).decode("latin1")


GLOBAL_NAMES = {
    0: "Misc Stats", 1: "Player Location", 2: "TES", 3: "Global Variables", 4: "Created Objects", 5: "Effects",
    6: "Weather", 7: "Audio", 8: "SkyCells", 9: "Unknown 9", 10: "Unknown 10", 11: "Unknown 11",
    100: "Process Lists", 101: "Combat", 102: "Interface", 103: "Actor Causes", 104: "Unknown 104", 105: "Detection Manager",
    106: "Location MetaData", 107: "Quest Static Data", 108: "StoryTeller", 109: "Magic Favorites", 110: "PlayerControls",
    111: "Story Event Manager", 112: "Ingredient Shared", 113: "MenuControls", 114: "MenuTopicManager",
    1000: "Temp Effects", 1001: "Papyrus", 1002: "Anim Objects", 1003: "Timer", 1004: "Synchronized Animations", 1005: "Main",
}

CHANGE_TYPES = [
    "REFR", "ACHR", "PMIS", "PGRE", "PBEA", "PFLA", "CELL", "INFO", "QUST", "NPC_", "ACTI", "TACT", "ARMO", "BOOK", "CONT", "DOOR",
    "INGR", "LIGH", "MISC", "APPA", "STAT", "MSTT", "FURN", "WEAP", "AMMO", "KEYM", "ALCH", "IDLM", "NOTE", "ECZN", "CLAS", "FACT",
    "PACK", "NAVM", "WOOP", "MGEF", "SMQN", "SCEN", "LCTN", "RELA", "PHZD", "PBAR", "PCON", "FLST", "LVLN", "LVLI", "LVSP", "PARW",
    "ENCH",
]


def read_ess(path):
    with open(path, "rb") as f:
        raw = f.read()
    r = Reader(raw)
    if r.take(13) != b"TESV_SAVEGAME":
        raise ValueError(f"{path}: not a Skyrim save")
    header_size = r.u32()
    h = Reader(r.take(header_size))
    info = {"version": h.u32(), "saveNumber": h.u32(), "player": h.wstr(), "level": h.u32(), "location": h.wstr(),
            "gameDate": h.wstr(), "race": h.wstr(), "sex": h.u16()}
    h.f32(), h.f32(), h.u64()
    width, height = h.u32(), h.u32()
    compression = h.u16() if info["version"] >= 12 else 0
    r.take(width * height * (4 if info["version"] >= 11 else 3))
    if compression:
        size, packed = r.u32(), r.u32()
        blob = r.take(packed)
        body = lz4_block(blob, size) if compression == 2 else zlib.decompress(blob)
    else:
        body = raw[r.p:]
    b = Reader(body)
    form_version = b.u8()
    b.u32()  # plugin info size
    plugins = [b.wstr() for _ in range(b.u8())]
    light = [b.wstr() for _ in range(b.u16())] if form_version >= 78 else []
    flt = [b.u32() for _ in range(10)]
    b.take(15 * 4)
    t1, t2, t3, change_count = flt[6], flt[7], flt[8], flt[9]
    globals_ = {}

    def read_globals(count):
        for _ in range(count):
            kind, length = b.u32(), b.u32()
            globals_[kind] = b.take(length)

    read_globals(t1)
    read_globals(t2)
    changes = Counter()
    change_bytes = Counter()
    change_forms = {}  # type -> {refID: crc32 of flags + data}
    for _ in range(change_count):
        ref_id = int.from_bytes(b.take(3), "big")
        flags = b.u32()
        kind = b.u8()
        b.u8()  # version
        width_code = kind >> 6
        read_len = {0: b.u8, 1: b.u16, 2: b.u32}[width_code]
        length1 = read_len()
        read_len()
        data = b.take(length1)
        name = CHANGE_TYPES[kind & 0x3F] if (kind & 0x3F) < len(CHANGE_TYPES) else f"type{kind & 0x3F}"
        changes[name] += 1
        change_bytes[name] += length1
        change_forms.setdefault(name, {})[ref_id] = zlib.crc32(data, zlib.crc32(struct.pack("<I", flags)))
    read_globals(t3 + 1)  # table 3's count is one short in the file (known engine quirk)
    return {"info": info, "formVersion": form_version, "plugins": plugins, "light": light, "globals": globals_,
            "changes": changes, "changeBytes": change_bytes, "changeForms": change_forms,
            "papyrus": papyrus_summary(globals_.get(1001))}


def papyrus_summary(data):
    """Counts from the Papyrus block: script types, instances, references, arrays, active scripts.

    The string table is vanilla's (16-bit count and indices) or the 32-bit one big modded saves get
    (SKSE / Engine Fixes); try vanilla first, then 32-bit.
    """
    if not data:
        return None
    errors = []
    for wide in (False, True):
        try:
            result = papyrus_counts(data, wide)
            result["string table"] = "32-bit" if wide else "16-bit"
            return result
        except (EOFError, struct.error, ValueError) as e:
            errors.append(f"{'32' if wide else '16'}-bit: {e}")
    return {"error": "couldn't parse the Papyrus block (" + "; ".join(errors) + ")"}


def papyrus_counts(data, wide):
    """One parse of the Papyrus block with 16- or 32-bit string table counts and indices."""
    r = Reader(data)
    r.u16()  # header
    count = r.u32() if wide else r.u16()
    for _ in range(count):
        r.wstr()
    idx = r.u32 if wide else r.u16
    scripts = r.u32()
    for _ in range(scripts):
        idx(), idx()
        for _ in range(r.u32()):
            idx(), idx()
    instances = r.u32()
    for _ in range(instances):
        r.u64(), idx(), r.u16(), r.u16(), r.take(3), r.u8()
    references = r.u32()
    for _ in range(references):
        r.u64(), idx()
    arrays = r.u32()
    for _ in range(arrays):
        r.u64()
        if r.u8() == 1:  # object array: element type
            idx()
        r.u32()
    r.u32()  # papyrus runtime
    active = r.u32()
    if max(scripts, instances, references, arrays, active) > 50_000_000:
        raise ValueError("implausible counts")
    return {"strings": count, "script types": scripts, "script instances": instances, "references": references,
            "arrays": arrays, "active scripts": active}


def read_skse(path):
    """Per SKSE plugin: chunk (type, version) -> (total length, crc32 of the contents)."""
    if not os.path.exists(path):
        return None
    with open(path, "rb") as f:
        r = Reader(f.read())
    if r.take(4) != b"SKSE":
        return {"error": "not an SKSE co-save"}
    r.u32(), r.u32(), r.u32()  # format, SKSE and runtime versions
    plugins = {}
    for _ in range(r.u32()):
        uid, chunk_count, _length = r.u32(), r.u32(), r.u32()
        name = struct.pack("<I", uid)[::-1].decode("latin1") if all(32 <= c < 127 for c in struct.pack("<I", uid)) else f"{uid:08X}"
        chunks = {}
        for _ in range(chunk_count):
            kind, version, length = r.u32(), r.u32(), r.u32()
            data = r.take(length)
            tag = struct.pack("<I", kind)[::-1].decode("latin1") if all(32 <= c < 127 for c in struct.pack("<I", kind)) else f"{kind:08X}"
            key = f"{tag} v{version}"
            total, crc = chunks.get(key, (0, 0))
            chunks[key] = (total + length, zlib.crc32(data, crc))
        plugins[name] = chunks
    return plugins


# ---- report ----


def load(path):
    save = read_ess(path)
    save["skse"] = read_skse(os.path.splitext(path)[0] + ".skse")
    return save


def summary(path, s):
    i = s["info"]
    print(f"{os.path.basename(path)}")
    print(f"  {i['player']} lvl {i['level']}, {i['location']}, save {i['saveNumber']}, form version {s['formVersion']}")
    print(f"  {len(s['plugins'])} plugins + {len(s['light'])} light; {sum(s['changes'].values())} change forms")
    if s["papyrus"]:
        print("  papyrus: " + ", ".join(f"{k} {v}" for k, v in s["papyrus"].items()))
    if s["skse"] is not None:
        print(f"  co-save: {len(s['skse'])} plugins")


def diff_counts(title, a, b, unit=""):
    keys = sorted(set(a) | set(b), key=str)
    rows = [(k, a.get(k, 0), b.get(k, 0)) for k in keys if a.get(k, 0) != b.get(k, 0)]
    print(f"\n{title}: {'no differences' if not rows else f'{len(rows)} differ'}")
    for k, va, vb in rows:
        print(f"  {str(k):40} A {va:>10}{unit}   B {vb:>10}{unit}   ({vb - va:+}{unit})")


def diff_forms(a, b, show=8):
    """Change forms by ID: only in A, only in B, and in both with different contents."""
    rows = []
    for kind in sorted(set(a) | set(b)):
        fa, fb = a.get(kind, {}), b.get(kind, {})
        only_a = sorted(set(fa) - set(fb))
        only_b = sorted(set(fb) - set(fa))
        changed = sum(1 for k in set(fa) & set(fb) if fa[k] != fb[k])
        if only_a or only_b or changed:
            rows.append((kind, only_a, only_b, changed))
    print(f"\nChange forms by ID: {'no differences' if not rows else f'{len(rows)} types differ'}")
    for kind, only_a, only_b, changed in rows:
        print(f"  {kind:6} only in A {len(only_a):>6}   only in B {len(only_b):>6}   different contents {changed:>6}")
        for label, ids in (("only in A", only_a), ("only in B", only_b)):
            if ids:
                listed = ", ".join(f"{i:06X}" for i in ids[:show]) + (" ..." if len(ids) > show else "")
                print(f"         {label}: {listed}")


def compare(pa, pb):
    a, b = load(pa), load(pb)
    print("A: ", end="")
    summary(pa, a)
    print("B: ", end="")
    summary(pb, b)
    if a["info"]["player"] != b["info"]["player"]:
        print("\nWarning: different characters; most differences below are expected.")
    if a["plugins"] != b["plugins"] or a["light"] != b["light"]:
        print("\nWarning: the plugin lists differ; form IDs won't line up.")
    diff_counts("Change forms by type (count)", a["changes"], b["changes"])
    diff_counts("Change forms by type (bytes)", a["changeBytes"], b["changeBytes"], " B")
    diff_forms(a["changeForms"], b["changeForms"])
    names = lambda g: {f"{k} {GLOBAL_NAMES.get(k, '?')}": len(v) for k, v in g.items()}
    diff_counts("Global data blocks (bytes)", names(a["globals"]), names(b["globals"]), " B")
    if a["papyrus"] and b["papyrus"] and "error" not in a["papyrus"] and "error" not in b["papyrus"]:
        diff_counts("Papyrus", a["papyrus"], b["papyrus"])
    if a["skse"] is None or b["skse"] is None:
        print("\nCo-save: missing for " + ("A" if a["skse"] is None else "B"))
        return
    plugins = sorted(set(a["skse"]) | set(b["skse"]))
    flat = lambda s: {f"{p} / {c}": n for p in s for c, (n, _) in s[p].items()}
    only_a = [p for p in plugins if p not in b["skse"]]
    only_b = [p for p in plugins if p not in a["skse"]]
    print(f"\nCo-save plugins: {len(a['skse'])} in A, {len(b['skse'])} in B")
    for p in only_a:
        print(f"  only in A: {p}")
    for p in only_b:
        print(f"  only in B: {p}")
    diff_counts("Co-save chunks (bytes)", flat(a["skse"]), flat(b["skse"]), " B")
    same_size = [f"{p} / {c}" for p in plugins if p in a["skse"] and p in b["skse"]
                 for c, (n, crc) in a["skse"][p].items() if c in b["skse"][p] and b["skse"][p][c] != (n, crc) and b["skse"][p][c][0] == n]
    print(f"\nCo-save chunks with the same size but different contents: {len(same_size) or 'none'}")
    for name in same_size:
        print(f"  {name}")


if __name__ == "__main__":
    if len(sys.argv) == 2:
        s = load(sys.argv[1])
        summary(sys.argv[1], s)
        print("\nChange forms: " + ", ".join(f"{k} {v}" for k, v in s["changes"].most_common()))
        if s["skse"]:
            print("Co-save plugins: " + ", ".join(sorted(s["skse"])))
    elif len(sys.argv) == 3:
        compare(sys.argv[1], sys.argv[2])
    else:
        print(__doc__)
        sys.exit(1)
