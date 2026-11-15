# Building

This document explains how to compile LeagueOne from source.

---

## Prerequisites

- **Visual Studio 2022 or 2025** with the **"Desktop development with C++"** workload installed
  - Specifically: MSVC v143 compiler, Windows 10/11 SDK, and C++17 support
- **Windows 10 or 11** (the project targets Windows only)

No external dependencies need to be installed. ImGui and toml++ are included in the source tree under `leagueone/imgui/` and `leagueone/lib/`.

---

## Method 1: Visual Studio IDE

1. Open `leagueone.sln` in Visual Studio
2. Select the build configuration from the toolbar:
   - **Release | x64** -- Production DLL (recommended)
   - **Debug | x64** -- Debug DLL with full debug symbols
3. Press `Ctrl+Shift+B` to build (or Build > Build Solution)
4. The output DLL will be at:
   - Release: `x64\Release\leagueone.dll`
   - Debug: `x64\Debug\leagueone.dll`

---

## Method 2: Command Line (build.bat)

The project includes a `build.bat` script that auto-detects your Visual Studio installation:

```
build.bat              Build Release|x64 DLL (default)
build.bat debug        Build Debug|x64 DLL
build.bat release-x86  Build Release|Win32 EXE
build.bat debug-x86    Build Debug|Win32 EXE
build.bat all          Build all 4 configurations
build.bat clean        Clean Release|x64
build.bat clean-all    Clean all configurations
build.bat rebuild      Full rebuild Release|x64
build.bat info         Show detected MSBuild path
```

Open a command prompt (or terminal) in the project root and run:

```
build.bat
```

The script searches for MSBuild in these locations (in order):
1. Visual Studio 2025 Community/Professional/Enterprise
2. Visual Studio 2022 Community/Professional/Enterprise
3. MSBuild on PATH

---

## Method 3: GNU Make

If you have GNU Make installed (e.g., via MSYS2, MinGW, or Chocolatey):

```
make              Build Release|x64 DLL (default)
make debug        Build Debug|x64 DLL
make release-x86  Build Release|Win32 EXE
make debug-x86    Build Debug|Win32 EXE
make all-configs  Build all 4 configurations
make clean        Clean Release|x64
make clean-all    Clean everything
make rebuild      Full rebuild Release|x64
make info         Show build environment
```

---

## Build Configurations

| Configuration | Platform | Output Type | Output Path | Purpose |
|--------------|----------|-------------|-------------|---------|
| Release\|x64 | 64-bit | DLL | `x64\Release\leagueone.dll` | Production injectable DLL |
| Debug\|x64 | 64-bit | DLL | `x64\Debug\leagueone.dll` | Debug DLL with symbols |
| Release\|Win32 | 32-bit | EXE | `Release\leagueone.exe` | Console app (testing) |
| Debug\|Win32 | 32-bit | EXE | `Debug\leagueone.exe` | Console app (testing) |

The **x64 DLL** configurations are what you actually inject into the game. The Win32 EXE configurations exist for testing components in a standalone console application.

---

## Project Structure (Build-Relevant)

```
nrx.league/
  leagueone.sln                   Solution file (open this in VS)
  build.bat                        Command-line build script
  Makefile                         GNU Make build file
  leagueone/
    leagueone.vcxproj              Project file (build settings)
    leagueone.vcxproj.user         User-specific settings (not committed)
    *.cpp, *.h                     Source files
    imgui/                         ImGui library (compiled as part of project)
      backends/
        imgui_impl_dx11.cpp/.h     DirectX 11 backend
        imgui_impl_win32.cpp/.h    Win32 backend
      imgui.cpp, imgui_draw.cpp, imgui_tables.cpp, imgui_widgets.cpp, imgui_demo.cpp
    lib/
      toml.hpp                     toml++ header-only library
      impl/                        toml++ implementation files
```

---

## Compiler Settings

All configurations use:
- **C++17 standard** (`/std:c++17`)
- **MSVC v143 toolset** (Visual Studio 2022/2025)
- **Unicode character set**
- **Level 3 warnings** (`/W3`)
- **SDL checks** enabled
- **Conformance mode** enabled

Additional include directories (set for all configurations):
- `$(ProjectDir)imgui` -- ImGui headers
- `$(ProjectDir)imgui\backends` -- ImGui backend headers
- `$(ProjectDir)lib` -- toml++ headers

Linked libraries (set for all configurations):
- `d3d11.lib` -- DirectX 11
- `dxgi.lib` -- DXGI (swap chain)
- `winhttp.lib` -- WinHTTP (for GameAPI HTTP polling)
- Standard Windows libraries (kernel32, user32, gdi32, etc.)

Release configurations add:
- Whole program optimization (`/GL`)
- Function-level linking (`/Gy`)
- Intrinsic functions (`/Oi`)
- COMDAT folding and reference optimization in the linker

---

## Troubleshooting

### "Cannot open include file: 'imgui.h'"

The `AdditionalIncludeDirectories` setting is missing for your build configuration. This was fixed for all 4 configurations in the `.vcxproj` file. If you see this error, make sure you're using the latest `.vcxproj`.

### "unresolved external symbol" for D3D11 or WinHTTP functions

The linker needs `d3d11.lib`, `dxgi.lib`, and `winhttp.lib`. These are set in `AdditionalDependencies` in the `.vcxproj`. The source code also has `#pragma comment(lib, "d3d11.lib")` and `#pragma comment(lib, "winhttp.lib")` as fallbacks.

### "MSBuild.exe not found" from build.bat

Install Visual Studio 2022 or 2025 with the "Desktop development with C++" workload. The build script searches common installation paths but can't find MSBuild if Visual Studio isn't installed.

### Build succeeds but DLL doesn't work

Make sure you're building the **x64** configuration (not Win32). League of Legends is a 64-bit process and can only load 64-bit DLLs.

---

## Adding New Source Files

When you add a new `.cpp` or `.h` file:

1. **In Visual Studio**: Right-click the project > Add > Existing Item (or New Item)
2. **Manually**: Add entries to `leagueone.vcxproj`:

```xml
<ItemGroup>
  <ClCompile Include="MyNewFile.cpp" />
</ItemGroup>
<ItemGroup>
  <ClInclude Include="MyNewFile.h" />
</ItemGroup>
```

The build system will automatically compile and link the new files.
