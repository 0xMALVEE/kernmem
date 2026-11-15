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

    /* Registers + starts iqvw64e.sys as a kernel service and opens the device. */
    bool Load(const wchar_t* driverPath);

    /* Stops the service, deletes the service key and the dropped driver file. */
    bool Unload();

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
