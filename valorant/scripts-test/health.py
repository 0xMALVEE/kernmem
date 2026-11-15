"""Hunt the damage handler pointer on a real actor and test the local candidates.

For each actor, every pointer in the 0xC00-0xD00 window is tested for a float
health at +0x200, +0x1E0 and +0x124 plus a byte alive flag at +0x1F9. The two
controller candidates are then evaluated the same way and their positions
compared with the actor centroid.

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

HEALTHS = (0x200, 0x1E0, 0x124)


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


def u8(c, pid, a):
    try:
        return c.read(pid, a, 1)[0]
    except Exception:
        return -1


def sane(t):
    if any(math.isnan(v) or math.isinf(v) or abs(v) > 300000.0 for v in t):
        return False
    return sum(1 for v in t if abs(v) > 100.0) >= 2


def examine(c, pid, label, pawn):
    root = q(c, pid, pawn + ROOT)
    if not ok(root):
        return None
    try:
        point = struct.unpack("<3d", c.read(pid, root + LOC, 24))
    except Exception:
        return None
    if not sane(point):
        return None

    print(f"{label} pawn {pawn:#x} pos {point[0]:.0f},{point[1]:.0f}")

    try:
        raw = c.read(pid, pawn + 0xC00, 0x100)
    except Exception as error:
        print(f"  read failed: {error}")
        return point

    for offset in range(0, len(raw) - 8, 8):
        pointer = struct.unpack_from("<Q", raw, offset)[0]
        if not ok(pointer):
            continue
        for hoff in HEALTHS:
            hp = f32(c, pid, pointer + hoff)
            if hp is None or not (1.0 < hp < 200.0):
                continue
            print(
                f"  +{0xC00 + offset:#x} -> {pointer:#x} hp@{hoff:#x} {hp:.0f} "
                f"alive {u8(c, pid, pointer + 0x1F9)} team {i32(c, pid, pointer + 0xF8)}"
            )
    return point


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

        for entry, _point in actors[:2]:
            examine(c, pid, "actor", entry)

        print()
        print("[candidates]")
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
                if ok(pawn):
                    examine(c, pid, f"ctrl {candidate:#x}", pawn)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
