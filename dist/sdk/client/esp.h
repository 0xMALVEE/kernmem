/*
 * esp.h - game-agnostic external ESP framework.
 *
 * Everything that is the same for every game lives here: the shared types, the
 * provider interface a game implements, projection, and the frame loop. A game
 * folder supplies offsets and one Provider subclass; nothing else.
 *
 * A game never implements: the overlay window, the double buffer, the box and
 * health drawing, the attach retry, the re-attach on game loss, the stop key or
 * the driver handshake. Those are identical everywhere, and getting them right
 * once is the point.
 *
 * Nothing here calls ReadProcessMemory. Reads go through km::Client to the
 * kernel driver, so handle stripping never applies.
 */
#pragma once

#include <windows.h>

#include <cstdint>
#include <string>
#include <vector>

#include "../km-sdk.h"

namespace esp
{
    struct Vec3
    {
        float x;
        float y;
        float z;
    };

    /* 4x4 world-to-clip matrix. Row major unless the provider says otherwise. */
    struct Matrix
    {
        float m[16];
    };

    /* One tracked player. `id` is whatever the provider uses to tell players
     * apart - an entity address, a handle, an index. The framework only compares
     * it for equality.
     */
    struct Player
    {
        uint64_t    id;
        Vec3        feet;
        Vec3        head;
        int32_t     team;
        int32_t     health;
        bool        spotted;
        std::string name;
    };

    struct Local
    {
        uint64_t id;
        Vec3     feet;
        int32_t  team;
        int32_t  health;
        bool     valid;
    };

    /*
     * What a game has to provide. Every method returns false rather than
     * throwing, and a false from Attach/ReadMatrix/ReadLocal is treated as "not
     * ready yet" - the loop retries instead of dying.
     */
    class Provider
    {
    public:
        virtual ~Provider() = default;

        /* Image name as the kernel reports it, for PidByName. */
        virtual const wchar_t* ProcessName() const = 0;

        /* Module every offset is relative to. */
        virtual const wchar_t* ModuleName() const = 0;

        /* Game window title, so the overlay can follow it. */
        virtual const wchar_t* WindowTitle() const = 0;

        /* Resolves the process and module into the provider. False = not up. */
        virtual bool Attach(km::Client& client) = 0;

        /* False when the matrix is not populated (menu, loading). */
        virtual bool ReadMatrix(km::Client& client, Matrix* out) = 0;

        /* False when there is no local player. `valid` is the alive flag. */
        virtual bool ReadLocal(km::Client& client, Local* out) = 0;

        /* Every other live player. Own team included. */
        virtual std::vector<Player> Collect(km::Client& client) = 0;

        virtual const char* TeamName(int32_t team) const = 0;

        /* For the status line only. */
        virtual uint32_t Pid() const = 0;
        virtual uint64_t Base() const = 0;

        /* Matrix convention. True (the default) means row major with the w row at
         * m[12..15]; false means the transposed form with the w column at
         * m[3], m[7], m[11], m[15]. Getting this wrong mirrors and squashes
         * every box while still producing plausible numbers. */
        virtual bool RowMajorMatrix() const { return true; }

        /* Extra label per player, or "" for none. */
        virtual std::string Label(const Player&) const { return std::string(); }

        /* Extra status text appended to the frame line - walk counters and the
         * like. Empty by default. */
        virtual std::string Status() const { return std::string(); }

        /* Extra drawing, called with the frame DC after the boxes. The radar a
         * game falls back to when its camera is unresolved goes here. */
        virtual void DrawExtra(HDC, int, int, const Local&, const std::vector<Player>&) const {}

        /* World -> screen. Overridable because some games need a per-frame
         * correction on top of the raw matrix - League's projection has to be
         * centred on the local hero and normalised per axis before it is usable. */
        virtual bool ProjectPoint(const Matrix& matrix, const Vec3& world, int width,
                                  int height, float* x, float* y) const;
    };

    /* World position -> screen. False when the point is behind the camera. */
    bool Project(const Provider& provider, const Matrix& matrix, const Vec3& world,
                 int width, int height, float* x, float* y);

    /* Runs until the stop key or Ctrl+C. Owns the whole lifecycle. */
    void Run(Provider& provider);
}
