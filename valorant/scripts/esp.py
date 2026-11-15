"""
Valorant ESP - draws boxes over players through a click-through overlay.

Nothing is injected. The window is a transparent, click-through canvas pinned
to the game client rect, and every number comes from the driver via reader.py.

    python esp.py               # enemies only
    python esp.py --all         # teammates too
    python esp.py --fps 30 --max-distance 150

Run it once the driver is loaded and you are in a match. F9 pauses, END quits.
The game must be windowed or borderless; exclusive fullscreen has no composable
window to sit on top of.
"""

from __future__ import annotations

import argparse
import ctypes
import ctypes.wintypes as wt
import math
import sys
import tkinter as tk
from pathlib import Path
from typing import Optional

sys.path.insert(0, str(Path(__file__).resolve().parent))

import offsets  # noqa: E402
import reader  # noqa: E402

GWL_EXSTYLE = -20
WS_EX_LAYERED = 0x00080000
WS_EX_TRANSPARENT = 0x00000020
WS_EX_NOACTIVATE = 0x08000000
WS_EX_TOOLWINDOW = 0x00000080

TRANSPARENT_KEY = "#010203"

ALLY = "#4da3ff"
ENEMY = "#ff4d4d"

_user32 = ctypes.windll.user32
_user32.GetWindowThreadProcessId.argtypes = [wt.HWND, ctypes.POINTER(wt.DWORD)]
_user32.EnumWindows.argtypes = [ctypes.c_void_p, wt.LPARAM]
_user32.ClientToScreen.argtypes = [wt.HWND, ctypes.POINTER(wt.POINT)]
_user32.GetClientRect.argtypes = [wt.HWND, ctypes.POINTER(wt.RECT)]

_ENUMPROC = ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)


# --------------------------------------------------------------------------- #
# window plumbing
# --------------------------------------------------------------------------- #


def client_rect(pid: int) -> Optional[tuple[int, int, int, int]]:
    """(x, y, width, height) of the first visible top-level window of `pid`.

    Client rect plus a screen-space origin, so the overlay covers exactly the
    render area rather than the title bar.
    """

    found: list[int] = []

    def visit(hwnd, _lparam):
        owner = wt.DWORD()
        _user32.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
        if owner.value == pid and _user32.IsWindowVisible(hwnd):
            found.append(hwnd)
        return True

    _user32.EnumWindows(_ENUMPROC(visit), 0)
    if not found:
        return None

    hwnd = found[0]
    rect = wt.RECT()
    if not _user32.GetClientRect(hwnd, ctypes.byref(rect)):
        return None

    origin = wt.POINT(0, 0)
    if not _user32.ClientToScreen(hwnd, ctypes.byref(origin)):
        return None

    width, height = rect.right, rect.bottom
    if width < 64 or height < 64:
        return None

    return origin.x, origin.y, width, height


def make_click_through(window: tk.Tk) -> None:
    """Let the mouse pass through the overlay and keep it out of the taskbar."""

    hwnd = wt.HWND(int(window.wm_frame(), 16))
    style = _user32.GetWindowLongW(hwnd, GWL_EXSTYLE)
    _user32.SetWindowLongW(
        hwnd,
        GWL_EXSTYLE,
        style | WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
    )


# --------------------------------------------------------------------------- #
# drawing
# --------------------------------------------------------------------------- #


def health_colour(fraction: float) -> str:
    fraction = max(0.0, min(1.0, fraction))
    red = int(255 * (1.0 - fraction))
    green = int(255 * fraction)
    return f"#{red:02x}{green:02x}40"


