#include "provider.h"

#include <cmath>
#include <cstddef>
#include <cstring>

#include "offsets.h"

namespace
{
    bool Plausible(uint64_t address)
    {
        return address >= 0x10000 && address < 0x7FFFFFFFFFFFULL;
    }
}

namespace cs2
{
    const wchar_t* Provider::ProcessName() const { return off::PROCESS_NAME; }
    const wchar_t* Provider::ModuleName() const { return off::MODULE_NAME; }
    const wchar_t* Provider::WindowTitle() const { return off::WINDOW_TITLE; }

    uint32_t Provider::Pid() const { return pid_; }
    uint64_t Provider::Base() const { return base_; }

    std::string Provider::Label(const esp::Player& player) const { return player.name; }

    /*
     * Walk counters. "0 tracked" alone cannot say which step failed, and every
     * step here fails for a different reason: a bad entity list, a bad pawn
     * handle, a bad scene node, or a dormant/spectator entry. The counters name
     * the step so the status line is a diagnosis instead of a symptom.
     */
    std::string Provider::Status() const
    {
        char text[128];

        _snprintf_s(text, sizeof(text), _TRUNCATE,
                    "c%u p%u dorm%u dead%u node%u org%u lf%u flag%u ctrl%X pawn%X pos %.0f,%.0f,%.0f",
                    seenControllers_, seenPawns_, seenDormant_, seenDead_, seenNoNode_,
                    seenNoOrigin_, seenLocalFallback_, seenLocalFlag_, statusCtrlRaw_,
                    statusLocalRaw_, statusX_, statusY_, statusZ_);

        return std::string(text);
    }

    const char* Provider::TeamName(int32_t team) const
    {
        if (team == off::TeamT)
            return "T";
        if (team == off::TeamCT)
            return "CT";
        return "?";
    }

    /*
     * Controller at a list index. Controllers sit in a flat array at
     * list_entry, stride 0x70, with index 0 being the list entry itself - hence
     * (index + 1). They do NOT go through the chunk table; only handles do.
     */
    uint64_t Provider::ListEntry(km::Client& client, uint32_t index)
    {
        uint64_t controller = 0;

        const uint64_t slot = listEntry_ + off::EntityStride * (uint64_t)(index + 1);

        if (!client.ReadValue(pid_, slot, &controller) || !Plausible(controller))
            return 0;

        return controller;
    }

    /*
     * Entity behind a handle. A handle is 32-bit with the entry index in the low
     * 15 bits; the chunk table is indexed by index >> 9 and the entry sits at a
     * fixed stride inside the chunk. Callers pass the raw handle - masking is
     * done here so no call site can forget it.
     */
    uint64_t Provider::EntityAt(km::Client& client, uint32_t handle)
    {
        const uint32_t index = handle & off::EntityHandleMask;

        uint64_t chunk = 0;
        uint64_t entity = 0;

        /* The chunk table base is the ADDRESS entity_list + 0x10, not the value
         * dereferenced from it - that value is the controller array. Using the
         * dereferenced value here puts every pawn lookup one hop out. */
        const uint64_t chunkSlot = list_ + off::EntityListArray +
                                   off::EntityChunkStride * (uint64_t)(index >> off::EntityChunkShift);

        if (!client.ReadValue(pid_, chunkSlot, &chunk) || !Plausible(chunk))
            return 0;

        const uint64_t slot = chunk + off::EntityStride * (index & (off::EntityPerChunk - 1));

        if (!client.ReadValue(pid_, slot, &entity) || !Plausible(entity))
            return 0;

        return entity;
    }

    /* Dereferences the scene node of a pawn. Everything positional hangs off it. */
    uint64_t Provider::SceneNode(km::Client& client, uint64_t pawn)
    {
        uint64_t node = 0;

        if (!client.ReadValue(pid_, pawn + off::GameSceneNode, &node) || !Plausible(node))
            return 0;

        return node;
    }

    /* The name is an inline char[128] on the controller - no pointer hop. */
    std::string Provider::ReadName(km::Client& client, uint64_t controller)
    {
        char buffer[32] = { 0 };

        if (!client.Read(pid_, controller + off::PawnName, buffer, sizeof(buffer) - 1))
            return std::string();

        buffer[sizeof(buffer) - 1] = 0;
        return std::string(buffer);
    }

    bool Provider::Attach(km::Client& client)
    {
        pid_ = 0;
        base_ = 0;
        list_ = 0;
        listEntry_ = 0;

        const uint32_t pid = client.PidByName(off::PROCESS_NAME);
        if (!pid)
            return false;

        const uint64_t base = client.ModuleBase(pid, off::MODULE_NAME);
        if (!base)
            return false;

        /* entity_list -> CGameEntitySystem. +0x10 is the controller array (and the
         * chunk table base). Both are resolved once here. */
        uint64_t system = 0;

        if (!client.ReadValue(pid, base + off::EntityList, &system) || !Plausible(system))
            return false;

        uint64_t entry = 0;

        if (!client.ReadValue(pid, system + off::EntityListArray, &entry) || !Plausible(entry))
            return false;

        pid_ = pid;
        base_ = base;
        list_ = system;
        listEntry_ = entry;

        return true;
    }

