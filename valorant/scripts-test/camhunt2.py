"""Find the camera POV by signature, validated by projection.

A candidate block is accepted only when its location, rotation and FOV put at
least two known actor positions on screen. Reads only.
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

START = 0x9000000
END = 0xE200000
CHUNK = 0x1000

WIDTH = 1920
HEIGHT = 1080


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


def project(location, rotation, fov, point):
    pitch, yaw, roll = (math.radians(v) for v in rotation)
    dx = point[0] - location[0]
    dy = point[1] - location[1]
    dz = point[2] - location[2]

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
        count = i32(c, pid, slot + 8)
        print(f"array data {data:#x} num {count}")

        points = []
        seen = set()

        for index in range(min(max(count, 0), 64)):
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
            if any(math.isnan(v) or math.isinf(v) or abs(v) > 200000 for v in point):
                continue
            if sum(1 for v in point if abs(v) > 100.0) < 2:
                continue
            points.append(point)

        print(f"reference points {len(points)}")
        if len(points) < 2:
            return 2

        hits = 0

        for delta in range(START, END, CHUNK):
            try:
                raw = c.read(pid, base + delta, CHUNK)
            except Exception:
                continue

            for offset in range(0, CHUNK - 0x40, 4):
                location = struct.unpack_from("<3f", raw, offset)
                rotation = struct.unpack_from("<3f", raw, offset + 0x0C)
                fov = struct.unpack_from("<f", raw, offset + 0x28)[0]

                if any(math.isnan(v) or math.isinf(v) for v in location + rotation):
                    continue
                if sum(1 for v in location if abs(v) > 100.0) < 2:
                    continue
                if any(abs(v) > 200000 for v in location):
                    continue
                if any(abs(v) > 400 for v in rotation):
                    continue
                if not (40.0 < fov < 140.0):
                    continue

                on_screen = 0
                for point in points:
                    screen = project(location, rotation, fov, point)
                    if screen and 0 <= screen[0] <= WIDTH and 0 <= screen[1] <= HEIGHT:
                        on_screen += 1

                if on_screen >= 2:
                    address = base + delta + offset
                    print(
                        f"HIT {address:#x} on_screen {on_screen} "
                        f"loc {location[0]:.1f} {location[1]:.1f} {location[2]:.1f} "
                        f"rot {rotation[0]:.1f} {rotation[1]:.1f} {rotation[2]:.1f} fov {fov:.1f}"
                    )
                    hits += 1
                    if hits >= 8:
                        return 0

        print(f"hits {hits}")

    return 0 if hits else 3


if __name__ == "__main__":
    raise SystemExit(main())
