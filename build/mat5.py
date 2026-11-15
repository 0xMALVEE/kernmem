import math, struct, sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "sdk"))
import vgb

GAME = "League of Legends.exe"
NEW_LOCAL = 0x1cbb810
POS = 0xc20
EXPECT = 0x1D45BF0

pid = 0
c = None


def rd(a, n):
    try:
        d = c.read(pid, a, n)
        return d if len(d) == n else None
    except Exception:
        return None


def ndc(f, p):
    w = p[0] * f[3] + p[1] * f[7] + p[2] * f[11] + f[15]
    if abs(w) < 1e-6:
        return None
    return ((p[0] * f[0] + p[1] * f[4] + p[2] * f[8] + f[12]) / w,
            (p[0] * f[1] + p[1] * f[5] + p[2] * f[9] + f[13]) / w, w)


def main():
    global pid, c
    c = vgb.Client()
    c.open()
    c.require_version()
    pid = c.pid_by_name(GAME)
    base = c.main_base(pid)
    local = struct.unpack("<Q", rd(base + NEW_LOCAL, 8))[0]
    lp = struct.unpack("<3f", rd(local + POS, 12))
    print("base", hex(base), "local", hex(local), "pos", [round(v, 1) for v in lp])
    print("expect slot", hex(EXPECT))

    regions = ((EXPECT - 0x2000, EXPECT + 0x2000, "near-expect"),
               (0x1d00000, 0x1d80000, "band"),
               (NEW_LOCAL - 0x20000, NEW_LOCAL + 0x20000, "near-local"))

    for lo, hi, label in regions:
        print("==", label, hex(lo), hex(hi))
        hits = 0
        off = lo
        while off < hi and hits < 25:
            n = min(0x8000, hi - off)
            data = rd(base + off, n)
            if data:
                for i in range(0, n - 63, 4):
                    f = struct.unpack_from("<16f", data, i)
                    if not all(math.isfinite(z) and abs(z) < 1e9 for z in f):
                        continue
                    if abs(f[15]) > 4.0:
                        continue
                    nz = sum(1 for z in f if abs(z) > 1e-9)
                    if nz < 9:
                        continue
                    big = max(abs(f[12]), abs(f[13]), abs(f[14]))
                    if not (500.0 < big < 1e8):
                        continue
                    r = ndc(f, lp)
                    if not r or r[2] <= 1.0 or abs(r[0]) > 1.5 or abs(r[1]) > 1.5:
                        continue
                    print("  slot", hex(off + i), "m15", round(f[15], 3),
                          "t", [round(z, 1) for z in f[12:15]],
                          "ndc", round(r[0], 3), round(r[1], 3), "w", round(r[2], 1))
                    print("   m", [round(z, 5) for z in f])
                    hits += 1
                    if hits >= 25:
                        break
            off += 0x8000
        print("  hits", hits)

    c.close()
    return 0


try:
    sys.exit(main())
except Exception as exc:
    print("ABORTED:", type(exc).__name__, exc)
    sys.exit(2)
