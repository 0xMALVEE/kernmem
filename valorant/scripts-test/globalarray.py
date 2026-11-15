"""Find the global actor array by distinct-position count.

Components of one actor share a root and therefore a position, so the global
array is the one where many entries carry DIFFERENT positions. Every candidate
is sampled widely, each entry is tried through root+0x1B4 and root+0x170 at both
widths (smallest-magnitude sane triple wins), and arrays are ranked by distinct
positions. Reads only.
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
ROOT = 0x290
POSITIONS = (0x1B4, 0x170)
SAMPLE = 60


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


def position(c, pid, root):
    best = None
    found = None

    for offset in POSITIONS:
        for wide in (False, True):
            try:
                if wide:
                    point = struct.unpack("<3d", c.read(pid, root + offset, 24))
                else:
                    point = struct.unpack("<3f", c.read(pid, root + offset, 12))
            except Exception:
                continue
            if not sane(point):
                continue
            magnitude = max(abs(v) for v in point)
            if best is None or magnitude < best:
                best = magnitude
                found = point

    return found


def main():
    game = reader.Game(allow_stale=True)
    game.open()
    pid = game.pid
    base = game.base

    with game, vgb.Client() as c:
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
                if not (3 < count < 800):
                    continue

                seen = set()
                distinct = set()

                for slot in range(min(count, SAMPLE)):
                    entry = q(c, pid, data + slot * 8)
                    if not ok(entry) or entry in seen:
                        continue
                    seen.add(entry)
                    root = q(c, pid, entry + ROOT)
                    if not ok(root):
                        continue
                    point = position(c, pid, root)
                    if point is not None:
                        distinct.add(tuple(round(v, 1) for v in point))

                if len(distinct) >= 3:
                    found.append((len(distinct), count, base + delta + index))

        found.sort(reverse=True)
        print(f"candidates {len(found)}")

        for distinct, count, slot in found[:8]:
            print(f"  distinct {distinct} num {count} slot {slot:#x}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
