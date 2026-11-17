/*
 * kernmem-loader - command line front end.
 *
 * Verbs:
 *   status                    device and version probe
 *   kbase                     resolve the kernel base without loading
 *   load [--client <pid>]     map km.sys through the NAL driver
 *   load --selftest           map, then verify the shared block against the IOCTL path
 *   unload                    unmap the driver, leave NAL resident (no unload record)
 *   release                   unmap the driver and fully release NAL, state and logs
 *   pid <image name>          resolve a pid by image name
 *   mainbase <pid>            image base shortcut
 *   base <pid> <module>       module base by name
 *   modules <pid>             enumerate modules
 *   read <pid> <addr> <size>  hex dump
 *   write <pid> <addr> <hex>  write bytes
 *   attach <pid>              register a client pid for handle restore
 *   overlay                   start the per-game client (F9 in it stops it)
 */
#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <cwctype>
#include <string>
#include <vector>

#include "log.h"
#include "mapper.h"
#include "nal.h"
#include "nt.h"
#include "game.h"
#include "trace.h"
#include "km_shared_loader.h"

#include "res/km_sys.h"

namespace
{
    HANDLE g_device = INVALID_HANDLE_VALUE;

    std::wstring StatePath()
    {
        const std::wstring temp = nt::GetFullTempPath();
        return temp.empty() ? std::wstring(L"km-map.state") : temp + L"\\km-map.state";
    }

    typedef struct _MAP_STATE
    {
        uint64_t imageBase;
        uint64_t imageSize;
        uint64_t driverObject;
        uint64_t entryPoint;
    } MAP_STATE;

    bool SaveMapState(const MAP_STATE& state)
    {
        FILE* file = nullptr;
        if (_wfopen_s(&file, StatePath().c_str(), L"w") != 0 || !file)
            return false;

        fprintf(file, "%llX\n%llX\n%llX\n%llX\n",
                (unsigned long long)state.imageBase,
                (unsigned long long)state.imageSize,
                (unsigned long long)state.driverObject,
                (unsigned long long)state.entryPoint);
        fclose(file);
        return true;
    }

    bool LoadMapState(MAP_STATE* state)
    {
        FILE* file = nullptr;
        if (_wfopen_s(&file, StatePath().c_str(), L"r") != 0 || !file)
            return false;

        unsigned long long base = 0;
        unsigned long long size = 0;
        unsigned long long object = 0;
        unsigned long long entry = 0;

        const int read = fscanf_s(file, "%llX\n%llX\n%llX\n%llX\n", &base, &size, &object, &entry);
        fclose(file);

        if (read != 4 || !base)
            return false;

        state->imageBase = base;
        state->imageSize = size;
        state->driverObject = object;
        state->entryPoint = entry;
        return true;
    }

    void ClearMapState()
    {
        _wremove(StatePath().c_str());
    }

    bool Elevated()
    {
        BOOL admin = FALSE;
        PSID group = nullptr;
        SID_IDENTIFIER_AUTHORITY authority = { SECURITY_NT_AUTHORITY };

        if (AllocateAndInitializeSid(&authority, 2, SECURITY_BUILTIN_DOMAIN_RID,
                                     DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &group))
        {
            CheckTokenMembership(nullptr, group, &admin);
            FreeSid(group);
        }

        return admin != FALSE;
    }

    bool OpenDevice()
    {
        if (g_device != INVALID_HANDLE_VALUE && g_device)
            return true;

        /*
         * The device name is per install, so the candidates come from nal instead
         * of a fixed literal: this install's DOS symlink and device object first,
         * the historical namespace last. A teardown that deleted the symlink but
         * left the device pinned by an open handle leaves the object path as the
         * only way in, and without it every later open fails with "file not
         * found" while the driver is still answering.
         */
        for (const std::wstring& path : nal::DevicePaths())
        {
            g_device = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE,
                                   FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                   OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);

            if (g_device != INVALID_HANDLE_VALUE)
                return true;
        }

