# Pattern Scanner & Offset Discovery

This document explains what RVAs are, how byte patterns work, and how the `offsetscan` tool converts patterns into usable offsets.

---

## Table of Contents

1. [What is an RVA?](#what-is-an-rva)
2. [Memory Layout of a PE File](#memory-layout-of-a-pe-file)
3. [What is a Pattern (AOB)?](#what-is-a-pattern-aob)
4. [How Patterns Become Offsets](#how-patterns-become-offsets)
5. [The Three Types of Offsets](#the-three-types-of-offsets)
6. [RIP-Relative Addressing (The Math)](#rip-relative-addressing-the-math)
7. [Using the offsetscan Tool](#using-the-offsetscan-tool)
8. [Worked Examples](#worked-examples)
9. [How to Add New Patterns](#how-to-add-new-patterns)

---

## What is an RVA?

**RVA** stands for **Relative Virtual Address**. It's the distance (in bytes) from the start of a loaded module (DLL/EXE) to some location within it.

```
Absolute Address = ImageBase + RVA

Example:
  ImageBase     = 0x7FF7AD260000   (where Windows loaded League.exe)
  RVA           = 0x1E257E0        (offset to LocalPlayer pointer)
  Absolute Addr = 0x7FF7CB485DE0   (actual address in RAM)
```

Why use RVAs instead of absolute addresses?

- **ASLR** (Address Space Layout Randomization) changes the ImageBase every time the game starts
- RVAs stay the same regardless of where the module is loaded
- Your code reads the ImageBase at runtime with `GetModuleHandle(nullptr)` and adds the RVA

In code (from `SDK.h`):
```cpp
uintptr_t base = (uintptr_t)GetModuleHandleA(nullptr);  // ImageBase
uintptr_t localPlayer = SafeReadSDK<uintptr_t>(base + Offset::oLocalPlayer);
```

---

## Memory Layout of a PE File

A PE (Portable Executable) file has sections. The key ones for us:

```
+------------------+  0x00000000 (ImageBase)
| PE Headers       |  DOS header, NT headers, section table
+------------------+  0x00001000 (.text RVA)
| .text section    |  Executable code (25 MB for League)
|                  |  This is where all game functions live
+------------------+  0x01927000 (.rdata RVA)
| .rdata section   |  Read-only data (strings, vtables, constants)
+------------------+  0x01D4E000 (.data RVA)
| .data section    |  Read-write globals (LocalPlayer ptr, HeroList ptr, etc.)
|                  |  ^^ This is where global pointers live
+------------------+
| ... more sections|
+------------------+
```

Key insight: **Global pointers** (like `oLocalPlayer = 0x1E257E0`) point into `.data`. **Functions** (like `AttackDelay = 0x574390`) point into `.text`. **Struct member offsets** (like `oHealth = 0x1080`) are just numbers — they don't point to a section.

---

## What is a Pattern (AOB)?

A **pattern** (also called **AOB** = Array of Bytes, or **signature**) is a sequence of bytes that identifies a specific instruction in the game's code. Wildcards (`?`) mark bytes that change between patches.

From `patterns/patterns.txt`:
```
oLocalPlayer: 48 8B 0D ? ? ? ? 4C ? ? 74 ? 49
```

This means:
- `48 8B 0D` — these 3 bytes must match exactly (they're the instruction opcode)
- `? ? ? ?` — any 4 bytes (these are the displacement that changes each patch)
- `4C ? ? 74 ? 49` — surrounding instructions that confirm we found the right spot

In x86-64 assembly, `48 8B 0D` is:
```asm
mov rcx, [rip + ????????]    ; Load a 64-bit value from a global address
```

The `????????` is a 4-byte signed offset (the displacement) that tells the CPU where the data is relative to the next instruction. This displacement changes when Riot recompiles the game, but the **opcode** (`48 8B 0D`) stays the same because the operation hasn't changed — it's still loading the same pointer.

---

## How Patterns Become Offsets

The complete pipeline:

```
1. SCAN
   Search the .text section for byte pattern match
   Result: Match RVA (where the pattern was found)

2. CLASSIFY
   Look at the matched bytes to determine instruction type:
   - Is it a RIP-relative instruction? (reads a global pointer)
   - Is it a struct access? (reads a field from an object)
   - Is it a function prologue? (marks the start of a function)

3. RESOLVE
   Extract the actual offset based on the instruction type:
   - RIP-relative → math to compute the target address
   - Struct access → read the displacement directly
   - Function entry → the match RVA itself IS the offset
```

---

## The Three Types of Offsets

### Type 1: Global Pointers (RIP-relative)

These are addresses of global variables in the `.data` section. The game stores things like the local player pointer, hero list, and game time at fixed locations.

**Example pattern:** `oLocalPlayer: 48 8B 0D ? ? ? ?`
**Instruction:** `mov rcx, [rip+0x01B1B74B]`
**Resolution:** RIP-relative math → `0x1E257E0`

The offset `0x1E257E0` is the RVA of the global variable. At runtime:
```cpp
uintptr_t localPlayerPtr = base + 0x1E257E0;  // address of the pointer
uintptr_t localPlayer = *(uintptr_t*)localPlayerPtr;  // the actual player object
```

### Type 2: Struct Member Offsets (Displacement)

These are offsets within a game object (struct/class). When the game reads a champion's health, it does:
```asm
mov eax, [rcx + 0x1080]    ; rcx = GameObject*, 0x1080 = health field offset
```

**Example pattern:** `oTargetable: 0F B6 83 ? ? ? ?`
**Instruction:** `movzx eax, byte [rbx+0xED0]`
**Resolution:** The displacement `0xED0` IS the offset — no math needed.

At runtime:
```cpp
bool targetable = SafeReadSDK<uint8_t>(gameObject + 0xED0);
```

### Type 3: Function RVAs (Entry Points + CALL targets)

Some patterns identify functions by their prologue or by a `CALL` instruction that calls them.

**Function prologue example:** `isTurret: 40 53 48 83 EC 20 48 8B D9 48 85 C9 74 27`
This matches the start of the `isTurret()` function. The match RVA (`0x31CAF0`) is the function's address.

**CALL example:** `AttackDelay: E8 ? ? ? ? 33 C0 F3 0F 11 83`
`E8` is a `CALL rel32` instruction. The `?` bytes encode where the function is, resolved via RIP-relative math → `0x574390`.

---

## RIP-Relative Addressing (The Math)

In x86-64, most instructions that reference global data use **RIP-relative addressing**. "RIP" is the instruction pointer — the address of the *next* instruction after the current one.

### Formula

```
target_RVA = (match_RVA + instruction_length) + displacement
                ^-- next instruction's RVA --^    ^-- signed 32-bit value
```

### Step by Step

Take the pattern match for `oLocalPlayer` at RVA `0x30A08E`:

```
Bytes at match: 48 8B 0D 4B B7 B1 01 4C 3B F9 ...
                ^^^^^^^^                            opcode (MOV rcx, [rip+disp32])
                         ^^^^^^^^^^^                displacement bytes (little-endian)
                                     ^^             next instruction starts here
```

1. **Instruction length** = 7 bytes (`48 8B 0D` + 4 displacement bytes)
2. **Displacement** = bytes `4B B7 B1 01` in little-endian = `0x01B1B74B`
3. **Next instruction RVA** = `0x30A08E + 7 = 0x30A095`
4. **Target RVA** = `0x30A095 + 0x01B1B74B = 0x1E257E0`

That's `oLocalPlayer = 0x1E257E0` — matching `Offsets.h` exactly.

### Common Instruction Formats

| Bytes | Instruction | Disp at | Instr len | Example |
|-------|------------|---------|-----------|---------|
| `48 8B 0D/05/15` | `MOV reg, [rip+disp32]` | byte 3 | 7 | oLocalPlayer, oHeroList |
| `48 8D 0D/05` | `LEA reg, [rip+disp32]` | byte 3 | 7 | ViewProjectionMatrix |
| `4C 8B 3D` | `MOV r15, [rip+disp32]` | byte 3 | 7 | ViewPort |
| `E8` | `CALL rel32` | byte 1 | 5 | AttackDelay, WorldToScreen |
| `F3 0F 5C 35` | `SUBSS xmm6, [rip+disp32]` | byte 4 | 8 | oGametime |
| `FF 15` | `CALL [rip+disp32]` (indirect) | byte 2 | 6 | oIAT_GetCursorPos |

---

## Using the offsetscan Tool

### Prerequisites

- Go 1.21+ installed (`go version`)
- A memory dump of League of Legends (`.exe` file from Scylla/x64dbg)

### Building

```bash
cd tools/offsetscan
go build -o offsetscan.exe .
```

### Usage

```bash
# Basic scan — table output
offsetscan -dump "path/to/League of Legends_dump.exe" -patterns patterns/patterns.txt

# Generate Offsets.h
offsetscan -dump "path/to/dump.exe" -patterns patterns/patterns.txt -format header

# JSON output (for scripts)
offsetscan -dump "path/to/dump.exe" -patterns patterns/patterns.txt -format json

# Verbose mode (shows matched bytes, multiple match warnings)
offsetscan -dump "path/to/dump.exe" -patterns patterns/patterns.txt -verbose
```

### Output Formats

**Console (default)** — grouped table showing pattern name, match RVA, instruction class, and resolved offset:
```
  --- Global Pointers (RIP-relative) ---
  oLocalPlayer                   | 0x0030A08E | MOV reg, [rip+disp32]        | 0x1E257E0
  oHeroList                      | 0x00DFE1B5 | MOV reg, [rip+disp32]        | 0x1DEAEE0

  --- Struct Member Offsets ---
  oTargetable                    | 0x00299E99 | MOVZX [reg+disp32]           | 0xED0
  TeamID                         | 0x002AF212 | MOVZX [reg+disp32]           | 0x259
```

**Header** (`-format header`) — generates a complete C++ header file you can paste into `Offsets.h`.

**JSON** (`-format json`) — machine-readable output for automation scripts.

---

## Worked Examples

### Example 1: Finding oGametime (RIP-relative SSE)

Pattern: `F3 0F 5C 35 ? ? ? ? 0F 28 F8`

```asm
; F3 0F 5C 35 = SUBSS xmm6, [rip+disp32]
; This instruction subtracts the game time float from xmm6
subss xmm6, dword ptr [rip+0x01A7B44C]
```

1. Pattern found at RVA `0x37E1FC`
2. Instruction is 8 bytes: `F3 0F 5C 35` (4 opcode) + 4 displacement
3. Displacement bytes: `4C B4 A7 01` → `0x01A7B44C`
4. Next instruction RVA: `0x37E1FC + 8 = 0x37E204`
5. Target: `0x37E204 + 0x01A7B44C = 0x1DF9650`

Result: `oGametime = 0x1DF9650`

### Example 2: Finding TeamID (Struct Offset)

Pattern: `0F B6 88 ? ? ? ? EB 07 0F B6 8E`

```asm
; 0F B6 88 = MOVZX ecx, byte ptr [rax+disp32]
; Reads the team ID byte from a game object
movzx ecx, byte ptr [rax+0x259]
```

1. Pattern found at RVA `0x2AF212`
2. Instruction has `mod=10` (disp32), displacement at byte 3
3. Displacement bytes: `59 02 00 00` → `0x259`
4. No RIP math — the displacement IS the struct offset

Result: `TeamID = 0x259`

### Example 3: Finding AttackDelay (CALL target)

Pattern: `E8 ? ? ? ? 33 C0 F3 0F 11 83`

```asm
; E8 = CALL rel32
; Calls the GetAttackDelay function, followed by xor eax,eax
call 0x574390
xor eax, eax
```

1. Pattern found at RVA `0x2B2AFC`
2. `E8` is CALL rel32: displacement at byte 1, instruction length 5
3. Displacement bytes: `8F 18 2C 00` → `0x002C188F`
4. Next instruction RVA: `0x2B2AFC + 5 = 0x2B2B01`
5. Target: `0x2B2B01 + 0x002C188F = 0x574390`

Result: `AttackDelay = 0x574390`

---

## How to Add New Patterns

### Step 1: Find the instruction in a disassembler

Open the memory dump in IDA or use x64dbg while attached to the game. Find the instruction that accesses the data you want.

Example: You found this instruction that reads a new stat:
```asm
0x00400100:  F3 0F 10 81 AC 17 00 00    movss xmm0, [rcx+0x17AC]
```

### Step 2: Build the pattern

Take the instruction bytes and wildcard the parts that might change:
- Opcodes stay the same (Riot doesn't change what the instruction does)
- Displacements that are struct offsets (`0x17AC`) usually stay stable between patches too
- Add surrounding bytes for uniqueness

For the example above, the struct offset `0x17AC` is what we want, so we wildcard it:
```
oObjAD: F3 0F 10 81 ? ? ? ?
```

But this might match too many places. Add the next instruction as context:
```
oObjAD: F3 0F 10 81 ? ? ? ? F3 0F 11 47
```

### Step 3: Add to patterns.txt

```
oObjAD: F3 0F 10 81 ? ? ? ? F3 0F 11 47
```

### Step 4: Run the scanner

```bash
offsetscan -dump "dump.exe" -patterns patterns/patterns.txt -verbose
```

Check that:
- It finds exactly 1 match (if multiple, add more context bytes)
- The resolved offset looks reasonable (struct offsets are usually < 0x10000)

### Step 5: Update Offsets.h

Take the resolved offset and add it:
```cpp
inline constexpr uint64_t oObjAD = 0x17AC;
```

### Tips

- **RIP-relative patterns** (`48 8B 0D`, `E8`, etc.): wildcard the displacement bytes, keep the surrounding opcodes
- **Struct offset patterns** (`0F B6 83`, `F3 0F 10 80`, etc.): wildcard the displacement, keep surrounding context
- **Function entry patterns** (`40 53 48 83 EC`): use as many prologue bytes as needed for uniqueness
- If your pattern has too many matches, add more surrounding bytes
- If your pattern breaks after a patch, it means Riot changed the surrounding code — find a new unique sequence

---

## Architecture of the offsetscan Tool

```
tools/offsetscan/
  main.go       CLI entry, orchestration
  pe.go         PE header parsing (finds .text section in dump)
  pattern.go    Parses patterns.txt format
  scanner.go    Byte-by-byte pattern matching with wildcards
  resolver.go   Classifies instructions and resolves offsets
  output.go     Console table, C++ header, and JSON output
```

The flow:
1. `pe.go` parses the dump → extracts `.text` section raw bytes and its RVA
2. `pattern.go` reads `patterns.txt` → list of named patterns with wildcards
3. `scanner.go` searches `.text` bytes for each pattern → match position
4. `resolver.go` reads actual bytes at match, classifies the instruction, extracts displacement, and computes the final offset
5. `output.go` formats results for display
