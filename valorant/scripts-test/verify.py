"""Decisive check: level count offsets, camera chain, and position values.

Reads only.
"""

from __future__ import annotations

import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "scripts"))
sys.path.insert(0, str(Path(__file__).resolve().parent.parent.parent / "sdk"))

import vgb  # noqa: E402

import reader  # noqa: E402

LO = 0x1000000000
HI = 0x7FFFFFFFFFFF

WORLD_SLOT = 0xDB74B40
ARRAY_SLOT = 0xD91FDD0


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


def main():
    game = reader.Game(allow_stale=True)
    game.open()
    pid = game.pid
    base = game.base

    with game, vgb.Client() as c:
        c.require_version()
        print(f"pid {pid} base {base:#x} patch {game.patch}")

        world = q(c, pid, base + WORLD_SLOT)
        print(f"world {world:#x}")

        if ok(world):
            level = q(c, pid, world + 0x38)
            print(f"level {level:#x}")

            if ok(level):
                print("[count offsets on level]")
                for offset in range(0x98, 0xC8, 8):
                    data = q(c, pid, level + offset)
                    count = i32(c, pid, level + offset + 8)
                    print(f"  +{offset:#x} data {data:#x} num {count}")

            print("[camera chain]")
            instance = q(c, pid, world + 0x1D8)
            players = q(c, pid, instance + 0x40) if ok(instance) else 0
            controller = q(c, pid, players + 0x38) if ok(players) else 0
            pawn = q(c, pid, controller + 0x518) if ok(controller) else 0
            manager = q(c, pid, controller + 0x528) if ok(controller) else 0
            print(
                f"  instance {instance:#x} players {players:#x} "
                f"controller {controller:#x} pawn {pawn:#x} cam {manager:#x}"
            )

            if ok(manager):
                view = q(c, pid, manager + 0x17C0)
                print(f"  view {view:#x}")
                if ok(view):
                    try:
                        location = struct.unpack("<3d", c.read(pid, view + 0x10, 24))
                        fov = struct.unpack("<f", c.read(pid, view + 0x40, 4))[0]
                        print(f"  loc {location} fov {fov}")
                    except Exception as error:
                        print(f"  cam read failed: {error}")

        print("[positions at root+0x1b4]")
        slot = base + ARRAY_SLOT
        data = q(c, pid, slot)
        count = i32(c, pid, slot + 8)
        print(f"  slot {slot:#x} data {data:#x} num {count}")

        distinct = []

        for index in range(min(max(count, 0), 32)):
            entry = q(c, pid, data + index * 8)
            if not ok(entry):
                continue
            root = q(c, pid, entry + 0x290)
            if not ok(root):
                continue
            try:
                triple = struct.unpack("<3f", c.read(pid, root + 0x1B4, 12))
            except Exception:
                continue
            print(f"   [{index}] {entry:#x} root {root:#x} {triple[0]:.2f} {triple[1]:.2f} {triple[2]:.2f}")
            key = tuple(round(v, 1) for v in triple)
            if key not in distinct:
                distinct.append(key)

        print(f"distinct {len(distinct)}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
