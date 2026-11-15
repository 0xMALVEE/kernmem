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
 client/    example client and smoke test
 dist/      shippable output
 docs/      engineering documentation
 tools/     maintenance utilities
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

Elevation is required to load (it needs `SeLoadDriverPrivilege`), not to use.

```powershell
.\dist\vgb-loader.exe status
.\dist\vgb-loader.exe load --client <your pid>
.\dist\vgb-loader.exe pid League of Legends.exe
.\dist\vgb-loader.exe read <pid> <address> 64
.\dist\vgb-loader.exe unload
```

```python
import vgb

with vgb.Client() as c:
    c.require_version()
    pid = c.pid_by_name("League of Legends.exe")
    print(hex(c.main_base(pid)))
```

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
