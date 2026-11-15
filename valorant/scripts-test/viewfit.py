"""Fit eye point, rotation offset and FOV against known actor positions.

Every actor with a sane float position is tried as the eye (plus vertical
rises), every 4-byte-aligned triple in the controller and pawn is tried as a
rotation, and each combination is scored by how many *other* actors it puts on
screen. Reads only.
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

WIDTH = 1920
HEIGHT = 1080

SPAN = 0x700
FOVS = (55.0, 65.0, 75.0, 85.0, 103.0)
RISES = (0.0, 40.0, 62.0, 75.0)


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
    """Map positions read as (x, y, 0.0), so z is allowed to be zero."""

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

        actors = []
        seen = set()
        for index in range(min(max(num, 0), 64)):
            entry = q(c, pid, data + index * 8)
            if ok(entry) and entry not in seen:
                seen.add(entry)
                actors.append(entry)

        positioned = []
        for actor in actors:
            root = q(c, pid, actor + ROOT)
            if not ok(root):
                continue
            try:
                pos = struct.unpack("<3f", c.read(pid, root + POSITION, 12))
            except Exception:
                continue
            if sane(pos):
                positioned.append((actor, pos))

        print(f"actors {len(actors)} positioned {len(positioned)}")
        if len(positioned) < 4:
            return 1

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
                if q(c, pid, candidate + PAWN) in seen:
                    controller = candidate
                    break
            if controller:
                break

        print(f"controller {controller:#x}")
        if not controller:
            return 2

        rotations = []
        for label, address in (("ctrl", controller), ("pawn", positioned[0][0])):
            try:
                raw = c.read(pid, address, SPAN)
            except Exception:
                continue
            for offset in range(0, SPAN - 24, 4):
                for kind in ("f", "d"):
                    rot = struct.unpack_from("<3f" if kind == "f" else "<3d", raw, offset)
                    if any(math.isnan(v) or math.isinf(v) or abs(v) > 400.0 for v in rot):
                        continue
                    if max(abs(v) for v in rot) < 1.0:
                        continue
                    rotations.append((label, offset, kind, rot))

        print(f"rotation candidates {len(rotations)}")

        best = []

        for actor, eye in positioned:
            others = [p for a, p in positioned if a != actor]
            for rise in RISES:
                loc = (eye[0], eye[1], eye[2] + rise)
                for label, offset, kind, rot in rotations:
                    for fov in FOVS:
                        hits = 0
                        for point in others:
                            screen = project(loc, rot, fov, point)
                            if screen and 0 <= screen[0] <= WIDTH and 0 <= screen[1] <= HEIGHT:
                                hits += 1
                        if hits >= max(3, len(others) - 2):
                            best.append((hits, actor, eye, rise, label, offset, kind, fov, rot))

        best.sort(key=lambda item: (-item[0], abs(item[3])))
        print(f"fits {len(best)}")

        for hits, actor, eye, rise, label, offset, kind, fov, rot in best[:6]:
            print(
                f"  {hits} eye {actor:#x} rise {rise:.0f} {label}+{offset:#x} {kind} fov {fov:.0f} "
                f"rot {rot[0]:.1f} {rot[1]:.1f} {rot[2]:.1f}"
            )

    return 0 if best else 3


if __name__ == "__main__":
    raise SystemExit(main())
