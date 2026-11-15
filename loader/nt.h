/*
 * nt.h - thin native API layer (ntdll exports, system information, file and
 * path helpers, pattern scanning).
 */
#pragma once

#include <windows.h>
#include <winternl.h>
#include <cstdint>
#include <string>
#include <vector>

#pragma comment(lib, "ntdll.lib")

#pragma warning(push)
#pragma warning(disable : 4005)
#include <ntstatus.h>
#pragma warning(pop)

#ifndef NT_SUCCESS
#define NT_SUCCESS(Status) (((NTSTATUS)(Status)) >= 0)
#endif

#ifndef STATUS_ALREADY_REGISTERED
#define STATUS_ALREADY_REGISTERED ((NTSTATUS)0xC0000718L)
#endif

namespace nt
{
    constexpr auto SystemModuleInformation = 11;
    constexpr auto SystemExtendedHandleInformation = 64;

    extern "C" NTSTATUS NtLoadDriver(PUNICODE_STRING DriverServiceName);
    extern "C" NTSTATUS NtUnloadDriver(PUNICODE_STRING DriverServiceName);
    extern "C" NTSTATUS RtlAdjustPrivilege(ULONG Privilege, BOOLEAN Enable, BOOLEAN Client, BOOLEAN* WasEnabled);
    extern "C" NTSTATUS NtQuerySystemInformation(
        SYSTEM_INFORMATION_CLASS SystemInformationClass,
        PVOID SystemInformation,
        ULONG SystemInformationLength,
        PULONG ReturnLength);

    typedef struct _SYSTEM_HANDLE
    {
        PVOID Object;
        HANDLE UniqueProcessId;
        HANDLE HandleValue;
        ULONG GrantedAccess;
        USHORT CreatorBackTraceIndex;
        USHORT ObjectTypeIndex;
        ULONG HandleAttributes;
        ULONG Reserved;
    } SYSTEM_HANDLE, *PSYSTEM_HANDLE;

    typedef struct _SYSTEM_HANDLE_INFORMATION_EX
    {
        ULONG_PTR HandleCount;
        ULONG_PTR Reserved;
        SYSTEM_HANDLE Handles[1];
    } SYSTEM_HANDLE_INFORMATION_EX, *PSYSTEM_HANDLE_INFORMATION_EX;

    /* Values are ABI-stable pool type constants. */
    typedef enum class _POOL_TYPE : uint32_t
    {
        NonPagedPool,
        NonPagedPoolExecute = NonPagedPool,
        PagedPool,
        NonPagedPoolMustSucceed = NonPagedPool + 2,
        DontUseThisType,
        NonPagedPoolCacheAligned = NonPagedPool + 4,
        PagedPoolCacheAligned,
        NonPagedPoolCacheAlignedMustS = NonPagedPool + 6,
        MaxPoolType,
        NonPagedPoolBase = 0,
        NonPagedPoolBaseMustSucceed = NonPagedPoolBase + 2,
        NonPagedPoolBaseCacheAligned = NonPagedPoolBase + 4,
        NonPagedPoolBaseCacheAlignedMustS = NonPagedPoolBase + 6,
        NonPagedPoolSession = 32,
        PagedPoolSession = NonPagedPoolSession + 1,
        NonPagedPoolMustSucceedSession = PagedPoolSession + 1,
        DontUseThisTypeSession = NonPagedPoolMustSucceedSession + 1,
        NonPagedPoolCacheAlignedSession = DontUseThisTypeSession + 1,
        PagedPoolCacheAlignedSession = NonPagedPoolCacheAlignedSession + 1,
        NonPagedPoolCacheAlignedMustSSession = PagedPoolCacheAlignedSession + 1,
        NonPagedPoolNx = 512,
        NonPagedPoolNxCacheAligned = NonPagedPoolNx + 4,
        NonPagedPoolSessionNx = NonPagedPoolNx + 32,
    } POOL_TYPE;

    typedef struct _RTL_PROCESS_MODULE_INFORMATION
    {
        HANDLE Section;
        PVOID MappedBase;
        PVOID ImageBase;
        ULONG ImageSize;
        ULONG Flags;
        USHORT LoadOrderIndex;
        USHORT InitOrderIndex;
        USHORT LoadCount;
        USHORT OffsetToFileName;
        UCHAR FullPathName[256];
    } RTL_PROCESS_MODULE_INFORMATION, *PRTL_PROCESS_MODULE_INFORMATION;

    typedef struct _RTL_PROCESS_MODULES
    {
        ULONG NumberOfModules;
        RTL_PROCESS_MODULE_INFORMATION Modules[1];
    } RTL_PROCESS_MODULES, *PRTL_PROCESS_MODULES;

    /* --- helpers -------------------------------------------------------- */

    std::wstring GetFullTempPath();
    std::wstring GetCurrentAppFolder();

    bool ReadFileToMemory(const std::wstring& filePath, std::vector<BYTE>* outBuffer);
    bool CreateFileFromMemory(const std::wstring& filePath, const char* address, size_t size);

    /* Base address of a loaded kernel module, 0 when absent. */
    uint64_t GetKernelModuleAddress(const std::string& moduleName);

    /* Base address of the kernel image itself.
     *
     * An exact-name lookup is not enough: modern Windows loads the kernel as
     * ntkrnlmp.exe, so "ntoskrnl.exe" misses and callers get 0. Known names are
     * tried in order, then module 0 is used (the kernel is always first in
     * SystemModuleInformation). Returns 0 only if nothing plausible is found. */
    uint64_t GetKernelBase();

    BOOLEAN bDataCompare(const BYTE* pData, const BYTE* bMask, const char* szMask);
    uintptr_t FindPattern(uintptr_t dwAddress, uintptr_t dwLen, BYTE* bMask, const char* szMask);
    PVOID FindSection(const char* sectionName, uintptr_t modulePtr, PULONG size);
}