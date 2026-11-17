#include "nal.h"

#include "log.h"
#include "nt.h"
#include "service.h"

#include "res/iqvw_sys.h"
#include "res/km_sys.h"
#include "km_shared_loader.h"

#include <cstdlib>
#include <cstring>
#include <ctime>

#include <intrin.h>
#include <fstream>
#include <iterator>
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

    /*
     * Shared block. Allocated here, locked and serviced by the driver. While it is
     * up, kernel reads cost no IOCTL - and so no syscall stays patched while the
     * game runs.
     */
    KM_SHARED*      g_shared = nullptr;
    volatile LONG   g_sharedSlot = 0;

    /* Set once the dropped host image is off disk, so the delete is idempotent and
     * a failed removal can still be retried by a later call. */
    bool            g_dropUnlinked = false;

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

    /*
     * File name for the dropped host. Kept separate from the service name: the two
     * being the same string is a free correlation for anything that enumerates the
     * directory and the registry together. Lowercase alphanumeric, so it reads like
     * an ordinary vendor driver instead of a build artifact.
     */
    std::wstring RandomDropName()
    {
        static const char alphanum[] = "abcdefghijklmnopqrstuvwxyz0123456789";
        char buffer[16] = { 0 };

        srand((unsigned)time(nullptr) * GetCurrentThreadId() ^ GetTickCount());
        const int len = rand() % 5 + 10;

        for (int i = 0; i < len; ++i)
            buffer[i] = alphanum[rand() % (sizeof(alphanum) - 1)];

        return std::wstring(buffer, buffer + len);
    }

}

namespace nal
{
    /* Defined further down, next to HideTrail, where the string helpers are in
     * scope. Declared here because Load() reaches them and their definitions sit
     * below it. */
    void SweepOrphans(bool hostResident);
    void UnlinkDrop();

    bool CreateShared()
    {
        if (g_shared)
            return true;

        void* block = VirtualAlloc(nullptr, sizeof(KM_SHARED), MEM_COMMIT | MEM_RESERVE,
                                   PAGE_READWRITE);

        if (!block)
        {
            km_log::Warn("shared block allocation failed - reads stay on IOCTL");
            return false;
        }

        ZeroMemory(block, sizeof(KM_SHARED));
        g_shared = reinterpret_cast<KM_SHARED*>(block);

        return true;
    }

    uint64_t SharedUser()
    {
        return reinterpret_cast<uint64_t>(g_shared);
    }

    uint32_t SharedSize()
    {
        return g_shared ? (uint32_t)sizeof(KM_SHARED) : 0;
    }

    /*
     * Makes the driver drop every shared block this process owns.
     *
     * The block handed over at map time is the driver's kernel block, and there is
     * no IOCTL that releases it - only IRP_MJ_CLEANUP does, and that is sent when
     * the last handle to a file object is closed. Each CreateFileW makes a new
     * file object, so a throwaway open/close is enough.
     *
     * Without this the driver keeps sizeof(KM_SHARED) pages locked into this
     * process after the VA is freed, and reaping the process bugchecks
     * 0x76 PROCESS_HAS_LOCKED_PAGES. Arg3 0x21 is exactly those 33 pages.
     */
    bool ReleaseDriverBlock()
    {
        /*
         * Magic is only written by the driver at registration, so it tells us
         * whether any pages are actually locked into this process. Verbs that
         * never mapped (attach, status, a failed load before the handoff) leave it
         * zero, and there is nothing to wait for - freeing the VA is safe.
         */
        if (!g_shared || g_shared->Magic != KM_SHARED_MAGIC)
            return true;

        std::vector<std::wstring> paths = DevicePaths();

        /*
         * The cleanup handler runs from the close, but not synchronously with it,
         * so the block is polled until the driver has actually marked it dropped.
         * Ready lives in the mapped block itself, which stays readable until the
         * VA is freed.
         */
        for (int attempt = 0; attempt < 25; ++attempt)
        {
            for (size_t i = 0; i < paths.size(); ++i)
            {
                HANDLE probe = CreateFileW(paths[i].c_str(), FILE_ANY_ACCESS,
                                           FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);

                if (probe == INVALID_HANDLE_VALUE)
                    continue;

                CloseHandle(probe);

                if (g_shared && g_shared->Ready == 0)
                    return true;

                break;
            }

            Sleep(10);
        }

        return false;
    }

