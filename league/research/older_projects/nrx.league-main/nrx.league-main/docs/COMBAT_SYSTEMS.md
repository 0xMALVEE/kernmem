# Combat Systems

This document covers the four interconnected systems that automate combat: the **Orbwalker**, the **Prediction Engine**, the **Target Selector**, and the **Micro-Flick Input System**.

---

## 1. The Orbwalker (Orbwalker.cpp)

The orbwalker automates the core combat loop: attack a target, wait for the attack animation to complete, move during the downtime, then attack again as soon as possible. Manually doing this with perfect timing every game is what separates high-level players -- the orbwalker does it automatically.

### How It Works

Every frame, `Orbwalker::Update()` is called with the local player, all enemies, and all minions:

```cpp
void Orbwalker::Update(GameObject* local,
                       const std::vector<GameObject*>& heroes,
                       const std::vector<GameObject*>& minions)
```

#### Step 1: TCO Management

TCO (Target Champions Only) is a League keybind that prevents you from accidentally clicking on minions. The orbwalker auto-activates it during combo mode:

```cpp
if (Settings::UseTCO && Settings::AttackChampionsOnlyKey != 0) {
    bool isCombo = (GetAsyncKeyState(Settings::ComboKey) & 0x8000);
    if (Settings::IsTCOToggle) {
        // Toggle mode: tap key to activate/deactivate
        if (isCombo && !isTCOActive) { Input::TapKey(key); isTCOActive = true; }
        else if (!isCombo && isTCOActive) { Input::TapKey(key); isTCOActive = false; }
    } else {
        // Hold mode: hold key while combo is active
        if (isCombo && !isTCOActive) { Input::SendKey(key, true); isTCOActive = true; }
        else if (!isCombo && isTCOActive) { Input::SendKey(key, false); isTCOActive = false; }
    }
}
```

The TCO key and toggle/hold mode are auto-detected from League's own config files by `ConfigLoader`.

#### Step 2: Timing Calculation

```cpp
float as = GameAPI::CurrentAttackSpeed;      // From HTTP API
float delay = 1.0f / as;                     // Time between attacks
float windup = (delay * Settings::WindupPercent) + Debug::CurrentBuffer;
```

- **Attack delay** = `1 / attackSpeed` -- how many seconds between attacks
- **Windup** = the portion of the attack animation where you can't move (the "wind-up" before the projectile fires). It's a percentage of the attack delay plus a buffer for network latency.
- **Buffer** = `BaseBuffer + (ping / 2000)` -- half the round-trip ping converted to seconds, so the orbwalker accounts for network delay

#### Step 3: Target Selection

```cpp
GameObject* target = nullptr;
if (isCombo)     target = TargetSelector::GetBestHero(heroes, local, range);
if (isLaneClear) target = TargetSelector::GetBestMinion(minions, local, range);
```

In combo mode (default: Space key), only champions are targeted. In lane clear mode, minions are targeted.

#### Step 4: Attack

```cpp
if (target && Settings::AttackUnit) {
    if (now >= lastAttackTime + delay) {        // Attack cooldown elapsed?
        Vector2 screenPos;
        if (Utils::WorldToScreen(target->GetPos(), screenPos)) {
            Input::FlickRightClick(screenPos);   // Flick-click the target
            lastAttackTime = now;
            lastMoveTime = now + windup;          // Can't move until windup ends
            return;
        }
    }
}
```

The attack happens when enough time has passed since the last attack (respecting attack speed). It projects the target's world position to screen coordinates and performs a micro-flick right-click.

#### Step 5: Movement

```cpp
if (Settings::MoveUnit && now >= lastMoveTime) {
    POINT currentPos; GetCursorPos(&currentPos);
    Input::FlickRightClick(Vector2(currentPos.x, currentPos.y));
    nextMove = now + Settings::MoveDelay;
}
```

Between attacks, the orbwalker issues move commands toward the current mouse position. This is the "kiting" behavior -- attack, step, attack, step.

---

## 2. The Prediction Engine (Prediction.cpp)

