"""Dump the camera manager private block and any view pointer it holds.

Prints raw words with double and float interpretations, both directly and after
one dereference, so the real POV layout is visible. Reads only.
"""

from __future__ import annotations

import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "scripts"))
sys.path.insert(0, str(Path(__file__).resolve().parent.parent.parent / "sdk"))

import vgb  # noqa: E402

import reader  # noqa: E402

LO = 0x1000000000
HI = 0x7FFFFFFFFFFF


def ok(v):
    return LO <= v <= HI and v % 8 == 0


def dump(c, pid, label, address, span):
    print(f"\n=== {label} {address:#x} ===")
    try:
        raw = c.read(pid, address, span)
    except Exception as error:
        print(f"  read failed: {error}")
        return

    for offset in range(0, len(raw) - 7, 8):
        word = struct.unpack_from("<Q", raw, offset)[0]
        f = struct.unpack_from("<2f", raw, offset)
        d = struct.unpack_from("<2d", raw, offset) if offset + 16 <= len(raw) else (0.0, 0.0)
        print(
            f"  +{offset:#05x} {word:#018x} "
            f"d {d[0]:>16.6g} {d[1]:>16.6g} "
            f"f {f[0]:>12.6g} {f[1]:>12.6g}"
        )


def main():
    controller = int(sys.argv[1], 16)

    game = reader.Game(allow_stale=True)
    game.open()

    with game, vgb.Client() as c:
        c.require_version()
        pid = game.pid

        try:
            manager = struct.unpack("<Q", c.read(pid, controller + 0x528, 8))[0]
        except Exception as error:
            print(f"manager read failed: {error}")
            return 1

        print(f"controller {controller:#x} manager {manager:#x}")
        if not ok(manager):
            return 2

        dump(c, pid, "manager+0x1780", manager + 0x1780, 0xA0)

        for priv in (0x17B0, 0x17C0, 0x17D0):
            try:
                view = struct.unpack("<Q", c.read(pid, manager + priv, 8))[0]
            except Exception:
                view = 0
            print(f"\npriv +{priv:#x} -> {view:#x}")
            if not ok(view):
                continue
            dump(c, pid, f"view {view:#x}", view, 0x80)

            try:
                inner = struct.unpack("<Q", c.read(pid, view, 8))[0]
            except Exception:
                inner = 0
            print(f"  deref -> {inner:#x}")
            if ok(inner):
                dump(c, pid, f"view deref {inner:#x}", inner, 0x80)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
