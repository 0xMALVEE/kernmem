# Valorant

External ESP over the shared driver. Nothing is injected; every read goes through
`sdk/km.py` to the kernel driver, so Vanguard's handle stripping never applies.

**Status: radar works, boxes do not.** The camera POV is unresolved on the build
this was recovered against (5.3) â€” every documented camera path reads null or
denormal â€” so the overlay draws a top-down radar instead of screen boxes. The
box renderer is in place and engages automatically once `camera()` returns a POV.

## Offsets actually in use

All read live and repeatable against the running client:

| Value | Offset | Note |
|---|---|---|
| actor array (module static) | `0xD81F3B0` | `TArray<AActor*>`: Data `+0`, Num `+8` |
| root component | `0x290` | |
| relative location | `0x170` | **FVector as 3 doubles** (UE5 LWC) |
| acknowledged pawn | `0x518` | dumps say `0x4B0`/`0x510`; only `0x518` answers here |

Known-broken on this build: the world chain (`GWorld` reads 0, the level's own
actor array reads empty) â€” which is why the module static is used directly â€” and
the camera (see above). Health/team/name offsets are in the table but unverified.

## Run

Driver loaded, in a match, game **windowed or borderless**:

```powershell
python scripts\demo.py              # chain smoke test: actors, positions
python scripts\esp.py               # radar, 80 m
python scripts\esp.py --range 20000 # wider
```

`F9` pauses, `END` quits.

The pawn field is unreliable, so the radar centres on you when your position
reads cleanly and on the player centroid otherwise â€” the HUD says which.

## Offsets file

`scripts/offsets.py` is stamped `5.3` and carries provenance plus a `verified`
flag per entry. `python scripts\demo.py --offsets` prints the table and names the
unverified entries. `--allow-stale` bypasses the build check.

## Layout

```
scripts/offsets.py   stamped offset table
scripts/reader.py    driver-backed reader
scripts/esp.py       click-through radar overlay
scripts/demo.py      smoke test
scripts-test/        throwaway probes used to recover the offsets
```

The dumps this was recovered from are in `docs/`.
