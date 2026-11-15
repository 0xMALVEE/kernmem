"""Test the documented view_target -> pov chain on the camera actor.

The 11.07 note records camera_manager 0x528 as pointing at BaseShooterCamera_C,
with view_target at 0x580 and pov at 0x10 on that target, whose FMinimalViewInfo
is location 0x00 / rotation 0x18 / fov 0x30.

This dumps the camera actor around 0x540-0x5C0 so the pointers are visible, then
tests the chain (both inline and one more deref) and scores each candidate by how
many known actors it actually projects on screen. Reads only.
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

VT_OFFSETS = (0x580, 0x578, 0x588, 0x590, 0x570)


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


def try_pov(c, pid, label, pov, points):
    try:
        raw = c.read(pid, pov, 0x40)
    except Exception as error:
        print(f"  {label} pov {pov:#x} read failed: {error}")
        return 0

    loc = struct.unpack_from("<3d", raw, 0x00)
    rot = struct.unpack_from("<3d", raw, 0x18)
    fov = struct.unpack_from("<f", raw, 0x30)[0]

    print(
        f"  {label} pov {pov:#x} loc {loc[0]:.1f} {loc[1]:.1f} {loc[2]:.1f} "
        f"rot {rot[0]:.1f} {rot[1]:.1f} {rot[2]:.1f} fov {fov:.1f} sane={sane(loc)}"
    )

    if not sane(loc):
        return 0
    if any(math.isnan(v) or math.isinf(v) or abs(v) > 400.0 for v in rot):
        return 0
    if not (40.0 < fov < 140.0):
        return 0

    on_screen = 0
    for point in points:
        screen = project(loc, rot, fov, point)
        if screen and 0 <= screen[0] <= WIDTH and 0 <= screen[1] <= HEIGHT:
            on_screen += 1

    print(f"    -> on_screen {on_screen}/{len(points)}")
    return on_screen


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
        if len(points) < 3:
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

        cam = q(c, pid, controller + CAMERA)
        print(f"camera actor {cam:#x}")
        if not ok(cam):
            return 3

        try:
            raw = c.read(pid, cam + 0x540, 0x80)
        except Exception as error:
            print(f"camera dump failed: {error}")
            raw = b""

        for offset in range(0, len(raw), 8):
            word = struct.unpack_from("<Q", raw, offset)[0]
            mark = "ptr" if ok(word) else ""
            print(f"  cam+{0x540 + offset:#05x} {word:#018x} {mark}")

        best = 0

        for offset in VT_OFFSETS:
            view = q(c, pid, cam + offset)
            print(f"\nview_target +{offset:#x} -> {view:#x}")
            if not ok(view):
                continue

            best = max(best, try_pov(c, pid, f"inline{offset:#x}", view + 0x10, points))

            inner = q(c, pid, view + 0x10)
            if ok(inner):
                best = max(best, try_pov(c, pid, f"deref{offset:#x}", inner, points))

        print(f"\nbest on_screen {best}")

    return 0 if best >= 2 else 4


if __name__ == "__main__":
    raise SystemExit(main())
