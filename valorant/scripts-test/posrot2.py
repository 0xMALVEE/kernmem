"""Fit orientation from the rotation that sits beside the working position.

Position reads correctly as floats at root+0x1B4. RelativeRotation is documented
at 0x188 on the same component, so that window is swept as floats and every
candidate is scored by how many *other* actors it puts on screen. Pitch/yaw sign
flips and a yaw-only variant are included because the handedness is unknown.

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

ROT_FROM = 0x180
ROT_TO = 0x1B0
RISES = (0.0, 30.0, 62.0)
FOVS = (60.0, 70.0, 80.0, 90.0, 103.0)


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

    with game, vgb.Client() as c:
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

        eyes = [(f"{entry:#x}", root, point) for entry, root, point in positioned]

        cam_root = q(c, pid, manager + ROOT) if ok(manager) else 0
        if ok(cam_root):
            try:
                point = struct.unpack("<3f", c.read(pid, cam_root + POSITION, 12))
            except Exception:
                point = None
            if point and sane(point):
                eyes.append(("cam", cam_root, point))

        sources = [(f"{entry:#x}", root) for entry, root, _p in positioned]
        if ok(cam_root):
            sources.append(("cam", cam_root))

        rots = []
        for label, root in sources:
            try:
                raw = c.read(pid, root + ROT_FROM, ROT_TO - ROT_FROM + 12)
            except Exception:
                continue
            for offset in range(0, ROT_TO - ROT_FROM, 4):
                rot = struct.unpack_from("<3f", raw, offset)
                if rot_sane(rot):
                    rots.append((label, ROT_FROM + offset, rot))

        print(f"eyes {len(eyes)} rotations {len(rots)}")
        if not rots:
            return 2

        best = []

        for eye_label, eye_root, eye in eyes:
            others = [p for e, _r, p in positioned if e != eye_label]
            if len(others) < 3:
                continue

            for rot_label, rot_off, rot in rots:
                for variant, applied in (
                    ("pyr", rot),
                    ("py-r", (rot[0], -rot[1], rot[2])),
                    ("yaw", (0.0, rot[1], 0.0)),
                ):
                    for rise in RISES:
                        loc = (eye[0], eye[1], eye[2] + rise)
                        for fov in FOVS:
                            hits = 0
                            for point in others:
                                screen = project(loc, applied, fov, point)
                                if (
                                    screen
                                    and 0 <= screen[0] <= WIDTH
                                    and 0 <= screen[1] <= HEIGHT
                                ):
                                    hits += 1
                            if hits >= 3:
                                best.append(
                                    (
                                        hits, eye_label, eye_root, rot_label,
                                        rot_off, variant, rise, fov, applied,
                                    )
                                )

        best.sort(key=lambda item: (-item[0], abs(item[6])))
        print(f"fits {len(best)}")

        for hits, eye_label, eye_root, rot_label, rot_off, variant, rise, fov, applied in best[:8]:
            print(
                f"  {hits} eye {eye_label} rot {rot_label}+{rot_off:#x} {variant} "
                f"rise {rise:.0f} fov {fov:.0f} "
                f"{applied[0]:.1f} {applied[1]:.1f} {applied[2]:.1f}"
            )

    return 0 if best else 3


if __name__ == "__main__":
    raise SystemExit(main())
