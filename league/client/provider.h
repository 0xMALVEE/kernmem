/*
 * provider.h - League implementation of the esp::Provider interface.
 *
 * The overlay, drawing, frame loop and driver handshake come from sdk/client.
 * This file supplies only what is League-specific: the hero walk, the camera
 * correction, and the radar fallback for when the camera does not resolve.
 */
#pragma once

#include <string>
#include <vector>

#include "../../sdk/client/esp.h"

namespace league
{
    class Provider final : public esp::Provider
    {
    public:
        const wchar_t* ProcessName() const override;
        const wchar_t* ModuleName() const override;
        const wchar_t* WindowTitle() const override;

        bool Attach(km::Client& client) override;
        bool ReadMatrix(km::Client& client, esp::Matrix* out) override;
        bool ReadLocal(km::Client& client, esp::Local* out) override;

        std::vector<esp::Player> Collect(km::Client& client) override;

        const char* TeamName(int32_t team) const override;

        uint32_t Pid() const override;
        uint64_t Base() const override;

        /* League's matrix is the transposed form: the w column sits at
         * m[3], m[7], m[11], m[15]. */
        bool RowMajorMatrix() const override { return false; }

        std::string Label(const esp::Player&) const override;

        /* The projection has to be centred on the local hero and normalised per
         * axis before it is usable, so it is overridden. */
        bool ProjectPoint(const esp::Matrix& matrix, const esp::Vec3& world, int width,
                          int height, float* x, float* y) const override;

        /* Radar fallback, drawn instead of boxes while the camera is unresolved. */
        void DrawExtra(HDC dc, int width, int height, const esp::Local& local,
                       const std::vector<esp::Player>& players) const override;

    private:
        bool ReadPosition(km::Client& client, uint64_t hero, esp::Vec3* out) const;

        uint32_t pid_ = 0;
        uint64_t base_ = 0;

        /* Per-frame correction state, recomputed lazily after each matrix read. */
        mutable esp::Matrix corrMatrix_ = {};
        mutable esp::Vec3   corrLocal_ = {};
        mutable bool        haveMatrix_ = false;
        mutable bool        corrHasLocal_ = false;
        mutable bool        corrDone_ = false;
        mutable float       corrScaleX_ = 1.0f;
        mutable float       corrScaleY_ = 1.0f;
        mutable float       corrOffsetX_ = 0.0f;
        mutable float       corrOffsetY_ = 0.0f;

        void Correct(int width, int height) const;
    };
}
