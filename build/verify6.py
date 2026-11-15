"""Find the real object array and the view matrix."""

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

pid = 0
c = None


def rd(a, n):
    try:
        d = c.read(pid, a, n)
        return d if len(d) == n else None
    except Exception:
        return None


def rd_chunk(a, n, step=0x1000, cap=0x24000):
    n = min(n, cap)
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


def heap(v):
    return LO <= v <= HI and (v & 7) == 0


def nm(p):
    b = rd(p + NAME, 0x40)
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
    lp = struct.unpack("<3f", rd(local + POS, 12))
    print("local", hex(local), "name", repr(nm(local)), "pos", [round(v, 1) for v in lp])

    print("== module pointer scan ==")
    globals_ = {}
    off = 0
    chunk = 0x100000
    while off < size:
        n = min(chunk, size - off)
        data = rd(base + off, n)
        if data:
            for i in range(0, n - 7, 8):
                v = struct.unpack_from("<Q", data, i)[0]
                if heap(v):
                    globals_.setdefault(v, []).append(base + off + i)
        off += chunk
    print("  globals", len(globals_))

    pairs = ((0x18, 0x20), (0x0, 0x8), (0x8, 0x10), (0x10, 0x18), (0x20, 0x28))
    cands = []
    for v in globals_:
        h = rd(v, 0x30)
        if not h:
            continue
        for bo, eo in pairs:
            begin, end = struct.unpack_from("<2Q", h, bo)
            if not heap(begin) or not heap(end) or end <= begin:
                continue
            span = end - begin
            if span % 8 or span < 0x10 or span > 0x40000:
                continue
            cands.append((v, bo, begin, span))
    print("  vector candidates", len(cands))

    hit = None
    for idx, (v, bo, begin, span) in enumerate(cands):
        arr = rd_chunk(begin, span)
        if len(arr) < 8:
            continue
        vals = [struct.unpack_from("<Q", arr, i)[0] for i in range(0, len(arr) - 7, 8)]
        if local not in vals:
            continue
        heroes = []
        for x in vals:
            if not heap(x):
                continue
            t = rd(x + TEAM, 4)
            if t and struct.unpack("<i", t)[0] in (100, 200):
                heroes.append(x)
        print("  HIT off", hex(globals_[v][0] - base), "vec@", hex(bo),
              "count", len(vals), "heroes", len(heroes))
        if hit is None or len(heroes) > hit[0]:
            hit = (len(heroes), globals_[v][0] - base, bo, begin, len(vals))
            for x in heroes[:15]:
                p = rd(x + POS, 12)
                h = rd(x + HP, 8)
                print("     hero", hex(x), "team", struct.unpack("<i", rd(x + TEAM, 4))[0],
                      "name", repr(nm(x)),
                      "pos", [round(z, 1) for z in struct.unpack("<3f", p)] if p else "?",
                      "hp", [round(z, 1) for z in struct.unpack("<2f", h)] if h else "?")
        if idx > 6000:
            break

    obj = hit[1] if hit else None
    print("OBJECT", hex(obj) if obj else None)

    print("== matrix scan ==")
    found = 0
    off = 0
    chunk = 0x100000
    while off < size and found < 8:
        n = min(chunk, size - off)
        data = rd(base + off, n)
        if data:
            for i in range(0, n - 63, 4):
                f = struct.unpack_from("<16f", data, i)
                if not all(math.isfinite(x) and abs(x) < 1e7 for x in f):
                    continue
                if max(f) - min(f) < 0.5:
                    continue
                if abs(f[15]) > 0.01 and abs(f[15] - 1.0) > 0.01:
                    continue
                w = lp[0] * f[3] + lp[1] * f[7] + lp[2] * f[11] + f[15]
                if w <= 1.0:
                    continue
                cx = lp[0] * f[0] + lp[1] * f[4] + lp[2] * f[8] + f[12]
                cy = lp[0] * f[1] + lp[1] * f[5] + lp[2] * f[9] + f[13]
                if abs(cx / w) > 1.5 or abs(cy / w) > 1.5:
                    continue
                print("  MATRIX", hex(off + i), "w", round(w, 1),
                      "ndc", round(cx / w, 3), round(cy / w, 3))
                print("   m", [round(x, 5) for x in f])
                found += 1
                if found >= 8:
                    break
        off += chunk
    if not found:
        print("  none")

    print("SUMMARY local=%#x obj=%s" % (NEW_LOCAL, hex(obj) if obj else None))
    c.close()
    return 0


try:
    sys.exit(main())
except Exception as exc:
    print("ABORTED:", type(exc).__name__, exc)
    sys.exit(2)
