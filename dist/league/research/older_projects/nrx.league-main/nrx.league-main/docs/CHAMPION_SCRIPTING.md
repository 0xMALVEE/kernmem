# Champion Scripting Guide

This document explains how to write a new champion script for LeagueOne. It uses the existing Ezreal script as a reference and walks through creating a new one from scratch.

---

## 1. The IChampion Interface

Every champion script implements the `IChampion` interface defined in `IChampion.h`:

```cpp
// IChampion.h
#pragma once
#include <string>

class IChampion {
public:
    virtual ~IChampion() = default;
    virtual std::string GetName() = 0;    // Must match the in-game champion name
    virtual void Initialize() = 0;         // Called once when the module is loaded
    virtual void OnTick() = 0;             // Called every frame during gameplay
    virtual void OnDraw() = 0;             // Called every frame for custom drawing
    virtual void OnMenu() = 0;             // Called every frame when the menu is open
};
```

| Method | When It's Called | What To Do Here |
|--------|-----------------|-----------------|
| `GetName()` | During module matching | Return the exact champion name (e.g., `"Ezreal"`, `"Jinx"`, `"Lux"`) |
| `Initialize()` | Once, when your champion is detected | Set up any initial state, log a message |
| `OnTick()` | Every frame, after game state is updated | Run combo logic, cast spells |
| `OnDraw()` | Every frame, during the rendering phase | Draw custom overlays (damage bars, indicators) |
| `OnMenu()` | Every frame when the UI menu is visible | Render ImGui controls for your script's settings |

---

## 2. Creating a New Champion Script

Let's create a script for **Lux** as an example.

### Step 1: Create the Header (Lux.h)

```cpp
// Lux.h
#pragma once
#include "IChampion.h"

class Lux : public IChampion {
public:
    std::string GetName() override { return "Lux"; }
    void Initialize() override;
    void OnTick() override;
    void OnDraw() override;
    void OnMenu() override;

private:
    // Settings (toggles for the menu)
    bool useQInCombo = true;
    bool useEInCombo = true;
    bool useRInCombo = true;

    // Cooldown throttles (prevent spamming)
    float lastQCastTime = 0.0f;
    float lastECastTime = 0.0f;
    float lastRCastTime = 0.0f;
};
```

### Step 2: Create the Implementation (Lux.cpp)

```cpp
// Lux.cpp
#include "Lux.h"
#include "imgui.h"
#include "GameReader.h"
#include "TargetSelector.h"
#include "Input.h"
#include "Settings.h"
#include "Utils.h"
#include "Offsets.h"
#include "Logger.h"
#include "GameAPI.h"
#include "Prediction.h"

void Lux::Initialize() {
    Logger::Add("[LUX] Script loaded.");
}

void Lux::OnTick() {
    // 1. Only act during combo
    bool isCombo = (GetAsyncKeyState(Settings::ComboKey) & 0x8000);
    if (!isCombo) return;
    if (!Input::IsReady()) return;

    // 2. Get player and game time
    GameObject* local = (GameObject*)GameReader::GetLocalPlayer();
    float realTime = GameReader::GetGameTime();

    // 3. Get spell slots
    SpellSlot qSlot = local->GetSpellSlot(0);  // Q - Light Binding
    SpellSlot eSlot = local->GetSpellSlot(2);   // E - Lucent Singularity
    SpellSlot rSlot = local->GetSpellSlot(3);   // R - Final Spark

    // 4. Find a target
    GameObject* target = TargetSelector::GetBestHero(
        GameReader::GetEnemies(), local, 1175.0f  // Lux Q range
    );
    if (!target) return;

    // 5. Set up prediction for Q (line skillshot)
    PredictionInput qInput;
    qInput.Target = target;
    qInput.From = local->GetPos();
    qInput.Delay = 0.25f;        // Cast time
    qInput.Speed = 1200.0f;      // Missile speed
    qInput.Radius = 70.0f;       // Hitbox width
    qInput.Range = 1175.0f;      // Max range
    qInput.Type = SpellType::Line;
    qInput.CheckCollision = true; // Q can be blocked by minions

    PredictionOutput qPred = Prediction::GetPrediction(qInput);

    // 6. Set up prediction for E (AOE, no collision check)
    PredictionInput eInput;
    eInput.Target = target;
    eInput.From = local->GetPos();
    eInput.Delay = 0.25f;
    eInput.Speed = 1300.0f;
    eInput.Radius = 310.0f;       // E has a large AOE
    eInput.Range = 1100.0f;
    eInput.Type = SpellType::Line;
    eInput.CheckCollision = false; // E passes through minions

    PredictionOutput ePred = Prediction::GetPrediction(eInput);

    // 7. Cast spells
    if (useQInCombo && qSlot.IsReady(realTime) && (realTime - lastQCastTime > 0.3f)) {
        if (qPred.IsValid) {
            Input::FlickKey('Q', qPred.CastPosition);
            lastQCastTime = realTime;
            return;  // One spell per frame
        }
    }

    if (useEInCombo && eSlot.IsReady(realTime) && (realTime - lastECastTime > 0.3f)) {
        if (ePred.IsValid) {
            Input::FlickKey('E', ePred.CastPosition);
            lastECastTime = realTime;
            return;
        }
    }

    // R on low HP enemies only
    if (useRInCombo && rSlot.IsReady(realTime) && (realTime - lastRCastTime > 0.5f)) {
        float targetHp = target->GetHealth();
        if (targetHp < 300.0f && qPred.IsValid) {  // Simple threshold
            Input::FlickKey('R', qPred.CastPosition);
            lastRCastTime = realTime;
            return;
        }
    }
}

void Lux::OnDraw() {
    // Optional: draw custom overlays here using ImGui::GetBackgroundDrawList()
}

void Lux::OnMenu() {
    ImGui::TextColored(ImVec4(1, 0.8f, 0, 1), "Lux Combo Settings");
    ImGui::Separator();
    ImGui::Checkbox("Use Q (Light Binding)", &useQInCombo);
    ImGui::Checkbox("Use E (Lucent Singularity)", &useEInCombo);
    ImGui::Checkbox("Use R (Final Spark)", &useRInCombo);
}
```

