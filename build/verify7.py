"""Pick the real object array and find the view matrix."""

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
TEAM = 0x1cc
POS = 0xc20
NAME = 0x3f00
HP = 0x8a0
MAXHP = 0x8a4
LEVEL = 0x4d58

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


def printable(s):
    return len(s) >= 3 and all(0x20 <= ord(ch) <= 0x7E for ch in s)


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

    for cand, vo in ((0x1cb3188, 0x8), (0x1cbb7f8, 0x0)):
        print("== candidate", hex(cand), "vec@", hex(vo))
        p = struct.unpack("<Q", rd(base + cand, 8))[0]
        if not heap(p):
            print("   not a pointer")
            continue
        st = rd(p, 0x40)
        if not st:
            print("   cannot read struct")
            continue
        print("   struct", hex(p), st[:0x20].hex())
        begin, end, cap = struct.unpack_from("<3Q", st, vo)
        print("   begin", hex(begin), "end", hex(end), "cap", hex(cap))
        span = end - begin
        if not (heap(begin) and heap(end) and 0 < span <= 0x40000):
            print("   implausible")
            continue
        n = span // 8
        idxs = [int(i * (n - 1) / 59) for i in range(60)] if n > 60 else list(range(n))
        vt = pr = tm = ok = 0
        for i in idxs:
            b = rd(begin + i * 8, 8)
            if not b:
                continue
            ok += 1
            x = struct.unpack("<Q", b)[0]
            if not heap(x):
                continue
            h = rd(x, 8)
            if h and inmod(struct.unpack("<Q", h)[0]):
                vt += 1
            if printable(nm(x)):
                pr += 1
            t = rd(x + TEAM, 4)
            if t and struct.unpack("<i", t)[0] in (100, 200):
                tm += 1
        print("   sampled", ok, "vtable", vt, "printable", pr, "team", tm, "entries", n)

    print("== heroes in 0x1cbb7f8 array ==")
    p = struct.unpack("<Q", rd(base + 0x1cbb7f8, 8))[0]
    st = rd(p, 0x20)
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
    print("   entries", len(vals))
    for x in vals:
        if not heap(x):
            continue
        t = rd(x + TEAM, 4)
        if not t or struct.unpack("<i", t)[0] not in (100, 200):
            continue
        pz = rd(x + POS, 12)
        hh = rd(x + HP, 8)
        lv = rd(x + LEVEL, 4)
        print("   hero", hex(x), "team", struct.unpack("<i", t)[0], repr(nm(x)),
              [round(z, 1) for z in struct.unpack("<3f", pz)] if pz else "?",
              "hp", [round(z, 1) for z in struct.unpack("<2f", hh)] if hh else "?",
              "lvl", struct.unpack("<i", lv)[0] if lv else "?")

    print("== matrix by pointer target ==")
    lp = struct.unpack("<3f", rd(local + POS, 12))
    found = 0
    off = 0
    while off < size and found < 6:
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
                if not all(math.isfinite(x) and abs(x) < 1e7 for x in f):
                    continue
                if sum(1 for x in f if abs(x) > 1e-6) < 8:
                    continue
                w = lp[0] * f[3] + lp[1] * f[7] + lp[2] * f[11] + f[15]
                if w <= 1.0:
                    continue
                cx = lp[0] * f[0] + lp[1] * f[4] + lp[2] * f[8] + f[12]
                cy = lp[0] * f[1] + lp[1] * f[5] + lp[2] * f[9] + f[13]
                if abs(cx / w) > 1.5 or abs(cy / w) > 1.5:
                    continue
                print("   MATRIX slot", hex(off + i), "inst", hex(v), "w", round(w, 1),
                      "ndc", round(cx / w, 3), round(cy / w, 3))
                print("    m", [round(x, 5) for x in f])
                found += 1
                if found >= 6:
                    break
        off += 0x100000
    if not found:
        print("   none")

    c.close()
    return 0


try:
    sys.exit(main())
except Exception as exc:
    print("ABORTED:", type(exc).__name__, exc)
    sys.exit(2)
