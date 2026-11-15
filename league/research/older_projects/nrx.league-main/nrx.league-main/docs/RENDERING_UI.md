# Rendering & UI

This document covers how LeagueOne hooks into DirectX 11 for rendering, integrates ImGui for the settings menu, draws in-game overlays, and handles the zoom hack.

---

## 1. The DirectX 11 Hook

### How Present Gets Hooked

The game uses DirectX 11's `IDXGISwapChain::Present()` to display each frame. By hooking this function, our code runs every time the game renders.

The hook is installed in `MainThread` (leagueone.cpp:133-170):

1. **Create a dummy D3D11 device** -- We call `D3D11CreateDeviceAndSwapChain()` with a throwaway window. This gives us a swap chain object.
2. **Read the VTable** -- Every COM object has a virtual function table. `Present` is at index 8.
3. **Save and replace** -- We save the original function pointer (`oPresent`), then overwrite the VTable entry with our hook function.

```cpp
void** pVTable = *reinterpret_cast<void***>(pDummySwapChain);
oPresent = (D3D11PresentHook)pVTable[8];

DWORD oldProtect;
VirtualProtect(&pVTable[8], 8, PAGE_EXECUTE_READWRITE, &oldProtect);
pVTable[8] = hkPresent;
VirtualProtect(&pVTable[8], 8, oldProtect, &oldProtect);
```

Since all `IDXGISwapChain` instances share the same VTable, modifying it on our dummy object also affects the game's swap chain.

### Renderer Initialization

On the first frame, `Renderer::Initialize()` (Renderer.cpp:56) sets up the rendering pipeline:

```cpp
bool Initialize(IDXGISwapChain* pSwapChain) {
    // 1. Get the real D3D11 device from the game's swap chain
    pSwapChain->GetDevice(__uuidof(ID3D11Device), (void**)&pDevice);
    pDevice->GetImmediateContext(&pContext);

    // 2. Get the game window and its dimensions
    DXGI_SWAP_CHAIN_DESC sd;
    pSwapChain->GetDesc(&sd);
    window = sd.OutputWindow;
    RECT rect;
    GetClientRect(window, &rect);
    Utils::Initialize(rect.right - rect.left, rect.bottom - rect.top);

    // 3. Create a render target view for drawing
    ID3D11Texture2D* pBackBuffer;
    pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (LPVOID*)&pBackBuffer);
    pDevice->CreateRenderTargetView(pBackBuffer, NULL, &mainRenderTargetView);
    pBackBuffer->Release();

    // 4. Hook the window procedure for input handling
    oWndProc = (WNDPROC)SetWindowLongPtr(window, GWLP_WNDPROC, (LONG_PTR)WndProc);

    // 5. Initialize ImGui
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
    ImGui_ImplWin32_Init(window);
    ImGui_ImplDX11_Init(pDevice, pContext);

    return true;
}
```

Key points:
- We reuse the game's existing D3D11 device and context (we don't create our own)
- We hook `WndProc` to intercept keyboard/mouse input before the game sees it
- ImGui is initialized with the DirectX 11 + Win32 backends

---

## 2. The WndProc Hook

`WndProc` (Renderer.cpp:41) intercepts Windows messages before the game processes them:

```cpp
LRESULT __stdcall WndProc(const HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    // 1. Let ImGui handle input first (for menu interaction)
    if (ImGui_ImplWin32_WndProcHandler(hWnd, uMsg, wParam, lParam))
        return true;

    // 2. Block mouse messages during micro-flicks
    if (Input::g_IsFlicking) {
        if (uMsg == WM_MOUSEMOVE || uMsg == WM_SETCURSOR || uMsg == WM_NCHITTEST)
            return TRUE;  // Swallow the message
    }

    // 3. Block mouse clicks when the menu is open
    if (Settings::ShowMenu && (uMsg == WM_LBUTTONDOWN || ...))
        return true;

    // 4. Pass everything else to the game
    return CallWindowProc(oWndProc, hWnd, uMsg, wParam, lParam);
}
```

This serves three purposes:
- ImGui needs to receive mouse/keyboard events to make its UI interactive
- During flicks, mouse movement messages are blocked so the game UI doesn't jump
- When the menu is open, mouse clicks go to ImGui instead of the game

---

## 3. The ImGui Menu

The menu is rendered in `Renderer::Draw()` (Renderer.cpp:83). It's toggled with **Left Shift**:

```cpp
bool currentShift = GetAsyncKeyState(VK_LSHIFT) & 0x8000;
if (currentShift && !lastShift) Settings::ShowMenu = !Settings::ShowMenu;
```

### Menu Structure

The menu has 7 tabs inside a single window:

