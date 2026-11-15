"""Look for a controller whose pawn is an actor from the array. Reads only."""

from __future__ import annotations

import math
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "scripts"))
sys.path.insert(0, str(Path(__file__).resolve().parent.parent.parent / "sdk"))

import vgb  # noqa: E402

import reader  # noqa: E402

ARRAY = 0xD81F3B0
ROOT = 0x290
LOC = 0x170
PAWN_OFFSETS = (0x518, 0x4B0, 0x510)
LO = 0x1000000000
HI = 0x7FFFFFFFFFFF


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
        print(f"array {data:#x} num {num}")

        seen = set()
        for index in range(min(max(num, 0), 64)):
            entry = q(c, pid, data + index * 8)
            if not ok(entry):
                continue
            root = q(c, pid, entry + ROOT)
            if not ok(root):
                continue
            try:
                point = struct.unpack("<3d", c.read(pid, root + LOC, 24))
            except Exception:
                continue
            if sane(point):
                seen.add(entry)
                print(f"  actor {entry:#x} {point[0]:.0f},{point[1]:.0f}")

        print(f"actors {len(seen)}")

        for offset in PAWN_OFFSETS:
            hits = 0
            for delta in range(0x9000000, 0xE200000, 0x1000):
                try:
                    raw = c.read(pid, base + delta, 0x1000)
                except Exception:
                    continue
                for index in range(0, 0x1000, 8):
                    candidate = struct.unpack_from("<Q", raw, index)[0]
                    if not ok(candidate):
                        continue
                    pawn = q(c, pid, candidate + offset)
                    if pawn in seen:
                        print(f"  +{offset:#x} ctrl {candidate:#x} -> actor {pawn:#x}")
                        hits += 1
                        if hits >= 3:
                            break
                if hits >= 3:
                    break
            print(f"  +{offset:#x} hits {hits}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
