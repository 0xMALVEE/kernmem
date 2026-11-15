"""Vote on the coordinate offset across many actors of one array.

For each unique actor a wide window is read, every 4-byte-aligned float triple
is parsed, and an offset only wins when several different actors produce a
plausible triple there. Reads only.
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

SPAN = 0x1000
ROOT = 0x290


def ok(value):
    return LO <= value <= HI and value % 8 == 0


def finite(value):
    return not (math.isnan(value) or math.isinf(value))


def main():
    data = int(sys.argv[1], 16)
    limit = int(sys.argv[2]) if len(sys.argv) > 2 else 12

    game = reader.Game(allow_stale=True)
    game.open()

    with game, vgb.Client() as c:
        c.require_version()
        pid = game.pid

        try:
            count = struct.unpack("<i", c.read(pid, data + 8, 4))[0]
        except Exception as error:
            print(f"count read failed: {error}")
            return 1

        print(f"array {data:#x} num {count}")

        actors = []
        seen = set()

        for index in range(min(max(count, 0), 64)):
            try:
                actor = struct.unpack("<Q", c.read(pid, data + index * 8, 8))[0]
            except Exception:
                continue
            if ok(actor) and actor not in seen:
                seen.add(actor)
                actors.append(actor)

        print(f"unique actors {len(actors)}")

        windows = {}

        for actor in actors[:limit]:
            for label, anchor in (
                ("actor", actor),
                ("root", struct.unpack("<Q", c.read(pid, actor + ROOT, 8))[0] if True else 0),
            ):
                if label == "root" and not ok(anchor):
                    continue
                try:
                    windows[(actor, label)] = c.read(pid, anchor, SPAN)
                except Exception:
                    continue

        print(f"windows {len(windows)}")

        votes = {}

        for (_actor, label), raw in windows.items():
            for offset in range(0, SPAN - 12, 4):
                values = struct.unpack_from("<3f", raw, offset)
                if not all(finite(v) and abs(v) < 40000.0 for v in values):
                    continue
                if sum(1 for v in values if abs(v) > 50.0) < 2:
                    continue
                key = (label, offset)
                votes.setdefault(key, []).append(values)

        ranked = sorted(votes.items(), key=lambda item: -len(item[1]))
        print("\n[ranked]")

        for (label, offset), values in ranked[:10]:
            print(f"  {len(values):>2} actors  {label} + {offset:#x}")
            for triple in values[:4]:
                print(f"      {triple[0]:>11.2f} {triple[1]:>11.2f} {triple[2]:>11.2f}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
