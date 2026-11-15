#include "overlay.h"

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <vector>

#include "log.h"
#include "vgb_shared_loader.h"

namespace
{
    /*
     * Offsets for client build 22.8910, every one recovered from the live process
     * and confirmed against real data: the object array yields the local hero plus
     * a second named hero, positions are stable world coordinates across reads,
     * and the health pair sits 0x40 into the stat block.
     *
     * The team field is a small enum (1 = own side, 2 = enemy), not the 100/200 the
     * older table used - checking for those dropped every single hero.
     *
     * scripts/offsets.py carries the same values; update both in the same change.
     *
     * The view matrix is NOT recovered yet, so HAVE_VIEW_MATRIX is false and the
     * overlay falls back to the radar. Flip it once the matrix is found.
     */
    constexpr bool     HAVE_VIEW_MATRIX   = false;

    constexpr uint64_t OFF_LOCAL_PLAYER   = 0x1CBB810;
    constexpr uint64_t OFF_OBJECT_MANAGER = 0x1CBB7F8;
    constexpr uint64_t OFF_OBJ_BEGIN      = 0x0;
    constexpr uint64_t OFF_OBJ_END        = 0x8;
    constexpr uint64_t OFF_OBJ_STRIDE     = 0x8;
    constexpr uint64_t OFF_VIEW_MATRIX    = 0x0;

    constexpr uint64_t OFF_HERO_TEAM    = 0x128;
    constexpr uint64_t OFF_HERO_NAME    = 0x3F00;

    /*
     * The stat block is not at one distance for every entry: some list entries are
     * handed to us 0x100 above their own base, so the same field is reachable at
     * two distances. Try the near one, then the far one, and validate the result -
     * a denormal triple is the signature of a miss.
     */
    constexpr uint64_t OFF_HERO_POS_NEAR = 0x4584;
    constexpr uint64_t OFF_HERO_POS_FAR  = 0x4484;

    /* Health and max health live 0x40 and 0x44 into the stat block, i.e. relative
     * to whichever position offset resolved. */
    constexpr uint64_t OFF_HERO_HP_FROM_POS    = 0x40;
    constexpr uint64_t OFF_HERO_MAXHP_FROM_POS = 0x44;

    /* Own attack range, read from the local hero. */
    constexpr uint64_t OFF_HERO_ATTACK_RANGE = 0x30B0;

    constexpr int32_t TEAM_OWN   = 1;
    constexpr int32_t TEAM_ENEMY = 2;

    constexpr uint32_t MAX_OBJECTS  = 0x400;
    constexpr float    ATTACK_RANGE = 700.0f;
    constexpr float    HERO_HEIGHT  = 110.0f;

    HANDLE   g_device = INVALID_HANDLE_VALUE;
    uint32_t g_pid = 0;
    uint64_t g_base = 0;
    HANDLE   g_thread = NULL;
    DWORD    g_threadId = 0;
    HWND     g_hwnd = NULL;
    volatile LONG g_running = 0;

    int     g_width = 0;
    int     g_height = 0;
    HDC     g_memDc = NULL;
    HBITMAP g_dib = NULL;
    HGDIOBJ g_oldBitmap = NULL;
    void*   g_bits = NULL;

    struct Vec3 { float x, y, z; };

    struct Hero
    {
        uint64_t address;
        int32_t  team;
        float    health;
        float    maxHealth;
        Vec3     position;
        wchar_t  name[48];
    };

    /* ---- device ------------------------------------------------------- */

    bool Read(uint64_t address, void* buffer, uint32_t size)
    {
        if (g_device == INVALID_HANDLE_VALUE || !address || !size)
            return false;

        struct { uint32_t pid; uint32_t size; uint64_t address; } request = { g_pid, size, address };
        DWORD returned = 0;

        return DeviceIoControl(g_device, IOCTL_VGB_READ, &request, sizeof(request),
                               buffer, size, &returned, nullptr) != FALSE;
    }

    bool ReadU64(uint64_t address, uint64_t* out) { return Read(address, out, 8); }
    bool ReadI32(uint64_t address, int32_t* out) { return Read(address, out, 4); }
    bool ReadF32(uint64_t address, float* out) { return Read(address, out, 4); }

