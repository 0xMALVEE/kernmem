#include "provider.h"

#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>

#include "offsets.h"

namespace
{
    bool Plausible(uint64_t address)
    {
        return address >= 0x10000 && address < 0x7FFFFFFFFFFFULL;
    }

    bool Finite(const esp::Vec3& value)
    {
        return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
    }
}

namespace apex
{
    const wchar_t* Provider::ProcessName() const { return off::PROCESS_NAME; }
    const wchar_t* Provider::ModuleName() const { return off::MODULE_NAME; }
    const wchar_t* Provider::WindowTitle() const { return off::WINDOW_TITLE; }

    uint32_t Provider::Pid() const { return pid_; }
    uint64_t Provider::Base() const { return base_; }

    std::string Provider::Label(const esp::Player& player) const
    {
        for (const auto& extra : extras_)
            if (extra.first == player.id)
                return extra.second;

        return std::string();
    }

    /*
     * Walk counters. "0 tracked" alone cannot say which step failed, and every
     * step here fails for a different reason: an empty entity list, a slot that
     * holds a non-entity, a dummy team, a dead player, a zeroed origin. The
     * counters name the step so the status line is a diagnosis, not a symptom.
     */
    std::string Provider::Status() const
    {
        char text[160];

        std::snprintf(text, sizeof(text),
                      "slots %u ent %u dead %u noorg %u loc1 %u loc2 %u signon %u",
                      seenSlots_, seenEntities_, seenDead_, seenNoOrigin_,
                      seenLocalRoute1_, seenLocalRoute2_, statusSignon_);

        return std::string(text);
    }

    /*
     * Apex assigns a team id per squad and they are dense from 1, so there is no
     * fixed pair to name. The overlay compares ids; this is only for the status
     * line, hence the number rather than a label.
     */
    const char* Provider::TeamName(int32_t team) const
    {
        static char text[16];

        if (team == off::TeamUnassigned)
            return "-";

        std::snprintf(text, sizeof(text), "T%d", team);
        return text;
    }

    /*
     * Entity behind a handle. Apex has no chunk table: the handle's low 16 bits
     * are the slot index into a flat array of entity pointers, stride 0x20. The
     * high half is a serial and is ignored.
     */
    uint64_t Provider::EntityAt(km::Client& client, uint32_t handle)
    {
        const uint32_t index = handle & off::EntityHandleMask;

        if (!list_ || index >= off::MaxEntities)
            return 0;

        uint64_t entity = 0;

        const uint64_t slot = list_ + off::EntityStride * (uint64_t)index;

        if (!client.ReadValue(pid_, slot, &entity) || !Plausible(entity))
            return 0;

        return entity;
    }

    /*
     * The name is an inline char[] on the entity - no pointer hop. Apex writes a
     * truncated copy there, which is all a label needs.
     */
    std::string Provider::ReadName(km::Client& client, uint64_t entity)
    {
        char buffer[off::NameLength] = { 0 };

        if (!client.Read(pid_, entity + off::Name, buffer, sizeof(buffer) - 1))
            return std::string();

        buffer[sizeof(buffer) - 1] = 0;
        return std::string(buffer);
    }

    bool Provider::Attach(km::Client& client)
    {
        pid_ = 0;
        base_ = 0;
        list_ = 0;

        const uint32_t pid = client.PidByName(off::PROCESS_NAME);
        if (!pid)
            return false;

        const uint64_t base = client.ModuleBase(pid, off::MODULE_NAME);
        if (!base)
            return false;

        /* cl_entitylist is the array itself, not a system object holding it. */
        uint64_t list = 0;

        if (!client.ReadValue(pid, base + off::EntityList, &list) || !Plausible(list))
            return false;

        pid_ = pid;
        base_ = base;
        list_ = list;

        return true;
    }

    bool Provider::ReadMatrix(km::Client& client, esp::Matrix* out)
    {
        if (!base_ || !out)
            return false;

        if (!client.Read(pid_, base_ + off::ViewMatrix, out->m, sizeof(out->m)))
            return false;

        /* An all-zero matrix is what the menu and the drop ship hold. */
        for (int i = 0; i < 16; ++i)
            if (out->m[i] != 0.0f)
                return true;

        return false;
    }

    /*
     * Local entity. Route 1 is the pointer global. Route 2 is the handle global
     * resolved through the entity list, which survives builds where the pointer
     * global is stale after a respawn.
     */
    uint64_t Provider::LocalEntity(km::Client& client)
    {
        uint64_t entity = 0;

        if (client.ReadValue(pid_, base_ + off::LocalPlayer, &entity) && Plausible(entity))
        {
            ++seenLocalRoute1_;
            return entity;
        }

        uint32_t handle = 0;

        if (client.ReadValue(pid_, base_ + off::LocalEntityHandle, &handle) && handle)
        {
            const uint64_t fromHandle = EntityAt(client, handle);

            if (fromHandle)
            {
                ++seenLocalRoute2_;
                return fromHandle;
            }
        }

        return 0;
    }

