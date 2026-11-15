"""Layout discovery.

Does not trust the stale table at all. Finds the object manager by its vector
shape, then derives the real team / position / name offsets by frequency across
the array's elements, and finally locates the module global that holds the local
hero.
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


def q(buf, off):
    return struct.unpack_from("<Q", buf, off)[0]


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

    best = None
    for v in hits:
        h = rd(v + 0x10, 0x20)
        if not h:
            continue
        begin, end = struct.unpack_from("<2Q", h, 8)
        if not in_heap(begin) or not in_heap(end) or end <= begin:
            continue
        span = end - begin
        if span % 8 or span < 8 or span > 0x8000:
            continue
        count = span // 8
        if count < 2 or count > 1024:
            continue
        arr = rd(begin, min(span, 0x200))
        if not arr:
            continue
        vals = [struct.unpack_from("<Q", arr, i)[0] for i in range(0, len(arr) - 7, 8)]
        good = sum(1 for x in vals if in_heap(x))
        if good < 2:
            continue
        vt = 0
        for x in vals[:12]:
            hdr = rd(x, 8)
            if hdr and in_mod(q(hdr, 0)):
                vt += 1
        score = vt * 10 + good
        if best is None or score > best[0]:
            best = (score, v, begin, end, count)

    if not best:
        print("no object array found")
        return 1

    score, obj_ptr, begin, end, count = best
    obj = hits[obj_ptr][0] - base
    print("ObjectManager", hex(obj), "score", score, "count", count,
          "array", hex(begin), hex(end))

    span = min(end - begin, 0x200)
    arr = rd(begin, span)
    elems = [struct.unpack_from("<Q", arr, i)[0] for i in range(0, span - 7, 8)]
    elems = [x for x in elems if in_heap(x)][:40]
    print("elements", len(elems))

    teams = Counter()
    names = Counter()
    objs = {}
    for p in elems:
        buf = rd(p, 0x6000)
        if not buf:
            continue
        objs[p] = buf
        for o in range(0, min(len(buf), 0x600) - 4, 4):
            v = struct.unpack_from("<i", buf, o)[0]
            if v in (100, 200):
                teams[o] += 1
        for o in range(0, len(buf) - 8, 2):
            run = 0
            while o + run * 2 + 2 <= len(buf) and 0x20 <= buf[o + run * 2] <= 0x7E and buf[o + run * 2 + 1] == 0:
                run += 1
            if run >= 4:
                names[o] += 1

    print("team offsets", [(hex(k), n) for k, n in teams.most_common(8)])
    print("name offsets", [(hex(k), n) for k, n in names.most_common(8)])

    if not teams:
        print("no team field")
        return 1

    team_off = teams.most_common(1)[0][0]
    pos_scores = Counter()
    for p, buf in objs.items():
        if struct.unpack_from("<i", buf, team_off)[0] not in (100, 200):
            continue
        for o in range(max(0, team_off - 8), min(len(buf), team_off + 0x40) - 12, 4):
            x, y, z = struct.unpack_from("<3f", buf, o)
            if not all(math.isfinite(v) for v in (x, y, z)):
                continue
            if max(abs(x), abs(y), abs(z)) > 40000.0:
                continue
            if abs(x) + abs(y) + abs(z) < 0.5:
                continue
            pos_scores[o] += 1

    print("pos offsets", [(hex(k), n) for k, n in pos_scores.most_common(8)])
    pos_off = pos_scores.most_common(1)[0][0] if pos_scores else None
    name_off = names.most_common(1)[0][0] if names else None

    local = None
    if name_off is not None:
        for p, buf in objs.items():
            if p not in hits:
                continue
            raw = buf[name_off:name_off + 0x40]
            nm = raw.decode("utf-16-le", "replace").split("\x00")[0]
            if len(nm) >= 3:
                local = (p, nm)
                break

    print("SUMMARY team=%s pos=%s name=%s local=%s" % (
        hex(team_off) if team_off is not None else None,
        hex(pos_off) if pos_off is not None else None,
        hex(name_off) if name_off is not None else None,
        (hex(local[0]), local[1]) if local else None))

    if local:
        print("LocalPlayer offset", hex(hits[local[0]][0] - base))

    for p, buf in list(objs.items())[:12]:
        t = struct.unpack_from("<i", buf, team_off)[0]
        pos = struct.unpack_from("<3f", buf, pos_off) if pos_off is not None else (0, 0, 0)
        nm = buf[name_off:name_off + 0x30].decode("utf-16-le", "replace").split("\x00")[0] if name_off is not None else ""
        print("  obj", hex(p), "team", t, "pos", [round(x, 1) for x in pos], "name", repr(nm))

    c.close()
    return 0


try:
    sys.exit(main())
except Exception as exc:
    print("ABORTED:", type(exc).__name__, exc)
    sys.exit(2)
