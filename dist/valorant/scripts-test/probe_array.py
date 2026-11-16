"""Probe one array slot: entries, roots, positions, health, team, controller link.

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
ROOT = 0x290
POSITIONS = (0x1B4, 0x170)
HANDLER = 0xC70
HEALTH = 0x200
TEAM_COMPONENT = 0x6A0
TEAM_ID = 0xF8
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
        return 0.0


def sane(t):
    if any(math.isnan(v) or math.isinf(v) or abs(v) > 300000.0 for v in t):
        return False
    return sum(1 for v in t if abs(v) > 100.0) >= 2


def position(c, pid, root):
    best = None
    found = None
    used = None
    for offset in POSITIONS:
        for wide in (False, True):
            try:
                if wide:
                    point = struct.unpack("<3d", c.read(pid, root + offset, 24))
                else:
                    point = struct.unpack("<3f", c.read(pid, root + offset, 12))
            except Exception:
                continue
            if not sane(point):
                continue
            magnitude = max(abs(v) for v in point)
            if best is None or magnitude < best:
                best = magnitude
                found = point
                used = (offset, wide)
    return found, used


def main():
    slot = int(sys.argv[1], 16)
    game = reader.Game(allow_stale=True)
    game.open()
    pid = game.pid

    with game, km.Client() as c:
        c.require_version()

        data = q(c, pid, slot)
        num = i32(c, pid, slot + 8)
        print(f"slot {slot:#x} data {data:#x} num {num}")
        if not ok(data):
            return 1

        entries = []
        seen = set()
        for index in range(min(max(num, 0), 64)):
            entry = q(c, pid, data + index * 8)
            if ok(entry) and entry not in seen:
                seen.add(entry)
                entries.append(entry)

        print(f"unique entries {len(entries)}")
        print()

        for entry in entries:
            root = q(c, pid, entry + ROOT)
            point, used = position(c, pid, root) if ok(root) else (None, None)
            handler = q(c, pid, entry + HANDLER)
            hp = f32(c, pid, handler + HEALTH) if ok(handler) else 0.0
            comp = q(c, pid, entry + TEAM_COMPONENT)
            team = i32(c, pid, comp + TEAM_ID) if ok(comp) else -1
            where = f"{point[0]:.1f},{point[1]:.1f}" if point else "-"
            print(
                f"  {entry:#x} root {root:#x} pos {where} "
                f"({used}) hp {hp:.0f} team {team}"
            )

        print()
        print("[controller links]")
        found = 0
        base = game.base
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
                if pawn in seen:
                    print(f"  ctrl {candidate:#x} -> pawn {pawn:#x}")
                    found += 1
                    if found >= 3:
                        return 0

        print(f"  none ({found})")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
