/*
 * VanguardBypass kernel driver
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

#include "vgb_shared.h"
#include "vgb_ntcompat.h"

#define VGB_TAG 'BGVb'

#define VGB_MAX_HIDDEN 32

typedef struct _VGB_HIDDEN_ENTRY
{
    unsigned long Pid;
    void* Entry;
    void* DllBase;
} VGB_HIDDEN_ENTRY;

/* Layout of an LDR_DATA_TABLE_ENTRY from the three list heads onward. */
typedef struct _VGB_LDR_ENTRY
{
    LIST_ENTRY InLoadOrderLinks;
    LIST_ENTRY InMemoryOrderLinks;
    LIST_ENTRY InInitializationOrderLinks;
    void* DllBase;
    void* EntryPoint;
    unsigned long SizeOfImage;
    UNICODE_STRING FullDllName;
    UNICODE_STRING BaseDllName;
} VGB_LDR_ENTRY;

static PDEVICE_OBJECT g_DeviceObject = NULL;
static ULONG          g_ClientPid = 0;
static PVOID          g_ObRegistration = NULL;
static BOOLEAN        g_ObRegistered = FALSE;
static KSPIN_LOCK     g_HiddenLock;
static volatile LONG  g_Unloading = 0;
static VGB_HIDDEN_ENTRY g_Hidden[VGB_MAX_HIDDEN];

/* Own mapping, reported to the loader so unload can free it without a state
 * file. Populated from the init block at entry. */
static unsigned __int64 g_ImageBase = 0;
static unsigned __int64 g_ImageSize = 0;
static PVOID            g_DriverObject = NULL;

/* ------------------------------------------------------------------ */
/* helpers                                                             */
/* ------------------------------------------------------------------ */

/* The init block arrives as DriverEntry's 2nd argument. Under manual mapping
 * that is a kernel pool pointer, under service load it is a registry path, so
 * only plausibility is checked here and the magic is validated under SEH. */
static BOOLEAN VgbLooksLikePointer(const void* p)
{
    return ((ULONG_PTR)p > 0x10000);
}

static PEPROCESS VgbReferenceProcess(ULONG pid)
{
    PEPROCESS proc = NULL;
    if (!pid)
        return NULL;
    if (!NT_SUCCESS(PsLookupProcessByProcessId((HANDLE)(ULONG_PTR)pid, &proc)))
        return NULL;
    return proc;
}

static NTSTATUS VgbCopyFromProcess(PEPROCESS proc, void* remote, void* local, SIZE_T size)
{
    SIZE_T done = 0;
    return MmCopyVirtualMemory(proc, remote, PsGetCurrentProcess(), local, size, KernelMode, &done);
}

static NTSTATUS VgbCopyToProcess(PEPROCESS proc, void* remote, void* local, SIZE_T size)
{
    SIZE_T done = 0;
    return MmCopyVirtualMemory(PsGetCurrentProcess(), local, proc, remote, size, KernelMode, &done);
}

/* ------------------------------------------------------------------ */
/* object callback: give our client its access rights back             */
/* ------------------------------------------------------------------ */

