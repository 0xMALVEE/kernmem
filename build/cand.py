import math, struct, sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "sdk"))
import vgb

GAME = "League of Legends.exe"
NEW_LOCAL = 0x1cbb810
POS = 0xc20
HEAP_LO = 0x1000000000
HEAP_HI = 0x7FFFFFFFFFFF

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
    return HEAP_LO <= v <= HEAP_HI and (v & 7) == 0


def proj(f, q):
    w = q[0] * f[3] + q[1] * f[7] + q[2] * f[11] + f[15]
    if abs(w) < 1e-9:
        return None
    return ((q[0] * f[0] + q[1] * f[4] + q[2] * f[8] + f[12]) / w,
            (q[0] * f[1] + q[1] * f[5] + q[2] * f[9] + f[13]) / w, w)


def judge(f, lp):
    if not all(math.isfinite(z) and abs(z) < 1e6 for z in f):
        return None
    nz = sum(1 for z in f if abs(z) > 1e-9)
    if nz < 12:
        return None
    a = proj(f, lp)
    if not a:
        return None
    if a[2] <= 1.0 or abs(a[0]) > 3.0 or abs(a[1]) > 3.0:
        return None
    ox = proj(f, (lp[0] + 500.0, lp[1], lp[2]))
    oz = proj(f, (lp[0], lp[1], lp[2] + 500.0))
    if not ox or not oz or ox[2] <= 1.0 or oz[2] <= 1.0:
        return None
    sx = math.hypot(ox[0] - a[0], ox[1] - a[1])
    sz = math.hypot(oz[0] - a[0], oz[1] - a[1])
    if sx < 1e-6 or sz < 1e-6:
        return None
    return nz, a, sx, sz


def main():
    global pid, c
    c = vgb.Client()
    c.open()
    p("protocol", c.version())
    pid = c.pid_by_name(GAME)
    base = c.main_base(pid)
    p("base", hex(base))

    local = struct.unpack("<Q", rd(base + NEW_LOCAL, 8))[0]
    lp = struct.unpack("<3f", rd(local + POS, 12))
    p("local", hex(local), [round(v, 1) for v in lp])

    # deltas taken from the two UC tables, applied to the recovered LocalPlayer
    dumper = 0x1EA3660 - 0x1E257E0
    aio = 0x21ec740 - 0x21a56f8
    p("deltas: dumper=%#x aio=%#x" % (dumper, aio))

    seeds = [(NEW_LOCAL + dumper, "loldumper"), (NEW_LOCAL + aio, "aio")]
    for seed, label in seeds:
        p("== seed %s %s" % (label, hex(seed)))
        for step in range(-8, 9):
            slot = seed + step * 8
            if slot <= 0:
                continue
            raw = rd(base + slot, 8)
            if not raw:
                continue
            v = struct.unpack("<Q", raw)[0]
            if not heap(v):
                p("   %s inline? %s" % (hex(slot), hex(v)))
                continue
            blob = rd(v, 64)
            if not blob:
                continue
            f = struct.unpack("<16f", blob)
            r = judge(f, lp)
            p("   %s ptr=%s %s" % (hex(slot), hex(v), "GOOD" if r else "no"))
            if r:
                nz, a, sx, sz = r
                p("      nz=%d ndc=%.3f,%.3f w=%.1f sx=%.4f sz=%.4f" % (nz, a[0], a[1], a[2], sx, sz))
                p("      m", [round(z, 6) for z in f])

    c.close()
    return 0


try:
    sys.exit(main())
except Exception as exc:
    p("ABORTED:", type(exc).__name__, exc)
    sys.exit(2)
