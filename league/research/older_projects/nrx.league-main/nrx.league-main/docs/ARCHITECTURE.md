# Architecture

This document explains how LeagueOne is structured: how the DLL loads, how it hooks into the game, the threading model, and the per-frame execution flow.

---

## 1. DLL Entry Point

Everything begins in `leagueone.cpp` at the `DllMain` function:

```cpp
// leagueone.cpp:195
BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    if (ul_reason_for_call == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        CreateThread(nullptr, 0, MainThread, hModule, 0, nullptr);
    }
    return TRUE;
}
```

When the DLL is injected into the game process, Windows calls `DllMain` with `DLL_PROCESS_ATTACH`. The code:

1. Disables further thread attach/detach notifications (performance optimization)
2. Creates a new thread (`MainThread`) to do the actual work

`DllMain` itself must return quickly -- doing heavy work inside it can deadlock the process. That's why all real initialization happens on the spawned thread.

---

## 2. MainThread -- The Bootstrap

`MainThread` (leagueone.cpp:133) performs the initial setup:

### Step 1: Wait for the Game Module

```cpp
uintptr_t base = 0;
while (!(base = (uintptr_t)GetModuleHandle(NULL))) Sleep(100);
```

The game's main executable must be loaded before we can do anything. `GetModuleHandle(NULL)` returns the base address of the process's main module. We spin until it's available.

### Step 2: Create a Dummy DirectX Device

```cpp
D3D11CreateDeviceAndSwapChain(..., &pDummySwapChain, &pDummyDevice, ...);
```

To hook the game's rendering, we need to find the address of the `Present()` function. The trick: create our own temporary D3D11 device and swap chain. The swap chain object contains a virtual function table (VTable), and `Present` is always at index 8. Since all swap chains share the same VTable, the function address from our dummy device is the same one the game uses.

### Step 3: Hook Present

```cpp
void** pVTable = *reinterpret_cast<void***>(pDummySwapChain);
oPresent = (D3D11PresentHook)pVTable[8];   // Save original
VirtualProtect(&pVTable[8], 8, PAGE_EXECUTE_READWRITE, &oldProtect);
pVTable[8] = hkPresent;                     // Overwrite with our hook
VirtualProtect(&pVTable[8], 8, oldProtect, &oldProtect);
```

We save the original `Present` function pointer, make the VTable memory writable, replace the entry with our hook function `hkPresent`, then restore the memory protection. From this point on, every time the game calls `Present()` to display a frame, our `hkPresent` runs first.

### Step 4: Keep Alive

```cpp
while (g_Running) {
    if (GetAsyncKeyState(VK_DELETE) & 1) g_Running = false;
    Sleep(100);
}
Renderer::Cleanup();
FreeLibraryAndExitThread((HMODULE)lpParam, 0);
```

The thread loops until the user presses the Delete key, which triggers cleanup and DLL unloading.

---

## 3. hkPresent -- The Frame Loop

`hkPresent` (leagueone.cpp:55) is the heart of the framework. It runs once per rendered frame on the **render thread**. Here's what happens each frame:

### Phase 1: One-Time Initialization (First Frame Only)

```cpp
if (!g_Initialized) {
    Renderer::Initialize(pSwapChain);   // Set up D3D11 device, ImGui, WndProc hook
    ConfigManager::LoadSettings();       // Load Settings.toml
    ConfigLoader::LoadLeagueSettings();  // Auto-detect League keybinds
    Orbwalker::Initialize();             // Start the GameAPI HTTP polling thread
    ModuleManager::RegisterModule(new Ezreal());  // Register champion scripts
    ModuleManager::Initialize();                   // Match script to current champion
    g_Initialized = true;
}
```

This runs only on the very first frame after the hook activates. It:
- Grabs the real D3D11 device from the swap chain
- Sets up ImGui for rendering
- Hooks the window procedure (WndProc) for input
- Loads saved settings
- Auto-detects League's keybind configuration
- Starts a background thread that polls the local League API for live stats
- Registers all available champion scripts and activates the one matching your current champion

### Phase 2: Shadow VMT Hook (Game Thread Pivot)

```cpp
if (g_Initialized && !g_HookedGameThread) {
    uintptr_t hudInstance = SafeReadSDK<uintptr_t>(base + Offset::oHudInstance, 0);
    uintptr_t hudInputLogic = SafeReadSDK<uintptr_t>(hudInstance + 0x60, 0);

    // Copy the VTable
    shadowVTable = new uintptr_t[100];
    memcpy(shadowVTable, originalVTable, 100 * sizeof(uintptr_t));

    // Replace index 0 with our hook
    oHudUpdate = (fnHudUpdate)shadowVTable[0];
    shadowVTable[0] = (uintptr_t)hkHudUpdate;

    // Point the object to our modified VTable
    *(uintptr_t**)hudInputLogic = shadowVTable;
}
```

This is the "shadow VMT hook" technique. The game has a HUD object that gets updated on the **game logic thread**. By creating a copy of its VTable and swapping one function pointer, we get a hook that runs on the game thread. This is important for operations that must be synchronized with game logic rather than rendering.

**Why a shadow copy?** Modifying the original VTable would affect all objects of that class. A shadow copy only affects this one instance.

### Phase 3: Per-Frame Update

```cpp
GameReader::Update(base);              // Read heroes, minions, missiles from memory
Visuals::Update();                     // Apply zoom hack
Prediction::UpdateTrackers(enemies);   // Update kinematic tracking for enemies

if (localAddr != 0) {
    Orbwalker::Update(local, enemies, minions);  // Attack/move automation
    ModuleManager::OnTick();                      // Run champion script logic
}

Renderer::Draw();  // Render ImGui menu + overlays
```

