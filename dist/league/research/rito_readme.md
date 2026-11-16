# rito

`rito` reconstructs the module's sections, resolves hashed system imports, generates import trampolines, and uses [LIEF](https://lief.re/) to write the resulting PE image to disk.

> [!NOTE]
> This is an independent research project. It is not affiliated with or endorsed by Riot Games.

## Features

- Validates the `RITO` file signature
- Restores the original image base and entry point
- Reconstructs embedded module sections
- Resolves hashed module and function names
- Rebuilds the PE import table
- Generates RIP-relative import stubs
- Writes a native 64-bit Windows DLL

## Requirements

- 64-bit Windows
- A C++20-compatible compiler
- [LIEF](https://lief.re/) with C++ headers and libraries

The import resolver uses Windows module enumeration APIs and reads matching dependency exports from `%SystemRoot%\System32`.

## Building

The repository does not currently include a generated project or build-system configuration. Compile the following source files as an x64 console application:

```text
Main.cpp
VLX/Image.cpp
VLX/Imports/Imports.cpp
VLX/Sections/Sections.cpp
```

Add the repository root and the LIEF headers to the compiler's include paths, then link against your LIEF library. Enable C++20 or later.

The exact library name and any additional linker options depend on how LIEF was built or installed.

## Usage

The current entry point uses fixed filenames. Place the source module in the working directory as `VLXModule.bin`, then run:

```powershell
.\rito.exe
```

On success, the reconstructed image is written to:

```text
VLXModule.dll
```

The input and output paths can be changed in `Main.cpp`:

```cpp
#include "VLX/Image.hpp"

int main()
{
    VLX::Image image;

    if (!image.Initialize("VLXModule.bin"))
        return 1;

    if (!image.Build("VLXModule.dll"))
        return 1;

    return 0;
}
```

## How it works

1. **Parse the header** — The input is loaded and checked for the `RITO` signature. Its image base and entry-point RVA are copied into a new PE32+ image.
2. **Rebuild sections** — Section descriptors are read from the custom header and their contents are added to the PE image.
3. **Resolve imports** — Stored hashes are compared with loaded Windows modules and their exported functions.
4. **Generate stubs** — RIP-relative jump stubs are emitted for resolved imports and the original address references are patched.
5. **Write the DLL** — LIEF builds the import data and serializes the reconstructed image.

### Header fields used

| Offset | Size | Purpose |
| ---: | ---: | --- |
| `0x00` | 4 bytes | `RITO` magic signature |
| `0x08` | 8 bytes | Preferred image base |
| `0x14` | 4 bytes | Entry-point RVA |
| `0x20` | 4 bytes | Import table offset |
| `0x24` | 4 bytes | Import module count |
| `0x30` | 4 bytes | Section table offset |
| `0x34` | 4 bytes | Section count |

## Project structure

```text
.
├── Main.cpp
└── VLX
    ├── Image.cpp
    ├── Image.hpp
    ├── Imports
    │   ├── Imports.cpp
    │   └── Imports.hpp
    └── Sections
        ├── Sections.cpp
        └── Sections.hpp
```

- `VLX::Image` owns the source data and coordinates PE construction.
- `VLX::Sections` reconstructs section contents from the streamed module.
- `VLX::Imports` resolves hashes, adds imports, creates stubs, and patches references.

## Credits

This project was developed in part by [Xyrem](https://github.com/Xyrem).

