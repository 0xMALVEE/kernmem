"""Neighbour dump around the local hero + sensitivity-tested matrix scan."""

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
ARR = 0x1cbb7f8
TEAM = 0x1cc
POS = 0xc20
NAME = 0x3f00
HP = 0x8a0

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


def describe(x):
    v = rd(x, 8)
    vt = struct.unpack("<Q", v)[0] if v else 0
    t = rd(x + TEAM, 4)
    team = struct.unpack("<i", t)[0] if t else None
    p = rd(x + POS, 12)
    pos = [round(z, 1) for z in struct.unpack("<3f", p)] if p else None
    h = rd(x + HP, 8)
    hp = [round(z, 1) for z in struct.unpack("<2f", h)] if h else None
    return "vtable=%s team=%s pos=%s hp=%s name=%r" % (
        "mod" if inmod(vt) else hex(vt), team, pos, hp, nm(x))


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
    lp = struct.unpack("<3f", rd(local + POS, 12))
    print("local", hex(local), repr(nm(local)), "pos", [round(v, 1) for v in lp])

    st = rd(struct.unpack("<Q", rd(base + ARR, 8))[0], 0x20)
    begin, end, cap = struct.unpack_from("<3Q", st, 0)
    n = (end - begin) // 8
    print("array", hex(begin), "entries", n, "cap", hex(cap))

    probe = rd(begin, min(n * 8, 0x4000))
    vals = [struct.unpack_from("<Q", probe, i)[0] for i in range(0, len(probe) - 7, 8)]
    idx = vals.index(local) if local in vals else -1
    print("local index in first", len(vals), "entries:", idx)

    if idx >= 0:
        lo = max(0, idx - 12)
        for i in range(lo, min(len(vals), idx + 13)):
            x = vals[i]
            mark = "  <<<" if i == idx else ""
            if not heap(x):
                print("  [%d] %s not-a-pointer%s" % (i, hex(x), mark))
                continue
            print("  [%d] %s %s%s" % (i, hex(x), describe(x), mark))

    print("== matrix scan with sensitivity ==")
    found = 0
    band_lo = max(0, NEW_LOCAL - 0x400000)
    band_hi = min(size, NEW_LOCAL + 0x600000)
    off = band_lo
    while off < band_hi and found < 6:
        nread = min(0x100000, band_hi - off)
        data = rd(base + off, nread)
        if data:
            for i in range(0, nread - 63, 4):
                f = struct.unpack_from("<16f", data, i)
                if not all(math.isfinite(x) and abs(x) < 1e8 for x in f):
                    continue
                if abs(f[15]) > 0.01 and abs(f[15] - 1.0) > 0.01:
                    continue
                w = lp[0] * f[3] + lp[1] * f[7] + lp[2] * f[11] + f[15]
                if w <= 1.0:
                    continue
                cx = lp[0] * f[0] + lp[1] * f[4] + lp[2] * f[8] + f[12]
                cy = lp[0] * f[1] + lp[1] * f[5] + lp[2] * f[9] + f[13]
                nx, ny = cx / w, cy / w
                if abs(nx) > 1.2 or abs(ny) > 1.2:
                    continue
                px, py, pz = lp[0] + 500.0, lp[1], lp[2]
                w2 = px * f[3] + py * f[7] + pz * f[11] + f[15]
                if w2 <= 1.0:
                    continue
                cx2 = px * f[0] + py * f[4] + pz * f[8] + f[12]
                if abs(cx2 / w2 - nx) < 0.002:
                    continue
                print("  MATRIX", hex(off + i), "ndc", round(nx, 3), round(ny, 3),
                      "ndc+500", round(cx2 / w2, 3), "w", round(w, 1))
                print("   m", [round(x, 5) for x in f])
                found += 1
                if found >= 6:
                    break
        off += 0x100000
    if not found:
        print("  none")

    c.close()
    return 0


try:
    sys.exit(main())
except Exception as exc:
    print("ABORTED:", type(exc).__name__, exc)
    sys.exit(2)
