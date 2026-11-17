# 09 — Conventions

> Rules for this repository. They exist because the root accumulated ~50 one-off files and every build dropped `.obj` files into it.

---

## 1. The root is for four things only

```
README.md      what this is
LICENSE        licence
build.ps1      the one entry point
.gitignore     what is not tracked
```

Nothing else belongs there. Decide where a file goes before you create it:

```mermaid
flowchart TD
    A["new file"] --> B{"is it documentation?"}
    B -->|yes| C["docs/"]
    B -->|no| D{"is it a one-off script?"}
    D -->|yes| E["scripts/\nroot = Split-Path -Parent $PSScriptRoot"]
    D -->|no| F{"is it captured output?"}
    F -->|yes| G["logs/"]
    F -->|no| H{"is it compiler output?"}
    H -->|yes| I["build/ (ignored)"]
    H -->|no| J["is it shippable?"}
    J -->|yes| K["dist/"]
    J -->|no| L{"reconsider"}
    C --> M["index it in docs/README.md"]
    E --> N["never write \\$root"]
```

If a file is not one of those, it goes somewhere:

| Kind | Home |
|---|---|
| One-off diagnostic / probe scripts | `scripts/` |
| Captured output, logs, dumps | `logs/`, `dumps/` |
| Documentation | `docs/` |
| Compiler output | `build/` (ignored) |
| Shippable layout | `dist/` (tracked, generated) |

### Adding a script

Put it in `scripts/` and resolve the repository root **explicitly**:

```powershell
$root = Split-Path -Parent $PSScriptRoot   # correct
$root = $PSScriptRoot                      # wrong: that is scripts/
```

Never write `\$root` — it becomes a literal backslash argument.

### Never write output to the root

Redirect to `logs/`:

```powershell
$out = Join-Path $root 'logs/client.out.txt'
```

### Never let the compiler write to the root

Every `cl` invocation needs an explicit `/Fo`, or objects land in the current directory:

```powershell
"/Fo`"$OutDir\\`""
```

---

## 2. Git hygiene

- `*.obj` and `dist/*.old` are ignored. `.old` files appear when a rebuild runs against a live session — a running exe can only be renamed, not overwritten.
- `build/` is ignored except `*.py` and `*.md`, which are the committed offset scanners.
- `loader/res/*.h` **is** tracked: a fresh clone must build without running the driver stage first.
- Crash dumps in `dumps/bsod/` are committed — they are evidence, and they are what a fix is justified against.

---

## 3. Driver code

### `driver/km_shared.h` and `sdk/km_shared.h` are the same file

The build hashes both and **fails on a mismatch**. Any struct change is made in both, byte for byte.

### Guard, do not assume

A mapped image has no safety net. The rule is: **screen a pointer before dereferencing it**, and treat SEH as the backstop rather than the guard.

```c
if (!peb || !KmUserPointer((unsigned __int64)peb))   // guard
    return STATUS_NOT_FOUND;
__try { ... } __except (EXCEPTION_EXECUTE_HANDLER) { } // backstop
```

- `KmUserPointer` for user-mode addresses, `MmIsAddressValid` for kernel pages.
- `KmAddressCopyable` before any raw copy, because `MmCopyVirtualMemory` does **not** probe kernel-range addresses — it `memcpy`s and bugchecks `0x50`.
- Every `__try` needs the header skeleton and module entry in place first, or it escalates.

### Allocate with slack

`SizeOfImage` is section-aligned but copies are exact. An exact-fit pool block leaves nothing behind the last byte, and one byte over lands on the next chunk's header. Use `+ 0x2000` and bounds-check the writes.

### Drain every failure path

Any `return` from `DriverEntry` after state exists must run the same teardown `KmUnload` does. A returned failure lets the loader free the image while the worker still runs inside it.

### Order for safety, not for looks

Nothing may fault before its guard is up: header + publish, then device, then shared block, then the callback. On teardown, reverse it.

---

## 4. Loader code

- **Match on bytes, not names.** The sweep identifies our drops by exact content so an unrelated `.sys` is never touched.
- **Nothing may be left on disk after a crash.** Delete or move a file as soon as the device answers — everything past that point can bugcheck, and a bugcheck runs no cleanup.
- **`__try` needs its own frame.** C2712: it cannot live in a function with unwinding locals such as `std::wstring`. Wrap it in a small helper.
- **A silent failure is a bug.** Log the reason and the value, not just that something failed.
- **Check `GetLastError` / status.** `_wremove` failing for the whole session looked like the sweep never running.

---

## 5. Documentation

- Every page lives in `docs/` and is indexed in `docs/README.md`.
- Diagrams are **mermaid**, and they must match the code. A stale diagram is worse than none.
- Every hazard gets an entry in the hazard index in `driver-internals.md` — symptom, cause, guard. That table is the most useful page here.
- Reference real symbols and line numbers, so a reader can verify a claim instead of trusting it.

---

## 6. Commits

- Subject in the imperative, scoped: `driver: …`, `loader: …`, `docs: …`.
- The body explains **why**, and names the bugcheck code and the evidence (a dump, a log line).
- One concern per commit. A crash fix and an unrelated client do not share one.
- Never commit a build with an untested claim in the message; say what is unverified.
