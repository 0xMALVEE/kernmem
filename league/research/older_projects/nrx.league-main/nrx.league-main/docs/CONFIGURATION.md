# Configuration

This document covers the three configuration systems in LeagueOne: the **Settings namespace** (runtime values), the **ConfigManager** (TOML save/load), and the **ConfigLoader** (League keybind auto-detection).

---

## 1. The Settings Namespace (Settings.h)

All runtime settings live in a single static namespace. These are the actual variables that the rest of the code reads:

```cpp
// Settings.h
namespace Settings {
    // Orbwalker
    inline bool OrbwalkerEnabled = true;
    inline bool AttackUnit = true;
    inline bool MoveUnit = true;
    inline float WindupPercent = 0.25f;
    inline float BaseBuffer = 0.02f;
    inline float MoveDelay = 0.08f;

    // Flick Timing (milliseconds)
    inline float FlickPreDelay = 2.0f;
    inline float FlickPressDelay = 5.0f;
    inline float FlickPostDelay = 2.0f;

    // Targeting
    inline bool UseTCO = true;
    inline int TargetMode = 0;  // 0=Mouse, 1=LowestHP, 2=Priority
    inline std::map<std::string, int> PriorityMap;

    // Visuals
    inline bool DrawAttackRange = true;
    inline float RangeThickness = 2.0f;
    inline float RangeColor[4] = { 0.0f, 1.0f, 0.0f, 1.0f };
    inline bool DrawMissiles = true;
    inline bool ZoomHackEnabled = false;
    inline float MaxZoomValue = 2250.0f;

    // Keybinds
    inline int ComboKey = VK_SPACE;
    inline int LaneClearKey = 'V';
    inline int AttackChampionsOnlyKey = 0;  // Auto-detected
    inline bool IsTCOToggle = false;         // Auto-detected

    // UI
    inline bool ShowMenu = false;
}
```

Settings are modified in two ways:
1. **ImGui widgets** write directly to these variables through pointers
2. **ConfigManager** reads values from a TOML file and writes them here at startup

---

## 2. The Debug Namespace (Debug.h / Settings.h)

Live stats displayed in the UI:

```cpp
namespace Debug {
    inline int CurrentPing = 0;
    inline float CurrentBuffer = 0.0f;
}
```

These are updated by the Orbwalker every frame based on the current ping value.

---

## 3. ConfigManager -- TOML Save/Load (ConfigManager.cpp)

The ConfigManager uses the **toml++** library to persist settings to `Settings.toml`.

### File Format

The generated TOML file looks like this:

```toml
# LeagueOne Settings | Automatically generated

[Orbwalker]
Enabled = true
AttackUnits = true
MoveToMouse = true
WindupPercent = 0.25
BufferSeconds = 0.02
MoveDelay = 0.08

[Flick]
PreDelay = 2.0
PressDelay = 5.0
PostDelay = 2.0

[Visuals]
ZoomHackEnabled = false
MaxZoomValue = 2250.0
DrawAttackRange = true
RangeThickness = 2.0
RangeColor = [0.0, 1.0, 0.0, 1.0]

[Targeting]
UseTCO = true
TargetMode = 0

[Targeting.Priorities]
Jinx = 1
Yasuo = 3
Thresh = 5

[Keybinds]
Combo = 32
LaneClear = 86
```

### Loading

```cpp
void LoadSettings() {
    toml::table tbl = toml::parse_file("Settings.toml");

    // value_or() provides a fallback if the key doesn't exist
    Settings::OrbwalkerEnabled = tbl["Orbwalker"]["Enabled"].value_or(Settings::OrbwalkerEnabled);
    Settings::WindupPercent = tbl["Orbwalker"]["WindupPercent"].value_or(Settings::WindupPercent);

    // Nested tables for priorities
    if (auto* prioTable = tbl["Targeting"]["Priorities"].as_table()) {
        for (auto& [key, val] : *prioTable) {
            Settings::PriorityMap[std::string(key)] = static_cast<int>(val.as_integer()->get());
        }
    }
}
```

If the file doesn't exist (first run), defaults from `Settings.h` are used.

### Saving

The menu triggers a save whenever a setting changes:

```cpp
if (ImGui::Checkbox("Master Switch", &Settings::OrbwalkerEnabled))
    ConfigManager::SaveSettings();  // Writes entire Settings.toml
```

The save function builds a TOML table from all current values and writes it to disk.

### File Location

`Settings.toml` is written to the **current working directory** of the game process. Since the DLL is injected into League of Legends, this is typically the game's installation folder (e.g., `C:\Riot Games\League of Legends\Game\`).

---

