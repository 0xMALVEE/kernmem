# VanguardBypass

A kernel-assisted external memory interface for League of Legends.

Vanguard registers an object callback and strips `PROCESS_VM_READ`, `PROCESS_VM_WRITE` and `PROCESS_VM_OPERATION` from every handle another process opens to the game. `ReadProcessMemory` on `League of Legends.exe` therefore fails with `ERROR_PARTIAL_COPY`, which is why external script tooling dies at the first step.

This restores that access from kernel mode, two independent ways, and adds module hiding so injected code is not enumerated.

---

## How it works

| Path | Mechanism | Survives |
|------|-----------|----------|
| **A — primary** | `MmCopyVirtualMemory` against the target `EPROCESS`; no handle is ever opened | any handle-stripping version |
| **B — secondary** | own object pre-op callback at altitude `321000`, above Vanguard's, re-ORs the VM rights back in for the registered client PID | makes plain `ReadProcessMemory` work |

Neither path patches the game, so code-page hashing is unaffected.

Full explanation, with diagrams: [`docs/README.md`](docs/README.md).

---

## Layout

```
 driver/    kernel driver — the bypass
 loader/    user-mode mapper, loads vgb.sys through the signed Intel NAL driver
 sdk/       client library (C++ header, Python ctypes binding)
 scripts/   game reader and offset table (what an actual script is built on)
 client/    example client and smoke test
 dist/      shippable output
 docs/      engineering documentation
 tools/     maintenance utilities
 research/  primary sources; research/unknowncheats/ holds offset material
```

---

## Build

Requires MSVC 2022, Windows SDK 10.0.26100 and the matching WDK.

```powershell
.\build.ps1            # driver -> mapper-safety gate -> embed -> loader -> dist
.\build.ps1 -SkipDriver
.\build.ps1 -Clean
```

The build fails if the driver imports anything other than `ntoskrnl.exe`, has no relocation section, or carries a `/GS` cookie — all three make manual mapping impossible.

---

## Use

**Double-click `dist\vgb-loader.exe`. That is it.** It asks for admin once, loads the driver, starts the Riot client if needed, waits for you to enter a match, attaches, and draws. Nothing else is required and Python is optional.

Manual control, if you want it:

```powershell
.\dist\vgb-loader.exe status
.\dist\vgb-loader.exe load --client <your pid>
.\dist\vgb-loader.exe pid League of Legends.exe
.\dist\vgb-loader.exe overlay <pid>          # draws in game, F9 stops
.\dist\vgb-loader.exe read <pid> <address> 64
.\dist\vgb-loader.exe unload
```

### Overlay

`overlay <pid>` puts a click-through window over the game that draws your box, your attack range and every visible enemy with health and distance. It reads through the driver, so nothing is injected and the game is untouched. F9 stops it. It is also the fastest way to see whether the offsets still resolve — the status block reports `matrix` and `self` state.

See [docs/09-overlay.md](docs/09-overlay.md).

```python
import vgb

with vgb.Client() as c:
    c.require_version()
    pid = c.pid_by_name("League of Legends.exe")
    print(hex(c.main_base(pid)))
```

---

## Scripting

A script never calls `ReadProcessMemory` — it goes through the driver, so handle stripping never applies to it.

```python
from reader import Game

with Game() as game:
    for hero in game.heroes():
        print(hero.name, hero.level, hero.health, hero.visible)
```

Offsets are patch-stamped and refuse to run against a different client patch. See [docs/08-scripting.md](docs/08-scripting.md).

---

## Requirements

- Windows 10/11 x64
- Secure Boot may be on — the driver is never service-loaded, only mapped
- No WDK or compiler needed to *run* it

---

## Limitations

- One registered client pid at a time
- Writes to read-only game pages can fail; reads always work
- `iqvw64e.sys` ships as a third-party Intel binary
- Not a guarantee of permanent undetection — see [docs/02 §6](docs/02-vanguard-detection-model.md)
- Vanguard updates can move the callback altitude; `321000` is the constant to revisit first

---

## Licence

MIT for this project. `iqvw64e.sys` remains Intel's. See [LICENSE](LICENSE).
