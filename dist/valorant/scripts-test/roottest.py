"""Test root_component candidates and locate the coordinate triple per actor.

For each actor in a known TArray slot, read the candidate root and dump float
triples across a window. Reads only.
"""

from __future__ import annotations

import math
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "scripts"))
sys.path.insert(0, str(Path(__file__).resolve().parent.parent.parent / "sdk"))

import km  # noqa: E402

import reader  # noqa: E402

LO = 0x1000000000
HI = 0x7FFFFFFFFFFF
ROOTS = (0x290, 0x298, 0x288, 0x2A0)


def ok(v):
    return LO <= v <= HI and v % 8 == 0


def main():
    slot = int(sys.argv[1], 16)

    game = reader.Game(allow_stale=True)
    game.open()

    with game, km.Client() as c:
        c.require_version()
        pid = game.pid

        data = struct.unpack("<Q", c.read(pid, slot, 8))[0]
        num = struct.unpack("<i", c.read(pid, slot + 8, 4))[0]
        print(f"slot {slot:#x} data {data:#x} num {num}")

        actors = []
        for i in range(min(max(num, 0), 24)):
            try:
                a = struct.unpack("<Q", c.read(pid, data + i * 8, 8))[0]
            except Exception:
                continue
            if ok(a):
                actors.append(a)
        print(f"actors {len(actors)}")

        for root_off in ROOTS:
            roots = []
            for a in actors:
                try:
                    r = struct.unpack("<Q", c.read(pid, a + root_off, 8))[0]
                except Exception:
                    r = 0
                roots.append(r if ok(r) else 0)
            print(f"\nroot +{root_off:#x}: ptrs {sum(1 for r in roots if r)}/{len(roots)}")

            for offset in range(0x140, 0x1E0, 4):
                vals = []
                for r in roots:
                    if not r:
                        continue
                    try:
                        t = struct.unpack("<3f", c.read(pid, r + offset, 12))
                    except Exception:
                        continue
                    if any(math.isnan(v) or math.isinf(v) or abs(v) > 40000 for v in t):
                        continue
                    if sum(1 for v in t if abs(v) > 50) < 2:
                        continue
                    vals.append(t)

                if len(vals) >= 3:
                    uniq = len({tuple(round(v, 1) for v in t) for t in vals})
                    if uniq >= 3:
                        sample = " | ".join(f"{t[0]:.0f},{t[1]:.0f},{t[2]:.0f}" for t in vals[:3])
                        print(f"  +{offset:#x} n={len(vals)} uniq={uniq}  {sample}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
