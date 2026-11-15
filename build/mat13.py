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


def strong(f, lp):
    # f is row-major: row3 = (f3,f7,f11) is the negative forward axis (unit)
    if not all(math.isfinite(z) and abs(z) < 1e7 for z in f):
        return None
    fw = (f[3], f[7], f[11])
    nf = math.hypot(*fw)
    if not (0.8 < nf < 1.25):
        return None
    n0 = math.hypot(f[0], f[1], f[2])
    n1 = math.hypot(f[4], f[5], f[6])
    if n0 < 0.2 or n1 < 0.2:
        return None

    def w_of(q):
        return q[0] * f[3] + q[1] * f[7] + q[2] * f[11] + f[15]

    wh = w_of(lp)
    if wh < 5.0:
        return None
    ch = (lp[0] * f[0] + lp[1] * f[4] + lp[2] * f[8] + f[12]) / wh
    cy = (lp[0] * f[1] + lp[1] * f[5] + lp[2] * f[9] + f[13]) / wh
    if abs(ch) > 1.5 or abs(cy) > 1.5:
        return None
    ux, uy, uz = fw[0] / nf, fw[1] / nf, fw[2] / nf
    behind = (lp[0] - 30000.0 * ux, lp[1] - 30000.0 * uy, lp[2] - 30000.0 * uz)
    if w_of(behind) >= 0.0:
        return None
    return wh, ch, cy, nf


def transpose(f):
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
        for orient, g in (("row", f), ("col", transpose(f))):
            r = strong(g, lp)
            if r:
                found.append((r[0], kind, where, k, orient, g))

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
        if off % 0x800000 == 0:
            p("  scanned", hex(off), "seen", len(seen), "hits", len(found))

    p("seen", len(seen), "candidates", len(found))
    found.sort(key=lambda t: t[0])
    for wh, kind, where, k, orient, g in found[:12]:
        p("  %s where=%s inner=%s orient=%s w=%.1f" % (kind, hex(where), hex(k), orient, wh))
        p("   m", [round(z, 6) for z in g])

    c.close()
    return 0


try:
    sys.exit(main())
except Exception as exc:
    p("ABORTED:", type(exc).__name__, exc)
    sys.exit(2)