    void DestroyShared()
    {
        if (!g_shared)
            return;

        /*
         * The pages have to be released before the VA goes away, never after. If
         * the driver cannot be reached the VA is leaked on purpose - freeing it
         * while the MDL is still locked is exactly the 0x76 (33 pages = this
         * block) when the process then exits.
         *
         * IRP_MJ_CLEANUP only arrives when the LAST handle to the device closes,
         * so our own control handle has to go first. Leaving it open meant the
         * throwaway probe below never produced a cleanup at all, and the block
         * was released by the kernel during process teardown instead - racing
         * the very address space the pages are locked into.
         */
        if (hDevice != INVALID_HANDLE_VALUE && hDevice)
        {
            CloseHandle(hDevice);
            hDevice = INVALID_HANDLE_VALUE;
        }

        if (!ReleaseDriverBlock())
        {
            km_log::Warn("driver did not release the shared block - VA kept allocated");
            return;
        }

        VirtualFree(g_shared, 0, MEM_RELEASE);
        g_shared = nullptr;
    }
}

namespace
{
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

        /*
         * Do NOT require a kernel-range base here. In user mode SIDT returns the
         * KUSER_SHARED_DATA shadow (~0xFFFFF78000000000), so a kernel-range test
         * rejects the one route that does not depend on the module list before it
         * does anything. Only the handler address has to be a kernel address.
         */
        km_log::Line("*", "idt limit 0x%X base 0x%llX", idtr.limit,
                     (unsigned long long)idtr.base);

        if (!idtr.base)
            return 0;

        /*
         * In user mode SIDT hands back the KUSER_SHARED_DATA shadow of the IDT,
         * not the real table, so idtr.base sits nowhere near ntoskrnl and scanning
         * outwards from it finds nothing. The shadow still carries the real gate
         * descriptors though, and their handler addresses point straight into
         * ntoskrnl - that is the anchor the scan needs.
         */
        uint8_t gate[16] = { 0 };

        if (!ReadMemory(idtr.base, gate, sizeof(gate)))
        {
            km_log::Warn("IDT shadow unreadable at 0x%llX", (unsigned long long)idtr.base);
            return 0;
        }

        const uint64_t handler = (uint64_t)gate[0] | ((uint64_t)gate[1] << 8) |
                                 ((uint64_t)gate[6] << 16) | ((uint64_t)gate[7] << 24) |
                                 ((uint64_t)gate[8] << 32) | ((uint64_t)gate[9] << 40) |
                                 ((uint64_t)gate[10] << 48) | ((uint64_t)gate[11] << 56);

        if (handler < 0xFFFF800000000000ULL)
            return 0;

        km_log::Line("*", "idt handler 0x%llX", handler);

        const uint64_t anchor = handler & ~0xFFFULL;
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