## 4. ConfigLoader -- League Keybind Auto-Detection (ConfigLoader.cpp)

The ConfigLoader automatically finds your League of Legends installation and reads the **Target Champions Only (TCO)** keybind from the game's config files. This is necessary because:
- TCO is a critical keybind for combo mode (prevents clicking minions)
- Every player may have it bound to a different key
- The keybind may be in toggle or hold mode

### Auto-Detection Flow

```
1. Read %ProgramData%\Riot Games\RiotClientInstalls.json
2. Extract the League of Legends installation path
3. Navigate to {path}\Config\PersistedSettings.json
4. Parse the JSON to find "evtChampionOnly" keybind
5. Parse the JSON to find "TargetChampionsOnlyAsToggle" setting
6. Convert the key string to a virtual key code
```

### Scan Code Translation

League's config uses key names like `"g"`, `"Space"`, `"`"`. These need to be converted to Windows virtual key codes. The ConfigLoader uses a two-step process:

1. **String -> Scan Code**: A hardcoded map converts key names to physical scan codes (the position on the keyboard, independent of layout):

```cpp
static std::map<std::string, int> scanCodeMap = {
    {"Space", 0x39}, {"Tab", 0x0F},
    {"q", 0x10}, {"w", 0x11}, {"e", 0x12}, // ...
    {"`", 0x29}, {"~", 0x29},
    // ... all keys
};
```

2. **Scan Code -> Virtual Key**: `MapVirtualKeyA()` converts the physical scan code to the virtual key for the user's current keyboard layout. This handles international layouts correctly -- pressing the same physical key on a French (AZERTY) vs US (QWERTY) keyboard produces different virtual keys.

```cpp
UINT vk = MapVirtualKeyA(scanCode, MAPVK_VSC_TO_VK);
```

### Result

After detection, these values are set:
- `Settings::AttackChampionsOnlyKey` -- The VK code for TCO (e.g., `0x47` for 'G')
- `Settings::IsTCOToggle` -- `true` if TCO is toggle mode, `false` if hold mode

The orbwalker then uses these to automatically press/release TCO during combo mode.

---

## 5. Settings Quick Reference

| Setting | Default | Range | Where It's Used |
|---------|---------|-------|----------------|
| OrbwalkerEnabled | true | on/off | Orbwalker.cpp -- master switch |
| AttackUnit | true | on/off | Orbwalker.cpp -- auto-attack toggle |
| MoveUnit | true | on/off | Orbwalker.cpp -- auto-move toggle |
| WindupPercent | 0.25 | 0.0-0.5 | Orbwalker.cpp -- attack animation lockout |
| BaseBuffer | 0.02 | 0.0-0.1 | Orbwalker.cpp -- extra delay buffer |
| MoveDelay | 0.08 | 0.03-0.15 | Orbwalker.cpp -- minimum time between move commands |
| FlickPreDelay | 2.0 ms | 0.5-15 | Input.cpp -- delay before click after cursor move |
| FlickPressDelay | 5.0 ms | 0.5-25 | Input.cpp -- how long to hold click/key down |
| FlickPostDelay | 2.0 ms | 0.5-15 | Input.cpp -- delay before restoring cursor |
| TargetMode | 0 | 0/1/2 | TargetSelector.cpp -- targeting algorithm |
| ComboKey | Space | any key | Everywhere -- activates combo mode |
| LaneClearKey | V | any key | Orbwalker.cpp -- activates lane clear mode |
| DrawAttackRange | true | on/off | Overlays.h -- range circle |
| DrawMissiles | true | on/off | Overlays.h -- missile rectangles |
| ZoomHackEnabled | false | on/off | Visuals.cpp -- camera zoom |
| MaxZoomValue | 2250 | 2250-6000 | Visuals.cpp -- zoom distance |

---

## 6. Adding a New Setting

To add a new setting that persists across sessions:

1. **Declare it** in `Settings.h`:
```cpp
namespace Settings {
    inline bool MyNewFeature = false;
}
```

2. **Add to LoadSettings** in `ConfigManager.cpp`:
```cpp
Settings::MyNewFeature = tbl["MySection"]["MyNewFeature"].value_or(Settings::MyNewFeature);
```

3. **Add to SaveSettings** in `ConfigManager.cpp`:
```cpp
{ "MySection", toml::table{
    { "MyNewFeature", Settings::MyNewFeature }
}}
```

4. **Add a menu control** in `Renderer.cpp` (or your champion's `OnMenu`):
```cpp
if (ImGui::Checkbox("My New Feature", &Settings::MyNewFeature))
    ConfigManager::SaveSettings();
```