| Tab | What It Contains |
|-----|-----------------|
| **Visuals** | Zoom hack toggle/slider, attack range circle toggle/color, missile overlay toggle |
| **Champion** | Delegated to `ModuleManager::OnMenu()` which calls the active champion script's menu |
| **Orbwalker** | Master switch, attack/move toggles, windup/buffer/delay sliders, flick timing sliders, live ping/buffer display |
| **Targeting** | Mode selector (Closest to Mouse / Lowest HP / Priority), per-enemy priority sliders (when in Priority mode) |
| **Keybinds** | Combo key rebinding, TCO toggle, League config re-sync button |
| **Debug** | Buttons for all 16+ offset discovery scanners |
| **Spell Inspector** | Live memory viewer for spell slot offsets (for development) |

### How Settings Work with ImGui

ImGui widgets modify variables directly through pointers:

```cpp
// Checkbox: writes directly to Settings::OrbwalkerEnabled
if (ImGui::Checkbox("Master Switch", &Settings::OrbwalkerEnabled))
    ConfigManager::SaveSettings();  // Save to TOML whenever changed

// Slider: writes directly to Settings::WindupPercent
ImGui::SliderFloat("Windup %", &Settings::WindupPercent, 0.0f, 0.5f);

// Color picker: writes directly to Settings::RangeColor[4]
if (ImGui::ColorEdit4("Range Color", Settings::RangeColor))
    ConfigManager::SaveSettings();
```

The `SaveSettings()` call is triggered by the `if` wrapper -- ImGui returns `true` when the user modifies a value.

### Dynamic Priority Sliders

The Targeting tab dynamically shows sliders for each enemy champion currently in the game:

```cpp
for (auto* enemy : GameReader::GetEnemies()) {
    std::string name = enemy->GetChampionName();

    // Initialize with default priority if first seen
    if (Settings::PriorityMap.find(name) == Settings::PriorityMap.end())
        Settings::PriorityMap[name] = 5;

    int prio = Settings::PriorityMap[name];
    ImGui::PushID(name.c_str());  // Unique ID per enemy
    ImGui::SliderInt("##prio", &prio, 1, 5);
    Settings::PriorityMap[name] = prio;
    ImGui::PopID();
}
```

### Keybind Rebinding

The keybind system uses a two-state button:

```cpp
static bool waitCombo = false;
if (waitCombo) {
    ImGui::Button("Press key...");
    for (int i = 3; i < 255; i++) {
        if (GetAsyncKeyState(i) & 0x8000) {
            Settings::ComboKey = i;
            waitCombo = false;
            ConfigManager::SaveSettings();
        }
    }
} else {
    if (ImGui::Button(GetKeyName(Settings::ComboKey)))
        waitCombo = true;
}
```

Click the button to enter "listening" mode, then press any key to bind it.

---

## 4. Overlay Drawing (Overlays.h)

Overlays are drawn in `Renderer::Draw()` after the ImGui menu, using `ImGui::GetBackgroundDrawList()` which draws behind ImGui windows but on top of the game.

### Attack Range Circle

`Overlays::DrawAttackRange()` draws a circle around the player showing their auto-attack range:

```cpp
void DrawAttackRange(GameObject* local, float range) {
    if (!Settings::DrawAttackRange) return;

    Vector3 center = local->GetPos();
    float totalRange = range + local->GetBoundingRadius();
    int segments = 64;

    // Draw a circle by connecting 64 line segments
    for (int i = 0; i < segments; i++) {
        float a1 = (i / (float)segments) * 2 * 3.14159f;
        float a2 = ((i+1) / (float)segments) * 2 * 3.14159f;

        Vector3 p1 = { center.x + cos(a1) * totalRange, center.y, center.z + sin(a1) * totalRange };
        Vector3 p2 = { center.x + cos(a2) * totalRange, center.y, center.z + sin(a2) * totalRange };

        Vector2 s1, s2;
        if (Utils::WorldToScreen(p1, s1) && Utils::WorldToScreen(p2, s2)) {
            drawList->AddLine(ImVec2(s1.x, s1.y), ImVec2(s2.x, s2.y),
                ImColor(r, g, b, a), Settings::RangeThickness);
        }
    }
}
```

Each segment of the circle is a 3D point on the ground plane, projected to screen coordinates with WorldToScreen.

### Missile Danger Zones

`Overlays::DrawMissiles()` draws rectangles showing the path and danger zone of enemy skillshots:

