"""Find the actor array by scanning the level object at many offsets.

Position reads at root+0x1B4, which is the documented 0x170 shifted by 0x44, so
the documented actor_array 0xA0 is probably shifted as well. Every aligned
offset on the level is tested for a pointer/count pair whose entries resolve to
plausible positions.

Reads only.
"""

from __future__ import annotations

import math
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "scripts"))
sys.path.insert(0, str(Path(__file__).resolve().parent.parent.parent / "sdk"))

import vgb  # noqa: E402

import reader  # noqa: E402

LO = 0x1000000000
HI = 0x7FFFFFFFFFFF

WORLD_SLOTS = (0xDB8D780, 0xD92E288)
ROOTS = (0x290, 0x2D4, 0x288, 0x2A0)
POSITIONS = (0x1B4, 0x170)


def ok(v):
    return LO <= v <= HI and v % 8 == 0


def q(c, pid, a):
    if not a:
        return 0
    try:
        return struct.unpack("<Q", c.read(pid, a, 8))[0]
    except Exception:
        return 0


def sane(t):
    if any(math.isnan(v) or math.isinf(v) or abs(v) > 300000.0 for v in t):
        return False
    return sum(1 for v in t if abs(v) > 100.0) >= 2


def score(c, pid, data, count):
    """How many of the first entries give a plausible position."""

    good = 0
    seen = set()
    positions = []

    for index in range(min(count, 40)):
        actor = q(c, pid, data + index * 8)
        if not ok(actor) or actor in seen:
            continue
        seen.add(actor)

        for root_off in ROOTS:
            root = q(c, pid, actor + root_off)
            if not ok(root):
                continue
            for pos_off in POSITIONS:
                try:
                    point = struct.unpack("<3f", c.read(pid, root + pos_off, 12))
                except Exception:
                    continue
                if sane(point):
                    good += 1
                    positions.append(point)
                    break
            else:
                continue
            break

    return good, positions


def main():
    game = reader.Game(allow_stale=True)
    game.open()
    pid = game.pid
    base = game.base

    with game, vgb.Client() as c:
        c.require_version()
        print(f"pid {pid} base {base:#x} patch {game.patch}")

        for slot in WORLD_SLOTS:
            for depth in (1, 2):
                node = base + slot
                for _ in range(depth):
                    node = q(c, pid, node)
                if not ok(node):
                    continue

                level = q(c, pid, node + 0x38)
                print(f"\nslot {slot:#x} depth {depth} world {node:#x} level {level:#x}")
                if not ok(level):
                    continue

                try:
                    raw = c.read(pid, level, 0x200)
                except Exception as error:
                    print(f"  level read failed: {error}")
                    continue

                for offset in range(0, len(raw) - 16, 8):
                    data = struct.unpack_from("<Q", raw, offset)[0]
                    count = struct.unpack_from("<i", raw, offset + 8)[0]
                    if not ok(data) or not (2 < count < 400):
                        continue

                    good, positions = score(c, pid, data, count)
                    if good < 2:
                        continue

                    distinct = len({tuple(round(v, 1) for v in p) for p in positions})
                    print(
                        f"  level+{offset:#x} data {data:#x} num {count} "
                        f"good {good} distinct {distinct}"
                    )
                    for point in positions[:4]:
                        print(
                            f"      {point[0]:.1f} {point[1]:.1f} {point[2]:.1f}"
                        )

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
