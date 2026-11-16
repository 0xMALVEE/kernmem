"""Find the local player controller by anchoring on known actors.

APlayerController holds AcknowledgedPawn at +0x518, so any pointer whose
+0x518 is one of the actors in the working array is a candidate controller.
Each hit is then followed to its camera manager and POV. Reads only.
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
PAWN = 0x518
CAMERA = 0x528
PRIVATES = (0x17B0, 0x17C0)

START = 0x9000000
END = 0xE200000
CHUNK = 0x1000


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

        slot = base + ARRAY_SLOT
        data = q(c, pid, slot)
        num = i32(c, pid, slot + 8)

        actors = set()
        for index in range(min(max(num, 0), 64)):
            entry = q(c, pid, data + index * 8)
            if ok(entry):
                actors.add(entry)
        print(f"array num {num} actors {len(actors)}")
        if not actors:
            return 2

        hits = 0

        for delta in range(START, END, CHUNK):
            try:
                raw = c.read(pid, base + delta, CHUNK)
            except Exception:
                continue

            for index in range(0, CHUNK, 8):
                candidate = struct.unpack_from("<Q", raw, index)[0]
                if not ok(candidate):
                    continue

                pawn = q(c, pid, candidate + PAWN)
                if pawn not in actors:
                    continue

                manager = q(c, pid, candidate + CAMERA)
                print(
                    f"HIT slot {base + delta + index:#x} controller {candidate:#x} "
                    f"pawn {pawn:#x} cam {manager:#x}"
                )

                root = q(c, pid, pawn + ROOT)
                if ok(root):
                    try:
                        pos = struct.unpack("<3f", c.read(pid, root + POSITION, 12))
                        print(f"    pawn pos {pos[0]:.1f} {pos[1]:.1f} {pos[2]:.1f}")
                    except Exception:
                        pass

                if ok(manager):
                    for priv in PRIVATES:
                        view = q(c, pid, manager + priv)
                        print(f"    priv +{priv:#x} view {view:#x}")
                        if not ok(view):
                            continue
                        try:
                            dloc = struct.unpack("<3d", c.read(pid, view + 0x10, 24))
                            drot = struct.unpack("<3d", c.read(pid, view + 0x28, 24))
                            dfov = struct.unpack("<f", c.read(pid, view + 0x40, 4))[0]
                            print(f"      d loc {dloc} rot {drot} fov {dfov}")
                        except Exception as error:
                            print(f"      d read failed: {error}")
                        try:
                            floc = struct.unpack("<3f", c.read(pid, view + 0x10, 12))
                            frot = struct.unpack("<3f", c.read(pid, view + 0x1C, 12))
                            ffov = struct.unpack("<f", c.read(pid, view + 0x28, 4))[0]
                            print(f"      f loc {floc} rot {frot} fov {ffov}")
                        except Exception as error:
                            print(f"      f read failed: {error}")

                hits += 1
                if hits >= 5:
                    return 0

        print(f"hits {hits}")

    return 0 if hits else 3


if __name__ == "__main__":
    raise SystemExit(main())