```cpp
void DrawMissiles() {
    for (auto* missile : GameReader::GetMissiles()) {
        std::string name = missile->GetMissileSpellName();
        if (name.empty() || name.length() > 50) continue;

        Vector3 start = missile->GetMissileStartPos();
        Vector3 end = missile->GetMissileEndPos();

        // Calculate perpendicular offset for rectangle width
        Vector3 dir = { end.x - start.x, 0, end.z - start.z };
        float len = sqrtf(dir.x*dir.x + dir.z*dir.z);
        Vector3 perp = { -dir.z/len * width, 0, dir.x/len * width };

        // Four corners of the danger zone rectangle
        Vector3 corners[4] = {
            { start.x + perp.x, start.y, start.z + perp.z },
            { start.x - perp.x, start.y, start.z - perp.z },
            { end.x - perp.x,   end.y,   end.z - perp.z },
            { end.x + perp.x,   end.y,   end.z + perp.z }
        };

        // Project to screen and draw
        // Also draws the spell name at the start position
    }
}
```

### Prediction Path Lines

`Overlays::DrawPredictionPaths()` shows where each enemy is predicted to move:

```cpp
void DrawPredictionPaths(const std::vector<GameObject*>& enemies) {
    for (auto* enemy : enemies) {
        auto path = Prediction::GetKinematicPath(enemy);
        if (path.size() >= 2) {
            Vector2 s1, s2;
            if (WorldToScreen(path[0], s1) && WorldToScreen(path[1], s2)) {
                drawList->AddLine(ImVec2(s1.x, s1.y), ImVec2(s2.x, s2.y),
                    ImColor(0, 200, 200, 200), 2.0f);
            }
        }
    }
}
```

This draws a cyan line from each enemy's current position to where they'll be in 1.5 seconds.

---

## 5. The Logger (Logger.h)

The in-game log is an ImGui window that displays debug messages:

```cpp
namespace Logger {
    static std::vector<std::string> lines;

    static void Add(const char* fmt, ...) {
        char buf[256];
        va_list args;
        va_start(args, fmt);
        vsnprintf(buf, sizeof(buf), fmt, args);
        va_end(args);
        lines.push_back(buf);
    }

    static void Draw(bool* open) {
        ImGui::Begin("Log", open, ImGuiWindowFlags_NoFocusOnAppearing);
        for (auto& l : lines)
            ImGui::TextUnformatted(l.c_str());
        if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
            ImGui::SetScrollHereY(1.0f);  // Auto-scroll
        ImGui::End();
    }
}
```

Usage from anywhere: `Logger::Add("[SYS] Hook installed at 0x%llX", address);`

---

## 6. Zoom Hack (Visuals.cpp)

The zoom hack works by writing to the game's camera constraint values every frame:

```cpp
void Update() {
    if (!Settings::ZoomHackEnabled) return;

    uintptr_t hud = SafeRead(base + Offset::oHudInstance);

    // Path A: Classic camera object at +0x1E8
    uintptr_t camA = SafeRead(hud + 0x1E8);
    *(float*)(camA + 0x28) = Settings::MaxZoomValue;

    // Path B: Modern camera object at +0x18
    uintptr_t camB = SafeRead(hud + 0x18);
    *(float*)(camB + 0x320) = Settings::MaxZoomValue;
    *(float*)(camB + 0x324) = Settings::MaxZoomValue;
}
```

Two paths are used because different game versions store the camera constraint in different locations. Both are written to ensure compatibility. The default max zoom is 2250 units; the hack allows up to 6000 units.

---

## 7. Render Frame Lifecycle

Here's the complete sequence for a single rendered frame:

```
1. Game calls Present()
2. hkPresent() intercepts

3. If first frame:
   - Renderer::Initialize()
   - Config loading
   - Module registration

4. GameReader::Update()         -- read all game state
5. Visuals::Update()            -- write zoom hack values
6. Prediction::UpdateTrackers() -- kinematic snapshots
7. Orbwalker::Update()          -- attack/move decisions
8. ModuleManager::OnTick()      -- champion script logic

9. Renderer::Draw():
   a. Check LShift for menu toggle
   b. ImGui_ImplDX11_NewFrame()
   c. ImGui_ImplWin32_NewFrame()
   d. ImGui::NewFrame()
   e. If menu is open:
      - Render all tabs (Visuals, Champion, Orbwalker, etc.)
      - Logger::Draw()
   f. Overlays::DrawAttackRange()
   g. Overlays::DrawMissiles()
   h. Overlays::DrawPredictionPaths()
   i. ModuleManager::OnDraw()  -- champion script drawing
   j. ImGui::EndFrame()
   k. ImGui::Render()
   l. pContext->OMSetRenderTargets()
   m. ImGui_ImplDX11_RenderDrawData()

10. oPresent()                  -- original Present renders the game
```
