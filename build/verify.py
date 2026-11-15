"""Verify the recovered LocalPlayer and derive ObjectManager + ViewMatrix."""

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
OLD_LOCAL = 0x1F304B0
OLD_OBJ = 0x1EF0D28
OLD_MATRIX = 0x1FBA090

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
    print("pid", pid, "base", hex(base))

    local = struct.unpack("<Q", rd(base + NEW_LOCAL, 8))[0]
    print("local object", hex(local))
    if not in_heap(local):
        print("local is not a heap pointer")
        return 1

    buf = rd(local, 0x5000)
    if not buf:
        print("cannot read local")
        return 1

    teams = [(hex(o), struct.unpack_from("<i", buf, o)[0])
             for o in range(0x100, 0x400, 4)
             if struct.unpack_from("<i", buf, o)[0] in (100, 200)]
    print("team ints", teams)

    print("name @0x3f00", repr(buf[0x3f00:0x3f40].decode("utf-16-le", "replace").split("\x00")[0]))

    print("float triples 0x1a0..0x260")
    for o in range(0x1a0, 0x260, 4):
        x, y, z = struct.unpack_from("<3f", buf, o)
        if not all(math.isfinite(v) for v in (x, y, z)):
            continue
        if max(abs(x), abs(y), abs(z)) > 40000.0:
            continue
        if abs(x) < 1 and abs(y) < 1 and abs(z) < 1:
            continue
        print("  ", hex(o), [round(v, 2) for v in (x, y, z)])

    hits = {}
    chunk = 0x100000
    off = 0
    while off < size:
        n = min(chunk, size - off)
        data = rd(base + off, n)
        if data:
            for i in range(0, n - 7, 8):
                v = struct.unpack_from("<Q", data, i)[0]
                if in_heap(v):
                    hits.setdefault(v, []).append(base + off + i)
        off += chunk
    print("globals", len(hits))

    obj = None
    for v in hits:
        h = rd(v + 0x10, 0x20)
        if not h:
            continue
        begin, end = struct.unpack_from("<2Q", h, 8)
        if not in_heap(begin) or not in_heap(end) or end <= begin:
            continue
        span = end - begin
        if span % 8 or span < 0x10 or span > 0x8000:
            continue
        arr = rd(begin, span)
        if not arr:
            continue
        vals = [struct.unpack_from("<Q", arr, i)[0] for i in range(0, span - 7, 8)]
        if local not in vals:
            continue
        obj = hits[v][0] - base
        print("OBJECTMANAGER", hex(obj), "array", hex(begin), hex(end), "count", len(vals))
        for x in vals:
            if in_heap(x):
                t = rd(x + 0x1cc, 4)
                n = rd(x + 0x3f00, 0x20)
                if t and n:
                    print("   elem", hex(x), "team", struct.unpack("<i", t)[0],
                          repr(n.decode("utf-16-le", "replace").split("\x00")[0]))
        break

    expect_m = NEW_LOCAL + (OLD_MATRIX - OLD_LOCAL)
    print("expected matrix slot", hex(expect_m))
    found = 0
    for shift in range(0, 0x4000, 8):
        for cand in (expect_m + shift, expect_m - shift):
            if cand <= 0:
                continue
            inst = rd(base + cand, 8)
            if not inst:
                continue
            inst = struct.unpack("<Q", inst)[0]
            if not in_heap(inst):
                continue
            m = rd(inst, 64)
            if not m:
                continue
            f = struct.unpack("<16f", m)
            if not all(math.isfinite(x) for x in f):
                continue
            if abs(f[15] - 1.0) > 0.02:
                continue
            print("VIEWMATRIX", hex(cand), "instance", hex(inst))
            print("  m", [round(x, 4) for x in f])
            found = 1
            break
        if found:
            break

    print("RESULT local=%#x obj=%s matrix=%s" % (NEW_LOCAL, hex(obj) if obj else None,
                                                 hex(expect_m) if found else None))
    c.close()
    return 0


try:
    sys.exit(main())
except Exception as exc:
    print("ABORTED:", type(exc).__name__, exc)
    sys.exit(2)
