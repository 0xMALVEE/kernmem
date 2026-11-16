"""Resolve the full camera chain with the corrected 11.11 layout and validate it.

acknowledged_pawn is 0x4B0 and the POV is at camera_manager + 0x10, whose
FMinimalViewInfo is location 0x00, rotation 0x18, fov 0x30. A camera is only
accepted when it actually projects known actors on screen. Reads only.
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

WORLD_SLOTS = (0xDB8D780, 0xD92E288, 0xD727560, 0xDB74B40)
ROOT = 0x290
POSITION = 0x1B4
PAWN = 0x4B0
CAMERA = 0x528
POV = 0x10

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


def main():
    game = reader.Game(allow_stale=True)
    game.open()
    pid = game.pid
    base = game.base

    with game, km.Client() as c:
        c.require_version()
        print(f"pid {pid} base {base:#x} patch {game.patch}")

        actors = []
        seen = set()

        for slot in WORLD_SLOTS:
            world = q(c, pid, base + slot)
            print(f"\nworld slot {slot:#x} -> {world:#x} {'ptr' if ok(world) else 'NOT ptr'}")
            if not ok(world):
                continue

            level = q(c, pid, world + 0x38)
            if not ok(level):
                print("  level not a pointer")
                continue

            data = q(c, pid, level + 0xA0)
            count = i32(c, pid, level + 0xA8)
            print(f"  level {level:#x} array {data:#x} num {count}")
            if not ok(data) or not (0 < count < 5000):
                continue

            for index in range(min(count, 200)):
                actor = q(c, pid, data + index * 8)
                if not ok(actor) or actor in seen:
                    continue
                seen.add(actor)
                root = q(c, pid, actor + ROOT)
                if not ok(root):
                    continue
                try:
                    pos = struct.unpack("<3f", c.read(pid, root + POSITION, 12))
                except Exception:
                    continue
                if sane(pos):
                    actors.append(pos)

        print(f"\nactors with positions {len(actors)}")
        if len(actors) < 2:
            return 2

        hits = 0

        for delta in range(START, END, CHUNK):
            try:
                raw = c.read(pid, base + delta, CHUNK)
            except Exception:
                continue

            for index in range(0, CHUNK, 8):
                candidate = struct.unpack_from("<Q", raw, index)[0]
                if not ok(candidate):
                    continue

                manager = q(c, pid, candidate + CAMERA)
                if not ok(manager):
                    continue

                for pov in (q(c, pid, manager + POV), manager + POV):
                    if not ok(pov):
                        continue
                    try:
                        loc = struct.unpack("<3d", c.read(pid, pov + 0x00, 24))
                        rot = struct.unpack("<3d", c.read(pid, pov + 0x18, 24))
                        fov = struct.unpack("<f", c.read(pid, pov + 0x30, 4))[0]
                    except Exception:
                        continue

                    if not sane(loc):
                        continue
                    if any(math.isnan(v) or math.isinf(v) or abs(v) > 400.0 for v in rot):
                        continue
                    if not (40.0 < fov < 140.0):
                        continue

                    on_screen = 0
                    for point in actors:
                        screen = project(loc, rot, fov, point)
                        if screen and 0 <= screen[0] <= WIDTH and 0 <= screen[1] <= HEIGHT:
                            on_screen += 1

                    if on_screen < 2:
                        continue

                    print(
                        f"HIT slot {base + delta + index:#x} controller {candidate:#x} "
                        f"manager {manager:#x} pov {pov:#x} on_screen {on_screen}"
                    )
                    print(
                        f"    loc {loc[0]:.1f} {loc[1]:.1f} {loc[2]:.1f} "
                        f"rot {rot[0]:.1f} {rot[1]:.1f} {rot[2]:.1f} fov {fov:.1f}"
                    )
                    pawn = q(c, pid, candidate + PAWN)
                    print(f"    pawn {pawn:#x}")
                    hits += 1
                    if hits >= 5:
                        return 0

        print(f"hits {hits}")

    return 0 if hits else 3


if __name__ == "__main__":
    raise SystemExit(main())
