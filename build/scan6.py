"""Hero-shape recovery.

Per candidate object, require several independent signals together: a team id
of 100/200, a plausible world position within a short distance of it, and an
ASCII-in-UTF16 name run. Then confirm by array containment and locate the module
global that holds the local hero.
"""

import math
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "sdk"))
import vgb

GAME = "League of Legends.exe"
LO = 0x1000000000
HI = 0x7FFFFFFFFFFF
WIN = 0x700

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


def examine(p):
    head = rd(p, 0x100)
    if not head:
        return None
    teams = [o for o in range(0, len(head) - 4, 4)
             if struct.unpack_from("<i", head, o)[0] in (100, 200)]
    if not teams:
        return None

    body = rd(p, WIN)
    if not body:
        return None

    runs = name_runs(body, 0x40, WIN)
    best = None
    for to in teams:
        for po in range(max(0, to - 0x30), min(WIN - 12, to + 0x50), 4):
            pos = pos_at(body, po)
            if not pos:
                continue
            nm = [(o, r) for (o, r) in runs if o >= po]
            score = 2 + (1 if nm else 0)
            if best is None or score > best[0]:
                best = (score, to, po, pos, nm[:1])
            break
    return best


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
    done = 0
    for v in hits:
        r = examine(v)
        done += 1
        if r:
            found.append((v,) + r)
            if len(found) % 5 == 0:
                print("  ...", len(found), "at", done)
    print("hero-shaped objects", len(found))

    if not found:
        print("nothing hero-shaped")
        return 1

    for v, score, to, po, pos, nm in found[:25]:
        label = ""
        if nm:
            o, r = nm[0]
            label = body_name(v, o, r)
        print("  hero", hex(v), "score", score, "team@", hex(to), "pos@", hex(po),
              "pos", [round(x, 1) for x in pos], "name", repr(label),
              "globals", [hex(g) for g in hits[v][:3]])

    hero_set = set(v for v, *_ in found)

    def array_of(v):
        h = rd(v + 0x10, 0x20)
        if not h:
            return None
        begin, end = struct.unpack_from("<2Q", h, 8)
        if not in_heap(begin) or not in_heap(end) or end <= begin:
            return None
        span = end - begin
        if span % 8 or span < 0x10 or span > 0x8000:
            return None
        arr = rd(begin, span)
        if not arr:
            return None
        vals = [struct.unpack_from("<Q", arr, i)[0] for i in range(0, span - 7, 8)]
        if sum(1 for x in vals if x in hero_set) < 2:
            return None
        return begin, end, span, len(vals)

    for v, *_ in found:
        a = array_of(v)
        if a:
            print("ObjectManager offset", hex(hits[v][0] - base), "array", hex(a[0]),
                  hex(a[1]), "count", a[3])
            break

    print("SUMMARY")
    for v, score, to, po, pos, nm in found[:10]:
        print("  off=%s team=%s pos=%s name=%s globals=%s" % (
            hex(hits[v][0] - base), hex(to), hex(po),
            repr(body_name(v, nm[0][0], nm[0][1])) if nm else None,
            [hex(g) for g in hits[v][:3]]))

    c.close()
    return 0


def body_name(p, o, run):
    b = rd(p + o, run * 2 + 2)
    if not b:
        return ""
    return b.decode("utf-16-le", "replace").split("\x00")[0]


try:
    sys.exit(main())
except Exception as exc:
    print("ABORTED:", type(exc).__name__, exc)
    sys.exit(2)
