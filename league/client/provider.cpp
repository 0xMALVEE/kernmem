#include "provider.h"

#include <cmath>
#include <cstddef>
#include <cstring>

#include "offsets.h"

namespace
{
    constexpr float kMaxWorldXY = 20000.0f;
    constexpr float kMaxWorldZ  = 3000.0f;
    constexpr float kHeroHeight = 110.0f;
    constexpr float kDefaultRange = 700.0f;

    void Filled(HDC dc, int x, int y, int w, int h, COLORREF colour)
    {
        HBRUSH brush = CreateSolidBrush(colour);
        RECT rect = { x, y, x + w, y + h };
        FillRect(dc, &rect, brush);
        DeleteObject(brush);
    }

    void Outline(HDC dc, int x, int y, int w, int h, COLORREF colour)
    {
        HPEN pen = CreatePen(PS_SOLID, 1, colour);
        HGDIOBJ old = SelectObject(dc, pen);
        HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));

        Rectangle(dc, x, y, x + w, y + h);

        SelectObject(dc, oldBrush);
        SelectObject(dc, old);
        DeleteObject(pen);
    }

    void Segment(HDC dc, int x1, int y1, int x2, int y2, COLORREF colour)
    {
        HPEN pen = CreatePen(PS_SOLID, 1, colour);
        HGDIOBJ old = SelectObject(dc, pen);

        MoveToEx(dc, x1, y1, nullptr);
        LineTo(dc, x2, y2);

        SelectObject(dc, old);
        DeleteObject(pen);
    }

    void Text(HDC dc, int x, int y, COLORREF colour, const char* text)
    {
        SetTextColor(dc, colour);
        SetBkMode(dc, TRANSPARENT);
        TextOutA(dc, x, y, text, (int)strlen(text));
    }
}

namespace league
{
    const wchar_t* Provider::ProcessName() const { return off::PROCESS_NAME; }
    const wchar_t* Provider::ModuleName() const { return off::MODULE_NAME; }
    const wchar_t* Provider::WindowTitle() const { return off::WINDOW_TITLE; }

    uint32_t Provider::Pid() const { return pid_; }
    uint64_t Provider::Base() const { return base_; }

    /* The name is drawn by the framework already, so nothing extra. */
    std::string Provider::Label(const esp::Player&) const { return std::string(); }

    const char* Provider::TeamName(int32_t team) const
    {
        if (team == off::TeamOwn)
            return "own";
        if (team == off::TeamEnemy)
            return "enemy";
        return "?";
    }

    bool Provider::Attach(km::Client& client)
    {
        pid_ = 0;
        base_ = 0;

        const uint32_t pid = client.PidByName(off::PROCESS_NAME);

        if (!pid)
            return false;

        const uint64_t base = client.ModuleBase(pid, off::MODULE_NAME);

        if (!base)
            return false;

        pid_ = pid;
        base_ = base;

        return true;
    }