    bool ReadName(uint64_t address, wchar_t* out, size_t chars)
    {
        if (!Read(address, out, (uint32_t)(chars * sizeof(wchar_t))))
            return false;

        out[chars - 1] = 0;
        return true;
    }

    /* ---- projection --------------------------------------------------- */

    bool ReadMatrix(float matrix[16])
    {
        uint64_t instance = 0;

        if (!HAVE_VIEW_MATRIX)
            return false;

        if (!ReadU64(g_base + OFF_VIEW_MATRIX, &instance) || !instance)
            return false;

        if (!Read(instance, matrix, sizeof(float) * 16))
            return false;

        for (int i = 0; i < 16; ++i)
            if (!std::isfinite(matrix[i]))
                return false;

        return true;
    }

    bool Project(const float matrix[16], const Vec3& world, float* sx, float* sy)
    {
        const float clipX = world.x * matrix[0] + world.y * matrix[4] + world.z * matrix[8] + matrix[12];
        const float clipY = world.x * matrix[1] + world.y * matrix[5] + world.z * matrix[9] + matrix[13];
        const float clipW = world.x * matrix[3] + world.y * matrix[7] + world.z * matrix[11] + matrix[15];

        if (clipW <= 0.001f)
            return false;

        *sx = (g_width * 0.5f) * (1.0f + clipX / clipW);
        *sy = (g_height * 0.5f) * (1.0f - clipY / clipW);

        return std::isfinite(*sx) && std::isfinite(*sy);
    }

    /* ---- drawing ------------------------------------------------------ */

    void Line(HDC dc, int x1, int y1, int x2, int y2, COLORREF colour, int width)
    {
        HPEN pen = CreatePen(PS_SOLID, width, colour);
        HGDIOBJ old = SelectObject(dc, pen);

        MoveToEx(dc, x1, y1, nullptr);
        LineTo(dc, x2, y2);

        SelectObject(dc, old);
        DeleteObject(pen);
    }

