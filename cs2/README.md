# CS2

External ESP over the shared kernel driver. Nothing is injected; every read goes
through `sdk/km-sdk.h` to the driver, so handle stripping never applies and the
game stays untouched.

This folder is the reference for how a game is added. The ESP itself - window,
drawing, attach retry, frame loop, driver handshake - is **not here**, it is the
shared framework in `sdk/client`. A game supplies offsets and one provider.

## What a game supplies

| File | Role |
|---|---|
| `client/offsets.h` | every RVA, copied from `offsets.txt`, grouped by source struct |
| `client/provider.cpp` | entity resolution and validated reads, behind the `esp::Provider` interface |
| `client/main.cpp` | three lines: construct the provider, call `esp::Run` |
| `offsets.txt` | the raw dump the header is transcribed from |

Everything else comes from `sdk/client`: the click-through layered window, the
double buffer, box/health/range drawing, the `(hidden)` marker, the attach retry,
the re-attach on game loss, F9/Ctrl+C, and the driver open with protocol check.

## Rules the layout encodes

- **Offsets never appear in code.** They live in `offsets.h`, so a new dump is a
diff against one file instead of a hunt through the tree.
- **All game knowledge sits behind `provider.cpp`.** The renderer knows positions
  and health, not `0x1EB8`.
- **Every field is validated before it is drawn.** A failed sanity check drops the
  player; a broken offset shows as "0 tracked" instead of boxes at the origin.
- **Reads are batched.** One IOCTL per player per frame, not one per field.

## Adding another game

1. Copy this folder's `client/` shape: `offsets.h`, `provider.h/.cpp`, `main.cpp`.
2. Implement `esp::Provider` - the compiler lists every method still missing.
3. Set `RowMajorMatrix()` correctly. Wrong here mirrors and squashes every box
   while still producing plausible numbers, which is why it is explicit.
4. Copy `client/build.ps1`, change the output name, and add the step to the root
   `build.ps1` plus the `dist` copy.

## Offsets

Build specific. The set in `offsets.txt` is what the header was transcribed from;
after a game update the client still runs but tracks nothing, and the status line
says so. Re-run a dump and diff it against `client/offsets.h`.

## Build and run

Built by the root `build.ps1` on every run, and shipped as
`dist/kernmem-cs2.exe`. To build alone:

```
powershell -ExecutionPolicy Bypass -File cs2/client/build.ps1
```

Run the loader first (as admin), then the client. Enemies draw red, teammates
blue, with a health bar and range; unspotted players are marked `(hidden)`. F9 or
Ctrl+C stops it.

## Entity list

The list is chunked: a pointer table indexed by `index >> 9`, then a fixed stride
inside the chunk. Reading it as a flat array works up to index 511 and then reads
garbage, so `EntityAt` is the only place that does the arithmetic.

Handles are 32-bit with the entry index in the low 15 bits; they are masked, not
dereferenced directly. `dwLocalPlayerPawn` is the exception - it is a pointer, so
no hop and no masking.

World position lives in `CGameSceneNode`, so the node is dereferenced from the
pawn first. Reading it off the pawn returns a pointer's worth of garbage that
still parses as floats.
