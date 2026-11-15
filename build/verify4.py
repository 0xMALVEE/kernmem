"""Confirm the array, recover HP/MAXHP, and locate the view matrix."""

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
OBJ_OFF = 0x1cbb7f8
POS_OFF = 0xc20
TEAM_OFF = 0x1cc
NAME_OFF = 0x3f00

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

    print("== array at ObjectManager ==")
    hdr = rd(base + OBJ_OFF, 0x20)
    begin, end = struct.unpack_from("<2Q", hdr, 0)
    print("  begin", hex(begin), "end", hex(end), "count", (end - begin) // 8)
    span = end - begin
    if not (in_heap(begin) and in_heap(end) and 0 < span <= 0x40000):
        print("  array implausible")
        return 1
    arr = rd(begin, min(span, 0x2000))
    vals = [struct.unpack_from("<Q", arr, i)[0] for i in range(0, len(arr) - 7, 8)]
    print("  contains local:", local in vals)
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
        print("   hero", hex(x), "team", team, "name", repr(nm(x)),
              "pos", [round(v, 1) for v in struct.unpack("<3f", p)] if p else "?")

    print("== hp/maxhp pair search on local ==")
    buf = rd(local, 0x5000)
    first = {}
    for o in range(0, len(buf) - 8, 4):
        a, b = struct.unpack_from("<2f", buf, o)
        if not (0.0 < a <= b <= 200000.0):
            continue
        if b < 100.0:
            continue
        first[o] = (a, b)
    time.sleep(1.2)
    buf2 = rd(local, 0x5000)
    for o, (a, b) in first.items():
        a2, b2 = struct.unpack_from("<2f", buf2, o)
        if abs(b2 - b) < 0.01 and abs(a2 - a) < max(1.0, a * 0.05):
            print("  candidate hp/maxhp @", hex(o), round(a, 1), "/", round(b, 1))

    print("== matrix pointer scan (whole module, relaxed) ==")
    hits = []
    off = 0
    chunk = 0x100000
    while off < size:
        n = min(chunk, size - off)
        data = rd(base + off, n)
        if data:
            for i in range(0, n - 7, 8):
                v = struct.unpack_from("<Q", data, i)[0]
                if in_heap(v):
                    hits.append((base + off + i, v))
        off += chunk
    print("  pointers", len(hits))

    found = 0
    for ga, v in hits:
        m = rd(v, 64)
        if not m:
            continue
        f = struct.unpack("<16f", m)
        if not all(math.isfinite(x) and abs(x) < 5000.0 for x in f):
            continue
        if abs(f[15] - 1.0) > 0.02:
            continue
        if max(abs(f[12]), abs(f[13]), abs(f[14])) > 0.5:
            continue
        if min(abs(f[0]), abs(f[5]), abs(f[10])) < 0.05:
            continue
        if abs(f[0]) + abs(f[5]) + abs(f[10]) < 1.0:
            continue
        print("  MATRIX slot", hex(ga - base), "inst", hex(v))
        print("   m", [round(x, 5) for x in f])
        found += 1
        if found >= 6:
            break
    if not found:
        print("  none")

    print("RESULT local=%#x obj=%#x pos=%#x team=%#x name=%#x" % (
        NEW_LOCAL, OBJ_OFF, POS_OFF, TEAM_OFF, NAME_OFF))
    c.close()
    return 0


try:
    sys.exit(main())
except Exception as exc:
    print("ABORTED:", type(exc).__name__, exc)
    sys.exit(2)
