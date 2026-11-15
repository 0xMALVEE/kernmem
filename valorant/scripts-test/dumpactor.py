"""Dump raw memory around candidate actors so the real coordinate offset is visible.

Prints each word plus its double interpretation. Reads only.
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


def main():
    data = int(sys.argv[1], 16) if len(sys.argv) > 1 else 0x7FF71D032790
    count = int(sys.argv[2]) if len(sys.argv) > 2 else 5

    game = reader.Game(allow_stale=True)
    game.open()

    with game, vgb.Client() as c:
        c.require_version()
        pid = game.pid

        for index in range(count):
            try:
                actor = struct.unpack("<Q", c.read(pid, data + index * 8, 8))[0]
            except Exception as error:
                print(f"actor[{index}] read fail: {error}")
                continue

            print(f"actor[{index}] {actor:#x}")
            if not ok(actor):
                print("  not a pointer")
                continue

            try:
                root = struct.unpack("<Q", c.read(pid, actor + 0x290, 8))[0]
            except Exception as error:
                print(f"  root read fail: {error}")
                continue

            print(f"  root {root:#x}")
            if not ok(root):
                continue

            try:
                raw = c.read(pid, root + 0x120, 0xA0)
            except Exception as error:
                print(f"  body read fail: {error}")
                continue

            for offset in range(0, 0xA0, 8):
                word = struct.unpack_from("<Q", raw, offset)[0]
                values = ""
                if offset <= 0xA0 - 24:
                    triple = struct.unpack_from("<3d", raw, offset)
                    values = f"{triple[0]:>14.3f} {triple[1]:>14.3f} {triple[2]:>14.3f}"
                print(f"   +{0x120 + offset:#05x} {word:#018x}  {values}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
