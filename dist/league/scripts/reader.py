"""
Game reader â€” resolves the client and walks offset chains through the driver.

The important part is that nothing here calls ReadProcessMemory. Every read goes
through sdk/km.py to the driver, which copies kernel-side against the EPROCESS.
That is why Vanguard's handle stripping does not matter to a script.

    from reader import Game

    with Game() as game:
        for hero in game.heroes():
            print(hero.name, hero.health)
"""

from __future__ import annotations

import ctypes
import ctypes.wintypes as wt
import struct
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Iterator, Optional

sys.path.insert(0, str(Path(__file__).resolve().parent.parent.parent / "sdk"))

import km  # noqa: E402  (path is set up above)

import offsets  # noqa: E402

GAME_IMAGE = "League of Legends.exe"

PROCESS_QUERY_LIMITED_INFORMATION = 0x1000


def client_patch(pid: int) -> Optional[str]:
    """File version of the running client, e.g. "14.23.1.1234" -> "14.23".

    Uses the Win32 version API rather than the file on disk, so it describes the
    process actually running. Returns None when it cannot be read.
    """

    kernel32 = ctypes.windll.kernel32
    version = ctypes.windll.version

    handle = kernel32.OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, False, pid)
    if not handle:
        return None

    try:
        size = version.GetFileVersionInfoSizeExW(0, None, None)
        # The Ex variant needs the path, so fall back to the process image path.
        buffer = ctypes.create_unicode_buffer(32768)
        length = wt.DWORD(len(buffer))
        if not kernel32.QueryFullProcessImageNameW(handle, 0, buffer, ctypes.byref(length)):
            return None

        path = buffer.value
        size = version.GetFileVersionInfoSizeW(path, None)
        if not size:
            return None

        block = ctypes.create_string_buffer(size)
        if not version.GetFileVersionInfoW(path, 0, size, block):
            return None

        value = ctypes.c_void_p()
        value_len = wt.UINT()
        if not version.VerQueryValueW(block, "\\", ctypes.byref(value), ctypes.byref(value_len)):
            return None

        fixed = ctypes.cast(value, ctypes.POINTER(wt.DWORD * 4)).contents
        return f"{fixed[2] & 0xFFFF}.{fixed[3] & 0xFFFF}"
    finally:
        kernel32.CloseHandle(handle)


@dataclass
class Hero:
    address: int
    name: str
    health: float
    max_health: float
    mana: float
    level: int
    team: int
    position: tuple[float, float, float]
    visible: bool

    @property
    def health_percent(self) -> float:
        return (self.health / self.max_health * 100.0) if self.max_health else 0.0

    @property
    def alive(self) -> bool:
        return self.health > 0.0


