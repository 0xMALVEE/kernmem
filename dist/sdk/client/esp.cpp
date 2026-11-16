#include "esp.h"

#include <windows.h>

#include <cmath>
#include <cstdio>
#include <string>

#include <shellapi.h>

#pragma comment(lib, "shell32.lib")

#include "log.h"
#include "overlay.h"

namespace
{
    constexpr int kAttachRetries = 40;
    constexpr int kFrameDelay = 8;

    volatile LONG g_running = 1;

    BOOL WINAPI OnConsoleSignal(DWORD signal)
    {
        if (signal == CTRL_C_EVENT || signal == CTRL_BREAK_EVENT || signal == CTRL_CLOSE_EVENT)
            InterlockedExchange(&g_running, 0);

        return TRUE;
    }

    /* F9 is the stop key. */
    bool StopPressed()
    {
        static bool down = false;
        const bool pressed = (GetAsyncKeyState(VK_F9) & 0x8000) != 0;
        const bool edge = pressed && !down;

        down = pressed;
        return edge;
    }

    /* The device can be missing on the first frames while the loader is still
     * mapping. Retry rather than die. */
    bool OpenWithRetry(km::Client& client)
    {
        for (int attempt = 0; attempt < kAttachRetries; ++attempt)
        {
            if (!InterlockedCompareExchange(&g_running, 1, 1))
                return false;

            if (client.Open())
                return true;

            Sleep(250);
        }

        return false;
    }

    /*
     * Resolves the loader beside this executable and runs it elevated. The client
     * is the only thing the user starts; the loader is an implementation detail
     * and its console is minimised rather than something to keep open.
     */
    bool SpawnLoader()
    {
        wchar_t path[MAX_PATH + 1] = { 0 };

        const DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH + 1);

        if (!length || length >= MAX_PATH + 1)
            return false;

        std::wstring full(path);
        const std::wstring::size_type slash = full.find_last_of(L"\\/");

        if (slash == std::wstring::npos)
            return false;

        full = full.substr(0, slash + 1) + L"kernmem-loader.exe";

        if (GetFileAttributesW(full.c_str()) == INVALID_FILE_ATTRIBUTES)
        {
            km_log::Error("loader missing: %ls", full.c_str());
            return false;
        }

        km_log::Info("starting the loader - accept the elevation prompt");

        const INT_PTR result = (INT_PTR)ShellExecuteW(nullptr, L"runas", full.c_str(),
                                                     L"load", nullptr, SW_SHOWMINIMIZED);

        /* ShellExecute returns > 32 on success; a small value is the error code,
         * including the user cancelling the UAC prompt. */
        if (result <= 32)
        {
            km_log::Error("could not start the loader (code %lld) - run it as admin",
                          (long long)result);
            return false;
        }

        return true;
    }

    /*
     * Open the device, spawning the loader if it is not up yet. Mapping takes a
     * few seconds - drop, service, NAL, image - so the device is polled for a
     * while rather than given one shot.
     */
    bool EnsureDriver(km::Client& client)
    {
        /*
         * Short probe first. A loader already running answers immediately; one that
         * is not up should not cost the full retry budget before we try to start
         * it, or a first run sits silent for ten seconds.
         */
        for (int attempt = 0; attempt < 8; ++attempt)
        {
            if (client.Open())
                return true;

            Sleep(250);
        }

        if (!SpawnLoader())
            return false;

        for (int attempt = 0; attempt < 120; ++attempt)
        {
            if (!InterlockedCompareExchange(&g_running, 1, 1))
                return false;

            if (client.Open())
                return true;

            Sleep(250);
        }

        km_log::Error("the loader did not bring the driver up in time");
        return false;
    }
}

namespace esp
{
    bool Project(const Provider& provider, const Matrix& matrix, const Vec3& world,
                 int width, int height, float* x, float* y)
    {
        const float* m = matrix.m;
        float clipX;
        float clipY;
        float clipW;

        if (provider.RowMajorMatrix())
        {
            /* w row at m[12..15]: the vector is dotted against the rows. */
            clipX = world.x * m[0] + world.y * m[1] + world.z * m[2] + m[3];
            clipY = world.x * m[4] + world.y * m[5] + world.z * m[6] + m[7];
            clipW = world.x * m[12] + world.y * m[13] + world.z * m[14] + m[15];
        }
        else
        {
            /* Transposed: w column at m[3], m[7], m[11], m[15]. */
            clipX = world.x * m[0] + world.y * m[4] + world.z * m[8] + m[12];
            clipY = world.x * m[1] + world.y * m[5] + world.z * m[9] + m[13];
            clipW = world.x * m[3] + world.y * m[7] + world.z * m[11] + m[15];
        }

        if (clipW < 0.01f)
            return false;

        *x = (width * 0.5f) + (clipX / clipW) * (width * 0.5f);
        *y = (height * 0.5f) - (clipY / clipW) * (height * 0.5f);

        return true;
    }

