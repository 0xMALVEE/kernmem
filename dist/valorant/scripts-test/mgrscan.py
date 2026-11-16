"""Scan the camera manager object itself for the view info block.

Takes the live controller, walks to its camera manager, then tests every
aligned offset in the manager (and the controller) for a double FMinimalViewInfo
whose projection puts the known actors on screen. Reads only.
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
SPAN = 0x4000
NEED = 3


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


def scan(c, pid, label, address, points):
    try:
        raw = c.read(pid, address, SPAN)
    except Exception as error:
        print(f"{label}: read failed {error}")
        return 0

    found = 0

    for offset in range(0, len(raw) - 0x40, 8):
        loc = struct.unpack_from("<3d", raw, offset)
        if not sane(loc):
            continue

        rot = struct.unpack_from("<3d", raw, offset + 0x18)
        if any(math.isnan(v) or math.isinf(v) or abs(v) > 400.0 for v in rot):
            continue
        if max(abs(v) for v in rot) < 1.0:
            continue

        fov = struct.unpack_from("<f", raw, offset + 0x30)[0]
        if not (50.0 < fov < 130.0):
            continue

        on_screen = 0
        for point in points:
            screen = project(loc, rot, fov, point)
            if screen and 0 <= screen[0] <= WIDTH and 0 <= screen[1] <= HEIGHT:
                on_screen += 1

        if on_screen < NEED:
            continue

        print(
            f"HIT {label}+{offset:#x} on_screen {on_screen}/{len(points)} "
            f"loc {loc[0]:.1f} {loc[1]:.1f} {loc[2]:.1f} "
            f"rot {rot[0]:.1f} {rot[1]:.1f} {rot[2]:.1f} fov {fov:.1f}"
        )
        found += 1

    return found


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
        if len(points) < NEED:
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

        print(f"controller {controller:#x}")
        if not controller:
            return 2

        total = 0
        total += scan(c, pid, "controller", controller, points)

        manager = q(c, pid, controller + CAMERA)
        print(f"manager {manager:#x}")
        if ok(manager):
            total += scan(c, pid, "manager", manager, points)

        print(f"total hits {total}")

    return 0 if total else 3


if __name__ == "__main__":
    raise SystemExit(main())
