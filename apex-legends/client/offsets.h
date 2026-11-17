/*
 * offsets.h - Apex Legends (r5apex.exe) offsets.
 *
 * Source: apex-legends/offsets.txt. Every value below is copied from that dump,
 * and the struct or global it came from is named in the comment, so a new dump
 * can be diffed against this header line by line.
 *
 * All values are RVAs into r5apex.exe. They are build specific: after a game
 * update the client still runs but reports nothing, which is the intended
 * failure mode - see the status line in the overlay.
 */
#pragma once

#include <cstdint>

namespace off
{
    /* Image name as the kernel reports it, and the module every RVA is relative
     * to. r5apex.exe is the shipping build; r5apex_dx12.exe is a separate image
     * with the same layout, so it is not used here. */
    constexpr wchar_t PROCESS_NAME[] = L"r5apex.exe";
    constexpr wchar_t MODULE_NAME[]  = L"r5apex.exe";

    /* Game window title, so the shared overlay can follow it. */
    constexpr wchar_t WINDOW_TITLE[] = L"Apex Legends";

    /* ---- globals (misc) ---------------------------------------------- */
    /* Flat array of entity pointers. The handle's low 16 bits are the slot
     * index; there is no chunk table like CS2 has, the array is walked directly
     * with EntityStride. */
    constexpr uint64_t EntityList        = 0x6b515d8;   /* cl_entitylist */
    /* Local entity as a pointer. Route 1. */
    constexpr uint64_t LocalPlayer       = 0x3eac668;   /* LocalPlayer */
    /* Local entity as a handle. Route 2, resolved through EntityList. */
    constexpr uint64_t LocalEntityHandle = 0x3d3a8d0;   /* LocalEntityHandle */
    /* World-to-clip matrix, row major with the w row at m[12..15]. */
    constexpr uint64_t ViewMatrix        = 0x11a390;    /* ViewMatrix */
    /* 6 = fully connected. Anything else means the local entity may be stale. */
    constexpr uint64_t SignonState       = 0x36583fc;   /* SignonState */
    constexpr uint64_t LevelName         = 0x3658514;   /* LevelName */

    /* ---- entity list shape ------------------------------------------- */
    /* 0x20 bytes per slot. Walking this as anything else lands inside the next
     * entity and reads a plausible-looking garbage pointer. */
    constexpr uint64_t EntityStride      = 0x20;
    /* 16-bit slot index in the low half; the high half is the serial. */
    constexpr uint32_t EntityHandleMask  = 0xFFFF;
    /* Player entities live well below this. Scanning all 0x10000 slots every
     * frame costs more than it finds. */
    constexpr uint32_t MaxEntities       = 0x4000;

    /* ---- player entity -----------------------------------------------
     * Taken from the misc block: these are entity-relative fields, the same
     * values the DT_BaseEntity / DT_Player blocks repeat. */
    constexpr uint64_t AbsOrigin    = 0x170;   /* m_vecAbsOrigin, 3 floats */
    constexpr uint64_t Health       = 0x330;   /* m_iHealth */
    constexpr uint64_t TeamNum      = 0x340;   /* m_iTeamNum */
    constexpr uint64_t ShieldHealth = 0x18c;   /* m_shieldHealth */
    constexpr uint64_t Name         = 0x481;   /* m_iName, inline char[] */
    constexpr uint64_t LifeState    = 0x698;   /* m_lifeState, 0 = alive */
    constexpr uint64_t BleedoutState = 0x27a8; /* m_bleedoutState, knocked */
    /* lastVisibleTime. The dump lists it as `lastVisibleTime_-2` at 0x1a6a, which
     * is a float that straddles the alignment, so the field itself starts at
     * 0x1a6c. It is a float: the last time the entity was in the PVS. A stale
     * value is a player the server has stopped sending us. */
    constexpr uint64_t LastVisibleTime = 0x1a6c;

    /* Name field length. The inline buffer is longer, but nothing writes past
     * this and a shorter read is a cheaper read. */
    constexpr uint32_t NameLength = 32;

    /* ---- camera ------------------------------------------------------
     * The matrix is already world-to-clip, so no separate FOV or aspect maths is
     * needed: project with the matrix and divide by w. */
    constexpr float NearW = 0.01f;

    /* Box height above the feet, in Source units. Apex players are taller than
     * CS2 ones; a fixed height also keeps the box still while the view model
     * bobs. */
    constexpr float HeroHeight = 72.0f;

    /* Sanity bounds for a world position, in Source units. */
    constexpr float MaxWorldXY = 40000.0f;
    constexpr float MaxWorldZ  = 12000.0f;

    /* Apex assigns a team id per squad, so there is no fixed pair like T/CT.
     * The overlay only compares ids; TeamName is for the status line. */
    constexpr int32_t TeamUnassigned = 0;

    /* SignonState value for a fully connected client. */
    constexpr int32_t SignonStateFull = 6;
}
