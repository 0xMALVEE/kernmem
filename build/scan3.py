"""Offset scanner v3 - every read is fault tolerant.

A heap pointer that is mapped-but-unreadable aborts nothing; the scan simply
skips it. Heroes are recognised by team id + sane world position, LocalPlayer is
the module global pointing at one, and the array/matrix slots are then located
and re-verified.
"""

import math
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "sdk"))
import vgb

GAME = "League of Legends.exe"
LO = 0x1000000000
HI = 0x7FFFFFFFFFFF

TEAM = 0x239
POS = 0x23C
NAME = 0x4378
HP = 0x1080
MAXHP = 0x10D0
LEVEL = 0x4D58

pid = 0
c = None


def rd(a, n):
    try:
        d = c.read(pid, a, n)
        return d if len(d) == n else None
    except Exception:
        return None


def hero_shaped(p):
    buf = rd(p, POS + 12)
    if not buf:
        return None
    team = struct.unpack_from("<i", buf, TEAM)[0]
    if team not in (100, 200):
        return None
    x, y, z = struct.unpack_from("<3f", buf, POS)
    if not all(math.isfinite(v) for v in (x, y, z)):
        return None
    if max(abs(x), abs(y), abs(z)) > 40000.0:
        return None
    return team, (x, y, z)


def name_of(p):
    buf = rd(p + NAME, 0x40)
    if not buf:
        return ""
    return buf.decode("utf-16-le", "replace").split("\x00")[0]


def main():
    global pid, c
    c = vgb.Client()
    c.open()
    c.require_version()

    pid = c.pid_by_name(GAME)
    if not pid:
        print("game not running")
        return 1
    base = c.main_base(pid)
    print("pid", pid, "base", hex(base))

    e = struct.unpack("<I", rd(base + 0x3C, 4))[0]
    size = struct.unpack("<I", rd(base + e + 0x50, 4))[0]
    print("image size", hex(size))

    hits = {}
    chunk = 0x100000
    off = 0
    while off < size:
        n = min(chunk, size - off)
        data = rd(base + off, n)
        if data:
            for i in range(0, n - 7, 8):
                v = struct.unpack_from("<Q", data, i)[0]
                if LO <= v <= HI and (v & 7) == 0:
                    hits.setdefault(v, []).append(base + off + i)
        off += chunk
    print("unique heap pointers", len(hits))

    heroes = {}
    for v in hits:
        r = hero_shaped(v)
        if r:
            heroes[v] = r
    print("hero objects", len(heroes))

    for p, (team, pos) in list(heroes.items())[:25]:
        hp = rd(p + HP, 4)
        print("  hero", hex(p), "team", team, "pos", [round(x, 1) for x in pos],
              "hp", round(struct.unpack("<f", hp)[0], 1) if hp else "?",
              "name", repr(name_of(p)))

    if not heroes:
        print("no hero objects")
        return 1

    best = None
    for p in heroes:
        if p not in hits:
            continue
        nm = name_of(p)
        hp = rd(p + HP, 4)
        mhp = rd(p + MAXHP, 4)
        lvl = rd(p + LEVEL, 4)
        if not (hp and mhp and lvl):
            continue
        hp = struct.unpack("<f", hp)[0]
        mhp = struct.unpack("<f", mhp)[0]
        lvl = struct.unpack("<i", lvl)[0]
        if not (0 < mhp < 100000 and 0 < lvl < 31 and 0 <= hp <= mhp):
            continue
        score = 1 + (1 if nm else 0)
        if best is None or score > best[0]:
            best = (score, p, nm, hp, mhp, lvl)

    if not best:
        print("no local candidate")
        return 1

    _, local, nm, hp, mhp, lvl = best
    new_local = hits[local][0] - base
    print("LOCAL", hex(local), "name", repr(nm), "hp", round(hp, 1),
          "maxhp", round(mhp, 1), "level", lvl)
    print("LocalPlayer offset", hex(new_local),
          "globals", [hex(g) for g in hits[local][:3]])

    hero_set = set(heroes)

    def array_at(addr):
        buf = rd(addr + 0x10, 0x18)
        if not buf:
            return None
        begin, end = struct.unpack_from("<2Q", buf, 8)
        if not (LO <= begin <= HI and LO <= end <= HI) or end <= begin:
            return None
        span = end - begin
        if span % 8 or span > 0x4000:
            return None
        arr = rd(begin, span)
        if not arr:
            return None
        vals = struct.unpack("<%dQ" % (span // 8), arr)
        if local not in vals:
            return None
        if sum(1 for v in vals if v in hero_set) < 2:
            return None
        return begin, end

    obj = None
    for v in hits:
        hit = array_at(v)
        if hit:
            obj = hits[v][0] - base
            print("ObjectManager", hex(obj), "array", hex(hit[0]), hex(hit[1]))
            break

    mat = None
    for v in hits:
        m = rd(v, 64)
        if not m:
            continue
        f = struct.unpack("<16f", m)
        if not all(math.isfinite(x) for x in f):
            continue
        if abs(f[15] - 1.0) > 0.02:
            continue
        if max(abs(f[12]), abs(f[13]), abs(f[14])) > 0.02:
            continue
        if abs(f[0]) + abs(f[5]) + abs(f[10]) < 0.5:
            continue
        mat = hits[v][0] - base
        print("ViewMatrix", hex(mat), "instance", hex(v))
        print("  matrix", [round(x, 4) for x in f])
        break

    print("SUMMARY local=%s obj=%s matrix=%s" % (
        hex(new_local), hex(obj) if obj is not None else None,
        hex(mat) if mat is not None else None))
    c.close()
    return 0


try:
    sys.exit(main())
except Exception as exc:
    print("ABORTED:", type(exc).__name__, exc)
    sys.exit(2)
