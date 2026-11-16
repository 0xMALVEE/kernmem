/*
 * nal.h - user-mode interface to the signed, vulnerable Intel NAL driver
 * (iqvw64e.sys).
 *
 * The NAL driver exposes one IOCTL (0x80862007) with a case number in the
 * request block. It runs in kernel mode with full privileges, so it can copy
 * kernel memory and call arbitrary kernel functions. That is what lets us map
 * our own unsigned driver under Secure Boot.
 */
#pragma once

#include <windows.h>
#include <cstdint>
#include <string>
#include <vector>

namespace nal
{
    extern HANDLE  hDevice;
    extern ULONG64 ntoskrnlBase;

    bool IsRunning();

    /* Persists the random service name and dropped path across invocations so a
     * later process can tear down what an earlier one loaded. */
    void SetStatePath(const std::wstring& path);
    bool SaveState();
    bool LoadState();

    /* The per-install device name. Empty until Load() or LoadState() resolved
     * one; callers fall back to the static namespace when it is empty. */
    const std::wstring& DeviceName();

    /* Path the host image was dropped to. This is the file name ntoskrnl records
     * in its unload trace, so the scrub needs it. */
    const std::wstring& DriverPath();

    /* Device path candidates for this install, DOS symlink first. The historical
     * static namespace is always appended so an existing mapping is never
     * orphaned by a name change. */
    std::vector<std::wstring> DevicePaths();

    /* Device type this install uses, and the matching IOCTL re-stamp. The
     * compile-time value is returned when no name is known. */
    unsigned long DeviceType();
    unsigned long RecodeIoctl(unsigned long code);

    /* Registers + starts iqvw64e.sys as a kernel service and opens the device. */
    bool Load(const wchar_t* driverPath);

    /* Opens an ALREADY running host and resolves the kernel base, without
     * registering a service. Needed by teardown: a fresh process has no handle, so
     * every kernel read in the unmap path would fail. */
    bool Attach();

    /* Detaches. With release == false the host is left resident, so ntoskrnl never
     * writes an MmUnloadedDrivers record for it. With release == true the service
     * is stopped, the key and the dropped image deleted and the state file removed. */
    bool Unload(bool release);

    bool MemCopy(uint64_t destination, uint64_t source, uint64_t size);
    bool SetMemory(uint64_t address, uint32_t value, uint64_t size);

    bool ReadMemory(uint64_t address, void* buffer, uint64_t size);
    bool WriteMemory(uint64_t address, void* buffer, uint64_t size);

    bool     GetPhysicalAddress(uint64_t address, uint64_t* outPhysical);
    uint64_t MapIoSpace(uint64_t physicalAddress, uint32_t size);
    bool     UnmapIoSpace(uint64_t address, uint32_t size);

    /* Writes through the physical mapping, so it also works on read-only pages
     * (used to patch NtAddAtom for the kernel-call trampoline). */
    bool WriteToReadOnlyMemory(uint64_t address, void* buffer, uint32_t size);

    uint64_t AllocatePool(uint64_t size);
    bool     FreePool(uint64_t address);

    /* Resolves the kernel image base, trying the module APIs then an IDT-anchored
     * scan. Requires the device to be open for the scan route. */
    uint64_t ResolveKernelBase();

    uint64_t GetKernelModuleBase(const char* moduleName);
    uint64_t GetKernelModuleExport(uint64_t moduleBase, const std::string& functionName);

    /* Raw kernel call: patches NtAddAtom with a jump to kernelFunction, invokes
     * NtAddAtom from user mode with the given arguments, then restores it. */
    bool CallKernelFunctionRaw(void* outResult, uint64_t kernelFunction, const uint64_t args[4], int argCount);

    template <typename... A>
    bool CallKernelFunction(void* outResult, uint64_t kernelFunction, A... args)
    {
        const uint64_t argv[4] = { (uint64_t)args..., 0, 0, 0 };
        return CallKernelFunctionRaw(outResult, kernelFunction, argv, (int)sizeof...(A));
    }
}
