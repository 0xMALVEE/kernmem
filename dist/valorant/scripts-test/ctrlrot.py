"""Test the controller's control rotation and score it by projection.

ControlRotation lives on the PlayerController, not on the pawn. The pawn is the
eye, every aligned triple in the 0x4C0-0x520 window is tried as a rotation (as
doubles and as floats), and each is scored by how many other actors it puts on
screen.

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

OFFSETS = tuple(range(0x4C0, 0x520, 0x8))
FOVS = (70.0, 90.0, 103.0)
RISES = (0.0, 30.0, 62.0)


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

        print(f"actors {len(seen)} points {len(points)}")
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

        pawn = q(c, pid, controller + PAWN)
        pawn_root = q(c, pid, pawn + ROOT)
        eye = struct.unpack("<3f", c.read(pid, pawn_root + POSITION, 12))
        print(f"pawn {pawn:#x} eye {eye[0]:.1f} {eye[1]:.1f} {eye[2]:.1f}")

        others = [p for p in points if p != eye]
        print(f"others {len(others)}")

        best = []

        for offset in OFFSETS:
            for kind in ("d", "f"):
                try:
                    raw = c.read(pid, controller + offset, 24)
                except Exception:
                    continue
                rot = struct.unpack("<3d", raw) if kind == "d" else struct.unpack("<3f", raw[:12])
                if any(math.isnan(v) or math.isinf(v) or abs(v) > 400.0 for v in rot):
                    continue
                print(f"  +{offset:#x} {kind} {rot[0]:.3f} {rot[1]:.3f} {rot[2]:.3f}")

                if max(abs(v) for v in rot) < 0.5:
                    continue

                for rise in RISES:
                    loc = (eye[0], eye[1], eye[2] + rise)
                    for fov in FOVS:
                        hits = 0
                        for point in others:
                            screen = project(loc, rot, fov, point)
                            if screen and 0 <= screen[0] <= WIDTH and 0 <= screen[1] <= HEIGHT:
                                hits += 1
                        if hits:
                            best.append((hits, offset, kind, rise, fov, rot))

        best.sort(key=lambda item: -item[0])
        print(f"\nscores {len(best)}")
        for hits, offset, kind, rise, fov, rot in best[:6]:
            print(
                f"  {hits}/{len(others)} ctrl+{offset:#x} {kind} rise {rise:.0f} "
                f"fov {fov:.0f} {rot[0]:.1f} {rot[1]:.1f} {rot[2]:.1f}"
            )

    return 0 if best else 3


if __name__ == "__main__":
    raise SystemExit(main())
