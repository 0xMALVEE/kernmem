#pragma once
#include <Windows.h>
#include <atomic>
#include "Vector.h"

namespace Input {
    extern std::atomic<bool> g_IsFlicking;

    // Returns true if the input thread is available
    bool IsReady();

    // Raw Key Inputs (Used by Orbwalker TCO)
    void SendKey(int vk, bool down);
    void TapKey(int vk);

    // Unified Micro-Thread Flicks
    void FlickRightClick(const Vector2& screenPos);
    void FlickKey(int vk, const Vector3& targetWorldPos);
}