"""Hero recovery with a full-width window.

Reads 0x5000 per candidate so the name field (~0x4378) is inside the window,
and requires three independent signals at once: team id 100/200, a plausible
world position near it, and an ASCII-in-UTF16 name run. Then confirms by array
containment and locates the module global that holds the local hero.
"""

import math
import struct
import sys
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "sdk"))
import vgb

GAME = "League of Legends.exe"
LO = 0x1000000000
HI = 0x7FFFFFFFFFFF
WIN = 0x5000
TEAM_WIN = 0x1000

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


def in_heap(v):
    return LO <= v <= HI and (v & 7) == 0


def in_mod(v):
    return mod_lo <= v < mod_hi


def pos_at(buf, o):
    if o < 0 or o + 12 > len(buf):
        return None
    x, y, z = struct.unpack_from("<3f", buf, o)
    if not all(math.isfinite(v) for v in (x, y, z)):
        return None
    if max(abs(x), abs(y), abs(z)) > 40000.0:
        return None
    if abs(x) < 100.0 and abs(z) < 100.0:
        return None
    return x, y, z


def name_runs(buf, start, stop):
    out = []
    o = start
    while o < stop - 8:
        run = 0
        while (o + run * 2 + 2 <= stop and 0x20 <= buf[o + run * 2] <= 0x7E
               and buf[o + run * 2 + 1] == 0):
            run += 1
        if run >= 3:
            out.append((o, run))
            o += run * 2
        else:
            o += 2
    return out


def examine(v):
    body = rd(v, WIN)
    if not body:
        return None

    teams = [o for o in range(0, min(len(body), TEAM_WIN) - 4, 4)
             if struct.unpack_from("<i", body, o)[0] in (100, 200)]
    if not teams:
        return None

    runs = name_runs(body, 0x100, WIN)
    if not runs:
        return None

    for to in teams:
        for po in range(max(0, to - 0x30), min(WIN - 12, to + 0x50), 4):
            pos = pos_at(body, po)
            if not pos:
                continue
            nm = [(o, r) for (o, r) in runs if o >= po]
            if not nm:
                continue
            return to, po, pos, nm[0]
    return None


def name_at(p, o, run):
    b = rd(p + o, run * 2 + 2)
    if not b:
        return ""
    return b.decode("utf-16-le", "replace").split("\x00")[0]


def main():
    global pid, c, mod_lo, mod_hi
    c = vgb.Client()
    c.open()
    c.require_version()

    pid = c.pid_by_name(GAME)
    if not pid:
        print("game not running")
        return 1
    base = c.main_base(pid)
    e = struct.unpack("<I", rd(base + 0x3C, 4))[0]
    size = struct.unpack("<I", rd(base + e + 0x50, 4))[0]
    mod_lo, mod_hi = base, base + size
    print("pid", pid, "base", hex(base), "size", hex(size))

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
    print("heap-pointer globals", len(hits))

    found = []
    for v in hits:
        r = examine(v)
        if r:
            found.append((v,) + r)
    print("hero-shaped objects", len(found))

    team_c = Counter()
    pos_c = Counter()
    name_c = Counter()
    for v, to, po, pos, nm in found:
        team_c[to] += 1
        pos_c[po] += 1
        name_c[nm[0]] += 1

    print("team offsets", [(hex(k), n) for k, n in team_c.most_common(6)])
    print("pos offsets", [(hex(k), n) for k, n in pos_c.most_common(6)])
    print("name offsets", [(hex(k), n) for k, n in name_c.most_common(6)])

    if not found:
        print("nothing hero-shaped")
        return 1

    for v, to, po, pos, nm in found[:20]:
        print("  hero", hex(v), "team", struct.unpack("<i", struct.pack("<i", 0))[0],
              "team@", hex(to), "pos", [round(x, 1) for x in pos],
              "name", repr(name_at(v, nm[0], nm[1])),
              "globals", [hex(g) for g in hits[v][:2]])

    hero_set = set(v for v, *_ in found)

    obj = None
    for v, *_ in found:
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
        if sum(1 for x in vals if x in hero_set) < 2:
            continue
        obj = hits[v][0] - base
        print("ObjectManager offset", hex(obj), "array", hex(begin), hex(end),
              "count", len(vals))
        break

    best = None
    for v, to, po, pos, nm in found:
        if v not in hits:
            continue
        nm_s = name_at(v, nm[0], nm[1])
        if len(nm_s) < 3:
            continue
        if best is None or len(nm_s) > len(best[1]):
            best = (v, nm_s, nm)

    if best:
        v, nm_s, nm = best
        print("LOCAL", hex(v), repr(nm_s))
        print("LocalPlayer offset", hex(hits[v][0] - base))
        print("SUMMARY local=%s obj=%s team=%s pos=%s name=%s" % (
            hex(hits[v][0] - base), hex(obj) if obj else None,
            hex(nm and found[0][1]), hex(found[0][2]), hex(nm[0])))
    else:
        print("no named local hero")

    c.close()
    return 0


try:
    sys.exit(main())
except Exception as exc:
    print("ABORTED:", type(exc).__name__, exc)
    sys.exit(2)
