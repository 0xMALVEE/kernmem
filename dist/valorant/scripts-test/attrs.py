"""Read the per-actor attributes an ESP needs: health, alive, ally, team, name.

Runs against the known working array. Prints evidence only.
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

ARRAY_SLOT = 0xD91FDD0
ROOT = 0x290
POSITION = 0x1B4
HANDLER = 0xC70
HEALTH = 0x200
MAX_HEALTH = 0x204
ALLY = 0xF58
TEAM_COMPONENT = 0x6A0
TEAM_ID = 0xF8
PLAYER_STATE = 0x480
NAME = 0x3A8


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


def f32(c, pid, a):
    try:
        return struct.unpack("<f", c.read(pid, a, 4))[0]
    except Exception:
        return 0.0


def u8(c, pid, a):
    try:
        return c.read(pid, a, 1)[0]
    except Exception:
        return -1


def fstring(c, pid, a, limit=40):
    try:
        length = i32(c, pid, a)
        if not length or abs(length) > 200:
            return ""
        wide = length < 0
        pointer = q(c, pid, a + 8)
        if not ok(pointer):
            return ""
        raw = c.read(pid, pointer, min(abs(length), limit) * (2 if wide else 1))
        return raw.decode("utf-16-le" if wide else "utf-8", "replace").split("\x00", 1)[0]
    except Exception:
        return ""


def main():
    game = reader.Game(allow_stale=True)
    game.open()
    pid = game.pid
    base = game.base

    with game, km.Client() as c:
        c.require_version()
        slot = base + ARRAY_SLOT
        data = q(c, pid, slot)
        num = i32(c, pid, slot + 8)
        print(f"pid {pid} base {base:#x} array {data:#x} num {num}")

        seen = set()

        for index in range(min(max(num, 0), 64)):
            actor = q(c, pid, data + index * 8)
            if not ok(actor) or actor in seen:
                continue
            seen.add(actor)

            root = q(c, pid, actor + ROOT)
            pos = None
            if ok(root):
                try:
                    pos = struct.unpack("<3f", c.read(pid, root + POSITION, 12))
                except Exception:
                    pos = None

            handler = q(c, pid, actor + HANDLER)
            hp = f32(c, pid, handler + HEALTH) if ok(handler) else 0.0
            max_hp = f32(c, pid, handler + MAX_HEALTH) if ok(handler) else 0.0
            ally = u8(c, pid, actor + ALLY)
            comp = q(c, pid, actor + TEAM_COMPONENT)
            team = i32(c, pid, comp + TEAM_ID) if ok(comp) else -1
            state = q(c, pid, actor + PLAYER_STATE)
            name = fstring(c, pid, state + NAME) if ok(state) else ""

            where = f"{pos[0]:.1f},{pos[1]:.1f},{pos[2]:.1f}" if pos else "-"
            print(
                f"[{index}] {actor:#x} pos {where} hp {hp:.0f}/{max_hp:.0f} "
                f"ally {ally} team {team} name {name!r}"
            )

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
