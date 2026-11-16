# Offsets Guide — LeagueOne

This document explains how memory offsets work in the LeagueOne project: what they are, how they're structured, how they're read at runtime, and how new offsets are discovered when the game patches.

---

## 1. What Are Offsets?

Offsets are hard-coded numeric constants that represent the **byte distance** from a known base address to a specific piece of data inside the running League of Legends process. The game stores all its state (player positions, health, spells, missiles, etc.) in C++ objects in memory. Since the project runs as an internal DLL (injected into the game process), it can read those objects directly — but it needs to know *where* each field lives inside each object. That's what offsets provide.

All offsets are defined in **`Offsets.h`** as `inline constexpr uint64_t` values inside the `Offset` namespace:

```cpp
namespace Offset {
    inline constexpr uint64_t oLocalPlayer = 0x1E257E0;
    inline constexpr uint64_t oHealth      = 0x1080;
    // ...
}
```

They are compile-time constants — the values are baked into the binary. There is no runtime file or config that loads them.

---

## 2. Two Types of Offsets

### 2a. Base-Relative Offsets (Global Pointers)

These are offsets from the **game module base address** (`GetModuleHandle(NULL)`). They point to global manager objects or singleton values that the game engine maintains.

| Offset | Name | What It Points To |
|--------|------|-------------------|
| `0x1E257E0` | `oLocalPlayer` | Pointer to your champion's `GameObject` |
| `0x1DEAEE0` | `oHeroList` | Pointer to the hero manager (all champions) |
| `0x1DEED88` | `oMinionList` | Pointer to the minion manager |
| `0x1DEEDA0` | `oMissileList` | Pointer to the missile/projectile manager |
| `0x1DF9650` | `oGametime` | Float — current game clock in seconds |
| `0x1DEB028` | `oHudInstance` | Pointer to the HUD manager (camera, input) |
| `0x1EA3660` | `oViewMatrixPtr` | View and projection matrices (for W2S) |

**How they're used at runtime:**

```cpp
uintptr_t base = (uintptr_t)GetModuleHandle(NULL);
uintptr_t localPlayer = SafeReadSDK<uintptr_t>(base + Offset::oLocalPlayer, 0);
float gameTime = SafeReadSDK<float>(base + Offset::oGametime, 0.0f);
```

The module base address changes every time the game launches (ASLR), but the offset from base to data stays the same for a given game patch.

### 2b. Object-Relative Offsets (Struct Member Offsets)

Once you have a pointer to a game object (like a champion or minion), these offsets tell you where specific fields live inside that object's memory layout.

| Offset | Name | Type | Description |
|--------|------|------|-------------|
| `0x25C` | `oObjPosition` | `Vector3` | XYZ world position |
| `0x1080` | `oHealth` | `float` | Current HP |
| `0x10A8` | `oMaxHealth` | `float` | Max HP |
| `0x259` | `TeamID` | `byte` | Team identifier |
| `0xCC` | `oObjNetId` | `int` | Network ID |
| `0x17AC` | `oObjAD` | `float` | Attack damage |
| `0x17D4` | `oObjArmor` | `float` | Armor |
| `0x17DC` | `oObjMR` | `float` | Magic resist |
| `0x17EC` | `oObjMoveSpeed` | `float` | Movement speed |
| `0x17F4` | `oObjAttackRange` | `float` | Attack range |
| `0x140` | `oObjBoundingRadius` | `float` | Hitbox radius |
| `0x250` | `oDead` | - | Death flag |
| `0xED0` | `oTargetable` | `byte` | Whether unit can be targeted |
| `0x300` | `oVisibility` | - | Fog of war visibility |

**How they're used:**

```cpp
// Inside the GameObject class (SDK.h), `this` is the object pointer
float GetHealth() {
    return SafeReadSDK<float>((uintptr_t)this + Offset::oHealth, 0.0f);
}

Vector3 GetPos() {
    return SafeReadSDK<Vector3>((uintptr_t)this + Offset::oObjPosition, {0,0,0});
}
```