class Esp:
    def __init__(self, options: argparse.Namespace):
        self.options = options
        self.game: Optional[reader.Game] = None
        self.local_team = 0
        self.paused = False
        self.drawn = 0
        self.error = ""

        self.root = tk.Tk()
        self.root.overrideredirect(True)
        self.root.attributes("-topmost", True)
        self.root.attributes("-transparentcolor", TRANSPARENT_KEY)
        self.root.configure(bg=TRANSPARENT_KEY)

        self.canvas = tk.Canvas(
            self.root,
            bg=TRANSPARENT_KEY,
            highlightthickness=0,
            bd=0,
        )
        self.canvas.pack(fill="both", expand=True)

        self.root.update_idletasks()
        make_click_through(self.root)

        self.root.bind_all("<F9>", self.toggle)
        self.root.bind_all("<End>", lambda _event: self.root.destroy())

    # -- lifecycle ---------------------------------------------------------- #

    def toggle(self, _event=None) -> None:
        self.paused = not self.paused
        if self.paused:
            self.canvas.delete("all")
            self.title("paused")

    def title(self, text: str) -> None:
        self.canvas.create_text(
            12, 12, anchor="nw", text=text, fill="#e0e0e0",
            font=("Consolas", 10),
        )

    def connect(self) -> bool:
        if self.game is not None:
            return True

        try:
            game = reader.Game(allow_stale=self.options.allow_stale)
            game.open()
        except Exception as error:  # noqa: BLE001 - surfaced verbatim in the hud
            self.error = str(error)
            return False

        self.game = game
        pawn = game.local_pawn()
        self.local_team = self.team_of(pawn)
        return True

    def team_of(self, pawn: int) -> int:
        if not self.game or not pawn:
            return 0

        try:
            component = self.game.read_u64(pawn + offsets.get("TeamComponent").offset)
            if not component:
                return 0
            return self.game.read_i32(component + offsets.get("TeamId").offset)
        except Exception:  # noqa: BLE001
            return 0

    # -- frame -------------------------------------------------------------- #

    def frame(self) -> None:
        if not self.paused:
            self.render()

        self.root.after(max(1, 1000 // self.options.fps), self.frame)

    def render(self) -> None:
        self.canvas.delete("all")

        if not self.connect():
            self.title(f"driver/game not ready: {self.error}")
            return

        rect = client_rect(self.game.pid)
        if rect is None:
            self.title("no game window - use windowed or borderless")
            return

        x, y, width, height = rect
        self.root.geometry(f"{width}x{height}+{x}+{y}")

        camera = self.game.camera()
        if camera is None:
            self.title("camera not resolved - check CameraCachePriv offset")
            return

        location, rotation, fov = camera
        local_pawn = self.game.local_pawn()

        count = 0
        for player in self.game.players():
            if player.address == local_pawn:
                continue
            if not player.alive or player.health <= 0:
                continue
            if not self.options.all and self.local_team and player.team == self.local_team:
                continue

            distance = math.dist(location, player.position) / 100.0
            if distance > self.options.max_distance:
                continue

            half = player.height
            feet = reader.world_to_screen(
                location, rotation, fov, player.position, width, height
            )
            head = reader.world_to_screen(
                location, rotation, fov,
                (player.position[0], player.position[1], player.position[2] + half * 2),
                width, height,
            )

            if feet is None or head is None:
                continue

            self.draw(player, feet, head, distance)
            count += 1

        self.drawn = count
        self.title(f"{count} drawn  {self.game.patch or 'unknown build'}")

    def draw(self, player, feet, head, distance) -> None:
        left, right = feet[0], head[0]
        top, bottom = head[1], feet[1]

        box_height = bottom - top
        box_width = max(8.0, box_height * 0.42)

        x1 = min(left, right) - box_width / 2
        x2 = max(left, right) + box_width / 2

        colour = ALLY if self.local_team and player.team == self.local_team else ENEMY

        self.canvas.create_rectangle(x1, top, x2, bottom, outline=colour, width=2)

        fraction = player.health / player.max_health if player.max_health else 0.0
        bar = box_height * max(0.0, min(1.0, fraction))
        self.canvas.create_rectangle(
            x1 - 5, bottom - bar, x1 - 2, bottom,
            fill=health_colour(fraction), outline="",
        )

        self.canvas.create_text(
            (x1 + x2) / 2, top - 10, text=f"{player.name} {distance:.0f}m",
            fill=colour, font=("Consolas", 9),
        )

        self.canvas.create_text(
            (x1 + x2) / 2, bottom + 8, text=f"{player.health:.0f}",
            fill=colour, font=("Consolas", 8),
        )


# --------------------------------------------------------------------------- #
# entry
# --------------------------------------------------------------------------- #


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description="Valorant ESP over the driver")
    parser.add_argument("--all", action="store_true", help="include teammates")
    parser.add_argument("--fps", type=int, default=25, help="redraw rate (default 25)")
    parser.add_argument(
        "--max-distance", type=float, default=200.0,
        help="metres beyond which players are skipped (default 200)",
    )
    parser.add_argument(
        "--allow-stale", action="store_true",
        help="run even when the build does not match the offset stamp",
    )

    options = parser.parse_args(argv)

    esp = Esp(options)
    esp.root.after(0, esp.frame)
    esp.root.mainloop()

    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
