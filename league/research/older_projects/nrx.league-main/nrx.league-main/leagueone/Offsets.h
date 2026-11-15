#pragma once
#include <cstdint>

namespace Offset
{
    // Managers
    inline constexpr uint64_t oLocalPlayer = 0x1E257E0;
    inline constexpr uint64_t oHeroList = 0x1DEAEE0;
    inline constexpr uint64_t oGametime = 0x1DF9650;

    inline constexpr uint64_t oMissileList = 0x1DEEDA0;
    inline constexpr uint64_t oTurretList = 0x1D3BEB8; // Old (Not in dump)

    // --- MINION SPECIFIC ---
    inline constexpr uint64_t oMinionList = 0x1DEED88; // Found via Scanner
    inline constexpr uint64_t LaneMinionArray = 0x68;  // Confirmed 0x68
    inline constexpr uint64_t LaneMinionCount = 0x70;  // Confirmed 0x70
    // Note: This looks like a Function RVA (0x2AC7A0) now, not a struct member offset (previously 0x4C71).
    inline constexpr uint64_t LaneMinionType = 0x2AC7A0;

    // SpellBook
    inline constexpr uint64_t oSpellSlotArray = 0x3BC8; // Directly points to Q, W, E, R array
    // Functions
    inline constexpr uint64_t oCastSpell = 0x275650;
    inline constexpr uint64_t oSpellSlotInput = 0x120; // Pointer to SpellInput inside SpellSlot
    inline constexpr uint64_t oSpellSlotInfo = 0x128; // Pointer to SpellInfo inside SpellSlot

    // --> NEW: SILENT AIM OFFSETS FROM DUMP <--
    inline constexpr uint64_t oHudCursorTargetLogic = 0x28;
    inline constexpr uint64_t oHudCursorPos1 = 0x28;
    inline constexpr uint64_t oHudCursorPos2 = 0x34;
    inline constexpr uint64_t oHudCursorPos3 = 0x40;

    inline constexpr uint64_t oHudCursorPos2D = 0x158;
    inline constexpr uint64_t oHudCursorPos2DPtr = 0x48;
    inline constexpr uint64_t oHudCursorPos2DOffset = 0x78;

    // Spellbook
    inline constexpr uint64_t oObjSpellBook = 0x30E8; // Verified Pointer

    // Missile Specific (Offsets relative to the Missile GameObject)

    // --- MISSLE EVENT OFFSETS (Perfected) ---
    inline constexpr uint64_t oCastInfoPtr = 0x28;      // Pointer from Root to SpellCastInfo
    inline constexpr uint64_t oCastSpellName = 0x68;    // Inline String 
    inline constexpr uint64_t oCastCurrentPos = 0xF8;   // Vector3 (Moving Missile Head)

    // NEW OFFSETS DECODED FROM DEEPSPAWNDUMP
    inline constexpr uint64_t oCastStartPos = 0x388;    // Vector3 (Where it spawned)
    inline constexpr uint64_t oCastEndPos = 0x394;      // Vector3 (Max Range trajectory end)
    inline constexpr uint64_t oCastClickPos = 0x3A0;    // Vector3 (Where mouse was clicked)

    // GameObject
    inline constexpr uint64_t oObjNetId = 0xCC;
    inline constexpr uint64_t oObjPosition = 0x25C; // Old (Not in dump)
    inline constexpr uint64_t oHealth = 0x1080; // Old (Not in dump)
    inline constexpr uint64_t oMaxHealth = 0x10A8; // <--- NEW: Max Health
    inline constexpr uint64_t oDead = 0x250; // Old (Not in dump)
    inline constexpr uint64_t oTargetable = 0xED0;
    inline constexpr uint64_t oVisibility = 0x300; // Old (Not in dump)
    inline constexpr uint64_t TeamID = 0x259;
    inline constexpr uint64_t NamePlayer = 0x2EC0B9;
    inline constexpr uint64_t oObjName = 0x68; // Old (Not in dump)

    // --- NEW: DYNAMIC STATS ---
    inline constexpr uint64_t oObjAD = 0x17AC;
    inline constexpr uint64_t oObjArmor = 0x17D4;
    inline constexpr uint64_t oObjMR = 0x17DC;
    inline constexpr uint64_t oObjMoveSpeed = 0x17EC;
    inline constexpr uint64_t oObjAttackRange = 0x17F4;
    inline constexpr uint64_t oObjBoundingRadius = 0x140; // Usually fixed here or 0x480

    // Combat
    inline constexpr uint64_t RangeAttack = 0x264;
    inline constexpr uint64_t AttackDelay = 0x574390;
    inline constexpr uint64_t oGetAttackWindup = 0x1D8F10;

    // HUD & Input
    inline constexpr uint64_t oHudInstance = 0x1DEB028;
    inline constexpr uint64_t oMouseDevice = 0x1E964D8;

    // View & Rendering
    inline constexpr uint64_t ViewPortPtr = 0x1EB6F58; // From ViewPort
    inline constexpr uint64_t oViewMatrixPtr = 0x1EA3660; // From ViewProjectionMatrix

    // Functions
    inline constexpr uint64_t WorldToScreenRVA = 0x12ACD00;
    inline constexpr uint64_t oIssueOrderRVA = 0x2B0070;

    // Additional Utils found in dump
    inline constexpr uint64_t oGetPing = 0x6AB4D0;
    inline constexpr uint64_t oIsMinion = 0x31CC50;
    inline constexpr uint64_t oObjBuffManager = 0x28B8; // Using the last successful value from dump

    inline constexpr uint64_t oObjCharacterData = 0x3110; // Verified Stats offset

    inline constexpr uint64_t oObjAiManager = 0x44A0;
    inline constexpr uint64_t oAiManagerTargetPos = 0xF8; // Next immediate waypoint
    inline constexpr uint64_t oAiManagerServerPos = 0x25C; // Current verified position
}