"""Dump the local pawn's position candidates so the huge distance is explained.

Reads only.
"""

from __future__ import annotations

import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "scripts"))
sys.path.insert(0, str(Path(__file__).resolve().parent.parent.parent / "sdk"))

import reader  # noqa: E402


def main():
    game = reader.Game(allow_stale=True)
    game.open()

    with game:
        local = game.local()
        actors = {a.address for a in game.actors()}

        print(f"local {local.address:#x}" if local else "local unresolved")
        if local is None:
            return 1

        print(f"in array: {local.address in actors}")
        print(f"stored position: {local.position}")

        root = game.u64(local.address + 0x290)
        print(f"root {root:#x}")

        for offset in (0x170, 0x1B4):
            for wide in (True, False):
                try:
                    if wide:
                        value = struct.unpack("<3d", game.read(root + offset, 24))
                        kind = "d"
                    else:
                        value = struct.unpack("<3f", game.read(root + offset, 12))
                        kind = "f"
                except Exception as error:
                    value = error
                    kind = "?"
                print(f"  +{offset:#x} {kind} {value}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
