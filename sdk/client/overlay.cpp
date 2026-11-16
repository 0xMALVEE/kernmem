#include "overlay.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

#include <dwmapi.h>

#pragma comment(lib, "dwmapi.lib")

namespace
{
    constexpr int kMaxDistance = 4000;

    HWND      g_window = nullptr;
    HDC       g_memDc = nullptr;
    HBITMAP   g_bitmap = nullptr;
    HGDIOBJ   g_oldBitmap = nullptr;
    int       g_width = 0;
    int       g_height = 0;

    /* Per process, because two clients can be up at once and a fixed class name
     * makes the second RegisterClassExW fail outright. */
    wchar_t   g_className[64] = { 0 };

    void Pen(HDC dc, COLORREF colour)
    {
        static HPEN pen = nullptr;

        if (pen)
            DeleteObject(pen);

        pen = CreatePen(PS_SOLID, 1, colour);
        SelectObject(dc, pen);
    }

    void Text(HDC dc, int x, int y, COLORREF colour, const char* text)
    {
        SetTextColor(dc, colour);
        SetBkMode(dc, TRANSPARENT);
        TextOutA(dc, x, y, text, (int)strlen(text));
    }

    /*
     * Box corners from a feet and a head screen point, with a health bar down the
     * left edge. Width is a fraction of the height so the box keeps its shape at
     * range instead of collapsing to a line.
     */
    void Box(HDC dc, float feetX, float feetY, float headX, float headY,
             COLORREF colour, int health)
    {
        const float height = feetY - headY;

        if (height < 2.0f)
            return;

        /* height / 2.4, as the reference sizes it - a fraction of the height keeps
         * the box in proportion at range instead of collapsing to a line. */
        const float width = height / 2.4f;
        const int left = (int)(headX - width * 0.5f);
        const int right = (int)(headX + width * 0.5f);
        const int top = (int)headY;
        const int bottom = (int)feetY;

        Pen(dc, colour);
        SelectObject(dc, GetStockObject(NULL_BRUSH));
        Rectangle(dc, left, top, right, bottom);

        /* Health bar: green through red, filled from the bottom up. */
        const int barHeight = bottom - top;
        const int filled = barHeight * (health > 100 ? 100 : (health < 0 ? 0 : health)) / 100;

        const COLORREF barColour = health > 60 ? RGB(0, 220, 0)
                                : health > 30 ? RGB(230, 200, 0)
                                              : RGB(220, 0, 0);

        HBRUSH brush = CreateSolidBrush(barColour);
        RECT fill = { left - 5, bottom - filled, left - 2, bottom };

        FillRect(dc, &fill, brush);
        DeleteObject(brush);
    }

    struct WindowSearch
    {
        DWORD pid;
        HWND  found;
    };

    BOOL CALLBACK FindByPid(HWND window, LPARAM param)
    {
        WindowSearch* search = (WindowSearch*)param;
        DWORD owner = 0;

        GetWindowThreadProcessId(window, &owner);

        if (owner != search->pid)
            return TRUE;

        if (!IsWindowVisible(window) || GetWindow(window, GW_OWNER))
            return TRUE;

        RECT rect = { 0 };

        if (!GetClientRect(window, &rect) || rect.right < 16 || rect.bottom < 16)
            return TRUE;

        search->found = window;
        return FALSE;
    }

    /*
     * The game window, by process id first and title second. A title alone breaks
     * on a renamed window, a launcher splash or a title the game rewrites, and
     * matching the pid we already resolved is exact.
     */
    HWND GameWindow(DWORD pid, const wchar_t* title)
    {
        if (pid)
        {
            WindowSearch search = { pid, nullptr };

            EnumWindows(FindByPid, (LPARAM)&search);

            if (search.found)
                return search.found;
        }

        return title ? FindWindowW(nullptr, title) : nullptr;
    }

    LRESULT CALLBACK Procedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
    {
        if (message == WM_DESTROY)
        {
            PostQuitMessage(0);
            return 0;
        }

        return DefWindowProcW(window, message, wParam, lParam);
    }
}

namespace overlay
{
    bool Start()
    {
        WNDCLASSEXW windowClass = { 0 };

        windowClass.cbSize = sizeof(windowClass);
        windowClass.lpfnWndProc = Procedure;
        windowClass.hInstance = GetModuleHandleW(nullptr);
        windowClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
        _snwprintf_s(g_className, _countof(g_className), _TRUNCATE, L"kernmem overlay %u",
                     GetCurrentProcessId());

        windowClass.lpszClassName = g_className;

        if (!RegisterClassExW(&windowClass))
            return false;

        /*
         * Full screen, not fitted to the game window. The overlay then never moves,
         * so nothing has to be tracked, and it covers the game at any position.
         */
        const int screenWidth = GetSystemMetrics(SM_CXSCREEN);
        const int screenHeight = GetSystemMetrics(SM_CYSCREEN);

        g_window = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST |
                                       WS_EX_TOOLWINDOW,
                                   g_className, g_className, WS_POPUP | WS_VISIBLE,
                                   0, 0, screenWidth, screenHeight, nullptr, nullptr,
                                   windowClass.hInstance, nullptr);

        if (!g_window)
            return false;

        /*
         * LWA_ALPHA plus a DWM frame extended over the whole client area is the
         * composition path: DWM blends the window, so pure black is genuinely
         * transparent. LWA_COLORKEY on a GDI blit is exact-black fragile - one
         * off-black pixel keeps a solid box.
         */
        SetLayeredWindowAttributes(g_window, RGB(0, 0, 0), 255, LWA_ALPHA);

