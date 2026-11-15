# Game Memory & SDK

This document explains how LeagueOne reads data from the running game -- the foundation that everything else is built on.

---

## 1. The Problem

League of Legends stores all game state (player positions, health, spells, missiles, etc.) in C++ objects in its process memory. Since our DLL is injected into the same process, we can read these objects directly using plain pointer dereferences. But there are two challenges:

1. **Offsets change with patches** -- Riot recompiles the game every patch, shifting struct layouts and global addresses
2. **Pointers go stale** -- Game objects get created and destroyed constantly (champions die, missiles expire), so a pointer that was valid one frame might be invalid the next

The SDK layer in `SDK.h` solves challenge #2. The offset system in `Offsets.h` addresses challenge #1 (with help from the scanner tools in `Debugger.cpp`).

---

## 2. Safe Memory Reading (SDK.h)

Every memory read in the project goes through a three-layer safety system:

### Layer 1: Pointer Validation

```cpp
// SDK.h:10
inline bool IsValidPtr(void* addr, size_t size = 1) {
    uintptr_t v = (uintptr_t)addr;
    return (v > 0x10000 && v < 0x7FFFFFFFFFFF);
}
```

This checks if an address is in a plausible range:
- Below `0x10000`: NULL or near-NULL pointers (definitely invalid)
- Above `0x7FFFFFFFFFFF`: Beyond the user-mode address space on x64 Windows

This is a fast first filter that catches the most common bad pointers.

### Layer 2: SEH-Protected Copy

```cpp
// SDK.h:16
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
```

Even if a pointer passes the range check, the memory it points to might be freed or protected. The `__try/__except` block is a **Structured Exception Handler (SEH)** -- a Windows mechanism that catches hardware exceptions like access violations. If the `memcpy` triggers a crash, the exception handler catches it and returns `false` instead of crashing the entire game.

**Why not `VirtualQuery` or `IsBadReadPtr`?** Both are slower and have their own reliability issues. SEH is the gold standard for this use case because it handles every possible failure mode (freed pages, guard pages, no-access regions) with zero false negatives.

### Layer 3: Templated Safe Read

```cpp
// SDK.h:27
template <typename T>
T SafeReadSDK(uintptr_t addr, T defaultVal) {
    T val = defaultVal;
    SafeCopySDK(&val, (void*)addr, sizeof(T));
    return val;
}
```

This is the function you'll see used everywhere in the codebase. It reads a value of any type from a memory address and returns a default if the read fails. Examples:

```cpp
float health = SafeReadSDK<float>(objAddr + Offset::oHealth, 0.0f);
int team = SafeReadSDK<int>(objAddr + Offset::TeamID, 0);
uintptr_t ptr = SafeReadSDK<uintptr_t>(base + Offset::oLocalPlayer, 0);
```

---

## 3. The Offset System (Offsets.h)

Offsets are compile-time constants in the `Offset` namespace. There are two types:

### Base-Relative Offsets (Global Pointers)

These are distances from the game's module base address to global singleton objects:

```cpp
namespace Offset {
    inline constexpr uint64_t oLocalPlayer  = 0x1E257E0;  // -> your champion
    inline constexpr uint64_t oHeroList     = 0x1DEAEE0;  // -> hero manager
    inline constexpr uint64_t oMinionList   = 0x1DEED88;  // -> minion manager
    inline constexpr uint64_t oMissileList  = 0x1DEEDA0;  // -> missile manager
    inline constexpr uint64_t oGametime     = 0x1DF9650;  // -> float game clock
    inline constexpr uint64_t oHudInstance  = 0x1DEB028;  // -> HUD / camera
    inline constexpr uint64_t oViewMatrixPtr = 0x1EA3660; // -> view/proj matrices
}
```

Usage:
```cpp
uintptr_t base = (uintptr_t)GetModuleHandle(NULL);  // Game's base address (changes per launch)
uintptr_t localPlayer = SafeReadSDK<uintptr_t>(base + Offset::oLocalPlayer, 0);
```

The base address changes every launch due to ASLR (Address Space Layout Randomization), but the offset from base to each global remains the same for a given game patch.

### Object-Relative Offsets (Struct Member Offsets)

Once you have a pointer to a game object, these tell you where each field lives inside that object:

