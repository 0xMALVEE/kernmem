import math, struct, sys, time
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "sdk"))
import vgb
GAME = "League of Legends.exe"
LO = 0x1000000000
HI = 0x7FFFFFFFFFFF
NEW_LOCAL = 0x1cbb810
pid = 0
c = None

def rd(a, n):
    try:
        d = c.read(pid, a, n)
        return d if len(d) == n else None
    except Exception:
        return None

def in_heap(v):
    return LO <= v <= HI and (v & 7) == 0

c = vgb.Client(); c.open(); c.require_version()
pid = c.pid_by_name(GAME)
base = c.main_base(pid)
e = struct.unpack("<I", rd(base + 0x3C, 4))[0]
size = struct.unpack("<I", rd(base + e + 0x50, 4))[0]
print("base", hex(base), "size", hex(size))
local = struct.unpack("<Q", rd(base + NEW_LOCAL, 8))[0]
print("local", hex(local))

print("== position stability at 0xc10..0xc30 ==")
for _ in range(3):
    b = rd(local + 0xc10, 0x24)
    if b:
        vals = struct.unpack("<9f", b)
        print("  ", [round(v, 1) for v in vals])
    time.sleep(1)

print("== array hunt over module globals ==")
hits = {}
off = 0
chunk = 0x100000
while off < size:
    n = min(chunk, size - off)
    data = rd(base + off, n)
    if data:
        for i in range(0, n - 7, 8):
            v = struct.unpack_from("<Q", data, i)[0]
            if in_heap(v):
                hits.setdefault(v, []).append(base + off + i)
    off += chunk
print("globals", len(hits))

best = None
for v in hits:
    h = rd(v, 0x40)
    if not h:
        continue
    for vo in (0x0, 0x8, 0x10, 0x18, 0x20, 0x28, 0x30):
        begin, end = struct.unpack_from("<2Q", h, vo)
        if not in_heap(begin) or not in_heap(end) or end <= begin:
            continue
        span = end - begin
        if span % 8 or span < 0x20 or span > 0x10000:
            continue
        arr = rd(begin, min(span, 0x800))
        if not arr:
            continue
        vals = [struct.unpack_from("<Q", arr, i)[0] for i in range(0, len(arr) - 7, 8)]
        if local not in vals:
            continue
        heroes = 0
        for x in vals[:24]:
            if not in_heap(x):
                continue
            t = rd(x + 0x1cc, 4)
            if t and struct.unpack("<i", t)[0] in (100, 200):
                heroes += 1
        print("  cand off", hex(hits[v][0] - base), "vec@", hex(vo), "count", span // 8, "heroes", heroes)
        if heroes >= 2 and (best is None or heroes > best[0]):
            best = (heroes, hits[v][0] - base, vo, begin, end)

if best:
    print("OBJECTMANAGER", hex(best[1]), "vec@", hex(best[2]), "array", hex(best[3]), hex(best[4]))
    span = min(best[4] - best[3], 0x800)
    arr = rd(best[3], span)
    for i in range(0, span - 7, 8):
        x = struct.unpack_from("<Q", arr, i)[0]
        if not in_heap(x):
            continue
        t = rd(x + 0x1cc, 4)
        n = rd(x + 0x3f00, 0x20)
        p = rd(x + 0xc20, 12)
        print("   elem", hex(x), "team", struct.unpack("<i", t)[0] if t else "?",
              "name", repr(n.decode("utf-16-le", "replace").split("\x00")[0]) if n else "?",
              "pos", [round(z, 1) for z in struct.unpack("<3f", p)] if p else "?")

print("== matrix hunt (strict) ==")
found = 0
start = 0x1b80000
stop = min(size, 0x2100000)
while start < stop and not found:
    n = min(0x40000, stop - start)
    data = rd(base + start, n)
    if data:
        for i in range(0, n - 7, 8):
            v = struct.unpack_from("<Q", data, i)[0]
            if not in_heap(v):
                continue
            m = rd(v, 64)
            if not m:
                continue
            f = struct.unpack("<16f", m)
            if not all(math.isfinite(x) and abs(x) < 1000.0 for x in f):
                continue
            if abs(f[15] - 1.0) > 0.02 or max(abs(f[12]), abs(f[13]), abs(f[14])) > 0.5:
                continue
            if abs(f[0]) < 0.1 or abs(f[5]) < 0.1:
                continue
            if abs(f[11] + 1.0) > 0.2 and abs(f[11]) < 0.1:
                continue
            print("  MATRIX", hex(start + i), "inst", hex(v))
            print("   m", [round(x, 4) for x in f])
            found = 1
            break
    start += 0x40000
if not found:
    print("  none")
c.close()
