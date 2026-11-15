"""View matrix recovery with an orthonormality requirement.

A real world-to-clip matrix has orthonormal basis columns. Combined with the
clipW/NDC test and a per-axis sensitivity test, that rejects the constant junk
blocks the looser scans kept matching.
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

NEW_LOCAL = 0x1cbb810
POS = 0xc20

pid = 0
c = None


def rd(a, n):
    try:
        d = c.read(pid, a, n)
        return d if len(d) == n else None
    except Exception:
        return None


def heap(v):
    return LO <= v <= HI and (v & 7) == 0


def basis_ok(f):
    cols = [(f[0], f[1], f[2]), (f[4], f[5], f[6]), (f[8], f[9], f[10])]
    norms = [math.sqrt(sum(x * x for x in col)) for col in cols]
    if min(norms) < 1e-9:
        return None
    mx = max(norms)
    if (mx - min(norms)) / mx > 0.15:
        return None
    n = [tuple(x / norms[i] for x in cols[i]) for i in range(3)]
    for i in range(3):
        for j in range(i + 1, 3):
            if abs(sum(n[i][k] * n[j][k] for k in range(3))) > 0.08:
                return None
    return n


def test(f, lp):
    if not all(math.isfinite(x) and abs(x) < 1e7 for x in f):
        return None
    if abs(f[15]) > 0.5:
        return None
    if not basis_ok(f):
        return None

    def ndc(x, y, z):
        w = x * f[3] + y * f[7] + z * f[11] + f[15]
        if abs(w) < 1e-6:
            return None
        return ((x * f[0] + y * f[4] + z * f[8] + f[12]) / w,
                (x * f[1] + y * f[5] + z * f[9] + f[13]) / w)

    base = ndc(lp[0], lp[1], lp[2])
    if not base:
        return None
    if abs(base[0]) > 1.3 or abs(base[1]) > 1.3:
        return None

    spread = 0.0
    for dx, dy, dz in ((200, 0, 0), (0, 200, 0), (0, 0, 200)):
        o = ndc(lp[0] + dx, lp[1] + dy, lp[2] + dz)
        if not o:
            continue
        spread = max(spread, abs(o[0] - base[0]), abs(o[1] - base[1]))
    if spread < 0.005:
        return None
    return base, spread


def main():
    global pid, c
    c = vgb.Client()
    c.open()
    c.require_version()

    pid = c.pid_by_name(GAME)
    base = c.main_base(pid)
    e = struct.unpack("<I", rd(base + 0x3C, 4))[0]
    size = struct.unpack("<I", rd(base + e + 0x50, 4))[0]
    print("base", hex(base), "size", hex(size))

    local = struct.unpack("<Q", rd(base + NEW_LOCAL, 8))[0]
    lp = struct.unpack("<3f", rd(local + POS, 12))
    print("local", hex(local), "pos", [round(v, 1) for v in lp])

    print("== pointer-target scan ==")
    hits = 0
    found = 0
    off = 0
    while off < size and found < 8:
        n = min(0x100000, size - off)
        data = rd(base + off, n)
        if data:
            for i in range(0, n - 7, 8):
                v = struct.unpack_from("<Q", data, i)[0]
                if not heap(v):
                    continue
                hits += 1
                m = rd(v, 64)
                if not m:
                    continue
                f = struct.unpack("<16f", m)
                r = test(f, lp)
                if not r:
                    continue
                nd, spread = r
                print("  MATRIX slot", hex(off + i), "inst", hex(v),
                      "ndc", round(nd[0], 3), round(nd[1], 3), "spread", round(spread, 3))
                print("   m", [round(x, 6) for x in f])
                found += 1
                if found >= 8:
                    break
        off += 0x100000
    print("pointers", hits, "hits", found)

    if not found:
        print("== inline scan ==")
        off = 0
        while off < size and found < 8:
            n = min(0x100000, size - off)
            data = rd(base + off, n)
            if data:
                for i in range(0, n - 63, 4):
                    f = struct.unpack_from("<16f", data, i)
                    r = test(f, lp)
                    if not r:
                        continue
                    nd, spread = r
                    print("  INLINE", hex(off + i), "ndc", round(nd[0], 3), round(nd[1], 3),
                          "spread", round(spread, 3))
                    print("   m", [round(x, 6) for x in f])
                    found += 1
                    if found >= 8:
                        break
            off += 0x100000
        print("inline hits", found)

    c.close()
    return 0


try:
    sys.exit(main())
except Exception as exc:
    print("ABORTED:", type(exc).__name__, exc)
    sys.exit(2)
