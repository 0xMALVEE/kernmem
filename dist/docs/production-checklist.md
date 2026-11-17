# 07 â€” Production checklist

This is the honest status document. It separates **must work for the product to function at all** from **nice to have**, and gives the order.

---

## 1. Priority table â€” answering "do we need all of it?"

### Tier 0 â€” without these there is no product

| Item | Why | State |
|------|-----|-------|
| `driver/main.c` + build | the bypass itself | **done, builds, mapper-safe** |
| `dist/iqvw64e.sys` | only load path under Secure Boot | **done, extracted** |
| `loader/nal.cpp` | speaks to the mapping host | **done** |
| `loader/pe.cpp` | parse/relocate/resolve the image | **done** |
| `loader/mapper.cpp` | allocate, map, call `DriverEntry` | **done** |
| `loader/main.cpp` | the UI and CLI the user runs | **done** |
| `build.ps1` (root) | reproducible build + dist assembly | **done** |
| `loader/build.ps1` | builds the loader | **done** |

Eight items. Nothing here is optional â€” a driver that cannot be loaded and a loader that cannot map are a library, not a product.

### Tier 1 â€” needed for the product to be *good*

| Item | Why | State |
|------|-----|-------|
| `sdk/km-sdk.h` | clients stop hand-rolling IOCTL buffers | **done** |
| `sdk/km.py` | Python is the dominant LoL scripting language | **done** |
| `league/client/` example | proof it works end to end | **done** |
| Elevated-launch helper | `load` is useless without it | **done** (UI shows elevation state) |
| `status` / `unload` verbs | operator sanity and clean teardown | **done** |
| `league/client/` (C++ ESP on `sdk/client`) | visible proof the chain works; draws self, range, enemies | **done, builds** |
| Live smoke test | nothing has run in a kernel yet | **outstanding** |

### Tier 2 â€” needed to *sell* it

| Item | Why | State |
|------|-----|-------|
| Randomised device name | fixed name is a fingerprint | **done** - per install, device type derived from it |
| Version handshake on both sides | already designed, needs enforcement | partial |
| LICENSE + EULA | commercial distribution | **missing** |
| `dist/` layout + docs copy | what a customer downloads | partial |
| Antivirus notes | NAL and unsigned kernel code trigger heuristics | **missing** |

### Tier 3 â€” genuinely optional

| Item | Note |
|------|------|
| EV certificate + signed `km.sys` | removes the NAL dependency entirely; real money and a signing identity |
| Kernel page hashing defeat | only matters against active memory scanning |
| Pool-tag randomisation | cosmetic until someone greps for `'BGVb'` |
| Multi-client support | one pid is enough for a single script |
| GUI | the CLI is sufficient |

### Direct answer

**No, not all of it.** Tier 0 and Tier 1 are required â€” that is the difference between a design and a product. Tier 2 is what makes it sellable. Tier 3 can ship later or never.

---

## 2. Definition of done for Tier 0

- [ ] `build.ps1` runs clean from a clean tree
- [ ] `build\km.sys` passes the mapper-safety check in [04](build-and-load.md)
- [ ] `dist\kernmem-loader.exe` exists and `status` reports "not loaded"
- [ ] elevated `load` returns success and `status` reports version 3
- [ ] `read <pid> <base> 2` returns `4D 5A`
- [ ] `unload` returns success and `\Device\Nal` is free again
- [ ] second `load` immediately after `unload` succeeds (idempotency proof)

---

## 3. Definition of done for Tier 1

- [ ] C++ example compiles against `sdk/km-sdk.h` and reads the PE header
- [ ] Python example does the same through `km.py`
- [ ] `attach --client` makes plain `ReadProcessMemory` succeed
- [ ] `hide` removes a test DLL from `Module32First`, `unhide` restores it
- [ ] unhide-before-`FreeLibrary` proven by a clean process exit

---

## 4. Release checklist

- [ ] version bumped in `km_shared.h` and echoed by the loader
- [ ] driver rebuilt, not reused
- [ ] mapper-safety check passed
- [ ] `dist/` assembled: loader, sdk, `iqvw64e.sys`, docs, LICENSE
- [ ] SHA-256 of every shipped file recorded
- [ ] clean-machine test: no WDK, no MSVC â€” loader must still run
- [ ] clean-machine test: Secure Boot on, HVCI off
- [ ] clean-machine test: HVCI on
- [ ] uninstall path verified (unload + key deleted + temp file gone)
- [ ] after a successful map, the service key and the dropped image are gone while the driver still answers
- [ ] `MmUnloadedDrivers`, the PiDDB cache and the ci.dll hash bucket all report clear for the host image
- [ ] antivirus false-positive sweep on a fresh Defender definition

---

## 5. Known limitations to state in the store

1. **Windows 10/11 x64 only.** The PEB layout is x64-specific and offset-asserted.
2. **Elevation required to load.** Not required to use.
3. **One client pid at a time.**
4. **Writes to read-only game pages can fail.** Reads always work.
5. **`iqvw64e.sys` ships as a third-party binary.** Intel-signed, but it is Intel's file — and the host is still identifiable as NAL by its IOCTL surface even after the file is deleted.
6. **Not a guarantee of permanent undetection.** See [02 Â§6](detection-model.md).
7. **Patch-day risk.** Vanguard updates can move the callback altitude; `321000` is the constant to revisit first.

---

## 6. Maintenance triggers

| Trigger | Action |
|---------|--------|
| Vanguard major update | re-test Path B; adjust altitude; re-run the smoke test |
| Windows build change | re-verify PEB offsets (the `C_ASSERT`s should catch it) |
| `PsGetNextProcess` export appears/disappears | fallback already covers both â€” verify both paths |
| Intel pulls `iqvw64e.sys` | re-extract from a current driver package |
| IOCTL surface change | bump `KM_PROTOCOL_VERSION`, update `docs/05` |
| Windows build change (ci.dll) | re-verify the `g_KernelHashBucketList` pattern; a miss logs a warning and skips the scrub rather than failing |

---

## 7. Test matrix

| Case | Expected |
|------|----------|
| Secure Boot on, HVCI off | manual map, works |
| Secure Boot on, HVCI on | manual map, works |
| Game not running | `pid` returns 0, `GET_MAIN_BASE` â†’ `STATUS_INVALID_PARAMETER` |
| Read at unmapped address | `STATUS_PARTIAL_COPY`, no bugcheck |
| Write to `.text` | `STATUS_PARTIAL_COPY`, expected |
| Read 1 MiB in one call | clamped to 64 KiB, looped by the SDK |
| Two `load` in a row | second â†’ `STATUS_ALREADY_REGISTERED` |
| `unload` twice | second â†’ success (idempotent) |
| Hide then unhide then `FreeLibrary` | clean exit |
| Hide then `FreeLibrary` without unhide | documented fault |

---

Back to [index](README.md)
