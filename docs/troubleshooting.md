# 06 — Troubleshooting

Each tree maps to a numbered failure branch in [03 Component flows](component-flows.md).

---

## 1. `load` fails immediately

```mermaid
flowchart TD
    A["load returns error"] --> B{"which status?"}
    B -->|STATUS_PRIVILEGE_NOT_HELD| C["not elevated"]
    C --> C1["relaunch elevated:<br/>Start-Process powershell -Verb RunAs"]
    B -->|STATUS_ALREADY_REGISTERED| D["\\Device\\Nal already held"]
    D --> D1["sc query for stale iqvw service"]
    D1 --> D2["kill the stale process, or reboot"]
    B -->|STATUS_INVALID_IMAGE_HASH| E["NtLoadDriver rejected the image"]
    E --> E1["confirm the path is absolute and \\??\\-prefixed"]
    E --> E2["confirm the dropped file is complete"]
```

### Check whether NAL is already resident

```powershell
Get-Service | Where-Object Name -match 'iqvw|nal'
Get-Process | Where-Object Name -match 'vgb|kdmapper'
```

If a service key exists but no process does, a previous run crashed mid-teardown:

```powershell
sc.exe stop <name>
sc.exe delete <name>
```

---

## 2. `load` succeeds but the device is not there

```mermaid
flowchart TD
    A["load reported success"] --> B{"status reports the device?"}
    B -->|no| C["DriverEntry returned non-success"]
    C --> D{"which part failed?"}
    D -->|IoCreateDevice| D1["name taken — another build already mapped<br/>→ unload, retry"]
    D -->|IoCreateSymbolicLink| D2["link exists — same cause"]
    D -->|ObRegisterCallbacks| D3["non-fatal: Path A still works,<br/>Path B silently disabled"]
    B -->|yes| E["healthy"]
```

`ObRegisterCallbacks` failing is **not** fatal by design; it only costs Path B. If `ReadProcessMemory` fails while the SDK read works, that is the signature of a denied callback — check the altitude.

---

## 3. Reads fail with `STATUS_PARTIAL_COPY`

```mermaid
flowchart TD
    A["PARTIAL_COPY"] --> B{"is the address mapped in the game?"}
    B -->|no| B1["re-resolve the base, recompute the offset"]
    B -->|yes| C{"is it a write to a read-only page?"}
    C -->|yes| C1["expected — .text is not writable through<br/>MmCopyVirtualMemory"]
    C -->|no| D{"is the size above 0x10000?"}
    D -->|yes| D1["clamped; loop in 64 KiB chunks"]
    D -->|no| E["address straddles a page boundary into<br/>unmapped memory → shrink the read"]
```

A useful sanity check is reading the PE header at the image base — `MZ` at `base`, `PE\0\0` at `base + [base+0x3C]`. If that fails, the address is wrong, not the driver.

---

## 4. `ReadProcessMemory` fails but the SDK read works

That is Path B being off.

| Cause | Check | Fix |
|-------|-------|-----|
| Client pid not registered | `IOCTL_VGB_SET_CLIENT_PID` was never sent | send it from the working process |
| Callback not registered | DriverEntry returned non-success for `ObRegisterCallbacks` | check for an altitude collision |
| Altitude below Vanguard | rights stripped after we re-added them | raise the altitude above `320000` |
| Registered from another process | pid belongs to a different exe | re-register from the caller |

---

## 5. Game process not found

```mermaid
flowchart TD
    A["pid 0"] --> B{"is the game actually running?"}
    B -->|no| B1["start it; the process only exists in a match"]
    B -->|yes| C{"exact image name?"}
    C -->|no| C1["must be exactly League of Legends.exe<br/>(15-char image-name limit)"]
    C -->|yes| D{"was the runtime export available?"}
    D -->|no| D1["the query fallback is in use — that is fine"]
    D -->|yes| E["enumeration issue → try GET_MAIN_BASE with a known pid"]
```

The kernel image name is truncated to 15 characters, so matching is on the prefix. `LeagueClient.exe` and `League of Legends.exe` are different processes — use the game one.

---

## 6. `unload` leaves `\Device\Nal` busy

```mermaid
flowchart TD
    A["next load says ALREADY_REGISTERED"] --> B{"did unload complete?"}
    B -->|no| C["teardown aborted midway"]
    C --> C1["run unload again; it is idempotent"]
    C1 --> C2["still busy → sc stop / sc delete, then reboot if needed"]
    B -->|yes| D["another tool holds NAL"]
    D --> D1["close kdmapper or any other NAL user"]
```

Teardown is strictly reverse order: unhide → unregister callback → delete link → delete device → free image → unload NAL → delete key and file. Skipping a step is the usual cause.

---

## 7. Blue screens

| Bugcheck | Likely cause |
|----------|--------------|
| `0x50` `PAGE_FAULT_IN_NONPAGED_AREA` | dereferencing a bogus pointer outside `__try` |
| `0x3B` `SYSTEM_SERVICE_EXCEPTION` | PEB walk without `KeStackAttachProcess` |
| `0x8E` `KERNEL_MODE_EXCEPTION_NOT_HANDLED` | unguarded dereference in the dispatch path |
| `0xC4` verifier | pool misuse — check alloc/free pairing |
| `0x7E` | missing `.pdata` for `__try/__except` |

Crash dumps live in `%SystemRoot%\MEMORY.DMP` or `%LOCALAPPDATA%\CrashDumps`. Load in WinDbg and run `!analyze -v`.

---

## 8. Version skew

`STATUS_INVALID_DEVICE_REQUEST` on a call that used to work means the loader and driver disagree about the IOCTL table. `GET_VERSION` exists to catch this before real work. Rebuild both from the same tree — the root build refuses to package a diverged shared header.

---

## 9. Diagnostic ladder

Run in order and stop at the first failure:

```powershell
.\dist\vgb-loader.exe status                 # device present? version?
.\dist\vgb-loader.exe pid League of Legends.exe
.\dist\vgb-loader.exe mainbase <pid>
.\dist\vgb-loader.exe read <pid> <base> 2    # expect 4D 5A
.\dist\vgb-loader.exe attach <your pid>
.\dist\vgb-loader.exe read <pid> <base> 2    # now also via ReadProcessMemory
```

---

Next: [07 Production checklist](production-checklist.md)
