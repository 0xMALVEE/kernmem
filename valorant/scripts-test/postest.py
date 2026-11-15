"""Test candidate coordinate offsets directly on array entries and dump raw bytes.

Reads only.
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
OFFSETS = (0x130, 0x150, 0x170, 0x1A0, 0x1B0, 0x360)


def ok(v):
    return LO <= v <= HI and v % 8 == 0


def main():
    slot = int(sys.argv[1], 16)

    game = reader.Game(allow_stale=True)
    game.open()

    with game, vgb.Client() as c:
        c.require_version()
        pid = game.pid

        data = struct.unpack("<Q", c.read(pid, slot, 8))[0]
        num = struct.unpack("<i", c.read(pid, slot + 8, 4))[0]
        print(f"slot {slot:#x} data {data:#x} num {num}")

        entries = []
        for i in range(min(max(num, 0), 20)):
            try:
                e = struct.unpack("<Q", c.read(pid, data + i * 8, 8))[0]
            except Exception:
                continue
            if ok(e):
                entries.append(e)

        print(f"entries {len(entries)}")

        for offset in OFFSETS:
            print(f"\n[+{offset:#x} as float]")
            for e in entries[:8]:
                try:
                    t = struct.unpack("<3f", c.read(pid, e + offset, 12))
                except Exception as error:
                    print(f"  {e:#x} fail {error}")
                    continue
                mark = ""
                if all(not math.isnan(v) and not math.isinf(v) and abs(v) < 40000 for v in t):
                    if sum(1 for v in t if abs(v) > 50) >= 2:
                        mark = "  <-- plausible"
                print(f"  {e:#x} {t[0]:>12.2f} {t[1]:>12.2f} {t[2]:>12.2f}{mark}")

        print("\n[raw dump of first entry +0x100..0x1c0]")
        if entries:
            try:
                raw = c.read(pid, entries[0] + 0x100, 0xC0)
                for off in range(0, 0xC0, 8):
                    word = struct.unpack_from("<Q", raw, off)[0]
                    pair = struct.unpack_from("<2f", raw, off)
                    print(f"  +{0x100 + off:#05x} {word:#018x}  f {pair[0]:>12.3f} {pair[1]:>12.3f}")
            except Exception as error:
                print(f"  dump failed: {error}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