        return false;
    }

    void CloseDevice()
    {
        if (g_device != INVALID_HANDLE_VALUE && g_device)
            CloseHandle(g_device);

        g_device = INVALID_HANDLE_VALUE;
    }

    bool Ioctl(ULONG code, const void* in, DWORD inSize, void* out, DWORD outSize, DWORD* returned)
    {
        if (!OpenDevice())
            return false;

        /* The device type is per install. The call sites keep using the
         * compile-time codes and the re-stamp happens here, once. */
        DWORD bytes = 0;
        const BOOL ok = DeviceIoControl(g_device, nal::RecodeIoctl(code), const_cast<void*>(in),
                                        inSize, out, outSize, &bytes, nullptr);
        if (returned)
            *returned = bytes;

        return ok != FALSE;
    }

    void HexDump(const uint8_t* data, size_t size)
    {
        for (size_t offset = 0; offset < size; offset += 16)
        {
            printf("%016llX  ", (unsigned long long)offset);

            for (size_t i = 0; i < 16; ++i)
                offset + i < size ? printf("%02X ", data[offset + i]) : printf("   ");

            printf(" |");

            for (size_t i = 0; i < 16 && offset + i < size; ++i)
            {
                const uint8_t c = data[offset + i];
                printf("%c", (c >= 0x20 && c < 0x7F) ? c : '.');
            }

            printf("|\n");
        }
    }

    bool ParseHex(const wchar_t* text, std::vector<uint8_t>* out)
    {
        std::string digits;

        for (const wchar_t* p = text; *p; ++p)
        {
            if (*p == L' ' || *p == L'\t' || *p == L'-' || *p == L',')
                continue;

            if (!iswxdigit(*p))
                return false;

            digits.push_back(static_cast<char>(*p));
        }

        if (digits.empty() || (digits.size() % 2) != 0)
            return false;

        for (size_t i = 0; i < digits.size(); i += 2)
            out->push_back((uint8_t)strtoul(digits.substr(i, 2).c_str(), nullptr, 16));

        return true;
    }

    uint64_t ParseAddress(const wchar_t* text)
    {
        return _wcstoui64(text, nullptr, 0);
    }

    /*
     * SEH verification of a kernel base.
     *
     * Kept in its own function with no C++ objects: MSVC refuses `__try` in a
     * function that needs object unwinding (C2712), and the caller holds a
     * std::wstring.
     */
    void VerifyKernelBase(uint64_t base)
    {
        __try
        {
            const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);

            if (dos->e_magic != IMAGE_DOS_SIGNATURE)
            {
                km_log::Warn("base does not start with MZ (0x%04X)", dos->e_magic);
                return;
            }

            const auto ntHeaders = reinterpret_cast<const IMAGE_NT_HEADERS64*>(
                base + dos->e_lfanew);

            if (ntHeaders->Signature != IMAGE_NT_SIGNATURE)
            {
                km_log::Warn("base has no NT signature");
                return;
            }

            km_log::Good("kernel PE header valid, image size 0x%X",
                          ntHeaders->OptionalHeader.SizeOfImage);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            /* User mode cannot read supervisor pages, so a fault here is expected
             * and says nothing about whether the base is correct. */
            km_log::Warn("base not directly readable from user mode (0x%08X) - expected",
                          GetExceptionCode());
        }
    }

    /*
     * Resolves and prints the kernel image base, then tears the mapping host down
     * again. Needs elevation because the scan route reads kernel memory.
     */
    int CmdKernelBase()
    {
        if (!Elevated())
        {
            km_log::Error("elevated shell required - the scan reads kernel memory");
            return 1;
        }

        /*
         * The scan route reads through the vulnerable driver, so it has to be up.
         * `kbase` therefore loads it, resolves, and unloads again - proving the
         * resolver without mapping our own driver at all.
         */
        const bool alreadyUp = nal::IsRunning();
        const std::wstring statePath = nt::GetFullTempPath() + L"\\km-nal.state";
        nal::SetStatePath(statePath);

        if (!alreadyUp && !nal::Load(nullptr))
        {
            km_log::Error("could not bring up the mapping host");
            return 1;
        }

        const uint64_t base = nal::ResolveKernelBase();

        if (!alreadyUp)
        {
            nal::Unload(false);
            km_log::Line("*", "mapping host detached (still resident)");
        }

        if (!base)
        {
            km_log::Error("kernel base unresolved on every route");
            return 1;
        }

        km_log::Good("kernel base 0x%llX", (unsigned long long)base);

        /* Prove the base is really an image, not a stray MZ in some other page. */
        VerifyKernelBase(base);

        return 0;
    }

    int CmdStatus()
    {
        km_log::Line("*", "NAL: %s", nal::IsRunning() ? "resident" : "not loaded");

        if (!OpenDevice())
        {
            km_log::Line("*", "device: not loaded");
            return 0;
        }

        ULONG version = 0;
        if (Ioctl(IOCTL_KM_GET_VERSION, nullptr, 0, &version, sizeof(version), nullptr))
        {
            km_log::Good("device: present, protocol %u", version);
            if (version != KM_PROTOCOL_VERSION)
                km_log::Warn("protocol mismatch (expected %u) - rebuild loader and driver together",
                          KM_PROTOCOL_VERSION);
        }
        else
        {
            km_log::Error("device open but version probe failed");
            return 1;
        }

        return 0;
    }

    /* Declared here because the stale-image path inside CmdLoad evicts by calling
     * it, and CmdUnload is defined further down. */
    int CmdUnload(bool release);

    int CmdLoad(uint32_t clientPid, bool selfTest = false)
    {
        if (!Elevated())
        {
            km_log::Error("not elevated - driver loading needs SeLoadDriverPrivilege");
            return 1;
        }

        if (OpenDevice())
        {
            /* A live device at our protocol is a success, not an error: the
             * one-click path checks this too, so a second double-click is harmless. */
            ULONG existing = 0;

            if (Ioctl(IOCTL_KM_GET_VERSION, nullptr, 0, &existing, sizeof(existing), nullptr) &&
                existing == KM_PROTOCOL_VERSION)
            {
                km_log::Good("device already present, protocol %u - reusing it", existing);

                /* Nothing was mapped by this process, so there is no shared block to
                 * verify - say so rather than reporting a pass. */
                if (selfTest)
                    km_log::Warn("self-test needs a fresh load - the block is registered at map time");

                return 0;
            }

            /*
             * A different build is mapped - almost always one left resident by an
             * earlier session, because a plain unload deliberately leaves the host up
             * so no unload record is written. Evict it here instead of telling the
             * user to run unload: anything from protocol 4 on can report its own
             * mapping through GET_INFO, which is all the unmap needs, and asking a
             * human to do it by hand just loops.
             */
            km_log::Warn("a different driver build is mapped (protocol %u, expected %u) - evicting",
                         existing, KM_PROTOCOL_VERSION);

            CloseDevice();

            if (CmdUnload(false) != 0)
            {
                km_log::Error("could not evict the mapped image - reboot if it persists");
                return 1;
            }

            /* The stale image is gone, so fall through and map ours. */
        }

        const std::wstring nalState = nt::GetFullTempPath() + L"\\km-nal.state";
        nal::SetStatePath(nalState);

        if (!nal::Load(nullptr))
        {
            km_log::Error("could not bring up the mapping host");
            return 1;
        }

        /*
         * The host is a real service, so ntoskrnl recorded the previous session's
         * copy in MmUnloadedDrivers and in the PiDDB cache. Clear that now, while
         * kernel access is available, instead of leaving a growing trail.
         */
        if (!nal::DriverPath().empty())
            trace::Scrub(nal::DriverPath());

        std::wstring imagePath;
        if (!nt::GetFullTempPath().empty())
        {
            /* Random, so nothing on disk carries a project prefix. The sweep finds
             * a leftover by its bytes, never by the name. */
            imagePath = nt::GetFullTempPath() + L"\\" + nal::RandomImageName();
            if (!nt::CreateFileFromMemory(imagePath,
                                          reinterpret_cast<const char*>(km_sys),
                                          km_sys_size))
            {
                km_log::Error("could not stage the driver image");
                nal::Unload(true);
                return 1;
            }
        }

        mapper::MAP_RESULT result = { 0 };
        if (!mapper::Map(imagePath, clientPid, &result))
        {
            km_log::Error("mapping failed");
            _wremove(imagePath.c_str());
            nal::Unload(true);
            return 1;
        }

        _wremove(imagePath.c_str());

        /* Open whatever name the driver ended up with, not the one we asked for. */
        if (result.deviceName[0])
            nal::SetDeviceName(result.deviceName);

        /* Persist it. The adopt path had no service name, so its own write was a
         * no-op, and the client has nothing to open without this file. */
        nal::PersistState();

        MAP_STATE state = { result.imageBase, result.imageSize, result.driverObject, result.entryPoint };
        SaveMapState(state);

        if (!OpenDevice())
        {
            km_log::Error("driver mapped but the device did not appear");
            nal::DestroyShared();
            return 1;
        }

        ULONG version = 0;
        if (!Ioctl(IOCTL_KM_GET_VERSION, nullptr, 0, &version, sizeof(version), nullptr) ||
            version != KM_PROTOCOL_VERSION)
        {
            km_log::Error("mapped image does not answer with protocol %u", KM_PROTOCOL_VERSION);

            /* The block is already registered against this process here, so it has
             * to go back before the exit or the reap bugchecks 0x76. */
            nal::DestroyShared();
            return 1;
        }

        km_log::Good("driver loaded, protocol %u", version);

        /*
         * The host is up and answering, so the file it was loaded from is no
         * longer needed. Removing it now rather than on release means the temp
         * directory only holds the image while the map is actually in progress.
         */
        nal::HideTrail();

        if (clientPid)
            km_log::Good("client pid %u registered", clientPid);

        /*
         * Verify the shared block here, in the process that mapped the driver and so
         * owns the registered block. A silent fallback to IOCTL would otherwise look
         * exactly like success.
         */
        if (selfTest)
        {
            if (!nal::SelfTest())
            {
                km_log::Error("shared block self-test failed - reads are on the IOCTL path");
                nal::DestroyShared();
                return 1;
            }
        }

        return 0;
    }

    /*
     * release == false detaches and leaves the host resident, so ntoskrnl never
     * writes an MmUnloadedDrivers record for it. release == true does the full
     * teardown - service, key, dropped image, state file - and clears the logs.
     */
    int CmdUnload(bool release)
    {
        if (!Elevated())
        {
            km_log::Error("not elevated");
            return 1;
        }

        CloseDevice();

        nal::SetStatePath(nt::GetFullTempPath() + L"\\km-nal.state");

        /*
         * Ask the driver for its own mapping first. The state file is only a
         * fallback: when it was missing, unmap was skipped entirely and the image
         * leaked in pool for good (the device kept answering after "unload").
         */
        MAP_STATE state = { 0 };
        KM_INFO info = { 0 };

        if (Ioctl(IOCTL_KM_GET_INFO, nullptr, 0, &info, sizeof(info), nullptr))
        {
            state.imageBase = info.ImageBase;
            state.imageSize = info.ImageSize;
            state.driverObject = info.DriverObject;
        }

        CloseDevice();

        if (!state.imageBase)
            LoadMapState(&state);

        if (state.imageBase)
        {
            /* Unmap reads kernel memory, so the host has to be open first. Without
             * this the reads failed, DriverUnload was skipped, and the image was
             * freed while the device was still live. */
            if (!nal::Attach())
            {
                km_log::Error("cannot open the mapping host - driver NOT unmapped");
                return 1;
            }

            mapper::MAP_RESULT result = { state.imageBase, state.imageSize, state.driverObject,
                                          state.entryPoint, STATUS_SUCCESS };

            if (!mapper::Unmap(result))
            {
                km_log::Error("unmap failed - state kept, retry unload");
                return 1;
            }

            ClearMapState();
            km_log::Good("driver unmapped");
        }
        else
        {
            km_log::Warn("driver is not mapped - skipping unmap");
        }

        /* Still open: clear this session's trace before releasing the host. */
        if (!nal::DriverPath().empty())
            trace::Scrub(nal::DriverPath());

        nal::Unload(release);

        if (release)
        {
            km_log::ClearLogs();
            km_log::Good("mapping host released and trace cleared");
        }
        else
        {
            km_log::Good("mapping host left resident - no unload record");
        }

        return 0;
    }

    int CmdPid(const wchar_t* name)
    {
        KM_NAME_REQ request = { 0 };
        KM_PID_REQ reply = { 0 };

        request.Pid = 0;
        wcsncpy_s(request.Name, KM_MAX_NAME_CHARS, name, KM_MAX_NAME_CHARS - 1);

        if (!Ioctl(IOCTL_KM_GET_PID_BY_NAME, &request, sizeof(request), &reply, sizeof(reply), nullptr))
        {
            km_log::Error("pid lookup failed (0x%08X)", GetLastError());
            return 1;
        }

        if (!reply.Pid)
        {
            km_log::Error("no process named %ls", name);
            return 1;
        }

        printf("%u\n", reply.Pid);
        return 0;
    }

    int CmdMainBase(uint32_t pid)
    {
        KM_PID_REQ request = { pid, 0 };
        uint64_t base = 0;

        if (!Ioctl(IOCTL_KM_GET_MAIN_BASE, &request, sizeof(request), &base, sizeof(base), nullptr))
        {
            km_log::Error("main base failed (0x%08X)", GetLastError());
            return 1;
        }

        printf("0x%llX\n", (unsigned long long)base);
        return 0;
    }

    int CmdBase(uint32_t pid, const wchar_t* module)
    {
        KM_NAME_REQ request = { 0 };
        uint64_t base = 0;

        request.Pid = pid;
        wcsncpy_s(request.Name, KM_MAX_NAME_CHARS, module, KM_MAX_NAME_CHARS - 1);

        if (!Ioctl(IOCTL_KM_GET_MODULE_BASE, &request, sizeof(request), &base, sizeof(base), nullptr))
        {
            km_log::Error("module lookup failed (0x%08X)", GetLastError());
            return 1;
        }

        printf("0x%llX\n", (unsigned long long)base);
        return 0;
    }

    int CmdModules(uint32_t pid)
    {
        const DWORD capacity = 256 * 1024;
        std::vector<uint8_t> buffer(capacity);
        KM_PID_REQ request = { pid, 0 };
        DWORD returned = 0;

        if (!Ioctl(IOCTL_KM_ENUM_MODULES, &request, sizeof(request), buffer.data(), capacity, &returned))
        {
            km_log::Error("module enumeration failed (0x%08X)", GetLastError());
            return 1;
        }

        auto list = reinterpret_cast<KM_MODULE_LIST*>(buffer.data());

        for (ULONG i = 0; i < list->Count; ++i)
            printf("0x%016llX  %8u  %ls\n",
                   (unsigned long long)list->Entries[i].Base,
                   list->Entries[i].SizeOfImage,
                   list->Entries[i].Name);

        if (list->Truncated)
            km_log::Warn("list truncated at %u entries", list->Count);

        return 0;
    }

    int CmdRead(uint32_t pid, uint64_t address, uint32_t size)
    {
        if (!size)
        {
            km_log::Error("size must be non-zero");
            return 1;
        }

        std::vector<uint8_t> buffer(size);
        size_t done = 0;

        while (done < size)
        {
            const size_t remaining = size - done;
            const uint32_t chunk = (uint32_t)(remaining < KM_MAX_XFER ? remaining : KM_MAX_XFER);
            KM_MEM_REQ request = { pid, chunk, address + done };
            DWORD returned = 0;

            if (!Ioctl(IOCTL_KM_READ, &request, sizeof(request), buffer.data() + done,
                       chunk, &returned))
            {
                km_log::Error("read failed at 0x%llX (0x%08X)",
                           (unsigned long long)(address + done), GetLastError());
                return 1;
            }

            if (!returned)
                break;

            done += returned;
        }

        HexDump(buffer.data(), done);
        return 0;
    }

    int CmdWrite(uint32_t pid, uint64_t address, const wchar_t* hex)
    {
        std::vector<uint8_t> bytes;

        if (!ParseHex(hex, &bytes) || bytes.empty())
        {
            km_log::Error("could not parse the hex payload");
            return 1;
        }

        size_t done = 0;

        while (done < bytes.size())
        {
            const size_t remaining = bytes.size() - done;
            const uint32_t chunk = (uint32_t)(remaining < KM_MAX_XFER ? remaining : KM_MAX_XFER);
            std::vector<uint8_t> packet(sizeof(KM_MEM_REQ) + chunk);

            auto request = reinterpret_cast<KM_MEM_REQ*>(packet.data());
            request->Pid = pid;
            request->Size = chunk;
            request->Address = address + done;
            memcpy(packet.data() + sizeof(KM_MEM_REQ), bytes.data() + done, chunk);

            if (!Ioctl(IOCTL_KM_WRITE, packet.data(), (DWORD)packet.size(), nullptr, 0, nullptr))
            {
                km_log::Error("write failed at 0x%llX (0x%08X)",
                           (unsigned long long)(address + done), GetLastError());
                return 1;
            }

            done += chunk;
        }

        km_log::Good("wrote %zu bytes at 0x%llX", done, (unsigned long long)address);
        return 0;
    }

    int CmdAttach(uint32_t pid)
    {
        KM_PID_REQ request = { pid, 0 };

        if (!Ioctl(IOCTL_KM_SET_CLIENT_PID, &request, sizeof(request), nullptr, 0, nullptr))
        {
            km_log::Error("attach failed (0x%08X)", GetLastError());
            return 1;
        }

        km_log::Good("client pid %u registered for handle restoration", pid);
        return 0;
    }

    int CmdOverlay(uint32_t pid)
    {
        UNREFERENCED_PARAMETER(pid);

        if (game::IsRunning())
        {
            game::Stop();
            km_log::Good("client stopped");
            return 0;
        }

        if (!OpenDevice())
        {
            km_log::Error("driver is not loaded - load it first");
            return 1;
        }

        /* The client resolves the pid itself now, so the argument is only kept for
         * command-line compatibility. */
        if (!game::Start())
            return 1;

        km_log::Good("client running - F9 in it stops it");
        return 0;
    }
}