    void SetDeviceName(const std::wstring& name)
    {
        if (!name.empty())
            g_deviceName = name;
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
        /*
         * The service name is allowed to be empty. An adopted host was started by
         * an earlier session, so this one owns no service - but its device name
         * still has to reach the file, because that is what the client opens. The
         * old guard returned here and the name was silently dropped.
         */
        if (g_statePath.empty())
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

    /* Writes the current state out. Public entry point for callers that change
     * the device name after a mapping and need it on disk. */
    bool PersistState()
    {
        return SaveState();
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

        /* Empty only when the host was adopted - the name still has to be read. */
        if (service.empty() && device.empty())
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

        /*
         * A missing kernel base is NOT fatal here. Unmapping needs only the
         * driver object, an explicit DriverUnload address and raw memory reads -
         * none of which go through export resolution. Mapping does, and refuses
         * on its own. Treating it as fatal meant a corrupt PsLoadedModuleList
         * blocked the one operation that could repair it, so recovery needed a
         * reboot.
         */
        if (!EnsureKernelBase())
            km_log::Warn("kernel base unresolved - unload still works, mapping will not");

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

                /*
                 * A crash that leaves \Device\Nal held lands here, not in the fresh
                 * branch - so the sweep has to run on this path too, or the file the
                 * crash left behind is the one that survives. The host is resident,
                 * so nothing is stopped; its key and its file still go.
                 */
                SweepOrphans(true);

                /*
                 * The recorded name belongs to the dead image, and its device
                 * object is still in the namespace holding it. Reusing it made
                 * IoCreateDevice collide. A fresh name is mapped against instead;
                 * the driver reports the final one back if even that is taken.
                 */
                g_deviceName = RandomDeviceName();
                SaveState();

                /* An adopted host has no shared block from this session. */
                CreateShared();
                return true;
            }

            km_log::Warn("attach failed - another NAL user holds it, reboot if it persists");
            return false;
        }

        /*
         * Clear out drops left by sessions that died before they could clean up. A
         * bugcheck never reaches any teardown, so without this %TEMP% accumulates
         * copies of iqvw64e - and a scanner that hashes the directory matches the
         * host by whatever name it finds there.
         */
        SweepOrphans(false);

        g_serviceName = RandomServiceName();
        g_deviceName = RandomDeviceName();

        std::wstring temp = nt::GetFullTempPath();
        if (temp.empty())
        {
            km_log::Error("cannot resolve %%TEMP%%");
            return false;
        }

        g_driverPath = temp + L"\\" + RandomDropName() + L".sys";

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

        /*
         * The device answers, so the image section now holds its own reference and
         * the file is dead weight. Gone here rather than after the map: everything
         * below this line can bugcheck, and a bugcheck runs no cleanup at all.
         */
        UnlinkDrop();

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

        /* Allocated before the map so the init block can hand its address over. */
        CreateShared();

