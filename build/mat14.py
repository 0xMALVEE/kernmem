import math, struct, sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "sdk"))
import vgb

GAME = "League of Legends.exe"
LO = 0x1000000000
HI = 0x7FFFFFFFFFFF
NEW_LOCAL = 0x1cbb810
POS = 0xc20
NAME = 0x3f00

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


def nm(q):
    b = rd(q + NAME, 0x40)
    if not b:
        return ""
    return b.decode("utf-16-le", "replace").split("\x00")[0]


def proj(f, q):
    w = q[0] * f[3] + q[1] * f[7] + q[2] * f[11] + f[15]
    if abs(w) < 1e-9:
        return None
    return ((q[0] * f[0] + q[1] * f[4] + q[2] * f[8] + f[12]) / w,
            (q[0] * f[1] + q[1] * f[5] + q[2] * f[9] + f[13]) / w, w)


def strong(f, lp):
    if not all(math.isfinite(z) and abs(z) < 1e6 for z in f):
        return None
    if abs(f[15]) > 50.0:
        return None
    r0 = (f[0], f[1], f[2])
    r1 = (f[4], f[5], f[6])
    fw = (f[3], f[7], f[11])
    n0 = math.hypot(*r0)
    n1 = math.hypot(*r1)
    nf = math.hypot(*fw)
    if not (0.85 < nf < 1.2):
        return None
    if n0 < 0.2 or n1 < 0.2:
        return None
    t = (f[12], f[13], f[14])
    if abs(t[0]) + abs(t[1]) + abs(t[2]) < 1.0:
        return None
    eye = tuple(-(t[0] * r0[i] + t[1] * r1[i] + t[2] * fw[i]) for i in range(3))
    if not all(math.isfinite(v) and abs(v) < 25000.0 for v in eye):
        return None
    d = math.dist(eye, lp)
    if d > 3500.0:
        return None
    a = proj(f, lp)
    if not a or a[2] <= 5.0:
        return None
    if abs(a[0]) > 1.2 or abs(a[1]) > 1.2:
        return None
    off = proj(f, (lp[0] + 500.0, lp[1], lp[2]))
    if not off or off[2] <= 1.0:
        return None
    sp = math.hypot(off[0] - a[0], off[1] - a[1])
    if not (0.01 < sp < 3.0):
        return None
    return d, eye, a, sp


def tr(f):
    return (f[0], f[4], f[8], f[12], f[1], f[5], f[9], f[13],
            f[2], f[6], f[10], f[14], f[3], f[7], f[11], f[15])


def main():
    global pid, c
    c = vgb.Client()
    c.open()
    c.require_version()
    pid = c.pid_by_name(GAME)
    base = c.main_base(pid)
    e = struct.unpack("<I", rd(base + 0x3C, 4))[0]
    size = struct.unpack("<I", rd(base + e + 0x50, 4))[0]
    p("base", hex(base), "size", hex(size))

    local = struct.unpack("<Q", rd(base + NEW_LOCAL, 8))[0]
    lp = struct.unpack("<3f", rd(local + POS, 12))
    p("local", hex(local), repr(nm(local)), [round(v, 1) for v in lp])

    found = []
    seen = set()

    def check(kind, where, k, f):
        for orient, g in (("row", f), ("col", tr(f))):
            r = strong(g, lp)
            if r:
                found.append((r[0], kind, where, k, orient, g, r))

    def window(kind, where, tgt, span):
        blob = rd(tgt, span)
        if not blob:
            return None
        for k in range(0, len(blob) - 63, 4):
            check(kind, where, k, struct.unpack_from("<16f", blob, k))
        return blob

    off = 0
    while off < size:
        n = min(0x100000, size - off)
        data = rd(base + off, n)
        if data:
            for i in range(0, n - 63, 4):
                check("INLINE", off + i, 0, struct.unpack_from("<16f", data, i))
            for i in range(0, n - 7, 8):
                v = struct.unpack_from("<Q", data, i)[0]
                if not heap(v) or v in seen:
                    continue
                seen.add(v)
                blob = window("PTR", off + i, v, 0x100)
                if not blob:
                    continue
                for j in range(0, 0x80 - 7, 8):
                    w = struct.unpack_from("<Q", blob, j)[0]
                    if heap(w) and w not in seen:
                        seen.add(w)
                        window("PTR2", off + i, w, 0x100)
        off += 0x100000

    found.sort(key=lambda t: t[0])
    p("candidates", len(found))
    for d, kind, where, k, orient, g, r in found[:10]:
        _, eye, a, sp = r
        p("  %s where=%s inner=%s orient=%s eyeDist=%.1f eye=%s ndc=%.3f,%.3f sp=%.3f" % (
            kind, hex(where), hex(k), orient, d, [round(v, 1) for v in eye], a[0], a[1], sp))
        p("   m", [round(z, 6) for z in g])

    c.close()
    return 0


try:
    sys.exit(main())
except Exception as exc:
    p("ABORTED:", type(exc).__name__, exc)
    sys.exit(2)