### Step 3: Register the Script

In `leagueone.cpp`, add your script to the registration list:

```cpp
#include "Lux.h"  // Add this include

// Inside hkPresent, in the initialization block:
ModuleManager::RegisterModule(new Ezreal());
ModuleManager::RegisterModule(new Lux());    // Add this line
ModuleManager::Initialize();
```

### Step 4: Add to the Build

In `leagueone.vcxproj`, add the new files:

```xml
<ItemGroup>
  <ClCompile Include="Lux.cpp" />
  <!-- ... existing files ... -->
</ItemGroup>
<ItemGroup>
  <ClInclude Include="Lux.h" />
  <!-- ... existing files ... -->
</ItemGroup>
```

That's it. When you load into a game as Lux, `ModuleManager::Initialize()` will detect your champion name, match it against all registered modules, and activate the Lux script.

---

## 3. Key APIs Available to Scripts

### Reading Game State

```cpp
// Get your champion
GameObject* local = (GameObject*)GameReader::GetLocalPlayer();
float gameTime = GameReader::GetGameTime();

// Get lists of game objects
const auto& enemies = GameReader::GetEnemies();   // All enemy champions
const auto& minions = GameReader::GetMinions();    // All minions
const auto& missiles = GameReader::GetMissiles();  // All active projectiles

// Read object properties
Vector3 pos = enemy->GetPos();
float hp = enemy->GetHealth();
float maxHp = enemy->GetMaxHealth();
float armor = enemy->GetArmor();
float mr = enemy->GetMagicResist();
float moveSpeed = enemy->GetMoveSpeed();
float ad = enemy->GetTotalAD();
std::string name = enemy->GetChampionName();
int team = enemy->GetTeam();
```

### Spell Slots

```cpp
SpellSlot q = local->GetSpellSlot(0);  // Q
SpellSlot w = local->GetSpellSlot(1);  // W
SpellSlot e = local->GetSpellSlot(2);  // E
SpellSlot r = local->GetSpellSlot(3);  // R

int level = q.GetLevel();              // 0 = not learned, 1-5 = rank
bool ready = q.IsReady(gameTime);      // Off cooldown and learned?
```

### Your Stats (from HTTP API)

```cpp
float as = GameAPI::CurrentAttackSpeed;
float range = GameAPI::CurrentRange;
float ad = GameAPI::CurrentAttackDamage;
float ap = GameAPI::CurrentAbilityPower;
float armor = GameAPI::CurrentArmor;
float mr = GameAPI::CurrentMR;
float armorPen = GameAPI::CurrentArmorPenPercent;
float lethality = GameAPI::CurrentLethality;
float magicPen = GameAPI::CurrentMagicPenPercent;
float magicPenFlat = GameAPI::CurrentMagicPenFlat;
```

### Target Selection

```cpp
// Get best hero in range (uses current targeting mode from settings)
GameObject* target = TargetSelector::GetBestHero(enemies, local, 1000.0f);

// Get best minion in range
GameObject* minion = TargetSelector::GetBestMinion(minions, local, 550.0f);
```

### Prediction

```cpp
PredictionInput input;
input.Target = target;
input.From = local->GetPos();
input.Delay = 0.25f;              // Cast animation time
input.Speed = 2000.0f;            // Projectile speed (0 for instant)
input.Radius = 60.0f;             // Hitbox width
input.Range = 1150.0f;            // Max range
input.Type = SpellType::Line;     // Line or Circle
input.CheckCollision = true;      // Check for minion blocking?

PredictionOutput output = Prediction::GetPrediction(input);

if (output.IsValid) {
    // output.CastPosition = where to aim
    // output.HChance = High, Low, or Impossible
    Input::FlickKey('Q', output.CastPosition);
}
```

### Input