class Game:
    """A resolved, read-only view of the running client."""

    def __init__(self, allow_stale: bool = False):
        self._client: Optional[km.Client] = None
        self._pid = 0
        self._base = 0
        self._allow_stale = allow_stale
        self.patch: Optional[str] = None

    def __enter__(self) -> "Game":
        self.open()
        return self

    def __exit__(self, *_):
        self.close()

    # -- lifecycle ---------------------------------------------------------- #

    def open(self) -> None:
        client = km.Client()
        client.open()
        self._client = client

        try:
            client.require_version()
        except km.KmError:
            self.close()
            raise

        pid = client.pid_by_name(GAME_IMAGE)
        if not pid:
            self.close()
            raise offsets.OffsetsError(
                f"{GAME_IMAGE} is not running â€” it only exists during a match"
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
    def client(self) -> km.Client:
        if self._client is None:
            raise offsets.OffsetsError("reader is closed")
        return self._client

    def read(self, address: int, size: int) -> bytes:
        return self.client.read(self._pid, address, size)

    def read_u64(self, address: int) -> int:
        return struct.unpack("<Q", self.read(address, 8))[0]

    def read_i32(self, address: int) -> int:
        return struct.unpack("<i", self.read(address, 4))[0]

    def read_f32(self, address: int) -> float:
        return struct.unpack("<f", self.read(address, 4))[0]

    def read_floats(self, address: int, count: int) -> tuple[float, ...]:
        return struct.unpack(f"<{count}f", self.read(address, 4 * count))

    def read_cstring(self, address: int, limit: int = 64) -> str:
        raw = self.read(address, limit)
        return raw.split(b"\x00", 1)[0].decode("utf-8", "replace")

    def read_wstring(self, address: int, limit: int = 64) -> str:
        """UTF-16 string, as the game stores names.

        A half-read tail can split a code unit, so decoding is told to replace
        rather than raise.
        """

        raw = self.read(address, limit * 2)
        text = raw.decode("utf-16-le", "replace")
        return text.split("\x00", 1)[0]

    # -- chains ------------------------------------------------------------- #

    def walk(self, entry: offsets.Offset, base: Optional[int] = None) -> int:
        """Resolve a chain to a final address.

        A zero pointer anywhere in the chain yields 0 rather than a bogus address, so
        callers can treat 0 as "not available yet" instead of faulting.
        """

        if base is None:
            if entry.module != GAME_IMAGE:
                module = self.client.module_base(self._pid, entry.module)
                if not module:
                    return 0
                base = module
            else:
                base = self._base

        address = base

        for step in entry.chain:
            address = self.read_u64(address + step)
            if not address:
                return 0

        return address + entry.offset if address else 0

    def value_u64(self, name: str) -> int:
        address = self.walk(offsets.get(name))
        return self.read_u64(address) if address else 0

    def value_i32(self, name: str) -> int:
        address = self.walk(offsets.get(name))
        return self.read_i32(address) if address else 0

    def value_f32(self, name: str) -> float:
        address = self.walk(offsets.get(name))
        return self.read_f32(address) if address else 0.0

    # -- game objects ------------------------------------------------------- #

    def local_player(self) -> int:
        return self.value_u64("LocalPlayer")

    def object_array(self) -> tuple[int, int]:
        """Begin/end pointers of the live object array.

        The manager keeps the array as a begin/end pair rather than a base and a
        count, so the length is derived from the distance. Both are validated before
        use â€” a half-built array (during loading) gives end <= begin.
        """

        manager = self.value_u64("ObjectManager")
        if not manager:
            return (0, 0)

        try:
            begin = self.read_u64(manager + offsets.get("ObjArrayBegin").offset)
            end = self.read_u64(manager + offsets.get("ObjArrayEnd").offset)
        except km.KmError:
            return (0, 0)

        if not begin or not end or end <= begin:
            return (0, 0)

        return (begin, end)

    def object_count(self) -> int:
        begin, end = self.object_array()
        if not begin:
            return 0

        stride = offsets.get("ObjArrayStride").offset
        return (end - begin) // stride if stride else 0

    def objects(self) -> Iterator[int]:
        """Yield every object pointer. Stops on the first unreadable slot."""

        begin, end = self.object_array()
        if not begin:
            return

        stride = offsets.get("ObjArrayStride").offset
        cap = offsets.get("MaxObjects").offset

        index = 0
        while index < cap and begin + index * stride < end:
            try:
                pointer = self.read_u64(begin + index * stride)
            except km.KmError:
                return

            index += 1

            if pointer:
                yield pointer

    def heroes(self) -> Iterator[Hero]:
        """Yield objects that look like heroes.

        There is no hero-only list worth trusting, so entries are filtered by team
        id: 100 or 200 is the cheapest shape test that avoids an IsType call per
        object. Non-hero objects fail it and are skipped.
        """

        team_offset = offsets.get("HeroTeam").offset

        for pointer in self.objects():
            try:
                team = self.read_i32(pointer + team_offset)
            except km.KmError:
                continue

            if team not in (100, 200):
                continue

            try:
                yield self._hero(pointer)
            except km.KmError:
                continue

    def _hero(self, address: int) -> Hero:
        def field(name: str) -> int:
            return address + offsets.get(name).offset

        return Hero(
            address=address,
            name=self.read_wstring(field("HeroName")),
            health=self.read_f32(field("HeroHealth")),
            max_health=self.read_f32(field("HeroMaxHealth")),
            mana=self.read_f32(field("HeroMana")),
            level=self.read_i32(field("HeroLevel")),
            team=self.read_i32(field("HeroTeam")),
            position=self.read_floats(field("HeroPosition"), 3),
            visible=self.read_i32(field("HeroVisible")) != 0,
        )
