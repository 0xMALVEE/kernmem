/*
 * vgb_ntcompat.h
 *
 * The WDK 10.0.26100 kernel headers installed on this machine do not declare a
 * handful of ntoskrnl exports this driver needs (several of them only exist in
 * the user-mode header tree, which cannot be mixed into a kernel build).
 *
 * Declare them here with the documented signatures. Every one of them is a
 * genuine ntoskrnl export, so they resolve against ntoskrnl.lib.
 */
#pragma once

/* winnt.h process access rights (UM-only header). Values are ABI-stable. */
#ifndef PROCESS_CREATE_THREAD
#define PROCESS_CREATE_THREAD        0x0002
#endif
#ifndef PROCESS_VM_OPERATION
#define PROCESS_VM_OPERATION         0x0008
#endif
#ifndef PROCESS_VM_READ
#define PROCESS_VM_READ              0x0010
#endif
#ifndef PROCESS_VM_WRITE
#define PROCESS_VM_WRITE             0x0020
#endif
#ifndef PROCESS_QUERY_INFORMATION
#define PROCESS_QUERY_INFORMATION    0x0400
#endif
#ifndef PROCESS_SUSPEND_RESUME
#define PROCESS_SUSPEND_RESUME       0x0800
#endif

/*
 * Normally supplied by wdm.h next to OB_CALLBACK_REGISTRATION.
 *
 * This is a VERSION field, not a flag: zero is invalid, so ObRegisterCallbacks
 * rejects the registration with STATUS_INVALID_PARAMETER and the callback never
 * installs. The documented value is 1.
 */
#ifndef OB_FLUSH_ALL_PREPARE_CALLBACKS
#define OB_FLUSH_ALL_PREPARE_CALLBACKS 1
#endif

NTKERNELAPI PVOID NTAPI PsGetProcessPeb(_In_ PEPROCESS Process);

NTKERNELAPI PVOID NTAPI PsGetProcessSectionBaseAddress(_In_ PEPROCESS Process);

NTKERNELAPI PEPROCESS NTAPI PsGetNextProcess(_In_opt_ PEPROCESS Process);

NTKERNELAPI PUCHAR NTAPI PsGetProcessImageFileName(_In_ PEPROCESS Process);

/*
 * Minimal x64 PEB layout. The WDK only forward-declares _PEB, so the two
 * fields this driver touches are laid out explicitly. Offsets are fixed for
 * the x64 ABI: Ldr sits at 0x18, and the three module list heads start at
 * 0x10 / 0x20 / 0x30 inside _PEB_LDR_DATA.
 */
typedef struct _VGB_PEB_LDR_DATA
{
    ULONG Length;
    BOOLEAN Initialized;
    PVOID SsHandle;
    LIST_ENTRY InLoadOrderModuleList;
    LIST_ENTRY InMemoryOrderModuleList;
    LIST_ENTRY InInitializationOrderModuleList;
} VGB_PEB_LDR_DATA;

typedef struct _VGB_PEB
{
    UCHAR Reserved[0x18];
    VGB_PEB_LDR_DATA* Ldr;
} VGB_PEB;

/* Compile-time proof that the hand-rolled layout matches the x64 ABI. */
C_ASSERT(FIELD_OFFSET(VGB_PEB, Ldr) == 0x18);
C_ASSERT(FIELD_OFFSET(VGB_PEB_LDR_DATA, InMemoryOrderModuleList) == 0x20);
C_ASSERT(FIELD_OFFSET(VGB_PEB_LDR_DATA, InInitializationOrderModuleList) == 0x30);

/* PsGetNextProcess is exported by ntoskrnl but absent from this SDK's import
 * library, so it is resolved at runtime; this is its documented signature. */
typedef PEPROCESS (NTAPI* VGB_PFN_GET_NEXT_PROCESS)(_In_opt_ PEPROCESS Process);

/* Fallback process enumeration via the native system information query. */
typedef struct _VGB_SYSTEM_PROCESS_INFORMATION
{
    ULONG NextEntryOffset;
    ULONG NumberOfThreads;
    LARGE_INTEGER WorkingSetPrivateSize;
    ULONG HardFaultCount;
    ULONG NumberOfThreadsHighWatermark;
    ULONGLONG CycleTime;
    LARGE_INTEGER CreateTime;
    LARGE_INTEGER UserTime;
    LARGE_INTEGER KernelTime;
    UNICODE_STRING ImageName;
    LONG BasePriority;
    HANDLE UniqueProcessId;
} VGB_SYSTEM_PROCESS_INFORMATION;

#ifndef SystemProcessInformation
#define SystemProcessInformation 5
#endif

NTKERNELAPI NTSTATUS NTAPI ZwQuerySystemInformation(
    _In_ ULONG SystemInformationClass,
    _Out_writes_bytes_opt_(SystemInformationLength) PVOID SystemInformation,
    _In_ ULONG SystemInformationLength,
    _Out_opt_ PULONG ReturnLength);

NTKERNELAPI NTSTATUS NTAPI MmCopyVirtualMemory(
    _In_ PEPROCESS SourceProcess,
    _In_ PVOID SourceAddress,
    _In_ PEPROCESS TargetProcess,
    _In_ PVOID TargetAddress,
    _In_ SIZE_T BufferSize,
    _In_ KPROCESSOR_MODE PreviousMode,
    _Out_ PSIZE_T ReturnSize);