```cpp
namespace Offset {
    inline constexpr uint64_t oObjPosition      = 0x25C;   // Vector3
    inline constexpr uint64_t oHealth            = 0x1080;  // float
    inline constexpr uint64_t oMaxHealth         = 0x10A8;  // float
    inline constexpr uint64_t TeamID             = 0x259;   // byte
    inline constexpr uint64_t oObjAD             = 0x17AC;  // float
    inline constexpr uint64_t oObjArmor          = 0x17D4;  // float
    inline constexpr uint64_t oObjMR             = 0x17DC;  // float
    inline constexpr uint64_t oObjMoveSpeed      = 0x17EC;  // float
    inline constexpr uint64_t oObjAttackRange    = 0x17F4;  // float
    inline constexpr uint64_t oObjBoundingRadius = 0x140;   // float
}
```

Usage:
```cpp
float health = SafeReadSDK<float>(objAddr + Offset::oHealth, 0.0f);
Vector3 pos = SafeReadSDK<Vector3>(objAddr + Offset::oObjPosition, {0,0,0});
```

For a complete offset hierarchy diagram, see [OFFSETS_GUIDE.md](./OFFSETS_GUIDE.md).

---

## 4. The GameObject Class (SDK.h)

The `GameObject` class wraps a game object pointer and provides clean getter methods. It doesn't store any data itself -- the `this` pointer IS the game object's address in memory:

```cpp
// SDK.h:89
class GameObject {
public:
    Vector3 GetPos() {
        return SafeReadSDK<Vector3>((uintptr_t)this + Offset::oObjPosition, {0,0,0});
    }

    float GetHealth() {
        return SafeReadSDK<float>((uintptr_t)this + Offset::oHealth, 0.0f);
    }

    int GetTeam() {
        return (int)SafeReadSDK<unsigned char>((uintptr_t)this + Offset::TeamID, 0);
    }

    std::string GetChampionName() {
        uintptr_t charData = SafeReadSDK<uintptr_t>((uintptr_t)this + 0x220, 0);
        if (!charData) return "";
        return ReadMSVCString(charData + 0x10);
    }

    SpellSlot GetSpellSlot(int index) {
        uintptr_t slotPtr = SafeReadSDK<uintptr_t>(
            (uintptr_t)this + Offset::oSpellSlotArray + (index * 8), 0);
        return SpellSlot(slotPtr);
    }
    // ... more getters
};
```

**Important concept**: When you see `(GameObject*)someAddress`, the code is not creating a C++ object. It's reinterpreting a raw memory address as a `GameObject*`, so method calls read from that address. The game's actual object at that address has a completely different C++ class definition -- we just know where certain fields are via offsets.

---

## 5. Reading Strings (SDK.h)

MSVC's `std::string` uses **Small String Optimization (SSO)**. Strings shorter than 16 characters are stored inline in the string object itself. Longer strings use a heap-allocated buffer. The `ReadMSVCString` function handles both cases:

```cpp
// SDK.h:35
inline std::string ReadMSVCString(uintptr_t addr) {
    size_t length = SafeReadSDK<size_t>(addr + 0x10, 0);   // +0x10 = length
    size_t capacity = SafeReadSDK<size_t>(addr + 0x18, 0);  // +0x18 = capacity

    if (length == 0 || length > 255) return "";

    char buf[256] = { 0 };

    if (capacity < 16) {
        // SSO: string data is at the object address itself
        SafeCopySDK(buf, (void*)addr, length);
    } else {
        // Heap-allocated: first 8 bytes are a pointer to the string data
        uintptr_t strPtr = SafeReadSDK<uintptr_t>(addr, 0);
        if (IsValidPtr((void*)strPtr)) {
            SafeCopySDK(buf, (void*)strPtr, length);
        }
    }

    return std::string(buf);
}
```

This is used to read champion names, spell names, and other string data from game objects.

---

## 6. The SpellSlot Class (SDK.h)

Champions have 4 spell slots (Q, W, E, R) accessed through an array of pointers:

