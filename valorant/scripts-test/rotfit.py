"""Fit the camera orientation from the controller, using small chunked reads.

Large single reads from heap objects can cross an unmapped page and fail
outright, so the controller is read in small pieces. Every aligned triple is
tried as a rotation (double and float, both axis orders), every sane actor is
tried as the eye plus vertical rises, and each combination is scored by how many
*other* actors it puts on screen.

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

WIDTH = 1920
HEIGHT = 1080

CHUNK = 0x80
SCAN_FROM = 0x100
SCAN_TO = 0x700

RISES = (0.0, 40.0, 62.0, 75.0)
FOVS = tuple(float(v) for v in range(45, 120, 5))


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


def project(loc, rot, fov, point, swap):
    pitch, yaw, roll = (math.radians(v) for v in rot)

    if swap:
        dx = point[0] - loc[0]
        dz = point[1] - loc[1]
        dy = point[2] - loc[2]
    else:
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


def chunked(c, pid, address):
    """Yield small reads over the scan window, skipping failures."""

    for start in range(SCAN_FROM, SCAN_TO, CHUNK):
        try:
            raw = c.read(pid, address + start, CHUNK + 24)
        except Exception:
            continue
        yield start, raw


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
                positioned.append((entry, point))

        print(f"positioned {len(positioned)}")
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
                if ok(candidate) and q(c, pid, candidate + PAWN) in seen:
                    controller = candidate
                    break
            if controller:
                break

        print(f"controller {controller:#x}")
        if not controller:
            return 2

        rotations = []
        blocks = 0

        for start, raw in chunked(c, pid, controller):
            blocks += 1
            for offset in range(0, CHUNK, 4):
                for kind in ("f", "d"):
                    if kind == "f":
                        rot = struct.unpack_from("<3f", raw, offset)
                    else:
                        rot = struct.unpack_from("<3d", raw, offset)
                    if any(math.isnan(v) or math.isinf(v) or abs(v) > 400.0 for v in rot):
                        continue
                    if max(abs(v) for v in rot) < 1.0:
                        continue
                    rotations.append((start + offset, kind, rot))

        print(f"blocks {blocks} rotations {len(rotations)}")
        if not rotations:
            return 3

        best = []

        for actor, eye in positioned:
            others = [p for a, p in positioned if a != actor]
            if len(others) < 3:
                continue
            for rise in RISES:
                loc = (eye[0], eye[1], eye[2] + rise)
                for offset, kind, rot in rotations:
                    for swap in (False, True):
                        for fov in FOVS:
                            hits = 0
                            for point in others:
                                screen = project(loc, rot, fov, point, swap)
                                if screen and 0 <= screen[0] <= WIDTH and 0 <= screen[1] <= HEIGHT:
                                    hits += 1
                            if hits >= 3:
                                best.append((hits, actor, rise, offset, kind, swap, fov, rot))

        best.sort(key=lambda item: (-item[0], abs(item[2])))
        print(f"fits {len(best)}")

        for hits, actor, rise, offset, kind, swap, fov, rot in best[:8]:
            print(
                f"  {hits} eye {actor:#x} rise {rise:.0f} ctrl+{offset:#x} {kind} "
                f"swap {int(swap)} fov {fov:.0f} rot {rot[0]:.1f} {rot[1]:.1f} {rot[2]:.1f}"
            )

    return 0 if best else 4


if __name__ == "__main__":
    raise SystemExit(main())
