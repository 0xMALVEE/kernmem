"""
Valorant reader - resolves the game and walks UE5 structures through the driver.

Nothing here calls ReadProcessMemory. Every read goes through sdk/vgb.py to the
driver, which copies kernel-side against the EPROCESS, so Vanguard's handle
stripping never applies.

UE5 specifics this module owns:

  * FVector and FRotator are doubles. `vec3` and `rotator` read 24 bytes each.
  * UWorld is encrypted. `uworld()` runs the archived Decrypt_UWorld port over
    the seven-slot key state and returns the plain pointer.
  * FString is 16 bytes: int32 length, then a character pointer.

    from reader import Game

    with Game() as game:
        for player in game.players():
            print(player.name, player.health, player.position)
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

import vgb  # noqa: E402  (path is set up above)

import offsets  # noqa: E402

PROCESS_QUERY_LIMITED_INFORMATION = 0x1000

_MASK = (1 << 64) - 1


# --------------------------------------------------------------------------- #
# UWorld decryption
# --------------------------------------------------------------------------- #


def _lfsr1(x: int) -> int:
    return (x >> 1) ^ (((x >> 1) ^ (2 * x)) & 0xAAAAAAAAAAAAAAAA)


def _bitrev_ror(x: int) -> int:
    a = _lfsr1(x)
    b = (a >> 2) ^ (((a >> 2) ^ (4 * a)) & 0xCCCCCCCCCCCCCCCC)
    c = (b >> 4) ^ (((b >> 4) ^ (16 * b)) & 0xF0F0F0F0F0F0F0F0)
    d = (c >> 8) ^ (((c >> 8) ^ (c << 8)) & 0xFF00FF00FF00FF)
    return _ror(d, 32)


def _ror(value: int, bits: int) -> int:
    bits %= 64
    return ((value >> bits) | (value << (64 - bits))) & _MASK


def _rol(value: int, bits: int) -> int:
    bits %= 64
    return ((value << bits) | (value >> (64 - bits))) & _MASK


def decrypt_u_world(key: int, state: list[int]) -> int:
    """Port of the archived Decrypt_UWorld.

    The index comes from a mixed hash of the key modulo seven; each index takes
    a different bit-mangling path. Returns 0 when the state is unusable.
    """

    k32 = key & 0xFFFFFFFF

    mixed = (k32 ^ ((key << 25) & 0xFFFFFFFF) ^ ((k32 ^ (k32 >> 15)) >> 12)) & 0xFFFFFFFF
    h = (2685821657736338717 * mixed) & _MASK

    index = h % 7
    hi = (h >> 32) & 0xFFFFFFFF

    if index >= len(state):
        return 0

    value = state[index]

    if index % 7 == 0:
        value = _bitrev_ror((value - (hi - 1)) & _MASK)
    elif index % 7 == 1:
        value = _rol((value - (2 * index + hi)) & _MASK, (hi + index) % 0x3F + 1)

    base = 2 * index + hi

    if index % 7 == 2:
        value = (~(value - base)) & _MASK
    elif index % 7 == 3:
        value = (2 * value) ^ (((2 * value) ^ (value >> 1)) & 0x5555555555555555)
        value &= _MASK
    elif index % 7 == 4:
        rotated = _ror(value, base % 0x3F + 1)
        value = (2 * rotated) ^ (((2 * rotated) ^ (rotated >> 1)) & 0x5555555555555555)
        value &= _MASK
    elif index % 7 == 5:
        rotated = _ror(value, base % 0x3F + 1)
        value = _bitrev_ror(rotated)
    elif index % 7 == 6:
        value = ((~value) - (hi + index)) & _MASK

    return (value ^ k32) & _MASK


# --------------------------------------------------------------------------- #
# process / build
# --------------------------------------------------------------------------- #

_k32 = ctypes.windll.kernel32
_k32.OpenProcess.restype = wt.HANDLE
_k32.QueryFullProcessImageNameW.argtypes = [
    wt.HANDLE, wt.DWORD, wt.LPWSTR, ctypes.POINTER(wt.DWORD)
]


class _VersionInfo(ctypes.Structure):
    _fields_ = [("signature", wt.DWORD), ("struc_version", wt.DWORD)]


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
    """File version of the running build, reduced to major.minor.

    Read from the on-disk image of the live process, so it describes the build
    actually running rather than whatever was last installed.
    """

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
    major, minor = info.signature, info.struc_version

    return f"{major}.{minor}" if major else None


# --------------------------------------------------------------------------- #
# records
# --------------------------------------------------------------------------- #


@dataclass
class Player:
    address: int
    name: str
    health: float
    max_health: float
    alive: bool
    team: int
    position: tuple[float, float, float]

    @property
    def height(self) -> float:
        return offsets.get("CapsuleHalfHeight").offset


# --------------------------------------------------------------------------- #
# reader
# --------------------------------------------------------------------------- #


class Game:
    """Driver-backed handle on a running match."""

    def __init__(self, allow_stale: bool = False):
        self._client: Optional[vgb.Client] = None
        self._pid = 0
        self._base = 0
        self._allow_stale = allow_stale
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

    # -- raw access --------------------------------------------------------- #

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

    def read_u8(self, address: int) -> int:
        return self.read(address, 1)[0]

    def read_u64(self, address: int) -> int:
        return struct.unpack("<Q", self.read(address, 8))[0]

    def read_i32(self, address: int) -> int:
        return struct.unpack("<i", self.read(address, 4))[0]

    def read_f32(self, address: int) -> float:
        return struct.unpack("<f", self.read(address, 4))[0]

    def read_f64(self, address: int) -> float:
        return struct.unpack("<d", self.read(address, 8))[0]

    def vec3(self, address: int) -> tuple[float, float, float]:
        return struct.unpack("<3d", self.read(address, 24))

    def rotator(self, address: int) -> tuple[float, float, float]:
        """Pitch, yaw, roll in degrees."""

        return struct.unpack("<3d", self.read(address, 24))

    def fstring(self, address: int, limit: int = 48) -> str:
        """UE FString: int32 length, then a character pointer.

        Negative length means UTF-16. A zero pointer or a silly length yields an
        empty string rather than an exception.
        """

        try:
            length = self.read_i32(address)
            if length == 0:
                return ""

            wide = length < 0
            count = min(abs(length), limit)
            pointer = self.read_u64(address + 8)
            if not pointer:
                return ""

            raw = self.read(pointer, count * (2 if wide else 1))
            text = raw.decode("utf-16-le" if wide else "utf-8", "replace")
            return text.split("\x00", 1)[0]
        except (vgb.VgbError, struct.error):
            return ""

    # -- chains ------------------------------------------------------------- #

    def walk(self, name: str, base: int) -> int:
        """Resolve an entry's chain from `base`. A null hop yields 0."""

        entry = offsets.get(name)
        address = base

        for step in entry.chain:
            address = self.read_u64(address + step)
            if not address:
                return 0

        return address + entry.offset

    def uworld(self) -> int:
        """Decrypt the world pointer. 0 when the key state is not ready."""

        try:
            key = self.read_u64(self._base + offsets.get("GWorldKey").offset)
            if not key:
                return 0

            state_address = self._base + offsets.get("GWorldState").offset
            raw = self.read(state_address, 7 * 8)
            state = list(struct.unpack("<7Q", raw))
            if not state[0]:
                return 0

            return decrypt_u_world(key, state)
        except (vgb.VgbError, struct.error):
            return 0

    def local_pawn(self) -> int:
        """The pawn we control, or 0.

        Preferred path is the local player controller, which is authoritative.
        """

        world = self.uworld()
        if not world:
            return 0

        try:
            instance = self.walk("GameInstance", world)
            if not instance:
                return 0

            players = self.read_u64(instance + offsets.get("LocalPlayers").offset)
            if not players:
                return 0

            controller = self.read_u64(players + offsets.get("PlayerController").offset)
            if not controller:
                return 0

            return self.read_u64(controller + offsets.get("AcknowledgedPawn").offset)
        except (vgb.VgbError, struct.error):
            return 0

    def camera(self) -> Optional[tuple[tuple[float, float, float], tuple[float, float, float], float]]:
        """(location, rotation, vertical fov) from the camera cache, or None."""

        world = self.uworld()
        if not world:
            return None

        try:
            instance = self.walk("GameInstance", world)
            if not instance:
                return None

            players = self.read_u64(instance + offsets.get("LocalPlayers").offset)
            if not players:
                return None

            controller = self.read_u64(players + offsets.get("PlayerController").offset)
            if not controller:
                return None

            manager = self.read_u64(controller + offsets.get("CameraManager").offset)
            if not manager:
                return None

            cache = self.read_u64(manager + offsets.get("CameraCachePriv").offset)
            if not cache:
                return None

            pov = cache + offsets.get("PovOffset").offset

            location = self.vec3(pov + offsets.get("PovLocation").offset)
            rotation = self.rotator(pov + offsets.get("PovRotation").offset)
            fov = self.read_f32(pov + offsets.get("PovFov").offset)
        except (vgb.VgbError, struct.error):
            return None

        if not fov or not (1.0 < fov < 179.0):
            return None

        return location, rotation, fov

    # -- actors ------------------------------------------------------------- #

    def player_states(self) -> Iterator[int]:
        """Yield every APlayerState pointer in the game state array."""

        world = self.uworld()
        if not world:
            return

        try:
            game_state = self.read_u64(world + offsets.get("GameState").offset)
        except vgb.VgbError:
            return

        if not game_state:
            return

        array = game_state + offsets.get("PlayerArray").offset

        try:
            begin = self.read_u64(array)
            count = self.read_i32(array + offsets.get("ArrayCount").offset)
        except vgb.VgbError:
            return

        if not begin or count <= 0:
            return

        cap = offsets.get("MaxActors").offset
        stride = offsets.get("PtrStride").offset

        for index in range(min(count, cap)):
            try:
                pointer = self.read_u64(begin + index * stride)
            except vgb.VgbError:
                return

            if pointer:
                yield pointer

    def players(self) -> Iterator[Player]:
        """Yield every player with a pawn and a position.

        A PlayerState only carries the name; the pawn hangs off it and carries
        position, health and team. States without a pawn are skipped.
        """

        for state in self.player_states():
            try:
                pawn = self.read_u64(state + offsets.get("PawnPrivate").offset)
                if not pawn:
                    continue

                player = self._player(state, pawn)
            except (vgb.VgbError, struct.error):
                continue

            if player.position != (0.0, 0.0, 0.0):
                yield player

    def _player(self, state: int, pawn: int) -> Player:
        root = self.read_u64(pawn + offsets.get("RootComponent").offset)
        if not root:
            raise vgb.VgbError("pawn has no root component")

        position = self.vec3(root + offsets.get("RelativeLocation").offset)

        handler = self.read_u64(pawn + offsets.get("DamageHandler").offset)
        health = self.read_f32(handler + offsets.get("CachedLife").offset) if handler else 0.0
        max_health = (
            self.read_f32(handler + offsets.get("CachedMaxLife").offset) if handler else 0.0
        )
        alive = self.read_u8(handler + offsets.get("Alive").offset) != 0 if handler else False

        component = self.read_u64(pawn + offsets.get("TeamComponent").offset)
        team = 0
        if component:
            try:
                team = self.read_i32(component + offsets.get("TeamId").offset)
            except (vgb.VgbError, struct.error):
                team = 0

        name = self.fstring(state + offsets.get("PlayerName").offset)

        return Player(
            address=pawn,
            name=name or "player",
            health=health,
            max_health=max_health or 100.0,
            alive=alive,
            team=team,
            position=position,
        )


