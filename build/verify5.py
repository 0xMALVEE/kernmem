"""Full array read + direct matrix block scan."""

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
OBJ_OFF = 0x1cbb7f8
POS_OFF = 0xc20
TEAM_OFF = 0x1cc
NAME_OFF = 0x3f00
HP_OFF = 0x8a0
MAXHP_OFF = 0x8a4

pid = 0
c = None


def rd(a, n):
    try:
        d = c.read(pid, a, n)
        return d if len(d) == n else None
    except Exception:
        return None


def rd_chunked(a, n, step=0x1000):
    out = bytearray()
    off = 0
    while off < n:
        k = min(step, n - off)
        d = rd(a + off, k)
        if not d:
            break
        out += d
        off += k
    return bytes(out)


def in_heap(v):
    return LO <= v <= HI and (v & 7) == 0


def nm(p):
    b = rd(p + NAME_OFF, 0x40)
    if not b:
        return ""
    return b.decode("utf-16-le", "replace").split("\x00")[0]


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
    print("local", hex(local), "name", repr(nm(local)))
    lp = struct.unpack("<3f", rd(local + POS_OFF, 12))
    print("local pos", [round(v, 1) for v in lp])

    hdr = rd(base + OBJ_OFF, 0x10)
    begin, end = struct.unpack_from("<2Q", hdr, 0)
    span = end - begin
    print("array", hex(begin), hex(end), "span", hex(span))

    arr = rd_chunked(begin, min(span, 0x22000))
    print("read", len(arr), "bytes")
    vals = [struct.unpack_from("<Q", arr, i)[0] for i in range(0, len(arr) - 7, 8)]
    print("entries", len(vals), "contains local:", local in vals)

    heroes = []
    for x in vals:
        if not in_heap(x):
            continue
        t = rd(x + TEAM_OFF, 4)
        if not t:
            continue
        team = struct.unpack("<i", t)[0]
        if team not in (100, 200):
            continue
        p = rd(x + POS_OFF, 12)
        hp = rd(x + HP_OFF, 8)
        heroes.append(x)
        print("  hero", hex(x), "team", team, "name", repr(nm(x)),
              "pos", [round(v, 1) for v in struct.unpack("<3f", p)] if p else "?",
              "hp/max", [round(v, 1) for v in struct.unpack("<2f", hp)] if hp else "?")
    print("heroes", len(heroes))

    print("== direct 16-float matrix blocks (validated by clipW) ==")
    found = 0
    off = 0
    chunk = 0x100000
    while off < size and found < 5:
        n = min(chunk, size - off)
        data = rd(base + off, n)
        if data:
            for i in range(0, n - 63, 4):
                f = struct.unpack_from("<16f", data, i)
                if not all(math.isfinite(x) and abs(x) < 100000.0 for x in f):
                    continue
                if abs(f[15]) > 0.001 and abs(f[15] - 1.0) > 0.001:
                    continue
                if min(abs(f[0]), abs(f[5]), abs(f[10])) < 0.05:
                    continue
                if abs(f[11]) < 0.1 and abs(f[15] - 1.0) > 0.001:
                    continue
                w = lp[0] * f[3] + lp[1] * f[7] + lp[2] * f[11] + f[15]
                if w <= 0.0:
                    continue
                cx = lp[0] * f[0] + lp[1] * f[4] + lp[2] * f[8] + f[12]
                cy = lp[0] * f[1] + lp[1] * f[5] + lp[2] * f[9] + f[13]
                sx = cx / w
                sy = cy / w
                if abs(sx) > 2.0 or abs(sy) > 2.0:
                    continue
                print("  MATRIX slot", hex(off + i), "clipW", round(w, 2), "ndc", round(sx, 3), round(sy, 3))
                print("   m", [round(x, 5) for x in f])
                found += 1
                if found >= 5:
                    break
        off += chunk
    if not found:
        print("  none")

    c.close()
    return 0


try:
    sys.exit(main())
except Exception as exc:
    print("ABORTED:", type(exc).__name__, exc)
    sys.exit(2)
