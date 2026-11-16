"""Dump float triples around the working position offset so the rotation is visible.

Position reads correctly at root+0x1B4, which is the documented 0x170 shifted by
0x44, so the rotation should sit near 0x188 + 0x44 = 0x1CC. This prints the raw
float window for a few positioned actors. Reads only.
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

ARRAY_SLOT = 0xD91FDD0
ROOT = 0x290
POSITION = 0x1B4

FROM = 0x1B4
SPAN = 0x90


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
        print(f"pid {pid} base {base:#x} patch {game.patch}")

        slot = base + ARRAY_SLOT
        data = q(c, pid, slot)
        num = i32(c, pid, slot + 8)

        rows = []
        seen = set()

        for index in range(min(max(num, 0), 64)):
            entry = q(c, pid, data + index * 8)
            if not ok(entry) or entry in seen:
                continue
            seen.add(entry)
            root = q(c, pid, entry + ROOT)
            if not ok(root):
                continue
            try:
                point = struct.unpack("<3f", c.read(pid, root + POSITION, 12))
            except Exception:
                continue
            if sane(point):
                rows.append((entry, root, point))

        print(f"positioned {len(rows)}")

        for entry, root, point in rows[:4]:
            print(f"\n{entry:#x} root {root:#x} pos {point[0]:.2f} {point[1]:.2f} {point[2]:.2f}")
            try:
                raw = c.read(pid, root + FROM, SPAN)
            except Exception as error:
                print(f"  dump failed: {error}")
                continue

            for offset in range(0, len(raw) - 11, 4):
                triple = struct.unpack_from("<3f", raw, offset)
                mark = ""
                if all(math.isfinite(v) and abs(v) <= 400.0 for v in triple) and max(
                    abs(v) for v in triple
                ) >= 0.5:
                    mark = "  <-- rotation-like"
                print(
                    f"  +{FROM + offset:#05x} "
                    f"{triple[0]:>13.4f} {triple[1]:>13.4f} {triple[2]:>13.4f}{mark}"
                )

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