    bool Provider::ReadMatrix(km::Client& client, esp::Matrix* out)
    {
        if (!base_ || !out)
            return false;

        if (!client.Read(pid_, base_ + off::ViewMatrix, out->m, sizeof(out->m)))
            return false;

        /* An all-zero matrix is what the menu and the loading screen hold. */
        for (int i = 0; i < 16; ++i)
            if (out->m[i] != 0.0f)
                return true;

        return false;
    }

    /*
     * Walks the controllers for the one flagged local and returns its pawn. Costs a
     * controller walk, so it is only used when the global fails.
     */
    uint64_t Provider::LocalPawnFromControllers(km::Client& client)
    {
        seenControllers_ = 0;
        seenLocalFlag_ = 0;

        for (uint32_t index = 0; index < off::MaxControllers; ++index)
        {
            const uint64_t controller = ListEntry(client, index);

            if (!controller)
                continue;

            ++seenControllers_;

            uint8_t isLocal = 0;

            if (!client.ReadValue(pid_, controller + off::IsLocalController, &isLocal))
                continue;

            if (!isLocal)
                continue;

            ++seenLocalFlag_;

            uint32_t handle = 0;

            if (!client.ReadValue(pid_, controller + off::ControllerPawn, &handle) || !handle)
            {
                if (!client.ReadValue(pid_, controller + off::PlayerPawn, &handle) || !handle)
                    continue;
            }

            const uint64_t pawn = EntityAt(client, handle);

            if (pawn)
                return pawn;
        }

        return 0;
    }

    bool Provider::ReadLocal(km::Client& client, esp::Local* out)
    {
        out->valid = false;

        if (!base_ || !list_)
            return false;

        /*
         * dwLocalPlayerPawn is the pawn pointer itself - no entity-list hop and no
         * handle masking. When that global is stale the controllers are scanned for
         * the local flag instead, which is how the reference resolves it.
         */
        uint64_t pawn = 0;

        /* Route 1: the pawn pointer global. */
        if (!client.ReadValue(pid_, base_ + off::LocalPlayerPawn, &pawn) || !Plausible(pawn))
        {
            pawn = 0;

            /* Route 2: the controller HANDLE global, through the chunk table. */
            uint32_t controllerHandle = 0;

            if (client.ReadValue(pid_, base_ + off::LocalPlayerController, &controllerHandle) &&
                controllerHandle)
            {
                statusCtrlRaw_ = controllerHandle;

                const uint64_t controller = EntityAt(client, controllerHandle);

                if (controller)
                {
                    uint32_t handle = 0;

                    if (client.ReadValue(pid_, controller + off::ControllerPawn, &handle) && handle)
                        pawn = EntityAt(client, handle);
                }
            }

            /* Route 3: scan controllers for the local flag. */
            if (!pawn)
                pawn = LocalPawnFromControllers(client);

            if (!pawn)
                return false;

            ++seenLocalFallback_;
        }

        statusLocalRaw_ = (uint32_t)(pawn & 0xFFFFFFFF);

        struct
        {
            uint8_t team;
            uint8_t life;
            uint8_t pad[2];
            int32_t health;
            float   origin[3];
            float   view[3];
        } readout = {};

        const KM_BATCH_DESC entries[] =
        {
            { pawn + off::TeamNum, 1, offsetof(decltype(readout), team) },
            { pawn + off::LifeState, 1, offsetof(decltype(readout), life) },
            { pawn + off::Health, sizeof(int32_t), offsetof(decltype(readout), health) },
            { pawn + off::OldOrigin, sizeof(float) * 3, offsetof(decltype(readout), origin) },
            { pawn + off::ViewOffset, sizeof(float) * 3, offsetof(decltype(readout), view) },
        };

        if (!client.ReadBatch(pid_, entries, (uint32_t)(sizeof(entries) / sizeof(entries[0])),
                              &readout, (uint32_t)sizeof(readout)))
            return false;

        out->id = pawn;
        out->team = readout.team;
        out->health = readout.health;
        out->feet.x = readout.origin[0];
        out->feet.y = readout.origin[1];
        out->feet.z = readout.origin[2];

        /*
         * A bad position is not a lost game: it happens on a respawn and while the
         * pawn is still being built. Report the read as successful with valid ==
         * false, so the caller shows "no local player" instead of re-attaching.
         */
        if (!std::isfinite(out->feet.x) || !std::isfinite(out->feet.y) ||
            !std::isfinite(out->feet.z) ||
            (fabsf(out->feet.x) < 1.0f && fabsf(out->feet.y) < 1.0f) ||
            fabsf(out->feet.x) > off::MaxWorldXY || fabsf(out->feet.y) > off::MaxWorldXY ||
            fabsf(out->feet.z) > off::MaxWorldZ)
            return true;

        out->valid = readout.life == 0 && readout.health > 0;
        return true;
    }

