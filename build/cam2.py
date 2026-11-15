import math, struct, sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "sdk"))
import vgb

GAME = "League of Legends.exe"
LO = 0x1000000000
HI = 0x7FFFFFFFFFFF
NEW_LOCAL = 0x1cbb810
POS = 0xc20
SLOT = 0x1cbba50
INNER = (0x1A4, 0x1E4, 0x204, 0x268, 0x284)

pid = 0
c = None


def p(*a):
    print(*a)
    sys.stdout.flush()


def rd(a, n):
    try:
        d = c.read(pid, a, n)
        return d if len(d) == n else None
    except Exception:
        return None


def heap(v):
    return LO <= v <= HI and (v & 7) == 0


def q64(a):
    b = rd(a, 8)
    return struct.unpack("<Q", b)[0] if b else None


def det3(r0, r1, r2):
    return (r0[0] * (r1[1] * r2[2] - r1[2] * r2[1])
            - r0[1] * (r1[0] * r2[2] - r1[2] * r2[0])
            + r0[2] * (r1[0] * r2[1] - r1[1] * r2[0]))


def basis(f):
    if not all(math.isfinite(z) and abs(z) < 1e5 for z in f):
        return None
    rows = [(f[0], f[1], f[2]), (f[4], f[5], f[6]), (f[8], f[9], f[10])]
    n = [math.hypot(*r) for r in rows]
    if any(x < 0.9 or x > 1.1 for x in n):
        return None
    for i in range(3):
        for j in range(i + 1, 3):
            if abs(sum(rows[i][k] * rows[j][k] for k in range(3))) > 0.03:
                return None
    if abs(det3(*rows) - 1.0) > 0.03:
        return None
    return rows


def eyes(f, rows):
    t = (f[12], f[13], f[14])
    a = tuple(-sum(t[k] * rows[i][k] for k in range(3)) for i in range(3))
    b = t
    return a, b


def main():
    global pid, c
    c = vgb.Client()
    c.open()
    p("protocol", c.version())
    pid = c.pid_by_name(GAME)
    base = c.main_base(pid)
    p("base", hex(base))

    local = q64(base + NEW_LOCAL)
    lp = struct.unpack("<3f", rd(local + POS, 12))
    p("hero", [round(v, 1) for v in lp])

    found = []
    for step in range(-0x6000, 0x6001, 8):
        slot = SLOT + step
        v = q64(base + slot)
        if not v or not heap(v):
            continue
        blob = rd(v, 0x300)
        if not blob:
            continue
        for k in range(0, len(blob) - 63, 4):
            f = struct.unpack_from("<16f", blob, k)
            rows = basis(f)
            if not rows:
                continue
            inv, raw = eyes(f, rows)
            for kind, e in (("inv", inv), ("raw", raw)):
                if not all(math.isfinite(z) and abs(z) < 25000.0 for z in e):
                    continue
                d = math.dist(e, lp)
                found.append((d, kind, slot, v, k, e, f))

    found.sort(key=lambda x: x[0])
    p("hits", len(found))
    for d, kind, slot, v, k, e, f in found[:10]:
        p("  dist=%.1f %s slot=%s ptr=%s inner=%s eye=%s" % (
            d, kind, hex(slot), hex(v), hex(k), [round(z, 1) for z in e]))
        p("   m", [round(z, 5) for z in f])

    p("== inner offsets of the nearby object ==")
    for step in (0, -0x8, 8, -0x10, 0x10):
        slot = SLOT + step
        v = q64(base + slot)
        if not v or not heap(v):
            continue
        blob = rd(v, 0x300)
        if not blob:
            continue
        for inner in INNER:
            if inner + 64 > len(blob):
                continue
            f = struct.unpack_from("<16f", blob, inner)
            p("  slot=%s inner=%s %s" % (hex(slot), hex(inner), [round(z, 4) for z in f]))

    c.close()
    return 0


try:
    sys.exit(main())
except Exception as exc:
    p("ABORTED:", type(exc).__name__, exc)
    sys.exit(2)
