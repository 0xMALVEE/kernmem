"""
Valorant smoke test - proves the chain without drawing anything.

Resolves the client, decrypts UWorld, reads the camera and lists every player
with name, team, health and position. Run it in a match with the driver loaded.

    python demo.py
    python demo.py --allow-stale      # build does not match the offset stamp
    python demo.py --offsets          # print the table and exit
"""

from __future__ import annotations

import argparse
import math
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import offsets  # noqa: E402
import reader  # noqa: E402


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description="Valorant chain smoke test")
    parser.add_argument(
        "--allow-stale", action="store_true",
        help="continue even when the build does not match the offset stamp",
    )
    parser.add_argument(
        "--offsets", action="store_true",
        help="print the offset table with its provenance and exit",
    )
    options = parser.parse_args(argv)

    if options.offsets:
        print(offsets.summary())
        stale = offsets.unverified()
        print(f"\n{len(stale)} entries are unverified: {', '.join(stale)}")
        return 0

    try:
        game = reader.Game(allow_stale=options.allow_stale)
        game.open()
    except Exception as error:  # noqa: BLE001
        print(f"open failed: {error}")
        return 1

    with game:
        print(f"pid        {game.pid}")
        print(f"base       {game.base:#x}")
        print(f"patch      {game.patch or 'unreadable'} (table stamped {offsets.PATCH})")

        world = game.uworld()
        print(f"uworld     {world:#x}" if world else "uworld     0 (decrypt failed)")

        camera = game.camera()
        if camera is None:
            print("camera     unresolved - check CameraCachePriv")
        else:
            location, rotation, fov = camera
            print(
                "camera     "
                f"({location[0]:.1f}, {location[1]:.1f}, {location[2]:.1f}) "
                f"pitch {rotation[0]:.1f} yaw {rotation[1]:.1f} roll {rotation[2]:.1f} "
                f"fov {fov:.1f}"
            )

        local = game.local_pawn()
        print(f"local pawn {local:#x}" if local else "local pawn 0 (chain unresolved)")

        print()
        print(f"{'address':<18}{'team':<6}{'hp':<12}{'dist':<8}name")
        count = 0

        for player in game.players():
            distance = math.dist(camera[0], player.position) / 100.0 if camera else 0.0
            flag = " <- local" if player.address == local else ""
            print(
                f"{player.address:#018x}{player.team:<6}"
                f"{player.health:>5.0f}/{player.max_health:<5.0f}"
                f"{distance:>6.0f}m  {player.name}{flag}"
            )
            count += 1

        print(f"\n{count} players")

    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
