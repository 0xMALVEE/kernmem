"""
Valorant smoke test - proves the chain without drawing anything.

Resolves the client, walks the actor array and prints position, health, team and
name for every actor, plus which one is local. Run it in a match with the driver
loaded.

    python demo.py
    python demo.py --allow-stale
    python demo.py --offsets
"""

from __future__ import annotations

import argparse
import math
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import offsets  # noqa: E402
import reader  # noqa: E402


def main(argv: list) -> int:
    parser = argparse.ArgumentParser(description='Valorant chain smoke test')
    parser.add_argument('--allow-stale', action='store_true')
    parser.add_argument('--offsets', action='store_true')
    options = parser.parse_args(argv)

    if options.offsets:
        print(offsets.summary())
        stale = offsets.unverified()
        print(f'\n{len(stale)} unverified: {", ".join(stale)}')
        return 0

    try:
        game = reader.Game(allow_stale=options.allow_stale)
        game.open()
    except Exception as error:  # noqa: BLE001
        print(f'open failed: {error}')
        return 1

    with game:
        print(f'pid {game.pid}  base {game.base:#x}  patch {game.patch}')

        actors = list(game.actors())
        print(f'{len(actors)} actors')

        local = game.local()
        print(f'local: {local.address:#x}' if local else 'local: unresolved')

        origin, marked = game.centre()
        print()
        print(f'{"address":<20}{"team":<6}{"hp":<14}{"dist":<9}name')

        for actor in actors:
            distance = math.dist(origin, actor.position) / 100.0 if origin else 0.0
            flag = ' <- local' if actor.address == marked else ''
            where = f'{actor.position[0]:.0f},{actor.position[1]:.0f}'
            print(
                f'{actor.address:#018x}{actor.team:<6}'
                f'{actor.health:>5.0f}/{actor.max_health:<5.0f}'
                f'{distance:>7.0f}m  {actor.name or "?"}  {where}{flag}'
            )

    return 0


if __name__ == '__main__':
    raise SystemExit(main(sys.argv[1:]))
