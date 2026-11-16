# [Release] Valorant Reversal, Structs and Offsets 11.14

Source: https://www.unknowncheats.me/forum/valorant/781274-valorant-reversal-structs-offsets-11-14-a.html
Pages saved: 1–1


---

### Post #4859748 — Lendan — 13th November 2026, 04:53 PM

First, I want to ask how you guys write your stock SkinChanger? How is it done? How is GEngineRVA obtained? I'm not begging, it's just that I've been stuck here for too long. Here are my offsets, based on the Chinese server
![image](https://www.unknowncheats.me/forum/images/smilies/big_smile.png)

  Code:
```
GWorld       = 0xD92E288
GNameAddress = 0xD873D00
FNameKey     = 0xD93FD80
GObjects     = 0xD93FD90

// ============================================================
// VALORANT CN 13.09  -  Poseidon verified offsets
// generated from live process dumps, 2026-11-13
// base = 0x140000000   sizeOfImage = 0xF35F000   .text = 0xA0F6D77
// ============================================================

#pragma once
#include <cstdint>

namespace Offsets {

// ---------------- GLOBALS (signature-verified this session) ----------------
constexpr std::uintptr_t GWorld      = 0xD92E288;   // [VERIFIED] sig + world deref ok
constexpr std::uintptr_t FNamePool   = 0xD873D00;   // [VERIFIED] +0x08 block, +0x0C cursor, +0x10 Blocks
constexpr std::uintptr_t GObjects    = 0xD93FD90;   // [VERIFIED] runtime-located, Num=411k..458k

// ---------------- GNames / FNameKey ----------------
// [VERIFIED] FNameKey RVA = 0xD93FD80  (unique 32-bit match against derived key)
// The key VALUE is per-process random and stored at this RVA.
// Read it directly:  key = *(uint32_t*)(base + 0xD93FD80)
// Fallback: derive from FName entry index 0 ("None").
// FnameState / FnameStateKey from UC are INTERNATIONAL only and DO NOT apply to CN:
//   intl: FnameState = 0xD7C6190 , FnameStateKey = 0xD7C61C8
constexpr std::uintptr_t FNameKey    = 0xD93FD80; // [VERIFIED] unique module-wide match + decodes real names
constexpr std::uintptr_t FNameBlockArray = 0x10;    // FNamePool + 0x10 = Blocks[]
constexpr std::uintptr_t FNameEntryBase  = 0x04;    // entry[0] starts at Blocks[k] + 4
constexpr std::uintptr_t FNameEntryStride = 0x02;   // entry = base + 2 * (id & 0xFFFF)

// ---------------- ENGINE ----------------
// GEngine is NOT REQUIRED. UEngine has no World member in this build.
// SDK struct UEngine shows: LargeFontName 0xD0, GameViewportClientClass 0x178,
// WorldSettingsClass 0x1d8, GameViewport 0xbd0 -- NO World field.
// Use GWorld (0xD92E288) directly.
// International references seen on UC (NOT valid for CN):
//   0x0D727560 / 0x0DB8D780  -> do not use
constexpr std::uintptr_t GEngine     = 0;           // [N/A] UEngine has no World; use GWorld instead

// ---------------- UWorld ----------------
constexpr std::uintptr_t PersistentLevel  = 0x38;   // [SDK]
constexpr std::uintptr_t GameInstance     = 0x1D8;  // [SDK] CN runtime observed 0x1A0 in lobby
constexpr std::uintptr_t OwningGameInstAlt= 0x1A0;  // [VERIFIED runtime] lobby fallback
constexpr std::uintptr_t GameState        = 0x178;  // [SDK]

// ---------------- ULevel / actors ----------------
constexpr std::uintptr_t ActorArray   = 0xA0;       // [VERIFIED]
constexpr std::uintptr_t ActorCount   = 0xA8;       // [VERIFIED]

// ---------------- ULocalPlayer / Controller ----------------
constexpr std::uintptr_t LocalPlayerArray = 0x40;   // [SDK]
constexpr std::uintptr_t PlayerController = 0x38;   // [SDK]
constexpr std::uintptr_t AcknowledgedPawn = 0x518;  // [SDK] also 0x4B0 on some builds
constexpr std::uintptr_t CameraManager    = 0x528;  // [SDK]
constexpr std::uintptr_t CameraPrivate    = 0x17B0; // [SDK]

// ---------------- AShooterCharacter ----------------
constexpr std::uintptr_t PlayerState   = 0x488;     // [VERIFIED]
constexpr std::uintptr_t Mesh          = 0x4F0;     // [SDK]
constexpr std::uintptr_t DamageHandler = 0xC70;     // [SDK] AShooterCharacter only
constexpr std::uintptr_t WasAlly       = 0xF58;     // [SDK] ShooterGame_classes.h:48618
constexpr std::uintptr_t Health        = 0x200;     // [VERIFIED] +200=100.0
constexpr std::uintptr_t Shield        = 0x208;     // [VERIFIED]
constexpr std::uintptr_t MaxShield     = 0x20C;     // [VERIFIED]
constexpr std::uintptr_t Inventory     = 0xC08;     // [SDK] AresInventory
constexpr std::uintptr_t RootComponent = 0x290;     // [VERIFIED]

// ---------------- USceneComponent ----------------
constexpr std::uintptr_t RelativeLocation  = 0x170; // [VERIFIED]
constexpr std::uintptr_t RelativeRotation  = 0x188; // [SDK]
constexpr std::uintptr_t ComponentToWorld  = 0x300; // [SDK] 96-byte FTransform

// ---------------- USkeletalMeshComponent ----------------
constexpr std::uintptr_t BoneArray      = 0x760;    // [SDK]
constexpr std::uintptr_t BoneCount      = 0x768;    // [SDK]
constexpr std::uintptr_t BoneArrayCache = 0x770;    // [SDK]

// ---------------- UAresInventory ----------------
constexpr std::uintptr_t ItemSlots         = 0x1B8; // [SDK] UItemSlot* [0x11]
constexpr std::uintptr_t CurrentEquippable = 0x280; // [SDK]
constexpr std::uintptr_t DesiredEquippable = 0x268; // [SDK]

// ---------------- UAresPersonalizationViewModel ----------------
constexpr std::uintptr_t PersonalizationManager = 0xA0; // [SDK]
constexpr std::uintptr_t InventoryManager       = 0xA8; // [SDK]

// ---------------- UInventoryManager ----------------
constexpr std::uintptr_t EquippableModels = 0x100;  // [UC + SDK]

// ---------------- UEquippableInventoryModel ----------------
constexpr std::uintptr_t SkinDataAsset       = 0x120; // [UC]
constexpr std::uintptr_t SkinChromaDataAsset = 0x120; // [UC]
constexpr std::uintptr_t bIsOwned             = 0x10A; // [UC] local unlock flag
constexpr std::uintptr_t bIsUnlockedByConfig  = 0x116; // [UC] local unlock flag

// ---------------- weapon skin on world actor ----------------
constexpr std::uintptr_t SkinAssetPtr = 0xFB8;      // [VERIFIED] 4/4 hits

// ---------------- UObject ----------------
constexpr std::uintptr_t UObjectClass = 0x10;       // [SDK]
constexpr std::uintptr_t UObjectName  = 0x18;       // [SDK]

// ---------------- functions ----------------
// international RVAs - CN values NOT yet located, use sigscan
constexpr std::uintptr_t ApplySkin_intl            = 0x6709BB0;  // ContentLibrary::ApplySkin
constexpr std::uintptr_t ClearWeaponComponents_intl= 0x670A350;  // ContentLibrary::ClearWeaponComponents

} // namespace Offsets
```

 Have fun and good luck
![image](https://www.unknowncheats.me/forum/images/smilies/hump.gif)

![image](https://www.unknowncheats.me/forum/images/smilies/439.gif)
