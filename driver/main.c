/*
 * Kernmem kernel driver
 * ----------------------------
 * Riot Vanguard protects the game process with ObRegisterCallbacks and strips
 * PROCESS_VM_READ / PROCESS_VM_WRITE / PROCESS_VM_OPERATION out of every handle
 * anyone else opens to the game. That is why external ESP/script tooling cannot
 * simply OpenProcess + ReadProcessMemory against League of Legends.exe.
 *
 * This driver defeats that in two independent ways:
 *
 *   1. Attach-based access (primary). Memory is copied with MmCopyVirtualMemory
 *      against the target EPROCESS, so no user-mode handle is involved at all.
 *      Vanguard's callback can only edit handles, never kernel copies.
 *
 *   2. Handle restoration (secondary). We register our own object pre-operation
 *      callback at a high altitude. When the registered client process opens a
 *      process handle, we OR the VM rights back in after Vanguard's callback
 *      cleared them, so plain ReadProcessMemory/WriteProcessMemory works too.
 *
 * Additionally the driver can unlink a module from the target PEB LDR lists so
 * an injected scripting DLL disappears from Vanguard's module enumeration.
 *
 * Designed for manual mapping: no CRT, no /GS cookie, all imports are genuine
 * ntoskrnl exports.
 */

#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0A00
#define NTDDI_VERSION 0x0A00000C
#include <ntifs.h>
#include <ntstrsafe.h>

#include "km_shared.h"
#include "km_ntcompat.h"

/* Fallback pool tag. The real one is generated per install and arrives in the
 * init block, so a build does not stamp one greppable four bytes into every
 * machine's pool. */
#define KM_TAG_DEFAULT 'MENK'

#define KM_MAX_HIDDEN 32

/* LdrpHashTable has 32 buckets on x64. */
#define KM_LDR_HASH_COUNT 32

typedef struct _KM_HIDDEN_ENTRY
{
    unsigned long Pid;
    void* Entry;
    void* DllBase;
    void* HashBucket;   /* bucket head the entry was unlinked from, or NULL */
    unsigned long NodeOffset;   /* LdrNode offset, found while the node was live */
} KM_HIDDEN_ENTRY;

/* Layout of an LDR_DATA_TABLE_ENTRY from the three list heads onward. */
typedef struct _KM_LDR_ENTRY
{
    LIST_ENTRY InLoadOrderLinks;
    LIST_ENTRY InMemoryOrderLinks;
    LIST_ENTRY InInitializationOrderLinks;
    void* DllBase;
    void* EntryPoint;
    unsigned long SizeOfImage;
    UNICODE_STRING FullDllName;
    UNICODE_STRING BaseDllName;
    unsigned long Flags;
    unsigned short LoadCount;
    unsigned short TlsIndex;
    LIST_ENTRY HashLinks;
} KM_LDR_ENTRY;

/* HashLinks sits at 0x70 on every x64 build from 7600 to 26200; the fields in
 * between are only present to reach that offset, so assert it rather than trust
 * it. */
C_ASSERT(FIELD_OFFSET(KM_LDR_ENTRY, HashLinks) == 0x70);

static PDEVICE_OBJECT g_DeviceObject = NULL;
static ULONG          g_ClientPid = 0;
static PVOID          g_ObRegistration = NULL;
static BOOLEAN        g_ObRegistered = FALSE;
static KSPIN_LOCK     g_HiddenLock;
static volatile LONG  g_Unloading = 0;
/* Handlers currently executing inside this image. The loader frees the image the
 * moment DriverUnload returns, so unload has to drain this before letting go. */
static volatile LONG  g_ActiveCalls = 0;
static KEVENT         g_UnloadDrain;
/* Serialises the shared-worker start/stop decision; see KmStartShared. */
static KMUTEX         g_SharedLock;
static KM_HIDDEN_ENTRY g_Hidden[KM_MAX_HIDDEN];

/*
 * Device identity. The loader picks the name per install and hands it over in
 * the init block, so a build does not stamp one fixed namespace onto every
 * machine. The buffers are static because the driver object and the symbolic
 * link both keep pointing at them for the whole life of the mapping.
 */
static WCHAR          g_DeviceName[KM_MAX_DEVICE_CHARS];
static WCHAR          g_DevicePath[KM_MAX_DEVICE_CHARS + 16];
static WCHAR          g_SymlinkName[KM_MAX_DEVICE_CHARS + 16];
static WCHAR          g_DriverName[KM_MAX_DEVICE_CHARS + 16];
static UNICODE_STRING g_Symlink;
static ULONG          g_DeviceType = KM_DEVICE_TYPE;

/* Per-install seed for the callback altitude. A fixed altitude is one more
 * constant to signature on, and it also collided with other filters on some
 * machines and fell through the whole chain. */
static ULONG          g_AltitudeSeed = 0;

/* Per-install pool tag for the transient query buffer. */
static ULONG          g_PoolTag = KM_TAG_DEFAULT;

/* Own mapping, reported to the loader so unload can free it without a state
 * file. Populated from the init block at entry. */
static unsigned __int64 g_ImageBase = 0;
static unsigned __int64 g_ImageSize = 0;
static PVOID            g_DriverObject = NULL;

/* Published LDR_DATA_TABLE_ENTRY, or NULL. Pool, so it outlives nothing of its
 * own: unload unlinks and frees it before the image goes. */
static PVOID            g_ModuleEntry = NULL;

/* The image's own registered exception table, or NULL. It points into the image, so
 * it is deleted before the image is freed. */
static PKM_RUNTIME_FUNCTION g_FunctionTable = NULL;

/* Resolved once, on first use. Both are absent from the kernel import library. */
static KM_PFN_ADD_FUNCTION_TABLE    g_RtlAddFunctionTable = NULL;
static KM_PFN_DELETE_FUNCTION_TABLE g_RtlDeleteFunctionTable = NULL;

static VOID KmResolveTableRoutines(VOID)
{
    UNICODE_STRING routine;

    if (!g_RtlAddFunctionTable)
    {
        RtlInitUnicodeString(&routine, L"RtlAddFunctionTable");
        g_RtlAddFunctionTable =
            (KM_PFN_ADD_FUNCTION_TABLE)MmGetSystemRoutineAddress(&routine);
    }

    if (!g_RtlDeleteFunctionTable)
    {
        RtlInitUnicodeString(&routine, L"RtlDeleteFunctionTable");
        g_RtlDeleteFunctionTable =
            (KM_PFN_DELETE_FUNCTION_TABLE)MmGetSystemRoutineAddress(&routine);
    }
}

/*
 * Shared block. The client allocates it and hands the user VA over in the init
 * block; it is locked here and mapped into system space, and a worker thread
 * services it. Reads then cost no IOCTL at all, so no syscall is patched while
 * the game is running.
 */
/*
 * Two blocks: 0 is the loader's, for kernel reads; 1 is the client's, for target
 * reads. Separate so a client cannot see or overwrite the loader's slots.
 */
#define KM_BLOCK_COUNT  2
#define KM_BLOCK_KERNEL 0
#define KM_BLOCK_CLIENT 1

/* Upper bound on a client-supplied block. One KM_SHARED is all anything needs,
 * and an unbounded size hands an absurd VA range to MmProbeAndLockPages, which
 * then raises with part of the range already locked. */
#define KM_MAX_SHARED_BYTES 0x100000

typedef struct _KM_BLOCK_STATE
{
    PMDL       Mdl;
    KM_SHARED* Map;
    volatile LONG Running;   /* interlocked: two cleanups can race a start */
    ULONG      OwnerPid;  /* process whose VA the pages are locked into; 0 if idle */
    KMUTEX     Lock;   /* held by the worker while it services this block */
} KM_BLOCK_STATE;

static KM_BLOCK_STATE g_Blocks[KM_BLOCK_COUNT];
static PETHREAD        g_SharedThread = NULL;
static KEVENT          g_SharedStop;
static BOOLEAN         g_SharedRunning = FALSE;

/*
 * Declared ahead of the shared helpers: those sit above the definitions of the
 * copy helpers they use, and a manual-mapped image has no implicit declarations
 * to fall back on.
 */
static PEPROCESS KmReferenceProcess(ULONG pid);
static BOOLEAN KmAddressCopyable(ULONG_PTR address, ULONG size);
static NTSTATUS KmCopyFromProcess(PEPROCESS proc, void* remote, void* local, SIZE_T size);
static VOID KmStopShared(ULONG index);
static BOOLEAN KmUserPointer(unsigned __int64 p);

/* ------------------------------------------------------------------ */
/* helpers                                                             */
/* ------------------------------------------------------------------ */

/* The init block arrives as DriverEntry's 2nd argument. Under manual mapping
 * that is a kernel pool pointer, under service load it is a registry path, so
 * only plausibility is checked here and the magic is validated under SEH. */
static BOOLEAN KmLooksLikePointer(const void* p)
{
    return ((ULONG_PTR)p > 0x10000);
}

/*
 * Services one shared slot: copies the requested range from the target process
 * into the shared Data buffer. Done is written last, and only ever to the Seq it
 * serviced, so the client can spin on Seq == Done without a lock.
 */
static VOID KmSharedService(KM_BLOCK_STATE* block, KM_SHARED_REQ* req)
{
    PEPROCESS proc;
    NTSTATUS status = STATUS_PARTIAL_COPY;
    ULONG size = req->Size;

    if (!block || !block->Map)
    {
        req->Status = (unsigned long)STATUS_INVALID_PARAMETER;
        req->Done = req->Seq;
        return;
    }

    if (!size || size > KM_SHARED_DATA || req->Offset >= KM_SHARED_DATA ||
        size > KM_SHARED_DATA - req->Offset)
    {
        req->Status = (unsigned long)STATUS_BUFFER_TOO_SMALL;
        req->Done = req->Seq;
        return;
    }

    /*
     * Pid 0 means a kernel address. Kernel VAs are valid in any context, so the
     * range is copied directly instead of through a process.
     */
    if (!req->Pid)
    {
        /*
         * Kernel VAs are valid in any context, but not every value handed over is
         * one. -1, or a stale object field, is above MmHighestUserAddress and so
         * takes this raw memcpy path - and that copy has no probe behind it. The
         * __try below only helps while the exception table is registered, so screen
         * the range first and refuse it while a status is still possible.
         */
        if (!KmAddressCopyable((ULONG_PTR)req->Address, size))
        {
            req->Status = (unsigned long)STATUS_PARTIAL_COPY;
            req->Done = req->Seq;
            return;
        }

        __try
        {
            RtlCopyMemory(block->Map->Data + req->Offset, (void*)req->Address, size);
            status = STATUS_SUCCESS;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            status = STATUS_PARTIAL_COPY;
        }

        req->Status = (unsigned long)status;
        req->Done = req->Seq;
        return;
    }

    proc = KmReferenceProcess(req->Pid);

    if (proc)
    {
        if (KmAddressCopyable((ULONG_PTR)req->Address, size))
        {
            __try
            {
                status = KmCopyFromProcess(proc, (void*)req->Address,
                                           block->Map->Data + req->Offset, size);
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                status = STATUS_PARTIAL_COPY;
            }
        }

        ObDereferenceObject(proc);
    }
    else
    {
        status = STATUS_INVALID_PARAMETER;
    }

    req->Status = (unsigned long)status;
    req->Done = req->Seq;
}

