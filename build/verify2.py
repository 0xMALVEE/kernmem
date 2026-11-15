"""Locate the real position field, confirm the object array, and find the matrix."""

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
OBJ_CAND = 0x1cbba98

pid = 0
c = None


def rd(a, n):
    try:
        d = c.read(pid, a, n)
        return d if len(d) == n else None
    except Exception:
        return None


def in_heap(v):
    return LO <= v <= HI and (v & 7) == 0


def main():
    global pid, c
    c = vgb.Client()
    c.open()
    c.require_version()
    pid = c.pid_by_name(GAME)
    base = c.main_base(pid)
    e = struct.unpack("<I", rd(base + 0x3C, 4))[0]
    size = struct.unpack("<I", rd(base + e + 0x50, 4))[0]
    print("pid", pid, "base", hex(base), "size", hex(size))

    local = struct.unpack("<Q", rd(base + NEW_LOCAL, 8))[0]
    print("local", hex(local))
    buf = rd(local, 0x5000)
    if not buf:
        print("cannot read local")
        return 1

    print("== world-range float triples ==")
    for o in range(0, len(buf) - 12, 4):
        x, y, z = struct.unpack_from("<3f", buf, o)
        if not all(math.isfinite(v) for v in (x, y, z)):
            continue
        if max(abs(x), abs(y), abs(z)) > 20000.0:
            continue
        if sum(1 for v in (x, y, z) if abs(v) > 100.0) < 2:
            continue
        print("  ", hex(o), [round(v, 1) for v in (x, y, z)])

    print("== array candidate ==")
    h = rd(base + OBJ_CAND + 0x10, 0x20)
    if h:
        begin, end = struct.unpack_from("<2Q", h, 8)
        print("  begin", hex(begin), "end", hex(end))
        if in_heap(begin) and in_heap(end) and end > begin and (end - begin) <= 0x8000:
            span = end - begin
            arr = rd(begin, span)
            if arr:
                vals = [struct.unpack_from("<Q", arr, i)[0] for i in range(0, span - 7, 8)]
                print("  count", len(vals), "contains local:", local in vals)
                for x in vals[:40]:
                    if not in_heap(x):
                        continue
                    t = rd(x + 0x1cc, 4)
                    n = rd(x + 0x3f00, 0x20)
                    hp = rd(x + 0x1080, 4)
                    print("   elem", hex(x), "team", struct.unpack("<i", t)[0] if t else "?",
                          "name", repr(n.decode("utf-16-le", "replace").split("\x00")[0]) if n else "?",
                          "hp", round(struct.unpack("<f", hp)[0], 1) if hp else "?")

    print("== matrix hunt ==")
    found = 0
    start = 0x1d00000
    stop = min(size, 0x1e40000)
    while start < stop and not found:
        n = min(0x40000, stop - start)
        data = rd(base + start, n)
        if data:
            for i in range(0, n - 7, 8):
                v = struct.unpack_from("<Q", data, i)[0]
                if not in_heap(v):
                    continue
                m = rd(v, 64)
                if not m:
                    continue
                f = struct.unpack("<16f", m)
                if not all(math.isfinite(x) for x in f):
                    continue
                if abs(f[15] - 1.0) > 0.2:
                    continue
                if max(abs(f[12]), abs(f[13]), abs(f[14])) > 1.0:
                    continue
                if abs(f[0]) < 0.3 and abs(f[5]) < 0.3:
                    continue
                if max(abs(f[3]), abs(f[7]), abs(f[11])) < 0.01:
                    continue
                print("  MATRIX", hex(start + i), "inst", hex(v))
                print("   m", [round(x, 4) for x in f])
                found = 1
                break
        start += 0x40000

    if not found:
        print("  no matrix in the scanned band")

    c.close()
    return 0


try:
    sys.exit(main())
except Exception as exc:
    print("ABORTED:", type(exc).__name__, exc)
    sys.exit(2)
