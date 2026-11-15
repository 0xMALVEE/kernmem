"""
Valorant reader - walks the live actor array through the driver.

Nothing here calls ReadProcessMemory. Every read goes through sdk/vgb.py to the
driver, which copies kernel-side against the EPROCESS, so Vanguard's handle
stripping never applies.

Two things this build forced:

  * The actor array is a module static (offsets.ActorArraySlot). The world chain
    is broken here - GWorld reads 0 and the level's own array reads empty.
  * Positions are three doubles at root + 0x170 (UE5 large-world coordinates).
    The 0x1B4 slot is the same bytes read as floats and is only a fallback.

The camera is unresolved, so `camera()` returns None and the ESP stays a radar.
`centre()` therefore falls back to the centroid of the actors when the pawn we
control is not in the array.

    from reader import Game

    with Game() as game:
        for actor in game.actors():
            print(actor.name, actor.position)
"""

from __future__ import annotations

import ctypes
import ctypes.wintypes as wt
import math
import struct
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Iterator, Optional

sys.path.insert(0, str(Path(__file__).resolve().parent.parent.parent / "sdk"))

import vgb  # noqa: E402

import offsets  # noqa: E402

PROCESS_QUERY_LIMITED_INFORMATION = 0x1000

SCAN_START = 0x9000000
SCAN_END = 0xE200000
SCAN_CHUNK = 0x1000

LO = 0x1000000000
HI = 0x7FFFFFFFFFFF


_k32 = ctypes.windll.kernel32
_k32.OpenProcess.restype = wt.HANDLE
_k32.QueryFullProcessImageNameW.argtypes = [
    wt.HANDLE, wt.DWORD, wt.LPWSTR, ctypes.POINTER(wt.DWORD)
]


class _VersionInfo(ctypes.Structure):
    _fields_ = [
        ("signature", wt.DWORD), ("struc_version", wt.DWORD),
        ("version_ms", wt.DWORD), ("version_ls", wt.DWORD),
    ]


def _image_path(pid: int) -> Optional[str]:
    handle = _k32.OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, False, pid)
    if not handle:
        return None
    try:
        size = wt.DWORD(1024)
        buffer = ctypes.create_unicode_buffer(size.value)
        if not _k32.QueryFullProcessImageNameW(handle, 0, buffer, ctypes.byref(size)):
            return None
        return buffer.value
    finally:
        _k32.CloseHandle(handle)


def client_patch(pid: int) -> Optional[str]:
    """File version of the running image, reduced to major.minor."""

    path = _image_path(pid)
    if not path:
        return None

    version = ctypes.windll.version
    size = version.GetFileVersionInfoSizeW(path, None)
    if not size:
        return None

    block = ctypes.create_string_buffer(size)
    if not version.GetFileVersionInfoW(path, 0, size, block):
        return None

    pointer = ctypes.c_void_p()
    length = wt.UINT()
    if not version.VerQueryValueW(block, "\\", ctypes.byref(pointer), ctypes.byref(length)):
        return None

    info = ctypes.cast(pointer, ctypes.POINTER(_VersionInfo)).contents
    major, minor = info.version_ms >> 16, info.version_ms & 0xFFFF
    return f"{major}.{minor}" if major else None


def is_pointer(value: int) -> bool:
    return LO <= value <= HI and value % 8 == 0


def sane(position) -> bool:
    """Map positions read as (x, y, 0.0), so z may be zero.

    Some actors store the triple as doubles, which read as enormous floats when
    parsed the other way, so large finite triples are accepted too.
    """

    if any(math.isnan(v) or math.isinf(v) for v in position):
        return False
    if sum(1 for v in position if abs(v) > 100.0) < 2:
        return False
    if all(abs(v) <= 300000.0 for v in position):
        return True
    return all(abs(v) <= 1e30 for v in position)


@dataclass
class Actor:
    address: int
    position: tuple
    health: float
    max_health: float
    team: int
    name: str

    @property
    def height(self) -> float:
        return offsets.at("ActorHeight")