static VOID KmSharedWorker(PVOID context)
{
    UNREFERENCED_PARAMETER(context);

    for (;;)
    {
        ULONG i;
        LARGE_INTEGER tick;

        /*
         * Timed, and it has to be. g_SharedStop is a notification event that is
         * never reset, so an untimed wait parked the worker right here until a stop
         * and the service loop below was never reached - no slot was ever serviced.
         * The 1 ms tick is also what paces the loop when idle, without a busy spin.
         */
        tick.QuadPart = -10000;

        if (KeWaitForSingleObject(&g_SharedStop, Executive, KernelMode, FALSE, &tick) ==
            STATUS_WAIT_0)
            break;

        for (i = 0; i < KM_BLOCK_COUNT; ++i)
        {
            KM_BLOCK_STATE* block = &g_Blocks[i];
            ULONG slot;

            if (!block->Running || !block->Map)
                continue;

            /*
             * The stop path takes this same lock before it unmaps, so a slot can
             * never be serviced against a mapping that is going away. The state is
             * re-checked under the lock because it may have been torn down while we
             * were waiting for it.
             */
            KeWaitForMutexObject(&block->Lock, Executive, KernelMode, FALSE, NULL);

            if (block->Running && block->Map)
            {
                for (slot = 0; slot < KM_SHARED_SLOTS; ++slot)
                {
                    KM_SHARED_REQ* req = &block->Map->Reqs[slot];

                    if (req->Seq == req->Done)
                        continue;

                    KmSharedService(block, req);
                }
            }

            KeReleaseMutex(&block->Lock, FALSE);
        }
    }

    PsTerminateSystemThread(STATUS_SUCCESS);
}

static BOOLEAN KmStartShared(ULONG index, unsigned __int64 user, ULONG size)
{
    KM_BLOCK_STATE* block;
    OBJECT_ATTRIBUTES attributes;
    HANDLE thread = NULL;
    NTSTATUS status;
    KM_SHARED* map;

    if (index >= KM_BLOCK_COUNT || !user || size < sizeof(KM_SHARED) ||
        size > KM_MAX_SHARED_BYTES)
        return FALSE;

    block = &g_Blocks[index];

    /*
     * A repeat registration with a different VA or size has to drop the old
     * block first. Returning TRUE on a live slot leaves the old MDL locked
     * against a VA nobody owns any more, and OwnerPid goes stale - which is the
     * 0x76 path. Identical VA and size: nothing to do.
     */
    if (block->Running)
    {
        if (block->Mdl && MmGetMdlByteCount(block->Mdl) == size &&
            (unsigned __int64)MmGetMdlVirtualAddress(block->Mdl) == user)
        {
            /* Same VA, but possibly a new owner: cleanup matches on the pid, so
             * a stale one would leak this block past the process's exit. */
            block->OwnerPid = (ULONG)(ULONG_PTR)PsGetCurrentProcessId();
            return TRUE;
        }

        KmStopShared(index);
    }

    block->Mdl = IoAllocateMdl((PVOID)user, size, FALSE, FALSE, NULL);

    if (!block->Mdl)
        return FALSE;

    __try
    {
        MmProbeAndLockPages(block->Mdl, UserMode, IoModifyAccess);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        /*
         * The probe raises after part of the range is already locked, and an MDL
         * freed without unlocking leaves those pages charged to the calling
         * process for good - bugcheck 0x76 PROCESS_HAS_LOCKED_PAGES, 33 pages for
         * one KM_SHARED. Unlock under its own guard: a half-built MDL can raise
         * again here, and that must not take the machine down on a path that is
         * already failing.
         */
        __try
        {
            MmUnlockPages(block->Mdl);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }

        IoFreeMdl(block->Mdl);
        block->Mdl = NULL;
        return FALSE;
    }

    map = (KM_SHARED*)MmMapLockedPagesSpecifyCache(block->Mdl, KernelMode, MmCached,
                                                   NULL, FALSE, NormalPagePriority);

    if (!map)
    {
        MmUnlockPages(block->Mdl);
        IoFreeMdl(block->Mdl);
        block->Mdl = NULL;
        return FALSE;
    }

    block->Map = map;
    KeInitializeMutex(&block->Lock, 0);

    RtlZeroMemory(map, sizeof(KM_SHARED));
    map->Slots = KM_SHARED_SLOTS;
    map->DataSize = KM_SHARED_DATA;

    /*
     * Held from the decision until this block is marked Running. Without it a
     * cleanup could stop the worker in the window where the thread exists but the
     * block does not count as live yet, leaving a live block with no worker - and
     * the stopping side then cleared g_SharedThread under the new thread.
     */
    KeWaitForMutexObject(&g_SharedLock, Executive, KernelMode, FALSE, NULL);

    /*
     * One worker serves every block, so it starts with the first and stops with the
     * last. Starting it per block would leave threads behind on every re-register.
     */
    if (!g_SharedRunning)
    {
        KeInitializeEvent(&g_SharedStop, NotificationEvent, FALSE);
        InitializeObjectAttributes(&attributes, NULL, OBJ_KERNEL_HANDLE, NULL, NULL);

        status = PsCreateSystemThread(&thread, THREAD_ALL_ACCESS, &attributes, NULL, NULL,
                                      KmSharedWorker, NULL);

        if (!NT_SUCCESS(status))
        {
            MmUnmapLockedPages(map, block->Mdl);
            MmUnlockPages(block->Mdl);
            IoFreeMdl(block->Mdl);
            block->Map = NULL;
            block->Mdl = NULL;
            KeReleaseMutex(&g_SharedLock, FALSE);
            return FALSE;
        }

        /*
         * KeWaitForSingleObject wants a dispatcher object pointer, not a handle.
         * A handle is a sign-extended 32-bit value, so waiting on it dereferences
         * 0xFFFFFFFF80004A88 and bugchecks 0x0A at DISPATCH_LEVEL (write, IRQL 2).
         * Keep a referenced object and close the handle instead.
         */
        g_SharedThread = NULL;

        status = ObReferenceObjectByHandle(thread, THREAD_ALL_ACCESS, NULL, KernelMode,
                                           (PVOID*)&g_SharedThread, NULL);

        if (!NT_SUCCESS(status))
        {
            /*
             * The worker is already running and its code lives in this image, so
             * it has to be gone before the map is dropped. A thread handle is not
             * a dispatcher object, but ZwWaitForSingleObject takes the handle - so
             * wait here, while the handle is still open.
             */
            KeSetEvent(&g_SharedStop, IO_NO_INCREMENT, FALSE);
            ZwWaitForSingleObject(thread, FALSE, NULL);
            ZwClose(thread);
            g_SharedThread = NULL;

            MmUnmapLockedPages(map, block->Mdl);
            MmUnlockPages(block->Mdl);
            IoFreeMdl(block->Mdl);
            block->Map = NULL;
            block->Mdl = NULL;
            KeReleaseMutex(&g_SharedLock, FALSE);
            return FALSE;
        }

        ZwClose(thread);

        g_SharedRunning = TRUE;
    }

    /*
     * The pages are locked against the calling process's address space, so that
     * process is what the teardown paths have to match on. Recorded before
     * Running goes up, so a cleanup racing this cannot see a live block with a
     * stale owner.
     */
    block->OwnerPid = (ULONG)(ULONG_PTR)PsGetCurrentProcessId();

    /* Ready last: a client waits on it before issuing anything. */
    map->Magic = KM_SHARED_MAGIC;
    InterlockedExchange(&block->Running, TRUE);
    map->Ready = 1;

    KeReleaseMutex(&g_SharedLock, FALSE);
    return TRUE;
}

static VOID KmStopShared(ULONG index)
{
    KM_BLOCK_STATE* block;
    ULONG i;
    BOOLEAN remaining = FALSE;

    if (index >= KM_BLOCK_COUNT)
        return;

    block = &g_Blocks[index];

    /* Same lifecycle lock as the start side: the stop decision reads the other
     * blocks and owns g_SharedThread. */
    KeWaitForMutexObject(&g_SharedLock, Executive, KernelMode, FALSE, NULL);

    /* Two cleanups can race here (two handles closing at once). Only the caller
     * that actually clears the flag may tear the block down. */
    if (InterlockedExchange(&block->Running, FALSE) == FALSE)
    {
        KeReleaseMutex(&g_SharedLock, FALSE);
        return;
    }

    if (block->Map)
        block->Map->Ready = 0;

    for (i = 0; i < KM_BLOCK_COUNT; ++i)
    {
        if (g_Blocks[i].Running)
            remaining = TRUE;
    }

    /* Last block out stops the shared worker, and waiting on it is what makes the
     * unmap below safe. */
    if (!remaining && g_SharedRunning)
    {
        g_SharedRunning = FALSE;
        KeSetEvent(&g_SharedStop, IO_NO_INCREMENT, FALSE);

        if (g_SharedThread)
        {
            KeWaitForSingleObject(g_SharedThread, Executive, KernelMode, FALSE, NULL);
            ObDereferenceObject(g_SharedThread);
            g_SharedThread = NULL;
        }
    }

    /* Serialises against a worker that may still be servicing this block, which
     * happens whenever another block keeps the thread alive. */
    KeWaitForMutexObject(&block->Lock, Executive, KernelMode, FALSE, NULL);

    if (block->Map)
    {
        MmUnmapLockedPages(block->Map, block->Mdl);
        block->Map = NULL;
    }

    if (block->Mdl)
    {
        MmUnlockPages(block->Mdl);
        IoFreeMdl(block->Mdl);
        block->Mdl = NULL;
    }

    KeReleaseMutex(&block->Lock, FALSE);

    /* No pages are owned any more, so a later cleanup must not match this slot. */
    block->OwnerPid = 0;

    KeReleaseMutex(&g_SharedLock, FALSE);
}

/*
 * Used by the entry failure paths. Returning a failed DriverEntry lets the loader
 * free this image, and the worker thread executes code inside it - so anything
 * brought up has to be stopped here, not left to an unload that never runs.
 */
static VOID KmTeardownShared(VOID)
{
    ULONG i;

    for (i = 0; i < KM_BLOCK_COUNT; ++i)
        KmStopShared(i);
}

static PEPROCESS KmReferenceProcess(ULONG pid)
{
    PEPROCESS proc = NULL;
    if (!pid)
        return NULL;
    if (!NT_SUCCESS(PsLookupProcessByProcessId((HANDLE)(ULONG_PTR)pid, &proc)))
        return NULL;
    return proc;
}

static NTSTATUS KmCopyFromProcess(PEPROCESS proc, void* remote, void* local, SIZE_T size)
{
    SIZE_T done = 0;
    return MmCopyVirtualMemory(proc, remote, PsGetCurrentProcess(), local, size, KernelMode, &done);
}

static NTSTATUS KmCopyToProcess(PEPROCESS proc, void* remote, void* local, SIZE_T size)
{
    SIZE_T done = 0;
    return MmCopyVirtualMemory(PsGetCurrentProcess(), local, proc, remote, size, KernelMode, &done);
}

/* ------------------------------------------------------------------ */
/* object callback: give our client its access rights back             */
/* ------------------------------------------------------------------ */

static OB_PREOP_CALLBACK_STATUS KmProcessPreOperation(
    _In_ PVOID RegistrationContext,
    _Inout_ POB_PRE_OPERATION_INFORMATION Info)
{
    ACCESS_MASK add;

    UNREFERENCED_PARAMETER(RegistrationContext);

    if (!g_ClientPid || Info->ObjectType != *PsProcessType)
        return OB_PREOP_SUCCESS;

    if ((ULONG)(ULONG_PTR)PsGetCurrentProcessId() != g_ClientPid)
        return OB_PREOP_SUCCESS;

    add = PROCESS_VM_READ | PROCESS_VM_WRITE | PROCESS_VM_OPERATION |
          PROCESS_QUERY_INFORMATION | PROCESS_SUSPEND_RESUME | PROCESS_CREATE_THREAD;

    if (Info->Operation == OB_OPERATION_HANDLE_CREATE)
    {
        Info->Parameters->CreateHandleInformation.DesiredAccess |= add;
    }
    else if (Info->Operation == OB_OPERATION_HANDLE_DUPLICATE)
    {
        Info->Parameters->DuplicateHandleInformation.DesiredAccess |= add;
    }

    return OB_PREOP_SUCCESS;
}

/*
 * Historical fallbacks, tried after the seeded values. Kept as a table of
 * pointers rather than inline arrays: x64 code is RIP-relative, so once no
 * absolute address remains the linker omits the relocation section entirely and
 * the mapper refuses the image. This table keeps one in place.
 */
