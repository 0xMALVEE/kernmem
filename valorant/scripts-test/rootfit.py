"""Fit orientation from the root component's own rotation.

Per the 11.14 dump, RelativeLocation is 0x170 and RelativeRotation is 0x188 on
USceneComponent. Both are tried as doubles and as floats, each positioned actor
is tried as the eye plus vertical rises, and every combination is scored by how
many *other* actors it puts on screen.

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
LOCATION = 0x170
ROTATION = 0x188

WIDTH = 1920
HEIGHT = 1080

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


def rot_sane(t):
    if any(math.isnan(v) or math.isinf(v) or abs(v) > 400.0 for v in t):
        return False
    return max(abs(v) for v in t) >= 1.0


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

        rows = []
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
                loc_d = struct.unpack("<3d", c.read(pid, root + LOCATION, 24))
            except Exception:
                loc_d = None
            try:
                loc_f = struct.unpack("<3f", c.read(pid, root + LOCATION, 12))
            except Exception:
                loc_f = None
            try:
                rot_d = struct.unpack("<3d", c.read(pid, root + ROTATION, 24))
            except Exception:
                rot_d = None
            try:
                rot_f = struct.unpack("<3f", c.read(pid, root + ROTATION, 12))
            except Exception:
                rot_f = None

            rows.append((entry, root, loc_d, loc_f, rot_d, rot_f))

        print(f"entries {len(rows)}")
        for entry, root, ld, lf, rd, rf in rows:
            print(f"  {entry:#x} loc_d {ld} loc_f {lf}")
            print(f"       rot_d {rd} rot_f {rf}")

        eyes = [(e, ld, "d") for e, r, ld, lf, rd, rf in rows if ld and sane(ld)]
        eyes += [(e, lf, "f") for e, r, ld, lf, rd, rf in rows if lf and sane(lf)]

        rots = [(e, rd, "d") for e, r, ld, lf, rd, rf in rows if rd and rot_sane(rd)]
        rots += [(e, rf, "f") for e, r, ld, lf, rd, rf in rows if rf and rot_sane(rf)]

        print(f"\neyes {len(eyes)} rotations {len(rots)}")
        if not eyes or not rots:
            return 1

        best = []

        for eye_actor, eye, eye_kind in eyes:
            others = []
            for e, r, ld, lf, rd, rf in rows:
                if e == eye_actor:
                    continue
                point = ld if eye_kind == "d" else lf
                if point and sane(point):
                    others.append(point)
            if len(others) < 3:
                continue

            for rot_actor, rot, rot_kind in rots:
                for rise in RISES:
                    loc = (eye[0], eye[1], eye[2] + rise)
                    for fov in FOVS:
                        hits = 0
                        for point in others:
                            screen = project(loc, rot, fov, point)
                            if screen and 0 <= screen[0] <= WIDTH and 0 <= screen[1] <= HEIGHT:
                                hits += 1
                        if hits >= 3:
                            best.append(
                                (hits, eye_actor, rot_actor, eye_kind, rot_kind, rise, fov, rot)
                            )

        best.sort(key=lambda item: (-item[0], abs(item[5])))
        print(f"fits {len(best)}")

        for hits, ea, ra, ek, rk, rise, fov, rot in best[:8]:
            print(
                f"  {hits} eye {ea:#x}({ek}) rot {ra:#x}({rk}) "
                f"rise {rise:.0f} fov {fov:.0f} {rot[0]:.1f} {rot[1]:.1f} {rot[2]:.1f}"
            )

    return 0 if best else 2


if __name__ == "__main__":
    raise SystemExit(main())