        MARGINS margins = { -1, -1, -1, -1 };
        DwmExtendFrameIntoClientArea(g_window, &margins);

        return true;
    }

    void Stop()
    {
        if (g_window)
            DestroyWindow(g_window);

        g_window = nullptr;

        /* Class names are process unique now, so unregister or a restart of the
         * same pid leaks the class. */
        if (g_className[0])
        {
            UnregisterClassW(g_className, GetModuleHandleW(nullptr));
            g_className[0] = 0;
        }

        if (g_memDc)
        {
            SelectObject(g_memDc, g_oldBitmap);
            DeleteObject(g_bitmap);
            DeleteDC(g_memDc);

            g_memDc = nullptr;
            g_bitmap = nullptr;
            g_oldBitmap = nullptr;
            g_width = 0;
            g_height = 0;
        }
    }

    void Draw(const esp::Provider& provider, const esp::Local& local,
              const esp::Matrix& matrix, const std::vector<esp::Player>& players,
              const char* status, const wchar_t* title)
    {
        if (!g_window)
            return;

        const int width = GetSystemMetrics(SM_CXSCREEN);
        const int height = GetSystemMetrics(SM_CYSCREEN);

        if (width < 16 || height < 16)
            return;

        /*
         * The game window is only used to decide whether anything is worth
         * drawing. The overlay itself is full screen and never moves, and black is
         * genuinely transparent, so an idle overlay is invisible instead of a
         * black box - and it cannot be left showing over the desktop.
         */
        /*
         * Only draw while the game itself is the foreground window. Checking that
         * the window merely exists is not enough - the overlay then sits over the
         * browser, the desktop, anything.
         */
        HWND gameWindow = GameWindow(provider.Pid(), title);
        const bool inGame = gameWindow && GetForegroundWindow() == gameWindow;

        if (width != g_width || height != g_height)
        {
            if (g_memDc)
            {
                SelectObject(g_memDc, g_oldBitmap);
                DeleteObject(g_bitmap);
                DeleteDC(g_memDc);
            }

            HDC screen = GetDC(nullptr);
            g_memDc = CreateCompatibleDC(screen);
            g_bitmap = CreateCompatibleBitmap(screen, width, height);
            g_oldBitmap = SelectObject(g_memDc, g_bitmap);
            ReleaseDC(nullptr, screen);

            g_width = width;
            g_height = height;

            SetWindowPos(g_window, HWND_TOPMOST, 0, 0, width, height,
                         SWP_NOACTIVATE | SWP_SHOWWINDOW);
        }
        else
        {
            SetWindowPos(g_window, HWND_TOPMOST, 0, 0, width, height,
                         SWP_NOACTIVATE | SWP_NOSIZE);
        }

        /* Fixed and transparent where black, so it stays up while in game and is
         * taken down entirely when the game is gone - never left over the desktop. */
        ShowWindow(g_window, inGame ? SW_SHOWNOACTIVATE : SW_HIDE);

        /* Black is the colour key, so clearing to black erases the last frame. */
        HBRUSH black = CreateSolidBrush(RGB(0, 0, 0));
        RECT full = { 0, 0, width, height };
        FillRect(g_memDc, &full, black);
        DeleteObject(black);

        Text(g_memDc, 8, 8, RGB(0, 255, 120), status);

        if (local.valid)
        {
            int drawn = 0;

            for (const esp::Player& player : players)
            {
                float feetX = 0.0f, feetY = 0.0f, headX = 0.0f, headY = 0.0f;

                if (!provider.ProjectPoint(matrix, player.feet, width, height, &feetX, &feetY))
                    continue;
                if (!provider.ProjectPoint(matrix, player.head, width, height, &headX, &headY))
                    continue;

                if (feetX < 0 || feetX > width || feetY < 0 || feetY > height)
                    continue;

                const float distance = std::sqrt(
                    (player.feet.x - local.feet.x) * (player.feet.x - local.feet.x) +
                    (player.feet.y - local.feet.y) * (player.feet.y - local.feet.y));

                if (distance > kMaxDistance)
                    continue;

                const bool enemy = player.team != local.team;
                const COLORREF colour = enemy ? RGB(255, 60, 60) : RGB(60, 160, 255);

                Box(g_memDc, feetX, feetY, headX, headY, colour, player.health);

                const std::string extra = provider.Label(player);

                char label[160];
                _snprintf_s(label, sizeof(label), _TRUNCATE, "%s %dm %s%s",
                            player.name.c_str(), (int)distance,
                            player.spotted ? "" : "(hidden)", extra.c_str());

                Text(g_memDc, (int)headX - 30, (int)headY - 16, colour, label);
                ++drawn;
            }

            char summary[96];
            _snprintf_s(summary, sizeof(summary), _TRUNCATE, "%d drawn, hp %d",
                        drawn, local.health);
            Text(g_memDc, 8, 24, RGB(200, 200, 200), summary);

            /* Game-specific extras: League's radar, for one. */
            provider.DrawExtra(g_memDc, width, height, local, players);
        }

        /*
         * Blit into the WINDOW dc, not the screen dc. A layered window with a
         * colour key is composed by the window manager; writing straight to the
         * screen bypasses that and leaves every black key pixel opaque, which is
         * exactly an opaque black box over the game.
         */
        HDC windowDc = GetDC(g_window);

        if (windowDc)
        {
            BitBlt(windowDc, 0, 0, width, height, g_memDc, 0, 0, SRCCOPY);
            ReleaseDC(g_window, windowDc);
        }
    }
}
