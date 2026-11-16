"""Dump the root component of each unique actor, floats only, in small spans.

Reads only.
"""

from __future__ import annotations

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

        unique = []
        for index in range(min(max(num, 0), 64)):
            try:
                entry = struct.unpack("<Q", c.read(pid, data + index * 8, 8))[0]
            except Exception:
                continue
            if ok(entry) and entry not in unique:
                unique.append(entry)

        print(f"unique {len(unique)}")

        for entry in unique[:10]:
            try:
                root = struct.unpack("<Q", c.read(pid, entry + ROOT, 8))[0]
            except Exception as error:
                print(f"\n{entry:#x} root read failed: {error}")
                continue

            print(f"\n{entry:#x} root {root:#x}")
            if not ok(root):
                continue

            for span_start in (0x180, 0x1A0, 0x1C0):
                try:
                    raw = c.read(pid, root + span_start, 0x20)
                except Exception as error:
                    print(f"  +{span_start:#x} failed: {error}")
                    continue
                for offset in range(0, len(raw) - 11, 4):
                    triple = struct.unpack_from("<3f", raw, offset)
                    print(
                        f"  +{span_start + offset:#05x} "
                        f"{triple[0]:>12.2f} {triple[1]:>12.2f} {triple[2]:>12.2f}"
                    )

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
