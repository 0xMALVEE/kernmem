#include "Utils.h"
#include "Offsets.h"
#include "SDK.h"
#include <d3d11.h>

namespace Utils {
    int ScreenWidth = 1920;
    int ScreenHeight = 1080;

    void Initialize(int width, int height) {
        ScreenWidth = width;
        ScreenHeight = height;
    }

    float GetGameTime() {
        return (float)GetTickCount64() / 1000.0f;
    }

    struct Matrix4x4 { float m[16]; };

    Matrix4x4 Multiply(Matrix4x4 A, Matrix4x4 B) {
        Matrix4x4 R;
        for (int i = 0; i < 4; i++)
            for (int j = 0; j < 4; j++)
                R.m[i * 4 + j] = A.m[i * 4] * B.m[j] + A.m[i * 4 + 1] * B.m[4 + j] + A.m[i * 4 + 2] * B.m[8 + j] + A.m[i * 4 + 3] * B.m[12 + j];
        return R;
    }

    bool WorldToScreen(const Vector3& pos, Vector2& out) {
        uintptr_t base = (uintptr_t)GetModuleHandle(NULL);
        if (!IsValidPtr((void*)(base + Offset::oViewMatrixPtr), 8)) return false;

        Matrix4x4 view = SafeReadSDK<Matrix4x4>(base + Offset::oViewMatrixPtr, {});
        Matrix4x4 proj = SafeReadSDK<Matrix4x4>(base + Offset::oViewMatrixPtr + 0x40, {});

        if (view.m[15] == 0 && view.m[0] == 0) return false;

        Matrix4x4 vp = Multiply(view, proj);
        float cX = pos.x * vp.m[0] + pos.y * vp.m[4] + pos.z * vp.m[8] + vp.m[12];
        float cY = pos.x * vp.m[1] + pos.y * vp.m[5] + pos.z * vp.m[9] + vp.m[13];
        float cW = pos.x * vp.m[3] + pos.y * vp.m[7] + pos.z * vp.m[11] + vp.m[15];

        if (cW < 0.1f) return false;

        float M = 1.0f / cW;
        float ndc_x = cX * M; float ndc_y = cY * M;
        out.x = (float)((ScreenWidth / 2 * ndc_x) + (ndc_x + ScreenWidth / 2));
        out.y = (float)(-(ScreenHeight / 2 * ndc_y) + (ndc_y + ScreenHeight / 2));
        return true;
    }
}