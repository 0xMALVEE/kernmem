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


def rd(a, n):
    try:
        d = c.read(pid, a, n)
        return d if len(d) == n else None
    except Exception:
        return None


def heap(v):
    return LO <= v <= HI and (v & 7) == 0


def nm(p):
    b = rd(p + NAME, 0x40)
    if not b:
        return ""
    return b.decode("utf-16-le", "replace").split("\x00")[0]


def project(f, p):
    w = p[0] * f[3] + p[1] * f[7] + p[2] * f[11] + f[15]
    if abs(w) < 1e-9:
        return None
    return ((p[0] * f[0] + p[1] * f[4] + p[2] * f[8] + f[12]) / w,
            (p[0] * f[1] + p[1] * f[5] + p[2] * f[9] + f[13]) / w, w)


def score(f, lp):
    if not all(math.isfinite(z) and abs(z) < 1e6 for z in f):
        return None
    if abs(f[15]) > 20.0:
        return None
    r0 = (f[0], f[1], f[2])
    r1 = (f[4], f[5], f[6])
    r2 = (f[8], f[9], f[10])
    n0 = math.hypot(*r0)
    n1 = math.hypot(*r1)
    n2 = math.hypot(*r2)
    if not (0.5 < n0 < 2.0) or not (0.5 < n1 < 2.0) or not (0.5 < n2 < 2.0):
        return None
    for u, nu, v, nv in ((r0, n0, r1, n1), (r0, n0, r2, n2), (r1, n1, r2, n2)):
        if abs(sum(u[i] * v[i] for i in range(3)) / (nu * nv)) > 0.2:
            return None
    t = (f[12], f[13], f[14])
    eye = tuple(-(t[0] * r0[i] + t[1] * r1[i] + t[2] * r2[i]) for i in range(3))
    if not all(math.isfinite(v) and abs(v) < 25000.0 for v in eye):
        return None
    d = math.dist(eye, lp)
    if d > 4000.0:
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
    base = c.main_base(pid)
    e = struct.unpack("<I", rd(base + 0x3C, 4))[0]
    size = struct.unpack("<I", rd(base + e + 0x50, 4))[0]
    print("base", hex(base), "size", hex(size))

    local = struct.unpack("<Q", rd(base + NEW_LOCAL, 8))[0]
    lp = struct.unpack("<3f", rd(local + POS, 12))
    print("local", hex(local), repr(nm(local)), [round(v, 1) for v in lp])

    found = []
    seen = set()

    def consider(kind, where, k, f):
        r = score(f, lp)
        if r:
            found.append((r[0], kind, where, k, f, r))

    def scan_blob(kind, where, blob, limit=0x400):
        lim = min(len(blob), limit)
        fin = sum(1 for i in range(0, lim - 3, 4)
                  if math.isfinite(struct.unpack_from("<f", blob, i)[0])
                  and abs(struct.unpack_from("<f", blob, i)[0]) > 1e-9)
        if fin < 12:
            return
        for k in range(0, lim - 63, 4):
            consider(kind, where, k, struct.unpack_from("<16f", blob, k))

    print("== module inline ==")
    off = 0
    while off < size:
        n = min(0x100000, size - off)
        data = rd(base + off, n)
        if data:
            for i in range(0, n - 63, 4):
                f = struct.unpack_from("<16f", data, i)
                r = score(f, lp)
                if r:
                    found.append((r[0], "INLINE", off + i, 0, f, r))
            for i in range(0, n - 7, 8):
                v = struct.unpack_from("<Q", data, i)[0]
                if heap(v) and v not in seen:
                    seen.add(v)
                    blob = rd(v, 0x400)
                    if blob:
                        scan_blob("PTR", off + i, blob)
        off += 0x100000

    found.sort(key=lambda t: t[0])
    print("candidates", len(found))
    for d, kind, where, k, f, r in found[:10]:
        _, eye, a, w = r
        print("  %s where=%s inner=%s eyeDist=%.1f eye=%s ndc=%.3f,%.3f w=%.1f" % (
            kind, hex(where), hex(k), d, [round(v, 1) for v in eye], a[0], a[1], w))
        print("   m", [round(z, 6) for z in f])

    c.close()
    return 0


try:
    sys.exit(main())
except Exception as exc:
    print("ABORTED:", type(exc).__name__, exc)
    sys.exit(2)
