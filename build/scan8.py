"""Hero recovery with a layout-banded shape test.

Cheap prefilter on a team id in 0x100..0x400, then require a vtable in the
module, a sane position next to the team id, and a NUL-terminated 3-24 char
UTF-16 name run in the 0x3f00..0x4a00 band. Then confirm by array containment.
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
TEAM_LO, TEAM_HI = 0x100, 0x400
NAME_LO, NAME_HI = 0x3F00, 0x4A00

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


def name_run(buf, lo, hi):
    o = lo
    while o < min(hi, len(buf)) - 4:
        run = 0
        while (o + run * 2 + 2 <= len(buf) and 0x20 <= buf[o + run * 2] <= 0x7E
               and buf[o + run * 2 + 1] == 0):
            run += 1
        if 3 <= run <= 24:
            end = o + run * 2
            if end + 1 < len(buf) and buf[end] == 0 and buf[end + 1] == 0:
                return o, run
        o += 2
    return None


def examine(v):
    head = rd(v, TEAM_HI + 4)
    if not head:
        return None
    if not in_mod(struct.unpack_from("<Q", head, 0)[0]):
        return None

    teams = [o for o in range(TEAM_LO, TEAM_HI - 4, 4)
             if struct.unpack_from("<i", head, o)[0] in (100, 200)]
    if not teams:
        return None

    body = rd(v, WIN)
    if not body:
        return None

    for to in teams:
        for po in range(max(0, to - 8), min(WIN - 12, to + 0x18), 4):
            x, y, z = struct.unpack_from("<3f", body, po)
            if not all(math.isfinite(t) for t in (x, y, z)):
                continue
            if max(abs(x), abs(y), abs(z)) > 40000.0:
                continue
            if abs(x) < 1.0 and abs(y) < 1.0 and abs(z) < 1.0:
                continue
            nm = name_run(body, NAME_LO, NAME_HI)
            if not nm:
                continue
            return to, po, (x, y, z), nm
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

    tc = Counter()
    pc = Counter()
    nc = Counter()
    for v, to, po, pos, nm in found:
        tc[to] += 1
        pc[po] += 1
        nc[nm[0]] += 1
    print("team offsets", [(hex(k), n) for k, n in tc.most_common(5)])
    print("pos offsets", [(hex(k), n) for k, n in pc.most_common(5)])
    print("name offsets", [(hex(k), n) for k, n in nc.most_common(5)])

    for v, to, po, pos, nm in found[:25]:
        print("  hero", hex(v), "team", struct.unpack_from("<i", rd(v, to + 4), 0)[0],
              "team@", hex(to), "pos@", hex(po), "pos", [round(x, 1) for x in pos],
              "name@", hex(nm[0]), repr(name_at(v, nm[0], nm[1])),
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
        print("ObjectManager offset", hex(obj), "array", hex(begin), hex(end), "count", len(vals))
        break

    local = None
    for v, to, po, pos, nm in found:
        if v not in hits:
            continue
        s = name_at(v, nm[0], nm[1])
        if len(s) < 3 or "\\" in s or ".dll" in s.lower():
            continue
        if local is None or len(s) > len(local[1]):
            local = (v, s, to, po, nm)

    if local:
        v, s, to, po, nm = local
        print("LOCAL", hex(v), repr(s))
        print("SUMMARY local=%s obj=%s team=%s pos=%s name=%s" % (
            hex(hits[v][0] - base), hex(obj) if obj else None,
            hex(to), hex(po), hex(nm[0])))
    else:
        print("no named local hero")

    c.close()
    return 0


try:
    sys.exit(main())
except Exception as exc:
    print("ABORTED:", type(exc).__name__, exc)
    sys.exit(2)
