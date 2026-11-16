"""Score world candidates and walk the best one with the current-build offsets.

Reads only. Prints evidence.
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

LEVEL = 0x38
ARRAY = 0xA0
COUNT = 0xA8
ROOT = 0x290
POSITION = 0x170
HANDLER = 0xC70
HEALTH = 0x200
TEAM_COMPONENT = 0x6A8
TEAM_ID = 0xF8
DORMANT = 0x225
INSTANCE = 0x1D8


def ok(value):
    return LO <= value <= HI and value % 8 == 0


def q(c, pid, address):
    if not address:
        return 0
    try:
        return struct.unpack("<Q", c.read(pid, address, 8))[0]
    except Exception:
        return 0


def i32(c, pid, address):
    try:
        return struct.unpack("<i", c.read(pid, address, 4))[0]
    except Exception:
        return 0


def f32(c, pid, address):
    try:
        return struct.unpack("<f", c.read(pid, address, 4))[0]
    except Exception:
        return 0.0


def u8(c, pid, address):
    try:
        return c.read(pid, address, 1)[0]
    except Exception:
        return 0


def vec3(c, pid, address):
    try:
        return struct.unpack("<3d", c.read(pid, address, 24))
    except Exception:
        return (0.0, 0.0, 0.0)


def plausible(position):
    if any(math.isnan(v) or math.isinf(v) for v in position):
        return False
    if position == (0.0, 0.0, 0.0):
        return False
    return all(abs(v) < 200000.0 for v in position)


def score(c, pid, level, count):
    data = q(c, pid, level + ARRAY)
    if not ok(data):
        return 0, 0

    good = 0
    sample = min(count, 12)

    for index in range(sample):
        actor = q(c, pid, data + index * 8)
        if not ok(actor):
            continue
        if u8(c, pid, actor + DORMANT):
            continue
        root = q(c, pid, actor + ROOT)
        if not ok(root):
            continue
        if not plausible(vec3(c, pid, root + POSITION)):
            continue
        health = f32(c, pid, q(c, pid, actor + HANDLER) + HEALTH) if ok(q(c, pid, actor + HANDLER)) else 0.0
        if 0.0 < health <= 200.0:
            good += 1

    return good, sample


def main():
    game = reader.Game(allow_stale=True)
    game.open()
    pid = game.pid
    base = game.base

    with game, km.Client() as c:
        c.require_version()
        print(f"pid {pid} base {base:#x} patch {game.patch}")

        found = []

        for delta in range(0xD700000, 0xDD00000, 0x1000):
            try:
                raw = c.read(pid, base + delta, 0x1000)
            except Exception:
                continue

            for index in range(0, 0x1000, 8):
                world = struct.unpack_from("<Q", raw, index)[0]
                if not ok(world):
                    continue

                level = q(c, pid, world + LEVEL)
                if not ok(level) or level == world:
                    continue

                count = i32(c, pid, level + COUNT)
                if not (0 < count < 3000):
                    continue

                good, sample = score(c, pid, level, count)
                if good:
                    found.append((good, count, base + delta + index, world, level))

        found.sort(reverse=True)
        print(f"\n[top candidates] {len(found)} scored")

        for good, count, slot, world, level in found[:5]:
            print(f"  good {good} num {count} slot {slot:#x} world {world:#x} level {level:#x}")

        if not found:
            return 2

        _, count, slot, world, level = found[0]
        print(f"\n[walk {world:#x}]")

        data = q(c, pid, level + ARRAY)

        for index in range(min(count, 15)):
            actor = q(c, pid, data + index * 8)
            if not ok(actor):
                continue
            dormant = u8(c, pid, actor + DORMANT)
            root = q(c, pid, actor + ROOT)
            position = vec3(c, pid, root + POSITION) if ok(root) else None
            handler = q(c, pid, actor + HANDLER)
            health = f32(c, pid, handler + HEALTH) if ok(handler) else 0.0
            component = q(c, pid, actor + TEAM_COMPONENT)
            team = i32(c, pid, component + TEAM_ID) if ok(component) else -1
            print(
                f"  [{index}] {actor:#x} dormant {dormant} team {team} hp {health:.0f} pos {position}"
            )

        print(f"\n[instance on {world:#x}]")
        for offset in (INSTANCE, 0x1C8, 0x1E0, 0x1F0, 0x200):
            instance = q(c, pid, world + offset)
            note = ""
            if ok(instance):
                players = q(c, pid, instance + 0x40)
                controller = q(c, pid, players + 0x38) if ok(players) else 0
                pawn = q(c, pid, controller + 0x518) if ok(controller) else 0
                manager = q(c, pid, controller + 0x528) if ok(controller) else 0
                note = f"players {players:#x} controller {controller:#x} pawn {pawn:#x} cam {manager:#x}"
                if ok(manager):
                    view = q(c, pid, manager + 0x17C0)
                    location = vec3(c, pid, view + 0x10) if ok(view) else None
                    fov = f32(c, pid, view + 0x40) if ok(view) else 0.0
                    note += f" view {view:#x} loc {location} fov {fov:.1f}"
            print(f"  +{offset:#x} instance {instance:#x} {note}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
