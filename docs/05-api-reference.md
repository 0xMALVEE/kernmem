# 05 — API reference

All IOCTLs are `METHOD_BUFFERED` with `FILE_ANY_ACCESS` on device type `0x8000`. Input and output share the system buffer: **input is read from the start, output is written to the start**. For reads, the reply overwrites the request header, so a caller must copy the result out before reusing the buffer.

---

## 1. Device names

| Kind | Value |
|------|-------|
| Kernel device | `\Device\VanguardBypass` |
| Symbolic link | `\DosDevices\VanguardBypass` |
| User-mode path | `\\.\VanguardBypass` |
| Protocol version | `3` |

---

## 2. IOCTL table

Codes are `CTL_CODE(0x8000, fn, METHOD_BUFFERED, FILE_ANY_ACCESS)` = `0x80000000 | (fn << 2)`.

| IOCTL | Function | Code | Input | Output |
|-------|----------|------|-------|--------|
| `IOCTL_VGB_GET_VERSION` | protocol probe | `0x80002000` | none | `ULONG` version |
| `IOCTL_VGB_SET_CLIENT_PID` | pin client for handle restore | `0x80002004` | `VGB_PID_REQ` | none |
| `IOCTL_VGB_GET_PID_BY_NAME` | resolve image name → pid | `0x80002008` | `VGB_NAME_REQ` | `VGB_PID_REQ` |
| `IOCTL_VGB_GET_MODULE_BASE` | module base by name | `0x8000200C` | `VGB_NAME_REQ` | `uint64` |
| `IOCTL_VGB_READ` | read target memory | `0x80002010` | `VGB_MEM_REQ` | bytes |
| `IOCTL_VGB_WRITE` | write target memory | `0x80002014` | `VGB_MEM_REQ` + payload | none |
| `IOCTL_VGB_ENUM_MODULES` | list modules | `0x80002018` | `VGB_PID_REQ` | `VGB_MODULE_LIST` |
| `IOCTL_VGB_HIDE_MODULE` | unlink from LDR | `0x8000201C` | `VGB_BASE_REQ` | none |
| `IOCTL_VGB_UNHIDE_MODULE` | relink into LDR | `0x80002020` | `VGB_BASE_REQ` | none |
| `IOCTL_VGB_GET_MAIN_BASE` | image base shortcut | `0x80002024` | `VGB_PID_REQ` | `uint64` |

---

## 3. Structures (x64 layout)

```c
typedef struct _VGB_MEM_REQ          // 16 bytes
{
    unsigned long  Pid;      // 0
    unsigned long  Size;     // 4   capped at 0x10000
    unsigned __int64 Address; // 8
} VGB_MEM_REQ;

typedef struct _VGB_NAME_REQ         // 200 bytes
{
    unsigned long Pid;                  // 0
    unsigned long Pad;                  // 4
    wchar_t       Name[96];             // 8
} VGB_NAME_REQ;

typedef struct _VGB_PID_REQ          // 8 bytes
{
    unsigned long Pid;
    unsigned long Pad;
} VGB_PID_REQ;

typedef struct _VGB_BASE_REQ         // 16 bytes
{
    unsigned long  Pid;
    unsigned long  Pad;
    unsigned __int64 Base;
} VGB_BASE_REQ;

typedef struct _VGB_MODULE_INFO      // 208 bytes
{
    unsigned __int64 Base;
    unsigned long    SizeOfImage;
    unsigned long    Pad;
    wchar_t          Name[96];
} VGB_MODULE_INFO;

typedef struct _VGB_MODULE_LIST      // 8 + N*208
{
    unsigned long    Count;
    unsigned long    Truncated;
    VGB_MODULE_INFO  Entries[1];
} VGB_MODULE_LIST;

typedef struct _VGB_INIT_PARAMS      // 8 bytes, DriverEntry arg 2
{
    unsigned long Magic;      // VGB_INIT_MAGIC 0x42475631
    unsigned long ClientPid;
} VGB_INIT_PARAMS;
```

`VGB_MAX_XFER = 0x10000` — a caller asking for more is clamped, and the return length tells it how much actually moved.

---

## 4. Status codes

| NTSTATUS | Value | Meaning | Recovery |
|----------|-------|---------|----------|
| `STATUS_SUCCESS` | `0x0` | ok | — |
| `STATUS_BUFFER_TOO_SMALL` | `0xC0000023` | input/output too small for the struct | size the buffer correctly |
| `STATUS_INVALID_PARAMETER` | `0xC000000D` | pid not found | re-resolve the pid |
| `STATUS_NOT_FOUND` | `0xC0000225` | module not in LDR | check the name and that the game is running |
| `STATUS_PARTIAL_COPY` | `0x8000001D` | address invalid or page read-only | fix the address; writes to `.text` need a different path |
| `STATUS_INVALID_DEVICE_REQUEST` | `0xC0000010` | unknown IOCTL / version skew | rebuild loader and driver together |

`DeviceIoControl` returns `FALSE` and `GetLastError()` carries the Win32 mapping of these.

---

## 5. C++ SDK

```cpp
#include "vgb-sdk.h"

vgb::Client vgb;
if (!vgb.Open())                    return 1;   // device missing → not loaded
if (vgb.Version() != 3)             return 2;   // version skew

uint32_t pid = vgb.PidByName("League of Legends.exe");
if (!pid)                            return 3;

vgb.SetClientPid(GetCurrentProcessId());       // enables Path B for this process

uint64_t base = vgb.MainBase(pid);
std::vector<uint8_t> buf(64);
vgb.Read(pid, base, buf.data(), buf.size());    // Path A — works regardless
```

Reads larger than 64 KiB are split by the SDK; callers pass any size.

---

## 6. Python binding

```python
import vgb

with vgb.Client() as c:
    c.require_version(3)
    pid  = c.pid_by_name("League of Legends.exe")
    base = c.main_base(pid)
    data = c.read(pid, base, 64)
    c.write(pid, base + 0x10, b"\x90" * 4)
    for m in c.modules(pid):
        print(hex(m.base), m.size, m.name)
```

`vgb.py` uses `ctypes` and the same struct layouts. `require_version` raises on mismatch rather than failing obscurely later.

---

## 7. Caller contract

1. **Probe the version first.** `GET_VERSION` is the cheapest possible health check and the only way to detect a stale pairing.
2. **Resolve the pid per session.** Pids change; a cached one gives `STATUS_INVALID_PARAMETER`.
3. **Call `SET_CLIENT_PID` only from the process that will do the work.** One pid at a time; the last caller wins.
4. **Never exceed `VGB_MAX_XFER` in one call** — the SDK handles it, hand-rolled callers must too.
5. **Unhide before `FreeLibrary`.** Otherwise the loader tears down an entry that is not in any list.

---

## 8. Error-handling example

```cpp
switch (vgb.LastStatus())
{
case STATUS_INVALID_PARAMETER:
    pid = vgb.PidByName("League of Legends.exe");
    break;
case STATUS_PARTIAL_COPY:
    // address wrong, or read-only page for a write
    break;
case STATUS_INVALID_DEVICE_REQUEST:
    vgb.Close();
    // rebuild: loader/driver skew
    break;
}
```

---

Next: [06 Troubleshooting](06-troubleshooting.md)