The prediction engine answers the question: "If I cast a skillshot now, where will the target be when it arrives?"

### Kinematic Tracking

Every 50ms, `UpdateTrackers()` takes a snapshot of each enemy's position and calculates their velocity:

```cpp
struct UnitTracker {
    Vector3 LastPos;
    float LastTime;
    Vector3 Direction;  // Normalized movement direction
    float Speed;        // Movement speed (units/second)
};
```

```cpp
void UpdateTrackers(const std::vector<GameObject*>& units) {
    for (auto* unit : units) {
        Vector3 currentPos = unit->GetPos();
        auto& trk = Trackers[addr];
        float dt = currentTime - trk.LastTime;

        if (dt >= 0.05f) {  // Every 50ms
            float dist = currentPos.distance(trk.LastPos);

            if (dist > 5.0f && dist < 1000.0f) {
                // Use official move speed from memory (more accurate than calculated)
                float officialMS = unit->GetMoveSpeed();
                trk.Speed = (officialMS > 50.0f) ? officialMS : (dist / dt);

                // Calculate normalized direction
                Vector3 dir = { currentPos.x - trk.LastPos.x, 0, currentPos.z - trk.LastPos.z };
                float len = sqrtf(dir.x * dir.x + dir.z * dir.z);
                trk.Direction = { dir.x / len, 0, dir.z / len };
            }
            else if (dist <= 2.0f) {
                trk.Speed = 0.0f;  // Standing still
            }
        }
    }
}
```

Speed is read from the game's memory (`GetMoveSpeed()`) rather than calculated from position deltas, because the memory value is more accurate and doesn't suffer from timing jitter.

### Iterative Interception

The core prediction algorithm uses an iterative solver to find the exact collision point:

```cpp
Vector3 CalculateTargetPosition(GameObject* target, float delay, float radius,
                                 float speed, const Vector3& from) {
    float totalDelay = delay + 0.035f;  // Cast delay + network compensation
    float t = totalDelay;
    Vector3 predictedPos = currentPos;

    // 4 iterations of refinement
    for (int i = 0; i < 4; i++) {
        // 1. Where will the target be in 't' seconds?
        predictedPos = {
            currentPos.x + (trk.Direction.x * trk.Speed * t),
            currentPos.y,
            currentPos.z + (trk.Direction.z * trk.Speed * t)
        };

        // 2. How long for our missile to reach that point?
        float flightTime = from.distance(predictedPos) / speed;

        // 3. Refine: total time = cast delay + flight time
        t = totalDelay + flightTime;
    }

    return predictedPos;
}
```

**Why iterative?** The problem is circular: where the target will be depends on how long our missile takes to get there, but flight time depends on where we're aiming. The iterative approach converges quickly -- 4 iterations is enough for sub-pixel accuracy.

**The math step by step:**
1. Start with an initial time guess (just the cast delay)
2. Project the target forward by that time
3. Calculate how long our missile would take to reach that projected point
4. Use the new total time (delay + flight) as the next guess
5. Repeat 4 times

### Minion Collision Check

For line skillshots, the engine checks if any enemy minion blocks the path:

```cpp
bool CheckMinionCollision(const Vector3& from, const Vector3& targetPos, float spellWidth) {
    float collisionRadius = 65.0f + spellWidth;  // Minion hitbox + spell width

    for (auto* minion : GameReader::GetMinions()) {
        if (minion is dead or untargetable or friendly) continue;

        float distToLine = mPos.distanceToSegment2D(from, targetPos);
        if (distToLine <= collisionRadius) return true;  // Blocked!
    }
    return false;
}
```

This uses the `distanceToSegment2D` function from `Vector.h` to calculate the shortest distance from each minion's position to the line segment of the spell path.

### Hit Chance

The `GetPrediction()` function combines everything and assigns a hit chance:

```cpp
PredictionOutput GetPrediction(const PredictionInput& input) {
    Vector3 castPos = CalculateTargetPosition(...);

    // Out of range?
    if (from.distance(castPos) > input.Range)
        return HitChance::Impossible;

    // Blocked by minions?
    if (input.CheckCollision && CheckMinionCollision(from, castPos, radius))
        return HitChance::Low;

    // Clear shot in range
    return HitChance::High;
}
```

