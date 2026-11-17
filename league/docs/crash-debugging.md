# Crash debugging

A bugcheck (BSOD) leaves almost nothing behind on its own. This document covers what the loader now records so a crash is diagnosable instead of a guess, and how to read the system's own record of it.

---

## 1. The crash log

Every run of `kernmem-loader.exe` writes a line-flushed log to:

```
%TEMP%\km-logs\<yyyyMMdd-HHmmss>.log
```

The path is printed as the first line of output, so you never have to hunt for it.

**Every line is `fflush`ed and then `FlushFileBuffers`ed immediately.** That is the whole point: when the machine bugchecks, the CRT buffer and the OS cache are both lost, so only a forced disk write keeps the lines that were already produced. The last line in the file is the step that was executing. A log that is the right size but all zero bytes means the flush did not reach the disk.

### What gets logged

| Marker | Meaning |
|--------|---------|
| `kernmem-loader started` | process start, argument count |
| `vulnerable driver dropped` | NAL image written to `%TEMP%` |
| `kernel base resolved / from EnumDeviceDrivers / by IDT scan` | which resolver route won |
| `patching NtAddAtom at 0xâ€¦ (page offset 0xâ€¦)` | patch site **and its page offset** â€” offsets near `0xFFF` are the risky ones |
| `entering kernel function 0xâ€¦ with N arg(s)` | immediately before every kernel call |
| `kernel function returned 0xâ€¦` | immediately after |
| `calling DriverEntry at 0xâ€¦` | the mapped entry, with object and client pid |
| `calling DriverUnload` / `DriverUnload returned, draining IRPs` | teardown |
| `UNHANDLED EXCEPTION 0xâ€¦ at 0xâ€¦` | user-mode fault, via `SetUnhandledExceptionFilter` |

So a file ending in `entering kernel function 0xFFFFF8049â€¦` means the machine died **inside** that call, not before it.

---

## 2. Getting the bugcheck code

This is the single most useful thing to collect. Windows records it in Event ID 1001:

```powershell
Get-WinEvent -FilterHashtable @{LogName='System'; Id=1001} -MaxEvents 3 |
  Format-List TimeCreated, Message
```

That message carries the bugcheck code and its four parameters. It is enough to name the fault precisely, in most cases without a dump.

### If a dump exists, keep it

```powershell
Get-ChildItem C:\Windows\Minidump, C:\Windows\MEMORY.DMP -ErrorAction SilentlyContinue
```

Open it in WinDbg and run `!analyze -v`. The output names the faulting instruction and the owning module.

---

## 3. Reading the codes

| Code | Name | Typical cause here |
|------|------|--------------------|
| `0x50` | `PAGE_FAULT_IN_NONPAGED_AREA` | dereferencing a bogus kernel pointer â€” a bad export walk, a stale object field |
| `0x7E` | `KERNEL_MODE_EXCEPTION_NOT_HANDLED` | an unguarded dereference; missing `.pdata` for `__try/__except` |
| `0x8E` | same, different path | dispatch into freed memory, or a NULL `MajorFunction` entry |
| `0x3B` | `SYSTEM_SERVICE_EXCEPTION` | PEB walk without `KeStackAttachProcess` |
| `0x1A` | `MEMORY_MANAGEMENT` | a write landing in the wrong physical page â€” the page-straddling class |
| `0xC4` | verifier | pool misuse: double free, wrong tag, use after free |
| `0x9F` | `DRIVER_POWER_STATE_FAILURE` | an IRP not completing during a power transition |
| `0xD1` | `DRIVER_IRQL_NOT_LESS_OR_EQUAL` | touching paged memory at raised IRQL |

---

## 4. Faults already fixed

Each of these was a real crash cause found by reading, and each is closed:

| Fault | Why it crashed |
|-------|----------------|
| NULL `MajorFunction` slots | manual mapping never runs `IopInvalidDeviceRequest`, so `IRP_MJ_CLEANUP` on handle close dispatched to address 0 |
| `WriteToReadOnlyMemory` mapped one page and wrote `size` | physical pages are not contiguous; a patch crossing a 4 KB boundary corrupted a foreign page |
| `OB_FLUSH_ALL_PREPARE_CALLBACKS` was `0` | it is a version field, not a flag; zero made `ObRegisterCallbacks` fail |
| NULL `DriverExtension` | several kernel paths dereference it |
| `driver->DeviceObject` list head NULL | `IoCreateDevice` calls `InsertTailList`, which dereferences `Blink` |
| Image freed the instant `DriverUnload` returned | in-flight IRPs still executed inside it |
| Double hide of the same module | second record, then a triple insert â€” list corruption |
| Copy handlers unguarded | a bad address could raise in kernel; now SEH-wrapped |
| Garbage address above `MmHighestUserAddress` | `MmCopyVirtualMemory` treats it as kernel memory and plain-copies it with no fault handling, so a stale game pointer bugchecked `0x50` in `nt!memcpy`; the range is now screened with `MmIsAddressValid` and refused as `STATUS_PARTIAL_COPY` |
| `OBJECT_HEADER.TypeIndex` written raw | the kernel decodes it as `stored ^ ObHeaderCookie ^ (Header >> 8)`, so a raw index resolved to a NULL object type and the first reference bugchecked `0x18`; the loader now recovers the cookie from a live object header and stores the encoded byte |

---

## 5. Isolating without loading

Two verbs let you test layers separately, so a failure is attributable:

```powershell
.\dist\kernmem-loader.exe kbase     # resolve the kernel base, then tear NAL down
.\dist\kernmem-loader.exe status    # device + protocol probe only
```

`kbase` loads the vulnerable driver but **never maps `km.sys`**, so a crash there is in the host or the resolver, not in our driver.

---

## 6. Recovery after a crash

A bugcheck can leave two different messes, and they need different answers.

**Host held (`\Device\Nal` busy, load says "already resident").**
The loader now attaches to a resident host instead of refusing, and unload sweeps any
leftover service whose display name contains `iqvw` even when the state file is gone:

```powershell
.\dist\kernmem-loader.exe unload
```

**A mapped image leaked in pool.** If the device still answers after a successful
`unload`, the image was never freed and only a **reboot** reclaims the pool. This
happens with images older than protocol 4, which cannot report their own mapping. After
rebooting, `load` starts clean.

Signs of the leak, so it is not mistaken for something else:

```
[+] driver unmapped
[*] NAL: resident
[+] device: present, protocol 3      <-- still there after unload
[!] protocol mismatch (expected 4)
```

Manual cleanup of the file and key, if you want it gone:

```powershell
Get-Service | Where-Object Name -match 'iqvw'
sc.exe stop <name>
sc.exe delete <name>
Remove-Item "$env:TEMP\<random>.sys"
```

## 7. Why the leak cannot recur

The driver reports its own `ImageBase`, `ImageSize` and `DriverObject` over
`IOCTL_KM_GET_INFO` (protocol 4), so teardown reads them from the driver instead of a
temp file. `mapper::Unmap` also refuses to free the image if it cannot read
`DriverUnload`, so the pool can no longer be released while the device is still live.

---

## 8. Bugcheck 0x76: locked pages

`PROCESS_HAS_LOCKED_PAGES (0x76)` means a process died while an MDL still had
pages locked into its address space. The parameters name it directly:

```
0x76 (0x0, <MDL>, <pages still locked>, 0x0)
```

If the third parameter is `0x21`, that is 33 pages, which is exactly
`sizeof(KM_SHARED)`: `Data[0x20000]` + 32 x `KM_SHARED_REQ` (0x400) + a 0x14
header = `0x20414`. So the offending MDL is the shared read block, not the
read/write IOCTL path (that one locks and unlocks inside a single request).

The shared block is locked into the process that registered it and stays locked
until the driver is told to stop. Two things have to be true for that to be safe:

- the loader must not free the VA while the driver still holds the MDL, and
- the driver must release the block even when the process never asks.

Both were wrong once. `DestroyShared()` freed the block with a bare
`VirtualFree` and no deregistration, and `nal::Unload(false)` closes the device
handle and then frees the block while deliberately leaving the host resident, so
the process exited with the pages still locked. On the driver side
`IRP_MJ_CLEANUP` was a no-op, so nothing ever cleaned up after an abrupt exit.

How it is fixed now:

- the driver records `OwnerPid` per block and its `IRP_MJ_CLEANUP` handler stops
every block owned by the closing process. Cleanup fires before the address space
is torn down, so it is the last moment the pages can be released. Blocks owned by
other processes are never touched.
- `nal::DestroyShared()` opens and closes a throwaway handle to the device before
`VirtualFree`, which triggers that cleanup in its own process, and `nal::Unload`
frees the shared block before the control handle is closed.

If you see 0x76 on a build older than this change, the leak is in the client, not
the mapper: re-run `unload` after rebooting so the stale image does not stay
resident.

---

Back to [index](README.md)