```cpp
// SDK.h:65
class SpellSlot {
public:
    uintptr_t address;  // Pointer to the spell slot object in game memory
    SpellSlot(uintptr_t addr) : address(addr) {}

    int GetLevel() {
        return SafeReadSDK<int>(address + 0x28, 0);
    }

    bool IsReady(float currentGameTime) {
        int level = GetLevel();
        if (level < 1) return false;          // Spell not learned

        float readyTime = SafeReadSDK<float>(address + 0x30, 0.0f);
        return readyTime <= currentGameTime;  // Ready if cooldown has passed
    }
};
```

Usage:
```cpp
GameObject* local = (GameObject*)localAddr;
SpellSlot qSlot = local->GetSpellSlot(0);  // Q
SpellSlot wSlot = local->GetSpellSlot(1);  // W
SpellSlot eSlot = local->GetSpellSlot(2);  // E
SpellSlot rSlot = local->GetSpellSlot(3);  // R

if (qSlot.IsReady(gameTime)) {
    // Q is off cooldown and has been learned
}
```

---

## 7. Manager Structures (GameReader.cpp)

The game stores collections of objects in **manager** objects. Each manager holds a pointer to an array and a count:

### Hero Manager

```cpp
// GameReader.cpp:29
uintptr_t heroMgr = SafeReadSDK<uintptr_t>(base + Offset::oHeroList, 0);
uintptr_t arr = SafeReadSDK<uintptr_t>(heroMgr + 0x08, 0);   // Array of pointers
int count = SafeReadSDK<int>(heroMgr + 0x10, 0);               // Number of heroes

for (int i = 0; i < count; i++) {
    uintptr_t obj = SafeReadSDK<uintptr_t>(arr + (i * 8), 0);  // Each pointer is 8 bytes
    if (obj && obj != localAddr)
        enemies.push_back((GameObject*)obj);
}
```

### Minion Manager

Uses different sub-offsets (`+0x68` for array, `+0x70` for count):

```cpp
// GameReader.cpp:42
uintptr_t minionMgr = SafeReadSDK<uintptr_t>(base + Offset::oMinionList, 0);
uintptr_t arr = SafeReadSDK<uintptr_t>(minionMgr + Offset::LaneMinionArray, 0);  // +0x68
int count = SafeReadSDK<int>(minionMgr + Offset::LaneMinionCount, 0);             // +0x70
```

### Missile Manager

Same layout as the hero manager:

```cpp
// GameReader.cpp:59
uintptr_t missileMgr = SafeReadSDK<uintptr_t>(base + Offset::oMissileList, 0);
uintptr_t arr = SafeReadSDK<uintptr_t>(missileMgr + 0x08, 0);
int count = SafeReadSDK<int>(missileMgr + 0x10, 0);
```

All three managers are read every frame by `GameReader::Update()`. The results are cached in static vectors and accessed via `GameReader::GetEnemies()`, `GameReader::GetMinions()`, and `GameReader::GetMissiles()`.

---

## 8. Pointer Chains

Some data requires following multiple pointers in sequence. Each step dereferences a pointer and adds another offset.

### Missile Spell Data

```
Missile GameObject
  +0x28 (oCastInfoPtr) --> SpellCastInfo struct
    +0x68  (oCastSpellName)  : char[] -- spell name
    +0xF8  (oCastCurrentPos) : Vector3 -- current position
    +0x388 (oCastStartPos)   : Vector3 -- origin
    +0x394 (oCastEndPos)     : Vector3 -- destination
    +0x3A0 (oCastClickPos)   : Vector3 -- where mouse was clicked
```

In code:
```cpp
uintptr_t castInfo = SafeReadSDK<uintptr_t>((uintptr_t)this + 0x28, 0);
Vector3 startPos = SafeReadSDK<Vector3>(castInfo + 0x388, {0,0,0});
```

### AI Manager

Used for movement prediction:

```
GameObject
  +0x44A0 (oObjAiManager) --> AIManager
    +0xF8  (oAiManagerTargetPos)  : Vector3 -- next waypoint
    +0x25C (oAiManagerServerPos)  : Vector3 -- server position
```

### View Matrices

Used for WorldToScreen projection:

```
base + 0x1EA3660 : Matrix4x4 -- View matrix (64 bytes)
base + 0x1EA36A0 : Matrix4x4 -- Projection matrix (64 bytes)
```

---

## 9. The GameAPI -- Live Client Data

Some stats (attack speed, ability power, penetration values) are difficult to read reliably from memory because they involve complex calculations the game performs internally. Instead, `GameAPI.cpp` polls League's **local HTTP API**:

```
https://127.0.0.1:2999/liveclientdata/activeplayer
```

This is an official Riot API that returns a JSON blob with your champion's current stats. The `StatsFetcherLoop` runs on a background thread, polls every 100ms, and updates atomic float variables:

```cpp
std::atomic<float> CurrentAttackSpeed = 0.625f;
std::atomic<float> CurrentRange = 550.0f;
std::atomic<float> CurrentAttackDamage = 0.0f;
std::atomic<float> CurrentAbilityPower = 0.0f;
// ... and more
```

These atomics can be safely read from any thread without synchronization.

### Ping Reading

Ping is read directly from memory using pattern scanning:

```cpp
int GetPing() {
    uintptr_t leaInstr = PatternScanner::FindPattern(base, 0x5000000, {
        0x48, 0x8D, 0x05, -1, -1, -1, -1,
        0x48, 0x81, 0xC1, 0xC8, 0x00, 0x00, 0x00
    });
    // Decode the LEA instruction's RIP-relative offset
    int32_t ripOff = SafeRead(leaInstr + 3);
    pStaticAddress = leaInstr + 7 + ripOff;
    // Read ping value
    return SafeRead<int>(pStaticAddress + 0x1B8) * 2;  // One-way -> round-trip
}
```

This finds a known instruction in the game code, decodes its operand to find the ping data structure, and reads the value.

---

## 10. WorldToScreen Projection (Utils.cpp)

Converting 3D world coordinates to 2D screen pixels:

```cpp
bool WorldToScreen(const Vector3& pos, Vector2& out) {
    // 1. Read view and projection matrices from game memory
    Matrix4x4 view = SafeReadSDK<Matrix4x4>(base + Offset::oViewMatrixPtr, {});
    Matrix4x4 proj = SafeReadSDK<Matrix4x4>(base + Offset::oViewMatrixPtr + 0x40, {});

    // 2. Multiply them together
    Matrix4x4 vp = Multiply(view, proj);

    // 3. Transform the world position
    float cX = pos.x * vp.m[0] + pos.y * vp.m[4] + pos.z * vp.m[8] + vp.m[12];
    float cY = pos.x * vp.m[1] + pos.y * vp.m[5] + pos.z * vp.m[9] + vp.m[13];
    float cW = pos.x * vp.m[3] + pos.y * vp.m[7] + pos.z * vp.m[11] + vp.m[15];

    if (cW < 0.1f) return false;  // Behind camera

    // 4. Perspective divide and screen mapping
    float M = 1.0f / cW;
    float ndc_x = cX * M;
    float ndc_y = cY * M;
    out.x = (ScreenWidth / 2 * ndc_x) + (ndc_x + ScreenWidth / 2);
    out.y = -(ScreenHeight / 2 * ndc_y) + (ndc_y + ScreenHeight / 2);
    return true;
}
```

This is the standard 3D-to-2D projection pipeline: multiply the position by the view-projection matrix, do a perspective divide, and map from normalized device coordinates to screen pixels.

---

## 11. Pattern Scanner (PatternScanner.cpp)

When function addresses need to be found dynamically (because they move with patches), the pattern scanner searches for a known byte sequence:

```cpp
uintptr_t FindPattern(uintptr_t start, size_t searchSize, std::vector<int> pattern) {
    uintptr_t end = start + searchSize;
    for (uintptr_t addr = start; addr < end; addr++) {
        MEMORY_BASIC_INFORMATION mbi;
        VirtualQuery((void*)addr, &mbi, sizeof(mbi));

        // Skip non-executable and non-readable memory
        if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) {
            addr = (uintptr_t)mbi.BaseAddress + mbi.RegionSize - 1;
            continue;
        }

        // Compare each byte (value of -1 means wildcard)
        bool found = true;
        for (size_t j = 0; j < pattern.size(); j++) {
            if (pattern[j] != -1 && *(unsigned char*)(addr + j) != (unsigned char)pattern[j]) {
                found = false;
                break;
            }
        }
        if (found) return addr;
    }
    return 0;
}
```

Patterns are specified as vectors of integers where `-1` means "match any byte" (wildcard). This is also called AOB scanning (Array of Bytes).