    /*
     * Reads a hero's world position. It is inline, so this is one read plus
     * validation: the triple must be finite world coordinates rather than a
     * denormal or the map origin, so a half-built object is skipped instead of
     * being drawn at (0, 0, 0).
     */
    bool Provider::ReadPosition(km::Client& client, uint64_t hero, esp::Vec3* out) const
    {
        const uint64_t address = hero + off::HeroPos;

        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;

        if (!client.Read(pid_, address, &x, sizeof(x)))
            return false;
        if (!client.Read(pid_, address + 4, &y, sizeof(y)))
            return false;
        if (!client.Read(pid_, address + 8, &z, sizeof(z)))
            return false;

        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z))
            return false;

        if (fabsf(x) < 1.0f || fabsf(y) < 1.0f)
            return false;

        if (fabsf(x) > kMaxWorldXY || fabsf(y) > kMaxWorldXY || fabsf(z) > kMaxWorldZ)
            return false;

        out->x = x;
        out->y = y;
        out->z = z;

        return true;
    }

    /*
     * Reads the camera. Both matrices sit back to back in the image and neither is
     * behind a pointer. They are composed here, once, so ProjectPoint stays a
     * single multiply against a finished matrix.
     *
     * The composition and the apply must agree on handedness. Composed this way
     * and applied as a column vector, the local hero lands on screen centre - which
     * is what the camera does, since it follows the player. The row-vector apply of
     * the same data put the hero 867 px off on a 3818x2032 client.
     *
     * A missing camera is not a failure: the radar fallback still has something
     * honest to draw, so this reports success and leaves haveMatrix_ false.
     */
    bool Provider::ReadMatrix(km::Client& client, esp::Matrix* out)
    {
        haveMatrix_ = false;
        corrDone_ = false;

        float view[16] = { 0 };
        float projection[16] = { 0 };

        bool ok = base_ != 0 &&
                  client.Read(pid_, base_ + off::ViewMatrix, view, sizeof(view)) &&
                  client.Read(pid_, base_ + off::ViewMatrix + off::ViewProjection,
                              projection, sizeof(projection));

        if (ok)
        {
            /* A camera that has not been set up yet reads as all zeroes, and every
             * point then divides by zero. */
            if (view[0] == 0.0f && view[15] == 0.0f)
                ok = false;
        }

        if (ok)
        {
            for (int i = 0; i < 16; ++i)
            {
                if (!std::isfinite(view[i]) || !std::isfinite(projection[i]))
                {
                    ok = false;
                    break;
                }
            }
        }

        if (!ok)
        {
            /* Identity, so the framework still has a matrix to hand around. */
            memset(out, 0, sizeof(*out));
            out->m[0] = out->m[5] = out->m[10] = out->m[15] = 1.0f;
            corrMatrix_ = *out;
            return true;
        }

        /* matrix = view * projection, row-major flat. */
        for (int row = 0; row < 4; ++row)
        {
            for (int column = 0; column < 4; ++column)
            {
                float sum = 0.0f;

                for (int k = 0; k < 4; ++k)
                    sum += view[row * 4 + k] * projection[k * 4 + column];

                out->m[row * 4 + column] = sum;
            }
        }

        for (int i = 0; i < 16; ++i)
        {
            if (!std::isfinite(out->m[i]))
            {
                memset(out, 0, sizeof(*out));
                out->m[0] = out->m[5] = out->m[10] = out->m[15] = 1.0f;
                corrMatrix_ = *out;
                return true;
            }
        }

        corrMatrix_ = *out;
        haveMatrix_ = true;

        return true;
    }

    bool Provider::ReadLocal(km::Client& client, esp::Local* out)
    {
        out->valid = false;
        corrHasLocal_ = false;
        corrDone_ = false;

        if (!base_)
            return false;

        uint64_t local = 0;

        if (!client.ReadValue(pid_, base_ + off::LocalPlayer, &local) || !local)
            return false;

        esp::Vec3 position = {};

        if (!ReadPosition(client, local, &position))
            return false;

        struct
        {
            uint8_t team;
            uint8_t pad[3];
            float   health;
            float   maxHealth;
            float   range;
        } readout = {};

        const KM_BATCH_DESC entries[] =
        {
            { local + off::HeroTeam, 1, offsetof(decltype(readout), team) },
            { local + off::HeroPos + (uint64_t)off::HeroHealthFromPos, sizeof(float),
              offsetof(decltype(readout), health) },
            { local + off::HeroPos + (uint64_t)off::HeroMaxHealthFromPos, sizeof(float),
              offsetof(decltype(readout), maxHealth) },
            { local + off::HeroAttackRange, sizeof(float), offsetof(decltype(readout), range) },
        };

        client.ReadBatch(pid_, entries, (uint32_t)(sizeof(entries) / sizeof(entries[0])),
                         &readout, (uint32_t)sizeof(readout));

        out->id = local;
        out->feet = position;
        out->team = readout.team;
        out->health = (int32_t)readout.health;
        out->valid = readout.health >= 0.0f;

        /* Only an in-range value is trusted; the ring falls back otherwise. */
        localRange_ = (readout.range > 50.0f && readout.range < 3000.0f) ? readout.range : 0.0f;

        /* The correction needs the local position, so it is stashed here. */
        corrLocal_ = position;
        corrHasLocal_ = haveMatrix_;

        return true;
    }

    std::vector<esp::Player> Provider::Collect(km::Client& client)
    {
        std::vector<esp::Player> players;

        if (!base_)
            return players;

        uint64_t list = 0;

        if (!client.ReadValue(pid_, base_ + off::HeroList, &list) || !list)
            return players;

        uint64_t array = 0;
        int32_t count = 0;

        if (!client.ReadValue(pid_, list + off::HeroListArray, &array) || !array)
            return players;

        if (!client.ReadValue(pid_, list + off::HeroListCount, &count) || count < 1)
            return players;

        if ((uint32_t)count > off::MaxObjects)
            count = (int32_t)off::MaxObjects;

        for (int32_t index = 0; index < count; ++index)
        {
            uint64_t object = 0;

            if (!client.ReadValue(pid_, array + (uint64_t)index * 8, &object) || !object)
                continue;

            /*
             * Everything this hero contributes, fetched in one IOCTL: one round
             * trip per champion instead of six.
             */
            struct
            {
                uint8_t team;
                uint8_t visible;
                uint8_t pad[2];
                float   position[3];
                float   health;
                char    name[32];
            } readout = {};

            const KM_BATCH_DESC entries[] =
            {
                { object + off::HeroTeam, 1, offsetof(decltype(readout), team) },
                { object + off::HeroVisible, 1, offsetof(decltype(readout), visible) },
                { object + off::HeroPos, sizeof(float) * 3, offsetof(decltype(readout), position) },
                { object + off::HeroPos + (uint64_t)off::HeroHealthFromPos, sizeof(float),
                  offsetof(decltype(readout), health) },
                { object + off::HeroName, (unsigned long)_countof(readout.name),
                  offsetof(decltype(readout), name) },
            };

            if (!client.ReadBatch(pid_, entries, (uint32_t)_countof(entries), &readout,
                                  (uint32_t)sizeof(readout)))
                continue;

            if (readout.team != off::TeamOwn && readout.team != off::TeamEnemy)
                continue;

            if (readout.health < 0.0f)
                continue;

            esp::Player player = {};
            player.id = object;
            player.team = readout.team;
            player.health = (int32_t)readout.health;

            player.feet.x = readout.position[0];
            player.feet.y = readout.position[1];
            player.feet.z = readout.position[2];

            /* The batch reports success when any descriptor landed, so the
             * position is validated here. */
            if (!std::isfinite(player.feet.x) || !std::isfinite(player.feet.y) ||
                !std::isfinite(player.feet.z))
                continue;

            if (fabsf(player.feet.x) > kMaxWorldXY || fabsf(player.feet.y) > kMaxWorldXY)
                continue;

            player.head = player.feet;
            player.head.z += kHeroHeight;

            readout.name[_countof(readout.name) - 1] = 0;
            player.name.assign(readout.name);

            /* Visible/dead flags are not identified on this build, so spotted stays
             * false and the draw path filters on health alone. */
            players.push_back(player);
        }

        return players;
    }

    /*
     * Two corrections, both measured at the local hero.
     *
     * The camera block's rotation and translation are not in the frame the world
     * positions use: with the plain projection the hero lands a constant distance
     * off screen, and because the camera is pitched a flat ground ring comes out
     * squashed (measured 464x55 px at 700 units).
     *
     * The camera follows the player, so the hero is the one point whose screen
     * position is known - it is the centre. Measure where it lands and shift every
     * point by that delta, then measure how many pixels one world unit moves each
     * axis and rescale so both axes agree. Everything shares the transform, so the
     * ring keeps its shape, sits on the champion and is the right size.
     */
    void Provider::Correct(int width, int height) const
    {
        corrScaleX_ = 1.0f;
        corrScaleY_ = 1.0f;
        corrOffsetX_ = 0.0f;
        corrOffsetY_ = 0.0f;
        corrDone_ = true;

        if (!corrHasLocal_)
            return;

        const float step = 1000.0f;
        const esp::Vec3 localPos = corrLocal_;

        esp::Vec3 alongX = { localPos.x + step, localPos.y, localPos.z };
        esp::Vec3 alongY = { localPos.x, localPos.y + step, localPos.z };

        float selfX = 0.0f;
        float selfY = 0.0f;
        float stepX = 0.0f;
        float stepY = 0.0f;

        /* Raw projection: transposed stride, no correction applied yet. */
        auto raw = [&](const esp::Vec3& world, float* sx, float* sy)
        {
            const float* m = corrMatrix_.m;

            const float clipX = world.x * m[0] + world.y * m[4] + world.z * m[8] + m[12];
            const float clipY = world.x * m[1] + world.y * m[5] + world.z * m[9] + m[13];
            const float clipW = world.x * m[3] + world.y * m[7] + world.z * m[11] + m[15];

            if (fabsf(clipW) <= 0.001f)
                return false;

            *sx = (width * 0.5f) + (width * 0.5f) * (clipX / clipW);
            *sy = (height * 0.5f) - (height * 0.5f) * (clipY / clipW);

            return std::isfinite(*sx) && std::isfinite(*sy);
        };

        if (!raw(localPos, &selfX, &selfY))
            return;

        if (!raw(alongX, &stepX, &stepY))
            return;

        const float gradientX = fabsf(stepX - selfX);

        if (!raw(alongY, &stepX, &stepY))
            return;

        const float gradientY = fabsf(stepY - selfY);

        if (gradientX > 0.01f && gradientY > 0.01f)
        {
            const float target = (gradientX + gradientY) * 0.5f;

            corrScaleX_ = target / gradientX;
            corrScaleY_ = target / gradientY;

            /* Keep it sane if a gradient is tiny or the camera is degenerate. */
            if (corrScaleX_ < 0.05f) corrScaleX_ = 0.05f;
            if (corrScaleX_ > 20.0f) corrScaleX_ = 20.0f;
            if (corrScaleY_ < 0.05f) corrScaleY_ = 0.05f;
            if (corrScaleY_ > 20.0f) corrScaleY_ = 20.0f;
        }

        if (!raw(localPos, &selfX, &selfY))
            return;

        corrOffsetX_ = width * 0.5f - selfX;
        corrOffsetY_ = height * 0.5f - selfY;
    }

    bool Provider::ProjectPoint(const esp::Matrix& matrix, const esp::Vec3& world, int width,
                                int height, float* x, float* y) const
    {
        /* No camera, no boxes - the radar in DrawExtra carries the frame. */
        if (!haveMatrix_)
            return false;

        if (!corrDone_)
        {
            corrMatrix_ = matrix;
            Correct(width, height);
        }

        const float* m = matrix.m;

        const float clipX = world.x * m[0] + world.y * m[4] + world.z * m[8] + m[12];
        const float clipY = world.x * m[1] + world.y * m[5] + world.z * m[9] + m[13];
        const float clipW = world.x * m[3] + world.y * m[7] + world.z * m[11] + m[15];

        /* The camera looks down -Z, so anything in front has a negative w and only
         * a w of (near) zero is degenerate. Testing w > 0 here would reject the
         * whole visible world. */
        if (fabsf(clipW) <= 0.001f)
            return false;

        *x = (width * 0.5f) + (width * 0.5f) * (clipX / clipW) * corrScaleX_ + corrOffsetX_;
        *y = (height * 0.5f) - (height * 0.5f) * (clipY / clipW) * corrScaleY_ + corrOffsetY_;

        return std::isfinite(*x) && std::isfinite(*y);
    }

    /*
     * Own attack range ring plus the radar fallback. The ring only makes sense with
     * a camera; the radar needs no projection at all, only world positions, which
     * is why it works on the offsets we actually have.
     */
    void Provider::DrawExtra(HDC dc, int width, int height, const esp::Local& local,
                             const std::vector<esp::Player>& players) const
    {
        if (!corrHasLocal_)
        {
            const int size = 220;
            const int x = 12;
            const int y = height - size - 12;

            if (y < 90)
                return;

            Filled(dc, x, y, size, size, RGB(16, 16, 16));
            Outline(dc, x, y, size, size, RGB(70, 70, 70));

            const float scale = 1.0f / 20.0f;   /* 1 px per 20 world units */
            const float half = (float)size * 0.5f;

            Filled(dc, x + (int)half - 2, y + (int)half - 2, 5, 5, RGB(120, 255, 120));

            for (const esp::Player& player : players)
            {
                const float dx = (player.feet.x - local.feet.x) * scale;
                const float dy = (player.feet.y - local.feet.y) * scale;

                const int px = x + (int)(half + dx);
                const int py = y + (int)(half - dy);

                if (px < x + 2 || px > x + size - 2 || py < y + 2 || py > y + size - 2)
                    continue;

                const COLORREF colour = player.team == local.team ? RGB(90, 160, 255)
                                                                 : RGB(255, 70, 70);

                Filled(dc, px - 2, py - 2, 5, 5, colour);
            }

            Text(dc, x + 6, y + 4, RGB(150, 150, 150), "radar - no camera");
            return;
        }

        /* Own attack range ring, in world space, through the same correction as
         * every other point so it stays circular and correctly sized. */
        const float range = localRange_ > 0.0f ? localRange_ : kDefaultRange;

        const int segments = 64;
        float prevX = 0.0f;
        float prevY = 0.0f;
        bool prevValid = false;

        for (int i = 0; i <= segments; ++i)
        {
            const float angle = (float)i / (float)segments * 6.2831853f;

            esp::Vec3 point = { local.feet.x + cosf(angle) * range,
                                local.feet.y + sinf(angle) * range,
                                local.feet.z };

            float sx = 0.0f;
            float sy = 0.0f;
            const bool valid = ProjectPoint(corrMatrix_, point, width, height, &sx, &sy);

            if (valid && prevValid)
                Segment(dc, (int)prevX, (int)prevY, (int)sx, (int)sy, RGB(90, 200, 255));

            prevValid = valid;
            prevX = sx;
            prevY = sy;
        }
    }
}
