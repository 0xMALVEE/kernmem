"""
Offsets for Valorant - patch-stamped, source-tagged.

Provenance: transcribed from the community reversal thread archived at
valorant/docs/reversal-structs-and-offsets.md (patch 12.07, HVCI on).

UE5, so two things differ from the League table:

  * FVector / FRotator are **doubles**, not floats. Every vector read is 3 x 8
    bytes and the rotation is 3 x 8 bytes of degrees.
  * UWorld is not stored in the clear. It is an index into a seven-entry key
    state, run through Decrypt_UWorld with a per-build key. Both halves are
    offsets here and the decrypt lives in reader.py.

What `verified` means, exactly:
  verified=True   present in the archived dump AND self-consistent against the
                  other entries (chain resolves, plausible type).
  verified=False  cited once, uncorroborated, or shape guessed.

True is "transcribed and coherent", not "proven against a live process".
Valorant ships a new build every couple of weeks and this table is stamped
12.07, so expect drift; `allow_stale` exists for exactly that.
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Optional


# --------------------------------------------------------------------------- #
# provenance
# --------------------------------------------------------------------------- #

PATCH = "12.07"
CLIENT = "12.07"
SCRAPED = "2026-11-15"
SOURCE = "../docs/reversal-structs-and-offsets.md"
THREAD = (
    "https://www.unknowncheats.me/forum/valorant/"
    "703073-valorant-reversal-structs-offsets.html"
)

#: Builds this table has ever been stamped with.
KNOWN_PATCHES = {PATCH}

GAME_IMAGE = "VALORANT-Win64-Shipping.exe"


class OffsetsError(Exception):
    """Base error for offset problems."""


class StaleOffsets(OffsetsError):
    """Raised when the game build is not the build these offsets came from."""


@dataclass(frozen=True)
class Offset:
    """One addressable value.

    A bare offset is useless on its own: every entry names the module it is
    relative to and the pointer steps that reach it. `constant` marks literals
    (strides, caps) that are never dereferenced.
    """

    name: str
    offset: int
    module: str = GAME_IMAGE
    #: Pointer steps applied before `offset` is added. Empty = offset is used
    #: directly against the module base.
    chain: tuple[int, ...] = ()
    note: str = ""
    verified: bool = False
    constant: bool = False

    @property
    def label(self) -> str:
        if self.constant:
            if isinstance(self.offset, float):
                return f"constant {self.offset:g}"
            return f"constant {self.offset:#x}"

        return f"{self.module}+{self.offset:#x}" + (
            f" (chain {len(self.chain)})" if self.chain else ""
        )


# --------------------------------------------------------------------------- #
# root objects - module-relative
# --------------------------------------------------------------------------- #

OFFSETS: dict[str, Offset] = {
    # -- encrypted world ---------------------------------------------------- #
    "GWorldState": Offset(
        "GWorldState", 0x9F3F050,
        note="seven 64-bit key slots; not the world itself", verified=True,
    ),
    "GWorldKey": Offset(
        "GWorldKey", 0x24A04C0,
        note="key fed to Decrypt_UWorld alongside GWorldState", verified=True,
    ),

    # -- engine chains ------------------------------------------------------ #
    "GameState": Offset(
        "GameState", 0x0178,
        note="UWorld -> AGameStateBase; holds the player array", verified=True,
    ),
    "GameInstance": Offset(
        "GameInstance", 0x01D8,
        note="UWorld -> UGameInstance", verified=False,
    ),
    "LocalPlayers": Offset(
        "LocalPlayers", 0x0040,
        note="UGameInstance -> TArray<ULocalPlayer>", verified=False,
    ),
    "PlayerController": Offset(
        "PlayerController", 0x0038,
        note="ULocalPlayer -> APlayerController", verified=False,
    ),
    "AcknowledgedPawn": Offset(
        "AcknowledgedPawn", 0x0510,
        note="APlayerController -> the pawn we control", verified=True,
    ),
    "CameraManager": Offset(
        "CameraManager", 0x0520,
        note="APlayerController -> APlayerCameraManager", verified=True,
    ),

    # -- camera ------------------------------------------------------------- #
    "CameraCachePriv": Offset(
        "CameraCachePriv", 0x17B0,
        note="APlayerCameraManager -> FCameraCacheEntry (0x2630 + 0x10 on newer builds)",
        verified=False,
    ),
    "PovOffset": Offset(
        "PovOffset", 0x0010,
        note="FCameraCacheEntry -> FMinimalViewInfo (Location, Rotation, FOV)",
        constant=True, verified=True,
    ),
    "PovLocation": Offset("PovLocation", 0x0000, constant=True, note="FVector, doubles", verified=True),
    "PovRotation": Offset("PovRotation", 0x0018, constant=True, note="FRotator, doubles, degrees", verified=True),
    "PovFov": Offset("PovFov", 0x0030, constant=True, note="float, vertical degrees", verified=True),

    # -- pawn --------------------------------------------------------------- #
    "RootComponent": Offset(
        "RootComponent", 0x0288,
        note="AActor -> USceneComponent; position lives under it", verified=True,
    ),
    "RelativeLocation": Offset(
        "RelativeLocation", 0x0170,
        note="USceneComponent -> FVector (doubles)", verified=True,
    ),
    "PlayerState": Offset(
        "PlayerState", 0x0480,
        note="APawn -> APlayerState; holds the display name", verified=True,
    ),
    "PawnPrivate": Offset(
        "PawnPrivate", 0x04E0,
        note="APlayerState -> APawn", verified=True,
    ),
    "PlayerName": Offset(
        "PlayerName", 0x03A8,
        note="APlayerState -> FString display name", verified=False,
    ),
    "TeamComponent": Offset(
        "TeamComponent", 0x06A8,
        note="APawn -> team component", verified=True,
    ),
    "TeamId": Offset(
        "TeamId", 0x00F8,
        note="team component -> int team id; same value on every pawn of a side",
        verified=False,
    ),
    "DamageHandler": Offset(
        "DamageHandler", 0x0C50,
        note="APawn -> damage handler; health and alive flag live under it", verified=True,
    ),
    "CachedLife": Offset(
        "CachedLife", 0x0200,
        note="damage handler -> float health", verified=True,
    ),
    "CachedMaxLife": Offset(
        "CachedMaxLife", 0x0204,
        note="damage handler -> float max health", verified=True,
    ),
    "Alive": Offset(
        "Alive", 0x01F9,
        note="damage handler -> bAlive, byte", verified=True,
    ),

    # -- player array layout (relative to GameState) ------------------------ #
    "PlayerArray": Offset(
        "PlayerArray", 0x00A0,
        note="ULevel -> TArray<AActor*>; Data, then Num, then Max. Reached through "
             "UWorld->PersistentLevel, not through GameState",
        verified=True,
    ),
    "Dormant": Offset(
        "Dormant", 0x0101,
        note="AActor -> byte; non-zero means not currently replicated",
        verified=True,
    ),
    "LastRenderTime": Offset(
        "LastRenderTime", 0x047C,
        note="AActor -> float; a zero value means the actor is not being drawn",
        verified=True,
    ),
    "ComponentToWorld": Offset(
        "ComponentToWorld", 0x02D0,
        note="USceneComponent -> FTransform; alternative to RelativeLocation",
        verified=True,
    ),
    "ArrayCount": Offset(
        "ArrayCount", 0x0008,
        note="TArray -> Num (int32) - applied after the Data pointer is dereferenced",
        constant=True, verified=True,
    ),
    "PtrStride": Offset("PtrStride", 0x0008, constant=True, note="pointer size", verified=True),
    "MaxActors": Offset("MaxActors", 512, constant=True, note="sanity cap on the array walk", verified=True),

    # -- presentation ------------------------------------------------------- #
    "CapsuleHalfHeight": Offset(
        "CapsuleHalfHeight", 72.0, constant=True,
        note="world units; used to place head and feet for the box", verified=False,
    ),
}


def get(name: str) -> Offset:
    try:
        return OFFSETS[name]
    except KeyError:
        raise OffsetsError(f"no such offset: {name}") from None


def check_patch(patch: Optional[str], allow_stale: bool = False) -> None:
    """Raise unless `patch` matches the stamp this table was scraped for."""

    if patch is None:
        if allow_stale:
            return
        raise StaleOffsets(
            "could not read the game build; pass allow_stale=True to continue anyway"
        )

    if patch in KNOWN_PATCHES or allow_stale:
        return

    raise StaleOffsets(
        f"offsets are stamped {PATCH}, running build is {patch} - "
        "pass allow_stale=True once you have checked the table still resolves"
    )


def unverified() -> list[str]:
    return [name for name, entry in OFFSETS.items() if not entry.verified]


def summary() -> str:
    lines = [
        f"patch {PATCH}  client {CLIENT}  scraped {SCRAPED}",
        f"source {SOURCE}",
        "",
    ]

    for name, entry in OFFSETS.items():
        mark = "    " if entry.verified else "UNV "
        note = f"  # {entry.note}" if entry.note else ""
        lines.append(f"{mark}{name:<20} {entry.label}{note}")

    return "\n".join(lines)