Hit chances:
- **High** -- Clear shot, target in range, no minion collision
- **Low** -- Shot would be blocked by minions
- **Impossible** -- Target is out of range

---

## 3. The Target Selector (TargetSelector.cpp)

The target selector provides three modes for choosing the best enemy to attack:

### Mode 0: Closest to Mouse

```cpp
for (auto* unit : heroes) {
    Vector2 screenPos;
    Utils::WorldToScreen(unit->GetPos(), screenPos);
    float mouseDist = distance(screenPos, mousePos);
    if (mouseDist < 600.0f && mouseDist < bestDist) {
        best = unit;
    }
}
```

Projects each enemy to screen coordinates and picks the one closest to the mouse cursor (within 600 pixels). This gives the player natural control -- aim near the enemy you want to target.

### Mode 1: Lowest HP

```cpp
for (auto* unit : heroes) {
    float hp = unit->GetHealth();
    if (hp < lowestHp) {
        best = unit;
    }
}
```

Always targets the lowest health enemy in range. Good for securing kills.

### Mode 2: Priority List

```cpp
for (auto* unit : heroes) {
    std::string name = unit->GetChampionName();
    int prio = Settings::PriorityMap[name];  // 1 (highest) to 5 (lowest)

    if (prio < bestPriority) {
        best = unit;
    }
    else if (prio == bestPriority && hp < bestHp) {
        best = unit;  // Tie-break by HP
    }
}
```

Uses a player-configured priority list. The in-game menu shows sliders (1-5) for each enemy champion. Equal-priority targets are tie-broken by lowest HP.

### Validation

All modes share a common validation check:

```cpp
bool IsValidHero(GameObject* unit, int myTeam, float range, GameObject* local) {
    if (unit->GetTeam() == myTeam) return false;     // Not an ally
    if (unit->GetHealth() <= 1.0f) return false;      // Not dead
    if (!isTargetable) return false;                   // Can be targeted
    if (distance > range + 100) return false;          // In range (+buffer)
    return true;
}
```

### Minion Targeting

`GetBestMinion` is simpler -- it picks the closest enemy minion or jungle monster:

```cpp
GameObject* GetBestMinion(minions, local, range) {
    // Team detection uses name parsing: "_T100" = team 1, "_T200" = team 2, "Sru"/"Crab" = jungle
    // Returns closest enemy/neutral minion within range
}
```

---

## 4. The Micro-Flick Input System (Input.cpp)

The input system solves a fundamental problem: how do you click on an enemy that's in a different screen position than where your mouse currently is, without the player seeing the mouse jump around?

The answer is **micro-flicking**: moving the mouse to the target, performing the action, and moving it back -- all in under 5 milliseconds.

### FlickRightClick -- Attack/Move Commands

```cpp
void FlickRightClick(const Vector2& screenPos) {
    if (g_IsFlicking) return;  // One flick at a time

    POINT currentPos;
    GetCursorPos(&currentPos);

    // If mouse is already at the target, just click in place
    if (distance(currentPos, screenPos) < 5.0f) {
        mouse_event(MOUSEEVENTF_RIGHTDOWN, 0, 0, 0, 0);
        PrecisionWait(Settings::FlickPressDelay);
        mouse_event(MOUSEEVENTF_RIGHTUP, 0, 0, 0, 0);
        return;
    }

    g_IsFlicking = true;

    std::thread([screenPos, currentPos]() {
        SetCursorPos(screenPos.x, screenPos.y);       // 1. Move to target
        PrecisionWait(Settings::FlickPreDelay);        //    Wait ~2ms

        mouse_event(MOUSEEVENTF_RIGHTDOWN, 0, 0, 0, 0);  // 2. Click down
        PrecisionWait(Settings::FlickPressDelay);          //    Wait ~5ms

        mouse_event(MOUSEEVENTF_RIGHTUP, 0, 0, 0, 0);    // 3. Click up
        PrecisionWait(Settings::FlickPostDelay);           //    Wait ~2ms

        SetCursorPos(currentPos.x, currentPos.y);     // 4. Restore mouse

        g_IsFlicking = false;
    }).detach();
}
```

