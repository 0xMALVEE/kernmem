/*
 * vgb-loader - command line front end.
 *
 * Verbs:
 *   status                    device and version probe
 *   load [--client <pid>]     map vgb.sys through the NAL driver
 *   unload                    unmap the driver and release NAL
 *   pid <image name>          resolve a pid by image name
 *   mainbase <pid>            image base shortcut
 *   base <pid> <module>       module base by name
 *   modules <pid>             enumerate modules
 *   read <pid> <addr> <size>  hex dump
 *   write <pid> <addr> <hex>  write bytes
 *   attach <pid>              register a client pid for handle restore
 *   overlay <pid>             toggle the in-game overlay (F9 stops it)
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
#include "overlay.h"
#include "vgb_shared_loader.h"

#include "res/vgb_sys.h"

namespace
{
    HANDLE g_device = INVALID_HANDLE_VALUE;

    std::wstring StatePath()
    {
        const std::wstring temp = nt::GetFullTempPath();
        return temp.empty() ? std::wstring(L"vgb-map.state") : temp + L"\\vgb-map.state";
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

        g_device = CreateFileW(VGB_WIN32_NAME, GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);

        return g_device != INVALID_HANDLE_VALUE;
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

        DWORD bytes = 0;
        const BOOL ok = DeviceIoControl(g_device, code, const_cast<void*>(in), inSize,
                                        out, outSize, &bytes, nullptr);
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

    int CmdStatus()
    {
        vgb_log::Line("*", "NAL: %s", nal::IsRunning() ? "resident" : "not loaded");

        if (!OpenDevice())
        {
            vgb_log::Line("*", "device: not loaded");
            return 0;
        }

        ULONG version = 0;
        if (Ioctl(IOCTL_VGB_GET_VERSION, nullptr, 0, &version, sizeof(version), nullptr))
        {
            vgb_log::Good("device: present, protocol %u", version);
            if (version != VGB_PROTOCOL_VERSION)
                vgb_log::Warn("protocol mismatch (expected %u) - rebuild loader and driver together",
                          VGB_PROTOCOL_VERSION);
        }
        else
        {
            vgb_log::Error("device open but version probe failed");
            return 1;
        }

        return 0;
    }

    int CmdLoad(uint32_t clientPid)
    {
        if (!Elevated())
        {
            vgb_log::Error("not elevated - driver loading needs SeLoadDriverPrivilege");
            return 1;
        }

        if (OpenDevice())
        {
            vgb_log::Warn("device already present - unload first");
            return 1;
        }

        const std::wstring nalState = nt::GetFullTempPath() + L"\\vgb-nal.state";
        nal::SetStatePath(nalState);

        if (!nal::Load(nullptr))
        {
            vgb_log::Error("could not bring up the mapping host");
            return 1;
        }

        std::wstring imagePath;
        if (!nt::GetFullTempPath().empty())
        {
            imagePath = nt::GetFullTempPath() + L"\\vgb-mapped.sys";
            if (!nt::CreateFileFromMemory(imagePath,
                                          reinterpret_cast<const char*>(vgb_sys),
                                          vgb_sys_size))
            {
                vgb_log::Error("could not stage the driver image");
                nal::Unload();
                return 1;
            }
        }

        mapper::MAP_RESULT result = { 0 };
        if (!mapper::Map(imagePath, clientPid, &result))
        {
            vgb_log::Error("mapping failed");
            _wremove(imagePath.c_str());
            nal::Unload();
            return 1;
        }

        _wremove(imagePath.c_str());

        MAP_STATE state = { result.imageBase, result.imageSize, result.driverObject, result.entryPoint };
        SaveMapState(state);

        if (!OpenDevice())
        {
            vgb_log::Error("driver mapped but the device did not appear");
            return 1;
        }

        ULONG version = 0;
        if (!Ioctl(IOCTL_VGB_GET_VERSION, nullptr, 0, &version, sizeof(version), nullptr) ||
            version != VGB_PROTOCOL_VERSION)
        {
            vgb_log::Error("mapped image does not answer with protocol %u", VGB_PROTOCOL_VERSION);
            return 1;
        }

        vgb_log::Good("driver loaded, protocol %u", version);

        if (clientPid)
            vgb_log::Good("client pid %u registered", clientPid);

        return 0;
    }

    int CmdUnload()
    {
        if (!Elevated())
        {
            vgb_log::Error("not elevated");
            return 1;
        }

        CloseDevice();

        MAP_STATE state = { 0 };
        if (LoadMapState(&state))
        {
            mapper::MAP_RESULT result = { state.imageBase, state.imageSize, state.driverObject,
                                          state.entryPoint, STATUS_SUCCESS };
            mapper::Unmap(result);
            ClearMapState();
            vgb_log::Good("driver unmapped");
        }
        else
        {
            vgb_log::Warn("no mapping state found - skipping unmap");
        }

        nal::SetStatePath(nt::GetFullTempPath() + L"\\vgb-nal.state");
        nal::Unload();
        vgb_log::Good("mapping host released");

        return 0;
    }

    int CmdPid(const wchar_t* name)
    {
        VGB_NAME_REQ request = { 0 };
        VGB_PID_REQ reply = { 0 };

        request.Pid = 0;
        wcsncpy_s(request.Name, VGB_MAX_NAME_CHARS, name, VGB_MAX_NAME_CHARS - 1);

        if (!Ioctl(IOCTL_VGB_GET_PID_BY_NAME, &request, sizeof(request), &reply, sizeof(reply), nullptr))
        {
            vgb_log::Error("pid lookup failed (0x%08X)", GetLastError());
            return 1;
        }

        if (!reply.Pid)
        {
            vgb_log::Error("no process named %ls", name);
            return 1;
        }

        printf("%u\n", reply.Pid);
        return 0;
    }

    int CmdMainBase(uint32_t pid)
    {
        VGB_PID_REQ request = { pid, 0 };
        uint64_t base = 0;

        if (!Ioctl(IOCTL_VGB_GET_MAIN_BASE, &request, sizeof(request), &base, sizeof(base), nullptr))
        {
            vgb_log::Error("main base failed (0x%08X)", GetLastError());
            return 1;
        }

        printf("0x%llX\n", (unsigned long long)base);
        return 0;
    }

    int CmdBase(uint32_t pid, const wchar_t* module)
    {
        VGB_NAME_REQ request = { 0 };
        uint64_t base = 0;

        request.Pid = pid;
        wcsncpy_s(request.Name, VGB_MAX_NAME_CHARS, module, VGB_MAX_NAME_CHARS - 1);

        if (!Ioctl(IOCTL_VGB_GET_MODULE_BASE, &request, sizeof(request), &base, sizeof(base), nullptr))
        {
            vgb_log::Error("module lookup failed (0x%08X)", GetLastError());
            return 1;
        }

        printf("0x%llX\n", (unsigned long long)base);
        return 0;
    }

    int CmdModules(uint32_t pid)
    {
        const DWORD capacity = 256 * 1024;
        std::vector<uint8_t> buffer(capacity);
        VGB_PID_REQ request = { pid, 0 };
        DWORD returned = 0;

        if (!Ioctl(IOCTL_VGB_ENUM_MODULES, &request, sizeof(request), buffer.data(), capacity, &returned))
        {
            vgb_log::Error("module enumeration failed (0x%08X)", GetLastError());
            return 1;
        }

        auto list = reinterpret_cast<VGB_MODULE_LIST*>(buffer.data());

        for (ULONG i = 0; i < list->Count; ++i)
            printf("0x%016llX  %8u  %ls\n",
                   (unsigned long long)list->Entries[i].Base,
                   list->Entries[i].SizeOfImage,
                   list->Entries[i].Name);

        if (list->Truncated)
            vgb_log::Warn("list truncated at %u entries", list->Count);

        return 0;
    }

    int CmdRead(uint32_t pid, uint64_t address, uint32_t size)
    {
        if (!size)
        {
            vgb_log::Error("size must be non-zero");
            return 1;
        }

        std::vector<uint8_t> buffer(size);
        size_t done = 0;

        while (done < size)
        {
            const size_t remaining = size - done;
            const uint32_t chunk = (uint32_t)(remaining < VGB_MAX_XFER ? remaining : VGB_MAX_XFER);
            VGB_MEM_REQ request = { pid, chunk, address + done };
            DWORD returned = 0;

            if (!Ioctl(IOCTL_VGB_READ, &request, sizeof(request), buffer.data() + done,
                       chunk, &returned))
            {
                vgb_log::Error("read failed at 0x%llX (0x%08X)",
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
            vgb_log::Error("could not parse the hex payload");
            return 1;
        }

        size_t done = 0;

        while (done < bytes.size())
        {
            const size_t remaining = bytes.size() - done;
            const uint32_t chunk = (uint32_t)(remaining < VGB_MAX_XFER ? remaining : VGB_MAX_XFER);
            std::vector<uint8_t> packet(sizeof(VGB_MEM_REQ) + chunk);

            auto request = reinterpret_cast<VGB_MEM_REQ*>(packet.data());
            request->Pid = pid;
            request->Size = chunk;
            request->Address = address + done;
            memcpy(packet.data() + sizeof(VGB_MEM_REQ), bytes.data() + done, chunk);

            if (!Ioctl(IOCTL_VGB_WRITE, packet.data(), (DWORD)packet.size(), nullptr, 0, nullptr))
            {
                vgb_log::Error("write failed at 0x%llX (0x%08X)",
                           (unsigned long long)(address + done), GetLastError());
                return 1;
            }

            done += chunk;
        }

        vgb_log::Good("wrote %zu bytes at 0x%llX", done, (unsigned long long)address);
        return 0;
    }

    int CmdAttach(uint32_t pid)
    {
        VGB_PID_REQ request = { pid, 0 };

        if (!Ioctl(IOCTL_VGB_SET_CLIENT_PID, &request, sizeof(request), nullptr, 0, nullptr))
        {
            vgb_log::Error("attach failed (0x%08X)", GetLastError());
            return 1;
        }

        vgb_log::Good("client pid %u registered for handle restoration", pid);
        return 0;
    }

    int CmdOverlay(uint32_t pid)
    {
        if (overlay::IsRunning())
        {
            overlay::Stop();
            vgb_log::Good("overlay stopped");
            return 0;
        }

        if (!OpenDevice())
        {
            vgb_log::Error("driver is not loaded - load it first");
            return 1;
        }

        if (!pid)
        {
            vgb_log::Error("usage: overlay <pid>");
            return 1;
        }

        if (!overlay::Start(pid))
            return 1;

        vgb_log::Good("overlay running over pid %u - F9 stops it", pid);
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
        VGB_NAME_REQ request = { 0 };
        VGB_PID_REQ reply = { 0 };

        wcsncpy_s(request.Name, VGB_MAX_NAME_CHARS, name, VGB_MAX_NAME_CHARS - 1);

        if (!Ioctl(IOCTL_VGB_GET_PID_BY_NAME, &request, sizeof(request),
                   &reply, sizeof(reply), nullptr))
            return 0;

        return reply.Pid;
    }

    uint64_t MainBaseOf(uint32_t pid)
    {
        VGB_PID_REQ request = { pid, 0 };
        uint64_t base = 0;

        if (!Ioctl(IOCTL_VGB_GET_MAIN_BASE, &request, sizeof(request), &base, sizeof(base), nullptr))
            return 0;

        return base;
    }

    uint64_t ModuleBaseOf(uint32_t pid, const wchar_t* name)
    {
        VGB_NAME_REQ request = { 0 };
        uint64_t base = 0;

        request.Pid = pid;
        wcsncpy_s(request.Name, VGB_MAX_NAME_CHARS, name, VGB_MAX_NAME_CHARS - 1);

        if (!Ioctl(IOCTL_VGB_GET_MODULE_BASE, &request, sizeof(request), &base, sizeof(base), nullptr))
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
        printf("  VanguardBypass\n");
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
            vgb_log::Error("game not running - the process only exists during a match");
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
            vgb_log::Error("elevation failed or was declined (0x%08X)", GetLastError());
            vgb_log::Info("right-click the exe and choose Run as administrator");
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
                vgb_log::Step("waiting for the game%s", i ? "" : " - enter a match");

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
        printf("\n  VanguardBypass\n");
        printf("  ==============================================\n\n");

        if (OpenDevice())
        {
            vgb_log::Good("driver already loaded");
        }
        else
        {
            vgb_log::Step("loading the driver");

            if (CmdLoad(GetCurrentProcessId()) != 0)
            {
                vgb_log::Error("driver load failed - see above");
                Pause();
                return 1;
            }
        }

        uint32_t pid = LookupPid(L"League of Legends.exe");

        if (!pid)
        {
            if (LaunchClient())
                vgb_log::Info("starting the Riot client - log in and enter a match");
            else
                vgb_log::Info("open League of Legends and enter a match");

            pid = WaitForGame(300);
        }

        if (!pid)
        {
            vgb_log::Error("no game process found - is League of Legends installed?");
            Pause();
            return 1;
        }

        g_sessionPid = pid;
        vgb_log::Good("game pid %u", pid);

        /* Lets this process use plain ReadProcessMemory too. */
        CmdAttach(GetCurrentProcessId());

        if (!overlay::Start(pid))
        {
            vgb_log::Error("the overlay did not start");
            Pause();
            return 1;
        }

        vgb_log::Good("overlay running");
        vgb_log::Info("F9 stops it. Close this window when done.");
        vgb_log::Info("nothing is drawn until you are in a match.");

        /* Keep the process - and with it the render thread - alive. */
        while (overlay::IsRunning())
            Sleep(200);

        vgb_log::Info("overlay stopped");
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
            vgb_log::Good("image base 0x%llX", (unsigned long long)base);
        else
            vgb_log::Warn("could not resolve the image base");
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

        wchar_t name[VGB_MAX_NAME_CHARS] = { 0 };

        printf("  module name: ");
        if (!ReadLine(name, VGB_MAX_NAME_CHARS) || !name[0])
            return;

        const uint64_t base = ModuleBaseOf(pid, name);

        if (!base)
        {
            vgb_log::Error("module not found: %ls", name);
            return;
        }

        struct { uint32_t Pid; uint32_t Pad; uint64_t Base; } request = { pid, 0, base };
        const ULONG code = hide ? IOCTL_VGB_HIDE_MODULE : IOCTL_VGB_UNHIDE_MODULE;

        if (!Ioctl(code, &request, sizeof(request), nullptr, 0, nullptr))
        {
            vgb_log::Error("%s failed (0x%08X)", hide ? "hide" : "unhide", GetLastError());
            return;
        }

        vgb_log::Good("%s: %ls (0x%llX)", hide ? "hidden" : "unhidden", name,
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
            printf("  o) toggle overlay\n");
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
                overlay::Stop();
                CmdUnload();
                g_sessionPid = 0;
                break;
            case L'o':
            case L'O':
                UiOverlay();
                break;
            case L'0': return 0;
            default: vgb_log::Warn("unknown choice"); break;
            }

            Pause();
        }

        return 0;
    }
}

int wmain(int argc, wchar_t** argv)
{
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

        for (int i = 2; i + 1 < argc; ++i)
        {
            if (std::wstring(argv[i]) == L"--client")
                clientPid = (uint32_t)_wtoi(argv[i + 1]);
        }

        result = CmdLoad(clientPid);
    }
    else if (verb == L"unload")
    {
        result = CmdUnload();
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
    else if (verb == L"overlay" && argc >= 3)
    {
        result = CmdOverlay((uint32_t)_wtoi(argv[2]));

        /* The render thread dies with this process, so the CLI form has to stay
         * resident. F9 (handled inside the overlay) ends it. */
        while (result == 0 && overlay::IsRunning())
            Sleep(100);
    }
    else
    {
        vgb_log::Error("unknown verb or missing arguments");
    }

    CloseDevice();
    return result;
}