```cpp
// Check if the flick system is ready (not mid-flick)
if (!Input::IsReady()) return;

// Cast a spell at a world position
Input::FlickKey('Q', worldPosition);    // Uses WorldToScreen internally
Input::FlickKey('W', worldPosition);

// Right-click at a screen position
Input::FlickRightClick(screenPosition);

// Raw key input (for non-targeted abilities or items)
Input::TapKey(VK_F1);                  // Quick press and release
Input::SendKey('D', true);             // Hold down
Input::SendKey('D', false);            // Release
```

### Drawing (OnDraw)

```cpp
ImDrawList* draw = ImGui::GetBackgroundDrawList();

// Draw a circle at a screen position
draw->AddCircle(ImVec2(x, y), radius, ImColor(255, 0, 0, 255), segments, thickness);

// Draw a filled rectangle
draw->AddRectFilled(ImVec2(x1, y1), ImVec2(x2, y2), ImColor(0, 0, 0, 200));

// Draw text
draw->AddText(ImVec2(x, y), ImColor(255, 255, 255, 255), "Hello");

// Draw a line
draw->AddLine(ImVec2(x1, y1), ImVec2(x2, y2), ImColor(0, 255, 0, 255), thickness);

// WorldToScreen for positioning
Vector2 screenPos;
if (Utils::WorldToScreen(enemy->GetPos(), screenPos)) {
    draw->AddCircleFilled(ImVec2(screenPos.x, screenPos.y), 5.0f, ImColor(255, 0, 0));
}
```

### Menu (OnMenu)

```cpp
// Your OnMenu runs inside an ImGui tab, so just use ImGui directly:
ImGui::Checkbox("Enable Feature", &myBool);
ImGui::SliderFloat("Range", &myFloat, 0.0f, 1000.0f);
ImGui::SliderInt("Priority", &myInt, 1, 5);
ImGui::Text("Current Value: %.1f", someValue);
ImGui::TextColored(ImVec4(1, 0, 0, 1), "Warning text");
ImGui::Separator();
ImGui::Spacing();

if (ImGui::RadioButton("Option A", myChoice == 0)) myChoice = 0;
ImGui::SameLine();
if (ImGui::RadioButton("Option B", myChoice == 1)) myChoice = 1;
```

---

## 4. The Ezreal Script in Detail

The Ezreal script (`Ezreal.cpp`) is the reference implementation. Here's what makes it notable:

### Damage Calculation

Ezreal has a full damage calculator that computes how much damage each spell will deal to a specific target, accounting for:
- Base damage per rank
- AD and AP scaling ratios
- Target's armor and magic resist
- Your penetration stats (lethality, % armor pen, magic pen)

```cpp
float CalculateEffectivePhysicalDamage(float rawDamage, float targetArmor) {
    float effArmor = (targetArmor * GameAPI::CurrentArmorPenPercent) - GameAPI::CurrentLethality;
    if (effArmor < 0.0f) effArmor = 0.0f;
    return rawDamage * (100.0f / (100.0f + effArmor));
}
```

The `GetFullComboDamage()` function sums up Q + W + E damage to determine if a target is "killable" (LETHAL).

### Intelligent E Repositioning

The most sophisticated part is the E (Arcane Shift) logic. Instead of just blinking forward, it evaluates 24 possible positions around Ezreal:

```cpp
for (int i = 0; i < 360; i += 15) {
    Vector3 testPos = localPos + 475.0f * direction(i);

    // 1. Is this position safe (not too close, not too far)?
    if (distToTarget >= 450 && distToTarget <= 1100) {
        // 2. Can Q reach the target from here without minion collision?
        if (!Prediction::CheckMinionCollision(testPos, targetPos, 60.0f)) {
            // 3. Is this position close to where the player is aiming?
            if (distToMouse < bestMouseDist) {
                bestEPos = testPos;  // Best position so far
            }
        }
    }
}
```

It picks the position that is:
- Between 450 and 1100 units from the target (safe but within Q range)
- Has a clear line of fire to the target (no minion collision)
- Closest to where the player's mouse is pointing (respects player intent)

### Damage Overlay ESP

The `OnDraw()` method renders a damage bar on each enemy showing how much of their HP your combo would remove:

```
[==========--------]  -450   (orange section = your combo damage)
[==========LETHAL!!]         (pulsing red text if target is killable)
```

---

## 5. Best Practices

1. **Always check `Input::IsReady()`** before casting -- if a flick is in progress, your input will be lost
2. **Use throttle timers** (`lastQCastTime`, etc.) to prevent casting the same spell multiple times per second
3. **Return after casting** -- only cast one spell per frame to avoid conflicting flicks
4. **Use the prediction system** for any skillshot -- don't cast at `target->GetPos()` directly (that's where they are now, not where they'll be)
5. **Check `IsReady(gameTime)`** on spell slots before casting -- don't waste a flick on a spell that's on cooldown
6. **Set `CheckCollision = true`** for spells that can be blocked by minions (most Q abilities)
7. **Use `GameAPI` stats** for your own champion's values -- they're more reliable than reading from memory
