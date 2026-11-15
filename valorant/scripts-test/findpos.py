"""Locate the world-position triple by cross-actor consistency.

For the best-scoring actor array, scan each actor and its root component for
three-component vectors, testing both float and double triples, and keep only
offsets that several different actors agree on. Reads only.
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

ROOT_CANDIDATES = (0x290, 0x288, 0x280, 0x298)
SCAN_SPAN = 0x600
STEP = 4


def ok(value):
    return LO <= value <= HI and value % 8 == 0


def q(c, pid, address):
    if not address:
        return 0
    try:
        return struct.unpack("<Q", c.read(pid, address, 8))[0]
    except Exception:
        return 0


def triple(raw, offset, kind):
    try:
        if kind == "f":
            return struct.unpack_from("<3f", raw, offset)
        return struct.unpack_from("<3d", raw, offset)
    except Exception:
        return None


def plausible(values):
    if values is None:
        return False
    if any(math.isnan(v) or math.isinf(v) for v in values):
        return False
    if all(abs(v) < 0.01 for v in values):
        return False
    if any(abs(v) > 100000.0 for v in values):
        return False
    return sum(1 for v in values if abs(v) > 1.0) >= 2


def find_array(c, pid, base):
    best = None

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

            good = 0
            for slot in range(min(count, 20)):
                actor = q(c, pid, data + slot * 8)
                if not ok(actor):
                    continue
                for root_off in ROOT_CANDIDATES:
                    root = q(c, pid, actor + root_off)
                    if not ok(root):
                        continue
                    try:
                        values = struct.unpack("<3f", c.read(pid, root + 0x170, 12))
                    except Exception:
                        continue
                    if plausible(values):
                        good += 1
                        break

            if best is None or good > best[0]:
                best = (good, count, data)

    return best


def main():
    game = reader.Game(allow_stale=True)
    game.open()
    pid = game.pid
    base = game.base

    with game, vgb.Client() as c:
        c.require_version()
        print(f"pid {pid} base {base:#x} patch {game.patch}")

        found = find_array(c, pid, base)
        if not found or not found[0]:
            print("no array found")
            return 2

        good, count, data = found
        print(f"array {data:#x} num {count} good {good}")

        actors = []
        for index in range(min(count, 40)):
            actor = q(c, pid, data + index * 8)
            if ok(actor):
                actors.append(actor)
        print(f"actors {len(actors)}")

        votes = {}

        for actor in actors[:10]:
            anchors = [("actor", actor)] + [
                (f"root{off:#x}", q(c, pid, actor + off)) for off in ROOT_CANDIDATES
            ]
            for label, anchor in anchors:
                if not ok(anchor):
                    continue
                try:
                    raw = c.read(pid, anchor, SCAN_SPAN)
                except Exception:
                    continue

                for offset in range(0, SCAN_SPAN - 24, STEP):
                    for kind in ("f", "d"):
                        if plausible(triple(raw, offset, kind)):
                            key = (label, offset, kind)
                            votes[key] = votes.get(key, 0) + 1

        ranked = sorted(votes.items(), key=lambda item: -item[1])
        print("\n[ranked offsets]")
        for (label, offset, kind), hits in ranked[:12]:
            kind_name = "float" if kind == "f" else "double"
            print(f"  {hits:>3} hits  {label} + {offset:#x} as {kind_name}")

        if not ranked:
            return 3

        (label, offset, kind), hits = ranked[0]
        print(f"\n[values for {label} + {offset:#x} {kind}, {hits} hits]")

        for actor in actors[:10]:
            anchor = actor if label == "actor" else q(c, pid, actor + int(label[4:], 16))
            if not ok(anchor):
                continue
            try:
                raw = c.read(pid, anchor, offset + 24)
            except Exception:
                continue
            print(f"  {actor:#x} {triple(raw, offset, kind)}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