    std::vector<esp::Player> Provider::Collect(km::Client& client)
    {
        std::vector<esp::Player> players;

        if (!base_ || !list_)
            return players;

        esp::Local local = {};
        ReadLocal(client, &local);

        seenControllers_ = 0;
        seenPawns_ = 0;
        seenDormant_ = 0;
        seenDead_ = 0;
        seenNoNode_ = 0;
        seenNoOrigin_ = 0;

        statusX_ = local.feet.x;
        statusY_ = local.feet.y;
        statusZ_ = local.feet.z;

        for (uint32_t index = 1; index < off::MaxControllers; ++index)
        {
            const uint64_t controller = ListEntry(client, index);

            if (!controller)
                continue;

            ++seenControllers_;

            /* m_hPawn on the base controller is what the reference walks with; the
             * derived m_hPlayerPawn is only a fallback for builds that drop it. */
            uint32_t pawnHandle = 0;

            if (!client.ReadValue(pid_, controller + off::ControllerPawn, &pawnHandle) ||
                !pawnHandle)
            {
                if (!client.ReadValue(pid_, controller + off::PlayerPawn, &pawnHandle) ||
                    !pawnHandle)
                    continue;
            }

            /* The local controller is skipped by its own flag, not only by comparing
             * the pawn pointer - a spectator has a controller but no pawn. */
            uint8_t isLocal = 0;

            if (client.ReadValue(pid_, controller + off::IsLocalController, &isLocal) && isLocal)
                continue;

            const uint64_t pawn = EntityAt(client, pawnHandle);

            if (!pawn)
                continue;

            ++seenPawns_;

            if (local.valid && pawn == local.id)
                continue;

            /*
             * Dormant lives on the scene node, so the node is only needed for
             * that flag - the position comes off the pawn itself. A pawn with no
             * node still gets drawn: a missing node is not a reason to drop it.
             */
            const uint64_t node = SceneNode(client, pawn);

            if (node)
            {
                uint8_t dormant = 0;

                if (client.ReadValue(pid_, node + off::Dormant, &dormant) && dormant)
                {
                    ++seenDormant_;
                    continue;
                }
            }
            else
            {
                ++seenNoNode_;
            }

            struct
            {
                uint8_t  team;
                uint8_t  life;
                uint8_t  pad[2];
                int32_t  health;
                uint32_t spotted;
                float    origin[3];
                float    view[3];
            } readout = {};

            const KM_BATCH_DESC entries[] =
            {
                { pawn + off::TeamNum, 1, offsetof(decltype(readout), team) },
                { pawn + off::LifeState, 1, offsetof(decltype(readout), life) },
                { pawn + off::Spotted, sizeof(uint32_t), offsetof(decltype(readout), spotted) },
                { pawn + off::Health, sizeof(int32_t), offsetof(decltype(readout), health) },
                { pawn + off::OldOrigin, sizeof(float) * 3, offsetof(decltype(readout), origin) },
                { pawn + off::ViewOffset, sizeof(float) * 3, offsetof(decltype(readout), view) },
            };

            if (!client.ReadBatch(pid_, entries, (uint32_t)(sizeof(entries) / sizeof(entries[0])),
                                  &readout, (uint32_t)sizeof(readout)))
                continue;

            if (readout.life != 0 || readout.health <= 0)
            {
                ++seenDead_;
                continue;
            }

            if (readout.team != off::TeamT && readout.team != off::TeamCT)
                continue;

            esp::Player player = {};
            player.id = pawn;
            player.team = readout.team;
            player.health = readout.health;
            player.spotted = readout.spotted != 0;

            player.feet.x = readout.origin[0];
            player.feet.y = readout.origin[1];
            player.feet.z = readout.origin[2];

            /*
             * m_vOldOrigin came back as zero: fall back to the scene-node copy
             * before dropping the player. Zero is the common failure of a moved
             * offset and it passes a finite/bounds test, which is what pinned
             * every box to the same screen point.
             */
            if (fabsf(player.feet.x) < 1.0f && fabsf(player.feet.y) < 1.0f && node)
            {
                float scene[3] = { 0 };

                if (client.Read(pid_, node + off::AbsOrigin, scene, sizeof(scene)))
                {
                    player.feet.x = scene[0];
                    player.feet.y = scene[1];
                    player.feet.z = scene[2];
                    ++seenNoOrigin_;
                }
            }

            if (!std::isfinite(player.feet.x) || !std::isfinite(player.feet.y) ||
                !std::isfinite(player.feet.z))
                continue;

            /* Zero is the map origin, never a real player position. */
            if (fabsf(player.feet.x) < 1.0f && fabsf(player.feet.y) < 1.0f)
                continue;

            if (fabsf(player.feet.x) > off::MaxWorldXY || fabsf(player.feet.y) > off::MaxWorldXY ||
                fabsf(player.feet.z) > off::MaxWorldZ)
                continue;

            /* Fixed height above the feet, the way the reference does it. The view
             * offset is the eye, which is inside the head - as a box top it gives a
             * box with no height that never appears to move. */
            player.head = player.feet;
            player.head.z += off::HeroHeight;

            player.name = ReadName(client, controller);
            players.push_back(player);
        }

        return players;
    }
}
