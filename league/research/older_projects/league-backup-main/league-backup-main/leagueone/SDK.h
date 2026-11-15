#pragma once
#include <Windows.h>
#include <string>
#include <vector>
#include "Offsets.h"
#include "Vector.h"
#include "Logger.h"


inline bool IsValidPtr(void* addr, size_t size = 1) {
    uintptr_t v = (uintptr_t)addr;
    return (v > 0x10000 && v < 0x7FFFFFFFFFFF);
}

// Safely isolated block to prevent object unwinding errors
inline bool SafeCopySDK(void* dst, void* src, size_t size) {
    if (!IsValidPtr(src)) return false;
    __try {
        memcpy(dst, src, size);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

template <typename T>
T SafeReadSDK(uintptr_t addr, T defaultVal) {
    T val = defaultVal;
    SafeCopySDK(&val, (void*)addr, sizeof(T));
    return val;
}

// MSVC 64-bit std::string decoder
inline std::string ReadMSVCString(uintptr_t addr) {
    if (!IsValidPtr((void*)addr)) return "";

    // std::string structure in x64:
    // 0x00: char array[16] OR char* pointer
    // 0x10: size_t length
    // 0x18: size_t capacity

    size_t length = SafeReadSDK<size_t>(addr + 0x10, 0);
    size_t capacity = SafeReadSDK<size_t>(addr + 0x18, 0);

    if (length == 0 || length > 255) return ""; // Sanity check

    char buf[256] = { 0 };

    if (capacity < 16) {
        // Small String Optimization (SSO) - String is stored inline
        SafeCopySDK(buf, (void*)addr, length);
    }
    else {
        // String is stored dynamically via pointer
        uintptr_t strPtr = SafeReadSDK<uintptr_t>(addr, 0);
        if (IsValidPtr((void*)strPtr)) {
            SafeCopySDK(buf, (void*)strPtr, length);
        }
    }

    return std::string(buf);
}

class SpellSlot {
public:
    uintptr_t address;
    SpellSlot(uintptr_t addr) : address(addr) {}

    int GetLevel() {
        if (!IsValidPtr((void*)address)) return 0;
        return SafeReadSDK<int>(address + 0x28, 0);
    }

    bool IsReady(float currentGameTime) {
        if (!IsValidPtr((void*)address)) return false;

        int level = GetLevel();
        if (level < 1) return false; // Spell not learned

        // PERFECTED: Cooldown ReadyTime offset
        float readyTime = SafeReadSDK<float>(address + 0x30, 0.0f);

        // Returns true if the current time has passed the cooldown end time
        return readyTime <= currentGameTime;
    }
};

class GameObject {
public:
    Vector3 GetPos() {
        return SafeReadSDK<Vector3>((uintptr_t)this + Offset::oObjPosition, { 0,0,0 });
    }

    int GetTeam() {
        return (int)SafeReadSDK<unsigned char>((uintptr_t)this + Offset::TeamID, 0);
    }

    int GetMinionTeam() {
        std::string name = GetNameStr();
        if (name.find("_T100") != std::string::npos) return 100;
        if (name.find("_T200") != std::string::npos) return 200;
        if (name.find("Sru") != std::string::npos || name.find("Crab") != std::string::npos) return 300;
        return 0;
    }

    // --- NEW STAT GETTERS ---
    float GetHealth() {
        return SafeReadSDK<float>((uintptr_t)this + Offset::oHealth, 0.0f);
    }

    float GetMaxHealth() {
        return SafeReadSDK<float>((uintptr_t)this + Offset::oMaxHealth, 1.0f); // Fallback to 1 to prevent divide by zero
    }

    float GetArmor() {
        return SafeReadSDK<float>((uintptr_t)this + Offset::oObjArmor, 0.0f);
    }

    float GetMagicResist() {
        return SafeReadSDK<float>((uintptr_t)this + Offset::oObjMR, 0.0f);
    }

    float GetTotalAD() {
        return SafeReadSDK<float>((uintptr_t)this + Offset::oObjAD, 0.0f);
    }

    float GetMoveSpeed() {
        return SafeReadSDK<float>((uintptr_t)this + Offset::oObjMoveSpeed, 0.0f);
    }

    // --- Standard Object Name ---
    std::string GetNameStr() {
        char buf[32] = { 0 };
        uintptr_t nameAddr = (uintptr_t)this + Offset::oObjName;
        uintptr_t ptr = SafeReadSDK<uintptr_t>(nameAddr, 0);
        if (ptr > 0x10000 && IsValidPtr((void*)ptr)) SafeCopySDK(buf, (void*)ptr, 31);
        else SafeCopySDK(buf, (void*)nameAddr, 31);
        return std::string(buf);
    }

    std::string GetChampionName() {
        // 1. Read the CharacterData pointer at 0x220
        uintptr_t charData = SafeReadSDK<uintptr_t>((uintptr_t)this + 0x220, 0);
        if (!charData) return "";

        // 2. Read the MSVC std::string at +0x10 inside CharacterData
        return ReadMSVCString(charData + 0x10);
    }

    // --- MISSLE / SPELL EVENT GETTERS ---
    uintptr_t GetSpellCastInfo() {
        return SafeReadSDK<uintptr_t>((uintptr_t)this + Offset::oCastInfoPtr, 0);
    }

    std::string GetMissileSpellName() {
        uintptr_t castInfo = GetSpellCastInfo();
        if (!IsValidPtr((void*)castInfo)) return "";

        uintptr_t nameAddr = castInfo + Offset::oCastSpellName;
        char buf[64] = { 0 };
        if (SafeCopySDK(buf, (void*)nameAddr, 63)) {
            uintptr_t ptr = *(uintptr_t*)buf;
            if (ptr > 0x100000 && IsValidPtr((void*)ptr)) SafeCopySDK(buf, (void*)ptr, 63);
        }
        return std::string(buf);
    }

    Vector3 GetMissileCurrentPos() {
        uintptr_t castInfo = GetSpellCastInfo();
        if (!IsValidPtr((void*)castInfo)) return { 0,0,0 };
        return SafeReadSDK<Vector3>(castInfo + Offset::oCastCurrentPos, { 0,0,0 });
    }

    Vector3 GetMissileStartPos() {
        uintptr_t castInfo = GetSpellCastInfo();
        if (!IsValidPtr((void*)castInfo)) return { 0,0,0 };
        return SafeReadSDK<Vector3>(castInfo + Offset::oCastStartPos, { 0,0,0 });
    }

    Vector3 GetMissileEndPos() {
        uintptr_t castInfo = GetSpellCastInfo();
        if (!IsValidPtr((void*)castInfo)) return { 0,0,0 };
        return SafeReadSDK<Vector3>(castInfo + Offset::oCastEndPos, { 0,0,0 });
    }

    Vector3 GetMissileClickPos() {
        uintptr_t castInfo = GetSpellCastInfo();
        if (!IsValidPtr((void*)castInfo)) return { 0,0,0 };
        return SafeReadSDK<Vector3>(castInfo + Offset::oCastClickPos, { 0,0,0 });
    }

    SpellSlot GetSpellSlot(int index) {
        // Index 0 = Q, 1 = W, 2 = E, 3 = R
        uintptr_t slotPtr = SafeReadSDK<uintptr_t>((uintptr_t)this + Offset::oSpellSlotArray + (index * 8), 0);
        return SpellSlot(slotPtr);
    }

    float GetBoundingRadius() {
        // Returns ~65.0f for standard champs
        float radius = SafeReadSDK<float>((uintptr_t)this + Offset::oObjBoundingRadius, 65.0f);
        if (radius < 20.0f || radius > 200.0f) return 65.0f; // Failsafe
        return radius;
    }
};