        SaveState();
        return true;
    }

    bool Unload(bool release)
    {
        LoadState();

        /*
         * The shared block goes first, while the device is still reachable.
         *
         * DestroyShared() has to make the driver release the block, and once the
         * host is torn down there is nothing left to ask. This ordering is what
         * made the old teardown unsafe: the pages were freed with the MDL still
         * locked, and the process then exited.
         */
        DestroyShared();

        if (hDevice != INVALID_HANDLE_VALUE && hDevice)
        {
            CloseHandle(hDevice);
            hDevice = INVALID_HANDLE_VALUE;
        }

        /*
         * Default is detach and leave resident.
         *
         * Stopping the service makes ntoskrnl write an MmUnloadedDrivers record and
         * a PiDDBCache entry for iqvw64e, and both are written BY the unload - so no
         * scrub in the same session can clear them. Leaving the host up means the
         * record is never written at all. The cost is the service key and the dropped
         * image on disk, which is the quieter trade: a file is not a kernel record.
         *
         * release == true does the full teardown, for when the disk trail matters
         * more than the kernel one.
         */
        if (!release)
        {
            if (g_ownedService || !g_serviceName.empty())
                km_log::Info("mapping host left resident - no unload record written");

            ntoskrnlBase = 0;
            return true;
        }

        /*
         * Only release a host this session started. When \Device\Nal was already
         * resident we attached to a service somebody else registered - ours from
         * an earlier run, or another tool's - and stopping it would pull the
         * device out from under whatever still holds it. The dropped image is
         * kept too, because the registry ImagePath still points at it.
         */
        /*
         * An adopted host is normally left running. On an explicit release it is
         * torn down anyway - after a default unload the host is always resident, so
         * the next session adopts it, and without this the guard would bail on every
         * release and nothing would ever be cleaned up.
         */
        if (!g_ownedService && !release)
        {
            if (!g_serviceName.empty())
                km_log::Info("mapping host left running - not started by this session");

            ntoskrnlBase = 0;
            return true;
        }

        if (!g_serviceName.empty())
        {
            /*
             * HideTrail deleted the key, so it has to exist again before
             * NtUnloadDriver can name the service by it. Rebuilt from the same
             * values, so it is identical to the one that was removed.
             */
            if (g_ownedService && !g_driverPath.empty())
                service::WriteKey(g_driverPath, g_serviceName);

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

        /*
         * The state file names the service, the dropped path and the device name
         * together, which is a far stronger clue than any one of them alone.
         */
        if (!g_statePath.empty())
        {
            _wremove(g_statePath.c_str());
            g_statePath.clear();
        }

        ntoskrnlBase = 0;
        return true;
    }

    std::wstring RandomImageName()
    {
        return RandomDropName() + L".sys";
    }

    /*
     * Removes host images left behind by sessions that died mid-load, and the
     * service keys that named them.
     *
     * Matched on exact bytes, never on the name, so an unrelated .sys in the
     * directory is safe. The live host is skipped by name. A stale key is a second
     * trail on its own - it names the file even after the file is gone - so the key
     * goes with it.
     */
    void SweepOrphans(bool hostResident)
    {
        const std::wstring temp = nt::GetFullTempPath();
        if (temp.empty())
            return;

        /*
         * Nothing is kept by name: every candidate is matched on its bytes instead,
         * so an unrelated .sys in the directory is never touched. When the host is
         * still resident the caller says so and the service is left loaded.
         */
        WIN32_FIND_DATAW found = { 0 };
        HANDLE search = FindFirstFileW((temp + L"*.sys").c_str(), &found);
        if (search == INVALID_HANDLE_VALUE)
            return;

        int removed = 0;

        do
        {
            if (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                continue;

            /*
             * Matched on exact bytes of either image we drop - the host and the
             * mapped driver - so the name can be anything and an unrelated .sys in
             * the directory is never touched. Size first, as a cheap gate.
             */
            bool match = false;

            if (found.nFileSizeLow == iqvw64e_sys_size || found.nFileSizeLow == km_sys_size)
            {
                const std::wstring path = temp + found.cFileName;
                std::ifstream in(path, std::ios::binary);

                if (in)
                {
                    std::string bytes((std::istreambuf_iterator<char>(in)),
                                      std::istreambuf_iterator<char>());

                    match = (bytes.size() == iqvw64e_sys_size &&
                             bytes.compare(0, bytes.size(),
                                           reinterpret_cast<const char*>(iqvw64e_sys),
                                           iqvw64e_sys_size) == 0) ||
                            (bytes.size() == km_sys_size &&
                             bytes.compare(0, bytes.size(),
                                           reinterpret_cast<const char*>(km_sys),
                                           km_sys_size) == 0);
                }
            }

            if (!match)
                continue;

            const std::wstring path = temp + found.cFileName;

            /* Key first: it names the file, so it is the stronger of the two trails
             * and the one to clear even if the file itself is somehow in use. */
            service::RemoveKeyForImage(path, !hostResident);

            if (_wremove(path.c_str()) != 0)
                continue;

            ++removed;
        }
        while (FindNextFileW(search, &found));

        FindClose(search);

        /*
         * The state records go too. They hold the service name, the %TEMP% path and
         * the device name on three plain lines - together, the strongest clue any of
         * this leaves. Matched on age against this process's own start time, so the
         * record this session writes later survives.
         */
        FILETIME created = { 0 };
        FILETIME exited = { 0 };
        FILETIME kernel = { 0 };
        FILETIME user = { 0 };

        GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user);

        WIN32_FIND_DATAW state = { 0 };
        HANDLE states = FindFirstFileW((temp + L"km-*.state").c_str(), &state);

        if (states != INVALID_HANDLE_VALUE)
        {
            do
            {
                if (CompareFileTime(&state.ftLastWriteTime, &created) >= 0)
                    continue;

                if (_wremove((temp + state.cFileName).c_str()) == 0)
                    ++removed;
            }
            while (FindNextFileW(states, &state));

            FindClose(states);
        }

        if (removed)
            km_log::Step("swept %d stale trail(s) out of %%TEMP%%", removed);
    }

    /* Deletes the dropped host image. The image section holds its own reference, so
     * the driver keeps running and unloading still works - what goes away is the
     * file a scanner can hash, or read a version resource out of. Idempotent, and
     * the path string is kept: the trace scrub and the release path still want it. */
    void UnlinkDrop()
    {
        if (g_dropUnlinked || g_driverPath.empty())
            return;

        if (GetFileAttributesW(g_driverPath.c_str()) == INVALID_FILE_ATTRIBUTES)
        {
            g_dropUnlinked = true;
            return;
        }

        if (_wremove(g_driverPath.c_str()) == 0)
        {
            g_dropUnlinked = true;
            km_log::Step("dropped host image removed - nothing left on disk to hash");
        }
        else
        {
            km_log::Warn("could not remove %ls - the file stays on disk", g_driverPath.c_str());
        }
    }

    void HideTrail()
    {
        UnlinkDrop();

        /*
         * The service key is an independent trail: it names the random driver and
         * its %TEMP% ImagePath, and it outlives the session unless removed here.
         * NtUnloadDriver needs a key to name the service by, so teardown writes it
         * back from the same values instead of leaving it for the whole session.
         *
         * Owned service only - an adopted key belongs to whatever still holds the
         * device. Removed even when the path is already gone, which is the normal
         * case now that the image goes as soon as the device answers.
         */
        if (g_ownedService && !g_serviceName.empty() &&
            NT_SUCCESS(service::RemoveKey(g_serviceName)))
            km_log::Step("service key removed - the registry holds nothing while the session runs");
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

        /*
         * Shared block first. While it is up a read is a memcpy and a short spin,
         * with no IOCTL and so no syscall patch live. Only ranges that fit the
         * block take this path; anything larger stays on the IOCTL below.
         */
        if (g_shared && g_shared->Ready == 1 && g_shared->Magic == KM_SHARED_MAGIC &&
            size <= KM_SHARED_DATA)
        {
            const uint32_t slot =
                (uint32_t)(InterlockedIncrement(&g_sharedSlot) & (KM_SHARED_SLOTS - 1));
            KM_SHARED_REQ* request = &g_shared->Reqs[slot];

            request->Pid = 0;               /* 0 = kernel address */
            request->Address = address;
            request->Size = (uint32_t)size;
            request->Offset = 0;
            request->Status = 1;

            /* Seq last, so the worker cannot pick up a half-built descriptor. */
            const uint32_t seq = request->Seq + 1;
            request->Seq = seq;

            for (int spin = 0; spin < 400000; ++spin)
            {
                if (request->Done == seq)
                    break;

                _mm_pause();
            }

            if (request->Done == seq)
            {
                if (request->Status == 0)
                {
                    memcpy(buffer, g_shared->Data, (size_t)size);
                    return true;
                }

                /* A bad address will not get better on the IOCTL path. */
                return false;
            }
        }

        READ_MEMORY_BUFFER_INFO request = { 0 };
        request.case_number = 0x31;
        request.address = address;
        request.buffer = reinterpret_cast<uint64_t>(buffer);
        request.size = size;

        return Ioctl(&request, sizeof(request), nullptr, 0, nullptr);
    }

    /*
     * Proves the shared block actually works, rather than silently falling back.
     * The same kernel range is read twice - once through the block, once through the
     * NAL IOCTL - and the two buffers must agree. A mismatch means the block is
     * corrupting data, which is far worse than it being absent.
     */
    bool SelfTest()
    {
        if (!EnsureKernelBase())
        {
            km_log::Error("kernel base unresolved - cannot probe");
            return false;
        }

        if (!g_shared || g_shared->Ready != 1 || g_shared->Magic != KM_SHARED_MAGIC)
        {
            km_log::Error("shared block is not registered - run this as part of `load`");
            return false;
        }

        const uint64_t probe = ntoskrnlBase;
        uint8_t viaBlock[16] = { 0 };
        uint8_t viaIoctl[16] = { 0 };

        if (!ReadMemory(probe, viaBlock, sizeof(viaBlock)))
        {
            km_log::Error("shared block read of 0x%llX failed", (unsigned long long)probe);
            return false;
        }

        READ_MEMORY_BUFFER_INFO request = { 0 };
        request.case_number = 0x31;
        request.address = probe;
        request.buffer = reinterpret_cast<uint64_t>(viaIoctl);
        request.size = sizeof(viaIoctl);

        if (!Ioctl(&request, sizeof(request), nullptr, 0, nullptr))
        {
            km_log::Error("IOCTL read of 0x%llX failed", (unsigned long long)probe);
            return false;
        }

        if (memcmp(viaBlock, viaIoctl, sizeof(viaBlock)) != 0)
        {
            km_log::Error("shared block disagrees with the IOCTL path - block is corrupting data");
            return false;
        }

        km_log::Good("shared block verified against IOCTL (%02X %02X %02X %02X ...)",
                     viaBlock[0], viaBlock[1], viaBlock[2], viaBlock[3]);

        return true;
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

    /*
     * Hook sites. A single well-known syscall gets signatured - NtAddAtom is in
     * every second public bypass - so the site is picked per session from a pool of
     * rarely-used ntoskrnl exports. Every entry is a real export, so the call lands
     * in a genuine syscall either way and nothing else about the path changes.
     */
    static const char* const kHookTargets[] =
    {
        "NtAddAtom",
        "NtAllocateLocallyUniqueId",
        "NtSetEventBoostPriority",
        "NtTestAlert",
        "NtYieldExecution",
        "NtDrawText",
        "NtApphelpCacheControl",
        "NtManageHotPatch",
        "NtGetNextProcess"
    };

    /*
     * Stub shapes. Every one ends in an indirect jump, so the return address the
     * syscall dispatcher pushed stays on the stack and the kernel function returns
     * straight back into the dispatcher. Only rax and r10 are written - rcx, rdx,
     * r8 and r9 carry the arguments.
     */
    constexpr size_t kStubMax = 16;

    size_t BuildStub(uint8_t* out, uint64_t target, int shape)
    {
        switch (shape)
        {
        case 0: /* mov rax, imm64 ; jmp rax */
            out[0] = 0x48; out[1] = 0xB8;
            *reinterpret_cast<uint64_t*>(&out[2]) = target;
            out[10] = 0xFF; out[11] = 0xE0;
            return 12;

        case 1: /* the same, NOP padded - different length, different bytes */
            out[0] = 0x48; out[1] = 0xB8;
            *reinterpret_cast<uint64_t*>(&out[2]) = target;
            out[10] = 0xFF; out[11] = 0xE0;
            out[12] = 0x90; out[13] = 0x90; out[14] = 0x90; out[15] = 0x90;
            return 16;

        case 2: /* jmp qword ptr [rip] ; dq target - writes no register */
            out[0] = 0xFF; out[1] = 0x25;
            out[2] = 0x00; out[3] = 0x00; out[4] = 0x00; out[5] = 0x00;
            *reinterpret_cast<uint64_t*>(&out[6]) = target;
            return 14;

        default: /* mov r10, imm64 ; jmp r10 */
            out[0] = 0x49; out[1] = 0xBA;
            *reinterpret_cast<uint64_t*>(&out[2]) = target;
            out[10] = 0x41; out[11] = 0xFF; out[12] = 0xE2;
            return 13;
        }
    }

    bool CallKernelFunctionRaw(void* outResult, uint64_t kernelFunction,
                               const uint64_t args[4], int argCount)
    {
        if (!kernelFunction)
            return false;

        const HMODULE ntdll = GetModuleHandleA("ntdll.dll");
        if (!ntdll)
            return false;

        if (!EnsureKernelBase())
            return false;

        /* Site and shape are chosen once per session and kept: the site is patched
         * and restored around every call, so it is never live for longer than one
         * kernel transition. */
        static uint64_t hookSite = 0;
        static const char* hookName = nullptr;
        static int hookShape = -1;

        if (!hookSite)
        {
            const size_t count = sizeof(kHookTargets) / sizeof(kHookTargets[0]);

            srand((unsigned)time(nullptr) * GetCurrentThreadId() ^ GetTickCount());
            const size_t start = (size_t)rand() % count;

            for (size_t i = 0; i < count; ++i)
            {
                const char* candidate = kHookTargets[(start + i) % count];
                const uint64_t site = GetKernelModuleExport(ntoskrnlBase, candidate);

                /* A bad export walk can return a plausible address from anywhere.
                 * Patching outside ntoskrnl would corrupt an unrelated page. */
                if (!site || site < ntoskrnlBase || site > ntoskrnlBase + 0x2000000ULL)
                    continue;

                hookSite = site;
                hookName = candidate;
                break;
            }

            if (!hookSite)
            {
                km_log::Error("no usable syscall hook site resolved");
                return false;
            }

            hookShape = rand() % 4;

            km_log::Step("hook site %s at 0x%llX, shape %d", hookName,
                          (unsigned long long)hookSite, hookShape);
        }

        /* Resolved once: this is on the path of every kernel call. */
        using call4_t = uint64_t(NTAPI*)(uint64_t, uint64_t, uint64_t, uint64_t);
        static call4_t call = nullptr;

        if (!call)
        {
            call = reinterpret_cast<call4_t>(GetProcAddress(ntdll, hookName));

            if (!call)
            {
                km_log::Error("user thunk for %s is missing", hookName);
                return false;
            }
        }

        uint8_t stub[kStubMax] = { 0 };
        const size_t stubLength = BuildStub(stub, kernelFunction, hookShape);

        uint8_t original[kStubMax] = { 0 };
        if (!ReadMemory(hookSite, original, stubLength))
            return false;

        /* Already patched: an earlier call died before it could restore. Then the
         * bytes read above are our own stub, not the original code. */
        const bool alreadyLive = memcmp(original, stub, stubLength) == 0;

        if (!alreadyLive && !WriteToReadOnlyMemory(hookSite, stub, (uint32_t)stubLength))
            return false;

        if (!alreadyLive)
        {
            /* Confirm the patch landed; a silently failed write would send the next
             * call into garbage. */
            uint8_t verify[kStubMax] = { 0 };

            if (!ReadMemory(hookSite, verify, stubLength) ||
                memcmp(verify, stub, stubLength) != 0)
            {
                WriteToReadOnlyMemory(hookSite, original, (uint32_t)stubLength);
                km_log::Error("%s patch did not take effect", hookName);
                return false;
            }
        }

        /*
         * The syscall dispatch preserves rcx/rdx/r8/r9, so calling the user-mode
         * thunk lands on our patched kernel entry with the arguments intact.
         */
        const uint64_t result = call(argCount > 0 ? args[0] : 0,
                                     argCount > 1 ? args[1] : 0,
                                     argCount > 2 ? args[2] : 0,
                                     argCount > 3 ? args[3] : 0);

        if (!WriteToReadOnlyMemory(hookSite, original, (uint32_t)stubLength))
            km_log::Error("%s restore failed - the patch is still live", hookName);

        if (outResult)
            *reinterpret_cast<uint64_t*>(outResult) = result;

        return true;
    }
}
