"""View matrix recovery validated against every valid hero."""

Collects all array entries with a vtable in the module and a finite, in-range
world position, then accepts a matrix candidate only if it projects ALL of them
with positive W, in-bounds NDC and distinct screen points.
"""

import math
import struct
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "sdk"))
import vgb

GAME = "League of Legends.exe"
LO = 0x1000000000
HI = 0x7FFFFFFFFFFF

NEW_LOCAL = 0x1cbb810
ARR = 0x1cbb7f8
TEAM = 0x1cc
POS = 0xc20
NAME = 0x3f00

pid = 0
c = None
mod_lo = 0
mod_hi = 0


def rd(a, n):
    try:
        d = c.read(pid, a, n)
        return d if len(d) == n else None
    except Exception:
        return None


def heap(v):
    return LO <= v <= HI and (v & 7) == 0


def inmod(v):
    return mod_lo <= v < mod_hi


def nm(p):
    b = rd(p + NAME, 0x40)
    if not b:
        return ""
    return b.decode("utf-16-le", "replace").split("\x00")[0]


def main():
    global pid, c, mod_lo, mod_hi
    c = vgb.Client()
    c.open()
    c.require_version()

    pid = c.pid_by_name(GAME)
    base = c.main_base(pid)
    e = struct.unpack("<I", rd(base + 0x3C, 4))[0]
    size = struct.unpack("<I", rd(base + e + 0x50, 4))[0]
    mod_lo, mod_hi = base, base + size
    print("base", hex(base), "size", hex(size))

    local = struct.unpack("<Q", rd(base + NEW_LOCAL, 8))[0]
    print("local", hex(local), repr(nm(local)))

    st = rd(struct.unpack("<Q", rd(base + ARR, 8))[0], 0x20)
    begin, end = struct.unpack_from("<2Q", st, 0)
    span = min(end - begin, 0x40000)
    arr = bytearray()
    o = 0
    while o < span:
        k = min(0x1000, span - o)
        d = rd(begin + o, k)
        if not d:
            break
        arr += d
        o += k
    vals = [struct.unpack_from("<Q", arr, i)[0] for i in range(0, len(arr) - 7, 8)]

    heroes = []
    for x in vals:
        if not heap(x):
            continue
        v = rd(x, 8)
        if not v or not inmod(struct.unpack("<Q", v)[0]):
            continue
        p = rd(x + POS, 12)
        if not p:
            continue
        xyz = struct.unpack("<3f", p)
        if not all(math.isfinite(z) and abs(z) < 40000.0 for z in xyz):
            continue
        if sum(1 for z in xyz if abs(z) > 100.0) < 2:
            continue
        heroes.append((x, xyz, nm(x)))

    print("valid heroes", len(heroes))
    for x, xyz, name in heroes:
        print("  ", hex(x), [round(z, 1) for z in xyz], repr(name))

    if len(heroes) < 2:
        print("need at least two heroes")

    def project(f, xyz):
        w = xyz[0] * f[3] + xyz[1] * f[7] + xyz[2] * f[11] + f[15]
        if abs(w) < 1e-3:
            return None
        return ((xyz[0] * f[0] + xyz[1] * f[4] + xyz[2] * f[8] + f[12]) / w,
                (xyz[0] * f[1] + xyz[1] * f[5] + xyz[2] * f[9] + f[13]) / w, w)

    def test(f):
        if not all(math.isfinite(z) and abs(z) < 1e7 for z in f):
            return None
        if abs(f[15]) > 1.0:
            return None
        pts = []
        for _x, xyz, _n in heroes:
            r = project(f, xyz)
            if not r:
                return None
            if r[2] <= 1.0:
                return None
            if abs(r[0]) > 1.2 or abs(r[1]) > 1.2:
                return None
            pts.append(r)
        spread = 0.0
        for i in range(len(pts)):
            for j in range(i + 1, len(pts)):
                spread = max(spread, abs(pts[i][0] - pts[j][0]), abs(pts[i][1] - pts[j][1]))
        if len(pts) > 1 and spread < 0.02:
            return None
        return pts, spread

    print("== pointer scan ==")
    found = []
    off = 0
    while off < size and len(found) < 10:
        n = min(0x100000, size - off)
        data = rd(base + off, n)
        if data:
            for i in range(0, n - 7, 8):
                v = struct.unpack_from("<Q", data, i)[0]
                if not heap(v):
                    continue
                m = rd(v, 64)
                if not m:
                    continue
                f = struct.unpack("<16f", m)
                r = test(f)
                if not r:
                    continue
                print("  SLOT", hex(off + i), "inst", hex(v), "spread", round(r[1], 3))
                print("   m", [round(z, 6) for z in f])
                found.append((off + i, v, f))
                if len(found) >= 10:
                    break
        off += 0x100000
    print("hits", len(found))

    if found:
        print("== stability re-check ==")
        time.sleep(1.5)
        for slot, inst, f in found:
            v2 = rd(base + slot, 8)
            if not v2:
                print("  ", hex(slot), "unreadable")
                continue
            i2 = struct.unpack("<Q", v2)[0]
            m2 = rd(i2, 64)
            if not m2:
                print("  ", hex(slot), "target gone")
                continue
            f2 = struct.unpack("<16f", m2)
            same = sum(1 for a, b in zip(f, f2) if abs(a - b) < 0.01)
            r = test(f2)
            print("  ", hex(slot), "same-fields", same, "still-valid", bool(r))

    c.close()
    return 0


try:
    sys.exit(main())
except Exception as exc:
    print("ABORTED:", type(exc).__name__, exc)
    sys.exit(2)
