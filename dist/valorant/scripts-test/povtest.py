"""Test the ViewInfo pointer chain under the camera manager.

manager+0x17C0 and manager+0x17B0 both hold pointers. Every earlier test read
those slots as if they were the FMinimalViewInfo itself, so the struct was never
actually dereferenced. This walks each slot one and two levels deep and scores
each candidate by how many known actors it projects on screen, for doubles and
for floats.

Reads only.
"""

from __future__ import annotations

import math
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "scripts"))
sys.path.insert(0, str(Path(__file__).resolve().parent.parent.parent / "sdk"))

import km  # noqa: E402

import reader  # noqa: E402

LO = 0x1000000000
HI = 0x7FFFFFFFFFFF

ARRAY_SLOT = 0xD91FDD0
ROOT = 0x290
POSITION = 0x1B4
PAWN = 0x4B0
CAMERA = 0x528

WIDTH = 1920
HEIGHT = 1080

SLOTS = (0x17C0, 0x17B0, 0x17D0, 0x2630, 0x2640)


def ok(v):
    return LO <= v <= HI and v % 8 == 0


def q(c, pid, a):
    if not a:
        return 0
    try:
        return struct.unpack("<Q", c.read(pid, a, 8))[0]
    except Exception:
        return 0


def i32(c, pid, a):
    try:
        return struct.unpack("<i", c.read(pid, a, 4))[0]
    except Exception:
        return 0


def sane(t):
    if any(math.isnan(v) or math.isinf(v) or abs(v) > 300000.0 for v in t):
        return False
    return sum(1 for v in t if abs(v) > 100.0) >= 2


def project(loc, rot, fov, point):
    pitch, yaw, roll = (math.radians(v) for v in rot)
    dx = point[0] - loc[0]
    dy = point[1] - loc[1]
    dz = point[2] - loc[2]

    cy, sy = math.cos(yaw), math.sin(yaw)
    cp, sp = math.cos(pitch), math.sin(pitch)
    cr, sr = math.cos(roll), math.sin(roll)

    x = dx * cy + dy * sy
    y = -dx * sy + dy * cy
    y, z = y * cp - dz * sp, y * sp + dz * cp
    x, y = x * cr - y * sr, x * sr + y * cr

    if y <= 0.5:
        return None

    tan_v = math.tan(math.radians(fov) / 2.0)
    tan_h = tan_v * (WIDTH / HEIGHT)

    return (
        WIDTH / 2.0 + (x / y) * (WIDTH / 2.0) / tan_h,
        HEIGHT / 2.0 - (z / y) * (HEIGHT / 2.0) / tan_v,
    )


def score(loc, rot, fov, points):
    hits = 0
    for point in points:
        screen = project(loc, rot, fov, point)
        if screen and 0 <= screen[0] <= WIDTH and 0 <= screen[1] <= HEIGHT:
            hits += 1
    return hits


def try_block(c, pid, label, pov, points):
    best = 0

    for kind in ("d", "f"):
        try:
            raw = c.read(pid, pov, 0x40)
        except Exception as error:
            print(f"  {label} {kind}: read failed {error}")
            continue

        if kind == "d":
            loc = struct.unpack_from("<3d", raw, 0x00)
            rot = struct.unpack_from("<3d", raw, 0x18)
            fov = struct.unpack_from("<f", raw, 0x30)[0]
        else:
            loc = struct.unpack_from("<3f", raw, 0x00)
            rot = struct.unpack_from("<3f", raw, 0x0C)
            fov = struct.unpack_from("<f", raw, 0x28)[0]

        good_loc = sane(loc)
        good_rot = all(math.isfinite(v) and abs(v) <= 400.0 for v in rot)
        good_fov = 40.0 < fov < 140.0
        hits = score(loc, rot, fov, points) if (good_loc and good_rot and good_fov) else 0

        print(
            f"  {label} {kind} pov {pov:#x} loc {loc[0]:.1f} {loc[1]:.1f} {loc[2]:.1f} "
            f"rot {rot[0]:.1f} {rot[1]:.1f} {rot[2]:.1f} fov {fov:.1f} "
            f"loc_ok {good_loc} fov_ok {good_fov} hits {hits}"
        )

        best = max(best, hits)

    return best


def main():
    game = reader.Game(allow_stale=True)
    game.open()
    pid = game.pid
    base = game.base

    with game, km.Client() as c:
        c.require_version()
        print(f"pid {pid} base {base:#x} patch {game.patch}")

        slot = base + ARRAY_SLOT
        data = q(c, pid, slot)
        num = i32(c, pid, slot + 8)

        points = []
        seen = set()

        for index in range(min(max(num, 0), 64)):
            entry = q(c, pid, data + index * 8)
            if not ok(entry) or entry in seen:
                continue
            seen.add(entry)
            root = q(c, pid, entry + ROOT)
            if not ok(root):
                continue
            try:
                point = struct.unpack("<3f", c.read(pid, root + POSITION, 12))
            except Exception:
                continue
            if sane(point):
                points.append(point)

        print(f"reference points {len(points)}")
        if len(points) < 2:
            return 1

        controller = 0
        for delta in range(0x9000000, 0xE200000, 0x1000):
            try:
                raw = c.read(pid, base + delta, 0x1000)
            except Exception:
                continue
            for index in range(0, 0x1000, 8):
                candidate = struct.unpack_from("<Q", raw, index)[0]
                if ok(candidate) and q(c, pid, candidate + PAWN) in seen:
                    controller = candidate
                    break
            if controller:
                break

        manager = q(c, pid, controller + CAMERA) if controller else 0
        print(f"controller {controller:#x} manager {manager:#x}")
        if not ok(manager):
            return 2

        best = 0

        for slot_off in SLOTS:
            node = manager + slot_off
            for depth in (1, 2):
                node = q(c, pid, node)
                if not ok(node):
                    print(f"+{slot_off:#x} depth {depth}: no pointer")
                    break
                best = max(best, try_block(c, pid, f"+{slot_off:#x} d{depth}", node, points))

        print(f"\nbest hits {best}")

    return 0 if best >= 2 else 3


if __name__ == "__main__":
    raise SystemExit(main())