class Game:
    """Driver-backed handle on a running match."""

    def __init__(self, allow_stale: bool = False):
        self._client: Optional[vgb.Client] = None
        self._pid = 0
        self._base = 0
        self._allow_stale = allow_stale
        self._local = 0
        self.patch: Optional[str] = None

    def __enter__(self) -> "Game":
        self.open()
        return self

    def __exit__(self, *_exc) -> None:
        self.close()

    def open(self) -> None:
        client = vgb.Client()
        client.open()
        self._client = client

        try:
            client.require_version()
        except vgb.VgbError:
            self.close()
            raise

        pid = client.pid_by_name(offsets.GAME_IMAGE)
        if not pid:
            self.close()
            raise offsets.OffsetsError(
                f"{offsets.GAME_IMAGE} is not running - start a match first"
            )

        self._pid = pid
        self.patch = client_patch(pid)
        offsets.check_patch(self.patch, self._allow_stale)

        base = client.main_base(pid)
        if not base:
            self.close()
            raise offsets.OffsetsError("could not resolve the image base")

        self._base = base

    def close(self) -> None:
        if self._client is not None:
            self._client.close()
            self._client = None

    @property
    def pid(self) -> int:
        return self._pid

    @property
    def base(self) -> int:
        return self._base

    @property
    def client(self) -> vgb.Client:
        if self._client is None:
            raise offsets.OffsetsError("reader is closed")
        return self._client

    def read(self, address: int, size: int) -> bytes:
        return self.client.read(self._pid, address, size)

    def u64(self, address: int) -> int:
        return struct.unpack("<Q", self.read(address, 8))[0]

    def i32(self, address: int) -> int:
        return struct.unpack("<i", self.read(address, 4))[0]

    def f32(self, address: int) -> float:
        return struct.unpack("<f", self.read(address, 4))[0]

    def vec3(self, address: int) -> tuple:
        return struct.unpack("<3f", self.read(address, 12))

    def vec3d(self, address: int) -> tuple:
        return struct.unpack("<3d", self.read(address, 24))

    def fstring(self, address: int, limit: int = 32) -> str:
        try:
            length = self.i32(address)
            if not length or abs(length) > 200:
                return ""
            pointer = self.u64(address + 8)
            if not is_pointer(pointer):
                return ""
            raw = self.read(pointer, min(abs(length), limit) * 2)
            return raw.decode("utf-16-le", "replace").split("\x00", 1)[0]
        except (vgb.VgbError, struct.error):
            return ""

    def actors(self) -> Iterator[Actor]:
        """Yield every actor that resolves to a plausible map position."""

        try:
            array = self.base + offsets.at("ActorArraySlot")
            data = self.u64(array)
            count = self.i32(array + offsets.at("ArrayCount"))
        except (vgb.VgbError, struct.error):
            return

        if not is_pointer(data) or count <= 0:
            return

        seen = set()

        for index in range(min(count, offsets.at("MaxActors"))):
            try:
                address = self.u64(data + index * offsets.at("PtrStride"))
            except vgb.VgbError:
                return

            if not is_pointer(address) or address in seen:
                continue
            seen.add(address)

            try:
                actor = self._actor(address)
            except (vgb.VgbError, struct.error):
                continue

            if actor is not None:
                yield actor

    def _actor(self, address: int) -> Optional[Actor]:
        root = self.u64(address + offsets.at("RootComponent"))
        if not is_pointer(root):
            return None

        # Widths and offsets both vary per actor: a double read as floats comes
        # back enormous, so every combination is tried and the smallest-magnitude
        # sane triple wins.
        position = None
        best = None

        for offset in (
            offsets.at("RelativeLocation"),
            offsets.at("RelativeLocationAlt"),
        ):
            for unpacker in (self.vec3, self.vec3d):
                try:
                    candidate = unpacker(root + offset)
                except (vgb.VgbError, struct.error):
                    continue
                if not sane(candidate):
                    continue
                magnitude = max(abs(v) for v in candidate)
                if best is None or magnitude < best:
                    best = magnitude
                    position = candidate

        if position is None:
            return None

        handler = self.u64(address + offsets.at("DamageHandler"))
        health = max_health = 0.0
        if is_pointer(handler):
            try:
                health = self.f32(handler + offsets.at("Health"))
                max_health = self.f32(handler + offsets.at("MaxHealth"))
            except (vgb.VgbError, struct.error):
                health = max_health = 0.0

        component = self.u64(address + offsets.at("TeamComponent"))
        team = 0
        if is_pointer(component):
            try:
                team = self.i32(component + offsets.at("TeamId"))
            except (vgb.VgbError, struct.error):
                team = 0

        state = self.u64(address + offsets.at("PlayerState"))
        name = self.fstring(state + offsets.at("PlayerName")) if is_pointer(state) else ""

        return Actor(
            address=address,
            position=position,
            health=health,
            max_health=max_health,
            team=team,
            name=name,
        )

    def local(self) -> Optional[Actor]:
        """The actor we control, or None.

        Found by scanning the module for a controller whose AcknowledgedPawn is a
        known actor. Cached until it stops resolving.
        """

        actors = {actor.address: actor for actor in self.actors()}
        if self._local in actors:
            return actors[self._local]

        pawn = self._find_local(set(actors))
        if not pawn:
            return None

        self._local = pawn
        if pawn in actors:
            return actors[pawn]

        try:
            return self._actor(pawn)
        except (vgb.VgbError, struct.error):
            return None

    def _find_local(self, actors: dict) -> int:
        for delta in range(SCAN_START, SCAN_END, SCAN_CHUNK):
            try:
                raw = self.read(self._base + delta, SCAN_CHUNK)
            except vgb.VgbError:
                continue

            for index in range(0, SCAN_CHUNK, 8):
                candidate = struct.unpack_from("<Q", raw, index)[0]
                if not is_pointer(candidate):
                    continue

                try:
                    pawn = self.u64(candidate + offsets.at("AcknowledgedPawn"))
                except vgb.VgbError:
                    continue

                if not is_pointer(pawn):
                    continue

                # Only an actor from the array counts. Image pointers also answer
                # 0x518 with plausible-looking values but are not the pawn, so
                # accepting them puts the radar origin kilometres away.
                if pawn in actors:
                    return pawn

        return 0

    def centre(self):
        """Radar origin: (position, local address).

        Uses the local pawn when its position reads cleanly, otherwise the
        centroid of the actors, so the radar always has something to draw
        around. Returns (None, 0) when there is nothing to draw at all.
        """

        local = self.local()
        if local is not None and sane(local.position):
            if max(abs(v) for v in local.position) < 300000.0:
                return local.position, local.address

        actors = list(self.actors())
        if not actors:
            return None, 0

        count = len(actors)
        return (
            (
                sum(a.position[0] for a in actors) / count,
                sum(a.position[1] for a in actors) / count,
                0.0,
            ),
            0,
        )

    def camera(self):
        """(location, rotation, fov) or None. Unresolved on build 5.3."""

        return None
