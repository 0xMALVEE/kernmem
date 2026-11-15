"""
Offsets for League of Legends — patch-stamped, source-tagged.

Provenance: transcribed from a community dump posted in the UC reversal thread
(2026-11-15) for client 16.16.804.9184 / patch 26.16. Every value here is
present in that dump.

What `verified` means, exactly:
  verified=True   the value is in the current dump AND was self-consistent when
                  cross-checked against the other entries (a chain that resolves
                  and a plausible type). It has NOT been confirmed against a live
                  process by this project yet.
  verified=False  cited once, uncorroborated, or shape guessed.

So True is "transcribed and coherent", not "proven". The demo prints UNV marks for
the rest, and `--allow-stale` exists because all of it drifts on patch day.
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Optional


# --------------------------------------------------------------------------- #
# provenance
# --------------------------------------------------------------------------- #

PATCH = "26.16"
CLIENT = "16.16.804.9184"
SCRAPED = "2026-11-15"
SOURCE = "research/unknowncheats/posts/310587_reversal-structs-offsets_2026-11-15.md"
THREAD = "https://www.unknowncheats.me/forum/league-of-legends/310587-league-legends-reversal-structs-offsets.html"

#: Patches this table has ever been stamped with.
KNOWN_PATCHES = {PATCH}


class OffsetsError(Exception):
    """Base error for offset problems."""


class StaleOffsets(OffsetsError):
    """Raised when the client patch is not the patch these offsets came from."""


@dataclass(frozen=True)
class Offset:
    """One addressable value.

    A bare offset is useless on its own: every entry names the module it is relative
    to and the dereference chain that reaches it. `constant` marks literals (strides,
    caps) that are never dereferenced.
    """

    name: str
    offset: int
    module: str = "League of Legends.exe"
    #: Pointer steps applied before `offset` is added. Empty = offset is used
    #: directly against the module base.
    chain: tuple[int, ...] = ()
    note: str = ""
    verified: bool = False
    constant: bool = False

    @property
    def label(self) -> str:
        if self.constant:
            return f"constant {self.offset:#x}"

        return f"{self.module}+{self.offset:#x}" + (
            f" (chain {len(self.chain)})" if self.chain else ""
        )


# --------------------------------------------------------------------------- #
# globals — module-relative, each holds a pointer or a value
# --------------------------------------------------------------------------- #

OFFSETS: dict[str, Offset] = {
    # -- root objects ------------------------------------------------------- #
    "GameClient": Offset(
        "GameClient", 0x1F0DFF8,
        note="root of the client graph", verified=True,
    ),
    "LocalPlayer": Offset(
        "LocalPlayer", 0x1F304B0,
        note="pointer to the local hero; null before the match starts", verified=True,
    ),
    "ObjectManager": Offset(
        "ObjectManager", 0x1EF0D28,
        note="object manager; holds the live object array", verified=True,
    ),
    "HeroManager": Offset(
        "HeroManager", 0x1EF0E48,
        note="hero-only manager", verified=True,
    ),
    "EntityList": Offset(
        "EntityList", 0x1EF0ED0,
        note="entity list head", verified=True,
    ),
    "GameTime": Offset("GameTime", 0x1F00698, note="match clock", verified=True),
    "UnderMouseObj": Offset("UnderMouseObj", 0x1EF44B8, note="object under the cursor", verified=True),

    # -- object array layout (relative to ObjectManager) -------------------- #
    "ObjArrayBegin": Offset("ObjArrayBegin", 0x18, constant=False, note="array start (pointer)", verified=True),
    "ObjArrayEnd": Offset("ObjArrayEnd", 0x20, constant=False, note="array end (pointer)", verified=True),
    "ObjArrayStride": Offset("ObjArrayStride", 0x8, constant=True, note="pointer size", verified=True),
    "ObjArrayCap": Offset("ObjArrayCap", 0x28, constant=False, note="array capacity", verified=True),

    # -- GameObject --------------------------------------------------------- #
    "HeroTeam": Offset("HeroTeam", 0x239, note="team id: 100 blue, 200 red", verified=True),
    "HeroPosition": Offset("HeroPosition", 0x23C, note="world position, 3 floats x/y/z", verified=True),
    "HeroVisible": Offset("HeroVisible", 0x2E8, note="visibility flag", verified=True),
    "HeroDead": Offset("HeroDead", 0x231, note="dead flag", verified=True),
    "HeroName": Offset("HeroName", 0x4378, note="champion name string", verified=True),
    "HeroNetworkId": Offset("HeroNetworkId", 0xCC, note="network id", verified=True),
    "HeroIndex": Offset("HeroIndex", 0x20, note="index", verified=True),

    # -- stats -------------------------------------------------------------- #
    "HeroHealth": Offset("HeroHealth", 0x1080, note="current health", verified=True),
    "HeroMaxHealth": Offset("HeroMaxHealth", 0x10D0, note="max health", verified=True),
    "HeroMana": Offset("HeroMana", 0x360, note="current mana", verified=True),
    "HeroMaxMana": Offset("HeroMaxMana", 0x48, note="max mana", verified=True),
    "HeroLevel": Offset("HeroLevel", 0x4D58, note="level", verified=True),
    "HeroArmor": Offset("HeroArmor", 0x2E8, note="armor (inside stats block)", verified=False),
    "HeroMoveSpeed": Offset("HeroMoveSpeed", 0x500C, note="move speed", verified=False),

    # -- camera ------------------------------------------------------------- #
    "CameraPosition": Offset("CameraPosition", 0x8, chain=(0x1FBA090, 0x0),
                            note="camera position, 3 floats (via ViewMatrixInstance)", verified=False),
    "CameraFov": Offset("CameraFov", 0x3C, chain=(0x1FBA090, 0x0), note="field of view", verified=False),

    # -- render ------------------------------------------------------------- #
    "ViewMatrix": Offset("ViewMatrix", 0x0, chain=(0x1FBA090,), note="view matrix instance", verified=False),
    "SwapChain": Offset("SwapChain", 0x1F919D0, note="D3D swap chain", verified=True),

    # -- caps --------------------------------------------------------------- #
    "MaxObjects": Offset("MaxObjects", 0x400, constant=True, note="sanity cap on array walk", verified=True),
}

#: RVA of GameObject__IsType, usable to filter entries by type instead of by team.
IS_TYPE_RVA = 0x293FD0


def get(name: str) -> Offset:
    try:
        return OFFSETS[name]
    except KeyError:
        raise OffsetsError(
            f"unknown offset {name!r}; known: {', '.join(sorted(OFFSETS))}"
        ) from None


def unverified() -> list[str]:
    return sorted(name for name, entry in OFFSETS.items() if not entry.verified)


def check_patch(client_patch: Optional[str], allow_stale: bool = False) -> None:
    """Guard against running a stale table against a patched client.

    A patch mismatch is the most common cause of "the offsets are wrong" — usually
    the offsets are fine and the client moved.
    """

    if client_patch is None:
        if allow_stale:
            return
        raise StaleOffsets(
            f"client patch unknown; table is for {PATCH} ({CLIENT}). "
            "Pass allow_stale=True to run anyway."
        )

    if client_patch != PATCH:
        if allow_stale:
            return
        raise StaleOffsets(
            f"client is {client_patch}, table is for {PATCH} ({CLIENT}, scraped {SCRAPED}). "
            f"Re-distil from {SOURCE} or pass allow_stale=True."
        )


def summary() -> str:
    lines = [
        f"patch {PATCH} / client {CLIENT} (scraped {SCRAPED})",
        f"{len(OFFSETS)} entries, {len(unverified())} unverified",
        f"source: {SOURCE}",
    ]

    for name in sorted(OFFSETS):
        entry = OFFSETS[name]
        lines.append(f"  {'ok  ' if entry.verified else 'UNV '}{name:<16} {entry.label}")

    return "\n".join(lines)
