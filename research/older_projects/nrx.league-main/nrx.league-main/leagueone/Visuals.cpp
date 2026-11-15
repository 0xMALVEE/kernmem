#include "Visuals.h"
#include "SDK.h"
#include "Settings.h"
#include "Offsets.h"

namespace Visuals {

    bool SafeReadPtr(uintptr_t addr, uintptr_t* out) {
        if (addr < 0x10000 || addr > 0x7FFFFFFFFFFF) return false;
        __try {
            *out = *(uintptr_t*)addr;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    void Update() {
        if (!Settings::ZoomHackEnabled) return;

        static uintptr_t base = (uintptr_t)GetModuleHandle(NULL);
        uintptr_t hud = 0;
        if (!SafeReadPtr(base + Offset::oHudInstance, &hud) || !hud) return;

        // Path A: The 0x1E8 Path (Classic)
        uintptr_t camA = 0;
        if (SafeReadPtr(hud + 0x1E8, &camA) && IsValidPtr((void*)camA)) {
            // Write to Max Zoom (0x28)
            *(float*)(camA + 0x28) = Settings::MaxZoomValue;
        }

        // Path B: The 0x18 Path (Modern x64)
        // Your scan found survivors at 0x320 and 0x324 here.
        uintptr_t camB = 0;
        if (SafeReadPtr(hud + 0x18, &camB) && IsValidPtr((void*)camB)) {
            // Usually 0x320 is MaxZoom and 0x324 is a secondary constraint
            *(float*)(camB + 0x320) = Settings::MaxZoomValue;
            *(float*)(camB + 0x324) = Settings::MaxZoomValue;

            // --- THE TRICK: Sync Current Zoom ---
            // Often, current zoom is at 0x318 or 0x31C. 
            // If we write to it, the camera will jump to the new distance immediately.
            float currentZoom = *(float*)(camB + 0x31C);
            if (currentZoom > 0 && currentZoom < Settings::MaxZoomValue) {
                // Uncomment the line below if you want the camera to "auto-zoom" 
                // when you enable the hack:
                // *(float*)(camB + 0x31C) = Settings::MaxZoomValue - 1.0f;
            }
        }
    }
}