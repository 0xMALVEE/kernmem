import math, struct, sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "sdk"))
import vgb

GAME = "League of Legends.exe"
LO = 0x1000000000
HI = 0x7FFFFFFFFFFF
NEW_LOCAL = 0x1cbb810
POS = 0xc20
SUSPECT = 0x1d99340

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


def q64(a):
    b = rd(a, 8)
    return struct.unpack("<Q", b)[0] if b else None


def det3(r0, r1, r2):
    return (r0[0] * (r1[1] * r2[2] - r1[2] * r2[1])
            - r0[1] * (r1[0] * r2[2] - r1[2] * r2[0])
            + r0[2] * (r1[0] * r2[1] - r1[1] * r2[0]))


def view_info(f):
    if not all(math.isfinite(z) and abs(z) < 1e5 for z in f):
        return None
    if abs(f[3]) > 0.01 or abs(f[7]) > 0.01 or abs(f[11]) > 0.01:
        return None
    if abs(f[15] - 1.0) > 0.01:
        return None
    rows = [(f[0], f[1], f[2]), (f[4], f[5], f[6]), (f[8], f[9], f[10])]
    n = [math.hypot(*r) for r in rows]
    if any(x < 0.9 or x > 1.1 for x in n):
        return None
    for i in range(3):
        for j in range(i + 1, 3):
            if abs(sum(rows[i][k] * rows[j][k] for k in range(3))) > 0.02:
                return None
    if abs(det3(*rows) - 1.0) > 0.02:
        return None
    t = (f[12], f[13], f[14])
    nt = math.hypot(*t)
    if not (10.0 < nt < 40000.0):
        return None
    eye = tuple(-sum(t[k] * rows[i][k] for k in range(3)) for i in range(3))
    return eye, nt


def mul(a, b):
    return [sum(a[i * 4 + k] * b[k * 4 + j] for k in range(4))
            for i in range(4) for j in range(4)]


def project(m, q):
    w = q[0] * m[3] + q[1] * m[7] + q[2] * m[11] + m[15]
    if abs(w) < 1e-9:
        return None
    return ((q[0] * m[0] + q[1] * m[4] + q[2] * m[8] + m[12]) / w,
            (q[0] * m[1] + q[1] * m[5] + q[2] * m[9] + m[13]) / w, w)


def main():
    global pid, c
    c = vgb.Client()
    c.open()
    p("protocol", c.version())
    pid = c.pid_by_name(GAME)
    base = c.main_base(pid)
    e = struct.unpack("<I", rd(base + 0x3C, 4))[0]
    size = struct.unpack("<I", rd(base + e + 0x50, 4))[0]
    p("base", hex(base), "size", hex(size))

    local = q64(base + NEW_LOCAL)
    lp = struct.unpack("<3f", rd(local + POS, 12))
    p("local", hex(local), [round(v, 1) for v in lp])

    p("== suspect object dump")
    obj = q64(base + SUSPECT)
    p("  slot %s -> %s" % (hex(SUSPECT), hex(obj) if obj else "?"))
    if obj and heap(obj):
        blob = rd(obj, 0x260)
        if blob:
            for k in range(0, len(blob) - 63, 16):
                f = struct.unpack_from("<16f", blob, k)
                vi = view_info(f)
                p("  +%s %s" % (hex(k), [round(z, 3) for z in f]))
                if vi:
                    p("     VIEW eye=%s dist=%.1f" % ([round(z, 1) for z in vi[0]], math.dist(vi[0], lp)))

    p("== rank every valid view by camera distance to hero ==")
    seen = set()
    out = []
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
                blob = rd(v, 0x400)
                if not blob:
                    continue
                for k in range(0, len(blob) - 63, 4):
                    f = struct.unpack_from("<16f", blob, k)
                    vi = view_info(f)
                    if vi:
                        out.append((math.dist(vi[0], lp), off + i, v, k, vi[0], f))
        off += 0x100000

    out.sort(key=lambda x: x[0])
    p("valid views", len(out))
    for dist, slot, v, k, eye, f in out[:8]:
        p("  dist=%.1f slot=%s ptr=%s inner=%s eye=%s" % (
            dist, hex(slot), hex(v), hex(k), [round(z, 1) for z in eye]))
        p("   m", [round(z, 5) for z in f])
        blob = rd(v, 0x400)
        if not blob:
            continue
        for pk in range(0, len(blob) - 63, 4):
            if abs(pk - k) > 0x120 or pk == k:
                continue
            pm = struct.unpack_from("<16f", blob, pk)
            if abs(pm[15]) > 0.2 or abs(abs(pm[11]) - 1.0) > 0.2:
                continue
            comb = mul(f, pm)
            a = project(comb, lp)
            if not a or a[2] <= 1.0:
                continue
            if abs(a[0]) > 1.5 or abs(a[1]) > 1.5:
                continue
            p("     PROJ inner=%s ndc=%.3f,%.3f w=%.1f" % (hex(pk), a[0], a[1], a[2]))
            p("      proj", [round(z, 5) for z in pm])
            break

    c.close()
    return 0


try:
    sys.exit(main())
except Exception as exc:
    p("ABORTED:", type(exc).__name__, exc)
    sys.exit(2)
