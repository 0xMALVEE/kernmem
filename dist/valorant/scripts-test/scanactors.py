"""Find actor arrays by scoring, bypassing the world pointer entirely.

An array is only trusted when several of its entries resolve through
root_component to a plausible world position. Reads only.
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
POSITION = 0x170
HANDLER = 0xC70
HEALTH = 0x200


def ok(value):
    return LO <= value <= HI and value % 8 == 0


def q(c, pid, address):
    if not address:
        return 0
    try:
        return struct.unpack("<Q", c.read(pid, address, 8))[0]
    except Exception:
        return 0


def vec3(c, pid, address):
    try:
        return struct.unpack("<3d", c.read(pid, address, 24))
    except Exception:
        return None


def plausible(position):
    if position is None:
        return False
    if any(math.isnan(v) or math.isinf(v) for v in position):
        return False
    if position == (0.0, 0.0, 0.0):
        return False
    return all(abs(v) < 200000.0 for v in position)


def score_array(c, pid, data, count):
    good = 0
    for index in range(min(count, 20)):
        actor = q(c, pid, data + index * 8)
        if not ok(actor):
            continue
        root = q(c, pid, actor + ROOT)
        if not ok(root):
            continue
        if plausible(vec3(c, pid, root + POSITION)):
            good += 1
    return good


def main():
    game = reader.Game(allow_stale=True)
    game.open()
    pid = game.pid
    base = game.base

    results = []

    with game, km.Client() as c:
        c.require_version()
        print(f"pid {pid} base {base:#x}")

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

                good = score_array(c, pid, data, count)
                if good >= 3:
                    results.append((good, count, base + delta + index, data))

        results.sort(reverse=True)
        print(f"candidates {len(results)}")

        for good, count, slot, data in results[:6]:
            print(f"  good {good} num {count} slot {slot:#x} data {data:#x}")

        if not results:
            return 2

        good, count, slot, data = results[0]
        print(f"\n[walk {data:#x}]")

        for index in range(min(count, 25)):
            actor = q(c, pid, data + index * 8)
            if not ok(actor):
                continue
            root = q(c, pid, actor + ROOT)
            position = vec3(c, pid, root + POSITION) if ok(root) else None
            if not plausible(position):
                continue
            handler = q(c, pid, actor + HANDLER)
            health = 0.0
            if ok(handler):
                try:
                    health = struct.unpack("<f", c.read(pid, handler + HEALTH, 4))[0]
                except Exception:
                    health = 0.0
            print(f"  [{index}] {actor:#x} pos {position} hp {health:.0f}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