    void Box(HDC dc, int x, int y, int w, int h, COLORREF colour, int thickness)
    {
        HPEN pen = CreatePen(PS_SOLID, thickness, colour);
        HGDIOBJ oldPen = SelectObject(dc, pen);
        HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));

        Rectangle(dc, x, y, x + w, y + h);

        SelectObject(dc, oldBrush);
        SelectObject(dc, oldPen);
        DeleteObject(pen);
    }

    void Circle(HDC dc, int cx, int cy, int radius, COLORREF colour, int thickness)
    {
        HPEN pen = CreatePen(PS_SOLID, thickness, colour);
        HGDIOBJ oldPen = SelectObject(dc, pen);
        HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));

        Ellipse(dc, cx - radius, cy - radius, cx + radius, cy + radius);

        SelectObject(dc, oldBrush);
        SelectObject(dc, oldPen);
        DeleteObject(pen);
    }

    void Filled(HDC dc, int x, int y, int w, int h, COLORREF colour)
    {
        RECT rect = { x, y, x + w, y + h };
        HBRUSH brush = CreateSolidBrush(colour);

        FillRect(dc, &rect, brush);
        DeleteObject(brush);
    }

    void Text(HDC dc, int x, int y, COLORREF colour, const wchar_t* format, ...)
    {
        wchar_t buffer[512];
        va_list args;

        va_start(args, format);
        _vsnwprintf_s(buffer, _countof(buffer), _TRUNCATE, format, args);
        va_end(args);

        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, colour);
        TextOutW(dc, x, y, buffer, (int)wcslen(buffer));
    }

    /* ---- scene -------------------------------------------------------- */

    /*
     * Reads a hero's world position, trying both stat-block distances. Returns the
     * distance that worked so the caller can find the health pair from it.
     */
    bool ReadHeroPosition(uint64_t object, Vec3* out, uint64_t* posOffset)
    {
        const uint64_t candidates[] = { OFF_HERO_POS_NEAR, OFF_HERO_POS_FAR };

        for (uint64_t offset : candidates)
        {
            float x = 0.0f;
            float y = 0.0f;
            float z = 0.0f;

            if (!ReadF32(object + offset, &x) ||
                !ReadF32(object + offset + 4, &y) ||
                !ReadF32(object + offset + 8, &z))
                continue;

            if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z))
                continue;

            /* A miss reads as a denormal or as zero; a real hero is never at the
             * origin of the map, so both cases are rejected. */
            if (fabsf(x) < 1.0f || fabsf(y) < 1.0f)
                continue;

            if (fabsf(x) > 100000.0f || fabsf(y) > 100000.0f || fabsf(z) > 10000.0f)
                continue;

            out->x = x;
            out->y = y;
            out->z = z;

            if (posOffset)
                *posOffset = offset;

            return true;
        }

        return false;
    }

    bool CollectHeroes(uint64_t localAddress, std::vector<Hero>* out)
    {
        uint64_t manager = 0;
        if (!ReadU64(g_base + OFF_OBJECT_MANAGER, &manager) || !manager)
            return false;

        uint64_t begin = 0;
        uint64_t end = 0;

        if (!ReadU64(manager + OFF_OBJ_BEGIN, &begin) || !ReadU64(manager + OFF_OBJ_END, &end))
            return false;

        if (!begin || !end || end <= begin)
            return false;

        const uint64_t count = (end - begin) / OFF_OBJ_STRIDE;

        for (uint64_t i = 0; i < count && i < MAX_OBJECTS; ++i)
        {
            uint64_t object = 0;
            if (!ReadU64(begin + i * OFF_OBJ_STRIDE, &object) || !object)
                continue;

            if (object == localAddress)
                continue;

            Hero hero = {};

            if (!ReadI32(object + OFF_HERO_TEAM, &hero.team))
                continue;

            if (hero.team != TEAM_OWN && hero.team != TEAM_ENEMY)
                continue;

            uint64_t posOffset = 0;

            if (!ReadHeroPosition(object, &hero.position, &posOffset))
                continue;

            ReadF32(object + posOffset + OFF_HERO_HP_FROM_POS, &hero.health);
            ReadF32(object + posOffset + OFF_HERO_MAXHP_FROM_POS, &hero.maxHealth);
            ReadName(object + OFF_HERO_NAME, hero.name, _countof(hero.name));

            /* The visible/dead flags are not identified on this build, so they stay
             * false and the draw path filters on health alone. */
            hero.address = object;

            out->push_back(hero);
        }

        return true;
    }

    /*
     * Matrix-free fallback. Without a view matrix there is still something honest
     * to show: a top-down radar around the local hero. It needs no projection at
     * all, only world positions, so it works on the offsets we actually have.
     */
    void DrawRadar(HDC dc, const Vec3& localPos, int32_t localTeam, float attackRange,
                   const std::vector<Hero>& heroes)
    {
        const int size = 220;
        const int x = 12;
        const int y = g_height - size - 12;

        if (y < 90)
            return;

        Filled(dc, x, y, size, size, RGB(16, 16, 16));
        Box(dc, x, y, size, size, RGB(70, 70, 70), 1);

        const float scale = 1.0f / 20.0f;   /* 1 px per 20 world units */
        const float half = (float)size * 0.5f;

        Filled(dc, x + (int)half - 2, y + (int)half - 2, 5, 5, RGB(120, 255, 120));

        /* Own attack range. The ring belongs to the projection path, but that path
         * needs a view matrix we do not have - so draw it here, in radar space,
         * where a world radius is just range * scale. */
        const int ringRadius = (int)(attackRange * scale);

        if (ringRadius > 2 && ringRadius < size / 2)
            Circle(dc, x + (int)half, y + (int)half, ringRadius, RGB(90, 200, 255), 1);

        for (const Hero& hero : heroes)
        {
            const float dx = (hero.position.x - localPos.x) * scale;
            const float dy = (hero.position.y - localPos.y) * scale;

            const int px = x + (int)(half + dx);
            const int py = y + (int)(half - dy);

            if (px < x + 2 || px > x + size - 5 || py < y + 2 || py > y + size - 5)
                continue;

            const bool ally = localTeam != 0 && hero.team == localTeam;
            Filled(dc, px, py, 5, 5, ally ? RGB(110, 190, 255) : RGB(255, 90, 90));
        }

        Text(dc, x + 6, y + 5, RGB(150, 150, 150), L"radar  %zu   range %.0f",
             heroes.size(), attackRange);
    }

    void DrawFrame(HDC dc, float fps)
    {
        float matrix[16];
        const bool hasMatrix = ReadMatrix(matrix);

        uint64_t local = 0;
        ReadU64(g_base + OFF_LOCAL_PLAYER, &local);

        Vec3 localPos = { 0, 0, 0 };
        int32_t localTeam = 0;
        float localHp = 0.0f;
        float localMaxHp = 0.0f;
        bool hasLocal = false;

        float attackRange = ATTACK_RANGE;

        if (local)
        {
            uint64_t posOffset = 0;
            hasLocal = ReadHeroPosition(local, &localPos, &posOffset);

            if (hasLocal)
            {
                ReadF32(local + posOffset + OFF_HERO_HP_FROM_POS, &localHp);
                ReadF32(local + posOffset + OFF_HERO_MAXHP_FROM_POS, &localMaxHp);
            }

            ReadI32(local + OFF_HERO_TEAM, &localTeam);

            /* The real range is on the hero; the constant is only a fallback for
             * when the read misses. */
            float measured = 0.0f;

            if (ReadF32(local + OFF_HERO_ATTACK_RANGE, &measured) &&
                std::isfinite(measured) && measured > 100.0f && measured < 1500.0f)
                attackRange = measured;
        }

        std::vector<Hero> heroes;
        CollectHeroes(local, &heroes);

        if (!hasMatrix && hasLocal)
            DrawRadar(dc, localPos, localTeam, attackRange, heroes);

        if (hasLocal && hasMatrix)
        {
            /* own attack range ring */
            const int segments = 64;
            float prevX = 0.0f;
            float prevY = 0.0f;
            bool prevValid = false;

            for (int i = 0; i <= segments; ++i)
            {
                const float angle = (float)i / (float)segments * 6.2831853f;
                Vec3 point = { localPos.x + cosf(angle) * ATTACK_RANGE,
                               localPos.y + sinf(angle) * ATTACK_RANGE,
                               localPos.z };

                float sx = 0.0f;
                float sy = 0.0f;
                const bool valid = Project(matrix, point, &sx, &sy);

                if (valid && prevValid)
                    Line(dc, (int)prevX, (int)prevY, (int)sx, (int)sy, RGB(90, 200, 255), 1);

                prevValid = valid;
                prevX = sx;
                prevY = sy;
            }

            /* own box */
            float feetX = 0.0f, feetY = 0.0f, headX = 0.0f, headY = 0.0f;
            Vec3 head = { localPos.x, localPos.y, localPos.z + HERO_HEIGHT };

            if (Project(matrix, localPos, &feetX, &feetY) && Project(matrix, head, &headX, &headY))
            {
                const int height = (int)fabsf(feetY - headY);
                const int width = (int)(height * 0.55f);
                const int left = (int)feetX - width / 2;
                const int top = (int)headY;

                Box(dc, left, top, width, height, RGB(120, 255, 120), 2);
                Text(dc, left, top - 16, RGB(120, 255, 120), L"YOU  %.0f/%.0f", localHp, localMaxHp);
            }
        }

        for (const Hero& hero : heroes)
        {
            /* Visible/dead flags are not verified on this build, so filter on
             * health only - a stale flag would otherwise hide every hero. */
            if (hero.health < 0.0f)
                continue;

            if (!hasMatrix)
                continue;

            float feetX = 0.0f, feetY = 0.0f, headX = 0.0f, headY = 0.0f;
            Vec3 head = { hero.position.x, hero.position.y, hero.position.z + HERO_HEIGHT };

            if (!Project(matrix, hero.position, &feetX, &feetY))
                continue;

            if (!Project(matrix, head, &headX, &headY))
                continue;

            if (feetX < -200.0f || feetX > g_width + 200.0f ||
                feetY < -200.0f || feetY > g_height + 200.0f)
                continue;

            const bool ally = localTeam != 0 && hero.team == localTeam;
            const COLORREF colour = ally ? RGB(110, 190, 255) : RGB(255, 90, 90);

            const int height = (int)fabsf(feetY - headY);
            const int width = (int)(height * 0.55f);
            const int left = (int)feetX - width / 2;
            const int top = (int)headY;

            Box(dc, left, top, width, height, colour, 1);

            if (hero.maxHealth > 0.0f)
            {
                float ratio = hero.health / hero.maxHealth;
                ratio = ratio < 0.0f ? 0.0f : (ratio > 1.0f ? 1.0f : ratio);

                Filled(dc, left, top - 8, width, 4, RGB(20, 20, 20));
                Filled(dc, left, top - 8, (int)(width * ratio), 4,
                       ally ? RGB(90, 170, 255) : RGB(255, 60, 60));
            }

            const float dx = hero.position.x - localPos.x;
            const float dy = hero.position.y - localPos.y;
            const float distance = sqrtf(dx * dx + dy * dy);

            Text(dc, left, top - 26, colour, L"%ls %.0f", hero.name, distance);

            if (hasLocal)
            {
                float selfX = 0.0f;
                float selfY = 0.0f;

                if (Project(matrix, localPos, &selfX, &selfY))
                    Line(dc, (int)selfX, (int)selfY, (int)feetX, (int)feetY, colour, 1);
            }
        }

        Text(dc, 12, 10, RGB(235, 235, 235), L"VGB overlay   F9 stop");
        Text(dc, 12, 28, RGB(190, 190, 190), L"pid %u   base %llX   fps %.0f",
             g_pid, (unsigned long long)g_base, fps);
        Text(dc, 12, 46, RGB(190, 190, 190), L"heroes %zu   matrix %s   self %s",
             heroes.size(), hasMatrix ? L"ok" : L"MISSING", hasLocal ? L"ok" : L"null");

        if (!hasLocal)
        {
            Text(dc, 12, 72, RGB(255, 210, 90),
                 L"no data yet - be in a match; offsets are build specific");
        }
        else if (!hasMatrix)
        {
            Text(dc, 12, 72, RGB(255, 210, 90),
                 L"radar mode - view matrix not recovered yet, boxes disabled");
        }
    }

    /* ---- window ------------------------------------------------------- */

    LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
    {
        if (message == WM_HOTKEY && wParam == 1)
        {
            InterlockedExchange(&g_running, 0);
            PostThreadMessageW(g_threadId, WM_QUIT, 0, 0);
            return 0;
        }

        return DefWindowProcW(hwnd, message, wParam, lParam);
    }

    struct FindContext
    {
        uint32_t pid;
        HWND     result;
    };

    BOOL CALLBACK FindWindowProc(HWND hwnd, LPARAM param)
    {
        auto context = reinterpret_cast<FindContext*>(param);
        DWORD pid = 0;

        GetWindowThreadProcessId(hwnd, &pid);

        if (pid != context->pid || !IsWindowVisible(hwnd))
            return TRUE;

        RECT rect = { 0 };
        if (!GetClientRect(hwnd, &rect) || rect.right < 100 || rect.bottom < 100)
            return TRUE;

        context->result = hwnd;
        return FALSE;
    }

    /*
     * Re-resolves the target pid and image base.
     *
     * The game relaunches for every match, so a pid captured once goes stale - and
     * the window does not exist yet during the loading screen, which is exactly
     * when the loader reaches this point. Both are handled by re-resolving here
     * instead of failing.
     */
    bool RefreshTarget()
    {
        struct { uint32_t pid; uint32_t pad; wchar_t name[VGB_MAX_NAME_CHARS]; } request = { 0 };
        struct { uint32_t pid; uint32_t pad; } reply = { 0 };
        DWORD returned = 0;

        wcsncpy_s(request.name, VGB_MAX_NAME_CHARS, L"League of Legends.exe",
                  VGB_MAX_NAME_CHARS - 1);

        if (!DeviceIoControl(g_device, IOCTL_VGB_GET_PID_BY_NAME, &request, sizeof(request),
                             &reply, sizeof(reply), &returned, nullptr) || !reply.pid)
            return false;

        uint64_t base = 0;
        struct { uint32_t pid; uint32_t pad; } baseRequest = { reply.pid, 0 };

        if (!DeviceIoControl(g_device, IOCTL_VGB_GET_MAIN_BASE, &baseRequest, sizeof(baseRequest),
                             &base, sizeof(base), &returned, nullptr) || !base)
            return false;

        g_pid = reply.pid;
        g_base = base;
        return true;
    }

    DWORD WINAPI ThreadProc(void*)
    {
        g_threadId = GetCurrentThreadId();

        /* The overlay window is created before the game window is known, so the
         * hotkey and the message pump are alive while waiting. */
        WNDCLASSEXW windowClass = { 0 };
        windowClass.cbSize = sizeof(windowClass);
        windowClass.lpfnWndProc = WndProc;
        windowClass.hInstance = GetModuleHandleW(nullptr);
        windowClass.lpszClassName = L"VgbOverlay";
        RegisterClassExW(&windowClass);

        g_hwnd = CreateWindowExW(
            WS_EX_TOPMOST | WS_EX_TRANSPARENT | WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
            windowClass.lpszClassName, L"VGB overlay", WS_POPUP,
            0, 0, 100, 100, nullptr, nullptr, windowClass.hInstance, nullptr);

        if (!g_hwnd)
        {
            vgb_log::Error("overlay: window creation failed");
            InterlockedExchange(&g_running, 0);
            return 0;
        }

        RegisterHotKey(g_hwnd, 1, 0, VK_F9);
        ShowWindow(g_hwnd, SW_SHOWNOACTIVATE);

        HDC screen = GetDC(nullptr);
        g_memDc = CreateCompatibleDC(screen);

        ULONGLONG last = GetTickCount64();
        ULONGLONG lastSearch = 0;
        int frames = 0;
        int searches = 0;
        float fps = 0.0f;
        bool running = true;
        HWND game = nullptr;

        vgb_log::Info("overlay: waiting for the game window");

        while (running && InterlockedCompareExchange(&g_running, 1, 1))
        {
            MSG message;

            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
            {
                if (message.message == WM_QUIT)
                {
                    running = false;
                    break;
                }

                TranslateMessage(&message);
                DispatchMessageW(&message);
            }

            if (!running)
                break;

            /* (Re)acquire the game window: absent during loading, and it can be
             * destroyed between matches. Search once a second and stay alive
             * indefinitely rather than exiting. */
            if (!game || !IsWindow(game))
            {
                game = nullptr;

                const ULONGLONG now = GetTickCount64();

                if (now - lastSearch >= 1000)
                {
                    lastSearch = now;
                    RefreshTarget();

                    FindContext context = { g_pid, nullptr };
                    EnumWindows(FindWindowProc, reinterpret_cast<LPARAM>(&context));
                    game = context.result;

                    if (game)
                    {
                        vgb_log::Good("overlay: game window found");
                    }
                    else if (++searches % 10 == 0)
                    {
                        vgb_log::Step("overlay: still waiting for the game window");
                    }
                }

                if (!game)
                {
                    Sleep(200);
                    continue;
                }
            }

            RECT rect = { 0 };
            if (!GetClientRect(game, &rect))
            {
                game = nullptr;
                continue;
            }

            POINT topLeft = { 0, 0 };
            ClientToScreen(game, &topLeft);

            const int width = rect.right - rect.left;
            const int height = rect.bottom - rect.top;

            if (width < 64 || height < 64)
            {
                Sleep(100);
                continue;
            }

            if (width != g_width || height != g_height || !g_dib)
            {
                if (g_dib)
                {
                    SelectObject(g_memDc, g_oldBitmap);
                    DeleteObject(g_dib);
                    g_dib = NULL;
                    g_bits = nullptr;
                }

                BITMAPINFO info = { 0 };
                info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                info.bmiHeader.biWidth = width;
                info.bmiHeader.biHeight = -height;
                info.bmiHeader.biPlanes = 1;
                info.bmiHeader.biBitCount = 32;
                info.bmiHeader.biCompression = BI_RGB;

                void* bits = nullptr;
                g_dib = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &bits, nullptr, 0);

                if (!g_dib || !bits)
                {
                    vgb_log::Error("overlay: DIB creation failed");
                    break;
                }

                g_bits = bits;
                g_oldBitmap = SelectObject(g_memDc, g_dib);
                g_width = width;
                g_height = height;
            }

            SetWindowPos(g_hwnd, HWND_TOPMOST, topLeft.x, topLeft.y, width, height, SWP_NOACTIVATE);

            memset(g_bits, 0, (size_t)g_width * (size_t)g_height * 4u);
            DrawFrame(g_memDc, fps);

            /* GDI leaves alpha at 0; mark every drawn pixel opaque so the
             * layered window blends instead of showing black. */
            auto pixels = static_cast<uint32_t*>(g_bits);
            const size_t total = (size_t)g_width * (size_t)g_height;

            for (size_t i = 0; i < total; ++i)
                if (pixels[i] & 0x00FFFFFFu)
                    pixels[i] |= 0xFF000000u;

            POINT source = { 0, 0 };
            SIZE size = { g_width, g_height };
            BLENDFUNCTION blend = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };

            UpdateLayeredWindow(g_hwnd, screen, &topLeft, &size, g_memDc, &source, 0, &blend, ULW_ALPHA);

            ++frames;
            const ULONGLONG now = GetTickCount64();

            if (now - last >= 500)
            {
                fps = frames * 1000.0f / (float)(now - last);
                frames = 0;
                last = now;
            }

            Sleep(33);
        }

        UnregisterHotKey(g_hwnd, 1);

        if (g_dib)
        {
            SelectObject(g_memDc, g_oldBitmap);
            DeleteObject(g_dib);
            g_dib = NULL;
            g_bits = nullptr;
        }

        if (g_memDc)
        {
            DeleteDC(g_memDc);
            g_memDc = NULL;
        }

        ReleaseDC(nullptr, screen);
        DestroyWindow(g_hwnd);
        g_hwnd = NULL;

        g_width = 0;
        g_height = 0;
        InterlockedExchange(&g_running, 0);

        vgb_log::Info("overlay stopped");
        return 0;
    }
}

