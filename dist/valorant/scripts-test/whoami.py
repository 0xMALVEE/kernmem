"""Identify which array entry is the local player.

Each entry is tested for the controller's pawn at PawnPrivate 0x4E8 and
SpawnedCharacter 0xBC8. If a match is found its own position is printed.

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

ARRAY = 0xD81F3B0
ROOT = 0x290
LOC = 0x170
FIELDS = (0x4E8, 0xBC8, 0x480)
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
        print(f"local {local.address:#x}" if local else "local unresolved")

        slot = game.base + ARRAY
        data = game.u64(slot)
        num = game.i32(slot + 8)
        print(f"array {data:#x} num {num}")

        for index in range(min(max(num, 0), 64)):
            entry = game.u64(data + index * 8)
            if not ok(entry):
                continue

            marks = []
            for field in FIELDS:
                try:
                    value = game.u64(entry + field)
                except Exception:
                    continue
                if value and local and value == local.address:
                    marks.append(f"field{field:#x}=LOCAL")

            try:
                root = game.u64(entry + ROOT)
            except Exception:
                root = 0
            point = None
            if ok(root):
                try:
                    point = struct.unpack("<3d", game.read(root + LOC, 24))
                except Exception:
                    point = None

            where = f"{point[0]:.0f},{point[1]:.0f}" if point and sane(point) else "-"
            print(f"  [{index}] {entry:#x} pos {where} {' '.join(marks)}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
