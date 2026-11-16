/*
 * overlay.h - click-through overlay window and ESP drawing.
 *
 * Layered, topmost, transparent, covers the game window rect and never takes
 * input, so the game keeps focus. Everything is drawn into a memory DC and
 * blitted in one BitBlt - no swapchain, no flicker.
 *
 * The window follows the title the provider reports, so this file knows nothing
 * about any particular game.
 */
#pragma once

#include <windows.h>

#include <vector>

#include "esp.h"

namespace overlay
{
    /* Creates the window. */
    bool Start();

    /* Destroys it. */
    void Stop();

    /* Draws one frame. `title` is the game window title to track. */
    void Draw(const esp::Provider& provider, const esp::Local& local,
              const esp::Matrix& matrix, const std::vector<esp::Player>& players,
              const char* status, const wchar_t* title);
}
