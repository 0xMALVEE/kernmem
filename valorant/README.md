# Valorant

External ESP over the shared driver. Nothing is injected; every read goes through
`sdk/vgb.py` to the kernel driver, so Vanguard's handle stripping never applies.

Valorant is UE5, so the reader handles the two things that makes different from
the League side:

- `FVector` / `FRotator` are **doubles**, and rotation is degrees.
- `UWorld` is **encrypted** - a seven-slot key state and a per-build key, run
  through `Decrypt_UWorld` in `scripts/reader.py`.

The chain is the standard engine walk: `GWorldKey` + `GWorldState` -> `UWorld` ->
`GameState` -> `PlayerArray` -> `PlayerState` -> `PawnPrivate` -> `RootComponent`
-> `RelativeLocation`, with the camera POV taken from the player controller's
camera manager for projection.

## Run

Driver loaded, in a match, game **windowed or borderless** (exclusive fullscreen
has no composable window to sit on top of).

```powershell
python scripts\demo.py            # chain smoke test: uworld, camera, players
python scripts\esp.py             # overlay, enemies only
python scripts\esp.py --all       # include teammates
```

`F9` pauses, `END` quits.

## Offsets

`scripts/offsets.py` is stamped **12.07** from the archived reversal thread in
`docs/reversal-structs-and-offsets.md`. Every entry carries provenance and a
`verified` flag; `python scripts\demo.py --offsets` prints the table and names
the unverified entries.

Expect drift on every patch. When the ESP draws nothing, in order:

1. `CameraCachePriv` - moved to `0x2630` on newer builds (offset `0x10` into the
   entry is `FMinimalViewInfo`).
2. `GameInstance` / `LocalPlayers` / `PlayerController` - unverified, and the
   local-pawn chain depends on them.
3. `PlayerArray` and `PlayerName` - unverified shapes.
4. `GWorldState` / `GWorldKey` - re-read from a fresh dump; if these are wrong
   `uworld()` returns 0 and nothing else can work.

`--allow-stale` bypasses the build check once you have confirmed the table still
resolves.

## Layout

```
scripts/offsets.py   patch-stamped offset table
scripts/reader.py    driver-backed reader, UWorld decrypt, projection
scripts/esp.py       click-through overlay ESP
scripts/demo.py      smoke test
```
