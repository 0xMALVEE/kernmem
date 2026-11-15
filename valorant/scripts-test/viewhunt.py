"""Find the control rotation and FOV by brute force, validated by projection.

The eye point is taken from the pawn we control (plus a small vertical offset),
and every 4-byte-aligned triple in the controller and pawn is tried as a
rotation. A candidate is ranked by how many known actors it puts on screen.

Reads only.
"""

from __future__ import annotations

import math
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "scripts"))
sys.path.insert(0, str(Path(__file__).resolve().parent.parent.parent / "sdk"))

import vgb  # noqa: E402

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

SPAN = 0x700
FOVS = (55.0, 60.0, 65.0, 70.0, 75.0, 80.0, 90.0, 103.0)
RISES = (0.0, 30.0, 50.0, 60.0, 64.0, 70.0)


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
    return sum(1 for v in t if abs(v) > 50.0) >= 2


def project(loc, rot, fov, point):
    pitch, yaw, roll = (math.radians(v) for v in rot)
    dx, dy, dz = (point[i] - loc[i] for i in range(3))

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


def main():
    game = reader.Game(allow_stale=True)
    game.open()
    pid = game.pid
    base = game.base

    with game, vgb.Client() as c:
        c.require_version()
        print(f"pid {pid} base {base:#x} patch {game.patch}")

        slot = base + ARRAY_SLOT
        data = q(c, pid, slot)
        num = i32(c, pid, slot + 8)
        print(f"array data {data:#x} num {num}")

        actors = []
        seen = set()
        for index in range(min(max(num, 0), 64)):
            entry = q(c, pid, data + index * 8)
            if not ok(entry) or entry in seen:
                continue
            seen.add(entry)
            actors.append(entry)

        # local pawn: whichever actor the controller points at
        pawn = 0
        controller = 0
        for delta in range(0x9000000, 0xE200000, 0x1000):
            try:
                raw = c.read(pid, base + delta, 0x1000)
            except Exception:
                continue
            for index in range(0, 0x1000, 8):
                candidate = struct.unpack_from("<Q", raw, index)[0]
                if not ok(candidate):
                    continue
                value = q(c, pid, candidate + PAWN)
                if value in seen:
                    pawn = value
                    controller = candidate
                    break
            if pawn:
                break

        print(f"controller {controller:#x} pawn {pawn:#x}")
        if not pawn:
            return 1

        root = q(c, pid, pawn + ROOT)
        if not ok(root):
            return 2
        eye = struct.unpack("<3f", c.read(pid, root + POSITION, 12))
        print(f"eye base {eye[0]:.1f} {eye[1]:.1f} {eye[2]:.1f}")

        points = []
        for actor in actors:
            if actor == pawn:
                continue
            aroot = q(c, pid, actor + ROOT)
            if not ok(aroot):
                continue
            try:
                point = struct.unpack("<3f", c.read(pid, aroot + POSITION, 12))
            except Exception:
                continue
            if sane(point):
                points.append(point)

        print(f"reference points {len(points)}")
        if len(points) < 3:
            return 3

        regions = [("controller", controller), ("pawn", pawn)]
        results = []

        for label, address in regions:
            try:
                raw = c.read(pid, address, SPAN)
            except Exception:
                continue

            for offset in range(0, SPAN - 24, 4):
                for kind in ("f", "d"):
                    if kind == "f":
                        rot = struct.unpack_from("<3f", raw, offset)
                    else:
                        rot = struct.unpack_from("<3d", raw, offset)
                    if any(math.isnan(v) or math.isinf(v) or abs(v) > 400.0 for v in rot):
                        continue
                    if max(abs(v) for v in rot) < 1.0:
                        continue

                    for rise in RISES:
                        loc = (eye[0], eye[1], eye[2] + rise)
                        for fov in FOVS:
                            hits = score(loc, rot, fov, points)
                            if hits >= 3:
                                results.append((hits, label, offset, kind, rise, fov, rot))

        results.sort(key=lambda item: -item[0])
        print(f"candidates {len(results)}")

        for hits, label, offset, kind, rise, fov, rot in results[:8]:
            print(
                f"  {hits}/{len(points)} {label}+{offset:#x} {kind} rise {rise:.0f} fov {fov:.0f} "
                f"rot {rot[0]:.1f} {rot[1]:.1f} {rot[2]:.1f}"
            )

        if not results:
            return 4

        hits, label, offset, kind, rise, fov, rot = results[0]
        print(f"\n[best: {label}+{offset:#x} {kind} rise {rise} fov {fov}]")
        for point in points[:10]:
            screen = project((eye[0], eye[1], eye[2] + rise), rot, fov, point)
            print(f"  point {point[0]:.0f},{point[1]:.0f} -> {screen}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
