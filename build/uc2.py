import math, struct, sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "sdk"))
import vgb

GAME = "League of Legends.exe"
LO = 0x1000000000
HI = 0x7FFFFFFFFFFF
NEW_LOCAL = 0x1cbb810
POS_FOUND = 0xc20

# delta taken from the ObjectManager both sides agree on
POST_OBJ = 0x1CB8400
MY_OBJ = 0x1cbb7f8
DELTA = MY_OBJ - POST_OBJ

POST_HERO = 0x1CB8550
POST_RENDER = 0x1C8A418
VIEW_INNER = (0x1E4, 0x16C, 0x64, 0x1B4)
PROJ_INNER = (0x224, 0x1B4, 0xA4)

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


def vec(addr):
    h = rd(addr, 0x18)
    if not h:
        return None
    begin, end = struct.unpack_from("<2Q", h, 0)
    if not heap(begin) or not heap(end) or end <= begin:
        return None
    span = end - begin
    if span % 8 or span > 0x8000:
        return None
    return begin, span


def det3(r0, r1, r2):
    return (r0[0] * (r1[1] * r2[2] - r1[2] * r2[1])
            - r0[1] * (r1[0] * r2[2] - r1[2] * r2[0])
            + r0[2] * (r1[0] * r2[1] - r1[1] * r2[0]))


def view_score(f):
    if not all(math.isfinite(z) and abs(z) < 1e5 for z in f):
        return -1
    rows = [(f[0], f[1], f[2]), (f[4], f[5], f[6]), (f[8], f[9], f[10])]
    n = [math.hypot(*r) for r in rows]
    if any(x < 0.8 or x > 1.25 for x in n):
        return -1
    score = 0
    for i in range(3):
        for j in range(i + 1, 3):
            d = abs(sum(rows[i][k] * rows[j][k] for k in range(3)))
            if d > 0.1:
                return -1
            score += 1 - d
    det = det3(*rows)
    if abs(det - 1.0) > 0.1:
        return -1
    score += 1 - abs(det - 1.0)
    nt = math.hypot(f[12], f[13], f[14])
    if not (50.0 < nt < 30000.0):
        return -1
    if abs(f[3]) > 0.1 or abs(f[7]) > 0.1 or abs(f[11]) > 0.1:
        return -1
    return score + (1 if abs(f[15]) < 1.5 else 0)


def main():
    global pid, c
    c = vgb.Client()
    c.open()
    p("protocol", c.version())
    pid = c.pid_by_name(GAME)
    base = c.main_base(pid)
    p("base", hex(base), "delta", hex(DELTA))

    local = q64(base + NEW_LOCAL)
    lp = struct.unpack("<3f", rd(local + POS_FOUND, 12))
    p("local", hex(local), [round(v, 1) for v in lp])

    hero_slot = POST_HERO + DELTA
    obj_slot = POST_OBJ + DELTA
    p("predicted ObjectManager", hex(obj_slot), "->", hex(q64(base + obj_slot) or 0))
    hv = q64(base + hero_slot)
    p("predicted HeroManager", hex(hero_slot), "->", hex(hv or 0))
    if hv and heap(hv):
        v = vec(hv)
        if v:
            arr = rd(v[0], min(v[1], 0x4000))
            if arr:
                vals = [struct.unpack_from("<Q", arr, i)[0] for i in range(0, len(arr) - 7, 8)]
                heroes = 0
                for x in vals:
                    if not heap(x):
                        continue
                    t = rd(x + 0x1cc, 4)
                    if t and struct.unpack("<i", t)[0] in (100, 200):
                        heroes += 1
                p("  HeroManager vector count=%d localIn=%s heroes=%d" % (
                    len(vals), local in vals, heroes))
        else:
            p("  HeroManager is not a vector")

    render_slot = POST_RENDER + DELTA
    p("== renderer probe around", hex(render_slot))
    best = []
    for step in range(-0x2000, 0x2001, 8):
        s = render_slot + step
        val = q64(base + s)
        if val is None or not heap(val):
            continue
        blob = rd(val, 0x280)
        if not blob:
            continue
        for inner in VIEW_INNER:
            if inner + 64 > len(blob):
                continue
            f = struct.unpack_from("<16f", blob, inner)
            sc = view_score(f)
            if sc >= 0:
                best.append((sc, s, val, inner, f))
    best.sort(key=lambda x: -x[0])
    p("  view hits", len(best))
    for sc, s, val, inner, f in best[:6]:
        p("   slot=%s ptr=%s inner=%s score=%.3f" % (hex(s), hex(val), hex(inner), sc))
        p("    m", [round(z, 6) for z in f])
        for pi in PROJ_INNER:
            if pi + 64 > 0x280:
                continue
            pf = struct.unpack_from("<16f", blob_of(val), pi) if False else None
        blob = rd(val, 0x280)
        for pi in PROJ_INNER:
            if not blob or pi + 64 > len(blob):
                continue
            pf = struct.unpack_from("<16f", blob, pi)
            p("    inner %s -> %s" % (hex(pi), [round(z, 5) for z in pf]))

    c.close()
    return 0


def blob_of(v):
    return rd(v, 0x280)


try:
    sys.exit(main())
except Exception as exc:
    p("ABORTED:", type(exc).__name__, exc)
    sys.exit(2)
