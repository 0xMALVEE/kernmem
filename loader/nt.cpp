#include "nt.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>

#include <psapi.h>

#include "log.h"

namespace nt
{
    std::wstring GetFullTempPath()
    {
        wchar_t tempDirectory[MAX_PATH + 1] = { 0 };
        const uint32_t length = GetTempPathW(MAX_PATH + 1, tempDirectory);

        if (!length || length > MAX_PATH + 1)
            return L"";

        if (tempDirectory[length - 1] == L'\\')
            tempDirectory[length - 1] = 0;

        return std::wstring(tempDirectory);
    }

    std::wstring GetCurrentAppFolder()
    {
        wchar_t buffer[MAX_PATH + 1] = { 0 };
        GetModuleFileNameW(nullptr, buffer, MAX_PATH + 1);

        std::wstring path(buffer);
        const std::wstring::size_type pos = path.find_last_of(L"\\/");

        return pos == std::wstring::npos ? path : path.substr(0, pos);
    }

    bool ReadFileToMemory(const std::wstring& filePath, std::vector<BYTE>* outBuffer)
    {
        std::ifstream file(filePath, std::ios::binary);
        if (!file)
            return false;

        outBuffer->assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
        return true;
    }

    bool CreateFileFromMemory(const std::wstring& filePath, const char* address, size_t size)
    {
        std::ofstream file(filePath.c_str(), std::ios_base::out | std::ios_base::binary);
        if (!file.write(address, (std::streamsize)size))
            return false;

        file.close();
        return true;
    }

    namespace
    {
        /*
         * Pulls the module list into a heap buffer; free with VirtualFree.
         *
         * The buffer is allocated up front and grown on ANY failure, not only on
         * STATUS_INFO_LENGTH_MISMATCH. Starting from a null buffer and a zero
         * length makes some builds answer STATUS_INVALID_PARAMETER instead, and a
         * loop that only retries on LENGTH_MISMATCH then gives up on the very
         * first call - which is exactly how this reported "unreadable" on a
         * perfectly healthy machine.
         */
        PRTL_PROCESS_MODULES QueryModules()
        {
            ULONG bufferSize = 0x10000;
            NTSTATUS status = STATUS_INFO_LENGTH_MISMATCH;

            for (int attempt = 0; attempt < 8; ++attempt)
            {
                auto buffer = static_cast<PRTL_PROCESS_MODULES>(
                    VirtualAlloc(nullptr, bufferSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));

                if (!buffer)
                    return nullptr;

                ULONG needed = bufferSize;

                status = NtQuerySystemInformation(
                    (SYSTEM_INFORMATION_CLASS)SystemModuleInformation, buffer, bufferSize, &needed);

                if (NT_SUCCESS(status))
                    return buffer;

                VirtualFree(buffer, 0, MEM_RELEASE);

                if (needed > bufferSize)
                    bufferSize = needed + 0x1000;
                else
                    bufferSize *= 2;
            }

            km_log::Error("SystemModuleInformation failed (0x%08X)", (unsigned)status);
            return nullptr;
        }

        /*
         * Primary route: EnumDeviceDrivers returns real loaded driver bases.
         *
         * It is primary because SystemModuleInformation is unusable on the target
         * machine - it lists every module with the right name but a zeroed
         * ImageBase, so matching succeeds and yields 0x0.
         *
         * The whole list is scanned, not the first few entries: ordering is not
         * guaranteed across builds, and an arbitrary cap silently missed the
         * kernel on a real run.
         */
        uint64_t KernelBaseViaPsapi()
        {
            LPVOID drivers[1024];
            DWORD needed = 0;

            if (!EnumDeviceDrivers(drivers, sizeof(drivers), &needed) || !needed)
            {
                km_log::Error("EnumDeviceDrivers failed (0x%08X, needed=%lu)",
                               GetLastError(), needed);
                return 0;
            }

            const size_t count = needed / sizeof(LPVOID);
            const size_t limit = count < _countof(drivers) ? count : _countof(drivers);

            km_log::Line("*", "psapi: count=%zu first=0x%llX", count,
                          (unsigned long long)reinterpret_cast<uint64_t>(drivers[0]));

            for (size_t i = 0; i < limit && i < 6; ++i)
            {
                wchar_t probe[MAX_PATH] = { 0 };

                if (GetDeviceDriverFileNameW(drivers[i], probe, MAX_PATH))
                    km_log::Line("*", "  p[%zu] 0x%llX %ls", i,
                                  (unsigned long long)reinterpret_cast<uint64_t>(drivers[i]),
                                  probe);
            }

            for (size_t i = 0; i < limit; ++i)
            {
                wchar_t path[MAX_PATH] = { 0 };

                if (!GetDeviceDriverFileNameW(drivers[i], path, MAX_PATH))
                    continue;

                const wchar_t* base = wcsrchr(path, L'\\');
                base = base ? base + 1 : path;

                if (_wcsicmp(base, L"ntoskrnl.exe") == 0 ||
                    _wcsicmp(base, L"ntkrnlmp.exe") == 0)
                    return reinterpret_cast<uint64_t>(drivers[i]);
            }

            /* No name match: entry 0 is the kernel on every build seen. The
             * plausibility test is inlined because this function sits above
             * PlausibleKernelBase in the file. */
            const uint64_t first = reinterpret_cast<uint64_t>(drivers[0]);

            if (first < 0xFFFF800000000000ULL || (first & 0xFFF) != 0)
                return 0;

            return first;
        }

        const char* ModuleNameOf(const RTL_PROCESS_MODULE_INFORMATION& module)
        {
            return reinterpret_cast<const char*>(module.FullPathName) + module.OffsetToFileName;
        }