/* ------------------------------------------------------------------ */
/* interactive UI                                                      */
/* ------------------------------------------------------------------ */

namespace
{
    uint32_t g_sessionPid = 0;

    uint32_t LookupPid(const wchar_t* name)
    {
        KM_NAME_REQ request = { 0 };
        KM_PID_REQ reply = { 0 };

        wcsncpy_s(request.Name, KM_MAX_NAME_CHARS, name, KM_MAX_NAME_CHARS - 1);

        if (!Ioctl(IOCTL_KM_GET_PID_BY_NAME, &request, sizeof(request),
                   &reply, sizeof(reply), nullptr))
            return 0;

        return reply.Pid;
    }

    uint64_t MainBaseOf(uint32_t pid)
    {
        KM_PID_REQ request = { pid, 0 };
        uint64_t base = 0;

        if (!Ioctl(IOCTL_KM_GET_MAIN_BASE, &request, sizeof(request), &base, sizeof(base), nullptr))
            return 0;

        return base;
    }

    uint64_t ModuleBaseOf(uint32_t pid, const wchar_t* name)
    {
        KM_NAME_REQ request = { 0 };
        uint64_t base = 0;

        request.Pid = pid;
        wcsncpy_s(request.Name, KM_MAX_NAME_CHARS, name, KM_MAX_NAME_CHARS - 1);

        if (!Ioctl(IOCTL_KM_GET_MODULE_BASE, &request, sizeof(request), &base, sizeof(base), nullptr))
            return 0;

        return base;
    }