---

## 3. Manager Structures (How Lists Are Read)

The game stores collections of objects (heroes, minions, missiles) in **manager objects**. Each manager follows a common pattern:

```
Manager Pointer (base-relative)
  ├── +0x08: Pointer to an array of object pointers
  └── +0x10: Integer count of objects in the array
```

The array itself is a flat block of 8-byte pointers (`uintptr_t*`), one per object.

### Hero Manager

```cpp
uintptr_t heroMgr = SafeReadSDK<uintptr_t>(base + Offset::oHeroList, 0);  // Manager pointer
uintptr_t arr     = SafeReadSDK<uintptr_t>(heroMgr + 0x08, 0);            // Array of hero pointers
int count         = SafeReadSDK<int>(heroMgr + 0x10, 0);                   // Number of heroes

for (int i = 0; i < count; i++) {
    uintptr_t obj = SafeReadSDK<uintptr_t>(arr + (i * 8), 0);  // Each entry is 8 bytes
    // obj is now a pointer to a GameObject (hero)
}
```

### Minion Manager

The minion manager uses slightly different sub-offsets:

```cpp
uintptr_t minionMgr = SafeReadSDK<uintptr_t>(base + Offset::oMinionList, 0);
uintptr_t arr       = SafeReadSDK<uintptr_t>(minionMgr + Offset::LaneMinionArray, 0);  // +0x68
int count           = SafeReadSDK<int>(minionMgr + Offset::LaneMinionCount, 0);         // +0x70
```

### Missile Manager

Same pattern as the hero manager (`+0x08` for array, `+0x10` for count):

```cpp
uintptr_t missileMgr = SafeReadSDK<uintptr_t>(base + Offset::oMissileList, 0);
uintptr_t arr        = SafeReadSDK<uintptr_t>(missileMgr + 0x08, 0);
int count            = SafeReadSDK<int>(missileMgr + 0x10, 0);
```

All three managers are read every frame in `GameReader::Update()`.

---

## 4. Pointer Chains (Nested Offsets)

Some data isn't stored directly in the object — it requires following a chain of pointers, where each step dereferences a pointer and then applies another offset.

### Missile Spell Data

A missile `GameObject` stores its cast info behind a pointer:

```
Missile GameObject
  └── +0x28 (oCastInfoPtr) ──> SpellCastInfo struct
        ├── +0x68  (oCastSpellName)  : char[] — spell name (e.g. "EzrealQ")
        ├── +0xF8  (oCastCurrentPos) : Vector3 — current missile position
        ├── +0x388 (oCastStartPos)   : Vector3 — where it was cast from
        ├── +0x394 (oCastEndPos)     : Vector3 — max range trajectory endpoint
        └── +0x3A0 (oCastClickPos)   : Vector3 — where the mouse was clicked
```

In code:

```cpp
uintptr_t castInfo = SafeReadSDK<uintptr_t>((uintptr_t)this + Offset::oCastInfoPtr, 0);
Vector3 startPos   = SafeReadSDK<Vector3>(castInfo + Offset::oCastStartPos, {0,0,0});
```

### Spell Slots

Your champion's Q/W/E/R abilities are accessed through an array of spell slot pointers:

```
LocalPlayer
  └── +0x3BC8 (oSpellSlotArray)
        ├── [0] → SpellSlot (Q)
        ├── [1] → SpellSlot (W)
        ├── [2] → SpellSlot (E)
        └── [3] → SpellSlot (R)
              Each SpellSlot:
                ├── +0x28  : int    — spell level
                ├── +0x30  : float  — cooldown ready time
                ├── +0x120 (oSpellSlotInput) → SpellInput struct
                └── +0x128 (oSpellSlotInfo)  → SpellInfo struct
```

In code:

```cpp
uintptr_t slotPtr = SafeReadSDK<uintptr_t>(
    (uintptr_t)this + Offset::oSpellSlotArray + (index * 8), 0
);
int level = SafeReadSDK<int>(slotPtr + 0x28, 0);
float readyTime = SafeReadSDK<float>(slotPtr + 0x30, 0.0f);
```

