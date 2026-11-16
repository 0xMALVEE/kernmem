"""Dump raw memory for a few unique actors, as doubles and floats.

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


def ok(v):
    return LO <= v <= HI and v % 8 == 0


def dump(c, pid, label, base, span):
    print(f"\n=== {label} {base:#x} ===")
    try:
        raw = c.read(pid, base, span)
    except Exception as error:
        print(f"  read failed: {error}")
        return

    for offset in range(0, len(raw) - 7, 8):
        word = struct.unpack_from("<Q", raw, offset)[0]
        f = struct.unpack_from("<2f", raw, offset)
        d = struct.unpack_from("<2d", raw, offset) if offset + 16 <= len(raw) else (0.0, 0.0)
        print(
            f"  +{offset:#05x} {word:#018x} "
            f"d {d[0]:>18.3f} {d[1]:>18.3f} "
            f"f {f[0]:>12.2f} {f[1]:>12.2f}"
        )


def main():
    slot = int(sys.argv[1], 16)
    limit = int(sys.argv[2]) if len(sys.argv) > 2 else 3

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

        for entry in unique[:limit]:
            dump(c, pid, f"entry {entry:#x}", entry, 0x300)
            try:
                root = struct.unpack("<Q", c.read(pid, entry + 0x290, 8))[0]
            except Exception:
                root = 0
            if ok(root):
                dump(c, pid, f"root of {entry:#x}", root + 0x100, 0x180)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
