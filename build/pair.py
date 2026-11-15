import math, struct, sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "sdk"))
import vgb

GAME = "League of Legends.exe"
NEW_LOCAL = 0x1cbb810
POS = 0xc20
SLOT = 0x1cbba50
SPAN = 0x300

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


def q64(a):
    b = rd(a, 8)
    return struct.unpack("<Q", b)[0] if b else None


def mul(a, b):
    return [sum(a[i * 4 + k] * b[k * 4 + j] for k in range(4))
            for i in range(4) for j in range(4)]


def project(m, q):
    w = q[0] * m[3] + q[1] * m[7] + q[2] * m[11] + m[15]
    if abs(w) < 1e-9:
        return None
    return ((q[0] * m[0] + q[1] * m[4] + q[2] * m[8] + m[12]) / w,
            (q[0] * m[1] + q[1] * m[5] + q[2] * m[9] + m[13]) / w, w)


def usable(m, lp):
    if not all(math.isfinite(z) and abs(z) < 1e7 for z in m):
        return None
    a = project(m, lp)
    if not a or a[2] <= 1.0:
        return None
    if abs(a[0]) > 6.0 or abs(a[1]) > 6.0:
        return None
    up = project(m, (lp[0], lp[1], lp[2] + 3000.0))
    if not up or up[2] <= 1.0:
        return None
    sp = math.hypot(up[0] - a[0], up[1] - a[1])
    if sp < 1e-5:
        return None
    return abs(a[0]) + abs(a[1]), a, sp


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

    obj = q64(base + SLOT)
    p("slot", hex(SLOT), "->", hex(obj) if obj else "?")
    if not obj:
        c.close()
        return 1
    blob = rd(obj, SPAN)
    if not blob:
        p("unreadable")
        c.close()
        return 1

    p("== float grid (offset: f0 f1 f2 f3)")
    for k in range(0, len(blob) - 15, 16):
        f = struct.unpack_from("<4f", blob, k)
        if all(abs(z) < 1e-12 for z in f):
            continue
        p("  %s: %s" % (hex(k), [round(z, 4) for z in f]))

    p("== pair products")
    blocks = [k for k in range(0, len(blob) - 63, 4)]
    best = []
    for i, a in enumerate(blocks):
        ma = struct.unpack_from("<16f", blob, a)
        if sum(1 for z in ma if abs(z) > 1e-9) < 6:
            continue
        for b in blocks[i + 1:]:
            mb = struct.unpack_from("<16f", blob, b)
            if sum(1 for z in mb if abs(z) > 1e-9) < 6:
                continue
            comb = mul(ma, mb)
            r = usable(comb, lp)
            if r:
                best.append((r[0], a, b, comb, r))

    best.sort(key=lambda x: x[0])
    p("hits", len(best))
    for sc, a, b, comb, r in best[:8]:
        p("  a=%s b=%s score=%.3f ndc=%.3f,%.3f w=%.1f up=%.5f" % (
            hex(a), hex(b), sc, r[1][0], r[1][1], r[1][2], r[2]))
        p("   comb", [round(z, 6) for z in comb])

    c.close()
    return 0


try:
    sys.exit(main())
except Exception as exc:
    p("ABORTED:", type(exc).__name__, exc)
    sys.exit(2)
