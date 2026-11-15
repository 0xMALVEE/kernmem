import math, struct, sys, time
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "sdk"))
import vgb

GAME = "League of Legends.exe"
NEW_LOCAL = 0x1cbb810
POS = 0xc20
NAME = 0x3f00
SLOT = 0x1cbba50

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


def project(m, q):
    w = q[0] * m[3] + q[1] * m[7] + q[2] * m[11] + m[15]
    if abs(w) < 1e-9:
        return None
    return ((q[0] * m[0] + q[1] * m[4] + q[2] * m[8] + m[12]) / w,
            (q[0] * m[1] + q[1] * m[5] + q[2] * m[9] + m[13]) / w, w)


def mul(a, b):
    return [sum(a[i * 4 + k] * b[k * 4 + j] for k in range(4))
            for i in range(4) for j in range(4)]


def main():
    global pid, c
    c = vgb.Client()
    c.open()
    p("protocol", c.version())
    pid = c.pid_by_name(GAME)
    base = c.main_base(pid)
    p("base", hex(base))

    local = q64(base + NEW_LOCAL)
    name = rd(local + NAME, 0x40).decode("utf-16-le", "replace").split("\x00")[0]
    lp = struct.unpack("<3f", rd(local + POS, 12))
    p("local", hex(local), repr(name), [round(v, 1) for v in lp])

    obj = q64(base + SLOT)
    p("slot", hex(SLOT), "->", hex(obj) if obj else "?")
    if not obj:
        c.close()
        return 1

    blob = rd(obj, 0x600)
    p("object readable", bool(blob), len(blob) if blob else 0)
    if not blob:
        c.close()
        return 1

    view = struct.unpack_from("<16f", blob, 0x204)
    p("view", [round(z, 5) for z in view])

    a = project(view, lp)
    p("hero through view alone: ndc=%s w=%s" % (
        [round(z, 3) for z in a[:2]] if a else None, round(a[2], 1) if a else None))

    p("== projection hunt in same object ==")
    best = None
    for k in range(0, len(blob) - 63, 4):
        if k == 0x204:
            continue
        pm = struct.unpack_from("<16f", blob, k)
        if not all(math.isfinite(z) and abs(z) < 1e5 for z in pm):
            continue
        if abs(pm[3]) > 0.2 or abs(pm[7]) > 0.2:
            continue
        if abs(abs(pm[11]) - 1.0) > 0.25:
            continue
        if abs(pm[15]) > 0.3:
            continue
        if pm[0] < 0.05 or pm[5] < 0.05:
            continue
        comb = mul(view, pm)
        r = project(comb, lp)
        if not r or r[2] <= 1.0:
            continue
        score = abs(r[0]) + abs(r[1])
        p("  proj@%s ndc=%.3f,%.3f w=%.1f" % (hex(k), r[0], r[1], r[2]))
        p("   pm", [round(z, 5) for z in pm])
        if best is None or score < best[0]:
            best = (score, k, pm, comb, r)

    if best:
        p("BEST proj@%s score=%.4f" % (hex(best[1]), best[0]))
        p("  comb", [round(z, 6) for z in best[3]])

    p("== stability over 2s ==")
    time.sleep(2.0)
    blob2 = rd(obj, 0x600)
    if blob2:
        v2 = struct.unpack_from("<16f", blob2, 0x204)
        same = sum(1 for x, y in zip(view, v2) if abs(x - y) < 1e-4)
        p("  view fields unchanged", same, "of 16")
        p("  view now", [round(z, 5) for z in v2])

    c.close()
    return 0


try:
    sys.exit(main())
except Exception as exc:
    p("ABORTED:", type(exc).__name__, exc)
    sys.exit(2)
