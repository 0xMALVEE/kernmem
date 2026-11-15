"""Find the local camera by anchoring on a working actor array.

APlayerController holds the pawn at 0x4B0, so any pointer whose 0x4B0 is one of
the known actors is a controller candidate. Its camera manager (+0x528) is then
tested at both documented POV locations: inline at +0x10, and through the
private cache at +0x17B0 -> +0x10.

A camera is only accepted when it projects at least two known actors on screen.
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
CAMERA = 0x528

PAWN_OFFSETS = (0x4B0, 0x518, 0x510)

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

    with game, vgb.Client() as c:
        c.require_version()
        print(f"pid {pid} base {base:#x} patch {game.patch}")

        slot = base + ARRAY_SLOT
        data = q(c, pid, slot)
        num = i32(c, pid, slot + 8)
        print(f"array slot {slot:#x} data {data:#x} num {num}")
        if not ok(data) or num <= 0:
            return 1

        actors = set()
        points = []

        for index in range(min(num, 64)):
            actor = q(c, pid, data + index * 8)
            if not ok(actor):
                continue
            actors.add(actor)
            root = q(c, pid, actor + ROOT)
            if not ok(root):
                continue
            try:
                pos = struct.unpack("<3f", c.read(pid, root + POSITION, 12))
            except Exception:
                continue
            if sane(pos):
                points.append(pos)

        print(f"actors {len(actors)} with positions {len(points)}")
        if len(points) < 2:
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

                pawn = 0
                for offset in PAWN_OFFSETS:
                    value = q(c, pid, candidate + offset)
                    if value in actors:
                        pawn = value
                        break
                if not pawn:
                    continue

                manager = q(c, pid, candidate + CAMERA)
                if not ok(manager):
                    continue

                print(
                    f"CTRL slot {base + delta + index:#x} controller {candidate:#x} "
                    f"pawn {pawn:#x} manager {manager:#x}"
                )

                for label, first, second in (("inline", 0x10, None), ("private", 0x17B0, 0x10)):
                    pov = manager + first
                    if second is not None:
                        pov = q(c, pid, pov)
                    if not ok(pov):
                        print(f"  {label}: no pov")
                        continue

                    try:
                        loc = struct.unpack("<3d", c.read(pid, pov + 0x00, 24))
                        rot = struct.unpack("<3d", c.read(pid, pov + 0x18, 24))
                        fov = struct.unpack("<f", c.read(pid, pov + 0x30, 4))[0]
                    except Exception as error:
                        print(f"  {label}: read failed {error}")
                        continue

                    if not sane(loc):
                        print(f"  {label}: loc not sane")
                        continue

                    on_screen = 0
                    for point in points:
                        screen = project(loc, rot, fov, point)
                        if screen and 0 <= screen[0] <= WIDTH and 0 <= screen[1] <= HEIGHT:
                            on_screen += 1

                    print(
                        f"  {label} pov {pov:#x} on_screen {on_screen} "
                        f"loc {loc[0]:.1f} {loc[1]:.1f} {loc[2]:.1f} "
                        f"rot {rot[0]:.1f} {rot[1]:.1f} {rot[2]:.1f} fov {fov:.1f}"
                    )

                    if on_screen >= 2:
                        hits += 1

                if hits:
                    return 0

        print(f"hits {hits}")

    return 0 if hits else 3


if __name__ == "__main__":
    raise SystemExit(main())
