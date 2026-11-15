"""Scan the image for a live UWorld by back-reference, and dump the world slots.

A slot value is only accepted when it dereferences to a world whose
PersistentLevel points back at that world through OwningWorld. Nothing writes.
"""

from __future__ import annotations

import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "scripts"))
sys.path.insert(0, str(Path(__file__).resolve().parent.parent.parent / "sdk"))

import reader  # noqa: E402

LO = 0x1000000000
HI = 0x7FFFFFFFFFFF
CAP = 8000

SLOTS = (0xB490930, 0xB424930)


def q(game, address):
    if not address:
        return 0
    try:
        return struct.unpack("<Q", game.read(address, 8))[0]
    except Exception:
        return 0


def is_ptr(value):
    return LO <= value <= HI and value % 8 == 0


def main():
    game = reader.Game(allow_stale=True)
    try:
        game.open()
    except Exception as error:
        print(f"open failed: {error}")
        return 1

    with game:
        base = game.base
        print(f"pid {game.pid} base {base:#x} patch {game.patch}")

        for slot in SLOTS:
            print(f"--- around {slot:#x} ---")
            try:
                raw = game.read(base + slot - 0x20, 0x40)
            except Exception as error:
                print(f"  read failed: {error}")
                continue

            for index in range(0, len(raw), 8):
                value = struct.unpack_from("<Q", raw, index)[0]
                star = "*" if index == 0x20 else " "
                mark = "ptr" if is_ptr(value) else ""
                print(f" {star} {base + slot - 0x20 + index:#x} {value:#018x} {mark}")

        print("--- scan .data ---")
        tested = 0
        hits = 0

        for delta in range(0x9000000, 0xC000000, 0x1000):
            try:
                raw = game.read(base + delta, 0x1000)
            except Exception:
                continue

            for index in range(0, 0x1000, 8):
                first = struct.unpack_from("<Q", raw, index)[0]
                if not is_ptr(first):
                    continue

                tested += 1
                if tested > CAP:
                    break

                world = q(game, first)
                if not is_ptr(world):
                    continue

                level = q(game, world + 0x0038)
                if not level or level == world:
                    continue

                if q(game, level + 0x00C0) != world:
                    continue

                print(f"HIT slot {base + delta + index:#x} world {world:#x} level {level:#x}")
                hits += 1

            if tested > CAP:
                break

        print(f"tested {tested} hits {hits}")

    return 0 if hits else 2


if __name__ == "__main__":
    raise SystemExit(main())