static const WCHAR* const g_AltitudeFallbacks[] =
{
    L"321000", L"321100", L"329000", L"329500"
};

/* Six ASCII digits, zero padded, without the CRT. */
static void KmFormatAltitude(WCHAR* out, ULONG value)
{
    ULONG i;

    out[6] = 0;

    for (i = 6; i > 0; --i)
    {
        out[i - 1] = (WCHAR)(L'0' + (value % 10));
        value /= 10;
    }
}

static NTSTATUS KmRegisterObjectCallback(void)
{
    /*
     * The pre-operation chain runs in DESCENDING altitude order, so our filter
     * must sit above Vanguard's in order to put back the rights it strips.
     *
     * A fixed altitude is both a constant to signature on and a collision risk.
     * The primary value is seeded per install, safely above every process filter
     * in the field; the historical values stay behind it as fallbacks for the
     * case where the seeded one is already taken.
     */
    WCHAR candidates[6][7];
    OB_OPERATION_REGISTRATION operations[1];
    OB_CALLBACK_REGISTRATION registration;
    NTSTATUS status = STATUS_OBJECT_NAME_COLLISION;
    ULONG i;

    KmFormatAltitude(candidates[0], 330000 + (g_AltitudeSeed % 60000));
    KmFormatAltitude(candidates[1], 330000 + ((g_AltitudeSeed + 1) % 60000));
    for (i = 0; i < RTL_NUMBER_OF(g_AltitudeFallbacks); ++i)
        RtlStringCchCopyW(candidates[2 + i], RTL_NUMBER_OF(candidates[0]),
                          g_AltitudeFallbacks[i]);

    RtlZeroMemory(operations, sizeof(operations));
    /*
     * PsProcessType is a data import, so the symbol IS the IAT slot address. The
     * registration wants exactly that: a pointer to the variable that holds the
     * OBJECT_TYPE, not the type object itself (which is what *PsProcessType is
     * and what the pre-operation compares against).
     */
    operations[0].ObjectType = PsProcessType;
    operations[0].Operations = OB_OPERATION_HANDLE_CREATE | OB_OPERATION_HANDLE_DUPLICATE;
    operations[0].PreOperation = KmProcessPreOperation;
    operations[0].PostOperation = NULL;

    for (i = 0; i < RTL_NUMBER_OF(candidates); ++i)
    {
        UNICODE_STRING altitude;

        RtlInitUnicodeString(&altitude, candidates[i]);

        RtlZeroMemory(&registration, sizeof(registration));
        registration.Version = OB_FLUSH_ALL_PREPARE_CALLBACKS;
        registration.OperationRegistrationCount = 1;
        registration.Altitude = altitude;
        registration.RegistrationContext = NULL;
        registration.OperationRegistration = operations;

        g_ObRegistration = NULL;
        status = ObRegisterCallbacks(&registration, &g_ObRegistration);
        if (NT_SUCCESS(status))
        {
            g_ObRegistered = TRUE;
            return status;
        }
    }

    return status;
}

/* ------------------------------------------------------------------ */
/* process / module lookup                                             */
/* ------------------------------------------------------------------ */

/* PsGetProcessImageFileName stores at most 15 chars and does NOT NUL-terminate
 * when the name is exactly that long, so the image side must be length-bounded
 * and a truncated image name counts as a match for a longer query. */
#define KM_IMAGE_NAME_MAX 15

static BOOLEAN KmImageNameMatches(const WCHAR* image, const WCHAR* wanted)
{
    size_t i;

    if (!image || !image[0] || !wanted || !wanted[0])
        return FALSE;

    for (i = 0; i < KM_IMAGE_NAME_MAX && image[i]; ++i)
    {
        WCHAR a = image[i];
        WCHAR b = wanted[i];

        if (b == 0)
            return TRUE;
        if (a >= L'A' && a <= L'Z')
            a = (WCHAR)(a + 32);
        if (b >= L'A' && b <= L'Z')
            b = (WCHAR)(b + 32);
        if (a != b)
            return FALSE;
    }

    /* i == KM_IMAGE_NAME_MAX means the stored name was truncated: accept it. */
    return (i >= KM_IMAGE_NAME_MAX) || (wanted[i] == 0);
}

/* Last path component of an image path. The system information image names are
 * ANSI, so this works on bytes. */
/* Base name of an image path. ImageName is a counted UNICODE_STRING, so the
 * scan is length-bounded and works in wchar units (walking bytes would stop at
 * the high byte of the first character). */
static const WCHAR* KmBaseName(const WCHAR* path, ULONG chars)
{
    const WCHAR* base = path;
    ULONG i;

    for (i = 0; i < chars && path[i]; ++i)
    {
        if (path[i] == L'\\' || path[i] == L'/')
            base = path + i + 1;
    }

    return base;
}

/* Fallback enumeration through the native system information query, used when
 * PsGetNextProcess cannot be resolved at runtime. */
static ULONG KmFindPidByNameQuery(const wchar_t* name)
{
    ULONG size = 0x20000;
    ULONG attempt;

    for (attempt = 0; attempt < 8; ++attempt)
    {
        NTSTATUS status;
        ULONG needed = size;
        ULONG offset = 0;
        ULONG found = 0;
        PVOID buffer = ExAllocatePoolWithTag(NonPagedPool, size, g_PoolTag);

        if (!buffer)
            return 0;

        status = ZwQuerySystemInformation(SystemProcessInformation, buffer, size, &needed);
        if (status == STATUS_INFO_LENGTH_MISMATCH)
        {
            ExFreePoolWithTag(buffer, g_PoolTag);
            size = needed + 0x1000;
            continue;
        }
        if (!NT_SUCCESS(status))
        {
            ExFreePoolWithTag(buffer, g_PoolTag);
            return 0;
        }

        for (;;)
        {
            KM_SYSTEM_PROCESS_INFORMATION* entry =
                (KM_SYSTEM_PROCESS_INFORMATION*)((PUCHAR)buffer + offset);

            if (entry->ImageName.Buffer && entry->ImageName.Length >= sizeof(WCHAR))
            {
                const WCHAR* start = (const WCHAR*)entry->ImageName.Buffer;
                const ULONG chars = entry->ImageName.Length / sizeof(WCHAR);
                const WCHAR* base = KmBaseName(start, chars);
                const ULONG remaining = (ULONG)((start + chars) - base);
                WCHAR wide[KM_MAX_NAME_CHARS];
                ULONG i;

                for (i = 0; i < remaining && i < KM_MAX_NAME_CHARS - 1 && base[i]; ++i)
                    wide[i] = base[i];
                wide[i] = 0;

                if (KmImageNameMatches(wide, name))
                {
                    found = (ULONG)(ULONG_PTR)entry->UniqueProcessId;
                    break;
                }
            }

            if (!entry->NextEntryOffset)
                break;
            offset += entry->NextEntryOffset;
        }

        ExFreePoolWithTag(buffer, g_PoolTag);
        return found;
    }

    return 0;
}

static ULONG KmFindPidByName(const wchar_t* name)
{
    static KM_PFN_GET_NEXT_PROCESS getNext = NULL;
    static BOOLEAN resolved = FALSE;
    PEPROCESS proc;

    if (!name || !name[0])
        return 0;

    if (!resolved)
    {
        UNICODE_STRING routine;

        RtlInitUnicodeString(&routine, L"PsGetNextProcess");
        getNext = (KM_PFN_GET_NEXT_PROCESS)MmGetSystemRoutineAddress(&routine);
        resolved = TRUE;
    }

    if (getNext)
    {
        proc = getNext(NULL);
        while (proc)
        {
            PUCHAR image = PsGetProcessImageFileName(proc);

            if (image)
            {
                WCHAR wide[KM_IMAGE_NAME_MAX + 1];
                size_t i;

                for (i = 0; i < KM_IMAGE_NAME_MAX && image[i]; ++i)
                    wide[i] = (WCHAR)image[i];
                wide[i] = 0;

                if (KmImageNameMatches(wide, name))
                {
                    ULONG found = (ULONG)(ULONG_PTR)PsGetProcessId(proc);
                    ObDereferenceObject(proc);
                    return found;
                }
            }

            {
                PEPROCESS next = getNext(proc);
                ObDereferenceObject(proc);
                proc = next;
            }
        }

        return 0;
    }

    return KmFindPidByNameQuery(name);
}

static NTSTATUS KmGetMainBase(ULONG pid, unsigned __int64* base)
{
    PEPROCESS proc = KmReferenceProcess(pid);
    if (!proc)
        return STATUS_INVALID_PARAMETER;

    *base = (unsigned __int64)PsGetProcessSectionBaseAddress(proc);
    ObDereferenceObject(proc);
    return STATUS_SUCCESS;
}

