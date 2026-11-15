import math, struct, sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "sdk"))
import vgb

GAME = "League of Legends.exe"
LO = 0x1000000000
HI = 0x7FFFFFFFFFFF
NEW_LOCAL = 0x1cbb810
POS_FOUND = 0xc20
NAME_FOUND = 0x3f00

# post build -> live build delta is taken from LocalPlayer
POST_LOCAL = 0x1CFA7B0
POST_LOCAL_B = 0x1CC6FD8
POST_RENDERER = 0x1C8A418

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


def f32(a):
    b = rd(a, 4)
    return struct.unpack("<f", b)[0] if b else None


def vector_at(addr):
    h = rd(addr, 0x20)
    if not h:
        return None
    begin, end = struct.unpack_from("<2Q", h, 0)
    if not heap(begin) or not heap(end) or end <= begin:
        return None
    span = end - begin
    if span % 8 or span > 0x8000:
        return None
    return begin, end, span // 8


def det3(r0, r1, r2):
    return (r0[0] * (r1[1] * r2[2] - r1[2] * r2[1])
            - r0[1] * (r1[0] * r2[2] - r1[2] * r2[0])
            + r0[2] * (r1[0] * r2[1] - r1[1] * r2[0]))


def is_view(f):
    if not all(math.isfinite(z) and abs(z) < 1e5 for z in f):
        return False
    if abs(f[3]) > 0.05 or abs(f[7]) > 0.05 or abs(f[11]) > 0.05:
        return False
    if abs(f[15] - 1.0) > 0.1 and abs(f[15]) > 0.1:
        return False
    rows = [(f[0], f[1], f[2]), (f[4], f[5], f[6]), (f[8], f[9], f[10])]
    n = [math.hypot(*r) for r in rows]
    if any(not (0.85 < x < 1.15) for x in n):
        return False
    for i in range(3):
        for j in range(i + 1, 3):
            if abs(sum(rows[i][k] * rows[j][k] for k in range(3))) > 0.05:
                return False
    if abs(det3(*rows) - 1.0) > 0.05:
        return False
    t = (f[12], f[13], f[14])
    nt = math.hypot(*t)
    return 50.0 < nt < 30000.0


def is_proj(f):
    if not all(math.isfinite(z) and abs(z) < 1e5 for z in f):
        return False
    if abs(f[3]) > 0.05 or abs(f[7]) > 0.05:
        return False
    if abs(abs(f[11]) - 1.0) > 0.1:
        return False
    if abs(f[15]) > 0.1:
        return False
    return f[0] > 0.1 and f[5] > 0.1


def main():
    global pid, c
    c = vgb.Client()
    c.open()
    p("protocol", c.version())
    pid = c.pid_by_name(GAME)
    base = c.main_base(pid)
    p("base", hex(base))

    local = q64(base + NEW_LOCAL)
    pos = struct.unpack("<3f", rd(local + POS_FOUND, 12))
    name = rd(local + NAME_FOUND, 0x40).decode("utf-16-le", "replace").split("\x00")[0]
    p("local", hex(local), repr(name), [round(v, 1) for v in pos])

    delta = POST_LOCAL - NEW_LOCAL
    delta_b = POST_LOCAL_B - NEW_LOCAL
    p("deltas: %#x %#x" % (delta, delta_b))

    table = (
        ("HeroManager", 0x1CB8550, delta),
        ("ObjectManager", 0x1CB8400, delta),
        ("HandleManager", 0x1CB8400, delta),
        ("NetworkClient", 0x1CB8420, delta),
        ("HudInstance", 0x1CB85C0, delta),
        ("NavGrid", 0x1CBB9D0, delta),
        ("ViewPort", 0x1CBBA50, delta),
        ("UnderMouseObj", 0x1CBBC98, delta),
        ("GameTime", 0x1CC5E38, delta),
        ("ChatManager", 0x1CCDED0, delta),
        ("ObjMgrB", 0x1C88FF0, delta_b),
        ("HeroMgrB", 0x1C88FB0, delta_b),
    )

    p("== predicted globals ==")
    for label, post, d in table:
        slot = post - d
        val = q64(base + slot)
        if val is None:
            p("  %-14s %s unreadable" % (label, hex(slot)))
            continue
        note = ""
        if heap(val):
            vec = vector_at(val)
            if vec:
                note = "VECTOR count=%d" % vec[2]
                arr = rd(vec[0], min(vec[1] - vec[0], 0x800))
                if arr:
                    vals = [struct.unpack_from("<Q", arr, i)[0] for i in range(0, len(arr) - 7, 8)]
                    note += " localInArray=%s heroes=%d" % (
                        local in vals,
                        sum(1 for x in vals if heap(x) and (q64(x + 0x1cc) or 0) in (100, 200)))
            else:
                t = f32(val)
                if t is not None and math.isfinite(t) and abs(t) < 1e7:
                    note = "float=%.3f" % t
        p("  %-14s %s -> %s %s" % (label, hex(slot), hex(val), note))

    p("== position field at the post's 0x168 ==")
    for off in (0x168, 0x164, 0x16c, 0x170):
        tri = rd(local + off, 12)
        if tri:
            p("  %s %s" % (hex(off), [round(v, 2) for v in struct.unpack("<3f", tri)]))
    p("  found-field %s %s" % (hex(POS_FOUND), [round(v, 2) for v in pos]))

    p("== renderer predictions ==")
    for label, post_local in (("hotfix", POST_LOCAL), ("alt", POST_LOCAL_B)):
        slot = POST_RENDERER - (post_local - NEW_LOCAL)
        p("  %s seed %s" % (label, hex(slot)))
        for step in range(-0x40, 0x41, 8):
            s = slot + step
            val = q64(base + s)
            if val is None or not heap(val):
                continue
            blob = rd(val, 0x280)
            if not blob:
                continue
            view = None
            proj = None
            for inner in range(0x180, 0x260, 4):
                if inner + 64 > len(blob):
                    break
                f = struct.unpack_from("<16f", blob, inner)
                if view is None and is_view(f):
                    view = (inner, f)
                if proj is None and is_proj(f):
                    proj = (inner, f)
            if view or proj:
                p("    slot=%s ptr=%s view=%s proj=%s" % (
                    hex(s), hex(val),
                    hex(view[0]) if view else "-", hex(proj[0]) if proj else "-"))
                if view:
                    p("      view m", [round(z, 6) for z in view[1]])
                if proj:
                    p("      proj m", [round(z, 6) for z in proj[1]])

    c.close()
    return 0


try:
    sys.exit(main())
except Exception as exc:
    p("ABORTED:", type(exc).__name__, exc)
    sys.exit(2)
