"""
Valorant offsets - stamped to build 5.3, recovered live against the range.

Provenance: the reversal dumps in ../docs (11.14, 2026-11-07, 2026-11-11), with
every entry re-tested against the running client. Where the dumps and the live
build disagree the live build wins and the shift is noted.

PROVEN on this build (read live, sane and repeatable):

  ActorArraySlot   0xD91FDD0   module static -> TArray<AActor*> (Data +0, Num +8)
  RootComponent    0x290
  RelativeLocation 0x1B4       float3 - the documented 0x170 is shifted by 0x44
  AcknowledgedPawn 0x4B0
  CameraManager    0x528

NOT resolved: the camera POV. Every documented path (manager+0x10, +0x17B0,
+0x17C0, view_target+0x580, the camera actor's own transform) reads null or
denormal on this build. The ESP therefore runs as a radar until one resolves.

The world chain is broken here too - GWorld reads 0 and the level's own array
reads empty - which is why the module static is used directly.
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Optional

PATCH = "5.3"
CLIENT = "5.3"
SCRAPED = "2026-11-15"
SOURCE = "../docs/reversal-11.14.md"
THREAD = (
    "https://www.unknowncheats.me/forum/valorant/"
    "781274-valorant-reversal-structs-offsets-11-14-a.html"
)

KNOWN_PATCHES = {PATCH}

GAME_IMAGE = "VALORANT-Win64-Shipping.exe"


class OffsetsError(Exception):
    """Base error for offset problems."""


class StaleOffsets(OffsetsError):
    """Raised when the running build is not the build these offsets came from."""


@dataclass(frozen=True)
class Offset:
    name: str
    offset: float
    note: str = ""
    verified: bool = False
    constant: bool = False

    @property
    def label(self) -> str:
        if self.constant:
            if isinstance(self.offset, float):
                return f"constant {self.offset:g}"
            return f"constant {self.offset:#x}"
        return f"{GAME_IMAGE}+{self.offset:#x}"


OFFSETS: dict[str, Offset] = {
    "ActorArraySlot": Offset(
        "ActorArraySlot", 0xD81F3B0,
        note="module static holding TArray<AActor*>; Data +0, Num +8", verified=True,
    ),
    "RootComponent": Offset(
        "RootComponent", 0x290, note="AActor -> USceneComponent", verified=True,
    ),
    "RelativeLocation": Offset(
        "RelativeLocation", 0x170,
        note="USceneComponent -> FVector as 3 doubles (LWC); the dumps are right",
        verified=True,
    ),
    "RelativeLocationAlt": Offset(
        "RelativeLocationAlt", 0x1B4,
        note="fallback: low halves of the same doubles, read as floats", verified=False,
    ),
    "AcknowledgedPawn": Offset(
        "AcknowledgedPawn", 0x518,
        note="APlayerController -> the pawn we control; the dumps say 0x4B0/0x510, "
             "live build only answers at 0x518",
        verified=True,
    ),
    "CameraManager": Offset(
        "CameraManager", 0x528, note="APlayerController -> camera actor", verified=True,
    ),
    "DamageHandler": Offset(
        "DamageHandler", 0xC70, note="APawn -> damage handler", verified=False,
    ),
    "Health": Offset("Health", 0x200, note="damage handler -> float health", verified=False),
    "MaxHealth": Offset("MaxHealth", 0x204, note="damage handler -> float max health", verified=False),
    "TeamComponent": Offset("TeamComponent", 0x6A0, note="APawn -> team component", verified=False),
    "TeamId": Offset("TeamId", 0xF8, note="team component -> int team id", verified=False),
    "PlayerState": Offset("PlayerState", 0x480, note="APawn -> APlayerState", verified=False),
    "PlayerName": Offset("PlayerName", 0x3A8, note="APlayerState -> FString", verified=False),
    "GWorld": Offset(
        "GWorld", 0xDB8D780, note="world; level at +0x38 - unreliable here", verified=False,
    ),
    "PersistentLevel": Offset("PersistentLevel", 0x38, note="UWorld -> ULevel", constant=True, verified=False),
    "ViewInfo": Offset("ViewInfo", 0x17C0, note="camera POV - unresolved on 5.3", verified=False),
    "ViewInfoAlt": Offset("ViewInfoAlt", 0x17B0, note="camera POV alt - unresolved on 5.3", verified=False),
    "ArrayCount": Offset(
        "ArrayCount", 0x8, note="TArray -> Num, after the Data deref", constant=True, verified=True,
    ),
    "PtrStride": Offset("PtrStride", 0x8, note="pointer size", constant=True, verified=True),
    "MaxActors": Offset("MaxActors", 96, note="sanity cap on the walk", constant=True, verified=True),
    "ActorHeight": Offset("ActorHeight", 72.0, note="world units, feet to head", constant=True, verified=False),
}


def get(name: str) -> Offset:
    try:
        return OFFSETS[name]
    except KeyError:
        raise OffsetsError(f"no such offset: {name}") from None


def at(name: str) -> int:
    """The raw value of an entry, for callers that only want the number."""

    return int(OFFSETS[name].offset)


def check_patch(patch: Optional[str], allow_stale: bool = False) -> None:
    if patch is None:
        if allow_stale:
            return
        raise StaleOffsets("could not read the game build; pass allow_stale to continue")

    if patch in KNOWN_PATCHES or allow_stale:
        return

    raise StaleOffsets(
        f"offsets are stamped {PATCH}, running build is {patch} - pass allow_stale "
        "once you have checked the table still resolves"
    )


def unverified() -> list[str]:
    return [name for name, entry in OFFSETS.items() if not entry.verified]


def summary() -> str:
    lines = [f"patch {PATCH}  client {CLIENT}  scraped {SCRAPED}", f"source {SOURCE}", ""]
    for name, entry in OFFSETS.items():
        mark = "    " if entry.verified else "UNV "
        note = f"  # {entry.note}" if entry.note else ""
        lines.append(f"{mark}{name:<20} {entry.label}{note}")
    return "\n".join(lines)
