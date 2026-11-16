"""Probe one world slot with offset variants for the actor array and game instance.

Reads only. Prints evidence.
"""

from __future__ import annotations

import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "scripts"))
sys.path.insert(0, str(Path(__file__).resolve().parent.parent.parent / "sdk"))

import km  # noqa: E402

import reader  # noqa: E402

LO = 0x1000000000
HI = 0x7FFFFFFFFFFF

ARRAY_OFFSETS = (0x98, 0xA0, 0xA8, 0xB0, 0xB8, 0xC0)
INSTANCE_OFFSETS = (0x1C8, 0x1D0, 0x1D8, 0x1E0, 0x1E8)


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
    slot = int(sys.argv[1], 16) if len(sys.argv) > 1 else 0xDB74B40

    game = reader.Game(allow_stale=True)
    game.open()
    pid = game.pid
    base = game.base

    with game, km.Client() as c:
        c.require_version()

        world = q(c, pid, base + slot)
        print(f"slot {slot:#x} -> world {world:#x}")
        if not ok(world):
            return 1

        level = q(c, pid, world + 0x38)
        print(f"level {level:#x} {'ptr' if ok(level) else 'NOT ptr'}")

        if ok(level):
            print("\n[array variants on level]")
            for offset in ARRAY_OFFSETS:
                data = q(c, pid, level + offset)
                count = i32(c, pid, level + offset + 8)
                print(f"  +{offset:#x} data {data:#x} num {count}")

        print("\n[instance variants on world]")
        for offset in INSTANCE_OFFSETS:
            instance = q(c, pid, world + offset)
            note = ""
            if ok(instance):
                players = q(c, pid, instance + 0x40)
                controller = q(c, pid, players + 0x38) if ok(players) else 0
                pawn = q(c, pid, controller + 0x518) if ok(controller) else 0
                note = f"players {players:#x} controller {controller:#x} pawn {pawn:#x}"
            print(f"  +{offset:#x} instance {instance:#x} {note}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