    bool ReadLine(wchar_t* buffer, size_t count)
    {
        if (!fgetws(buffer, (int)count, stdin))
            return false;

        size_t length = wcslen(buffer);
        while (length && (buffer[length - 1] == L'\n' || buffer[length - 1] == L'\r'))
            buffer[--length] = 0;

        return true;
    }

    void Pause()
    {
        printf("\n  enter to continue...");

        int c;
        while ((c = getchar()) != '\n' && c != EOF) { }
    }

    void ShowHeader()
    {
        printf("\n==================================================\n");
        printf("  Kernmem\n");
        printf("==================================================\n");
        printf("  driver : %s\n", OpenDevice() ? "LOADED" : "not loaded");
        printf("  shell  : %s\n", Elevated() ? "elevated" : "NOT elevated (load will fail)");

        if (g_sessionPid)
            printf("  target : pid %u\n", g_sessionPid);
        else
            printf("  target : none - use option 2\n");

        printf("--------------------------------------------------\n");
    }

    uint32_t ResolveGame()
    {
        if (g_sessionPid)
            return g_sessionPid;

        const uint32_t pid = LookupPid(L"League of Legends.exe");

        if (!pid)
        {
            km_log::Error("game not running - the process only exists during a match");
            return 0;
        }

        g_sessionPid = pid;
        return pid;
    }

