"""Hunt for the player controller by validating its whole camera chain.

A candidate is only accepted when every link holds:
  candidate + 0x528 -> camera manager
  manager   + 0x17C0 -> FMinimalViewInfo
  view      + 0x10  -> plausible world position (doubles)
  view      + 0x40  -> plausible vertical FOV

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


def plausible_position(values):
    if any(math.isnan(v) or math.isinf(v) for v in values):
        return False
    if sum(1 for v in values if abs(v) > 100.0) < 2:
        return False
    return all(abs(v) < 200000.0 for v in values)


def main():
    game = reader.Game(allow_stale=True)
    game.open()
    pid = game.pid
    base = game.base

    hits = 0

    with game, vgb.Client() as c:
        c.require_version()
        print(f"pid {pid} base {base:#x} patch {game.patch}")

        for delta in range(START, END, CHUNK):
            try:
                raw = c.read(pid, base + delta, CHUNK)
            except Exception:
                continue

            for index in range(0, CHUNK, 8):
                candidate = struct.unpack_from("<Q", raw, index)[0]
                if not ok(candidate):
                    continue

                manager = q(c, pid, candidate + 0x528)
                if not ok(manager):
                    continue

                view = q(c, pid, manager + 0x17C0)
                if not ok(view):
                    continue

                try:
                    location = struct.unpack("<3d", c.read(pid, view + 0x10, 24))
                    rotation = struct.unpack("<3d", c.read(pid, view + 0x28, 24))
                    fov = struct.unpack("<f", c.read(pid, view + 0x40, 4))[0]
                except Exception:
                    continue

                if not plausible_position(location):
                    continue
                if not (40.0 < fov < 140.0):
                    continue
                if not all(abs(v) < 400.0 for v in rotation):
                    continue

                pawn = q(c, pid, candidate + 0x518)
                print(
                    f"HIT slot {base + delta + index:#x} controller {candidate:#x} "
                    f"pawn {pawn:#x} cam {manager:#x} view {view:#x}"
                )
                print(
                    f"    loc {location[0]:.1f} {location[1]:.1f} {location[2]:.1f} "
                    f"rot {rotation[0]:.1f} {rotation[1]:.1f} {rotation[2]:.1f} fov {fov:.1f}"
                )

                if ok(pawn):
                    root = q(c, pid, pawn + 0x290)
                    if ok(root):
                        try:
                            pos = struct.unpack("<3f", c.read(pid, root + 0x1B4, 12))
                            print(f"    pawn pos {pos[0]:.1f} {pos[1]:.1f} {pos[2]:.1f}")
                        except Exception:
                            pass

                hits += 1
                if hits >= 6:
                    print(f"stopping at {hits} hits")
                    return 0

        print(f"hits {hits}")

    return 0 if hits else 2


if __name__ == "__main__":
    raise SystemExit(main())