# --------------------------------------------------------------------------- #
# projection
# --------------------------------------------------------------------------- #


def world_to_screen(
    location: tuple[float, float, float],
    rotation: tuple[float, float, float],
    fov: float,
    point: tuple[float, float, float],
    width: int,
    height: int,
) -> Optional[tuple[float, float]]:
    """Project a world point to screen pixels.

    `rotation` is UE FRotator: pitch, yaw, roll, in degrees. `fov` is vertical.
    Returns None for anything behind the camera.
    """

    pitch, yaw, roll = (math.radians(angle) for angle in rotation)

    dx = point[0] - location[0]
    dy = point[1] - location[1]
    dz = point[2] - location[2]

    cy, sy = math.cos(yaw), math.sin(yaw)
    cp, sp = math.cos(pitch), math.sin(pitch)
    cr, sr = math.cos(roll), math.sin(roll)

    # world -> camera: undo yaw, then pitch, then roll
    x = dx * cy + dy * sy
    y = -dx * sy + dy * cy

    y, z = y * cp - dz * sp, y * sp + dz * cp
    x, y = x * cr - y * sr, x * sr + y * cr

    if y <= 0.5:
        return None

    aspect = width / height if height else 1.0
    tan_v = math.tan(math.radians(fov) / 2.0)
    tan_h = tan_v * aspect

    return (
        width / 2.0 + (x / y) * (width / 2.0) / tan_h,
        height / 2.0 - (z / y) * (height / 2.0) / tan_v,
    )
