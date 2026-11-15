/*
 * overlay.h - in-game overlay renderer.
 *
 * A click-through, topmost, per-pixel-alpha window laid over the game client.
 * Everything it draws comes through the driver device, so it works with
 * Vanguard's handle stripping in place and needs no injection.
 *
 * F9 stops it. Drawing is GDI into a 32-bit DIB, then UpdateLayeredWindow.
 */
#pragma once

#include <windows.h>
#include <cstdint>

namespace overlay
{
    /* Attaches to pid and starts the render thread. False when the device is
     * missing, the pid is dead, or the game window cannot be found. */
    bool Start(uint32_t pid);

    /* Stops the thread, destroys the window and releases the device. */
    void Stop();

    /* Start if stopped, stop if running. */
    bool Toggle(uint32_t pid);

    bool IsRunning();
}
