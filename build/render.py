import math, struct, sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "sdk"))
import vgb

GAME = "League of Legends.exe"
LO = 0x1000000000
HI = 0x7FFFFFFFFFFF
NEW_LOCAL = 0x1cbb810
POS = 0xc20

# inner offsets reported by the reversal thread for view / proj / camera
INNER = (0x64, 0x13C, 0x16C, 0x1B4, 0x1E4, 0x224)

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


def proj(f, q):
    w = q[0] * f[3] + q[1] * f[7] + q[2] * f[11] + f[15]
    if abs(w) < 1e-9:
        return None
    return ((q[0] * f[0] + q[1] * f[4] + q[2] * f[8] + f[12]) / w,
            (q[0] * f[1] + q[1] * f[5] + q[2] * f[9] + f[13]) / w, w)


def judge(f, lp):
    if not all(math.isfinite(z) and abs(z) < 1e6 for z in f):
        return None
    if sum(1 for z in f if abs(z) > 1e-9) < 12:
        return None
    a = proj(f, lp)
    if not a or a[2] <= 1.0:
        return None
    if abs(a[0]) > 4.0 or abs(a[1]) > 4.0:
        return None
    ox = proj(f, (lp[0] + 500.0, lp[1], lp[2]))
    oz = proj(f, (lp[0], lp[1], lp[2] + 500.0))
    sx = math.hypot(ox[0] - a[0], ox[1] - a[1]) if ox else 0.0
    sz = math.hypot(oz[0] - a[0], oz[1] - a[1]) if oz else 0.0
    if max(sx, sz) < 1e-6:
        return None
    return a, sx, sz


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

    pairs = ((0x1C8A418, 0x1CC4FD8), (0x1C8A418, 0x1CC6FD8))
    seeds = []
    for renderer, lplayer in pairs:
        delta = lplayer - renderer
        seeds.append(NEW_LOCAL - delta)
        p("pair renderer=%#x local=%#x delta=%#x -> seed %#x" % (
            renderer, lplayer, delta, NEW_LOCAL - delta))

    hits = 0
    seen = set()

    for seed in seeds:
        for step in range(-0x20000, 0x20001, 8):
            slot = seed + step
            if slot <= 0 or slot in seen:
                continue
            seen.add(slot)
            raw = rd(base + slot, 8)
            if not raw:
                continue
            v = struct.unpack("<Q", raw)[0]
            if not heap(v):
                continue
            blob = rd(v, 0x300)
            if not blob:
                continue
            for inner in INNER:
                if inner + 64 > len(blob):
                    continue
                f = struct.unpack_from("<16f", blob, inner)
                r = judge(f, lp)
                if not r:
                    continue
                a, sx, sz = r
                p("HIT slot=%s ptr=%s inner=%s ndc=%.3f,%.3f w=%.1f sx=%.5f sz=%.5f" % (
                    hex(slot), hex(v), hex(inner), a[0], a[1], a[2], sx, sz))
                p("   m", [round(z, 6) for z in f])
                hits += 1
    p("hits", hits)

    c.close()
    return 0


try:
    sys.exit(main())
except Exception as exc:
    p("ABORTED:", type(exc).__name__, exc)
    sys.exit(2)