    void UiLoad()
    {
        CmdLoad(GetCurrentProcessId());
    }

    /*
     * Relaunches this executable elevated. Returns true when the caller may carry
     * on; false means a new elevated instance took over (or UAC was declined) and
     * this one should exit.
     */
    bool SelfElevate(wchar_t* const* argv)
    {
        if (Elevated())
            return true;

        wchar_t path[MAX_PATH] = { 0 };

        if (!GetModuleFileNameW(nullptr, path, MAX_PATH))
            return false;

        std::wstring arguments;

        for (int i = 1; argv[i]; ++i)
        {
            arguments += L"\"";
            arguments += argv[i];
            arguments += L"\" ";
        }

        SHELLEXECUTEINFOW info = { 0 };
        info.cbSize = sizeof(info);
        info.fMask = SEE_MASK_NOCLOSEPROCESS;
        info.lpVerb = L"runas";
        info.lpFile = path;
        info.lpParameters = arguments.empty() ? nullptr : arguments.c_str();
        info.nShow = SW_SHOWNORMAL;

        if (!ShellExecuteExW(&info))
        {
            km_log::Error("elevation failed or was declined (0x%08X)", GetLastError());
            km_log::Info("right-click the exe and choose Run as administrator");
            return false;
        }

        if (info.hProcess)
            CloseHandle(info.hProcess);

        return false;
    }