### HUD / Camera Chain

Zoom hack follows a multi-step pointer chain:

```
base + oHudInstance (0x1DEB028) ──> HudManager
  ├── +0x1E8 ──> Camera Object A
  │     └── +0x28 : float — max zoom
  └── +0x18  ──> Camera Object B
        ├── +0x320 : float — max zoom constraint
        ├── +0x324 : float — secondary zoom constraint
        └── +0x31C : float — current zoom level
```

### AI Manager Chain

Movement prediction uses the AI Manager, accessed from any champion:

```
GameObject
  └── +0x44A0 (oObjAiManager) ──> AIManager
        ├── +0xF8  (oAiManagerTargetPos)  : Vector3 — next immediate waypoint
        └── +0x25C (oAiManagerServerPos)  : Vector3 — server-verified position
```

### View Matrices

World-to-screen projection reads two consecutive 4x4 matrices:

```
base + oViewMatrixPtr (0x1EA3660) : Matrix4x4 — View matrix (64 bytes)
base + oViewMatrixPtr + 0x40      : Matrix4x4 — Projection matrix (64 bytes)
```

These are multiplied together in `Utils::WorldToScreen()` to project 3D world coordinates to 2D screen coordinates.

---

## 5. Function RVAs (Relative Virtual Addresses)

Some offsets point to executable **functions** rather than data. These are called RVAs (offsets from module base to a function entry point).

| Offset | Name | Purpose |
|--------|------|---------|
| `0x275650` | `oCastSpell` | Function to cast a spell |
| `0x12ACD00` | `WorldToScreenRVA` | Native W2S projection function |
| `0x2B0070` | `oIssueOrderRVA` | Movement/attack command function |
| `0x574390` | `AttackDelay` | Function that returns attack delay |
| `0x1D8F10` | `oGetAttackWindup` | Function that returns attack windup time |
| `0x6AB4D0` | `oGetPing` | Function that returns network ping |
| `0x31CC50` | `oIsMinion` | Function that checks if object is a minion |
| `0x2AC7A0` | `LaneMinionType` | Function that returns minion type |

These are used to either call game functions directly or to hook them for interception.

---

## 6. Safe Memory Reading

All memory reads go through a protected helper (`SDK.h`) to avoid crashes from invalid pointers:

```cpp
// Step 1: Validate the pointer is in a sane range
bool IsValidPtr(void* addr, size_t size = 1) {
    uintptr_t v = (uintptr_t)addr;
    return (v > 0x10000 && v < 0x7FFFFFFFFFFF);
}

// Step 2: Copy memory inside a SEH (Structured Exception Handler) block
bool SafeCopySDK(void* dst, void* src, size_t size) {
    if (!IsValidPtr(src)) return false;
    __try {
        memcpy(dst, src, size);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Step 3: Templated read that returns a default value on failure
template <typename T>
T SafeReadSDK(uintptr_t addr, T defaultVal) {
    T val = defaultVal;
    SafeCopySDK(&val, (void*)addr, sizeof(T));
    return val;
}
```

**Why this matters:**
- Game objects can be freed at any time (champion dies, missile expires)
- Pointers can become dangling between frames
- Without SEH protection, a bad read would crash the entire game
- The `IsValidPtr` check filters out obviously invalid addresses before even attempting a read

---

## 7. How New Offsets Are Discovered

When Riot patches the game, struct layouts and global addresses change. The `Debugger.cpp` module contains multiple specialized scanners for rediscovering offsets:

### 7a. Pattern Scanner

