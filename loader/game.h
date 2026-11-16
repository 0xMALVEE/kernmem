/*
 * game.h - launches and supervises the per-game client.
 *
 * The loader no longer draws anything. The ESP lives in the per-game client
 * (league/client), a separate executable started beside the loader, so the
 * loader stays game-agnostic: it maps the driver and owns the device, and
 * anything game-specific is in a client.
 */
#pragma once

#include <windows.h>

namespace game
{
    /* Starts the client next to this executable. False when it is missing or
     * could not be started. */
    bool Start();

    /* Terminates the client and reaps it. */
    void Stop();

    bool IsRunning();
}
