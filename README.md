# kernmem

A kernel-assisted external memory interface for protected games, with per-game tooling kept separate.

Vanguard registers an object callback and strips `PROCESS_VM_READ`, `PROCESS_VM_WRITE` and `PROCESS_VM_OPERATION` from every handle another process opens to a protected game. `ReadProcessMemory` on the game therefore fails with `ERROR_PARTIAL_COPY`, which is why external script tooling dies at the first step.

The driver restores that access from kernel mode, two independent ways, and adds module hiding so injected code is not enumerated. Everything above the driver is per game.

---

## How it works

| Path | Mechanism | Survives |
|------|-----------|----------|
| **A â€” primary** | `MmCopyVirtualMemory` against the target `EPROCESS`; no handle is ever opened | any handle-stripping version |
| **B â€” secondary** | own object pre-op callback at a per-install altitude (seeded 330000-390000, above the anticheat's), re-ORs the VM rights back in for the registered client PID | makes plain `ReadProcessMemory` work |

Neither path patches the game, so code-page hashing is unaffected.

Full explanation, with diagrams: [`docs/README.md`](docs/README.md).

---

## Layout

Shared, game-independent:

```
 driver/    kernel driver â€” the memory interface
 loader/    user-mode mapper, loads km.sys through the signed Intel NAL driver
 sdk/       client library (C++ header, Python ctypes binding, shared client framework)
 docs/      engineering documentation
 tools/     maintenance utilities
 dist/      shippable output
```

Per game â€” each folder holds that game's scripts, offsets, overlay and notes:

```
 league/    League of Legends
 valorant/  Valorant
 cs2/       Counter-Strike 2
```

The driver does not change per game.

The ESP itself is **not** per game. `sdk/client/` holds everything that is identical everywhere: the click-through layered window, the double buffer, box/health/range drawing, the attach retry, the re-attach on game loss, the stop key, and the driver open with protocol check. A game supplies only offsets and one `esp::Provider` implementation.

A game folder therefore holds `offsets.h`, `provider.h/.cpp` and a three-line `main.cpp`, plus its docs. `cs2/client/` is the reference — copy its shape. Python in a game folder is offset-recovery tooling only and is never in the frame loop.

---

## Build

Requires MSVC 2022, Windows SDK 10.0.26100 and the matching WDK.

```powershell
.\build.ps1            # driver -> gate -> embed -> loader -> clients -> dist
.\build.ps1 -SkipDriver
.\build.ps1 -Clean
```

The build fails if the driver imports anything other than `ntoskrnl.exe`, has no relocation section, or carries a `/GS` cookie â€” all three make manual mapping impossible.

---

## Use

**Double-click `dist\kernmem-loader.exe`. That is it.** It asks for admin once, loads the driver, starts the Riot client if needed, waits for you to enter a match, attaches, and draws. Nothing else is required and Python is optional.

Manual control, if you want it:

```powershell
.\dist\kernmem-loader.exe status
.\dist\kernmem-loader.exe load --client <your pid>
.\dist\kernmem-loader.exe pid League of Legends.exe
.\dist\kernmem-loader.exe overlay                # starts the client, F9 in it stops
.\dist\kernmem-loader.exe read <pid> <address> 64
.\dist\kernmem-loader.exe unload
```

### Overlay

`overlay` starts the per-game client (`kernmem-league.exe`, shipped beside the loader) and stays resident, because the client reads through the loader's mapping. The client puts a click-through window over the game that draws your box, your attack range and every visible enemy with health and distance. It reads through the driver, so nothing is injected and the game is untouched. F9 stops it. It is also the fastest way to see whether the offsets still resolve â€” the status block reports `matrix` and `self` state.

The loader itself draws nothing and knows no offsets. The ESP lives in the per-game client, on the shared framework in `sdk/client`, so adding a game never touches the loader.

See [league/docs/overlay.md](league/docs/overlay.md) and [cs2/README.md](cs2/README.md).

```python
import km

with km.Client() as c:
    c.require_version()
    pid = c.pid_by_name("League of Legends.exe")
    print(hex(c.main_base(pid)))
```

---

## Scripting

A script never calls `ReadProcessMemory` â€” it goes through the driver, so handle stripping never applies to it. Readers live inside the game folder, not at the root.

```python
from reader import Game

with Game() as game:
    for hero in game.heroes():
        print(hero.name, hero.level, hero.health, hero.visible)
```

Offsets are patch-stamped and refuse to run against a different client patch. See [league/docs/scripting.md](league/docs/scripting.md).

---

## Requirements

- Windows 10/11 x64
- Secure Boot may be on — the driver is never service-loaded, only mapped
- No WDK or compiler needed to *run* it
- No Python needed to *run* it either; Python is dev tooling only (offset scanners, `sdk/km.py`, debugger server)

---

## Limitations

- One registered client pid at a time
- Writes to read-only game pages can fail; reads always work
- `iqvw64e.sys` ships as a third-party Intel binary
- Not a guarantee of permanent undetection â€” see [docs/detection-model.md](docs/detection-model.md)
- Anticheat updates can move the callback altitude; the seeded range (330000-390000) sits above every process filter known today, but that is the assumption to revisit first

---

## Licence

MIT for this project. `iqvw64e.sys` remains Intel's. See [LICENSE](LICENSE).
