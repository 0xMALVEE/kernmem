import math, struct, sys, time
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "sdk"))
import vgb

GAME = "League of Legends.exe"
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


def nm(p):
    b = rd(p + NAME, 0x40)
    if not b:
        return ""
    return b.decode("utf-16-le", "replace").split("\x00")[0]


def project(f, xyz):
    w = xyz[0] * f[3] + xyz[1] * f[7] + xyz[2] * f[11] + f[15]
    if abs(w) < 1e-4:
        return None
    return ((xyz[0] * f[0] + xyz[1] * f[4] + xyz[2] * f[8] + f[12]) / w,
            (xyz[0] * f[1] + xyz[1] * f[5] + xyz[2] * f[9] + f[13]) / w, w)


def test(f, xyz):
    if not all(math.isfinite(z) and abs(z) < 1e8 for z in f):
        return None
    if abs(f[15]) > 5.0:
        return None
    nz = sum(1 for z in f if abs(z) > 1e-9)
    if nz < 10:
        return None
    r = project(f, xyz)
    if not r:
        return None
    if abs(r[0]) > 1.6 or abs(r[1]) > 1.6:
        return None
    spread = 0.0
    for d in ((300, 0, 0), (0, 0, 300)):
        o = project(f, (xyz[0] + d[0], xyz[1] + d[1], xyz[2] + d[2]))
        if o:
            spread = max(spread, abs(o[0] - r[0]), abs(o[1] - r[1]))
    if spread < 1e-4:
        return None
    return r, spread


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
    xyz = struct.unpack("<3f", rd(local + POS, 12))
    print("local", hex(local), repr(nm(local)), [round(v, 1) for v in xyz])

    hits = []
    off = 0
    while off < size and len(hits) < 400:
        n = min(0x100000, size - off)
        data = rd(base + off, n)
        if data:
            for i in range(0, n - 63, 4):
                f = struct.unpack_from("<16f", data, i)
                r = test(f, xyz)
                if r:
                    hits.append((off + i, f, r[1]))
        off += 0x100000
    print("loose hits", len(hits))

    time.sleep(2.0)

    live = []
    for slot, f, spread in hits:
        d = rd(base + slot, 64)
        if not d:
            continue
        f2 = struct.unpack("<16f", d)
        changed = sum(1 for a, b in zip(f, f2) if abs(a - b) > max(1e-6, abs(a) * 1e-6))
        r2 = test(f2, xyz)
        if changed >= 4 and r2:
            live.append((slot, f2, changed, r2[1]))

    live.sort(key=lambda t: -t[2])
    print("changed+valid", len(live))
    for slot, f, changed, spread in live[:10]:
        print("  SLOT", hex(slot), "changed", changed, "spread", round(spread, 4))
        print("   m", [round(z, 6) for z in f])

    c.close()
    return 0


try:
    sys.exit(main())
except Exception as exc:
    print("ABORTED:", type(exc).__name__, exc)
    sys.exit(2)
