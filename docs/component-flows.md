# 03 — Component flows

Every flow, in order, with its failure branches. This is the document to read when something breaks: each diagram has a matching entry in [06 Troubleshooting](troubleshooting.md).

---

## 0. Flow index

| Flow | Entry point | Ends at |
|------|-------------|---------|
| A | Load | `vgb.sys` running |
| B | Read | bytes in client buffer |
| C | Write | bytes in game memory |
| D | Module base | address of a module |
| E | Hide / unhide | LDR entry unlinked / relinked |
| F | Unload | kernel clean |

---

## A. Load

```mermaid
sequenceDiagram
    autonumber
    participant U as User (elevated)
    participant L as vgb-loader
    participant S as SCM / registry
    participant N as iqvw64e.sys
    participant K as ntoskrnl
    participant D as vgb.sys (mapped)

    U->>L: vgb-loader load
    L->>L: extract iqvw64e.sys to %TEMP%\<random>
    L->>S: create service key, ImagePath, Type=1
    L->>K: RtlAdjustPrivilege(SeLoadDriverPrivilege)
    L->>K: NtLoadDriver(\Registry\...\<random>)
    K->>N: DriverEntry
    N-->>L: \Device\Nal openable
    L->>L: read vgb.sys into memory
    L->>L: parse PE, validate, compute image size
    L->>N: IOCTL 0x80862007 case 0x33 — allocate + copy
    L->>N: relocate image in place
    L->>N: resolve ntoskrnl imports via export table
    L->>L: build VGB_INIT_PARAMS {magic, clientPid}
    L->>N: kernel call DriverEntry(driverObject, &params)
    N->>D: runs in ring 0
    D->>K: IoCreateDevice, IoCreateSymbolicLink
    D->>K: ObRegisterCallbacks(altitude 321000)
    D-->>L: STATUS_SUCCESS
    L->>L: open \\.\VanguardBypass, IOCTL_VGB_GET_VERSION
```

### Failure branches

```mermaid
flowchart TD
    A["load"] --> B{"elevated?"}
    B -->|no| B1["STATUS_PRIVILEGE_NOT_HELD<br/>→ relaunch elevated"]
    B -->|yes| C{"\\Device\\Nal free?"}
    C -->|no| C1["STATUS_ALREADY_REGISTERED<br/>→ kill stale iqvw64e or reboot"]
    C -->|yes| D{"NtLoadDriver ok?"}
    D -->|no| D1["STATUS_INVALID_IMAGE_HASH<br/>→ driver already signed/present<br/>or path wrong"]
    D -->|yes| E{"PE validates?"}
    E -->|no| E1["bad image → rebuild vgb.sys"]
    E -->|yes| F{"DriverEntry ok?"}
    F -->|no| F1["device name taken / callback denied<br/>→ IOCTL_VGB_GET_VERSION tells you which"]
    F -->|yes| G["ready"]
```

**Invariant:** `vgb.sys` is never written to disk. It only ever exists in a kernel pool allocation. There is nothing to delete afterwards and nothing on disk for a scanner to find.

---

## B. Read

```mermaid
sequenceDiagram
    autonumber
    participant C as Client
    participant D as vgb.sys
    participant K as ntoskrnl
    participant G as Game

    C->>D: IOCTL_VGB_READ {pid, address, size ≤ 64 KiB}
    D->>D: validate inLen, clamp size
    D->>K: PsLookupProcessByProcessId(pid)
    K-->>D: PEPROCESS (+ref)
    D->>K: MmCopyVirtualMemory(game, addr, self, buf, size)
    alt success
        K->>G: translate + copy
        K-->>D: STATUS_SUCCESS
        D-->>C: bytes
    else invalid address
        K-->>D: STATUS_PARTIAL_COPY
        D-->>C: 0 bytes, status
    end
    D->>K: ObDereferenceObject
```

The client never opens a handle. If the read fails, it is an address problem, not a permission problem — a distinction that saves a lot of debugging time.

---

## C. Write

Identical shape with the operands swapped: the data block travels **after** the `VGB_MEM_REQ` header inside the same system buffer, and `MmCopyVirtualMemory` copies self → target.

```
+-------------------+---------------------------+
| VGB_MEM_REQ (24)  | payload (size bytes)      |
+-------------------+---------------------------+
  pid, size, address   copied into the game
```

Write to a read-only page (`.text` of the game) fails with `STATUS_PARTIAL_COPY` by default — expected, since `MmCopyVirtualMemory` honours page protection.

---

## D. Module base

```mermaid
sequenceDiagram
    participant C as Client
    participant D as vgb.sys
    participant K as ntoskrnl
    participant P as Game PEB

    C->>D: IOCTL_VGB_GET_MODULE_BASE {pid, "League of Legends.exe"}
    D->>K: PsLookupProcessByProcessId
    D->>K: KeStackAttachProcess
    Note over D: now in the game's address space
    D->>P: PEB->Ldr->InMemoryOrderModuleList
    loop each entry
        D->>P: RtlEqualUnicodeString(BaseDllName, wanted)
    end
    P-->>D: match → DllBase
    D->>K: KeUnstackDetachProcess
    D-->>C: base address
```

`IOCTL_VGB_GET_MAIN_BASE` is the shortcut: `PsGetProcessSectionBaseAddress` returns the image base without a PEB walk.

---

## E. Hide / unhide

```mermaid
stateDiagram-v2
    [*] --> Linked: module loaded normally
    Linked --> Unlinked: IOCTL_VGB_HIDE_MODULE
    note right of Unlinked
        all three list entries removed,
        heads re-initialised,
        record kept in g_Hidden[]
    end note
    Unlinked --> Linked: IOCTL_VGB_UNHIDE_MODULE
    note left of Linked
        re-inserted at head of
        each list — order preserved
    end note
```

Order matters: unhide **before** `FreeLibrary`, otherwise the loader has no entry to remove and the process can fault on exit.

---

## F. Unload

```mermaid
sequenceDiagram
    participant U as User
    participant L as vgb-loader
    participant D as vgb.sys
    participant N as iqvw64e.sys

    U->>L: vgb-loader unload
    L->>D: IOCTL_VGB_UNHIDE_MODULE (all hidden)
    L->>N: kernel call vgb!VgbUnload-equivalent
    D->>D: ObUnRegisterCallbacks, IoDeleteSymbolicLink, IoDeleteDevice
    L->>N: free mapped image pool
    L->>N: NtUnloadDriver(iqvw64e)
    L->>L: delete service key + %TEMP%\<random>
```

Teardown runs in reverse order of setup, always. A partial teardown leaves `\Device\Nal` busy and the next load fails with `STATUS_ALREADY_REGISTERED`.

---

## G. Full data flow, one picture

```mermaid
flowchart LR
    subgraph USER["Your process"]
        S["script"] --> SD["sdk"]
    end
    SD -->|"buffered IOCTL"| DV["\\\\.\\VanguardBypass"]
    DV --> DISP["VgbDispatch"]
    DISP --> R["VgbHandleRead"]
    DISP --> W["VgbHandleWrite"]
    DISP --> MB["VgbGetModuleBase"]
    DISP --> HD["VgbSetModuleHidden"]
    R --> CP["MmCopyVirtualMemory"]
    W --> CP
    MB --> AT["KeStackAttachProcess"]
    HD --> AT
    CP --> GM["game memory"]
    AT --> PEB["game PEB"]
```

---

Next: [04 Build and load](build-and-load.md)
