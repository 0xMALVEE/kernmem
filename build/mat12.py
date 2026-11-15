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


def project(f, q):
    w = q[0] * f[3] + q[1] * f[7] + q[2] * f[11] + f[15]
    if abs(w) < 1e-9:
        return None
    return ((q[0] * f[0] + q[1] * f[4] + q[2] * f[8] + f[12]) / w,
            (q[0] * f[1] + q[1] * f[5] + q[2] * f[9] + f[13]) / w, w)


def score(f, lp):
    if not all(math.isfinite(z) and abs(z) < 1e6 for z in f):
        return None
    if abs(f[15]) > 50.0:
        return None
    r0 = (f[0], f[1], f[2])
    r1 = (f[4], f[5], f[6])
    r2 = (f[8], f[9], f[10])
    n0 = math.hypot(*r0)
    n1 = math.hypot(*r1)
    n2 = math.hypot(*r2)
    if not (0.3 < n0 < 3.0) or not (0.3 < n1 < 3.0) or not (0.3 < n2 < 3.0):
        return None
    for u, nu, v, nv in ((r0, n0, r1, n1), (r0, n0, r2, n2), (r1, n1, r2, n2)):
        if abs(sum(u[i] * v[i] for i in range(3)) / (nu * nv)) > 0.3:
            return None
    t = (f[12], f[13], f[14])
    eye = tuple(-(t[0] * r0[i] + t[1] * r1[i] + t[2] * r2[i]) for i in range(3))
    if not all(math.isfinite(v) and abs(v) < 25000.0 for v in eye):
        return None
    d = math.dist(eye, lp)
    if d > 6000.0:
        return None
    a = project(f, lp)
    if not a or a[2] <= 1.0:
        return None
    if abs(a[0]) > 1.5 or abs(a[1]) > 1.5:
        return None
    return d, eye, a, a[2]


def main():
    global pid, c
    c = vgb.Client()
    c.open()
    c.require_version()
    pid = c.pid_by_name(GAME)
    p("pid", pid)
    base = c.main_base(pid)
    e = struct.unpack("<I", rd(base + 0x3C, 4))[0]
    size = struct.unpack("<I", rd(base + e + 0x50, 4))[0]
    p("base", hex(base), "size", hex(size))

    local = struct.unpack("<Q", rd(base + NEW_LOCAL, 8))[0]
    lp = struct.unpack("<3f", rd(local + POS, 12))
    p("local", hex(local), repr(nm(local)), [round(v, 1) for v in lp])

    found = []
    seen = set()
    ptrs = 0

    def look(kind, where, k, blob):
        r = score(struct.unpack_from("<16f", blob, k), lp)
        if r:
            found.append((r[0], kind, where, k, struct.unpack_from("<16f", blob, k), r))

    off = 0
    while off < size:
        n = min(0x100000, size - off)
        data = rd(base + off, n)
        if data:
            for i in range(0, n - 7, 8):
                v = struct.unpack_from("<Q", data, i)[0]
                if not heap(v) or v in seen:
                    continue
                seen.add(v)
                ptrs += 1
                blob = rd(v, 0x100)
                if not blob:
                    continue
                for k in range(0, 0x100 - 63, 4):
                    look("PTR", off + i, k, blob)
        off += 0x100000
        if off % 0x800000 == 0:
            p("  scanned", hex(off), "ptrs", ptrs, "hits", len(found))

    p("pointers", ptrs, "candidates", len(found))
    found.sort(key=lambda t: t[0])
    for d, kind, where, k, f, r in found[:10]:
        _, eye, a, w = r
        p("  %s where=%s inner=%s eyeDist=%.1f eye=%s ndc=%.3f,%.3f w=%.1f" % (
            kind, hex(where), hex(k), d, [round(v, 1) for v in eye], a[0], a[1], w))
        p("   m", [round(z, 6) for z in f])

    c.close()
    return 0


try:
    sys.exit(main())
except Exception as exc:
    p("ABORTED:", type(exc).__name__, exc)
    sys.exit(2)
