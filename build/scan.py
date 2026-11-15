"""One-off offset scanner.

Recovers LocalPlayer / ObjectManager / ViewMatrix for the RUNNING client build by
structure instead of by the stale table: a hero object is recognised by its team id
and a sane world position, LocalPlayer is the module global that points at one, and
the remaining slots are located by their shifted position and then verified.
"""

import math
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "sdk"))
import vgb  # noqa: E402

GAME = "League of Legends.exe"
HEAP_LO = 0x1000000000
HEAP_HI = 0x7FFFFFFFFFFF

OLD_LOCAL = 0x1F304B0
OLD_OBJ = 0x1EF0D28
OLD_MATRIX = 0x1FBA090

TEAM_OFF = 0x239
POS_OFF = 0x23C
NAME_OFF = 0x4378
HP_OFF = 0x1080
LEVEL_OFF = 0x4D58


def main():
    c = vgb.Client()
    c.open()
    c.require_version()

    pid = c.pid_by_name(GAME)
    if not pid:
        print("game not running")
        return 1
    base = c.main_base(pid)
    print("pid", pid, "base", hex(base))

    def u32(a):
        return struct.unpack("<I", c.read(pid, a, 4))[0]

    def u64(a):
        return struct.unpack("<Q", c.read(pid, a, 8))[0]

    e_lfanew = u32(base + 0x3C)
    size_of_image = u32(base + e_lfanew + 0x68)
    print("image size", hex(size_of_image))
    if not (0x1000 < size_of_image < 0x8000000):
        print("implausible image size")
        return 1

    cands = []
    step = 0x10000
    off = 0
    while off < size_of_image:
        n = min(step, size_of_image - off)
        data = c.read(pid, base + off, n)
        if len(data) == n:
            for i in range(0, n - 7, 8):
                v = struct.unpack_from("<Q", data, i)[0]
                if HEAP_LO <= v <= HEAP_HI and (v & 7) == 0:
                    cands.append((base + off + i, v))
        off += step
    print("candidates", len(cands))

    seen = set()
    heroes = []
    for _ga, p in cands:
        if p in seen:
            continue
        seen.add(p)
        buf = c.read(pid, p, 0x300)
        if len(buf) < 0x300:
            continue
        team = struct.unpack_from("<i", buf, TEAM_OFF)[0]
        if team not in (100, 200):
            continue
        x, y, z = struct.unpack_from("<3f", buf, POS_OFF)
        if not all(math.isfinite(v) for v in (x, y, z)):
            continue
        if max(abs(x), abs(y), abs(z)) > 30000.0:
            continue
        heroes.append(p)
        print("HERO", hex(p), "team", team, "pos", round(x, 1), round(y, 1), round(z, 1))

    if not heroes:
        print("no hero-shaped objects found")
        return 1

    hero_set = set(heroes)
    refs = {}
    for ga, p in cands:
        if p in hero_set:
            refs.setdefault(p, []).append(ga)

    for p, gas in refs.items():
        print("REF", hex(p), "from", [hex(g) for g in gas])

    if not refs:
        print("no module global references a hero")
        return 1

    def named(p):
        buf = c.read(pid, p + NAME_OFF, 0x40)
        if len(buf) < 0x40:
            return ""
        return buf.decode("utf-16-le", "replace").split("\x00")[0]

    local = max(refs, key=lambda p: len(named(p)))
    new_local = refs[local][0] - base
    print("LOCAL", hex(local), "name", repr(named(local)))
    print("new LocalPlayer offset", hex(new_local))

    buf = c.read(pid, local, 0x4E00)
    if len(buf) >= LEVEL_OFF + 4:
        print("  hp", struct.unpack_from("<f", buf, HP_OFF)[0],
              "level", struct.unpack_from("<i", buf, LEVEL_OFF)[0])

    def try_array(addr):
        begin = u64(addr + 0x18)
        end = u64(addr + 0x20)
        if not begin or not end or end <= begin:
            return None
        span = end - begin
        if span % 8 or span > 0x4000:
            return None
        arr = c.read(pid, begin, span)
        if len(arr) != span:
            return None
        vals = struct.unpack("<%dQ" % (span // 8), arr)
        if local not in vals:
            return None
        if sum(1 for v in vals if v in hero_set) < 2:
            return None
        return (begin, end)

    expected_obj = new_local - (OLD_LOCAL - OLD_OBJ)
    obj = 0
    for shift in range(0, 0x2000, 8):
        for cand in (expected_obj + shift, expected_obj - shift):
            if cand <= 0:
                continue
            hit = try_array(base + cand)
            if hit:
                obj = cand
                print("ObjectManager", hex(obj), "array", hex(hit[0]), hex(hit[1]))
                break
        if obj:
            break

    if not obj:
        print("falling back to a full scan for the object array")
        seen_t = set()
        for ga, p in cands[:60000]:
            if p in seen_t:
                continue
            seen_t.add(p)
            hit = try_array(p)
            if hit:
                obj = ga - base
                print("ObjectManager", hex(obj), "array", hex(hit[0]), hex(hit[1]))
                break

    if not obj:
        print("ObjectManager not found")

    expected_m = new_local + (OLD_MATRIX - OLD_LOCAL)
    for shift in range(0, 0x4000, 8):
        found = 0
        for cand in (expected_m + shift, expected_m - shift):
            if cand <= 0:
                continue
            inst = u64(base + cand)
            if not (HEAP_LO <= inst <= HEAP_HI):
                continue
            m = c.read(pid, inst, 64)
            if len(m) < 64:
                continue
            vals = struct.unpack("<16f", m)
            if not all(math.isfinite(v) for v in vals):
                continue
            if abs(vals[15] - 1.0) > 0.01:
                continue
            if max(abs(vals[12]), abs(vals[13]), abs(vals[14])) > 0.01:
                continue
            print("ViewMatrix", hex(cand), "instance", hex(inst))
            print("  m", [round(v, 4) for v in vals])
            found = 1
            break
        if found:
            break

    print("SUMMARY local=%#x obj=%#x matrix=%#x" % (new_local, obj, expected_m))
    c.close()
    return 0


sys.exit(main())
