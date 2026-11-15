"""Find the camera by testing GameInstance candidates for a full controller chain.

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

WORLD_SLOTS = (0xDB74B40, 0xD82CC70, 0xD847A40, 0xDA4C9C0)


def ok(v):
    return LO <= v <= HI and v % 8 == 0


def q(c, pid, a):
    if not a:
        return 0
    try:
        return struct.unpack("<Q", c.read(pid, a, 8))[0]
    except Exception:
        return 0


def main():
    game = reader.Game(allow_stale=True)
    game.open()
    pid = game.pid
    base = game.base

    with game, vgb.Client() as c:
        c.require_version()
        print(f"pid {pid} base {base:#x}")

        for slot in WORLD_SLOTS:
            world = q(c, pid, base + slot)
            print(f"\nslot {slot:#x} -> {world:#x}")
            if not ok(world):
                continue

            for off in range(0x1A0, 0x260, 8):
                instance = q(c, pid, world + off)
                if not ok(instance):
                    continue
                players = q(c, pid, instance + 0x40)
                if not ok(players):
                    continue
                controller = q(c, pid, players + 0x38)
                if not ok(controller):
                    continue
                manager = q(c, pid, controller + 0x528)
                view = q(c, pid, manager + 0x17C0) if ok(manager) else 0
                print(
                    f"  world+{off:#x} instance {instance:#x} players {players:#x} "
                    f"controller {controller:#x} cam {manager:#x} view {view:#x}"
                )
                if ok(view):
                    try:
                        location = struct.unpack("<3d", c.read(pid, view + 0x10, 24))
                        rotation = struct.unpack("<3d", c.read(pid, view + 0x28, 24))
                        fov = struct.unpack("<f", c.read(pid, view + 0x40, 4))[0]
                        print(f"    loc {location} rot {rotation} fov {fov}")
                    except Exception as error:
                        print(f"    cam read failed: {error}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
