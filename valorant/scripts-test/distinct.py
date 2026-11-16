"""Rank candidate actor arrays by how many DISTINCT positions they yield.

Reads only.
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
ROOT = 0x290
POS = 0x1B4


def ok(v):
    return LO <= v <= HI and v % 8 == 0


def q(c, pid, a):
    try:
        return struct.unpack("<Q", c.read(pid, a, 8))[0]
    except Exception:
        return 0


def sane(t):
    if any(math.isnan(v) or math.isinf(v) or abs(v) > 300000.0 for v in t):
        return False
    return sum(1 for v in t if abs(v) > 100.0) >= 2


def main():
    game = reader.Game(allow_stale=True)
    game.open()
    pid = game.pid
    base = game.base

    with game, km.Client() as c:
        c.require_version()
        print(f"pid {pid} base {base:#x}")

        found = []

        for delta in range(0xD000000, 0xE200000, 0x1000):
            try:
                raw = c.read(pid, base + delta, 0x1000)
            except Exception:
                continue

            for index in range(0, 0x1000, 8):
                data = struct.unpack_from("<Q", raw, index)[0]
                if not ok(data):
                    continue
                try:
                    count = struct.unpack_from("<i", raw, index + 8)[0]
                except Exception:
                    continue
                if not (2 < count < 400):
                    continue

                seen = set()
                positions = set()

                for slot in range(min(count, 24)):
                    entry = q(c, pid, data + slot * 8)
                    if not ok(entry) or entry in seen:
                        continue
                    seen.add(entry)
                    root = q(c, pid, entry + ROOT)
                    if not ok(root):
                        continue
                    try:
                        point = struct.unpack("<3f", c.read(pid, root + POS, 12))
                    except Exception:
                        continue
                    if sane(point):
                        positions.add(tuple(round(v, 1) for v in point))

                if len(positions) >= 3:
                    found.append((len(positions), count, base + delta + index))

        found.sort(reverse=True)
        print(f"candidates {len(found)}")

        for distinct, count, slot in found[:6]:
            print(f"  distinct {distinct} num {count} slot {slot:#x}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
