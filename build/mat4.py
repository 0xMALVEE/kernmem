import math, struct, sys, time
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "sdk"))
import vgb

GAME = "League of Legends.exe"
LO = 0x1000000000
HI = 0x7FFFFFFFFFFF
NEW_LOCAL = 0x1cbb810
POS = 0xc20
NAME = 0x3f00
MAP = 20000.0

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


def norm(v):
    n = math.sqrt(sum(x * x for x in v))
    if n < 1e-9:
        return None
    return tuple(x / n for x in v)


def camera_rows(f):
    # rows carry projection scale, so normalise each row on its own
    u0 = norm((f[0], f[4], f[8]))
    u1 = norm((f[1], f[5], f[9]))
    u2 = norm((f[3], f[7], f[11]))
    if not (u0 and u1 and u2):
        return None
    if abs(sum(u0[i] * u1[i] for i in range(3))) > 0.15:
        return None
    if abs(sum(u0[i] * u2[i] for i in range(3))) > 0.25:
        return None
    if abs(sum(u1[i] * u2[i] for i in range(3))) > 0.25:
        return None
    t = (f[12], f[13], f[14])
    cam = tuple(-(u0[i] * t[0] + u1[i] * t[1] + u2[i] * t[2]) for i in range(3))
    return cam, u0, u1, u2


def camera_cols(f):
    u0 = norm((f[0], f[1], f[2]))
    u1 = norm((f[4], f[5], f[6]))
    u2 = norm((f[8], f[9], f[10]))
    if not (u0 and u1 and u2):
        return None
    if abs(sum(u0[i] * u1[i] for i in range(3))) > 0.15:
        return None
    if abs(sum(u0[i] * u2[i] for i in range(3))) > 0.25:
        return None
    if abs(sum(u1[i] * u2[i] for i in range(3))) > 0.25:
        return None
    t = (f[12], f[13], f[14])
    cam = tuple(-(u0[i] * t[0] + u1[i] * t[1] + u2[i] * t[2]) for i in range(3))
    return cam, u0, u1, u2


def project(f, p):
    w = p[0] * f[3] + p[1] * f[7] + p[2] * f[11] + f[15]
    if abs(w) < 1e-6:
        return None
    return ((p[0] * f[0] + p[1] * f[4] + p[2] * f[8] + f[12]) / w,
            (p[0] * f[1] + p[1] * f[5] + p[2] * f[9] + f[13]) / w, w)


def test(f, lp):
    if not all(math.isfinite(z) and abs(z) < 1e9 for z in f):
        return None
    if abs(f[15]) > 5000.0:
        return None
    for kind, fn in (("rows", camera_rows), ("cols", camera_cols)):
        r = fn(f)
        if not r:
            continue
        cam = r[0]
        if not all(math.isfinite(v) and abs(v) < MAP for v in cam):
            continue
        dist = math.dist(cam, lp)
        if dist > 9000.0:
            continue
        a = project(f, lp)
        if not a or a[2] <= 1.0:
            continue
        if abs(a[0]) > 1.2 or abs(a[1]) > 1.2:
            continue
        off = project(f, (lp[0] + 400.0, lp[1], lp[2]))
        sp = 0.0
        if off and off[2] > 1.0:
            sp = max(sp, abs(off[0] - a[0]), abs(off[1] - a[1]))
        if sp < 1e-3:
            continue
        return kind, cam, dist, a, sp
    return None


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
        orient, cam, dist, a, sp = r
        print("  %s slot=%s inst=%s orient=%s cam=%s dist=%.1f ndc=%.3f,%.3f sp=%.3f" % (
            kind, hex(slot), hex(inst) if inst else "-", orient,
            [round(v, 1) for v in cam], dist, a[0], a[1], sp))
        print("   m", [round(z, 6) for z in f])
        return True

    print("== pointer slots ==")
    hits = 0
    off = 0
    while off < size and hits < 5:
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
                    if hits >= 5:
                        break
        off += 0x100000
    print("pointer hits", hits)

    print("== inline blocks ==")
    hits = 0
    off = 0
    while off < size and hits < 5:
        n = min(0x100000, size - off)
        data = rd(base + off, n)
        if data:
            for i in range(0, n - 63, 4):
                if report("INLINE", off + i, struct.unpack_from("<16f", data, i), 0):
                    hits += 1
                    if hits >= 5:
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
