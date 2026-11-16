#include "game.h"

#include <string>

#include "log.h"

namespace
{
    PROCESS_INFORMATION g_process = { 0 };

    /*
     * The client ships beside the loader, so the path is derived from this
     * executable rather than from the working directory - the loader gets started
     * from shortcuts and shells whose cwd is unrelated.
     */
    bool ClientPath(std::wstring* out)
    {
        wchar_t path[MAX_PATH + 1] = { 0 };

        const DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH + 1);

        if (!length || length >= MAX_PATH + 1)
            return false;

        const std::wstring full(path);
        const std::wstring::size_type slash = full.find_last_of(L"\\/");

        if (slash == std::wstring::npos)
            return false;

        *out = full.substr(0, slash + 1) + L"kernmem-league.exe";
        return true;
    }
}

namespace game
{
    bool Start()
    {
        if (IsRunning())
            return true;

        std::wstring path;

        if (!ClientPath(&path))
        {
            km_log::Error("cannot resolve the client path");
            return false;
        }

        if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES)
        {
            km_log::Error("client missing: %ls", path.c_str());
            km_log::Info("build it with build.ps1 - it ships beside the loader");
            return false;
        }

        STARTUPINFOW startup = { 0 };
        startup.cb = sizeof(startup);

        ZeroMemory(&g_process, sizeof(g_process));

        if (!CreateProcessW(path.c_str(), nullptr, nullptr, nullptr, FALSE, 0,
                            nullptr, nullptr, &startup, &g_process))
        {
            km_log::Error("could not start the client (0x%08X)", GetLastError());
            return false;
        }

        km_log::Step("client started: %ls (pid %u)", path.c_str(), g_process.dwProcessId);
        return true;
    }

    void Stop()
    {
        if (!g_process.hProcess)
            return;

        if (WaitForSingleObject(g_process.hProcess, 0) == WAIT_TIMEOUT)
        {
            TerminateProcess(g_process.hProcess, 0);
            WaitForSingleObject(g_process.hProcess, 3000);
        }

        CloseHandle(g_process.hThread);
        CloseHandle(g_process.hProcess);

        g_process.hProcess = nullptr;
        g_process.hThread = nullptr;
    }

    bool IsRunning()
    {
        if (!g_process.hProcess)
            return false;

        return WaitForSingleObject(g_process.hProcess, 0) == WAIT_TIMEOUT;
    }
}