static OB_PREOP_CALLBACK_STATUS VgbProcessPreOperation(
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

static NTSTATUS VgbRegisterObjectCallback(void)
{
    /*
     * The pre-operation chain runs in DESCENDING altitude order, so our filter
     * must sit above Vanguard's in order to put back the rights it strips.
     * Registering at an altitude another filter already uses fails with
     * STATUS_OBJECT_NAME_COLLISION, so several candidates are tried in order.
     */
    static const WCHAR* candidates[] = { L"321000", L"321100", L"329000", L"329500" };
    OB_OPERATION_REGISTRATION operations[1];
    OB_CALLBACK_REGISTRATION registration;
    NTSTATUS status = STATUS_OBJECT_NAME_COLLISION;
    ULONG i;

    RtlZeroMemory(operations, sizeof(operations));
    operations[0].ObjectType = PsProcessType;
    operations[0].Operations = OB_OPERATION_HANDLE_CREATE | OB_OPERATION_HANDLE_DUPLICATE;
    operations[0].PreOperation = VgbProcessPreOperation;
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
#define VGB_IMAGE_NAME_MAX 15

static BOOLEAN VgbImageNameMatches(const WCHAR* image, const WCHAR* wanted)
{
    size_t i;

    if (!image || !image[0] || !wanted || !wanted[0])
        return FALSE;

    for (i = 0; i < VGB_IMAGE_NAME_MAX && image[i]; ++i)
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

    /* i == VGB_IMAGE_NAME_MAX means the stored name was truncated: accept it. */
    return (i >= VGB_IMAGE_NAME_MAX) || (wanted[i] == 0);
}

/* Last path component of an image path. The system information image names are
 * ANSI, so this works on bytes. */
/* Base name of an image path. ImageName is a counted UNICODE_STRING, so the
 * scan is length-bounded and works in wchar units (walking bytes would stop at
 * the high byte of the first character). */
static const WCHAR* VgbBaseName(const WCHAR* path, ULONG chars)
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
static ULONG VgbFindPidByNameQuery(const wchar_t* name)
{
    ULONG size = 0x20000;
    ULONG attempt;

    for (attempt = 0; attempt < 8; ++attempt)
    {
        NTSTATUS status;
        ULONG needed = size;
        ULONG offset = 0;
        ULONG found = 0;
        PVOID buffer = ExAllocatePoolWithTag(NonPagedPool, size, VGB_TAG);

        if (!buffer)
            return 0;

        status = ZwQuerySystemInformation(SystemProcessInformation, buffer, size, &needed);
        if (status == STATUS_INFO_LENGTH_MISMATCH)
        {
            ExFreePoolWithTag(buffer, VGB_TAG);
            size = needed + 0x1000;
            continue;
        }
        if (!NT_SUCCESS(status))
        {
            ExFreePoolWithTag(buffer, VGB_TAG);
            return 0;
        }

        for (;;)
        {
            VGB_SYSTEM_PROCESS_INFORMATION* entry =
                (VGB_SYSTEM_PROCESS_INFORMATION*)((PUCHAR)buffer + offset);

            if (entry->ImageName.Buffer && entry->ImageName.Length >= sizeof(WCHAR))
            {
                const WCHAR* start = (const WCHAR*)entry->ImageName.Buffer;
                const ULONG chars = entry->ImageName.Length / sizeof(WCHAR);
                const WCHAR* base = VgbBaseName(start, chars);
                const ULONG remaining = (ULONG)((start + chars) - base);
                WCHAR wide[VGB_MAX_NAME_CHARS];
                ULONG i;

                for (i = 0; i < remaining && i < VGB_MAX_NAME_CHARS - 1 && base[i]; ++i)
                    wide[i] = base[i];
                wide[i] = 0;

                if (VgbImageNameMatches(wide, name))
                {
                    found = (ULONG)(ULONG_PTR)entry->UniqueProcessId;
                    break;
                }
            }

            if (!entry->NextEntryOffset)
                break;
            offset += entry->NextEntryOffset;
        }

        ExFreePoolWithTag(buffer, VGB_TAG);
        return found;
    }

    return 0;
}

static ULONG VgbFindPidByName(const wchar_t* name)
{
    static VGB_PFN_GET_NEXT_PROCESS getNext = NULL;
    static BOOLEAN resolved = FALSE;
    PEPROCESS proc;

    if (!name || !name[0])
        return 0;

    if (!resolved)
    {
        UNICODE_STRING routine;

        RtlInitUnicodeString(&routine, L"PsGetNextProcess");
        getNext = (VGB_PFN_GET_NEXT_PROCESS)MmGetSystemRoutineAddress(&routine);
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
                WCHAR wide[VGB_IMAGE_NAME_MAX + 1];
                size_t i;

                for (i = 0; i < VGB_IMAGE_NAME_MAX && image[i]; ++i)
                    wide[i] = (WCHAR)image[i];
                wide[i] = 0;

                if (VgbImageNameMatches(wide, name))
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

    return VgbFindPidByNameQuery(name);
}

static NTSTATUS VgbGetMainBase(ULONG pid, unsigned __int64* base)
{
    PEPROCESS proc = VgbReferenceProcess(pid);
    if (!proc)
        return STATUS_INVALID_PARAMETER;

    *base = (unsigned __int64)PsGetProcessSectionBaseAddress(proc);
    ObDereferenceObject(proc);
    return STATUS_SUCCESS;
}

/* Must be called attached to the target process. */
static NTSTATUS VgbWalkModules(ULONG pid, VGB_MODULE_LIST* list, ULONG capacity)
{
    PEPROCESS proc = NULL;
    KAPC_STATE apc;
    NTSTATUS status = STATUS_SUCCESS;
    VGB_PEB* peb;
    LIST_ENTRY* head;
    LIST_ENTRY* cursor;
    ULONG count = 0;
    BOOLEAN truncated = FALSE;

    proc = VgbReferenceProcess(pid);
    if (!proc)
        return STATUS_INVALID_PARAMETER;

    KeStackAttachProcess(proc, &apc);
    __try
    {
        peb = (VGB_PEB*)PsGetProcessPeb(proc);
        if (!peb || !peb->Ldr)
        {
            status = STATUS_NOT_FOUND;
        }
        else
        {
            head = &peb->Ldr->InMemoryOrderModuleList;
            for (cursor = head->Flink; cursor != head; cursor = cursor->Flink)
            {
                VGB_LDR_ENTRY* entry = CONTAINING_RECORD(cursor, VGB_LDR_ENTRY, InMemoryOrderLinks);

                if (count >= capacity)
                {
                    truncated = TRUE;
                    break;
                }

                list->Entries[count].Base = (unsigned __int64)entry->DllBase;
                list->Entries[count].SizeOfImage = entry->SizeOfImage;
                list->Entries[count].Pad = 0;
                list->Entries[count].Name[0] = 0;

                if (entry->BaseDllName.Buffer && entry->BaseDllName.Length)
                {
                    ULONG chars = entry->BaseDllName.Length / sizeof(WCHAR);
                    if (chars >= VGB_MAX_NAME_CHARS)
                        chars = VGB_MAX_NAME_CHARS - 1;
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

static NTSTATUS VgbGetModuleBase(ULONG pid, const wchar_t* name, unsigned __int64* base)
{
    PEPROCESS proc = NULL;
    KAPC_STATE apc;
    NTSTATUS status = STATUS_NOT_FOUND;
    UNICODE_STRING wanted;
    VGB_PEB* peb;
    LIST_ENTRY* head;
    LIST_ENTRY* cursor;

    *base = 0;
    RtlInitUnicodeString(&wanted, name);

    proc = VgbReferenceProcess(pid);
    if (!proc)
        return STATUS_INVALID_PARAMETER;

    KeStackAttachProcess(proc, &apc);
    __try
    {
        peb = (VGB_PEB*)PsGetProcessPeb(proc);
        if (peb && peb->Ldr)
        {
            head = &peb->Ldr->InMemoryOrderModuleList;
            for (cursor = head->Flink; cursor != head; cursor = cursor->Flink)
            {
                VGB_LDR_ENTRY* entry = CONTAINING_RECORD(cursor, VGB_LDR_ENTRY, InMemoryOrderLinks);
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
/* LDR unlinking (hide an injected module from Vanguard's scan)        */
/* ------------------------------------------------------------------ */

static NTSTATUS VgbSetModuleHidden(ULONG pid, unsigned __int64 base, BOOLEAN hide)
{
    PEPROCESS proc = NULL;
    KAPC_STATE apc;
    NTSTATUS status = STATUS_NOT_FOUND;
    VGB_PEB* peb;
    LIST_ENTRY* head;
    LIST_ENTRY* cursor;

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
        for (i = 0; i < VGB_MAX_HIDDEN; ++i)
        {
            if (g_Hidden[i].Entry && g_Hidden[i].DllBase == (void*)base && g_Hidden[i].Pid == pid)
            {
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

    proc = VgbReferenceProcess(pid);
    if (!proc)
        return STATUS_INVALID_PARAMETER;

    KeStackAttachProcess(proc, &apc);
    __try
    {
        peb = (VGB_PEB*)PsGetProcessPeb(proc);
        if (peb && peb->Ldr)
        {
            head = &peb->Ldr->InMemoryOrderModuleList;
            for (cursor = head->Flink; cursor != head; cursor = cursor->Flink)
            {
                VGB_LDR_ENTRY* entry = CONTAINING_RECORD(cursor, VGB_LDR_ENTRY, InMemoryOrderLinks);
                if ((unsigned __int64)entry->DllBase != base)
                    continue;

                if (hide)
                {
                    RemoveEntryList(&entry->InLoadOrderLinks);
                    RemoveEntryList(&entry->InMemoryOrderLinks);
                    RemoveEntryList(&entry->InInitializationOrderLinks);
                    InitializeListHead(&entry->InLoadOrderLinks);
                    InitializeListHead(&entry->InMemoryOrderLinks);
                    InitializeListHead(&entry->InInitializationOrderLinks);
                }
                else
                {
                    /* Inserting against the list HEAD is valid even when the
                     * list is empty; CONTAINING_RECORD on an empty list would
                     * compute a bogus entry. */
                    InsertHeadList(&peb->Ldr->InLoadOrderModuleList, &entry->InLoadOrderLinks);
                    InsertHeadList(&peb->Ldr->InMemoryOrderModuleList, &entry->InMemoryOrderLinks);
                    InsertHeadList(&peb->Ldr->InInitializationOrderModuleList, &entry->InInitializationOrderLinks);
                }

                status = STATUS_SUCCESS;
                break;
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
            for (i = 0; i < VGB_MAX_HIDDEN; ++i)
            {
                if (!g_Hidden[i].Entry)
                {
                    g_Hidden[i].Pid = pid;
                    g_Hidden[i].DllBase = (void*)base;
                    g_Hidden[i].Entry = (void*)base;
                    break;
                }
            }
        }
        else
        {
            for (i = 0; i < VGB_MAX_HIDDEN; ++i)
            {
                if (g_Hidden[i].DllBase == (void*)base && g_Hidden[i].Pid == pid)
                {
                    g_Hidden[i].Entry = NULL;
                    g_Hidden[i].DllBase = NULL;
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
static BOOLEAN VgbAddressCopyable(ULONG_PTR address, ULONG size)
{
    ULONG_PTR last;

    if (!address)
        return FALSE;

    last = address + size - 1;

    /* Wrapped the address space. */
    if (last < address)
        return FALSE;

    if (address <= (ULONG_PTR)MmHighestUserAddress)
        return TRUE;

    if (!MmIsAddressValid((PVOID)address) || !MmIsAddressValid((PVOID)last))
        return FALSE;

    return TRUE;
}

static NTSTATUS VgbHandleRead(PVOID in, ULONG inLen, PVOID out, ULONG outLen, PULONG_PTR written)
{
    VGB_MEM_REQ* req = (VGB_MEM_REQ*)in;
    PEPROCESS proc;
    NTSTATUS status;
    ULONG size;

    if (inLen < sizeof(VGB_MEM_REQ) || !outLen)
        return STATUS_BUFFER_TOO_SMALL;

    size = req->Size;
    if (size > VGB_MAX_XFER)
        size = VGB_MAX_XFER;
    if (size > outLen)
        size = outLen;
    if (!size)
        return STATUS_INVALID_PARAMETER;

    if (!VgbAddressCopyable((ULONG_PTR)req->Address, size))
        return STATUS_PARTIAL_COPY;

    proc = VgbReferenceProcess(req->Pid);
    if (!proc)
        return STATUS_INVALID_PARAMETER;

    /* MmCopyVirtualMemory normally reports a bad address as STATUS_PARTIAL_COPY,
     * but a probe can still raise on some builds. SEH turns that into a status
     * instead of a bugcheck the user has to reboot from. */
    __try
    {
        status = VgbCopyFromProcess(proc, (void*)req->Address, out, size);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        status = STATUS_PARTIAL_COPY;
    }

    ObDereferenceObject(proc);

    *written = NT_SUCCESS(status) ? size : 0;
    return status;
}

static NTSTATUS VgbHandleWrite(PVOID in, ULONG inLen, PULONG_PTR written)
{
    VGB_MEM_REQ* req = (VGB_MEM_REQ*)in;
    PEPROCESS proc;
    NTSTATUS status;
    ULONG size;
    PVOID data;

    if (inLen < sizeof(VGB_MEM_REQ))
        return STATUS_BUFFER_TOO_SMALL;

    size = req->Size;
    if (size > VGB_MAX_XFER)
        size = VGB_MAX_XFER;
    if (!size || inLen < sizeof(VGB_MEM_REQ) + size)
        return STATUS_BUFFER_TOO_SMALL;

    data = (PVOID)((PUCHAR)in + sizeof(VGB_MEM_REQ));

    if (!VgbAddressCopyable((ULONG_PTR)req->Address, size))
        return STATUS_PARTIAL_COPY;

    proc = VgbReferenceProcess(req->Pid);
    if (!proc)
        return STATUS_INVALID_PARAMETER;

    __try
    {
        status = VgbCopyToProcess(proc, (void*)req->Address, data, size);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        status = STATUS_PARTIAL_COPY;
    }

    ObDereferenceObject(proc);

    *written = NT_SUCCESS(status) ? size : 0;
    return status;
}

static NTSTATUS VgbDispatch(PDEVICE_OBJECT device, PIRP irp)
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

    /* The loader unmaps and frees this image right after DriverUnload returns;
     * an IRP arriving after that would dispatch into freed pool. */
    if (InterlockedCompareExchange(&g_Unloading, 1, 1) != 0)
    {
        irp->IoStatus.Status = STATUS_DELETE_PENDING;
        irp->IoStatus.Information = 0;
        IoCompleteRequest(irp, IO_NO_INCREMENT);
        return STATUS_DELETE_PENDING;
    }

    stack = IoGetCurrentIrpStackLocation(irp);
    code = stack->Parameters.DeviceIoControl.IoControlCode;
    inLen = stack->Parameters.DeviceIoControl.InputBufferLength;
    outLen = stack->Parameters.DeviceIoControl.OutputBufferLength;
    in = irp->AssociatedIrp.SystemBuffer;
    out = irp->AssociatedIrp.SystemBuffer;

    if (code == IOCTL_VGB_GET_VERSION)
    {
        if (outLen >= sizeof(ULONG))
        {
            *(ULONG*)out = VGB_PROTOCOL_VERSION;
            written = sizeof(ULONG);
            status = STATUS_SUCCESS;
        }
        else
        {
            status = STATUS_BUFFER_TOO_SMALL;
        }
    }
    else if (code == IOCTL_VGB_GET_INFO)
    {
        /* Teardown reads this: the loader may have lost its state file, and the
         * only reliable record of the mapping lives here. */
        if (outLen >= sizeof(VGB_INFO))
        {
            VGB_INFO* info = (VGB_INFO*)out;

            info->ImageBase = g_ImageBase;
            info->ImageSize = g_ImageSize;
            info->DriverObject = (unsigned __int64)g_DriverObject;
            written = sizeof(VGB_INFO);
            status = STATUS_SUCCESS;
        }
        else
        {
            status = STATUS_BUFFER_TOO_SMALL;
        }
    }
    else if (code == IOCTL_VGB_SET_CLIENT_PID)
    {
        if (inLen >= sizeof(VGB_PID_REQ))
        {
            g_ClientPid = ((VGB_PID_REQ*)in)->Pid;
            status = STATUS_SUCCESS;
        }
        else
        {
            status = STATUS_BUFFER_TOO_SMALL;
        }
    }
    else if (code == IOCTL_VGB_GET_PID_BY_NAME)
    {
        if (inLen >= sizeof(VGB_NAME_REQ) && outLen >= sizeof(VGB_PID_REQ))
        {
            ((VGB_NAME_REQ*)in)->Name[VGB_MAX_NAME_CHARS - 1] = 0;
            ((VGB_PID_REQ*)out)->Pid = VgbFindPidByName(((VGB_NAME_REQ*)in)->Name);
            ((VGB_PID_REQ*)out)->Pad = 0;
            written = sizeof(VGB_PID_REQ);
            status = STATUS_SUCCESS;
        }
        else
        {
            status = STATUS_BUFFER_TOO_SMALL;
        }
    }
    else if (code == IOCTL_VGB_GET_MODULE_BASE)
    {
        if (inLen >= sizeof(VGB_NAME_REQ) && outLen >= sizeof(unsigned __int64))
        {
            ((VGB_NAME_REQ*)in)->Name[VGB_MAX_NAME_CHARS - 1] = 0;
            status = VgbGetModuleBase(((VGB_NAME_REQ*)in)->Pid,
                                      ((VGB_NAME_REQ*)in)->Name,
                                      (unsigned __int64*)out);
            written = NT_SUCCESS(status) ? sizeof(unsigned __int64) : 0;
        }
        else
        {
            status = STATUS_BUFFER_TOO_SMALL;
        }
    }
    else if (code == IOCTL_VGB_GET_MAIN_BASE)
    {
        if (inLen >= sizeof(VGB_PID_REQ) && outLen >= sizeof(unsigned __int64))
        {
            status = VgbGetMainBase(((VGB_PID_REQ*)in)->Pid, (unsigned __int64*)out);
            written = NT_SUCCESS(status) ? sizeof(unsigned __int64) : 0;
        }
        else
        {
            status = STATUS_BUFFER_TOO_SMALL;
        }
    }
    else if (code == IOCTL_VGB_READ)
    {
        status = VgbHandleRead(in, inLen, out, outLen, &written);
    }
    else if (code == IOCTL_VGB_WRITE)
    {
        status = VgbHandleWrite(in, inLen, &written);
    }
    else if (code == IOCTL_VGB_ENUM_MODULES)
    {
        if (inLen >= sizeof(VGB_PID_REQ) &&
            outLen >= FIELD_OFFSET(VGB_MODULE_LIST, Entries) + sizeof(VGB_MODULE_INFO))
        {
            ULONG capacity = (outLen - FIELD_OFFSET(VGB_MODULE_LIST, Entries)) / sizeof(VGB_MODULE_INFO);

            if (capacity > VGB_MAX_MODULES)
                capacity = VGB_MAX_MODULES;
            status = VgbWalkModules(((VGB_PID_REQ*)in)->Pid, (VGB_MODULE_LIST*)out, capacity);
            if (NT_SUCCESS(status))
                written = FIELD_OFFSET(VGB_MODULE_LIST, Entries) +
                          ((VGB_MODULE_LIST*)out)->Count * sizeof(VGB_MODULE_INFO);
        }
        else
        {
            status = STATUS_BUFFER_TOO_SMALL;
        }
    }
    else if (code == IOCTL_VGB_HIDE_MODULE || code == IOCTL_VGB_UNHIDE_MODULE)
    {
        typedef struct _VGB_BASE_REQ
        {
            ULONG Pid;
            ULONG Pad;
            unsigned __int64 Base;
        } VGB_BASE_REQ;

        if (inLen >= sizeof(VGB_BASE_REQ))
        {
            status = VgbSetModuleHidden(((VGB_BASE_REQ*)in)->Pid,
                                        ((VGB_BASE_REQ*)in)->Base,
                                        (BOOLEAN)(code == IOCTL_VGB_HIDE_MODULE));
        }
        else
        {
            status = STATUS_BUFFER_TOO_SMALL;
        }
    }

    irp->IoStatus.Status = status;
    irp->IoStatus.Information = written;
    IoCompleteRequest(irp, IO_NO_INCREMENT);
    return status;
}

static NTSTATUS VgbCreateClose(PDEVICE_OBJECT device, PIRP irp)
{
    UNREFERENCED_PARAMETER(device);
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
static NTSTATUS VgbDefaultDispatch(PDEVICE_OBJECT device, PIRP irp)
{
    UNREFERENCED_PARAMETER(device);
    irp->IoStatus.Status = STATUS_INVALID_DEVICE_REQUEST;
    irp->IoStatus.Information = 0;
    IoCompleteRequest(irp, IO_NO_INCREMENT);
    return STATUS_INVALID_DEVICE_REQUEST;
}

static VOID VgbUnload(PDRIVER_OBJECT driver)
{
    UNICODE_STRING symlink;

    InterlockedExchange(&g_Unloading, 1);

    if (g_ObRegistered && g_ObRegistration)
    {
        ObUnRegisterCallbacks(g_ObRegistration);
        g_ObRegistration = NULL;
        g_ObRegistered = FALSE;
    }

    RtlInitUnicodeString(&symlink, VGB_SYMLINK_NAME);
    IoDeleteSymbolicLink(&symlink);

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

NTSTATUS DriverEntry(_In_ PDRIVER_OBJECT driver, _In_ PUNICODE_STRING registryPath)
{
    UNICODE_STRING deviceName;
    UNICODE_STRING symlink;
    NTSTATUS status;
    ULONG i;

    for (i = 0; i < VGB_MAX_HIDDEN; ++i)
        RtlZeroMemory(&g_Hidden[i], sizeof(g_Hidden[i]));
    KeInitializeSpinLock(&g_HiddenLock);

    driver->DriverUnload = VgbUnload;

    /* Every slot gets a real handler first; see VgbDefaultDispatch. */
    for (i = 0; i <= IRP_MJ_MAXIMUM_FUNCTION; ++i)
        driver->MajorFunction[i] = VgbDefaultDispatch;

    driver->MajorFunction[IRP_MJ_CREATE] = VgbCreateClose;
    driver->MajorFunction[IRP_MJ_CLEANUP] = VgbCreateClose;
    driver->MajorFunction[IRP_MJ_CLOSE] = VgbCreateClose;
    driver->MajorFunction[IRP_MJ_DEVICE_CONTROL] = VgbDispatch;

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
    if (VgbLooksLikePointer(registryPath))
    {
        __try
        {
            VGB_INIT_PARAMS* params = (VGB_INIT_PARAMS*)registryPath;
            if (params->Magic == VGB_INIT_MAGIC)
            {
                if (params->ClientPid)
                    g_ClientPid = params->ClientPid;

                driver->DriverStart = (void*)params->ImageBase;
                driver->DriverSize = (ULONG)params->ImageSize;

                g_ImageBase = params->ImageBase;
                g_ImageSize = params->ImageSize;
                g_DriverObject = driver;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            g_ClientPid = 0;
        }
    }

    RtlInitUnicodeString(&deviceName, VGB_DEVICE_NAME);
    status = IoCreateDevice(driver, 0, &deviceName, VGB_DEVICE_TYPE, FILE_DEVICE_SECURE_OPEN, FALSE, &g_DeviceObject);
    if (!NT_SUCCESS(status))
        return status;

    g_DeviceObject->Flags |= DO_BUFFERED_IO;
    /* Cleared automatically only for devices created inside a real DriverEntry.
     * Under IoCreateDriver it is not, and an initialising device cannot be
     * opened. */
    g_DeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;

    RtlInitUnicodeString(&symlink, VGB_SYMLINK_NAME);
    status = IoCreateSymbolicLink(&symlink, &deviceName);
    if (!NT_SUCCESS(status))
    {
        IoDeleteDevice(g_DeviceObject);
        g_DeviceObject = NULL;
        return status;
    }

    /* A failing object callback is not fatal: attach-based R/W still works. */
    VgbRegisterObjectCallback();

    return STATUS_SUCCESS;
}
