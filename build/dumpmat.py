import math, struct, sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "sdk"))
import vgb

GAME = "League of Legends.exe"
NEW_LOCAL = 0x1cbb810
POS = 0xc20
OLD_LOCAL = 0x1F304B0
OLD_MATRIX = 0x1FBA090

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


def main():
    global pid, c
    c = vgb.Client()
    c.open()
    c.require_version()
    pid = c.pid_by_name(GAME)
    base = c.main_base(pid)
    p("base", hex(base))

    lp = struct.unpack("<3f", rd(struct.unpack("<Q", rd(base + NEW_LOCAL, 8))[0] + POS, 12))
    p("local pos", [round(v, 1) for v in lp])

    delta = OLD_MATRIX - OLD_LOCAL
    slot = NEW_LOCAL + delta
    p("predicted slot", hex(slot))

    for label, lo in (("predicted", slot - 0x200), ("behind", slot - 0x2200)):
        p("== %s %s" % (label, hex(lo)))
        blob = rd(base + lo, 0x600)
        if not blob:
            p("  unreadable")
            continue
        for k in range(0, len(blob) - 63, 16):
            f = struct.unpack_from("<16f", blob, k)
            if not all(math.isfinite(z) and abs(z) < 1e7 for z in f):
                continue
            t = f[12:15]
            if abs(t[0]) + abs(t[1]) + abs(t[2]) < 10.0:
                continue
            fw = (f[3], f[7], f[11])
            nf = math.hypot(*fw)
            r0 = (f[0], f[1], f[2])
            r1 = (f[4], f[5], f[6])
            n0 = math.hypot(*r0)
            n1 = math.hypot(*r1)
            eye = None
            if n0 > 1e-6 and n1 > 1e-6 and nf > 1e-6:
                eye = tuple(-(t[0] * r0[i] / n0 + t[1] * r1[i] / n1 + t[2] * fw[i] / nf) for i in range(3))
            p("  @%s m15=%.4f t=%s n0=%.2f n1=%.2f nf=%.2f eye=%s" % (
                hex(lo + k), f[15], [round(v, 1) for v in t], n0, n1, nf,
                [round(v, 1) for v in eye] if eye else "-"))
            p("    m", [round(z, 5) for z in f])

    c.close()
    return 0


try:
    sys.exit(main())
except Exception as exc:
    p("ABORTED:", type(exc).__name__, exc)
    sys.exit(2)