The flick runs on a detached thread so it doesn't block the frame loop. The `g_IsFlicking` atomic bool prevents overlapping flicks.

### FlickKey -- Spell Casting

For abilities (Q, W, E, R), the same pattern is used but with keyboard input:

```cpp
void FlickKey(int vk, const Vector3& targetWorldPos) {
    Vector2 screenPos;
    Utils::WorldToScreen(targetWorldPos, screenPos);  // Convert 3D -> screen

    std::thread([vk, screenPos, currentPos]() {
        SetCursorPos(screenPos.x, screenPos.y);             // 1. Aim
        PrecisionWait(Settings::FlickPreDelay);

        keybd_event(vk, scanCode, KEYEVENTF_SCANCODE, 0);  // 2. Key down
        PrecisionWait(Settings::FlickPressDelay);

        keybd_event(vk, scanCode, KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP, 0);  // 3. Key up
        PrecisionWait(Settings::FlickPostDelay);

        SetCursorPos(currentPos.x, currentPos.y);           // 4. Restore
        g_IsFlicking = false;
    }).detach();
}
```

### Precision Timing

The `PrecisionWait` function uses `QueryPerformanceCounter` for sub-millisecond accuracy:

```cpp
void PrecisionWait(double milliseconds) {
    LARGE_INTEGER freq, start, current;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&start);
    while (elapsed < milliseconds) {
        QueryPerformanceCounter(&current);
        elapsed = (current - start) * 1000.0 / freq;
    }
}
```

This is a busy-wait loop (not `Sleep`) because `Sleep` only has ~15ms granularity on Windows, far too imprecise for sub-millisecond flick timing.

### WndProc Integration

When a flick is in progress, the `Renderer::WndProc` hook swallows mouse messages to prevent the game UI from reacting to the cursor jump:

```cpp
// Renderer.cpp:46
if (Input::g_IsFlicking) {
    if (uMsg == WM_MOUSEMOVE || uMsg == WM_SETCURSOR || uMsg == WM_NCHITTEST) {
        return TRUE;  // Eat the message
    }
}
```

### Configurable Delays

All three delays are exposed in the in-game menu under the Orbwalker tab:

| Setting | Default | Purpose |
|---------|---------|---------|
| Pre-Delay | 2.0 ms | Time between moving the cursor and starting the click/keypress |
| Press-Delay | 5.0 ms | How long to hold the click/key down |
| Post-Delay | 2.0 ms | Time between releasing and restoring the cursor |

Increasing Press-Delay helps if attacks are being cancelled (the game didn't register the click). Increasing Pre-Delay helps if the game registers the click at the old position.

---

## 5. How It All Fits Together

Here's a typical combo frame when the player holds Space (combo key):

```
Frame Start
  |
  v
Orbwalker::Update()
  |
  +-> Is combo key held? YES
  +-> Is Input ready (no flick in progress)? YES
  +-> Get attack speed (1.2), calculate delay (0.83s), windup (0.20s)
  +-> TargetSelector::GetBestHero() -> selects closest enemy to mouse
  +-> Has enough time passed since last attack? YES
  +-> WorldToScreen(target position) -> screen coordinates
  +-> Input::FlickRightClick(screenPos) -> attack the target
  |
  v
ModuleManager::OnTick() -> Ezreal::OnTick()
  |
  +-> Is combo key held? YES
  +-> Is Input ready? NO (flick from orbwalker is still running)
  +-> Return (skip spell casting this frame)
  |
  v
Next Frame: orbwalker sets lastMoveTime, so movement is blocked during windup
  |
Next Frame: Input is ready again, Ezreal::OnTick() runs prediction:
  +-> Get best target
  +-> Prediction::GetPrediction(Q params) -> calculate intercept point
  +-> HitChance is High? YES
  +-> Input::FlickKey('Q', predictedPosition) -> cast Q at predicted position
```