Every frame:
1. **GameReader** re-reads all game objects from memory (heroes, minions, missiles)
2. **Visuals** writes the zoom hack value if enabled
3. **Prediction** takes a kinematic snapshot of each enemy (position, velocity, direction)
4. **Orbwalker** decides whether to attack or move
5. **ModuleManager** calls the active champion script's `OnTick()`
6. **Renderer** draws the ImGui menu, overlays, and all visual indicators

### Phase 4: Call Original

```cpp
return oPresent(pSwapChain, SyncInterval, Flags);
```

After our code finishes, we call the original `Present` function so the game renders its frame normally.

---

## 4. Threading Model

The framework uses three types of threads:

| Thread | Source | Purpose |
|--------|--------|---------|
| **Render Thread** | Game's own thread, we run via the `Present` hook | Frame loop, ImGui rendering, game state reading, orbwalker, champion scripts |
| **Game Logic Thread** | Game's own thread, we run via the shadow VMT hook on HudUpdate | Currently used as a hook point (the actual game-thread work is stubbed -- just calls the original) |
| **API Polling Thread** | Created by `GameAPI::Start()` | Runs `StatsFetcherLoop()` which polls `localhost:2999` every 100ms for live player stats |
| **Flick Threads** | Created per-input in `Input.cpp` | Short-lived detached threads that perform mouse/keyboard micro-flicks with sub-millisecond timing |

Most of the work happens on the render thread. The flick threads are fire-and-forget: each flick spawns a thread, moves the mouse, presses a key, restores the mouse, and dies. The `g_IsFlicking` atomic bool prevents overlapping flicks.

---

## 5. File Map

Here's how the source files relate to each other, grouped by subsystem:

### Core / Entry
| File | Role |
|------|------|
| `leagueone.cpp` | DLL entry, MainThread, Present hook, frame loop |
| `SDK.h` | Safe memory reading, GameObject class, SpellSlot class |
| `Offsets.h` | All hard-coded memory offsets |
| `Vector.h` | Vector2 and Vector3 math types |

### Memory Reading
| File | Role |
|------|------|
| `GameReader.h/.cpp` | Reads hero, minion, and missile lists each frame |
| `GameAPI.h/.cpp` | Polls League's local HTTP API for attack speed, AD, AP, pen values; reads ping from memory |
| `PatternScanner.h/.cpp` | AOB pattern scanning for finding function addresses |

### Combat
| File | Role |
|------|------|
| `Orbwalker.h/.cpp` | Attack timing, movement commands, TCO management |
| `Prediction.h/.cpp` | Skillshot prediction with iterative interception |
| `TargetSelector.h/.cpp` | Best target selection (3 modes) |
| `Input.h/.cpp` | Micro-flick input engine (mouse + keyboard) |

### Champion Scripts
| File | Role |
|------|------|
| `IChampion.h` | Abstract interface all scripts implement |
| `Ezreal.h/.cpp` | Ezreal-specific combo logic, damage calculation, overlays |
| `ModuleManager.h/.cpp` | Registers scripts, matches to current champion, dispatches lifecycle events |

### Rendering
| File | Role |
|------|------|
| `Renderer.h/.cpp` | D3D11 setup, ImGui init, WndProc hook, full menu UI, overlay dispatch |
| `Overlays.h` | Drawing functions for attack range, missiles, prediction paths |
| `Visuals.h/.cpp` | Zoom hack implementation |
| `Utils.h/.cpp` | WorldToScreen projection, screen dimensions |

### Configuration
| File | Role |
|------|------|
| `Settings.h` | Global settings namespace (all runtime values) |
| `ConfigManager.h/.cpp` | TOML save/load for Settings.toml |
| `ConfigLoader.h/.cpp` | Auto-detects League installation, reads keybinds from PersistedSettings.json |

### Debug / Development
| File | Role |
|------|------|
| `Debugger.h/.cpp` | 16+ offset discovery scanners |
| `Logger.h` | In-game ImGui log window |
| `Debug.h` | Console allocation helper |

---

## 6. Execution Flow Diagram

```
Game Process Starts
  |
  v
DLL Injected -> DllMain(DLL_PROCESS_ATTACH)
  |
  v
MainThread spawned
  |
  +-> Wait for game module
  +-> Create dummy D3D11 device
  +-> Hook Present (VTable swap at index 8)
  +-> Enter keep-alive loop (Delete key to exit)
  |
  v
hkPresent() called every frame
  |
  +-- First frame only:
  |     +-> Renderer::Initialize() -- D3D11 + ImGui + WndProc hook
  |     +-> ConfigManager::LoadSettings()
  |     +-> ConfigLoader::LoadLeagueSettings()
  |     +-> Orbwalker::Initialize() -> GameAPI::Start() (spawns HTTP thread)
  |     +-> ModuleManager::RegisterModule(new Ezreal())
  |     +-> ModuleManager::Initialize()
  |
  +-- Until HUD hook installed:
  |     +-> Find HudInputLogic object
  |     +-> Shadow VMT hook on index 0 (HudUpdate)
  |
  +-- Every frame:
        +-> GameReader::Update()      -- read all game objects
        +-> Visuals::Update()         -- zoom hack
        +-> Prediction::UpdateTrackers()
        +-> Orbwalker::Update()       -- attack/move logic
        +-> ModuleManager::OnTick()   -- champion script
        +-> Renderer::Draw()          -- ImGui + overlays
        +-> oPresent()                -- original Present
```

---

## 7. Unloading

When the user presses Delete:

1. `g_Running` is set to false
2. `Renderer::Cleanup()` restores the original `WndProc`
3. `FreeLibraryAndExitThread()` unloads the DLL from the process

Note: The Present hook is **not** unhooked (this would require a trampoline/detour library like MinHook). The framework relies on the game being closed or accepts a potential crash on unload for simplicity.