/* Must be called attached to the target process. */
static NTSTATUS KmWalkModules(ULONG pid, KM_MODULE_LIST* list, ULONG capacity)
{
    PEPROCESS proc = NULL;
    KAPC_STATE apc;
    NTSTATUS status = STATUS_SUCCESS;
    KM_PEB* peb;
    LIST_ENTRY* head;
    LIST_ENTRY* cursor;
    ULONG count = 0;
    BOOLEAN truncated = FALSE;

    proc = KmReferenceProcess(pid);
    if (!proc)
        return STATUS_INVALID_PARAMETER;

    KeStackAttachProcess(proc, &apc);
    __try
    {
        /*
         * The PEB belongs to the target process and is only valid while it lives.
         * A process that is tearing down (cs2.exe closing) yields a PEB whose pages
         * are already going, so the pointer is checked before it is dereferenced -
         * the SEH below is the backstop, not the guard.
         */
        peb = (KM_PEB*)PsGetProcessPeb(proc);

        if (!peb || !KmUserPointer((unsigned __int64)peb))
        {
            status = STATUS_NOT_FOUND;
        }
        else if (!peb->Ldr || !MmIsAddressValid(peb->Ldr))
        {
            status = STATUS_NOT_FOUND;
        }
        else
        {
            head = &peb->Ldr->InMemoryOrderModuleList;
            for (cursor = head->Flink; cursor != head; cursor = cursor->Flink)
            {
                KM_LDR_ENTRY* entry = CONTAINING_RECORD(cursor, KM_LDR_ENTRY, InMemoryOrderLinks);

                if (count >= capacity)
                {
                    truncated = TRUE;
                    break;
                }

                /* Every cursor comes out of the target's own list, so it is only as
                 * trustworthy as the entry before it. One bad link in a process that
                 * is tearing down otherwise faults the whole walk. */
                if (!KmUserPointer((unsigned __int64)cursor) ||
                    !KmUserPointer((unsigned __int64)cursor->Flink))
                {
                    truncated = TRUE;
                    break;
                }

                list->Entries[count].Base = (unsigned __int64)entry->DllBase;
                list->Entries[count].SizeOfImage = entry->SizeOfImage;
                list->Entries[count].Pad = 0;
                list->Entries[count].Name[0] = 0;

                if (entry->BaseDllName.Buffer && entry->BaseDllName.Length &&
                    KmUserPointer((unsigned __int64)entry->BaseDllName.Buffer))
                {
                    ULONG chars = entry->BaseDllName.Length / sizeof(WCHAR);
                    if (chars >= KM_MAX_NAME_CHARS)
                        chars = KM_MAX_NAME_CHARS - 1;
                    RtlCopyMemory(list->Entries[count].Name, entry->BaseDllName.Buffer, chars * sizeof(WCHAR));
                    list->Entries[count].Name[chars] = 0;
                }
                ++count;
            }
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        status = GetExceptionCode();
    }
    KeUnstackDetachProcess(&apc);
    ObDereferenceObject(proc);

    list->Count = count;
    list->Truncated = truncated ? 1 : 0;
    return status;
}

static NTSTATUS KmGetModuleBase(ULONG pid, const wchar_t* name, unsigned __int64* base)
{
    PEPROCESS proc = NULL;
    KAPC_STATE apc;
    NTSTATUS status = STATUS_NOT_FOUND;
    UNICODE_STRING wanted;
    KM_PEB* peb;
    LIST_ENTRY* head;
    LIST_ENTRY* cursor;

    *base = 0;
    RtlInitUnicodeString(&wanted, name);

    proc = KmReferenceProcess(pid);
    if (!proc)
        return STATUS_INVALID_PARAMETER;

    KeStackAttachProcess(proc, &apc);
    __try
    {
        peb = (KM_PEB*)PsGetProcessPeb(proc);
        if (peb && peb->Ldr)
        {
            head = &peb->Ldr->InMemoryOrderModuleList;
            for (cursor = head->Flink; cursor != head; cursor = cursor->Flink)
            {
                KM_LDR_ENTRY* entry = CONTAINING_RECORD(cursor, KM_LDR_ENTRY, InMemoryOrderLinks);
                if (entry->BaseDllName.Buffer &&
                    RtlEqualUnicodeString(&entry->BaseDllName, &wanted, TRUE))
                {
                    *base = (unsigned __int64)entry->DllBase;
                    status = STATUS_SUCCESS;
                    break;
                }
            }
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        status = GetExceptionCode();
    }
    KeUnstackDetachProcess(&apc);
    ObDereferenceObject(proc);

    return status;
}

/* ------------------------------------------------------------------ */
/* module index (the by-name tree)                                     */
/* ------------------------------------------------------------------ */

/*
 * LdrpModuleIndex is an RB tree of LDR_DATA_TABLE_ENTRY.LdrNode, ordered by
 * DllBase, and it is what a name resolve (GetModuleHandle) walks. Unlinking the
 * three lists and the hash bucket leaves that path intact, so the node has to
 * come out of the tree too.
 *
 * Each parent is stored as ParentValue ^ key with the low bits holding the
 * colour. Nothing is hardcoded: the node offset is found by checking the BST
 * ordering and the key is recovered from a parent/child pair. Every step is
 * validated, and a failure returns FALSE - leaving the index entry in place is a
 * smaller loss than a bad splice into someone else's loader tree.
 */
#define KM_RB_LEFT   0x00
#define KM_RB_RIGHT  0x08
#define KM_RB_PARENT 0x10
#define KM_RB_MASK   0x7ULL
#define KM_RB_NODES  128

static BOOLEAN KmUserPointer(unsigned __int64 p)
{
    return p >= 0x10000 && p < 0x7FFFFFFFFFFFULL && (p & 7) == 0;
}

/* The field moved between builds, so the offset is found by ordering instead of
 * being assumed: a candidate only survives when both children are aligned user
 * addresses whose DllBase sits on the correct side of ours. */
static BOOLEAN KmFindLdrNodeOffset(KM_LDR_ENTRY* entry, unsigned __int64 base, ULONG* offset)
{
    ULONG probe;

    for (probe = 0x78; probe <= 0x110; probe += 8)
    {
        unsigned __int64* node = (unsigned __int64*)((PUCHAR)entry + probe);
        ULONG side;

        if (!node[0] && !node[1] && !node[2])
            continue;

        for (side = 0; side < 2; ++side)
        {
            unsigned __int64 child = side ? node[1] : node[0];
            unsigned __int64 childBase;

            if (!child)
                continue;
            if (!KmUserPointer(child))
                break;

            childBase = *(unsigned __int64*)((PUCHAR)child - probe + 0x30);

            if (side == 0 && childBase >= base)
                break;
            if (side == 1 && childBase <= base)
                break;
        }

        if (side != 2)
            continue;

        *offset = probe;
        return TRUE;
    }

    return FALSE;
}

/*
 * Takes the node out of the tree. No rebalancing: the colours are carried over
 * and the shape stays legal enough for a walk, which is all a resolve needs. A
 * missing rebalance costs depth, not correctness.
 */
static BOOLEAN KmUnlinkModuleIndex(void* ldrRaw, KM_LDR_ENTRY* entry, unsigned __int64 base,
                                   ULONG* offsetOut)
{
    unsigned __int64 nodes[KM_RB_NODES];
    unsigned __int64 key = 0;
    unsigned __int64* node;
    unsigned __int64 parentValue;
    unsigned __int64 parent;
    unsigned __int64 left;
    unsigned __int64 right;
    unsigned __int64 child;
    unsigned __int64* slot;
    LIST_ENTRY* head;
    LIST_ENTRY* cursor;
    ULONG offset = 0;
    ULONG count = 0;
    ULONG i;
    ULONG j;

    if (offsetOut)
        *offsetOut = 0;

    if (!ldrRaw || !entry)
        return FALSE;

    /*
     * Found here, while the node is still linked. Once it is out, its three link
     * words are zero and KmFindLdrNodeOffset rejects it - so the offset is handed
     * back and carried in the hidden record for the re-insert.
     */
    if (!KmFindLdrNodeOffset(entry, base, &offset))
        return FALSE;

    node = (unsigned __int64*)((PUCHAR)entry + offset);

    /* PEB_LDR_DATA.InMemoryOrderModuleList sits at 0x20. The walk only collects
     * the live node addresses, so the key can be recovered from a pair. */
    head = (LIST_ENTRY*)((PUCHAR)ldrRaw + 0x20);

    for (cursor = head->Flink; cursor != head && count < KM_RB_NODES; cursor = cursor->Flink)
    {
        KM_LDR_ENTRY* other = CONTAINING_RECORD(cursor, KM_LDR_ENTRY, InMemoryOrderLinks);
        nodes[count++] = (unsigned __int64)((PUCHAR)other + offset);
    }

    /* Recover the key from any parent/child pair. */
    for (i = 0; i < count && !key; ++i)
    {
        unsigned __int64* candidate = (unsigned __int64*)nodes[i];

        if (!candidate[KM_RB_PARENT / 8])
            continue;

        for (j = 0; j < count; ++j)
        {
            unsigned __int64* maybe = (unsigned __int64*)nodes[j];

            if (maybe[KM_RB_LEFT / 8] == (unsigned __int64)candidate ||
                maybe[KM_RB_RIGHT / 8] == (unsigned __int64)candidate)
            {
                key = (candidate[KM_RB_PARENT / 8] & ~KM_RB_MASK) ^ nodes[j];
                break;
            }
        }
    }

    if (!key)
        return FALSE;

    parentValue = node[KM_RB_PARENT / 8];
    parent = (parentValue & ~KM_RB_MASK) ^ key;
    left = node[KM_RB_LEFT / 8];
    right = node[KM_RB_RIGHT / 8];

    /* The parent has to point back at this node, or the layout is not what we
     * think it is and nothing gets written. */
    if (!parent || !KmUserPointer(parent))
        return FALSE;

    if (((unsigned __int64*)parent)[KM_RB_LEFT / 8] == (unsigned __int64)node)
        slot = (unsigned __int64*)parent + (KM_RB_LEFT / 8);
    else if (((unsigned __int64*)parent)[KM_RB_RIGHT / 8] == (unsigned __int64)node)
        slot = (unsigned __int64*)parent + (KM_RB_RIGHT / 8);
    else
        return FALSE;

    if (left && !KmUserPointer(left))
        return FALSE;
    if (right && !KmUserPointer(right))
        return FALSE;

    if (left && right)
    {
        /* Two children: splice in the in-order successor, which has no left child
         * by construction, and give it this node's place and colour. */
        unsigned __int64* successor = (unsigned __int64*)right;
        unsigned __int64* successorParent = node;
        ULONG depth = 0;

        while (successor[KM_RB_LEFT / 8] && depth++ < 64)
        {
            successorParent = successor;
            successor = (unsigned __int64*)successor[KM_RB_LEFT / 8];
        }

        if (successorParent == node)
        {
            successor[KM_RB_PARENT / 8] = parentValue;
            successor[KM_RB_LEFT / 8] = left;
        }
        else
        {
            successorParent[KM_RB_LEFT / 8] = successor[KM_RB_RIGHT / 8];

            if (successor[KM_RB_RIGHT / 8])
                ((unsigned __int64*)successor[KM_RB_RIGHT / 8])[KM_RB_PARENT / 8] =
                    ((unsigned __int64)successorParent ^ key) |
                    (((unsigned __int64*)successor[KM_RB_RIGHT / 8])[KM_RB_PARENT / 8] & KM_RB_MASK);

            successor[KM_RB_PARENT / 8] = parentValue;
            successor[KM_RB_LEFT / 8] = left;
            successor[KM_RB_RIGHT / 8] = right;

            ((unsigned __int64*)right)[KM_RB_PARENT / 8] =
                ((unsigned __int64)successor ^ key) |
                (((unsigned __int64*)right)[KM_RB_PARENT / 8] & KM_RB_MASK);
        }

        /*
         * This write has to stay inside the branch. successor is uninitialised in
         * the single-child path below, where left's parent has already been set
         * from parentValue - so doing it unconditionally stamped garbage into
         * left's parent, and faulted outright on a leaf, where left is NULL.
         */
        ((unsigned __int64*)left)[KM_RB_PARENT / 8] =
            ((unsigned __int64)successor ^ key) |
            (((unsigned __int64*)left)[KM_RB_PARENT / 8] & KM_RB_MASK);

        child = (unsigned __int64)successor;
    }
    else
    {
        /* At most one child: it inherits this node's position and colour. */
        child = left ? left : right;

        if (child)
            ((unsigned __int64*)child)[KM_RB_PARENT / 8] = parentValue;
    }

    *slot = child;

    /* Leave no stale links behind: unhide overwrites them anyway, but until then a
     * walker must not follow them out of the tree. */
    node[KM_RB_LEFT / 8] = 0;
    node[KM_RB_RIGHT / 8] = 0;
    node[KM_RB_PARENT / 8] = 0;

    if (offsetOut)
        *offsetOut = offset;

    return TRUE;
}

/*
 * Puts the node back. The tree is walked by DllBase down to the slot the order
 * calls for; no rebalancing, same as the unlink - a resolve only ever compares,
 * so depth and colour are irrelevant. The key is recovered from a live
 * parent/child pair as before, and the climb stops at the root because the root's
 * parent is LdrpModuleIndexBase, which is not an LDR entry and so does not point
 * back at it.
 */
static BOOLEAN KmInsertModuleIndex(void* ldrRaw, KM_LDR_ENTRY* entry, unsigned __int64 base,
                                   ULONG offset)
{
    unsigned __int64 nodes[KM_RB_NODES];
    unsigned __int64 key = 0;
    unsigned __int64* node;
    unsigned __int64* cursor;
    unsigned __int64* parent = NULL;
    unsigned __int64* slot = NULL;
    unsigned __int64 parentValue;
    unsigned __int64 next;
    unsigned __int64 nodeBase;
    LIST_ENTRY* head;
    LIST_ENTRY* walk;
    ULONG count = 0;
    ULONG side;
    ULONG steps;
    ULONG i;
    ULONG j;

    if (!ldrRaw || !entry)
        return FALSE;

    /*
     * The recorded offset is preferred: the node is detached by now and the probe
     * rejects a node whose links are all zero. The probe is only a fallback for a
     * hide whose unlink never ran, in which case the node is still linked.
     */
    if (!offset && !KmFindLdrNodeOffset(entry, base, &offset))
        return FALSE;

    node = (unsigned __int64*)((PUCHAR)entry + offset);

    head = (LIST_ENTRY*)((PUCHAR)ldrRaw + 0x20);

    for (walk = head->Flink; walk != head && count < KM_RB_NODES; walk = walk->Flink)
    {
        KM_LDR_ENTRY* other = CONTAINING_RECORD(walk, KM_LDR_ENTRY, InMemoryOrderLinks);
        unsigned __int64 candidate = (unsigned __int64)((PUCHAR)other + offset);

        /*
         * Unhide re-linked the entry before calling in here, so the head of the
         * list now yields the detached node itself - carrying stale tree links.
         * Using it for key recovery or as the climb start would read garbage, so
         * only live nodes go in.
         */
        if (candidate == (unsigned __int64)node)
            continue;

        nodes[count++] = candidate;
    }

    if (!count)
        return FALSE;

    for (i = 0; i < count && !key; ++i)
    {
        unsigned __int64* candidate = (unsigned __int64*)nodes[i];

        if (!candidate[KM_RB_PARENT / 8])
            continue;

        for (j = 0; j < count; ++j)
        {
            unsigned __int64* maybe = (unsigned __int64*)nodes[j];

            if (maybe[KM_RB_LEFT / 8] == (unsigned __int64)candidate ||
                maybe[KM_RB_RIGHT / 8] == (unsigned __int64)candidate)
            {
                key = (candidate[KM_RB_PARENT / 8] & ~KM_RB_MASK) ^ nodes[j];
                break;
            }
        }
    }

    if (!key)
        return FALSE;

    cursor = (unsigned __int64*)nodes[0];

    for (steps = 0; steps <= count; ++steps)
    {
        parentValue = cursor[KM_RB_PARENT / 8];
        parent = (unsigned __int64*)((parentValue & ~KM_RB_MASK) ^ key);

        if (!parent || parent == cursor || parent == node ||
            !KmUserPointer((unsigned __int64)parent))
            break;

        if (parent[KM_RB_LEFT / 8] != (unsigned __int64)cursor &&
            parent[KM_RB_RIGHT / 8] != (unsigned __int64)cursor)
            break;

        cursor = parent;
    }

    if (cursor == node)
        return FALSE;

    for (steps = 0; steps <= count; ++steps)
    {
        nodeBase = *(unsigned __int64*)((PUCHAR)cursor - offset + 0x30);
        side = base < nodeBase ? 0 : 1;
        next = cursor[side ? KM_RB_RIGHT / 8 : KM_RB_LEFT / 8];

        if (next == (unsigned __int64)node)
            next = 0;

        if (!next || !KmUserPointer(next))
        {
            slot = (unsigned __int64*)cursor + (side ? KM_RB_RIGHT / 8 : KM_RB_LEFT / 8);
            parent = cursor;
            break;
        }

        cursor = (unsigned __int64*)next;
    }

    if (!slot)
        return FALSE;

    node[KM_RB_LEFT / 8] = 0;
    node[KM_RB_RIGHT / 8] = 0;
    node[KM_RB_PARENT / 8] = ((unsigned __int64)parent ^ key);
    *slot = (unsigned __int64)node;

    return TRUE;
}

/* ------------------------------------------------------------------ */
/* LDR unlinking (hide an injected module from Vanguard's scan)        */
/* ------------------------------------------------------------------ */

static NTSTATUS KmSetModuleHidden(ULONG pid, unsigned __int64 base, BOOLEAN hide)
{
    PEPROCESS proc = NULL;
    KAPC_STATE apc;
    NTSTATUS status = STATUS_NOT_FOUND;
    KM_PEB* peb;
    LIST_ENTRY* head;
    LIST_ENTRY* cursor;
    LIST_ENTRY* bucket = NULL;
    KM_LDR_ENTRY* entry = NULL;
    KM_LDR_ENTRY* recorded = NULL;
    ULONG nodeOffset = 0;

    if (!base)
        return STATUS_INVALID_PARAMETER;

    /*
     * Balance the hidden record against repeats. A second hide would run
     * RemoveEntryList on entries that are already self-linked (harmless) but
     * then add a second record, after which unhide inserts the same entry into
     * three lists twice - list corruption, then a bugcheck on the next walk.
     */
    {
        KIRQL irql;
        ULONG i;
        BOOLEAN known = FALSE;

        KeAcquireSpinLock(&g_HiddenLock, &irql);
        for (i = 0; i < KM_MAX_HIDDEN; ++i)
        {
            if (g_Hidden[i].Entry && g_Hidden[i].DllBase == (void*)base && g_Hidden[i].Pid == pid)
            {
                /* Remembered bucket and entry, so unhide can put the links back. */
                bucket = (LIST_ENTRY*)g_Hidden[i].HashBucket;
                recorded = (KM_LDR_ENTRY*)g_Hidden[i].Entry;
                nodeOffset = g_Hidden[i].NodeOffset;
                known = TRUE;
                break;
            }
        }
        KeReleaseSpinLock(&g_HiddenLock, irql);

        if (hide && known)
            return STATUS_SUCCESS;
        if (!hide && !known)
            return STATUS_NOT_FOUND;
    }

    proc = KmReferenceProcess(pid);
    if (!proc)
        return STATUS_INVALID_PARAMETER;

    KeStackAttachProcess(proc, &apc);
    __try
    {
        peb = (KM_PEB*)PsGetProcessPeb(proc);
        if (peb && peb->Ldr)
        {
            head = &peb->Ldr->InMemoryOrderModuleList;

            /*
             * Hiding takes the entry out of every list, so a walk over
             * InMemoryOrderModuleList cannot find it again: unhide used to come
             * back STATUS_NOT_FOUND on every call and the module stayed invisible
             * for good. The entry pointer kept from the hide is the only way back
             * in, so a known unhide resolves straight from the record. A recorded
             * entry whose DllBase no longer matches (module reloaded at another
             * base) falls back to the walk.
             */
            if (!hide && recorded && recorded->DllBase == (void*)base)
                entry = recorded;

            for (cursor = head->Flink; !entry && cursor != head; cursor = cursor->Flink)
            {
                KM_LDR_ENTRY* other = CONTAINING_RECORD(cursor, KM_LDR_ENTRY, InMemoryOrderLinks);

                if ((unsigned __int64)other->DllBase == base)
                    entry = other;
            }

            if (entry)
            {
                if (hide)
                {
                    RemoveEntryList(&entry->InLoadOrderLinks);
                    RemoveEntryList(&entry->InMemoryOrderLinks);
                    RemoveEntryList(&entry->InInitializationOrderLinks);
                    InitializeListHead(&entry->InLoadOrderLinks);
                    InitializeListHead(&entry->InMemoryOrderLinks);
                    InitializeListHead(&entry->InInitializationOrderLinks);

                    /*
                     * The three lists are only what LdrEnumerateLoadedModules and
                     * a PEB walk see. LdrpHashTable indexes the same entry by
                     * name hash, and a scanner that resolves a module by name goes
                     * through it, so the entry has to leave that bucket too.
                     *
                     * Only unlink when the link is actually live - a repeat hide
                     * would otherwise run RemoveEntryList on a self-linked node and
                     * corrupt the bucket. The next node doubles as the bucket head
                     * on unhide; the list is circular, so it is valid either way.
                     */
                    if (entry->HashLinks.Flink && entry->HashLinks.Flink != &entry->HashLinks)
                    {
                        bucket = entry->HashLinks.Flink;
                        RemoveEntryList(&entry->HashLinks);
                        InitializeListHead(&entry->HashLinks);
                    }

                    /* And out of the by-name index tree, which is what a name
                     * resolve walks. A failure leaves the entry in the index, and
                     * the offset stays 0 for the re-insert. */
                    KmUnlinkModuleIndex(peb->Ldr, entry, base, &nodeOffset);
                }
                else
                {
                    /* Inserting against the list HEAD is valid even when the
                     * list is empty; CONTAINING_RECORD on an empty list would
                     * compute a bogus entry. */
                    InsertHeadList(&peb->Ldr->InLoadOrderModuleList, &entry->InLoadOrderLinks);
                    InsertHeadList(&peb->Ldr->InMemoryOrderModuleList, &entry->InMemoryOrderLinks);
                    InsertHeadList(&peb->Ldr->InInitializationOrderModuleList, &entry->InInitializationOrderLinks);

                    /* Back into the bucket it came from, if one was recorded and
                     * the link is not already live. */
                    if (bucket && entry->HashLinks.Flink == &entry->HashLinks)
                        InsertHeadList(bucket, &entry->HashLinks);

                    /* And back into the by-name index. A failure here only costs
                     * name resolves - the entry is already live in the lists. */
                    KmInsertModuleIndex(peb->Ldr, entry, base, nodeOffset);
                }

                status = STATUS_SUCCESS;
            }
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        status = GetExceptionCode();
    }
    KeUnstackDetachProcess(&apc);
    ObDereferenceObject(proc);

    if (NT_SUCCESS(status))
    {
        KIRQL irql;
        ULONG i;

        KeAcquireSpinLock(&g_HiddenLock, &irql);
        if (hide)
        {
            for (i = 0; i < KM_MAX_HIDDEN; ++i)
            {
                if (!g_Hidden[i].Entry)
                {
                    g_Hidden[i].Pid = pid;
                    g_Hidden[i].DllBase = (void*)base;
                    g_Hidden[i].Entry = (void*)entry;   /* the entry, not the base */
                    g_Hidden[i].HashBucket = (void*)bucket;
                    g_Hidden[i].NodeOffset = nodeOffset;
                    break;
                }
            }
        }
        else
        {
            for (i = 0; i < KM_MAX_HIDDEN; ++i)
            {
                if (g_Hidden[i].DllBase == (void*)base && g_Hidden[i].Pid == pid)
                {
                    g_Hidden[i].Entry = NULL;
                    g_Hidden[i].DllBase = NULL;
                    g_Hidden[i].HashBucket = NULL;
                    g_Hidden[i].NodeOffset = 0;
                    g_Hidden[i].Pid = 0;
                    break;
                }
            }
        }
        KeReleaseSpinLock(&g_HiddenLock, irql);
    }

    return status;
}

/* ------------------------------------------------------------------ */
/* IOCTL dispatch                                                      */
/* ------------------------------------------------------------------ */

/*
 * MmCopyVirtualMemory decides which side of the copy is user memory by
 * comparing against MmHighestUserAddress. An address above it is treated as
 * kernel memory and copied with a plain memcpy - no user probe, no fault
 * handling. A bogus pointer in that range therefore does not come back as
 * STATUS_PARTIAL_COPY, it bugchecks 0x50 inside nt!memcpy.
 *
 * Every pointer the overlay reads comes out of game data, so one stale object
 * field is enough to reach that path with garbage such as 0xfffffff8000001ec.
 * Screen the range ourselves first and refuse it while a status is still
 * possible.
 */
static BOOLEAN KmAddressCopyable(ULONG_PTR address, ULONG size)
{
    ULONG_PTR last;

    if (!address)
        return FALSE;

    last = address + size - 1;

    /* Wrapped the address space. */
    if (last < address)
        return FALSE;

    /* MmHighestUserAddress is a data import: dereference the IAT slot. */
    if (address <= *(ULONG_PTR*)&MmHighestUserAddress)
        return TRUE;

    if (!MmIsAddressValid((PVOID)address) || !MmIsAddressValid((PVOID)last))
        return FALSE;

    return TRUE;
}

static NTSTATUS KmHandleRead(PVOID in, ULONG inLen, PVOID out, ULONG outLen, PULONG_PTR written)
{
    KM_MEM_REQ* req = (KM_MEM_REQ*)in;
    PEPROCESS proc;
    NTSTATUS status;
    ULONG size;

    if (inLen < sizeof(KM_MEM_REQ) || !outLen)
        return STATUS_BUFFER_TOO_SMALL;

    size = req->Size;
    if (size > KM_MAX_XFER)
        size = KM_MAX_XFER;
    if (size > outLen)
        size = outLen;
    if (!size)
        return STATUS_INVALID_PARAMETER;

    if (!KmAddressCopyable((ULONG_PTR)req->Address, size))
        return STATUS_PARTIAL_COPY;

    proc = KmReferenceProcess(req->Pid);
    if (!proc)
        return STATUS_INVALID_PARAMETER;

    /* MmCopyVirtualMemory normally reports a bad address as STATUS_PARTIAL_COPY,
     * but a probe can still raise on some builds. SEH turns that into a status
     * instead of a bugcheck the user has to reboot from. */
    __try
    {
        status = KmCopyFromProcess(proc, (void*)req->Address, out, size);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        status = STATUS_PARTIAL_COPY;
    }

    ObDereferenceObject(proc);

    *written = NT_SUCCESS(status) ? size : 0;
    return status;
}

/*
 * One IOCTL, several reads. Each descriptor names an address, a size and the
 * offset inside the output buffer it belongs at, so a whole per-frame scan costs
 * a single round trip instead of one per field. A descriptor that fails leaves
 * its slot untouched and the call still succeeds, which matches how the single
 * read behaves from the caller's side: the caller checks the slot.
 */
static NTSTATUS KmHandleReadBatch(PVOID in, ULONG inLen, PVOID out, ULONG outLen,
                                   PULONG_PTR written)
{
    KM_BATCH_REQ* req = (KM_BATCH_REQ*)in;
    PEPROCESS proc;
    ULONG count;
    ULONG done = 0;
    ULONG i;

    if (inLen < (ULONG)FIELD_OFFSET(KM_BATCH_REQ, Entries) || !outLen)
        return STATUS_BUFFER_TOO_SMALL;

    count = req->Count;
    if (!count)
        return STATUS_INVALID_PARAMETER;

    if (count > KM_BATCH_MAX)
        count = KM_BATCH_MAX;

    /* Every descriptor has to actually be inside the input buffer. */
    if (inLen < (ULONG)FIELD_OFFSET(KM_BATCH_REQ, Entries) + count * (ULONG)sizeof(KM_BATCH_DESC))
        return STATUS_BUFFER_TOO_SMALL;

    proc = KmReferenceProcess(req->Pid);
    if (!proc)
        return STATUS_INVALID_PARAMETER;

    __try
    {
        for (i = 0; i < count; ++i)
        {
            KM_BATCH_DESC* desc = &req->Entries[i];
            NTSTATUS status;
            ULONG size = desc->Size;

            if (!size)
                continue;

            if (size > KM_MAX_XFER)
                size = KM_MAX_XFER;

            /* Keep the write inside the caller's output buffer. */
            if (desc->Offset >= outLen || size > outLen - desc->Offset)
                continue;

            if (!KmAddressCopyable((ULONG_PTR)desc->Address, size))
                continue;

            status = KmCopyFromProcess(proc, (void*)desc->Address,
                                        (PUCHAR)out + desc->Offset, size);

            if (NT_SUCCESS(status))
                done++;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        /* Whatever was copied before the fault stays valid; report the count. */
    }

    ObDereferenceObject(proc);

    *written = outLen;
    return done ? STATUS_SUCCESS : STATUS_PARTIAL_COPY;
}

static NTSTATUS KmHandleWrite(PVOID in, ULONG inLen, PULONG_PTR written)
{
    KM_MEM_REQ* req = (KM_MEM_REQ*)in;
    PEPROCESS proc;
    NTSTATUS status;
    ULONG size;
    PVOID data;

    if (inLen < sizeof(KM_MEM_REQ))
        return STATUS_BUFFER_TOO_SMALL;

    size = req->Size;
    if (size > KM_MAX_XFER)
        size = KM_MAX_XFER;
    if (!size || inLen < sizeof(KM_MEM_REQ) + size)
        return STATUS_BUFFER_TOO_SMALL;

    data = (PVOID)((PUCHAR)in + sizeof(KM_MEM_REQ));

    if (!KmAddressCopyable((ULONG_PTR)req->Address, size))
        return STATUS_PARTIAL_COPY;

    proc = KmReferenceProcess(req->Pid);
    if (!proc)
        return STATUS_INVALID_PARAMETER;

    __try
    {
        status = KmCopyToProcess(proc, (void*)req->Address, data, size);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        status = STATUS_PARTIAL_COPY;
    }

    ObDereferenceObject(proc);

    *written = NT_SUCCESS(status) ? size : 0;
    return status;
}

static NTSTATUS KmDispatch(PDEVICE_OBJECT device, PIRP irp)
{
    PIO_STACK_LOCATION stack;
    PVOID in;
    PVOID out;
    ULONG inLen;
    ULONG outLen;
    ULONG code;
    ULONG_PTR written = 0;
    NTSTATUS status = STATUS_INVALID_DEVICE_REQUEST;

    UNREFERENCED_PARAMETER(device);

    /*
     * Counted before the check: a handler that is only just starting must not be
     * missed by an unload that flips the flag in between. The loader unmaps and
     * frees this image the moment DriverUnload returns, so a handler still running
     * then would dispatch into freed pool.
     */
    InterlockedIncrement(&g_ActiveCalls);

    if (InterlockedCompareExchange(&g_Unloading, 1, 1) != 0)
    {
        InterlockedDecrement(&g_ActiveCalls);
        KeSetEvent(&g_UnloadDrain, IO_NO_INCREMENT, FALSE);
        irp->IoStatus.Status = STATUS_DELETE_PENDING;
        irp->IoStatus.Information = 0;
        IoCompleteRequest(irp, IO_NO_INCREMENT);
        return STATUS_DELETE_PENDING;
    }

    stack = IoGetCurrentIrpStackLocation(irp);

    /* The device type is derived from the per-install name, so the code that
     * arrives carries a type this build never compiled in. Fold it back to the
     * historical constant so every comparison below stays a plain equality, and
     * a client that still sends the old constant keeps working. */
    code = KmRecodeIoctl(stack->Parameters.DeviceIoControl.IoControlCode, KM_DEVICE_TYPE);
    inLen = stack->Parameters.DeviceIoControl.InputBufferLength;
    outLen = stack->Parameters.DeviceIoControl.OutputBufferLength;
    in = irp->AssociatedIrp.SystemBuffer;
    out = irp->AssociatedIrp.SystemBuffer;

    if (code == IOCTL_KM_GET_VERSION)
    {
        if (outLen >= sizeof(ULONG))
        {
            *(ULONG*)out = KM_PROTOCOL_VERSION;
            written = sizeof(ULONG);
            status = STATUS_SUCCESS;
        }
        else
        {
            status = STATUS_BUFFER_TOO_SMALL;
        }
    }
    else if (code == IOCTL_KM_GET_INFO)
    {
        /* Teardown reads this: the loader may have lost its state file, and the
         * only reliable record of the mapping lives here. */
        if (outLen >= sizeof(KM_INFO))
        {
            KM_INFO* info = (KM_INFO*)out;

            info->ImageBase = g_ImageBase;
            info->ImageSize = g_ImageSize;
            info->DriverObject = (unsigned __int64)g_DriverObject;
            written = sizeof(KM_INFO);
            status = STATUS_SUCCESS;
        }
        else
        {
            status = STATUS_BUFFER_TOO_SMALL;
        }
    }
    else if (code == IOCTL_KM_SET_CLIENT_PID)
    {
        if (inLen >= sizeof(KM_PID_REQ))
        {
            g_ClientPid = ((KM_PID_REQ*)in)->Pid;
            status = STATUS_SUCCESS;
        }
        else
        {
            status = STATUS_BUFFER_TOO_SMALL;
        }
    }
    else if (code == IOCTL_KM_GET_PID_BY_NAME)
    {
        if (inLen >= sizeof(KM_NAME_REQ) && outLen >= sizeof(KM_PID_REQ))
        {
            ((KM_NAME_REQ*)in)->Name[KM_MAX_NAME_CHARS - 1] = 0;
            ((KM_PID_REQ*)out)->Pid = KmFindPidByName(((KM_NAME_REQ*)in)->Name);
            ((KM_PID_REQ*)out)->Pad = 0;
            written = sizeof(KM_PID_REQ);
            status = STATUS_SUCCESS;
        }
        else
        {
            status = STATUS_BUFFER_TOO_SMALL;
        }
    }
    else if (code == IOCTL_KM_GET_MODULE_BASE)
    {
        if (inLen >= sizeof(KM_NAME_REQ) && outLen >= sizeof(unsigned __int64))
        {
            ((KM_NAME_REQ*)in)->Name[KM_MAX_NAME_CHARS - 1] = 0;
            status = KmGetModuleBase(((KM_NAME_REQ*)in)->Pid,
                                      ((KM_NAME_REQ*)in)->Name,
                                      (unsigned __int64*)out);
            written = NT_SUCCESS(status) ? sizeof(unsigned __int64) : 0;
        }
        else
        {
            status = STATUS_BUFFER_TOO_SMALL;
        }
    }
    else if (code == IOCTL_KM_GET_MAIN_BASE)
    {
        if (inLen >= sizeof(KM_PID_REQ) && outLen >= sizeof(unsigned __int64))
        {
            status = KmGetMainBase(((KM_PID_REQ*)in)->Pid, (unsigned __int64*)out);
            written = NT_SUCCESS(status) ? sizeof(unsigned __int64) : 0;
        }
        else
        {
            status = STATUS_BUFFER_TOO_SMALL;
        }
    }
    else if (code == IOCTL_KM_READ)
    {
        status = KmHandleRead(in, inLen, out, outLen, &written);
    }
    else if (code == IOCTL_KM_WRITE)
    {
        status = KmHandleWrite(in, inLen, &written);
    }
    else if (code == IOCTL_KM_READ_BATCH)
    {
        status = KmHandleReadBatch(in, inLen, out, outLen, &written);
    }
    else if (code == IOCTL_KM_ENUM_MODULES)
    {
        if (inLen >= sizeof(KM_PID_REQ) &&
            outLen >= FIELD_OFFSET(KM_MODULE_LIST, Entries) + sizeof(KM_MODULE_INFO))
        {
            ULONG capacity = (outLen - FIELD_OFFSET(KM_MODULE_LIST, Entries)) / sizeof(KM_MODULE_INFO);

            if (capacity > KM_MAX_MODULES)
                capacity = KM_MAX_MODULES;
            status = KmWalkModules(((KM_PID_REQ*)in)->Pid, (KM_MODULE_LIST*)out, capacity);
            if (NT_SUCCESS(status))
                written = FIELD_OFFSET(KM_MODULE_LIST, Entries) +
                          ((KM_MODULE_LIST*)out)->Count * sizeof(KM_MODULE_INFO);
        }
        else
        {
            status = STATUS_BUFFER_TOO_SMALL;
        }
    }
    else if (code == IOCTL_KM_HIDE_MODULE || code == IOCTL_KM_UNHIDE_MODULE)
    {
        typedef struct _KM_BASE_REQ
        {
            ULONG Pid;
            ULONG Pad;
            unsigned __int64 Base;
        } KM_BASE_REQ;

        if (inLen >= sizeof(KM_BASE_REQ))
        {
            status = KmSetModuleHidden(((KM_BASE_REQ*)in)->Pid,
                                        ((KM_BASE_REQ*)in)->Base,
                                        (BOOLEAN)(code == IOCTL_KM_HIDE_MODULE));
        }
        else
        {
            status = STATUS_BUFFER_TOO_SMALL;
        }
    }
    else if (code == IOCTL_KM_SET_CLIENT_BLOCK)
    {
        /*
         * A client hands its own block over here, once. The client index is fixed:
         * the loader's block is not addressable from user mode, so one process
         * cannot displace the loader's kernel read path. A zero User is a request to
         * drop the block again, which is what the client does on close.
         */
        if (inLen >= sizeof(KM_BLOCK_REQ))
        {
            const KM_BLOCK_REQ* req = (const KM_BLOCK_REQ*)in;

            if (!req->User || !req->Size)
            {
                KmStopShared(KM_BLOCK_CLIENT);
                status = STATUS_SUCCESS;
            }
            else if (KmStartShared(KM_BLOCK_CLIENT, req->User, req->Size))
            {
                status = STATUS_SUCCESS;
            }
            else
            {
                status = STATUS_INVALID_PARAMETER;
            }
        }
        else
        {
            status = STATUS_BUFFER_TOO_SMALL;
        }
    }

    irp->IoStatus.Status = status;
    irp->IoStatus.Information = written;
    IoCompleteRequest(irp, IO_NO_INCREMENT);

    /* Released only now: the completion above still executes code in this image. */
    if (InterlockedDecrement(&g_ActiveCalls) == 0 &&
        InterlockedCompareExchange(&g_Unloading, 1, 1) != 0)
        KeSetEvent(&g_UnloadDrain, IO_NO_INCREMENT, FALSE);

    return status;
}

static NTSTATUS KmCreateClose(PDEVICE_OBJECT device, PIRP irp)
{
    UNREFERENCED_PARAMETER(device);
    irp->IoStatus.Status = STATUS_SUCCESS;
    irp->IoStatus.Information = 0;
    IoCompleteRequest(irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

/*
 * IRP_MJ_CLEANUP is the only warning we get that a process is going away, and it
 * arrives before its address space is torn down - the last moment an MDL locked
 * into that process can still be released.
 *
 * Without this, a client that exits without deregistering (a crash, a kill, or a
 * loader that detaches and leaves the host resident on purpose) leaves its block's
 * pages locked against a dead process, and reaping it bugchecks
 * 0x76 PROCESS_HAS_LOCKED_PAGES. That is exactly the 0x21 = 33 page crash: 33
 * pages is sizeof(KM_SHARED).
 *
 * Only blocks owned by the closing process are dropped. One process closing a
 * handle must never pull another process's block out from under it, and the
 * loader's kernel block is registered from the loader's own context, so it is
 * matched only by the loader's own exit.
 */
static NTSTATUS KmCleanup(PDEVICE_OBJECT device, PIRP irp)
{
    ULONG pid = (ULONG)(ULONG_PTR)PsGetCurrentProcessId();
    ULONG b;

    UNREFERENCED_PARAMETER(device);

    for (b = 0; b < KM_BLOCK_COUNT; ++b)
    {
        if (g_Blocks[b].Running && g_Blocks[b].OwnerPid == pid)
            KmStopShared(b);
    }

    irp->IoStatus.Status = STATUS_SUCCESS;
    irp->IoStatus.Information = 0;
    IoCompleteRequest(irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

/*
 * Default handler for every major function we do not implement.
 *
 * This is the single biggest crash risk in the whole driver. NtLoadDriver fills
 * the unset MajorFunction slots with IopInvalidDeviceRequest, but a manually
 * mapped image never goes through it, so those slots stay NULL. Windows still
 * sends IRP_MJ_CLEANUP when the last handle closes, IRP_MJ_SHUTDOWN on power
 * events and IRP_MJ_SYSTEM_CONTROL for WMI - each of which would dispatch to
 * address 0 and bugcheck with 0x8E/0x7E.
 */
static NTSTATUS KmDefaultDispatch(PDEVICE_OBJECT device, PIRP irp)
{
    UNREFERENCED_PARAMETER(device);
    irp->IoStatus.Status = STATUS_INVALID_DEVICE_REQUEST;
    irp->IoStatus.Information = 0;
    IoCompleteRequest(irp, IO_NO_INCREMENT);
    return STATUS_INVALID_DEVICE_REQUEST;
}

/*
 * Publishes this image in PsLoadedModuleList.
 *
 * DriverStart already points at the mapped image, but nothing listed it, so
 * NtQuerySystemInformation(SystemModuleInformation) and a \Driver\ walk
 * disagreed: one showed the driver, the other showed nothing. This closes that
 * gap for anything that walks load order.
 *
 * The block carries 0x10 bytes ahead of the entry for the two reserved fields a
 * real LDR_DATA_TABLE_ENTRY has before InLoadOrderLinks, and the name buffer
 * directly behind it, so the entry is self-contained and one free undoes it.
 *
 * Not covered: the LdrpModuleIndex red-black tree, which is what
 * RtlPcToFileHeader consults. An address-to-module lookup by PC still misses.
 */
static NTSTATUS KmPublishModuleEntry(VOID)
{
    static const WCHAR prefix[] = L"\\SystemRoot\\System32\\drivers\\";
    static const WCHAR suffix[] = L".sys";

    KM_LDR_ENTRY* entry;
    PLIST_ENTRY head;
    UNICODE_STRING fullName;
    UNICODE_STRING baseName;
    SIZE_T deviceChars = 0;
    SIZE_T nameChars;
    SIZE_T bytes;
    WCHAR* buffer;
    PVOID block;

    if (!g_ImageBase || !g_ImageSize || !g_DeviceName[0])
        return STATUS_INVALID_PARAMETER;

    if (!NT_SUCCESS(RtlStringCchLengthW(g_DeviceName, KM_MAX_DEVICE_CHARS, &deviceChars)))
        return STATUS_INVALID_PARAMETER;

    nameChars = (RTL_NUMBER_OF(prefix) - 1) + deviceChars + (RTL_NUMBER_OF(suffix) - 1);
    bytes = 0x10 + sizeof(KM_LDR_ENTRY) + (nameChars + 1) * sizeof(WCHAR);

    block = ExAllocatePoolWithTag(NonPagedPool, bytes, g_PoolTag);

    if (!block)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlZeroMemory(block, bytes);

    entry = (KM_LDR_ENTRY*)((PUCHAR)block + 0x10);
    buffer = (WCHAR*)((PUCHAR)entry + sizeof(KM_LDR_ENTRY));

    RtlStringCchCopyW(buffer, nameChars + 1, prefix);
    RtlStringCchCatW(buffer, nameChars + 1, g_DeviceName);
    RtlStringCchCatW(buffer, nameChars + 1, suffix);

    fullName.Buffer = buffer;
    fullName.Length = (USHORT)(nameChars * sizeof(WCHAR));
    fullName.MaximumLength = (USHORT)((nameChars + 1) * sizeof(WCHAR));

    /* BaseDllName is the bare file name, not the full path. */
    baseName.Buffer = buffer + (RTL_NUMBER_OF(prefix) - 1);
    baseName.Length = (USHORT)((deviceChars + (RTL_NUMBER_OF(suffix) - 1)) * sizeof(WCHAR));
    baseName.MaximumLength = (USHORT)(baseName.Length + sizeof(WCHAR));

    entry->DllBase = (PVOID)g_ImageBase;
    entry->EntryPoint = NULL;
    entry->SizeOfImage = (ULONG)g_ImageSize;
    entry->FullDllName = fullName;
    entry->BaseDllName = baseName;
    /* Exact flag bits drift between builds and nothing load-bearing reads them. */
    entry->Flags = 0x00002000;
    entry->LoadCount = 1;
    entry->TlsIndex = 0;

    /*
     * PsLoadedModuleList is a DATA import, so the compiler addresses the IAT
     * slot itself for &PsLoadedModuleList instead of the list head - and that
     * slot is the LAST import in the table. head->Blink therefore reads 8 bytes
     * past the end of the IAT, which is zero, and InsertTailList writes through
     * NULL: bugcheck 0x1E with c0000005, a read of address 0. Dereferencing the
     * slot once yields the real list head.
     */
    head = *(PLIST_ENTRY*)&PsLoadedModuleList;

    if (!head)
    {
        ExFreePoolWithTag(block, g_PoolTag);
        return STATUS_PROCEDURE_NOT_FOUND;
    }

    /*
     * One insert, not three. InMemoryOrderLinks and InInitializationOrderLinks
     * belong to the user-mode PEB loader lists; the kernel walks
     * PsLoadedModuleList through InLoadOrderLinks and resolves every cursor with
     * CONTAINING_RECORD(cursor, KLDR_DATA_TABLE_ENTRY, InLoadOrderLinks). Linking
     * the same entry three times into that single list made each walker read the
     * entry 0x10 bytes off - which is what made EnumDeviceDrivers and
     * SystemModuleInformation fault with 0xC0000005 right after the fix. The two
     * unused links stay self-linked so RemoveEntryList stays safe on them.
     */
    InitializeListHead(&entry->InMemoryOrderLinks);
    InitializeListHead(&entry->InInitializationOrderLinks);

    InsertTailList(head, &entry->InLoadOrderLinks);

    g_ModuleEntry = block;
    return STATUS_SUCCESS;
}

/* Unlinks and frees the published entry. Runs before the image is freed. */
static VOID KmRetractModuleEntry(VOID)
{
    KM_LDR_ENTRY* entry;

    if (!g_ModuleEntry)
        return;

    entry = (KM_LDR_ENTRY*)((PUCHAR)g_ModuleEntry + 0x10);

    RemoveEntryList(&entry->InInitializationOrderLinks);
    RemoveEntryList(&entry->InMemoryOrderLinks);
    RemoveEntryList(&entry->InLoadOrderLinks);

    ExFreePoolWithTag(g_ModuleEntry, g_PoolTag);
    g_ModuleEntry = NULL;
}

static VOID KmUnload(PDRIVER_OBJECT driver)
{
    LARGE_INTEGER timeout;
    ULONG i;

    InterlockedExchange(&g_Unloading, 1);

    /*
     * Handlers already inside this image have to be out before it is freed, and
     * the loader frees it the moment this returns.
     *
     * The first wait is unconditional. A handler can increment in the window
     * between the flag going up and a count read, so a zero read is not proof
     * that none is running - but that handler will drop the event when it leaves,
     * and the settle wait catches it. The iteration cap is a backstop so a wedged
     * handler cannot keep the machine from unloading forever.
     */
    timeout.QuadPart = -1000000;   /* relative, 100ns units: 100 ms per pass */

    for (i = 0; i < 5; ++i)
    {
        KeWaitForSingleObject(&g_UnloadDrain, Executive, KernelMode, FALSE, &timeout);

        if (InterlockedCompareExchange(&g_ActiveCalls, 0, 0) == 0)
            break;
    }

    /* Stop the worker before anything it touches is released, then drop every
     * block. The last one stops the shared thread and waits for it, so the unmaps
     * below cannot race a slot that is still being serviced. */
    {
        ULONG b;

        for (b = 0; b < KM_BLOCK_COUNT; ++b)
            KmStopShared(b);
    }

    /* The table points into the image, so it goes before the image is freed. */
    if (g_FunctionTable)
    {
        if (g_RtlDeleteFunctionTable)
            g_RtlDeleteFunctionTable(g_FunctionTable);

        g_FunctionTable = NULL;
    }

    if (g_ObRegistered && g_ObRegistration)
    {
        ObUnRegisterCallbacks(g_ObRegistration);
        g_ObRegistration = NULL;
        g_ObRegistered = FALSE;
    }

    if (g_Symlink.Buffer)
    {
        IoDeleteSymbolicLink(&g_Symlink);
        /* Cleared so a reload of the same image cannot delete it twice. */
        g_Symlink.Buffer = NULL;
    }

    /* Before the image is freed, so the list never holds a dead pointer. */
    KmRetractModuleEntry();

    if (g_DeviceObject)
    {
        IoDeleteDevice(g_DeviceObject);
        g_DeviceObject = NULL;
    }

    UNREFERENCED_PARAMETER(driver);
}

/* ------------------------------------------------------------------ */
/* entry                                                               */
/* ------------------------------------------------------------------ */

/*
 * A run that never reached DriverUnload leaves its DEVICE_OBJECT and its
 * \DosDevices link behind. The object lives in pool, so it outlives the freed
 * image: the name stays owned while the device itself is unreachable - which is
 * why a later load both fails to open it and then fails to create it.
 */
#ifndef STATUS_OBJECT_NAME_COLLISION
#define STATUS_OBJECT_NAME_COLLISION ((NTSTATUS)0xC0000035L)
#endif
#ifndef STATUS_OBJECT_NAME_EXISTS
#define STATUS_OBJECT_NAME_EXISTS ((NTSTATUS)0xC0000034L)
#endif

NTSTATUS DriverEntry(_In_ PDRIVER_OBJECT driver, _In_ PUNICODE_STRING registryPath)
{
    UNICODE_STRING deviceName;
    NTSTATUS status;
    ULONG i;
    ULONG attempt;
    WCHAR baseName[KM_MAX_DEVICE_CHARS];
    KM_INIT_PARAMS* initParams = NULL;
    unsigned __int64 sharedUser = 0;
    ULONG sharedSize = 0;
    ULONG exceptionRva = 0;
    ULONG exceptionSize = 0;

    for (i = 0; i < KM_MAX_HIDDEN; ++i)
        RtlZeroMemory(&g_Hidden[i], sizeof(g_Hidden[i]));
    KeInitializeSpinLock(&g_HiddenLock);
    KeInitializeEvent(&g_UnloadDrain, NotificationEvent, FALSE);
    KeInitializeMutex(&g_SharedLock, 0);

    driver->DriverUnload = KmUnload;

    /* Every slot gets a real handler first; see KmDefaultDispatch. */
    for (i = 0; i <= IRP_MJ_MAXIMUM_FUNCTION; ++i)
        driver->MajorFunction[i] = KmDefaultDispatch;

    driver->MajorFunction[IRP_MJ_CREATE] = KmCreateClose;
    driver->MajorFunction[IRP_MJ_CLEANUP] = KmCleanup;
    driver->MajorFunction[IRP_MJ_CLOSE] = KmCreateClose;
    driver->MajorFunction[IRP_MJ_DEVICE_CONTROL] = KmDispatch;

    /*
     * The loader hands us a zeroed DRIVER_OBJECT. IoCreateDevice walks
     * DriverObject->DeviceObject and some consumers read Type/Size/DriverStart,
     * so populate the object before touching any I/O API.
     */
    driver->Type = IO_TYPE_DRIVER;
    driver->Size = sizeof(DRIVER_OBJECT);
    driver->DriverStart = NULL;
    driver->DriverSize = 0;

    /*
     * Zeroed is not initialised. IoCreateDevice calls
     * InsertTailList(&DriverObject->DeviceObject, ...), which reads Blink and
     * dereferences it - a zeroed head has Flink = Blink = NULL and faults. The
     * head must be a real, self-linked list head before any I/O API is touched.
     *
     * DeviceObject is declared PDEVICE_OBJECT but IoCreateDevice treats the field
     * as a LIST_ENTRY, so the cast is required - the OS does the same internally.
     */
    InitializeListHead((PLIST_ENTRY)&driver->DeviceObject);

    /* Manual mapping hands us the loader's init block in the 2nd argument. */
    if (KmLooksLikePointer(registryPath))
    {
        __try
        {
            KM_INIT_PARAMS* params = (KM_INIT_PARAMS*)registryPath;
            if (params->Magic == KM_INIT_MAGIC)
            {
                initParams = params;

                if (params->ClientPid)
                    g_ClientPid = params->ClientPid;

                /* Per-install device name, chosen by the loader. */
                if (params->DeviceName[0])
                    RtlStringCchCopyNW(g_DeviceName, RTL_NUMBER_OF(g_DeviceName),
                                       params->DeviceName, KM_MAX_DEVICE_CHARS - 1);

                g_AltitudeSeed = params->AltitudeSeed;

                if (params->PoolTag)
                    g_PoolTag = params->PoolTag;

                sharedUser = params->SharedUser;
                sharedSize = params->SharedSize;

                driver->DriverStart = (void*)params->ImageBase;
                driver->DriverSize = (ULONG)params->ImageSize;

                g_ImageBase = params->ImageBase;
                g_ImageSize = params->ImageSize;
                g_DriverObject = driver;

                exceptionRva = params->ExceptionRva;
                exceptionSize = params->ExceptionSize;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            g_ClientPid = 0;
        }
    }

    /*
     * Register the image's own exception table, before any code below can take a
     * fault. This is the difference between a survivable fault and a bugcheck: with
     * no table registered, RtlLookupFunctionEntry finds nothing for a RIP inside a
     * mapped image and every __try/__except in it escalates to 0x1E instead of being
     * caught.
     *
     * Done here rather than from the loader because this side is already in kernel
     * mode and links RtlAddFunctionTable directly - no pool copy, no export walk, no
     * syscall trampoline to go wrong. The RVAs are relative to the base handed over,
     * so the image base is that base and they are used as they sit.
     *
     * Non-fatal: a refused table costs the guards, not the load.
     */
    if (exceptionRva && exceptionSize >= sizeof(KM_RUNTIME_FUNCTION) &&
        exceptionRva < g_ImageSize && exceptionSize <= g_ImageSize - exceptionRva)
    {
        KmResolveTableRoutines();

        if (!g_RtlAddFunctionTable)
        {
            DbgPrint("kernmem: RtlAddFunctionTable unresolved - guarded faults will bugcheck\n");
        }
        else
        {
            g_FunctionTable = (PKM_RUNTIME_FUNCTION)(g_ImageBase + exceptionRva);

            if (!g_RtlAddFunctionTable(g_FunctionTable,
                                       exceptionSize / sizeof(KM_RUNTIME_FUNCTION),
                                       (ULONG_PTR)g_ImageBase))
            {
                g_FunctionTable = NULL;
                DbgPrint("kernmem: exception table refused - guarded faults will bugcheck\n");
            }
            else
            {
                DbgPrint("kernmem: %lu exception entries registered\n",
                         exceptionSize / sizeof(KM_RUNTIME_FUNCTION));
            }
        }
    }
    else if (initParams)
    {
        DbgPrint("kernmem: no exception directory handed over - guarded faults will bugcheck\n");
    }

    /*
     * Resolve the identity. An empty name means a service load or an old loader,
     * so the historical static namespace stays as the fallback.
     */
    if (!g_DeviceName[0])
        RtlStringCchCopyNW(g_DeviceName, RTL_NUMBER_OF(g_DeviceName),
                           L"Kernmem", KM_MAX_DEVICE_CHARS - 1);

    RtlStringCchCopyW(baseName, RTL_NUMBER_OF(baseName), g_DeviceName);

    /* A real driver always carries a DriverName. An unnamed driver object that
     * owns a named device is one of the cheapest mismatches to spot. */
    /* Copy + cat rather than RtlStringCchPrintfW: the printf family pulls in
     * __stdio_common_vswprintf from the UCRT, and this image links only
     * ntoskrnl/BufferOverflowK/wdm. */
    RtlStringCchCopyW(g_DriverName, RTL_NUMBER_OF(g_DriverName), L"\\Driver\\");
    RtlStringCchCatW(g_DriverName, RTL_NUMBER_OF(g_DriverName), baseName);
    RtlInitUnicodeString(&driver->DriverName, g_DriverName);

    /*
     * Published here, early, before anything below can take a fault.
     *
     * This is what makes the __try blocks in this image work at all. The unwinder
     * finds an image's .pdata by looking its RIP up in PsLoadedModuleList; an
     * unregistered mapping therefore has no unwind data, so RtlLookupFunctionEntry
     * returns NULL and a guarded fault escalates to 0x1E instead of being caught.
     *
     * Registering through RtlAddFunctionTable was supposed to cover that, but
     * ntoskrnl does not export it on this build, so MmGetSystemRoutineAddress
     * returns NULL every time and the table never registers. Listing the image is
     * the route that does not depend on that export. Name and base are final by
     * this point, which is all the entry needs.
     */
    if (!NT_SUCCESS(KmPublishModuleEntry()))
        DbgPrint("kernmem: could not publish module entry\n");

    /*
     * A name left owned by a dead image must not fail the load. Suffix and retry
     * (base, base_1, base_2 ...), and on success report the name actually used
     * back through the init block - the loader opens whatever comes back, so a
     * suffixed name is not a problem for it. Only a collision retries; any other
     * failure is final.
     */
    status = STATUS_OBJECT_NAME_COLLISION;

    for (attempt = 0; attempt < 16; ++attempt)
    {
        if (attempt)
        {
            WCHAR suffix[4];

            suffix[0] = L'_';
            suffix[1] = (WCHAR)(attempt < 10 ? (L'0' + attempt) : (L'a' + attempt - 10));
            suffix[2] = 0;
            suffix[3] = 0;

            RtlStringCchCopyW(g_DeviceName, RTL_NUMBER_OF(g_DeviceName), baseName);
            RtlStringCchCatW(g_DeviceName, RTL_NUMBER_OF(g_DeviceName), suffix);
        }

        /* Per attempt, from the name in hand. The client derives the IOCTL type
         * from the name it is told was used, so a suffixed name has to yield the
         * same type on both sides. */
        g_DeviceType = KmDeviceTypeFor(g_DeviceName);

        RtlStringCchCopyW(g_DevicePath, RTL_NUMBER_OF(g_DevicePath), L"\\Device\\");
        RtlStringCchCatW(g_DevicePath, RTL_NUMBER_OF(g_DevicePath), g_DeviceName);
        RtlInitUnicodeString(&deviceName, g_DevicePath);

        RtlStringCchCopyW(g_SymlinkName, RTL_NUMBER_OF(g_SymlinkName), L"\\DosDevices\\");
        RtlStringCchCatW(g_SymlinkName, RTL_NUMBER_OF(g_SymlinkName), g_DeviceName);
        RtlInitUnicodeString(&g_Symlink, g_SymlinkName);

        g_DeviceObject = NULL;
        status = IoCreateDevice(driver, 0, &deviceName, g_DeviceType,
                                FILE_DEVICE_SECURE_OPEN, FALSE, &g_DeviceObject);

        if (!NT_SUCCESS(status))
        {
            if (status == STATUS_OBJECT_NAME_COLLISION || status == STATUS_OBJECT_NAME_EXISTS)
                continue;

            break;
        }

        g_DeviceObject->Flags |= DO_BUFFERED_IO;
        /* Cleared automatically only for devices created inside a real
         * DriverEntry. Under IoCreateDriver it is not, and an initialising device
         * cannot be opened. */
        g_DeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;

        status = IoCreateSymbolicLink(&g_Symlink, &deviceName);

        if (NT_SUCCESS(status))
            break;

        IoDeleteDevice(g_DeviceObject);
        g_DeviceObject = NULL;

        if (status != STATUS_OBJECT_NAME_COLLISION && status != STATUS_OBJECT_NAME_EXISTS)
            break;
    }

    if (!NT_SUCCESS(status))
    {
        KmTeardownShared();

        if (g_Symlink.Buffer)
        {
            IoDeleteSymbolicLink(&g_Symlink);
            g_Symlink.Buffer = NULL;
        }

        KmRetractModuleEntry();

        if (g_DeviceObject)
        {
            IoDeleteDevice(g_DeviceObject);
            g_DeviceObject = NULL;
        }

        if (g_FunctionTable)
        {
            if (g_RtlDeleteFunctionTable)
                g_RtlDeleteFunctionTable(g_FunctionTable);

            g_FunctionTable = NULL;
        }

        return status;
    }

    /*
     * Brought up only now, with the device and symlink already in place. Starting
     * it earlier put a live system thread on the blocks while the entry was still
     * building the device, and left a worker running behind a load that could
     * still fail - the half-up state the next load then collided with.
     *
     * Optional: without it every read stays on IOCTL, so a failure here is not
     * fatal to the load and needs no unwind.
     */
    if (sharedUser)
    {
        if (KmStartShared(KM_BLOCK_KERNEL, sharedUser, sharedSize))
            DbgPrint("kernmem: shared block up, reads need no IOCTL\n");
        else
            DbgPrint("kernmem: shared block failed, reads stay on IOCTL\n");
    }

    /* The driver object name has to follow the device name actually used, or an
     * unnamed-looking driver owns a named device. */
    RtlStringCchCopyW(g_DriverName, RTL_NUMBER_OF(g_DriverName), L"\\Driver\\");
    RtlStringCchCatW(g_DriverName, RTL_NUMBER_OF(g_DriverName), g_DeviceName);
    RtlInitUnicodeString(&driver->DriverName, g_DriverName);

    if (attempt)
        DbgPrint("kernmem: device name taken, using %ws\n", g_DeviceName);

    /* Tell the loader which name was actually created. */
    if (initParams)
        RtlStringCchCopyNW(initParams->DeviceName, KM_MAX_DEVICE_CHARS,
                           g_DeviceName, KM_MAX_DEVICE_CHARS - 1);

    /*
     * The entry is already published above. This registers the object callback,
     * which walks the process object type chain by hand, so it stays guarded and
     * non-fatal - the driver works without it.
     */
    __try
    {
        KmRegisterObjectCallback();
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        DbgPrint("kernmem: callback registration faulted\n");
    }

    return STATUS_SUCCESS;
}
