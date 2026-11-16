"""Find the damage handler and the local pawn, scanning wide.

DamageHandler is shifted on this build like RelativeLocation was, so the whole
actor is scanned for a pointer whose +0x200 is a plausible health float. The
three documented pawn offsets (0x4B0, 0x510, 0x518) are each tested for a
controller whose target is an actor in the array.

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
ARRAY = 0xD81F3B0
ROOT = 0x290
LOC = 0x170
PAWN_OFFSETS = (0x4B0, 0x510, 0x518)
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


def actors(c, pid, base):
    slot = base + ARRAY
    data = q(c, pid, slot)
    num = i32(c, pid, slot + 8)
    out = []
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
            out.append(entry)
    return out, seen


def main():
    game = reader.Game(allow_stale=True)
    game.open()
    pid = game.pid
    base = game.base

    with game, km.Client() as c:
        c.require_version()
        entries, seen = actors(c, pid, base)
        print(f"actors {len(entries)}")

        for entry in entries[:2]:
            print(f"\n[handler hunt on {entry:#x}]")
            for start in range(0x400, 0x1400, 0x200):
                try:
                    raw = c.read(pid, entry + start, 0x200)
                except Exception:
                    continue
                for offset in range(0, len(raw) - 8, 8):
                    pointer = struct.unpack_from("<Q", raw, offset)[0]
                    if not ok(pointer):
                        continue
                    for hoff in HEALTHS:
                        hp = f32(c, pid, pointer + hoff)
                        if hp is None or not (1.0 < hp < 200.0):
                            continue
                        print(
                            f"  +{start + offset:#x} -> {pointer:#x} "
                            f"hp@{hoff:#x} {hp:.0f} alive {u8(c, pid, pointer + 0x1F9)} "
                            f"team {i32(c, pid, pointer + 0xF8)}"
                        )

        print("\n[pawn links]")
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
                        if hits >= 2:
                            break
                if hits >= 2:
                    break
            print(f"  +{offset:#x} hits {hits}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
