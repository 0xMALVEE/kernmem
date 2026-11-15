"""Structural layout recovery v5.

Trusts nothing in the stale table. Validates candidate object vectors by their
elements' shape (vtable in module), then derives the real team / position / name
offsets by statistics across the elements, and only then looks for the module
global that holds the local hero.
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
WIN = 0x800

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


def plausible_pos(buf, o):
    if o < 0 or o + 12 > len(buf):
        return None
    x, y, z = struct.unpack_from("<3f", buf, o)
    if not all(math.isfinite(v) for v in (x, y, z)):
        return None
    if max(abs(x), abs(y), abs(z)) > 40000.0:
        return None
    if abs(x) < 1.0 and abs(y) < 1.0:
        return None
    return x, y, z


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

    cands = []
    for v in hits:
        h = rd(v + 0x10, 0x20)
        if not h:
            continue
        begin, end = struct.unpack_from("<2Q", h, 8)
        if not in_heap(begin) or not in_heap(end) or end <= begin:
            continue
        span = end - begin
        if span % 8 or span < 0x28 or span > 0x8000:
            continue
        count = span // 8
        if count < 5 or count > 2048:
            continue
        head = rd(begin, 0x40)
        if not head:
            continue
        vt = sum(1 for i in range(0, len(head) - 7, 8) if in_mod(q(head, i)))
        cands.append((vt, v, begin, end, count))

    cands.sort(reverse=True)
    print("vector candidates", len(cands))

    best = None
    for vt, v, begin, end, count in cands[:80]:
        span = min(end - begin, count * 8, 300 * 8)
        arr = rd(begin, span)
        if not arr:
            continue
        elems = [struct.unpack_from("<Q", arr, i)[0] for i in range(0, span - 7, 8)]
        elems = [x for x in elems if in_heap(x)]
        if not elems:
            continue

        bufs = []
        for p in elems:
            b = rd(p, WIN)
            if b:
                bufs.append((p, b))
        if len(bufs) < 3:
            continue

        team = Counter()
        for p, b in bufs:
            for o in range(0, len(b) - 4, 4):
                if struct.unpack_from("<i", b, o)[0] in (100, 200):
                    team[o] += 1
        if not team:
            continue

        hero_score = 0
        pos_pairs = Counter()
        for p, b in bufs:
            for to in list(team):
                if struct.unpack_from("<i", b, to)[0] not in (100, 200):
                    continue
                for o in range(max(0, to - 0x10), min(len(b) - 12, to + 0x40), 4):
                    if plausible_pos(b, o):
                        hero_score += 1
                        pos_pairs[(to, o)] += 1
                        break

        print("  cand", hex(hits[v][0] - base), "vt", vt, "count", count,
              "heroes", hero_score)
        if hero_score >= 2 and (best is None or hero_score > best[0]):
            best = (hero_score, v, begin, end, count, dict(pos_pairs))

    if not best:
        print("no vector scored any hero-shaped element")
        return 1

    hero_score, obj_ptr, begin, end, count, pos_pairs = best
    obj = hits[obj_ptr][0] - base
    print("ObjectManager", hex(obj), "heroes", hero_score, "count", count,
          "array", hex(begin), hex(end))

    team_off = Counter({t: n for (t, _p), n in pos_pairs.items()}).most_common(1)[0][0]
    pos_off = Counter({p: n for (_t, p), n in pos_pairs.items()}).most_common(1)[0][0]
    print("team_off", hex(team_off), "pos_off", hex(pos_off))

    span = min(end - begin, count * 8)
    arr = rd(begin, span)
    elems = [struct.unpack_from("<Q", arr, i)[0] for i in range(0, span - 7, 8)]
    elems = [x for x in elems if in_heap(x)]

    hero_bufs = []
    for p in elems:
        b = rd(p, WIN)
        if not b:
            continue
        if struct.unpack_from("<i", b, team_off)[0] in (100, 200) and plausible_pos(b, pos_off):
            hero_bufs.append((p, b))
    print("hero elements", len(hero_bufs))

    names = Counter()
    for p, b in hero_bufs:
        o = 0
        while o < len(b) - 8:
            run = 0
            while (o + run * 2 + 2 <= len(b) and 0x20 <= b[o + run * 2] <= 0x7E
                   and b[o + run * 2 + 1] == 0):
                run += 1
            if run >= 4:
                names[o] += 1
                o += run * 2
            else:
                o += 2

    print("name offsets", [(hex(k), n) for k, n in names.most_common(6)])
    name_off = names.most_common(1)[0][0] if names else None

    def nm_of(b):
        if name_off is None:
            return ""
        return b[name_off:name_off + 0x30].decode("utf-16-le", "replace").split("\x00")[0]

    for p, b in hero_bufs[:15]:
        print("  hero", hex(p), "team", struct.unpack_from("<i", b, team_off)[0],
              "pos", [round(x, 1) for x in struct.unpack_from("<3f", b, pos_off)],
              "name", repr(nm_of(b)),
              "globals", [hex(g) for g in hits.get(p, [])[:2]])

    local = None
    for p, b in hero_bufs:
        if p not in hits:
            continue
        nm = nm_of(b)
        if len(nm) >= 3:
            local = (p, nm)
            break

    if local:
        print("LOCAL", hex(local[0]), repr(local[1]))
        print("LocalPlayer offset", hex(hits[local[0]][0] - base))
    else:
        print("no local hero among the referenced elements")

    print("SUMMARY team=%s pos=%s name=%s local=%s" % (
        hex(team_off), hex(pos_off),
        hex(name_off) if name_off is not None else None,
        hex(hits[local[0]][0] - base) if local else None))

    c.close()
    return 0


try:
    sys.exit(main())
except Exception as exc:
    print("ABORTED:", type(exc).__name__, exc)
    sys.exit(2)