Searches for a known sequence of bytes (a function's "signature") in the game's code section. Used for finding function addresses like `oCastSpell`:

```cpp
// The first bytes of the CastSpell function
std::vector<int> pattern = {
    0x40, 0x53, 0x55, 0x56, 0x57,
    0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57,
    0x48, 0x81, 0xEC
};
uintptr_t addr = PatternScanner::FindPattern(base, 0x1000000, pattern);
// addr - base = new oCastSpell offset
```

Wildcards (`-1`) can be used for bytes that change between patches.

### 7b. Deep Spawn Dump (`CatchAnySpawnThread`)

Monitors the missile and minion managers in a tight loop. When a new object appears (you fire a spell), it captures the entire object's memory and dumps it to `DeepSpawnDump.txt`, scanning every 8-byte field for:
- **Pointers** — follows them one level deep
- **Strings** — spell names, SFX identifiers
- **Coordinates** — Vector3 values near the player

This is how missile offsets like `oCastStartPos`, `oCastEndPos`, and `oCastClickPos` were discovered.

### 7c. Smart Stat Scanner (`FindStatOffsets`)

Cross-references known stat values from League's local HTTP API (`GameAPI`) with memory:
1. Reads your exact Attack Damage, Armor, etc. from the API
2. Scans the entire `LocalPlayer` object (shallow: directly in object; deep: following every pointer one level) for matching float values
3. Outputs matches to `StatOffsetsDump.txt`

This is how `oObjAD` (0x17AC), `oObjArmor` (0x17D4), `oObjMR` (0x17DC), and `oObjMoveSpeed` (0x17EC) were found.

### 7d. Behavioral / Physics Scanner (`RunBehavioralScanner`)

Uses player movement to find the AI Manager:
1. Takes a position snapshot while you're walking
2. Waits 0.5 seconds
3. Takes a second snapshot
4. Calculates your real speed from `distance / time`
5. Scans every pointer in the player object for sub-objects containing both a matching speed float and a matching position vector

If both your move speed and position appear inside the same sub-object, it's almost certainly the AI Manager.

### 7e. HP Bar Scanner (`FindHpBarOffset` / `FindHpBarDelta`)

Finds the screen-space HP bar position offset using a two-snapshot technique:
1. **Snapshot 1**: Record screen position of an enemy's feet and dump all memory
2. The user pans the camera
3. **Snapshot 2**: Record the new screen position
4. Calculate the expected screen-space shift (`deltaX`, `deltaY`)
5. Find any float pair in memory that shifted by exactly the same amount — that's the HP bar screen coordinate

### 7f. Navigation Array Cracker (`TraceNavArray`)

Finds the waypoint array inside the AI Manager, including support for XOR-encrypted pointers:
1. For each pointer in the AI Manager, tries three decryption methods: plain, `A ^ ~B`, `B ^ ~A`
2. If the decrypted pointer leads to a `Vector3` near the player, it's a waypoint
3. Verifies by checking if consecutive waypoints form a valid path

---

## 8. Complete Offset Hierarchy

```
Game Module Base (GetModuleHandle(NULL))
│
├── oLocalPlayer (0x1E257E0) ──> GameObject
│   ├── oObjPosition (0x25C)      : Vector3
│   ├── TeamID (0x259)            : byte
│   ├── oObjNetId (0xCC)          : int
│   ├── oHealth (0x1080)          : float
│   ├── oMaxHealth (0x10A8)       : float
│   ├── oDead (0x250)             : byte
│   ├── oTargetable (0xED0)       : byte
│   ├── oVisibility (0x300)       : byte
│   ├── oObjBoundingRadius (0x140): float
│   ├── oObjName (0x68)           : char* / inline
│   ├── oObjAD (0x17AC)           : float
│   ├── oObjArmor (0x17D4)        : float
│   ├── oObjMR (0x17DC)           : float
│   ├── oObjMoveSpeed (0x17EC)    : float
│   ├── oObjAttackRange (0x17F4)  : float
│   ├── oObjCharacterData (0x3110): ptr → CharacterData
│   ├── oObjBuffManager (0x28B8)  : ptr → BuffManager
│   ├── oObjSpellBook (0x30E8)    : ptr → SpellBook
│   ├── oSpellSlotArray (0x3BC8)  : ptr[] → SpellSlot[0..3]
│   │   └── SpellSlot
│   │       ├── +0x28 : int (level)
│   │       ├── +0x30 : float (ready time)
│   │       ├── oSpellSlotInput (0x120) → SpellInput
│   │       └── oSpellSlotInfo (0x128)  → SpellInfo
│   └── oObjAiManager (0x44A0) ──> AIManager
│       ├── oAiManagerTargetPos (0xF8) : Vector3
│       └── oAiManagerServerPos (0x25C): Vector3
│
├── oHeroList (0x1DEAEE0) ──> HeroManager
│   ├── +0x08 : uintptr_t* (array of GameObject pointers)
│   └── +0x10 : int (count)
│
├── oMinionList (0x1DEED88) ──> MinionManager
│   ├── LaneMinionArray (0x68) : uintptr_t* (array)
│   └── LaneMinionCount (0x70) : int (count)
│
├── oMissileList (0x1DEEDA0) ──> MissileManager
│   ├── +0x08 : uintptr_t* (array)
│   └── +0x10 : int (count)
│       └── Each Missile GameObject
│           └── oCastInfoPtr (0x28) ──> SpellCastInfo
│               ├── oCastSpellName (0x68)  : char[]
│               ├── oCastCurrentPos (0xF8) : Vector3
│               ├── oCastStartPos (0x388)  : Vector3
│               ├── oCastEndPos (0x394)    : Vector3
│               └── oCastClickPos (0x3A0)  : Vector3
│
├── oGametime (0x1DF9650) : float
│
├── oHudInstance (0x1DEB028) ──> HudManager
│   ├── +0x1E8 ──> CameraA (+0x28 = max zoom)
│   └── +0x18  ──> CameraB (+0x320/0x324 = zoom constraints)
│
├── oViewMatrixPtr (0x1EA3660) : Matrix4x4 (view)
│   └── +0x40 : Matrix4x4 (projection)
│
└── oMouseDevice (0x1E964D8) ──> Mouse input device
```

---

## 9. Silent Aim / Cursor Offsets

These offsets are used to manipulate where the game thinks the cursor is pointing, enabling "silent aim" (casting spells at a target without moving the visible mouse):

```
HudInstance
  └── +0x28 (oHudCursorTargetLogic) ──> CursorTarget
        ├── +0x28 (oHudCursorPos1) : Vector3 — 3D cursor position (set 1)
        ├── +0x34 (oHudCursorPos2) : Vector3 — 3D cursor position (set 2)
        └── +0x40 (oHudCursorPos3) : Vector3 — 3D cursor position (set 3)

HudInstance
  └── +0x48 (oHudCursorPos2DPtr) ──> 2D cursor struct
        └── +0x78 (oHudCursorPos2DOffset) : Vector2 — screen-space cursor
```

---

## 10. Offset Annotations in the Code

Comments in `Offsets.h` provide important context about each offset's status:

| Annotation | Meaning |
|-----------|---------|
| `// Old (Not in dump)` | This offset was not found in recent memory dumps; it may have been from a previous game version and could be stale |
| `// Confirmed 0xXX` | Verified through multiple methods |
| `// Found via Scanner` | Discovered using one of the automated scanners in Debugger.cpp |
| `// Verified Pointer` | Confirmed to be a valid, dereferenceable pointer |
| `// Perfected` | Thoroughly tested and confirmed working |
| `// Function RVA` | This is a function address, not a data member offset |
| `// NEW:` | Recently discovered or added |

---

## 11. What Happens When the Game Patches

When Riot releases a game update:

1. **All base-relative offsets** (oLocalPlayer, oHeroList, etc.) shift to new addresses
2. **Some struct member offsets** change when Riot adds/removes/reorders fields in their classes
3. **Function RVAs** move when code is recompiled

**Update workflow:**
1. Use the pattern scanner (`FindCastSpell`, `FindCastSpellPatterns`) to relocate function addresses
2. Use the deep spawn dump to verify missile struct layout
3. Use the smart stat scanner to re-find stat offsets like AD/Armor/MR
4. Use the behavioral scanner to re-find the AI Manager
5. Update `Offsets.h` with the new values and recompile
