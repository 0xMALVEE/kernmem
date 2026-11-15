"""Find the world whose level actually holds actors, and locate GameInstance by pattern.

Reads only. Prints evidence, no conclusions.
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
        print(f"pid {pid} base {base:#x}")

        print("\n[world slots with actors]")
        best = 0

        for delta in range(0xD700000, 0xDD00000, 0x1000):
            try:
                raw = c.read(pid, base + delta, 0x1000)
            except Exception:
                continue

            for index in range(0, 0x1000, 8):
                value = struct.unpack_from("<Q", raw, index)[0]
                if not ok(value):
                    continue

                level = q(c, pid, value + 0x38)
                if not ok(level) or level == value:
                    continue

                count = i32(c, pid, level + 0xA8)
                data = q(c, pid, level + 0xA0)

                if ok(data) and 0 < count < 2000:
                    slot = base + delta + index
                    print(f"  slot {slot:#x} world {value:#x} level {level:#x} num {count} data {data:#x}")
                    if not best:
                        best = value

        if not best:
            print("  none")
            return 2

        print(f"\n[walk best {best:#x}]")
        level = q(c, pid, best + 0x38)
        data = q(c, pid, level + 0xA0)
        count = i32(c, pid, level + 0xA8)

        for index in range(min(count, 8)):
            actor = q(c, pid, data + index * 8)
            if not actor:
                continue
            root = q(c, pid, actor + 0x290)
            try:
                pos = struct.unpack("<3d", c.read(pid, root + 0x170, 24)) if ok(root) else None
            except Exception:
                pos = None
            handler = q(c, pid, actor + 0xC70)
            try:
                hp = struct.unpack("<f", c.read(pid, handler + 0x1E0, 4))[0] if ok(handler) else 0.0
            except Exception:
                hp = 0.0
            print(f"  actor[{index}] {actor:#x} root {root:#x} pos {pos} hp {hp:.0f}")

        print(f"\n[GameInstance search on {best:#x}]")
        try:
            raw = c.read(pid, best, 0x800)
        except Exception as error:
            print(f"  read failed: {error}")
            return 3

        for index in range(0, 0x800, 8):
            first = struct.unpack_from("<Q", raw, index)[0]
            if not ok(first):
                continue
            players = q(c, pid, first + 0x40)
            if not ok(players):
                continue
            controller = q(c, pid, players + 0x38)
            if not ok(controller):
                continue
            pawn = q(c, pid, controller + 0x518)
            manager = q(c, pid, controller + 0x528)
            print(
                f"  world+{index:#x} instance {first:#x} players {players:#x} "
                f"controller {controller:#x} pawn {pawn:#x} cam {manager:#x}"
            )

            if ok(manager):
                view = q(c, pid, manager + 0x17C0)
                try:
                    loc = struct.unpack("<3d", c.read(pid, view + 0x10, 24))
                    fov = struct.unpack("<f", c.read(pid, view + 0x40, 4))[0]
                    print(f"    view {view:#x} loc {loc} fov {fov}")
                except Exception as error:
                    print(f"    view fail: {error}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