    bool FileExists(const wchar_t* path)
    {
        return GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES;
    }

    /* Starts the Riot client if it is installed at a known path. Best effort. */
    bool LaunchClient()
    {
        static const wchar_t* candidates[] =
        {
            L"C:\\Riot Games\\Riot Client\\RiotClientServices.exe",
            L"C:\\Program Files\\Riot Games\\Riot Client\\RiotClientServices.exe",
        };

        for (const wchar_t* candidate : candidates)
        {
            if (!FileExists(candidate))
                continue;

            if (ShellExecuteW(nullptr, L"open", candidate,
                              L"--launch-product=league_of_legends --launch-patchline=live",
                              nullptr, SW_SHOWNORMAL))
                return true;
        }

        return false;
    }

    /*
     * Waits for the game process. League of Legends.exe only exists inside a match,
     * so the wait is bounded and reports progress instead of hanging silently.
     */
    uint32_t WaitForGame(int seconds)
    {
        const int ticks = seconds * 10;

        for (int i = 0; i < ticks; ++i)
        {
            const uint32_t pid = LookupPid(L"League of Legends.exe");

            if (pid)
                return pid;

            if (i % 50 == 0)
                km_log::Step("waiting for the game%s", i ? "" : " - enter a match");

            Sleep(100);
        }

        return 0;
    }

    /*
     * The product path: double-click the exe, everything else happens here.
     * Elevate, load, find or start the game, attach, draw.
     */
    int RunOneClick()
    {
        printf("\n  Kernmem\n");
        printf("  ==============================================\n\n");

        if (OpenDevice())
        {
            km_log::Good("driver already loaded");
        }
        else
        {
            km_log::Step("loading the driver");

            if (CmdLoad(GetCurrentProcessId()) != 0)
            {
                km_log::Error("driver load failed - see above");
                Pause();
                return 1;
            }
        }

        uint32_t pid = LookupPid(L"League of Legends.exe");

        if (!pid)
        {
            if (LaunchClient())
                km_log::Info("starting the Riot client - log in and enter a match");
            else
                km_log::Info("open League of Legends and enter a match");

            pid = WaitForGame(300);
        }

        if (!pid)
        {
            km_log::Error("no game process found - is League of Legends installed?");
            Pause();
            return 1;
        }

        g_sessionPid = pid;
        km_log::Good("game pid %u", pid);

        /* Lets this process use plain ReadProcessMemory too. */
        CmdAttach(GetCurrentProcessId());

        if (!game::Start())
        {
            km_log::Error("the client did not start");
            Pause();
            return 1;
        }

        km_log::Good("client running");
        km_log::Info("F9 in the client stops it. Close this window when done.");
        km_log::Info("nothing is drawn until you are in a match.");

        /* Keep the device open - the client reads through this loader's mapping - 
         * until the client exits. */
        while (game::IsRunning())
            Sleep(200);

        km_log::Info("client stopped");
        Pause();
        return 0;
    }

