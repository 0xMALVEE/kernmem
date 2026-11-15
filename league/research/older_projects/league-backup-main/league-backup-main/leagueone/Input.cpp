#include "Input.h"
#include "Settings.h"
#include "Utils.h"
#include <cmath>
#include <thread>

namespace Input {

    std::atomic<bool> g_IsFlicking = false;

    bool IsReady() { return !g_IsFlicking; }

    // =========================================================================
    // RAW KEY INPUTS 
    // =========================================================================

    void SendKey(int vk, bool down) {
        if (vk == 0) return;
        UINT scanCode = MapVirtualKey(vk, MAPVK_VK_TO_VSC);
        DWORD flags = KEYEVENTF_SCANCODE;
        if (!down) flags |= KEYEVENTF_KEYUP;
        keybd_event((BYTE)vk, (BYTE)scanCode, flags, 0);
    }

    void TapKey(int vk) {
        SendKey(vk, true);
        SendKey(vk, false);
    }

    // =========================================================================
    // HIGH-PRECISION DELAY
    // =========================================================================

    void PrecisionWait(double milliseconds) {
        LARGE_INTEGER freq, start, current;
        QueryPerformanceFrequency(&freq);
        QueryPerformanceCounter(&start);
        double elapsed = 0.0;
        while (elapsed < milliseconds) {
            QueryPerformanceCounter(&current);
            elapsed = (double)(current.QuadPart - start.QuadPart) * 1000.0 / freq.QuadPart;
        }
    }

    // =========================================================================
    // UNIFIED ASYNC FLICK ENGINE (Fixed Restore)
    // =========================================================================

    void FlickRightClick(const Vector2& screenPos) {
        if (g_IsFlicking) return;

        POINT currentPos;
        GetCursorPos(&currentPos);

        float dx = (float)currentPos.x - screenPos.x;
        float dy = (float)currentPos.y - screenPos.y;
        if (sqrt(dx * dx + dy * dy) < 5.0f) {
            mouse_event(MOUSEEVENTF_RIGHTDOWN, 0, 0, 0, 0);
            PrecisionWait(Settings::FlickPressDelay);
            mouse_event(MOUSEEVENTF_RIGHTUP, 0, 0, 0, 0);
            return;
        }

        g_IsFlicking = true;

        std::thread([screenPos, currentPos]() {
            // 1. Move to Target
            SetCursorPos((int)screenPos.x, (int)screenPos.y);
            PrecisionWait(Settings::FlickPreDelay);

            // 2. Click Down
            mouse_event(MOUSEEVENTF_RIGHTDOWN, 0, 0, 0, 0);
            PrecisionWait(Settings::FlickPressDelay);

            // 3. Click Up
            mouse_event(MOUSEEVENTF_RIGHTUP, 0, 0, 0, 0);
            PrecisionWait(Settings::FlickPostDelay);

            // 4. Force Absolute Restore (No delta math clipping issues)
            SetCursorPos(currentPos.x, currentPos.y);

            g_IsFlicking = false;
            }).detach();
    }

    void FlickKey(int vk, const Vector3& targetWorldPos) {
        if (g_IsFlicking) return;

        Vector2 screenPos;
        if (!Utils::WorldToScreen(targetWorldPos, screenPos)) return;

        POINT currentPos;
        GetCursorPos(&currentPos);

        g_IsFlicking = true;

        std::thread([vk, screenPos, currentPos]() {
            // 1. Move to Target
            SetCursorPos((int)screenPos.x, (int)screenPos.y);
            PrecisionWait(Settings::FlickPreDelay);

            // 2. Key Down
            UINT scanCode = MapVirtualKey(vk, MAPVK_VK_TO_VSC);
            keybd_event((BYTE)vk, (BYTE)scanCode, KEYEVENTF_SCANCODE, 0);
            PrecisionWait(Settings::FlickPressDelay);

            // 3. Key Up
            keybd_event((BYTE)vk, (BYTE)scanCode, KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP, 0);
            PrecisionWait(Settings::FlickPostDelay);

            // 4. Force Absolute Restore
            SetCursorPos(currentPos.x, currentPos.y);

            g_IsFlicking = false;
            }).detach();
    }
}