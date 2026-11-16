"""Diff two array entries to find per-actor fields.

Entries that share a root produce identical positions, so this reads two
entries side by side and prints every offset where they differ, with float
triples from both. Offsets where both sides are plausible positions are marked.

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

ARRAY = 0xD91FDD0
SPAN = 0x400


def ok(v):
    return LO <= v <= HI and v % 8 == 0


def q(c, pid, a):
    try:
        return struct.unpack("<Q", c.read(pid, a, 8))[0]
    except Exception:
        return 0


def i32(c, pid, a):
    try:
        return struct.unpack("<i", c.read(pid, a, 4))[0]
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
        slot = base + ARRAY
        data = q(c, pid, slot)
        num = i32(c, pid, slot + 8)
        print(f"array {data:#x} num {num}")

        uniq = []
        for index in range(min(max(num, 0), 64)):
            entry = q(c, pid, data + index * 8)
            if ok(entry) and entry not in uniq:
                uniq.append(entry)

        print(f"unique {len(uniq)}")
        if len(uniq) < 2:
            return 1

        a, b = uniq[0], uniq[1]
        try:
            ra = c.read(pid, a, SPAN)
            rb = c.read(pid, b, SPAN)
        except Exception as error:
            print(f"read failed: {error}")
            return 2

        print(f"A {a:#x}\nB {b:#x}\n")

        shown = 0

        for offset in range(0, SPAN - 12, 4):
            if ra[offset:offset + 12] == rb[offset:offset + 12]:
                continue

            ta = struct.unpack_from("<3f", ra, offset)
            tb = struct.unpack_from("<3f", rb, offset)
            mark = " <== POSITION?" if sane(ta) and sane(tb) else ""

            if not mark and shown > 80:
                continue

            print(
                f"+{offset:#05x} A {ta[0]:>12.2f} {ta[1]:>12.2f} {ta[2]:>12.2f}"
                f" | B {tb[0]:>12.2f} {tb[1]:>12.2f} {tb[2]:>12.2f}{mark}"
            )
            shown += 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
