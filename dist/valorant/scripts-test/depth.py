"""Test dereference depth at the GWorld slot and validate each level.

A world is only accepted when PersistentLevel is a pointer and that level's
OwningWorld points back at the world. Reads only.
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

SLOTS = (0xD92E288, 0xD873D00, 0xD93FD80)


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


def main():
    game = reader.Game(allow_stale=True)
    game.open()
    pid = game.pid
    base = game.base

    with game, km.Client() as c:
        c.require_version()
        print(f"pid {pid} base {base:#x} patch {game.patch}")

        for slot in SLOTS:
            value = q(c, pid, base + slot)
            print(f"\nslot {slot:#x} -> {value:#x} {'ptr' if ok(value) else 'NOT ptr'}")
            if not ok(value):
                continue

            node = value
            for depth in range(1, 4):
                level = q(c, pid, node + 0x38)
                back = q(c, pid, level + 0xC0) if ok(level) else 0
                data = q(c, pid, level + 0xA0) if ok(level) else 0
                count = i32(c, pid, level + 0xA8) if ok(level) else 0
                print(
                    f"  depth {depth}: node {node:#x} level {level:#x} "
                    f"back {back:#x} {'VALID' if back == node else 'invalid'} "
                    f"array {data:#x} num {count}"
                )
                if ok(level) and 0 < count < 3000 and ok(data):
                    print("    -> usable")
                    for index in range(min(count, 6)):
                        actor = q(c, pid, data + index * 8)
                        if not ok(actor):
                            continue
                        root = q(c, pid, actor + 0x290)
                        if not ok(root):
                            continue
                        try:
                            pos = struct.unpack("<3f", c.read(pid, root + 0x170, 12))
                        except Exception:
                            continue
                        print(f"      actor {actor:#x} pos {pos[0]:.1f},{pos[1]:.1f},{pos[2]:.1f}")
                    return 0

                nxt = q(c, pid, node)
                if not ok(nxt):
                    break
                node = nxt

    return 2


if __name__ == "__main__":
    raise SystemExit(main())
