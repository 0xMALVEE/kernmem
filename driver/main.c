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
        peb = (KM_PEB*)PsGetProcessPeb(proc);
        if (!peb || !peb->Ldr)
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

                list->Entries[count].Base = (unsigned __int64)entry->DllBase;
                list->Entries[count].SizeOfImage = entry->SizeOfImage;
                list->Entries[count].Pad = 0;
                list->Entries[count].Name[0] = 0;

                if (entry->BaseDllName.Buffer && entry->BaseDllName.Length)
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
static BOOLEAN KmUnlinkModuleIndex(void* ldrRaw, KM_LDR_ENTRY* entry, unsigned __int64 base)
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

    if (!ldrRaw || !entry)
        return FALSE;

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
                /* Remembered bucket, so unhide can put the hash link back. */
                bucket = (LIST_ENTRY*)g_Hidden[i].HashBucket;
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
            for (cursor = head->Flink; cursor != head; cursor = cursor->Flink)
            {
                KM_LDR_ENTRY* entry = CONTAINING_RECORD(cursor, KM_LDR_ENTRY, InMemoryOrderLinks);
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
                     * resolve walks. A failure leaves the entry in the index. */
                    KmUnlinkModuleIndex(peb->Ldr, entry, base);
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
            for (i = 0; i < KM_MAX_HIDDEN; ++i)
            {
                if (!g_Hidden[i].Entry)
                {
                    g_Hidden[i].Pid = pid;
                    g_Hidden[i].DllBase = (void*)base;
                    g_Hidden[i].Entry = (void*)base;
                    g_Hidden[i].HashBucket = (void*)bucket;
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

    if (address <= (ULONG_PTR)MmHighestUserAddress)
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

    irp->IoStatus.Status = status;
    irp->IoStatus.Information = written;
    IoCompleteRequest(irp, IO_NO_INCREMENT);
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

static VOID KmUnload(PDRIVER_OBJECT driver)
{
    InterlockedExchange(&g_Unloading, 1);

    if (g_ObRegistered && g_ObRegistration)
    {
        ObUnRegisterCallbacks(g_ObRegistration);
        g_ObRegistration = NULL;
        g_ObRegistered = FALSE;
    }

    if (g_Symlink.Buffer)
        IoDeleteSymbolicLink(&g_Symlink);

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
    NTSTATUS status;
    ULONG i;

    for (i = 0; i < KM_MAX_HIDDEN; ++i)
        RtlZeroMemory(&g_Hidden[i], sizeof(g_Hidden[i]));
    KeInitializeSpinLock(&g_HiddenLock);

    driver->DriverUnload = KmUnload;

    /* Every slot gets a real handler first; see KmDefaultDispatch. */
    for (i = 0; i <= IRP_MJ_MAXIMUM_FUNCTION; ++i)
        driver->MajorFunction[i] = KmDefaultDispatch;

    driver->MajorFunction[IRP_MJ_CREATE] = KmCreateClose;
    driver->MajorFunction[IRP_MJ_CLEANUP] = KmCreateClose;
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
                if (params->ClientPid)
                    g_ClientPid = params->ClientPid;

                /* Per-install device name, chosen by the loader. */
                if (params->DeviceName[0])
                    RtlStringCchCopyNW(g_DeviceName, RTL_NUMBER_OF(g_DeviceName),
                                       params->DeviceName, KM_MAX_DEVICE_CHARS - 1);

                g_AltitudeSeed = params->AltitudeSeed;

                if (params->PoolTag)
                    g_PoolTag = params->PoolTag;

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

    /*
     * Resolve the identity. An empty name means a service load or an old loader,
     * so the historical static namespace stays as the fallback.
     */
    if (!g_DeviceName[0])
        RtlStringCchCopyNW(g_DeviceName, RTL_NUMBER_OF(g_DeviceName),
                           L"Kernmem", KM_MAX_DEVICE_CHARS - 1);

    /* Derive the device type from the name: one less constant to signature on. */
    g_DeviceType = KmDeviceTypeFor(g_DeviceName);

    /* A real driver always carries a DriverName. An unnamed driver object that
     * owns a named device is one of the cheapest mismatches to spot. */
    /* Copy + cat rather than RtlStringCchPrintfW: the printf family pulls in
     * __stdio_common_vswprintf from the UCRT, and this image links only
     * ntoskrnl/BufferOverflowK/wdm. */
    RtlStringCchCopyW(g_DriverName, RTL_NUMBER_OF(g_DriverName), L"\\Driver\\");
    RtlStringCchCatW(g_DriverName, RTL_NUMBER_OF(g_DriverName), g_DeviceName);
    RtlInitUnicodeString(&driver->DriverName, g_DriverName);

    RtlStringCchCopyW(g_DevicePath, RTL_NUMBER_OF(g_DevicePath), L"\\Device\\");
    RtlStringCchCatW(g_DevicePath, RTL_NUMBER_OF(g_DevicePath), g_DeviceName);
    RtlInitUnicodeString(&deviceName, g_DevicePath);

    RtlStringCchCopyW(g_SymlinkName, RTL_NUMBER_OF(g_SymlinkName), L"\\DosDevices\\");
    RtlStringCchCatW(g_SymlinkName, RTL_NUMBER_OF(g_SymlinkName), g_DeviceName);
    RtlInitUnicodeString(&g_Symlink, g_SymlinkName);

    status = IoCreateDevice(driver, 0, &deviceName, g_DeviceType, FILE_DEVICE_SECURE_OPEN, FALSE, &g_DeviceObject);
    if (!NT_SUCCESS(status))
        return status;

    g_DeviceObject->Flags |= DO_BUFFERED_IO;
    /* Cleared automatically only for devices created inside a real DriverEntry.
     * Under IoCreateDriver it is not, and an initialising device cannot be
     * opened. */
    g_DeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;

    status = IoCreateSymbolicLink(&g_Symlink, &deviceName);
    if (!NT_SUCCESS(status))
    {
        IoDeleteDevice(g_DeviceObject);
        g_DeviceObject = NULL;
        return status;
    }

    /* A failing object callback is not fatal: attach-based R/W still works. */
    KmRegisterObjectCallback();

    return STATUS_SUCCESS;
}
