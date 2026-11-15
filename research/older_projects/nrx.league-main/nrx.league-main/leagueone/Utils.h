#pragma once
#include <Windows.h>
#include "Vector.h"

namespace Utils {
    float GetGameTime();
    bool WorldToScreen(const Vector3& pos, Vector2& out);
    void Initialize(int width, int height);
    extern int ScreenWidth;
    extern int ScreenHeight;
}