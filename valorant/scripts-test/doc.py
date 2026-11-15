"""Test the documented chain from the 11.14 dump end to end.

GWorld (0xD92E288) needs at least one dereference. From the world the dump
documents GameState 0x178, PlayerStateArray 0x480, PawnPrivate 0x4E8, root
0x290, RelativeLocation 0x170 and DamageHandler 0xC70 -> Health 0x200. The
camera path is LocalPlayer -> PlayerController 0x528 -> view_target 0x580 ->
pov 0x10 with FMinimalViewInfo at 0x00 / 0x18 / 0x30.

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

GWORLD = 0xD92E288
LEVEL = 0x38
GAMESTATE = 0x178
PSA = 0x480
PAWN_PRIVATE = 0x4E8
ROOT = 0x290
LOC_D = 0x170
LOC_F = 0x1B4
HANDLER = 0xC70
HEALTH = 0x200
TEAM_COMPONENT = 0x6A0
TEAM_ID = 0xF8
NAME = 0x500

INSTANCE = 0x1D8
LOCAL_PLAYERS = 0x40
CONTROLLER = 0x38
PAWN = 0x4B0
CAMERA = 0x528
VIEW_TARGET = 0x580
POV = 0x10


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

    with game, vgb.Client() as c:
        c.require_version()
        print(f"pid {pid} base {base:#x} patch {game.patch}")

        world = gs = data = 0
        num = 0

        for depth in (1, 2, 3):
            node = base + GWORLD
            for _ in range(depth):
                node = q(c, pid, node)
                if not ok(node):
                    break
            if not ok(node):
                continue

            level = q(c, pid, node + LEVEL)
            state = q(c, pid, node + GAMESTATE)
            arr = state + PSA if ok(state) else 0
            count = i32(c, pid, arr + 8) if arr else 0
            pointer = q(c, pid, arr) if arr else 0
            print(
                f"depth {depth} world {node:#x} level {level:#x} "
                f"gamestate {state:#x} psa {pointer:#x} num {count}"
            )

            if ok(pointer) and 0 < count < 200:
                world, gs, data, num = node, state, pointer, count
                break

        if not world:
            print("no documented chain")
            return 1

        print(f"\n[players via {data:#x} num {num}]")

        for index in range(min(num, 40)):
            state = q(c, pid, data + index * 8)
            if not ok(state):
                continue
            pawn = q(c, pid, state + PAWN_PRIVATE)
            root = q(c, pid, pawn + ROOT) if ok(pawn) else 0
            pos = None
            if ok(root):
                try:
                    pos = struct.unpack("<3f", c.read(pid, root + LOC_F, 12))
                except Exception:
                    pos = None
            handler = q(c, pid, pawn + HANDLER) if ok(pawn) else 0
            hp = 0.0
            if ok(handler):
                try:
                    hp = struct.unpack("<f", c.read(pid, handler + HEALTH, 4))[0]
                except Exception:
                    hp = 0.0
            comp = q(c, pid, pawn + TEAM_COMPONENT) if ok(pawn) else 0
            team = i32(c, pid, comp + TEAM_ID) if ok(comp) else -1
            try:
                length = i32(c, pid, state + NAME)
                text_ptr = q(c, pid, state + NAME + 8)
                name = ""
                if 0 < abs(length) < 64 and ok(text_ptr):
                    raw = c.read(pid, text_ptr, abs(length) * 2)
                    name = raw.decode("utf-16-le", "replace").split("\x00", 1)[0]
            except Exception:
                name = ""
            where = f"{pos[0]:.1f},{pos[1]:.1f},{pos[2]:.1f}" if pos else "-"
            print(f"  [{index}] state {state:#x} pawn {pawn:#x} pos {where} hp {hp:.0f} team {team} {name!r}")

        print("\n[camera]")
        instance = q(c, pid, world + INSTANCE)
        players = q(c, pid, instance + LOCAL_PLAYERS) if ok(instance) else 0
        controller = q(c, pid, players + CONTROLLER) if ok(players) else 0
        pawn = q(c, pid, controller + PAWN) if ok(controller) else 0
        camera = q(c, pid, controller + CAMERA) if ok(controller) else 0
        target = q(c, pid, camera + VIEW_TARGET) if ok(camera) else 0
        print(
            f"  instance {instance:#x} players {players:#x} controller {controller:#x} "
            f"pawn {pawn:#x} camera {camera:#x} target {target:#x}"
        )

        for label, pov in (
            ("inline", target + POV),
            ("deref", q(c, pid, target + POV) if ok(target) else 0),
        ):
            if not ok(pov):
                print(f"  {label}: no pov")
                continue
            try:
                raw = c.read(pid, pov, 0x40)
            except Exception as error:
                print(f"  {label}: read failed {error}")
                continue
            loc = struct.unpack_from("<3d", raw, 0x00)
            rot = struct.unpack_from("<3d", raw, 0x18)
            fov = struct.unpack_from("<f", raw, 0x30)[0]
            print(
                f"  {label} pov {pov:#x} loc {loc[0]:.1f} {loc[1]:.1f} {loc[2]:.1f} "
                f"rot {rot[0]:.1f} {rot[1]:.1f} {rot[2]:.1f} fov {fov:.1f} sane {sane(loc)}"
            )

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