    void UiAttach()
    {
        const uint32_t pid = ResolveGame();
        if (!pid)
            return;

        CmdAttach(GetCurrentProcessId());

        const uint64_t base = MainBaseOf(pid);

        if (base)
            km_log::Good("image base 0x%llX", (unsigned long long)base);
        else
            km_log::Warn("could not resolve the image base");
    }

    void UiRead()
    {
        const uint32_t pid = ResolveGame();
        if (!pid)
            return;

        wchar_t line[64] = { 0 };
        wchar_t sizeLine[32] = { 0 };

        printf("  address (hex): ");
        if (!ReadLine(line, 64) || !line[0])
            return;

        printf("  size (bytes): ");
        if (!ReadLine(sizeLine, 32))
            return;

        CmdRead(pid, _wcstoui64(line, nullptr, 0), (uint32_t)_wtoi(sizeLine));
    }

    void UiWrite()
    {
        const uint32_t pid = ResolveGame();
        if (!pid)
            return;

        wchar_t line[64] = { 0 };
        wchar_t hex[512] = { 0 };

        printf("  address (hex): ");
        if (!ReadLine(line, 64) || !line[0])
            return;

        printf("  bytes (hex, space separated): ");
        if (!ReadLine(hex, 512))
            return;

        CmdWrite(pid, _wcstoui64(line, nullptr, 0), hex);
    }

    void UiModules()
    {
        const uint32_t pid = ResolveGame();
        if (!pid)
            return;

        CmdModules(pid);
    }

    void UiHide(bool hide)
    {
        const uint32_t pid = ResolveGame();
        if (!pid)
            return;

        wchar_t name[KM_MAX_NAME_CHARS] = { 0 };

        printf("  module name: ");
        if (!ReadLine(name, KM_MAX_NAME_CHARS) || !name[0])
            return;

        const uint64_t base = ModuleBaseOf(pid, name);

        if (!base)
        {
            km_log::Error("module not found: %ls", name);
            return;
        }

        struct { uint32_t Pid; uint32_t Pad; uint64_t Base; } request = { pid, 0, base };
        const ULONG code = hide ? IOCTL_KM_HIDE_MODULE : IOCTL_KM_UNHIDE_MODULE;

        if (!Ioctl(code, &request, sizeof(request), nullptr, 0, nullptr))
        {
            km_log::Error("%s failed (0x%08X)", hide ? "hide" : "unhide", GetLastError());
            return;
        }

        km_log::Good("%s: %ls (0x%llX)", hide ? "hidden" : "unhidden", name,
                      (unsigned long long)base);
    }

    void UiOverlay()
    {
        const uint32_t pid = ResolveGame();
        if (!pid)
            return;

        CmdOverlay(pid);
    }

    int RunUi()
    {
        wchar_t line[64] = { 0 };

        for (;;)
        {
            ShowHeader();
            printf("  1) load driver\n");
            printf("  2) attach to game\n");
            printf("  3) read memory\n");
            printf("  4) write memory\n");
            printf("  5) list modules\n");
            printf("  6) hide module\n");
            printf("  7) unhide module\n");
            printf("  8) status\n");
            printf("  9) unload driver\n");
            printf("  o) start/stop client\n");
            printf("  0) quit\n");
            printf("\n  choice: ");

            if (!ReadLine(line, 64))
                break;

            printf("\n");

            switch (line[0])
            {
            case L'1': UiLoad(); break;
            case L'2': UiAttach(); break;
            case L'3': UiRead(); break;
            case L'4': UiWrite(); break;
            case L'5': UiModules(); break;
            case L'6': UiHide(true); break;
            case L'7': UiHide(false); break;
            case L'8': CmdStatus(); break;
            case L'9':
                game::Stop();
                CmdUnload(false);
                g_sessionPid = 0;
                break;
            case L'o':
            case L'O':
                UiOverlay();
                break;
            case L'0': return 0;
            default: km_log::Warn("unknown choice"); break;
            }

            Pause();
        }

        return 0;
    }
}

/*
 * Windows virtualises coordinates for a DPI-unaware process: on a 4K panel at 150%
 * GetClientRect reports 2545x1354 for a 3840x2160 game window, the client draws
 * into that smaller surface, and the compositor then stretches it back up. Every
 * drawn pixel therefore lands about 1.5x too far from the origin, and the surface
 * does not even cover the window.
 *
 * Opt into per-monitor awareness before any window or DC exists, newest API first
 * so a mixed-DPI setup is handled correctly rather than clamped to one scale.
 */
