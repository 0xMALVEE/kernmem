"""Find the damage handler and team fields on a real actor, and resolve the local pawn.

For each actor with a valid position, scan its own pointer fields for a target
whose +0x200 reads a plausible health float, and whose +0xF8 reads a small team
id. Separately scan for a controller whose 0x4B0 points at a pawn with a valid
root component.

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
ARRAY = 0xD81F3B0
ROOT = 0x290
LOC = 0x170
PAWN = 0x4B0


def ok(v):
    return LO <= v <= HI and v % 8 == 0


def q(c, pid, a):
    try:
        return struct.unpack("<Q", c.read(pid, a, 8))[0]
    except Exception:
        return 0


def i32(c, pid, a):
    try:
        return struct.unpack("<i", c.read(pid, a, 4))[0]
    except Exception:
        return 0


def f32(c, pid, a):
    try:
        return struct.unpack("<f", c.read(pid, a, 4))[0]
    except Exception:
        return None


def sane(t):
    if any(math.isnan(v) or math.isinf(v) or abs(v) > 300000.0 for v in t):
        return False
    return sum(1 for v in t if abs(v) > 100.0) >= 2


def main():
    game = reader.Game(allow_stale=True)
    game.open()
    pid = game.pid
    base = game.base

    with game, vgb.Client() as c:
        c.require_version()

        slot = base + ARRAY
        data = q(c, pid, slot)
        num = i32(c, pid, slot + 8)

        actors = []
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
                point = struct.unpack("<3d", c.read(pid, root + LOC, 24))
            except Exception:
                continue
            if sane(point):
                actors.append((entry, point))

        print(f"actors {len(actors)}")

        for entry, point in actors[:3]:
            print(f"\n{entry:#x} pos {point[0]:.0f},{point[1]:.0f}")
            try:
                raw = c.read(pid, entry + 0x400, 0x900)
            except Exception as error:
                print(f"  read failed: {error}")
                continue
            for offset in range(0, len(raw) - 8, 8):
                pointer = struct.unpack_from("<Q", raw, offset)[0]
                if not ok(pointer):
                    continue
                hp = f32(c, pid, pointer + 0x200)
                if hp is None or not (1.0 < hp < 200.0):
                    continue
                team = i32(c, pid, pointer + 0xF8)
                alive = None
                try:
                    alive = c.read(pid, pointer + 0x1F9, 1)[0]
                except Exception:
                    alive = -1
                print(
                    f"  +{0x400 + offset:#x} -> {pointer:#x} "
                    f"hp {hp:.0f} team {team} alive {alive}"
                )

        print("\n[local pawn candidates]")
        for delta in range(0x9000000, 0xE200000, 0x1000):
            try:
                raw = c.read(pid, base + delta, 0x1000)
            except Exception:
                continue
            for index in range(0, 0x1000, 8):
                candidate = struct.unpack_from("<Q", raw, index)[0]
                if not ok(candidate):
                    continue
                pawn = q(c, pid, candidate + PAWN)
                if not ok(pawn):
                    continue
                root = q(c, pid, pawn + ROOT)
                if not ok(root):
                    continue
                try:
                    point = struct.unpack("<3d", c.read(pid, root + LOC, 24))
                except Exception:
                    continue
                if not sane(point):
                    continue
                print(
                    f"  ctrl {candidate:#x} pawn {pawn:#x} "
                    f"pos {point[0]:.0f},{point[1]:.0f}"
                )

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
