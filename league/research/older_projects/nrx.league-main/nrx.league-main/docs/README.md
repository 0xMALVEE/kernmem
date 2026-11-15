# nrx.league Documentation

Welcome to the nrx.league documentation. This is a comprehensive guide to understanding, building, and extending the LeagueOne internal framework.

## What Is This Project?

LeagueOne is an **internal DLL framework** for League of Legends. "Internal" means it runs inside the game's own process (via DLL injection) rather than as a separate external program. This gives it direct access to the game's memory, rendering pipeline, and input system.

The framework provides:

- **Automated combat** -- an orbwalker that handles attack timing, movement, and target selection
- **Skill shot prediction** -- a physics engine that predicts where enemies will be when your spell arrives
- **Champion scripts** -- modular AI logic per champion (Ezreal is implemented as the reference script)
- **Visual overlays** -- attack range circles, missile danger zones, prediction paths, and damage indicators drawn directly onto the game
- **Zoom hack** -- extends the maximum camera zoom distance
- **In-game configuration UI** -- a full ImGui menu system toggled with Left Shift
- **Offset discovery tools** -- 16+ built-in scanners for finding new memory offsets after game patches

## Documentation Index

| Document | What It Covers |
|----------|---------------|
| [Architecture](./ARCHITECTURE.md) | How the DLL hooks into the game, thread model, frame loop, and file map |
| [Game Memory & SDK](./GAME_MEMORY.md) | How game objects are read from memory, the SDK layer, safe reads, and pointer chains |
| [Combat Systems](./COMBAT_SYSTEMS.md) | Orbwalker, prediction engine, target selector, and the micro-flick input system |
| [Champion Scripting](./CHAMPION_SCRIPTING.md) | How to write a new champion script using the IChampion interface |
| [Rendering & UI](./RENDERING_UI.md) | DirectX 11 hook, ImGui integration, overlay drawing, and the menu system |
| [Configuration](./CONFIGURATION.md) | TOML settings, League config auto-detection, and the settings namespace |
| [Offsets Guide](./OFFSETS_GUIDE.md) | What offsets are, how they work, and how to discover new ones after patches |
| [Pattern Scanner](./PATTERN_SCANNER.md) | What RVAs are, how byte patterns work, and the `offsetscan` Go tool |
| [Building](./BUILDING.md) | How to compile the project with Visual Studio or the command line |

## Quick Start

1. **Build the project** -- see [Building](./BUILDING.md)
2. **Read the architecture** -- see [Architecture](./ARCHITECTURE.md) to understand how the DLL starts up
3. **Understand memory reading** -- see [Game Memory & SDK](./GAME_MEMORY.md) for the foundation everything else is built on
4. **Learn the combat loop** -- see [Combat Systems](./COMBAT_SYSTEMS.md) for how orbwalking and prediction work
5. **Write your own champion script** -- see [Champion Scripting](./CHAMPION_SCRIPTING.md) for a step-by-step tutorial

## Project at a Glance

```
nrx.league/
  leagueone.sln            Visual Studio solution
  build.bat                Command-line build script
  Makefile                 GNU Make build file
  docs/                    This documentation
  patterns/                Byte patterns for runtime scanning
    patterns.txt
  leagueone/               All source code
    leagueone.cpp          DLL entry point & hooks
    SDK.h                  Core memory reading layer
    Offsets.h              All game memory offsets
    GameReader.h/.cpp      Per-frame game state reader
    GameAPI.h/.cpp         HTTP stats + ping reader
    Orbwalker.h/.cpp       Auto-attack automation
    Prediction.h/.cpp      Skillshot prediction engine
    TargetSelector.h/.cpp  Target selection logic
    Input.h/.cpp           Micro-flick input system
    Renderer.h/.cpp        D3D11 + ImGui rendering
    Visuals.h/.cpp         Zoom hack
    Overlays.h             Drawing overlays (ranges, missiles, paths)
    IChampion.h            Champion script interface
    Ezreal.h/.cpp          Ezreal script implementation
    ModuleManager.h/.cpp   Champion script loader
    ConfigManager.h/.cpp   TOML settings save/load
    ConfigLoader.h/.cpp    League config auto-detection
    PatternScanner.h/.cpp  AOB pattern scanning
    Debugger.h/.cpp        Offset discovery scanners
    Utils.h/.cpp           WorldToScreen, game time
    Vector.h               Vector2/Vector3 math
    Logger.h               In-game debug log
    Settings.h             Global settings namespace
    Debug.h                Console allocation
    imgui/                 ImGui library (third-party)
    lib/                   toml++ library (third-party)
```

## Key Concepts for Beginners

### What is DLL Injection?

A DLL (Dynamic Link Library) is a file containing code that can be loaded into a running process. DLL injection is the technique of forcing a target process (the game) to load your DLL. Once loaded, your code runs inside the game's address space, meaning you can read and write the game's memory directly -- no need for `ReadProcessMemory` or other external APIs.

### What is an Offset?

An offset is a number that represents "how many bytes from a known starting point" a piece of data lives. For example, if a champion object starts at address `0x1A2B3C00` and the health value is at offset `0x1080`, then health is stored at address `0x1A2B3C00 + 0x1080 = 0x1A2B4C80`. Offsets change when the game is patched, which is why the project includes tools to rediscover them.

### What is Hooking?

Hooking means intercepting a function call so your code runs instead of (or in addition to) the original. This project hooks the DirectX `Present()` function -- every time the game renders a frame, our code runs first, allowing us to draw overlays and run game logic in sync with the game.

### What is ImGui?

ImGui (Dear ImGui) is a lightweight C++ UI library designed for rendering debug interfaces and tools inside graphical applications. It draws directly using your graphics API (DirectX 11 in this case) and requires no operating system windows or widgets. The entire in-game settings menu is built with ImGui.

### What is WorldToScreen?

3D games store positions in world coordinates (X, Y, Z). To draw something at a specific game position on your monitor, you need to convert those 3D coordinates to 2D screen pixels. This is called WorldToScreen (W2S) projection, and it uses the game's view and projection matrices to perform the math.
