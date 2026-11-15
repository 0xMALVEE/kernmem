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
    if not all(math.isfinite(z) and abs(z) < 1e7 for z in f):
        return None
    nz = sum(1 for z in f if abs(z) > 1e-9)
    if nz < 10:
        return None
    a = project(f, lp)
    if not a:
        return None
    w = a[2]
    if not (1.0 < w < 5e6):
        return None
    if abs(a[0]) > 1.0 or abs(a[1]) > 1.0:
        return None
    ox = project(f, (lp[0] + 400.0, lp[1], lp[2]))
    oz = project(f, (lp[0], lp[1], lp[2] + 400.0))
    if not ox or not oz or ox[2] <= 1.0 or oz[2] <= 1.0:
        return None
    dx = (ox[0] - a[0], ox[1] - a[1])
    dz = (oz[0] - a[0], oz[1] - a[1])
    sx = math.hypot(*dx)
    sz = math.hypot(*dz)
    if not (1e-4 < sx < 20.0) or not (1e-4 < sz < 20.0):
        return None
    if sx / sz > 12.0 or sz / sx > 12.0:
        return None
    cos = (dx[0] * dz[0] + dx[1] * dz[1]) / (sx * sz)
    if abs(cos) > 0.7:
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
    seen = set()
    ptrs = 0
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
                win = rd(v, 0x200)
                if not win:
                    continue
                fin = 0
                for k in range(0, 0x200, 4):
                    z = struct.unpack_from("<f", win, k)[0]
                    if math.isfinite(z) and abs(z) > 1e-9:
                        fin += 1
                if fin < 12:
                    continue
                for k in range(0, 0x200 - 63, 4):
                    f = struct.unpack_from("<16f", win, k)
                    r = score(f, lp)
                    if r:
                        found.append((r[0], off + i, v, k, f, r))
        off += 0x100000
    print("pointers", ptrs)

    found.sort(key=lambda t: t[0])
    print("candidates", len(found))
    for centre, slot, inst, k, f, r in found[:10]:
        _, a, sx, sz, cos, w = r
        print("  slot=%s inst=%s inner=%s centre=%.4f w=%.1f sx=%.4f sz=%.4f cos=%.2f" % (
            hex(slot), hex(inst), hex(k), centre, w, sx, sz, cos))
        print("   m", [round(z, 6) for z in f])

    c.close()
    return 0


try:
    sys.exit(main())
except Exception as exc:
    print("ABORTED:", type(exc).__name__, exc)
    sys.exit(2)
