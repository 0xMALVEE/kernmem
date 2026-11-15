import math, struct, sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "sdk"))
import vgb

GAME = "League of Legends.exe"
LO = 0x1000000000
HI = 0x7FFFFFFFFFFF
NEW_LOCAL = 0x1cbb810
POS_FOUND = 0xc20

DELTA = 0x33f8
POST_RENDER = 0x1C8A418
POST_VIEW = (0x1E4, 0x16C, 0x64, 0x1B4)

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


def proj(f, q):
    w = q[0] * f[3] + q[1] * f[7] + q[2] * f[11] + f[15]
    if abs(w) < 1e-9:
        return None
    return ((q[0] * f[0] + q[1] * f[4] + q[2] * f[8] + f[12]) / w,
            (q[0] * f[1] + q[1] * f[5] + q[2] * f[9] + f[13]) / w, w)


def combined(f, lp):
    if not all(math.isfinite(z) and abs(z) < 1e7 for z in f):
        return None
    if abs(f[3]) > 0.03 or abs(f[7]) > 0.03 or abs(f[15]) > 0.05:
        return None
    if not (0.85 < abs(f[11]) < 1.15):
        return None
    r0 = (f[0], f[1], f[2])
    r1 = (f[4], f[5], f[6])
    n0 = math.hypot(*r0)
    n1 = math.hypot(*r1)
    if not (0.05 < n0 < 50.0) or not (0.05 < n1 < 50.0):
        return None
    if abs(sum(r0[i] * r1[i] for i in range(3))) / (n0 * n1) > 0.2:
        return None
    a = proj(f, lp)
    if not a or a[2] <= 5.0:
        return None
    if abs(a[0]) > 1.6 or abs(a[1]) > 1.6:
        return None
    off = proj(f, (lp[0] + 500.0, lp[1], lp[2]))
    if not off or off[2] <= 1.0:
        return None
    sp = math.hypot(off[0] - a[0], off[1] - a[1])
    if not (0.004 < sp < 4.0):
        return None
    return a, sp


def scan_blob(where, blob, lp, out):
    for k in range(0, len(blob) - 63, 4):
        f = struct.unpack_from("<16f", blob, k)
        r = combined(f, lp)
        if r:
            out.append((r[1], where, k, f, r))


def main():
    global pid, c
    c = vgb.Client()
    c.open()
    p("protocol", c.version())
    pid = c.pid_by_name(GAME)
    base = c.main_base(pid)
    p("base", hex(base), "delta", hex(DELTA))

    local = q64(base + NEW_LOCAL)
    lp = struct.unpack("<3f", rd(local + POS_FOUND, 12))
    p("local", hex(local), [round(v, 1) for v in lp])

    seeds = (("obj-relative", POST_RENDER + DELTA),
             ("local-relative", POST_RENDER + (0x1CFA7B0 - NEW_LOCAL)))

    hits = []
    for label, seed in seeds:
        p("== %s seed %s" % (label, hex(seed)))
        for step in range(-0x3000, 0x3001, 8):
            s = seed + step
            val = q64(base + s)
            if val is None or not heap(val):
                continue
            blob = rd(val, 0x400)
            if blob:
                scan_blob(s, blob, lp, hits)
        p("  hits so far", len(hits))

    p("== direct inner offsets at seed slots ==")
    for label, seed in seeds:
        for step in range(-0x80, 0x81, 8):
            s = seed + step
            val = q64(base + s)
            if val is None or not heap(val):
                continue
            blob = rd(val, 0x280)
            if not blob:
                continue
            for inner in POST_VIEW:
                if inner + 64 > len(blob):
                    continue
                f = struct.unpack_from("<16f", blob, inner)
                if combined(f, lp):
                    p("  INNER %s slot=%s inner=%s" % (label, hex(s), hex(inner)))
                    p("   m", [round(z, 6) for z in f])

    hits.sort(key=lambda x: -x[0])
    p("== ranked", len(hits))
    for sp, where, k, f, r in hits[:8]:
        a = r[0]
        p("  slot=%s inner=%s sp=%.5f ndc=%.3f,%.3f w=%.1f" % (
            hex(where), hex(k), sp, a[0], a[1], a[2]))
        p("   m", [round(z, 6) for z in f])

    c.close()
    return 0


try:
    sys.exit(main())
except Exception as exc:
    p("ABORTED:", type(exc).__name__, exc)
    sys.exit(2)
