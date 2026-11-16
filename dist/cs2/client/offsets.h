/*
 * offsets.h - CS2 client.dll offsets.
 *
 * Source: cs2/offsets.txt (the dump in the repo root of this folder). Every value
 * below is copied from that file; the struct names in the comments are the ones
 * it uses, so a new dump can be diffed against this header line by line.
 *
 * All values are RVAs into client.dll. They are build specific: after a game
 * update the client still runs but reports nothing, which is the intended
 * failure mode - see the status line in the overlay.
 */
#pragma once

#include <cstdint>

namespace off
{
    /* Image name as the kernel reports it, and the module every RVA is relative
     * to. cs2.exe is the shipping build. */
    constexpr wchar_t PROCESS_NAME[] = L"cs2.exe";
    constexpr wchar_t MODULE_NAME[]  = L"client.dll";

    /* Game window title, so the shared overlay can follow it. */
    constexpr wchar_t WINDOW_TITLE[] = L"Counter-Strike 2";

    /* ---- globals (client.dll) ---------------------------------------- */
    constexpr uint64_t EntityList            = 0x2709010;
    constexpr uint64_t LocalPlayerController = 0x252B0C8;
    constexpr uint64_t LocalPlayerPawn       = 0x2553D38;  /* pointer, not a handle */
    constexpr uint64_t ViewMatrix            = 0x2559640;

    /* ---- entity list shape -------------------------------------------
     * The list is chunked, not flat: a pointer per 512 entries, then a fixed
     * stride inside a chunk. Walking it as a flat array is the classic mistake
     * and reads garbage past index 511. */
    /* CGameEntitySystem's chunk table starts here; the entity list global points
     * at the system, not at the table. */
    constexpr uint64_t EntityListArray   = 0x10;
    constexpr uint64_t EntityChunkStride = 0x8;
    /* 0x70. The value this replaces was 0x78 - with that, every entity lands 8
     * bytes into the next slot and reads garbage. */
    constexpr uint64_t EntityStride      = 0x70;
    constexpr uint32_t EntityChunkShift  = 9;
    constexpr uint32_t EntityPerChunk    = 1u << EntityChunkShift;

    /* Controllers sit at low indices; nothing above this is a player. */
    constexpr uint32_t MaxControllers = 64;

    /* Entity handles are 32-bit with a serial in the high bits. */
    constexpr uint32_t EntityHandleMask = 0x7FFF;

    /* ---- C_BaseEntity ------------------------------------------------ */
    constexpr uint64_t GameSceneNode = 0x330;
    constexpr uint64_t TeamNum       = 0x3E7;   /* byte: 2 = T, 3 = CT */

    /* ---- CGameSceneNode ---------------------------------------------- */
    /* Scene-node position. Bones use this; the ESP uses m_vOldOrigin on the pawn,
     * which needs no dereference. */
    constexpr uint64_t AbsOrigin = 0xC8;

    /* Set while a pawn exists but is not in the PVS. Drawing one gives a box that
     * never moves and often sits at the origin. */
    constexpr uint64_t Dormant = 0x103;   /* CGameSceneNode, byte */

    /* ---- C_BasePlayerPawn -------------------------------------------- */
    /* World position, 3 floats, read straight off the pawn. This is the field an
     * external uses - the scene-node copy is for bones and costs a dereference. */
    constexpr uint64_t OldOrigin  = 0x14A4;
    constexpr uint64_t ViewOffset = 0xF60;      /* eye offset from origin */
    constexpr uint64_t Health     = 0x34C;      /* C_BaseEntity */
    constexpr uint64_t LifeState  = 0x354;      /* 0 = alive */

    /* ---- C_CSPlayerPawn ---------------------------------------------- */
    constexpr uint64_t EyeAngles    = 0x3630;
    constexpr uint64_t SpottedState  = 0x1EB8;
    constexpr uint64_t SpottedByMask = 0xC;    /* uint32[2] inside the state */
    constexpr uint64_t Spotted       = SpottedState + SpottedByMask;   /* uint32 */

    /* ---- CCSPlayerController ----------------------------------------- */
    /* m_hPawn on CBasePlayerController - what the reference walks with. The
     * derived m_hPlayerPawn below is kept as a fallback only. */
    constexpr uint64_t ControllerPawn = 0x6BC;   /* handle, not a pointer */
    constexpr uint64_t IsLocalController = 0x790; /* byte */
    constexpr uint64_t PlayerPawn    = 0x92C;   /* handle, not a pointer */
    constexpr uint64_t PawnIsAlive   = 0x934;   /* byte */
    /* Inline char[128] on the controller - read directly, no pointer hop. */
    constexpr uint64_t PawnName      = 0x6FC;
    constexpr uint64_t SanitizedName = 0x878;   /* CUtlString: pointer first */

    /* ---- camera ------------------------------------------------------
     * The matrix is already world-to-clip, so no separate FOV or aspect maths is
     * needed: project with the matrix and divide by w. */
    constexpr float NearW = 0.01f;

    /* Box height above the feet, in Source units. Fixed rather than derived from
     * the view offset: the eye sits inside the head, so using it as the box top
     * gives a box with no height that never appears to move. */
    constexpr float HeroHeight = 65.0f;

    /* Sanity bounds for a world position, in Source units. */
    constexpr float MaxWorldXY = 20000.0f;
    constexpr float MaxWorldZ  = 5000.0f;

    /* Team ids. */
    constexpr int32_t TeamT  = 2;
    constexpr int32_t TeamCT = 3;
}
