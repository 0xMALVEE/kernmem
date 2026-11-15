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
    if not all(math.isfinite(z) and abs(z) < 1e9 for z in f):
        return None
    a = project(f, lp)
    if not a:
        return None
    w = a[2]
    if not (20.0 < w < 60000.0):
        return None
    if abs(a[0]) > 0.5 or abs(a[1]) > 0.5:
        return None
    ox = project(f, (lp[0] + 500.0, lp[1], lp[2]))
    oz = project(f, (lp[0], lp[1], lp[2] + 500.0))
    if not ox or not oz or ox[2] <= 1.0 or oz[2] <= 1.0:
        return None
    dx = (ox[0] - a[0], ox[1] - a[1])
    dz = (oz[0] - a[0], oz[1] - a[1])
    sx = math.hypot(*dx)
    sz = math.hypot(*dz)
    if not (0.005 < sx < 5.0) or not (0.005 < sz < 5.0):
        return None
    if sx / sz > 8.0 or sz / sx > 8.0:
        return None
    cos = (dx[0] * dz[0] + dx[1] * dz[1]) / (sx * sz)
    if abs(cos) > 0.6:
        return None
    return abs(a[0]) + abs(a[1]), a, sx, sz, cos, w


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

    def consider(kind, slot, f, inst):
        r = score(f, lp)
        if r:
            found.append((r[0], kind, slot, inst, f, r))

    off = 0
    while off < size:
        n = min(0x100000, size - off)
        data = rd(base + off, n)
        if data:
            for i in range(0, n - 7, 8):
                v = struct.unpack_from("<Q", data, i)[0]
                if not heap(v):
                    continue
                m = rd(v, 64)
                if m:
                    consider("PTR", off + i, struct.unpack("<16f", m), v)
            for i in range(0, n - 63, 4):
                consider("INLINE", off + i, struct.unpack_from("<16f", data, i), 0)
        off += 0x100000

    found.sort(key=lambda t: t[0])
    print("candidates", len(found))
    for centre, kind, slot, inst, f, r in found[:10]:
        _, a, sx, sz, cos, w = r
        print("  %s slot=%s inst=%s centre=%.4f w=%.1f sx=%.3f sz=%.3f cos=%.2f" % (
            kind, hex(slot), hex(inst) if inst else "-", centre, w, sx, sz, cos))
        print("   m", [round(z, 6) for z in f])

    c.close()
    return 0


try:
    sys.exit(main())
except Exception as exc:
    print("ABORTED:", type(exc).__name__, exc)
    sys.exit(2)
