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
MAP = 16000.0

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


def camera(f):
    c0 = (f[0], f[1], f[2])
    c1 = (f[4], f[5], f[6])
    c2 = (f[8], f[9], f[10])
    n = [math.sqrt(sum(x * x for x in v)) for v in (c0, c1, c2)]
    if min(n) < 1e-6:
        return None
    mx = max(n)
    if (mx - min(n)) / mx > 0.02:
        return None
    u = [tuple(v[i] / n[k] for i in range(3)) for k, v in enumerate((c0, c1, c2))]
    for i in range(3):
        for j in range(i + 1, 3):
            if abs(sum(u[i][k] * u[j][k] for k in range(3))) > 0.02:
                return None
    det = (u[0][0] * (u[1][1] * u[2][2] - u[1][2] * u[2][1])
           - u[0][1] * (u[1][0] * u[2][2] - u[1][2] * u[2][0])
           + u[0][2] * (u[1][0] * u[2][1] - u[1][1] * u[2][0]))
    if det < 0.98:
        return None
    t = (f[12], f[13], f[14])
    cam = (-(u[0][0] * t[0] + u[0][1] * t[1] + u[0][2] * t[2]),
           -(u[1][0] * t[0] + u[1][1] * t[1] + u[1][2] * t[2]),
           -(u[2][0] * t[0] + u[2][1] * t[1] + u[2][2] * t[2]))
    return cam, det


def project(f, p):
    w = p[0] * f[3] + p[1] * f[7] + p[2] * f[11] + f[15]
    if abs(w) < 1e-6:
        return None
    return ((p[0] * f[0] + p[1] * f[4] + p[2] * f[8] + f[12]) / w,
            (p[0] * f[1] + p[1] * f[5] + p[2] * f[9] + f[13]) / w, w)


def test(f, lp):
    if not all(math.isfinite(z) and abs(z) < 1e9 for z in f):
        return None
    r = camera(f)
    if not r:
        return None
    cam, det = r
    if not all(math.isfinite(v) and abs(v) < MAP for v in cam):
        return None
    dist = math.dist(cam, lp)
    if dist > 8000.0:
        return None
    a = project(f, lp)
    if not a or a[2] <= 1.0 or abs(a[0]) > 1.5 or abs(a[1]) > 1.5:
        return None
    far = project(f, (lp[0], lp[1], lp[2] + 4000.0))
    if far and far[2] <= 0.0:
        return None
    return cam, dist, det, a


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

    def report(kind, slot, f, inst):
        r = test(f, lp)
        if not r:
            return False
        cam, dist, det, a = r
        print("  %s slot=%s inst=%s cam=%s dist=%.1f det=%.3f ndc=%.3f,%.3f" % (
            kind, hex(slot), hex(inst) if inst else "-",
            [round(v, 1) for v in cam], dist, det, a[0], a[1]))
        print("   m", [round(z, 6) for z in f])
        return True

    print("== pointer slots ==")
    hits = 0
    off = 0
    while off < size and hits < 6:
        n = min(0x100000, size - off)
        data = rd(base + off, n)
        if data:
            for i in range(0, n - 7, 8):
                v = struct.unpack_from("<Q", data, i)[0]
                if not heap(v):
                    continue
                m = rd(v, 64)
                if not m:
                    continue
                if report("PTR", off + i, struct.unpack("<16f", m), v):
                    hits += 1
                    if hits >= 6:
                        break
        off += 0x100000
    print("pointer hits", hits)

    print("== inline blocks ==")
    hits = 0
    off = 0
    while off < size and hits < 6:
        n = min(0x100000, size - off)
        data = rd(base + off, n)
        if data:
            for i in range(0, n - 63, 4):
                if report("INLINE", off + i, struct.unpack_from("<16f", data, i), 0):
                    hits += 1
                    if hits >= 6:
                        break
        off += 0x100000
    print("inline hits", hits)

    c.close()
    return 0


try:
    sys.exit(main())
except Exception as exc:
    print("ABORTED:", type(exc).__name__, exc)
    sys.exit(2)
