"""
Demo CLI for the game reader.

    python demo.py offsets          show the table and its provenance
    python demo.py info             pid, base, patch, local player
    python demo.py heroes           list live heroes
    python demo.py heroes --watch   refresh every second

The driver must be loaded first (vgb-loader.exe, elevated). Nothing here needs
elevation and nothing here calls ReadProcessMemory — every read goes through the
driver.
"""

from __future__ import annotations

import argparse
import sys
import time

import offsets
import reader
import vgb


def cmd_offsets(_args) -> int:
    print(offsets.summary())

    missing = offsets.unverified()
    if missing:
        print(f"\n{len(missing)} entries still need a live check.")

    return 0


def cmd_info(_args) -> int:
    with reader.Game() as game:
        print(f"pid        : {game.pid}")
        print(f"patch      : {game.patch or 'unknown'}")
        print(f"table      : {offsets.PATCH} (scraped {offsets.SCRAPED})")
        print(f"image base : {game.base:#x}")

        try:
            header = game.read(game.base, 2)
        except vgb.VgbError as error:
            print(f"header read failed: {error}")
            return 1

        print(f"header     : {header.hex(' ').upper()}")

        if header != b"MZ":
            print("warning: image base does not start with MZ")

        player = game.local_player()

        if player:
            print(f"local player: {player:#x}")
        else:
            print("local player: null (not in a match, or the offset is wrong)")

        print(f"objects    : {game.object_count()}")

    return 0


def show_heroes(game: reader.Game) -> None:
    shown = 0

    for hero in game.heroes():
        state = "alive" if hero.alive else "dead"
        visible = "visible" if hero.visible else "hidden"
        position = ", ".join(f"{axis:.1f}" for axis in hero.position)

        print(
            f"  {hero.name or '?':<20} lvl {hero.level:<3} team {hero.team:<4} "
            f"{hero.health:>8.1f}/{hero.max_health:<8.1f} "
            f"({hero.health_percent:5.1f}%) mana {hero.mana:>7.1f} "
            f"{state:<5} {visible:<7} at ({position})"
        )
        shown += 1

    if not shown:
        print("  no heroes resolved — the offset table is probably unverified")


def cmd_heroes(args) -> int:
    with reader.Game() as game:
        if not args.watch:
            show_heroes(game)
            return 0

        try:
            while True:
                print(f"\n--- {time.strftime('%H:%M:%S')} ---")
                show_heroes(game)
                time.sleep(args.interval)
        except KeyboardInterrupt:
            print("\nstopped")

    return 0


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--allow-stale",
        action="store_true",
        help="run even when the client patch differs from the table",
    )

    sub = parser.add_subparsers(dest="command", required=True)

    sub.add_parser("offsets", help="show the offset table").set_defaults(func=cmd_offsets)
    sub.add_parser("info", help="show client facts").set_defaults(func=cmd_info)

    heroes = sub.add_parser("heroes", help="list live heroes")
    heroes.add_argument("--watch", action="store_true", help="refresh continuously")
    heroes.add_argument("--interval", type=float, default=1.0, help="seconds between refreshes")
    heroes.set_defaults(func=cmd_heroes)

    args = parser.parse_args(argv)

    try:
        return args.func(args)
    except offsets.StaleOffsets as error:
        print(f"stale offsets: {error}")
        return 2
    except offsets.OffsetsError as error:
        print(f"error: {error}")
        return 3
    except vgb.VgbError as error:
        print(f"driver error: {error}")
        print("is the driver loaded? run vgb-loader.exe (elevated)")
        return 4


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
