"""Scan the local pawn's root for a sane position triple. Reads only."""

from __future__ import annotations

import math
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "scripts"))
sys.path.insert(0, str(Path(__file__).resolve().parent.parent.parent / "sdk"))

import reader  # noqa: E402


def sane(t):
    if any(math.isnan(v) or math.isinf(v) or abs(v) > 300000.0 for v in t):
        return False
    return sum(1 for v in t if abs(v) > 100.0) >= 2


def main():
    game = reader.Game(allow_stale=True)
    game.open()

    with game:
        local = game.local()
        if local is None:
            print("local unresolved")
            return 1

        print(f"local {local.address:#x}")
        print(f"actors: {[hex(a.address) for a in game.actors()]}")

        root = game.u64(local.address + 0x290)
        print(f"root {root:#x}")

        for start in range(0x100, 0x500, 0x80):
            try:
                raw = game.read(root + start, 0x80)
            except Exception as error:
                print(f"  +{start:#x} read failed: {error}")
                continue
            for offset in range(0, len(raw) - 24, 8):
                point = struct.unpack_from("<3d", raw, offset)
                if sane(point):
                    print(
                        f"  +{start + offset:#05x} "
                        f"{point[0]:.1f} {point[1]:.1f} {point[2]:.1f}"
                    )

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
