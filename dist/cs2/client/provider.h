/*
 * provider.h - CS2 implementation of the esp::Provider interface.
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
#include <vector>

#include "../../sdk/client/esp.h"

namespace cs2
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

        /* CS2 stores the matrix row major with the w row at m[12..15]. */
        bool RowMajorMatrix() const override { return true; }

        /* Player name already goes in the label, so nothing extra. */
        std::string Label(const esp::Player& player) const override;

        /* Walk counters for the status line. */
        std::string Status() const override;

    private:
        /* Entity resolution and field readers. Chunked list, scene node, name. */
        /* Controller at a list index. Controllers are read straight off the list
         * entry with a fixed stride - they do not go through the chunk table. */
        uint64_t ListEntry(km::Client& client, uint32_t index);

        /* Entity behind a handle. Only handles use the chunk table. */
        uint64_t EntityAt(km::Client& client, uint32_t handle);

        /* Scene node of a pawn. Only used for the dormant flag now - position
         * comes off the pawn via m_vOldOrigin. */
        uint64_t SceneNode(km::Client& client, uint64_t pawn);

        std::string ReadName(km::Client& client, uint64_t controller);

        /* Per-frame walk counters, so a failure names its own step instead of
         * just showing "0 tracked". */
        mutable uint32_t seenControllers_ = 0;
        mutable uint32_t seenPawns_ = 0;
        mutable uint32_t seenDormant_ = 0;
        mutable uint32_t seenDead_ = 0;
        mutable uint32_t seenNoNode_ = 0;
        mutable uint32_t seenNoOrigin_ = 0;

        /* Last local position, for the status line. */
        mutable float statusX_ = 0.0f;
        mutable float statusY_ = 0.0f;
        mutable float statusZ_ = 0.0f;

        uint32_t pid_ = 0;
        uint64_t base_ = 0;
        uint64_t list_ = 0;

        /* entity_list + 0x10, resolved once per attach: the base of the controller
         * array. */
        uint64_t listEntry_ = 0;
    };
}
