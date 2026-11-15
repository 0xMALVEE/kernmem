"""Rank actor-array candidates by distinct plausible positions, sweeping offsets.

Reads only. Everything runs in one process so addresses stay valid.
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

ROOTS = (0x290, 0x288, 0x2A0, 0x280)
OFFSETS = tuple(range(0x120, 0x240, 0x10))


def ok(v):
    return LO <= v <= HI and v % 8 == 0


def q(c, pid, a):
    if not a:
        return 0
    try:
        return struct.unpack("<Q", c.read(pid, a, 8))[0]
    except Exception:
        return 0


def i32(c, pid, a):
    try:
        return struct.unpack("<i", c.read(pid, a, 4))[0]
    except Exception:
        return 0


def triple(c, pid, a, kind):
    try:
        raw = c.read(pid, a, 24)
    except Exception:
        return None
    values = struct.unpack("<3d", raw) if kind == "d" else struct.unpack("<3f", raw[:12])
    if any(math.isnan(v) or math.isinf(v) or abs(v) > 60000.0 for v in values):
        return None
    if sum(1 for v in values if abs(v) > 20.0) < 2:
        return None
    return values


def unique_positions(c, pid, level, count, root_off, off, kind):
    data = q(c, pid, level + 0xA0)
    if not ok(data):
        return 0

    seen = set()
    for index in range(min(count, 40)):
        actor = q(c, pid, data + index * 8)
        if not ok(actor):
            continue
        root = q(c, pid, actor + root_off)
        if not ok(root):
            continue
        values = triple(c, pid, root + off, kind)
        if values is None:
            continue
        seen.add(tuple(round(v, 1) for v in values))
    return len(seen)


def main():
    game = reader.Game(allow_stale=True)
    game.open()
    pid = game.pid
    base = game.base

    with game, vgb.Client() as c:
        c.require_version()
        print(f"pid {pid} base {base:#x} patch {game.patch}")

        levels = []

        for delta in range(0xD000000, 0xE200000, 0x1000):
            if len(levels) > 400:
                break

            try:
                raw = c.read(pid, base + delta, 0x1000)
            except Exception:
                continue

            for index in range(0, 0x1000, 8):
                level = struct.unpack_from("<Q", raw, index)[0]
                if not ok(level):
                    continue

                data = q(c, pid, level + 0xA0)
                count = i32(c, pid, level + 0xA8)
                if not ok(data) or not (2 < count < 3000):
                    continue
                levels.append((level, count, base + delta + index))

        print(f"level candidates {len(levels)}")
        if not levels:
            return 2

        scored = []
        for level, count, slot in levels[:400]:
            best = (0, None, None, None)
            for root_off in ROOTS:
                for off in OFFSETS:
                    for kind in ("d", "f"):
                        found = unique_positions(c, pid, level, count, root_off, off, kind)
                        if found > best[0]:
                            best = (found, root_off, off, kind)
            if best[0] >= 3:
                scored.append((best[0], count, level, slot, best[1], best[2], best[3]))

        scored.sort(reverse=True)
        print(f"scored {len(scored)}")

        for found, count, level, slot, root_off, off, kind in scored[:6]:
            print(
                f"  distinct {found} num {count} level {level:#x} slot {slot:#x} "
                f"root +{root_off:#x} off +{off:#x} {kind}"
            )

        if not scored:
            return 3

        found, count, level, slot, root_off, off, kind = scored[0]
        data = q(c, pid, level + 0xA0)
        print(f"\n[walk level {level:#x} data {data:#x} root +{root_off:#x} off +{off:#x} {kind}]")

        for index in range(min(count, 20)):
            actor = q(c, pid, data + index * 8)
            if not ok(actor):
                continue
            root = q(c, pid, actor + root_off)
            if not ok(root):
                continue
            values = triple(c, pid, root + off, kind)
            handler = q(c, pid, actor + 0xC70)
            health = 0.0
            if ok(handler):
                try:
                    health = struct.unpack("<f", c.read(pid, handler + 0x1E0, 4))[0]
                except Exception:
                    health = 0.0
            print(f"  [{index}] {actor:#x} pos {values} hp {health:.0f}")

        print(f"\n[raw dump of {data:#x} first entry +0x100..0x2a0]")
        first = q(c, pid, data)
        if ok(first):
            try:
                body = c.read(pid, first + 0x100, 0x1A0)
                for offset in range(0, 0x1A0, 8):
                    word = struct.unpack_from("<Q", body, offset)[0]
                    pair = struct.unpack_from("<2d", body, offset)
                    print(
                        f"  +{0x100 + offset:#05x} {word:#018x} "
                        f"d {pair[0]:>16.2f} {pair[1]:>16.2f}"
                    )
            except Exception as error:
                print(f"  dump failed: {error}")
        else:
            print(f"  first entry {first:#x} not a pointer")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
