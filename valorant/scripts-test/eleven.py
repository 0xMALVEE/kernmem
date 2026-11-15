"""Test the 11.14 release dump against the live client.

GWorld is plain here, so no decrypt is needed. Everything is validated by the
walk actually producing distinct, plausible positions. Reads only.
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

GWORLD = 0xD92E288


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
    return sum(1 for v in t if abs(v) > 50.0) >= 2


def main():
    game = reader.Game(allow_stale=True)
    game.open()
    pid = game.pid
    base = game.base

    with game, vgb.Client() as c:
        c.require_version()
        print(f"pid {pid} base {base:#x} patch {game.patch}")

        world = q(c, pid, base + GWORLD)
        print(f"GWorld slot -> {world:#x} {'ptr' if ok(world) else 'NOT ptr'}")
        if not ok(world):
            return 1

        level = q(c, pid, world + 0x38)
        print(f"level {level:#x} {'ptr' if ok(level) else 'NOT ptr'}")
        if not ok(level):
            return 2

        data = q(c, pid, level + 0xA0)
        num = i32(c, pid, level + 0xA8)
        print(f"actor array {data:#x} num {num}")

        print("[actors]")
        seen = set()
        good = 0

        for index in range(min(max(num, 0), 40)):
            actor = q(c, pid, data + index * 8)
            if not ok(actor) or actor in seen:
                continue
            seen.add(actor)
            root = q(c, pid, actor + 0x290)
            if not ok(root):
                continue
            try:
                d = struct.unpack("<3d", c.read(pid, root + 0x170, 24))
                f = struct.unpack("<3f", c.read(pid, root + 0x170, 12))
            except Exception:
                continue
            handler = q(c, pid, actor + 0xC70)
            hp = 0.0
            if ok(handler):
                try:
                    hp = struct.unpack("<f", c.read(pid, handler + 0x200, 4))[0]
                except Exception:
                    hp = 0.0
            mark = "D" if sane(d) else ("F" if sane(f) else " ")
            if mark != " ":
                good += 1
            print(
                f"  [{index}] {actor:#x} {mark} d {d[0]:.1f},{d[1]:.1f},{d[2]:.1f} "
                f"f {f[0]:.1f},{f[1]:.1f},{f[2]:.1f} hp {hp:.0f}"
            )

        print(f"sane positions {good}")

        print("[camera]")
        for inst_off in (0x1D8, 0x1A0):
            instance = q(c, pid, world + inst_off)
            players = q(c, pid, instance + 0x40) if ok(instance) else 0
            controller = q(c, pid, players + 0x38) if ok(players) else 0
            pawn = q(c, pid, controller + 0x518) if ok(controller) else 0
            manager = q(c, pid, controller + 0x528) if ok(controller) else 0
            print(
                f"  inst+{inst_off:#x} instance {instance:#x} players {players:#x} "
                f"controller {controller:#x} pawn {pawn:#x} cam {manager:#x}"
            )
            if ok(manager):
                for priv in (0x17B0, 0x17C0):
                    view = q(c, pid, manager + priv)
                    print(f"    priv+{priv:#x} view {view:#x}")
                    if not ok(view):
                        continue
                    try:
                        loc = struct.unpack("<3d", c.read(pid, view + 0x10, 24))
                        rot = struct.unpack("<3d", c.read(pid, view + 0x28, 24))
                        fov = struct.unpack("<f", c.read(pid, view + 0x40, 4))[0]
                        print(f"      loc {loc} rot {rot} fov {fov}")
                    except Exception as error:
                        print(f"      read failed: {error}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
