"""
Live chain probe - reports what actually resolves, without drawing anything.

Every fixed link is tested and given a verdict, then a scan looks for a real
UWorld using the validity test VEX uses: a candidate P is only accepted when
P+PersistentLevel is a pointer L and L+OwningWorld points back at P.

    python probe.py
    python probe.py --scan-only

The driver must be loaded and a match running. Nothing here writes.
"""

from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "scripts"))
sys.path.insert(0, str(Path(__file__).resolve().parent.parent.parent / "sdk"))

import km  # noqa: E402

import reader  # noqa: E402

WORLD_CANDIDATES = (0xB490930, 0xB424930, 0x9F3F050, 0x24A04C0)

SCAN_START = 0xB400000
SCAN_END = 0xB500000
SCAN_CHUNK = 0x1000
MAX_CANDIDATES = 3000

PTR_MIN = 0x1000000000
PTR_MAX = 0x7FF000000000


def is_ptr(value):
    return value is not None and PTR_MIN <= value <= PTR_MAX and value % 8 == 0


class Probe:
    def __init__(self, game):
        self.game = game
        self.fails = 0

    def q(self, address):
        if not address:
            return None
        try:
            return struct.unpack("<Q", self.game.read(address, 8))[0]
        except Exception:
            return None

    def row(self, label, address, want_ptr=True):
        value = self.q(address)

        if value is None:
            verdict = "READ FAILED"
        elif value == 0:
            verdict = "zero"
        elif want_ptr and not is_ptr(value):
            verdict = "not a pointer"
        else:
            verdict = "ok"

        if verdict != "ok":
            self.fails += 1

        print(f"  {label:<26} {address:#018x}  {verdict}")
        return value or 0


def scan(game):
    base = game.base
    probe = Probe(game)
    tested = 0

    for delta in range(SCAN_START, SCAN_END, SCAN_CHUNK):
        try:
            raw = game.read(base + delta, SCAN_CHUNK)
        except Exception:
            continue

        for index in range(0, SCAN_CHUNK, 8):
            candidate = struct.unpack_from("<Q", raw, index)[0]
            if not is_ptr(candidate):
                continue

            tested += 1
            if tested > MAX_CANDIDATES:
                break

            level = probe.q(candidate + 0x0038)
            if not level or level == candidate:
                continue

            if probe.q(level + 0x00C0) == candidate:
                slot = base + delta + index
                print(f"  HIT slot {slot:#x} -> world {candidate:#x}")
                return candidate

        if tested > MAX_CANDIDATES:
            break

    print(f"  tested {tested} candidates, no world")
    return 0


def walk(probe, world):
    print()
    print("[world walk]")

    level = probe.row("world+PersistentLevel", world + 0x0038)
    if level:
        data = probe.row("level+ActorArray.Data", level + 0x00A0)
        count = probe.q(level + 0x00A8)
        print(f"  {'level+ActorArray.Num':<26} {level + 0x00A8:#018x}  {count}")

        if data and count:
            for index in range(min(int(count), 10)):
                actor = probe.q(data + index * 8)
                if not actor:
                    continue
                root = probe.q(actor + 0x0288)
                where = probe.q(root + 0x0170) if root else 0
                handler = probe.q(actor + 0x0C50)
                team = probe.q(actor + 0x00E8)
                print(
                    f"  actor[{index}] {actor:#x} root {root or 0:#x} "
                    f"team {team if team is not None else 0} pos {where or 0:#x}"
                )

    instance = probe.row("world+GameInstance", world + 0x01D8)
    if instance:
        players = probe.row("instance+LocalPlayers", instance + 0x0038)
        if players:
            controller = probe.row("players+PlayerController", players + 0x0030)
            if controller:
                probe.row("controller+AcknowledgedPawn", controller + 0x0510)
                manager = probe.row("controller+CameraManager", controller + 0x0520)
                if manager:
                    cache = probe.row("manager+CameraCache", manager + 0x17B0)
                    if cache:
                        try:
                            location = struct.unpack("<3d", probe.game.read(cache + 0x0010, 24))
                            fov = struct.unpack("<f", probe.game.read(cache + 0x0010 + 0x30, 4))[0]
                            print(f"  camera {location} fov {fov}")
                        except Exception as error:
                            print(f"  camera read failed: {error}")


def main(argv):
    parser = argparse.ArgumentParser(description="Valorant chain probe")
    parser.add_argument("--scan-only", action="store_true")
    options = parser.parse_args(argv)

    game = reader.Game(allow_stale=True)
    try:
        game.open()
    except Exception as error:
        print(f"open failed: {error}")
        return 1

    with game:
        print(f"pid {game.pid}  base {game.base:#x}  patch {game.patch}")
        probe = Probe(game)

        if not options.scan_only:
            print()
            print("[fixed offsets]")
            for name, offset in (
                ("GWorldState", 0x9F3F050),
                ("GWorldKey", 0x24A04C0),
                ("FNamePool", 0xB600040),
                ("FNameState", 0xB7DC100),
            ):
                probe.row(name, game.base + offset)

            key = probe.q(game.base + 0x24A04C0)
            try:
                state = list(struct.unpack("<7Q", game.read(game.base + 0x9F3F050, 56)))
                print(f"  decrypt -> {reader.decrypt_u_world(key, state) if key else 0:#x}")
            except Exception as error:
                print(f"  decrypt failed: {error}")

            print()
            print("[plain candidates]")
            for candidate in WORLD_CANDIDATES:
                value = probe.q(game.base + candidate)
                print(f"  {candidate:#x} -> {value if value is None else hex(value)}")

        print()
        print("[scan]")
        world = scan(game)

        if not world:
            return 2

        walk(Probe(game), world)

    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
