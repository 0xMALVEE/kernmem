import math, struct, sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "sdk"))
import vgb

GAME = "League of Legends.exe"
LO = 0x1000000000
HI = 0x7FFFFFFFFFFF
NEW_LOCAL = 0x1cbb810
POS = 0xc20
OBJ_SLOT = 0x1cbb7f8

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


def is_view(f):
    if not all(math.isfinite(z) and abs(z) < 1e5 for z in f):
        return False
    if abs(f[3]) > 0.08 or abs(f[7]) > 0.08 or abs(f[11]) > 0.08:
        return False
    if abs(f[15]) > 1.5:
        return False
    rows = [(f[0], f[1], f[2]), (f[4], f[5], f[6]), (f[8], f[9], f[10])]
    n = [math.hypot(*r) for r in rows]
    if any(x < 0.8 or x > 1.25 for x in n):
        return False
    for i in range(3):
        for j in range(i + 1, 3):
            if abs(sum(rows[i][k] * rows[j][k] for k in range(3))) > 0.08:
                return False
    if abs(det3(*rows) - 1.0) > 0.08:
        return False
    nt = math.hypot(f[12], f[13], f[14])
    return 20.0 < nt < 40000.0


def is_proj(f):
    if not all(math.isfinite(z) and abs(z) < 1e5 for z in f):
        return False
    if abs(f[3]) > 0.08 or abs(f[7]) > 0.08:
        return False
    if abs(abs(f[11]) - 1.0) > 0.12:
        return False
    if abs(f[15]) > 0.12:
        return False
    return f[0] > 0.05 and f[5] > 0.05


def mul(a, b):
    out = [0.0] * 16
    for i in range(4):
        for j in range(4):
            out[i * 4 + j] = sum(a[i * 4 + k] * b[k * 4 + j] for k in range(4))
    return out


def project(m, q):
    w = q[0] * m[3] + q[1] * m[7] + q[2] * m[11] + m[15]
    if abs(w) < 1e-9:
        return None
    return ((q[0] * m[0] + q[1] * m[4] + q[2] * m[8] + m[12]) / w,
            (q[0] * m[1] + q[1] * m[5] + q[2] * m[9] + m[13]) / w, w)


def hero_check(m, lp):
    a = project(m, lp)
    if not a or a[2] <= 1.0:
        return None
    if abs(a[0]) > 2.0 or abs(a[1]) > 2.0:
        return None
    off = project(m, (lp[0] + 500.0, lp[1], lp[2]))
    if not off or off[2] <= 1.0:
        return None
    sp = math.hypot(off[0] - a[0], off[1] - a[1])
    if sp < 1e-4:
        return None
    return a, sp


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

    p("== GameTime / neighbourhood of ObjectManager ==")
    for off in (-0x20, -0x18, -0x10, -8, 0, 8, 0x20, 0x150, 0x1C0, 0x9A38):
        slot = OBJ_SLOT + off
        v = q64(base + slot)
        p("  %s -> %s" % (hex(slot), hex(v) if v is not None else "?"))

    p("== HeroManager window search ==")
    for step in range(-0x4000, 0x4001, 8):
        slot = OBJ_SLOT + step
        v = q64(base + slot)
        if not v or not heap(v):
            continue
        h = rd(v, 0x18)
        if not h:
            continue
        begin, end = struct.unpack_from("<2Q", h, 0)
        if not heap(begin) or not heap(end) or end <= begin:
            continue
        span = end - begin
        if span % 8 or span > 0x8000 or span < 0x10:
            continue
        arr = rd(begin, min(span, 0x2000))
        if not arr:
            continue
        vals = [struct.unpack_from("<Q", arr, i)[0] for i in range(0, len(arr) - 7, 8)]
        heroes = 0
        for x in vals:
            if not heap(x):
                continue
            t = rd(x + 0x1cc, 4)
            if t and struct.unpack("<i", t)[0] in (100, 200):
                heroes += 1
        if local in vals or heroes >= 2:
            p("  HERO slot=%s count=%d localIn=%s heroes=%d" % (
                hex(slot), len(vals), local in vals, heroes))

    p("== renderer: view+proj pair scan ==")
    seen = set()
    hits = []
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
                blob = rd(v, 0x280)
                if not blob:
                    continue
                views = [k for k in range(0, 0x240, 4)
                         if is_view(struct.unpack_from("<16f", blob, k))]
                if not views:
                    continue
                projs = [k for k in range(0, 0x240, 4)
                         if is_proj(struct.unpack_from("<16f", blob, k))]
                if not projs:
                    continue
                for vk in views:
                    for pk in projs:
                        if pk == vk:
                            continue
                        if abs(pk - vk) > 0x80:
                            continue
                        vm = struct.unpack_from("<16f", blob, vk)
                        pm = struct.unpack_from("<16f", blob, pk)
                        comb = mul(vm, pm)
                        r = hero_check(comb, lp)
                        hits.append((0 if r else 1, off + i, v, vk, pk, vm, pm, comb, r))
        off += 0x100000

    hits.sort(key=lambda x: x[0])
    p("pair candidates", len(hits))
    for bad, slot, v, vk, pk, vm, pm, comb, r in hits[:8]:
        p("  slot=%s ptr=%s view=%s proj=%s heroOk=%s" % (
            hex(slot), hex(v), hex(vk), hex(pk), bool(r)))
        p("   view", [round(z, 5) for z in vm])
        p("   proj", [round(z, 5) for z in pm])
        p("   comb", [round(z, 5) for z in comb])
        if r:
            p("   ndc=%.3f,%.3f w=%.1f sp=%.4f" % (r[0][0], r[0][1], r[0][2], r[1]))

    c.close()
    return 0


try:
    sys.exit(main())
except Exception as exc:
    p("ABORTED:", type(exc).__name__, exc)
    sys.exit(2)