        /* A kernel image base is page aligned and inside the kernel range. */
        bool PlausibleKernelBase(uint64_t base)
        {
            return base >= 0xFFFF800000000000ULL && (base & 0xFFF) == 0;
        }
    }

    uint64_t GetKernelModuleAddress(const std::string& moduleName)
    {
        /*
         * EnumDeviceDrivers, not SystemModuleInformation. QueryModules lists every
         * module under the right name but with a zeroed ImageBase on this machine -
         * which is why `ci.dll not resolved` printed on every load while the kernel
         * base, resolved this same way through psapi, always worked. Matching on the
         * name here was never the problem; the base was.
         */
        LPVOID drivers[1024];
        DWORD needed = 0;

        if (!EnumDeviceDrivers(drivers, sizeof(drivers), &needed) || !needed)
            return 0;

        const size_t count = needed / sizeof(LPVOID);
        const size_t limit = count < (sizeof(drivers) / sizeof(drivers[0]))
                                 ? count
                                 : (sizeof(drivers) / sizeof(drivers[0]));

        const std::wstring wanted(moduleName.begin(), moduleName.end());

        for (size_t i = 0; i < limit; ++i)
        {
            wchar_t path[MAX_PATH] = { 0 };

            if (!GetDeviceDriverFileNameW(drivers[i], path, MAX_PATH))
                continue;

            const wchar_t* base = wcsrchr(path, L'\\');
            base = base ? base + 1 : path;

            if (_wcsicmp(base, wanted.c_str()) == 0)
                return reinterpret_cast<uint64_t>(drivers[i]);
        }

        return 0;
    }

    uint64_t GetKernelBase()
    {
        /*
         * EnumDeviceDrivers is the PRIMARY source, not a fallback.
         *
         * Measured on the target machine: SystemModuleInformation returns all 215
         * modules with correct names but a zeroed ImageBase on every entry, so
         * matching succeeds and then yields 0x0. EnumDeviceDrivers returns real
         * addresses for the same list. The module query stays as a secondary route
         * because other builds are the other way round.
         */
        const uint64_t viaPsapi = KernelBaseViaPsapi();

        if (PlausibleKernelBase(viaPsapi))
        {
            km_log::Step("kernel base from EnumDeviceDrivers (0x%llX)",
                          (unsigned long long)viaPsapi);
            return viaPsapi;
        }

        /* ntkrnlmp.exe is the real x64 name; the rest are historical. */
        static const char* names[] =
        {
            "ntoskrnl.exe",
            "ntkrnlmp.exe",
            "ntkrnlpa.exe",
            "ntkrnlpamp.exe",
        };

        PRTL_PROCESS_MODULES modules = QueryModules();

        if (!modules)
        {
            km_log::Error("both kernel-base routes failed");
            return 0;
        }

        uint64_t result = 0;

        for (const char* name : names)
        {
            for (ULONG i = 0; i < modules->NumberOfModules; ++i)
            {
                if (_stricmp(ModuleNameOf(modules->Modules[i]), name) == 0)
                {
                    result = reinterpret_cast<uint64_t>(modules->Modules[i].ImageBase);
                    break;
                }
            }

            if (result)
                break;
        }

        /* The kernel is always module 0, so this is a sound last resort. */
        if (!result && modules->NumberOfModules > 0)
            result = reinterpret_cast<uint64_t>(modules->Modules[0].ImageBase);

        VirtualFree(modules, 0, MEM_RELEASE);

        if (!PlausibleKernelBase(result))
        {
            km_log::Error("module-list base 0x%llX failed the kernel-range test",
                           (unsigned long long)result);
            return 0;
        }

        km_log::Step("kernel base from SystemModuleInformation (0x%llX)",
                      (unsigned long long)result);
        return result;
    }

    BOOLEAN bDataCompare(const BYTE* pData, const BYTE* bMask, const char* szMask)
    {
        for (; *szMask; ++szMask, ++pData, ++bMask)
        {
            if (*szMask == 'x' && *pData != *bMask)
                return FALSE;
        }

        return (*szMask) == 0;
    }

    uintptr_t FindPattern(uintptr_t dwAddress, uintptr_t dwLen, BYTE* bMask, const char* szMask)
    {
        const size_t maskLength = strlen(szMask);
        if (dwLen < maskLength)
            return 0;

        const size_t maxLength = dwLen - maskLength;
        for (uintptr_t i = 0; i <= maxLength; ++i)
        {
            if (bDataCompare((BYTE*)(dwAddress + i), bMask, szMask))
                return dwAddress + i;
        }

        return 0;
    }

    PVOID FindSection(const char* sectionName, uintptr_t modulePtr, PULONG size)
    {
        const size_t nameLength = strlen(sectionName);
        PIMAGE_NT_HEADERS headers =
            (PIMAGE_NT_HEADERS)(modulePtr + ((PIMAGE_DOS_HEADER)modulePtr)->e_lfanew);
        PIMAGE_SECTION_HEADER sections = IMAGE_FIRST_SECTION(headers);

        for (WORD i = 0; i < headers->FileHeader.NumberOfSections; ++i)
        {
            PIMAGE_SECTION_HEADER section = &sections[i];

            if (memcmp(section->Name, sectionName, nameLength) == 0 &&
                nameLength == strlen((char*)section->Name))
            {
                if (!section->VirtualAddress)
                    return nullptr;
                if (size)
                    *size = section->Misc.VirtualSize;
                return (PVOID)(modulePtr + section->VirtualAddress);
            }
        }

        return nullptr;
    }
}