    bool Provider::ReadLocal(km::Client& client, esp::Local* out)
    {
        out->valid = false;
        out->id = 0;

        if (!base_ || !list_)
            return false;

        /*
         * A stale local pointer during a menu transition reads as a plausible
         * address with garbage in it, so the signon state gates the whole read.
         * Anything below 6 is still connecting.
         */
        int32_t signon = 0;

        if (client.ReadValue(pid_, base_ + off::SignonState, &signon))
        {
            statusSignon_ = (uint32_t)(signon & 0xFFFF);

            if (signon != off::SignonStateFull)
                return true;
        }

        const uint64_t entity = LocalEntity(client);

        if (!entity)
            return false;

        struct
        {
            int32_t team;
            int32_t health;
            uint8_t life;
            uint8_t pad[3];
            float   origin[3];
        } readout = {};

        const KM_BATCH_DESC entries[] =
        {
            { entity + off::TeamNum, sizeof(int32_t), offsetof(decltype(readout), team) },
            { entity + off::Health, sizeof(int32_t), offsetof(decltype(readout), health) },
            { entity + off::LifeState, 1, offsetof(decltype(readout), life) },
            { entity + off::AbsOrigin, sizeof(float) * 3, offsetof(decltype(readout), origin) },
        };

        if (!client.ReadBatch(pid_, entries, (uint32_t)(sizeof(entries) / sizeof(entries[0])),
                              &readout, (uint32_t)sizeof(readout)))
            return false;

        out->id = entity;
        out->team = readout.team;
        out->health = readout.health;
        out->feet.x = readout.origin[0];
        out->feet.y = readout.origin[1];
        out->feet.z = readout.origin[2];

        /*
         * A bad position is not a lost game: it happens on a respawn and while
         * the entity is still being built. Report the read as successful with
         * valid == false, so the caller shows "no local player" instead of
         * re-attaching.
         */
        if (!Finite(out->feet))
            return true;

        if (fabsf(out->feet.x) < 1.0f && fabsf(out->feet.y) < 1.0f)
            return true;

        if (fabsf(out->feet.x) > off::MaxWorldXY || fabsf(out->feet.y) > off::MaxWorldXY ||
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

        /* Reset before the local read: ReadLocal increments the route counters,
         * so resetting afterwards would report them as permanently 0. */
        seenSlots_ = 0;
        seenEntities_ = 0;
        seenDead_ = 0;
        seenNoOrigin_ = 0;
        seenLocalRoute1_ = 0;
        seenLocalRoute2_ = 0;
        extras_.clear();

        esp::Local local = {};
        ReadLocal(client, &local);

        for (uint32_t index = 1; index < off::MaxEntities; ++index)
        {
            const uint64_t entity = EntityAt(client, index);

            if (!entity)
                continue;

            ++seenSlots_;

            if (local.id && entity == local.id)
                continue;

            struct
            {
                int32_t team;
                int32_t health;
                int32_t shield;
                float   lastVisible;
                uint8_t life;
                uint8_t knocked;
                uint8_t pad[2];
                float   origin[3];
            } readout = {};

            const KM_BATCH_DESC entries[] =
            {
                { entity + off::TeamNum, sizeof(int32_t), offsetof(decltype(readout), team) },
                { entity + off::Health, sizeof(int32_t), offsetof(decltype(readout), health) },
                { entity + off::ShieldHealth, sizeof(int32_t), offsetof(decltype(readout), shield) },
                { entity + off::LastVisibleTime, sizeof(float), offsetof(decltype(readout), lastVisible) },
                { entity + off::LifeState, 1, offsetof(decltype(readout), life) },
                { entity + off::BleedoutState, 1, offsetof(decltype(readout), knocked) },
                { entity + off::AbsOrigin, sizeof(float) * 3, offsetof(decltype(readout), origin) },
            };

            if (!client.ReadBatch(pid_, entries, (uint32_t)(sizeof(entries) / sizeof(entries[0])),
                                  &readout, (uint32_t)sizeof(readout)))
                continue;

            /* Health 0 also catches the world and prop entities that share the
             * array - they have a plausible pointer but no health. */
            if (readout.health <= 0 || readout.health > 200 || readout.life != 0)
            {
                ++seenDead_;
                continue;
            }

            if (readout.team <= off::TeamUnassigned || readout.team > 64)
                continue;

            esp::Player player = {};
            player.id = entity;
            player.team = readout.team;
            player.health = readout.health;
            player.spotted = readout.lastVisible > 0.0f;

            player.feet.x = readout.origin[0];
            player.feet.y = readout.origin[1];
            player.feet.z = readout.origin[2];

            if (!Finite(player.feet))
                continue;

            /* Zero is the map origin, never a real player position. */
            if (fabsf(player.feet.x) < 1.0f && fabsf(player.feet.y) < 1.0f)
            {
                ++seenNoOrigin_;
                continue;
            }

            if (fabsf(player.feet.x) > off::MaxWorldXY || fabsf(player.feet.y) > off::MaxWorldXY ||
                fabsf(player.feet.z) > off::MaxWorldZ)
                continue;

            /* Fixed height above the feet. m_vecAbsOrigin is the feet, not the
             * eyes, so a fixed box height is what keeps the box on the body. */
            player.head = player.feet;
            player.head.z += off::HeroHeight;

            player.name = ReadName(client, entity);

            /* Shield and knock state ride along as a label, so the provider
             * stays free of drawing. */
            if (readout.shield > 0 || readout.knocked)
            {
                char extra[32];

                if (readout.knocked)
                    std::snprintf(extra, sizeof(extra), "KNOCK");
                else
                    std::snprintf(extra, sizeof(extra), "+%d shield", readout.shield);

                extras_.emplace_back(entity, std::string(extra));
            }

            ++seenEntities_;
            players.push_back(player);
        }

        return players;
    }
}