static void BecomeDpiAware()
{
    /* Per-monitor v2: 26200. */
    typedef BOOL (WINAPI* SetContextFn)(HANDLE);

    if (HMODULE user32 = GetModuleHandleW(L"user32.dll"))
    {
        if (auto setContext = reinterpret_cast<SetContextFn>(
                GetProcAddress(user32, "SetProcessDpiAwarenessContext")))
        {
            /* DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 */
            if (setContext(reinterpret_cast<HANDLE>(-4)))
                return;
        }
    }

    /* Shcore fallback: PROCESS_PER_MONITOR_DPI_AWARE. */
    if (HMODULE shcore = LoadLibraryW(L"shcore.dll"))
    {
        typedef HRESULT (WINAPI* SetAwarenessFn)(int);

        if (auto setAwareness = reinterpret_cast<SetAwarenessFn>(
                GetProcAddress(shcore, "SetProcessDpiAwareness")))
        {
            if (SUCCEEDED(setAwareness(2)))
                return;
        }
    }

    /* Last resort: system-wide awareness, which at least stops the stretching. */
    SetProcessDPIAware();
}

int wmain(int argc, wchar_t** argv)
{
    BecomeDpiAware();

    /*
     * Crash log first, before anything can fault. The file is flushed per line, so
     * after a bugcheck the last entry names the step that was running. The path is
     * printed so it can be found without guessing.
     */
    km_log::InstallExceptionFilter();

    const std::wstring crashLog = km_log::OpenCrashLog();

    if (!crashLog.empty())
        km_log::Info("log: %ls", crashLog.c_str());
    else
        printf("[!] could not open a crash log\n");

    km_log::Line("*", "kernmem-loader started, %d argument(s)", argc - 1);

    /* No arguments at all is the product: one click, everything automatic. */
    if (argc < 2)
    {
        if (!SelfElevate(argv))
            return 0;

        return RunOneClick();
    }

    if (std::wstring(argv[1]) == L"ui")
    {
        if (!SelfElevate(argv))
            return 0;

        return RunUi();
    }

    const std::wstring verb = argv[1];
    int result = 1;

    if (verb == L"status")
    {
        result = CmdStatus();
    }
    else if (verb == L"kbase")
    {
        result = CmdKernelBase();
    }
    else if (verb == L"auto")
    {
        if (SelfElevate(argv))
            result = RunOneClick();
        else
            result = 0;
    }
    else if (verb == L"load")
    {
        uint32_t clientPid = 0;
        bool selfTest = false;

        for (int i = 2; i < argc; ++i)
        {
            if (std::wstring(argv[i]) == L"--selftest")
                selfTest = true;

            if (std::wstring(argv[i]) == L"--client" && i + 1 < argc)
                clientPid = (uint32_t)_wtoi(argv[i + 1]);
        }

        result = CmdLoad(clientPid, selfTest);
    }
    else if (verb == L"unload")
    {
        result = CmdUnload(false);
    }
    else if (verb == L"release")
    {
        result = CmdUnload(true);
    }
    else if (verb == L"pid" && argc >= 3)
    {
        result = CmdPid(argv[2]);
    }
    else if (verb == L"mainbase" && argc >= 3)
    {
        result = CmdMainBase((uint32_t)_wtoi(argv[2]));
    }
    else if (verb == L"base" && argc >= 4)
    {
        result = CmdBase((uint32_t)_wtoi(argv[2]), argv[3]);
    }
    else if (verb == L"modules" && argc >= 3)
    {
        result = CmdModules((uint32_t)_wtoi(argv[2]));
    }
    else if (verb == L"read" && argc >= 5)
    {
        result = CmdRead((uint32_t)_wtoi(argv[2]), ParseAddress(argv[3]), (uint32_t)_wtoi(argv[4]));
    }
    else if (verb == L"write" && argc >= 5)
    {
        result = CmdWrite((uint32_t)_wtoi(argv[2]), ParseAddress(argv[3]), argv[4]);
    }
    else if (verb == L"attach" && argc >= 3)
    {
        result = CmdAttach((uint32_t)_wtoi(argv[2]));
    }
    else if (verb == L"overlay")
    {
        /* No pid any more - the client resolves it. */
        result = CmdOverlay(argc >= 3 ? (uint32_t)_wtoi(argv[2]) : 0);

        /* The client reads through this process's mapping, so the CLI form has to
         * stay resident until it exits. F9 in the client ends it. */
        while (result == 0 && game::IsRunning())
            Sleep(100);
    }
    else
    {
        km_log::Error("unknown verb or missing arguments");
    }

    /*
     * The shared block is locked into THIS process, so it has to be handed back
     * before the address space goes away - otherwise the process is reaped with
     * 33 pages still locked (0x76). DestroyShared closes the control handle, so
     * CloseDevice has to run first: cleanup only arrives on the last close.
     * A verb that already tore the block down (unload, release) is a no-op here.
     */
    CloseDevice();
    nal::DestroyShared();

    return result;
}