namespace overlay
{
    bool IsRunning()
    {
        return InterlockedCompareExchange(&g_running, 1, 1) != 0;
    }

    bool Start(uint32_t pid)
    {
        if (IsRunning())
            return true;

        if (!pid)
            return false;

        /* The DOS symlink can be gone while the device object is still live, so
         * fall back to the object path before giving up. */
        static const wchar_t* const paths[] =
        {
            VGB_WIN32_NAME,
            L"\\\\?\\GLOBALROOT\\Device\\VanguardBypass",
        };

        for (const wchar_t* path : paths)
        {
            g_device = CreateFileW(path, GENERIC_READ | GENERIC_WRITE,
                                   FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                   OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);

            if (g_device != INVALID_HANDLE_VALUE)
                break;
        }

        if (g_device == INVALID_HANDLE_VALUE)
        {
            vgb_log::Error("overlay: device missing - load the driver first");
            return false;
        }

        uint64_t base = 0;
        struct { uint32_t pid; uint32_t pad; } request = { pid, 0 };
        DWORD returned = 0;

        if (!DeviceIoControl(g_device, IOCTL_VGB_GET_MAIN_BASE, &request, sizeof(request),
                             &base, sizeof(base), &returned, nullptr) || !base)
        {
            vgb_log::Error("overlay: could not resolve the image base for pid %u", pid);
            CloseHandle(g_device);
            g_device = INVALID_HANDLE_VALUE;
            return false;
        }

        g_pid = pid;
        g_base = base;
        InterlockedExchange(&g_running, 1);

        g_thread = CreateThread(nullptr, 0, ThreadProc, nullptr, 0, nullptr);

        if (!g_thread)
        {
            InterlockedExchange(&g_running, 0);
            CloseHandle(g_device);
            g_device = INVALID_HANDLE_VALUE;
            return false;
        }

        return true;
    }

    void Stop()
    {
        if (InterlockedExchange(&g_running, 0) == 0 && !g_thread)
            return;

        if (g_threadId)
            PostThreadMessageW(g_threadId, WM_QUIT, 0, 0);

        if (g_thread)
        {
            WaitForSingleObject(g_thread, 3000);
            CloseHandle(g_thread);
            g_thread = NULL;
        }

        /* A stale thread id would make the next Start post WM_QUIT into an
         * unrelated thread, so it is cleared with the rest of the state. */
        g_threadId = 0;
        g_hwnd = NULL;
        g_pid = 0;
        g_base = 0;

        if (g_device != INVALID_HANDLE_VALUE)
        {
            CloseHandle(g_device);
            g_device = INVALID_HANDLE_VALUE;
        }
    }

    bool Toggle(uint32_t pid)
    {
        if (IsRunning())
        {
            Stop();
            return false;
        }

        return Start(pid);
    }
}
