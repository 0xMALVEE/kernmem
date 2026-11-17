/*
 * provider.h - Apex Legends implementation of the esp::Provider interface.
 *
 * This is all a game has to supply: where the process and module are, and how
 * to read players out of it. The overlay window, drawing, attach retry, frame
 * loop and driver handshake come from sdk/client.
 *
 * Nothing here touches ReadProcessMemory - every read goes through km::Client to
 * the kernel driver, so handle stripping never applies.
 */
#pragma once

#include <string>
#include <utility>
#include <vector>

#include "../../sdk/client/esp.h"

namespace apex
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

        /* Apex stores the matrix row major with the w row at m[12..15]. */
        bool RowMajorMatrix() const override { return true; }

        /* Shield and knocked state go in the label; the name is already drawn by
         * the framework. */
        std::string Label(const esp::Player& player) const override;

        /* Walk counters for the status line. */
        std::string Status() const override;

    private:
        /* Entity behind a handle. Apex has no chunk table: the handle's low 16
         * bits index the flat entity array at EntityStride. */
        uint64_t EntityAt(km::Client& client, uint32_t handle);

        /* Local entity, resolved from the pointer global first and the handle
         * global second. */
        uint64_t LocalEntity(km::Client& client);

        std::string ReadName(km::Client& client, uint64_t entity);

        /* Per-frame extras keyed by player id, so Label stays cheap. */
        mutable std::vector<std::pair<uint64_t, std::string>> extras_;

        /* Per-frame walk counters, so a failure names its own step instead of
         * just showing "0 tracked". */
        mutable uint32_t seenSlots_ = 0;
        mutable uint32_t seenEntities_ = 0;
        mutable uint32_t seenDead_ = 0;
        mutable uint32_t seenNoOrigin_ = 0;
        mutable uint32_t seenLocalRoute1_ = 0;
        mutable uint32_t seenLocalRoute2_ = 0;
        mutable uint32_t statusSignon_ = 0;

        uint32_t pid_ = 0;
        uint64_t base_ = 0;
        uint64_t list_ = 0;
    };
}
