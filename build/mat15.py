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


def proj(f, q):
    w = q[0] * f[3] + q[1] * f[7] + q[2] * f[11] + f[15]
    if abs(w) < 1e-9:
        return None
    return ((q[0] * f[0] + q[1] * f[4] + q[2] * f[8] + f[12]) / w,
            (q[0] * f[1] + q[1] * f[5] + q[2] * f[9] + f[13]) / w, w)


def test(f, lp):
    if not all(math.isfinite(z) and abs(z) < 1e7 for z in f):
        return None
    if abs(f[15]) > 5.0:
        return None
    fw = (f[3], f[7], f[11])
    t = (f[12], f[13], f[14])
    nf = math.hypot(*fw)
    nt = math.hypot(*t)
    if not (0.9 < nf < 1.1):
        return None
    if not (100.0 < nt < 9000.0):
        return None
    cos = (fw[0] * t[0] + fw[1] * t[1] + fw[2] * t[2]) / (nf * nt)
    if abs(cos) < 0.9:
        return None
    a = proj(f, lp)
    if not a or a[2] <= 1.0:
        return None
    if abs(a[0]) > 2.0 or abs(a[1]) > 2.0:
        return None
    if not (0.2 < a[2] / nt < 6.0):
        return None
    return abs(cos), nt, a, cos


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
    p("local", hex(local), [round(v, 1) for v in lp])

    found = []
    seen = set()

    def check(kind, where, k, f):
        r = test(f, lp)
        if r:
            found.append((r[0], kind, where, k, f, r))

    def window(kind, where, tgt):
        blob = rd(tgt, 0x80)
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
                blob = window("PTR", off + i, v)
                if not blob:
                    continue
                for j in range(0, 0x80 - 7, 8):
                    w = struct.unpack_from("<Q", blob, j)[0]
                    if heap(w) and w not in seen:
                        seen.add(w)
                        window("PTR2", off + i, w)
        off += 0x100000

    found.sort(key=lambda x: -x[0])
    p("candidates", len(found))
    for cos, kind, where, k, f, r in found[:10]:
        _, nt, a, cs = r
        p("  %s where=%s inner=%s cos=%.4f dist=%.1f ndc=%.3f,%.3f w=%.1f" % (
            kind, hex(where), hex(k), cs, nt, a[0], a[1], a[2]))
        p("   m", [round(z, 6) for z in f])

    c.close()
    return 0


try:
    sys.exit(main())
except Exception as exc:
    p("ABORTED:", type(exc).__name__, exc)
    sys.exit(2)
