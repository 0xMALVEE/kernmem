"""Find the camera POV by proximity to a real actor, then verify by projection.

A genuine camera sits within a few hundred units of the pawn it looks through,
so every 8-byte-aligned double triple near the camera manager (and near each
heap pointer the manager holds) is tested: the location must be near a known
actor, the rotation sane, the FOV plausible, and the projection must put at
least three actors on screen.

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

NEAR = 500.0
SPAN = 0x1000
POINTER_SPAN = 0x300


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


def scan(c, pid, label, address, points):
    hits = 0

    for start in range(-SPAN, SPAN, 0x80):
        try:
            raw = c.read(pid, address + start, 0x80 + 0x40)
        except Exception:
            continue

        for offset in range(0, 0x80, 8):
            loc = struct.unpack_from("<3d", raw, offset)
            if not sane(loc):
                continue
            if min(math.dist(loc, p) for p in points) > NEAR:
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

            if on_screen < 3:
                continue

            print(
                f"HIT {label}{start + offset:+#x} on_screen {on_screen} "
                f"loc {loc[0]:.1f} {loc[1]:.1f} {loc[2]:.1f} "
                f"rot {rot[0]:.1f} {rot[1]:.1f} {rot[2]:.1f} fov {fov:.1f}"
            )
            hits += 1

    return hits


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
                positioned.append(point)

        print(f"reference points {len(positioned)}")
        if len(positioned) < 3:
            return 1

        controller = 0
        manager = 0

        for delta in range(0x9000000, 0xE200000, 0x1000):
            try:
                raw = c.read(pid, base + delta, 0x1000)
            except Exception:
                continue
            for index in range(0, 0x1000, 8):
                candidate = struct.unpack_from("<Q", raw, index)[0]
                if not ok(candidate):
                    continue
                if q(c, pid, candidate + PAWN) not in seen:
                    continue
                held = q(c, pid, candidate + CAMERA)
                if ok(held):
                    controller, manager = candidate, held
                    break
            if controller:
                break

        print(f"controller {controller:#x} manager {manager:#x}")
        if not controller:
            return 2

        total = scan(c, pid, "manager", manager, positioned)

        try:
            head = c.read(pid, manager, POINTER_SPAN)
        except Exception as error:
            print(f"manager head read failed: {error}")
            head = b""

        inner = []
        for offset in range(0, len(head), 8):
            pointer = struct.unpack_from("<Q", head, offset)[0]
            if ok(pointer) and pointer != manager:
                inner.append((offset, pointer))

        print(f"inner pointers {len(inner)}")

        for offset, pointer in inner[:12]:
            total += scan(c, pid, f"[manager+{offset:#x}]", pointer, positioned)

        print(f"total hits {total}")

    return 0 if total else 3


if __name__ == "__main__":
    raise SystemExit(main())