    bool Provider::ProjectPoint(const Matrix& matrix, const Vec3& world, int width,
                                int height, float* x, float* y) const
    {
        return Project(*this, matrix, world, width, height, x, y);
    }

    void Run(Provider& provider)
    {
        SetConsoleCtrlHandler(OnConsoleSignal, TRUE);

        km::Client client;

        km_log::Info("client: attaching to driver");

        if (!EnsureDriver(client))
        {
            km_log::Error("no driver and the loader could not start it");
            return;
        }

        km_log::Good("driver connected");

        if (!client.Version())
        {
            km_log::Error("driver answered with no protocol version");
            return;
        }

        client.SetClientPid(GetCurrentProcessId());

        if (!overlay::Start())
        {
            km_log::Error("could not create the overlay window");
            return;
        }

        bool attached = false;
        int frames = 0;

        /* A single bad frame is normal (respawn, menu, map load). Only re-attach
         * after a sustained run of failures. */
        int misses = 0;
        constexpr int kMaxMisses = 20;

        const wchar_t* title = provider.WindowTitle();
        const wchar_t* process = provider.ProcessName();

        char waiting[128];
        _snprintf_s(waiting, sizeof(waiting), _TRUNCATE, "waiting for %ls", process);

        while (InterlockedCompareExchange(&g_running, 1, 1))
        {
            if (StopPressed())
                break;

            if (!attached)
            {
                if (!provider.Attach(client))
                {
                    overlay::Draw(provider, {}, {}, {}, waiting, title);
                    Sleep(250);
                    continue;
                }

                attached = true;
                km_log::Good("attached to pid %u at 0x%llX", provider.Pid(),
                             (unsigned long long)provider.Base());
            }

            Matrix matrix = {};
            Local local = {};

            const bool haveMatrix = provider.ReadMatrix(client, &matrix);
            const bool haveLocal = haveMatrix && provider.ReadLocal(client, &local);

            if (!haveLocal)
            {
                /* Name the failing read: the two have different causes, and the
                 * shared "game lost" text could not tell them apart. */
                if (++misses < kMaxMisses)
                {
                    const char* why = haveMatrix ? "local player unresolved"
                                                 : "view matrix unresolved";

                    char unresolved[224];
                    const std::string detail = provider.Status();

                    _snprintf_s(unresolved, sizeof(unresolved), _TRUNCATE, "%s %s",
                                why, detail.c_str());

                    overlay::Draw(provider, {}, {}, {}, unresolved, title);
                    Sleep(kFrameDelay);
                    continue;
                }

                /* Sustained: the game is gone or the offsets are dead, so
                 * re-attach rather than spin on stale addresses. */
                misses = 0;
                attached = false;
                overlay::Draw(provider, {}, {}, {}, "game lost - re-attaching", title);
                Sleep(250);
                continue;
            }

            misses = 0;

            if (!local.valid)
            {
                /* The counters are what make this diagnosable - without them the
                 * line only says the local player did not resolve. */
                char noLocal[224];
                const std::string detail = provider.Status();

                _snprintf_s(noLocal, sizeof(noLocal), _TRUNCATE, "no local player %s",
                            detail.c_str());

                overlay::Draw(provider, local, matrix, {}, noLocal, title);
                Sleep(kFrameDelay);
                continue;
            }

            const std::vector<Player> players = provider.Collect(client);

            char status[192];
            const std::string extra = provider.Status();

            _snprintf_s(status, sizeof(status), _TRUNCATE, "%s hp %d, %zu tracked %s",
                        provider.TeamName(local.team), local.health, players.size(),
                        extra.c_str());

            overlay::Draw(provider, local, matrix, players, status, title);

            if (++frames % 120 == 0)
                km_log::Step("%d frames, %zu players", frames, players.size());

            Sleep(kFrameDelay);
        }

        overlay::Stop();
        client.Close();

        km_log::Info("stopped after %d frames", frames);
    }
}
