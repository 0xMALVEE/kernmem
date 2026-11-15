"""Sweep controller offsets for one that points at a real array actor.

The pawn field is shifted on this build like RelativeLocation was, so every
aligned offset in a window is tested and any pointer whose target is an actor
from the working array is reported.

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

ARRAY = 0xD81F3B0
ROOT = 0x290
LOC = 0x170
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

        actors = set()
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
                actors.add(entry)

        print(f"actors {len(actors)}")
        if not actors:
            return 1

        hits = {}

        for delta in range(0x9000000, 0xE200000, 0x1000):
            try:
                raw = c.read(pid, base + delta, 0x1000)
            except Exception:
                continue

            for index in range(0, 0x1000, 8):
                candidate = struct.unpack_from("<Q", raw, index)[0]
                if not ok(candidate):
                    continue

                for offset in range(0x480, 0x5A0, 8):
                    pawn = q(c, pid, candidate + offset)
                    if pawn in actors:
                        hits.setdefault(offset, []).append((candidate, pawn))

        for offset in sorted(hits):
            found = hits[offset]
            print(f"  +{offset:#x} hits {len(found)}")
            for candidate, pawn in found[:2]:
                print(f"      ctrl {candidate:#x} -> actor {pawn:#x}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
