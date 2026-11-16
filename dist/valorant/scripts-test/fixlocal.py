"""Find the local pawn's real position component.

0x290 on the resolved local pawn yields a root with garbage, so each pointer
field of the pawn is followed and the target tested for a sane double triple at
0x170. Also prints the pawn header so its class is visible.

Reads only.
"""

from __future__ import annotations

import math
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "scripts"))
sys.path.insert(0, str(Path(__file__).resolve().parent.parent.parent / "sdk"))

import reader  # noqa: E402

LO = 0x1000000000
HI = 0x7FFFFFFFFFFF


def ok(v):
    return LO <= v <= HI and v % 8 == 0


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

        try:
            head = game.read(local.address, 0x40)
            print("head " + " ".join(f"{w:016x}" for w in struct.unpack("<8Q", head)))
        except Exception as error:
            print(f"head read failed: {error}")

        for start in range(0x80, 0x900, 0x80):
            try:
                raw = game.read(local.address + start, 0x80)
            except Exception:
                continue
            for offset in range(0, len(raw) - 8, 8):
                pointer = struct.unpack_from("<Q", raw, offset)[0]
                if not ok(pointer):
                    continue
                for target_off in (0x170, 0x1B4):
                    try:
                        point = struct.unpack("<3d", game.read(pointer + target_off, 24))
                    except Exception:
                        continue
                    if sane(point):
                        print(
                            f"  pawn+{start + offset:#05x} -> {pointer:#x} "
                            f"pos@{target_off:#x} {point[0]:.1f} {point[1]:.1f} {point[2]:.1f}"
                        )

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
