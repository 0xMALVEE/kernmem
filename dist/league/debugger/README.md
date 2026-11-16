# Live debugger

A local viewer for the overlay's numbers. It polls the driver and shows every raw value plus all projection conventions **side by side**, so the right position offset and matrix convention can be picked by looking at them instead of guessing.

Nothing here injects anything. Reads go through the driver device, same as the overlay.

## Run

Two terminals, driver already loaded and you in a match:

```powershell
python server.py                  # api on 127.0.0.1:8777
cd ui; npm run dev        # ui on 127.0.0.1:5273
```

Open http://127.0.0.1:5273

`start.ps1` does both.

## What it shows

- **raw** — device path, client size, local player, team, attack range, camera offset, view checksum, signature hits
- **position candidates** — every candidate offset with live x/y/z, so a stale one is obvious
- **heroes** — index, team, name, address and position
- **views** — one panel per projection convention, each drawing your ring, your dot and every hero
- **scan** — brute-forces every plausible triple in your object and ranks it by distance from screen centre

## Reading it

`view crc` changing means the camera is live. If it is frozen, the game is not focused and nothing else is meaningful — the browser takes focus, so click back into the game and the numbers resume.

In the views, the panel where your dot sits at the crosshair with a **circular** ring and the enemy on a sensible side is the correct convention. `REFERENCE w>0` is the older project's exact form; if every other panel is blank and only that one draws, the matrix needs `w` flipped.

## Notes

- The api server is standard library only, no dependencies.
- `position candidates` and `scan` exist because the field offsets move on every patch; run `scan` first after one.
