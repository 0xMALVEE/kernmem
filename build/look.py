import math, struct, sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "sdk"))
import vgb

GAME = "League of Legends.exe"
NEW_LOCAL = 0x1cbb810
POS = 0xc20

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


def main():
    global pid, c
    c = vgb.Client()
    c.open()
    v = c.version()
    p("protocol", v)
    pid = c.pid_by_name(GAME)
    p("pid", pid)
    base = c.main_base(pid)
    p("base", hex(base))

    raw = rd(base + NEW_LOCAL, 8)
    local = struct.unpack("<Q", raw)[0] if raw else 0
    p("local", hex(local))
    if not local:
        p("local pointer null")
        c.close()
        return 1
    lp = struct.unpack("<3f", rd(local + POS, 12))
    p("pos", [round(x, 1) for x in lp])

    for label, at, span in (("cand", 0x1993b4 - 0x100, 0x300),
                            ("candptr", 0x1877718, 0x40)):
        p("==", label, hex(at))
        blob = rd(base + at, span)
        if not blob:
            p("  unreadable")
            continue
        for k in range(0, len(blob) - 3, 4):
            f = struct.unpack_from("<f", blob, k)[0]
            if not math.isfinite(f) or abs(f) < 1e-9:
                continue
            p("  %s %.5f" % (hex(at + k), f))

    c.close()
    return 0


try:
    sys.exit(main())
except Exception as exc:
    p("ABORTED:", type(exc).__name__, exc)
    sys.exit(2)
