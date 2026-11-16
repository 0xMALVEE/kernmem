"""Locate the rotation next to the working position offset.

The position reads correctly as floats at root+0x1B4 (map coordinates with
z = 0), so the rotation should follow it. This dumps the surrounding window and
then fits every aligned candidate as a rotation, scoring by real projection.

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

WIDTH = 1920
HEIGHT = 1080

RISES = (0.0, 40.0, 62.0, 75.0)
FOVS = (60.0, 70.0, 80.0, 90.0, 103.0)

ROT_OFFSETS = tuple(range(POSITION + 0x0C, POSITION + 0x40, 4))


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


def rot_sane(t):
    if any(math.isnan(v) or math.isinf(v) or abs(v) > 400.0 for v in t):
        return False
    return max(abs(v) for v in t) >= 0.5


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

        positioned = []
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
                positioned.append((entry, root, point))

        print(f"positioned {len(positioned)}")
        if len(positioned) < 4:
            return 1

        for entry, root, point in positioned[:3]:
            print(
                f"\n{entry:#x} root {root:#x} "
                f"pos {point[0]:.1f} {point[1]:.1f} {point[2]:.1f}"
            )
            try:
                raw = c.read(pid, root + 0x1A0, 0x60)
            except Exception as error:
                print(f"  dump failed: {error}")
                continue
            for offset in range(0, len(raw) - 11, 4):
                triple = struct.unpack_from("<3f", raw, offset)
                print(
                    f"  +{0x1A0 + offset:#05x} "
                    f"{triple[0]:>13.2f} {triple[1]:>13.2f} {triple[2]:>13.2f}"
                )

        best = []

        for entry, root, eye in positioned:
            others = [p for e, r, p in positioned if e != entry]
            if len(others) < 3:
                continue

            rots = []
            for offset in ROT_OFFSETS:
                try:
                    rot = struct.unpack("<3f", c.read(pid, root + offset, 12))
                except Exception:
                    continue
                if rot_sane(rot):
                    rots.append((offset, rot))

            for offset, rot in rots:
                for rise in RISES:
                    loc = (eye[0], eye[1], eye[2] + rise)
                    for fov in FOVS:
                        hits = 0
                        for point in others:
                            screen = project(loc, rot, fov, point)
                            if screen and 0 <= screen[0] <= WIDTH and 0 <= screen[1] <= HEIGHT:
                                hits += 1
                        if hits >= 3:
                            best.append((hits, entry, offset, rise, fov, rot))

        best.sort(key=lambda item: (-item[0], abs(item[3])))
        print(f"\nfits {len(best)}")

        for hits, entry, offset, rise, fov, rot in best[:8]:
            print(
                f"  {hits} eye {entry:#x} rot+{offset:#x} rise {rise:.0f} fov {fov:.0f} "
                f"{rot[0]:.1f} {rot[1]:.1f} {rot[2]:.1f}"
            )

    return 0 if best else 2


if __name__ == "__main__":
    raise SystemExit(main())
