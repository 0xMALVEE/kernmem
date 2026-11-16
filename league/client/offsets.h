/*
 * offsets.h - League of Legends offsets.
 *
 * Every value was recovered by running the patterns in
 * research/older_projects/ls-bp-n-main/.../pattern-league.txt against the live
 * image, then confirmed against real data on six heroes at once. The patterns are
 * the source of truth, not these numbers: re-run them after a patch
 * (scripts/patterns.py) and update this block.
 */
#pragma once

#include <cstdint>

namespace off
{
    constexpr wchar_t PROCESS_NAME[] = L"League of Legends.exe";
    constexpr wchar_t MODULE_NAME[]  = L"League of Legends.exe";
    constexpr wchar_t WINDOW_TITLE[] = L"League of Legends (TM) Client";

    constexpr uint64_t LocalPlayer = 0x1CFA7B0;

    /*
     * The hero list is a struct, not a bare array: +0x8 is the array and +0x10 the
     * count. Walking it directly is what makes all ten champions visible - the old
     * object list only ever held two.
     */
    constexpr uint64_t HeroList      = 0x1CB8550;
    constexpr uint64_t HeroListArray = 0x8;
    constexpr uint64_t HeroListCount = 0x10;

    /*
     * The view matrix is inline at the LEA target of `48 8D 0D ? ? ? ? 0F 10 00`,
     * which hits once in the image. The projection sits 0x40 after it and the two
     * are multiplied. Confirmed by projecting the local hero: it lands within a
     * pixel of screen centre.
     */
    constexpr uint64_t ViewMatrix     = 0x1D7CC60;
    constexpr uint64_t ViewProjection = 0x40;

    /* Team is a byte, 1 = own side, 2 = enemy. */
    constexpr uint64_t HeroTeam = 0x164;

    /* Champion name is ASCII, not UTF-16. */
    constexpr uint64_t HeroName = 0x3890;

    /* World position, inline. */
    constexpr uint64_t HeroPos = 0xA0;

    /* Current health then max health, 0x64 and 0x68 into the stat block. */
    constexpr int64_t HeroHealthFromPos    = 0x64;
    constexpr int64_t HeroMaxHealthFromPos = 0x68;

    constexpr uint32_t MaxObjects = 0x400;
    constexpr float    HeroHeight = 110.0f;
    constexpr float    AttackRange = 700.0f;

    constexpr int32_t TeamOwn   = 1;
    constexpr int32_t TeamEnemy = 2;
}
