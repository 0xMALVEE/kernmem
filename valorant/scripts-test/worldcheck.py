"""Resolve UWorld the way VEX does, then walk it and report honestly.

GWorld holds a pointer *to* the world pointer. A slot is only accepted when the
double deref lands on something whose PersistentLevel points back at it through
OwningWorld. Nothing here writes.
"""

from __future__ import annotations

import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "scripts"))
sys.path.insert(0, str(Path(__file__).resolve().parent.parent.parent / "sdk"))

import km  # noqa: E402

import reader  # noqa: E402

SLOTS = (0xB490930, 0xB424930)


def q(game, address):
    if not address:
        return 0
    try:
        return struct.unpack("<Q", game.read(address, 8))[0]
    except Exception:
        return 0


def resolve(game):
    base = game.base
    for slot in SLOTS:
        first = q(game, base + slot)
        if not first:
            print(f"slot {slot:#x}: first deref 0")
            continue

        world = q(game, first)
        if not world:
            print(f"slot {slot:#x}: first {first:#x} -> second deref 0")
            continue

        level = q(game, world + 0x0038)
        back = q(game, level + 0x00C0) if level else 0
        ok = bool(level) and level != world and back == world
        print(
            f"slot {slot:#x}: first {first:#x} world {world:#x} "
            f"level {level:#x} back {back:#x} {'VALID' if ok else 'invalid'}"
        )

        if ok:
            return world, level

    return 0, 0


def walk(game, world, level):
    data = q(game, level + 0x00A0)
    try:
        count = struct.unpack("<i", game.read(level + 0x00A8, 4))[0]
    except Exception:
        count = 0
    print(f"ActorArray {data:#x} num {count}")

    for index in range(min(max(count, 0), 10)):
        actor = q(game, data + index * 8)
        if not actor:
            continue
        root = q(game, actor + 0x0288)
        try:
            pos = struct.unpack("<3d", game.read(root + 0x0170, 24))
        except Exception:
            pos = (0.0, 0.0, 0.0)
        team = q(game, actor + 0x00E8)
        handler = q(game, actor + 0x0C50)
        try:
            health = struct.unpack("<f", game.read(handler + 0x01E0, 4))[0]
        except Exception:
            health = 0.0
        print(f"  actor[{index}] {actor:#x} team {team} pos {pos} hp {health:.0f}")

    instance = q(game, world + 0x01D8)
    players = q(game, instance + 0x0038) if instance else 0
    controller = q(game, players + 0x0038) if players else 0
    pawn = q(game, controller + 0x0510) if controller else 0
    manager = q(game, controller + 0x0520) if controller else 0
    cache = q(game, manager + 0x17B0) if manager else 0

    print(f"instance {instance:#x} players {players:#x} controller {controller:#x}")
    print(f"pawn {pawn:#x} camera {manager:#x} cache {cache:#x}")

    if cache:
        try:
            print(f"  loc {struct.unpack('<3d', game.read(cache + 0x10, 24))}")
            print(f"  fov {struct.unpack('<f', game.read(cache + 0x40, 4))[0]}")
        except Exception as error:
            print(f"  camera read failed: {error}")


def main():
    game = reader.Game(allow_stale=True)
    try:
        game.open()
    except Exception as error:
        print(f"open failed: {error}")
        return 1

    with game:
        print(f"pid {game.pid} base {game.base:#x}")
        world, level = resolve(game)
        if not world:
            print("no valid world")
            return 2
        walk(game, world, level)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
