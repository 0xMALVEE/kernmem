"""Find whichever object actually owns a ViewInfo, and verify it by projection.

manager+0x17C0 was null on the object held at controller+0x528, so that hop no
longer reaches a camera manager. Candidate owners are tried instead: the
controller, the pawn, the object at 0x528, and every heap pointer inside the
controller and pawn. Each owner is tested at 0x17C0 / 0x17B0 / 0x17D0, raw and
dereferenced. A candidate is only reported when it puts two actors on screen.

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
PAWN = 0x4B0
CAMERA = 0x528

WIDTH = 1920
HEIGHT = 1080

SLOTS = (0x17C0, 0x17B0, 0x17D0)
POINTER_WINDOW = 0x900


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


def try_block(c, pid, label, pov, points):
    best = 0

    for kind in ("d", "f"):
        try:
            raw = c.read(pid, pov, 0x40)
        except Exception:
            continue

        if kind == "d":
            loc = struct.unpack_from("<3d", raw, 0x00)
            rot = struct.unpack_from("<3d", raw, 0x18)
            fov = struct.unpack_from("<f", raw, 0x30)[0]
        else:
            loc = struct.unpack_from("<3f", raw, 0x00)
            rot = struct.unpack_from("<3f", raw, 0x0C)
            fov = struct.unpack_from("<f", raw, 0x28)[0]

        if not sane(loc):
            continue
        if any(math.isnan(v) or math.isinf(v) or abs(v) > 400.0 for v in rot):
            continue
        if not (40.0 < fov < 140.0):
            continue

        hits = 0
        for point in points:
            screen = project(loc, rot, fov, point)
            if screen and 0 <= screen[0] <= WIDTH and 0 <= screen[1] <= HEIGHT:
                hits += 1

        print(
            f"  {label} {kind} pov {pov:#x} loc {loc[0]:.1f} {loc[1]:.1f} {loc[2]:.1f} "
            f"rot {rot[0]:.1f} {rot[1]:.1f} {rot[2]:.1f} fov {fov:.1f} hits {hits}"
        )
        best = max(best, hits)

    return best


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

        print(f"reference points {len(points)}")
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
                if ok(candidate) and q(c, pid, candidate + PAWN) in seen:
                    controller = candidate
                    break
            if controller:
                break

        if not controller:
            print("no controller")
            return 2

        pawn = q(c, pid, controller + PAWN)
        held = q(c, pid, controller + CAMERA)
        print(f"controller {controller:#x} pawn {pawn:#x} held {held:#x}")

        owners = [("controller", controller), ("pawn", pawn), ("held", held)]

        for label, owner in (("controller", controller), ("pawn", pawn)):
            if not ok(owner):
                continue
            try:
                raw = c.read(pid, owner, POINTER_WINDOW)
            except Exception:
                continue
            for offset in range(0x80, len(raw), 8):
                pointer = struct.unpack_from("<Q", raw, offset)[0]
                if ok(pointer) and pointer not in (controller, pawn):
                    owners.append((f"{label}+{offset:#x}", pointer))

        print(f"candidate owners {len(owners)}")

        best = 0

        for label, owner in owners:
            if not ok(owner):
                continue
            for slot_off in SLOTS:
                raw_slot = owner + slot_off
                best = max(
                    best, try_block(c, pid, f"{label}+{slot_off:#x}", raw_slot, points)
                )
                pointer = q(c, pid, raw_slot)
                if ok(pointer):
                    best = max(
                        best,
                        try_block(c, pid, f"{label}+{slot_off:#x}->", pointer, points),
                    )

        print(f"\nbest hits {best}")

    return 0 if best >= 2 else 3


if __name__ == "__main__":
    raise SystemExit(main())
